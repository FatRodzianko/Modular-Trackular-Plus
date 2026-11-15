/* Modular-Trackular Plus: an example Master FX plugin. SPDX-License-Identifier: MIT
 *
 * Room: a small reverb for the reverb slot. Four combs in parallel, each with a low-pass in its
 * loop, then two all-passes on each side. It shows:
 *   - float blocks (PTMFX_FLAG_FLOAT): the send comes in as +-1.0, the return goes out in place;
 *   - the slot's memory: the combs and all-passes in area 0 (32 KB of fast RAM), the predelay in
 *     area 1 (32 KB of SDRAM), both cleared by the host before reset();
 *   - CE's four reverb knobs, read from the block every time, and one own knob, Width;
 *   - the return as CE's reverb gives it: send + (wet - send) * amount.
 *
 *     python masterfx-sdk/ptmfx_builder.py masterfx-sdk/examples/room.c -o Room.ptmfx
 */
#include "ptmfx.h"

#define COMBS 4
#define PASSES 2
#define PREDELAY_MAX 5001u                  /* CE's Predelay goes up to 5,000 samples */

/* Delay lengths in samples, all prime so their echoes don't line up */
static const uint32_t kComb[COMBS] = { 1031, 1153, 1289, 1399 };
static const uint32_t kPassL[PASSES] = { 347, 113 };
static const uint32_t kPassR[PASSES] = { 359, 127 };

typedef struct
{
    float *buf;
    uint32_t size, at;
} Line;

typedef struct
{
    Line comb[COMBS];
    float damp[COMBS];                      /* each comb's low-pass */
    Line passL[PASSES], passR[PASSES];
    Line pre;
    float width;                            /* 0 to 1 */
    uint32_t ready;                         /* the slot's memory is big enough */
} Room;

PTMFX_INFO = {
    0, 0, PTMFX_FLAG_FLOAT,                 /* magic and version (the builder's), flags */
    PTMFX_SLOT_REVERB, 1, {0, 0},           /* slot, own knobs, reserved */
    "Room", "MT+ examples",
    {"", "", "", ""},                       /* CE's Size, Damping, Predelay and Diffusion as they are */
    sizeof(Room),
    0, 0, 0, 0, 0, 0, 0,                    /* the code's place and the entries (the builder's) */
    {0, 0, 0},
    {
        PTMFX_KNOB("Width", 0, 100, 80, 1, "%", PTMFX_PARAM_INT),
    },
};

/* Lines carved out of an area, one after another. Returns the floats used, or 0 if it's too small. */
static uint32_t carve(Line *lines, const uint32_t *sizes, uint32_t n, float *base, uint32_t room, uint32_t used)
{
    for (uint32_t i = 0; i < n; i++)
    {
        if (used + sizes[i] > room)
        {
            return 0;
        }
        lines[i].buf = base + used;
        lines[i].size = sizes[i];
        lines[i].at = 0;
        used += sizes[i];
    }
    return used;
}

PTMFX_INIT(void *st, const PtmfxMemory *mem, const PtmfxHost *host)
{
    (void)host;
    Room *r = (Room *)st;                   /* zeroed by the host */
    /* Only the places here: the memory is cleared later, before reset() */
    float *fast = (float *)mem->area[0];
    const uint32_t fastFloats = mem->size[0] / sizeof(float);
    uint32_t used = carve(r->comb, kComb, COMBS, fast, fastFloats, 0);
    used = used ? carve(r->passL, kPassL, PASSES, fast, fastFloats, used) : 0;
    used = used ? carve(r->passR, kPassR, PASSES, fast, fastFloats, used) : 0;
    const uint32_t pre = PREDELAY_MAX;
    r->ready = used != 0 && carve(&r->pre, &pre, 1, (float *)mem->area[1], mem->size[1] / sizeof(float), 0) != 0;
    r->width = 0.8f;
}

PTMFX_SET_PARAM(void *st, uint32_t index, float value)
{
    if (index == 0)
    {
        ((Room *)st)->width = value * 0.01f;
    }
}

PTMFX_RESET(void *st)
{
    Room *r = (Room *)st;                   /* the lines are already silent: start them over */
    for (uint32_t i = 0; i < COMBS; i++)
    {
        r->comb[i].at = 0;
        r->damp[i] = 0.0f;
    }
    for (uint32_t i = 0; i < PASSES; i++)
    {
        r->passL[i].at = 0;
        r->passR[i].at = 0;
    }
    r->pre.at = 0;
}

static inline float allpass(Line *l, float x, float g)
{
    const float d = l->buf[l->at];
    const float y = d - g * x;
    l->buf[l->at] = x + g * y;
    if (++l->at == l->size)
    {
        l->at = 0;
    }
    return y;
}

PTMFX_PROCESS(void *st, const PtmfxBlock *b)
{
    Room *r = (Room *)st;
    const PtmfxReverbKnobs *k = (const PtmfxReverbKnobs *)b->knobs;
    float *left = b->fleft, *right = b->fright;
    if (!r->ready)
    {
        return 1.0f;                        /* not enough memory: the send passes as it is */
    }
    const float feedback = 0.70f + 0.27f * k->size;
    const float lp = k->lp;                 /* 1 - Damping: 1 keeps the highs */
    const float diffusion = k->diffusion;
    const uint32_t delay = (uint32_t)(k->predelay < 0 ? 0 : k->predelay) % PREDELAY_MAX;
    const float side = 0.5f * r->width;

    for (uint32_t i = 0; i < b->frames; i++)
    {
        /* the predelay: write now, read `delay` samples back */
        Line *p = &r->pre;
        p->buf[p->at] = 0.5f * (left[i] + right[i]) * k->input_gain;
        const uint32_t back = p->at >= delay ? p->at - delay : p->at + p->size - delay;
        const float in = p->buf[back];
        if (++p->at == p->size)
        {
            p->at = 0;
        }

        float c[COMBS];
        for (uint32_t n = 0; n < COMBS; n++)
        {
            Line *l = &r->comb[n];
            const float y = l->buf[l->at];
            r->damp[n] += lp * (y - r->damp[n]);
            l->buf[l->at] = in + feedback * r->damp[n];
            if (++l->at == l->size)
            {
                l->at = 0;
            }
            c[n] = y;
        }
        float wl = c[0] + c[2], wr = c[1] + c[3];
        for (uint32_t n = 0; n < PASSES; n++)
        {
            wl = allpass(&r->passL[n], wl, diffusion);
            wr = allpass(&r->passR[n], wr, diffusion);
        }
        /* Width: the sides' difference, scaled; 0 is mono */
        const float mid = 0.5f * (wl + wr), dif = side * (wl - wr);
        wl = 0.25f * (mid + dif);
        wr = 0.25f * (mid - dif);

        left[i] += (wl - left[i]) * k->amount;
        right[i] += (wr - right[i]) * k->amount;
    }
    return 1.0f;
}
