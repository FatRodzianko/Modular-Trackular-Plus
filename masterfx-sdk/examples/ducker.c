/* Modular-Trackular Plus: an example Master FX plugin. SPDX-License-Identifier: MIT
 *
 * Ducker: turns the master down while the key is loud, for the dynamics slot (in place of CE's
 * limiter). Pick a kick in CE's Sidechain list and the rest of the mix makes room for it. It shows:
 *   - the dynamics slot's block: the master in place, and the key when the Sidechain list has one
 *     (without a key it follows the master itself);
 *   - CE's Attack and Release as the follower's times, and CE's Threshold, renamed Trigger: the
 *     key's level at which the duck is full;
 *   - two own knobs, one of them a list shown by format();
 *   - PTMFX_FLAG_DUCK: the Limiter page's Ducks list (Shift + the column 4 button) can turn down
 *     chosen tracks instead of the master. Then the host passes a gain array and the plugin fills
 *     it, frame by frame; what it leaves in the block is thrown away.
 *   - the meter: process() returns the block's mean gain.
 *
 *     python masterfx-sdk/ptmfx_builder.py masterfx-sdk/examples/ducker.c -o Ducker.ptmfx
 */
#include "ptmfx.h"

typedef struct
{
    const PtmfxHost *host;
    float depth;                            /* 0 to 1: how far down a full duck goes */
    uint32_t smooth;                        /* the curve: 0 linear, 1 smooth */
    float attackMs, releaseMs, threshold;   /* CE's knobs as last seen */
    float attack, release, inverse;         /* worked out from them */
    float level;                            /* the follower */
} Ducker;

PTMFX_INFO = {
    0, 0, PTMFX_FLAG_FLOAT | PTMFX_FLAG_DUCK, /* magic and version (the builder's), flags */
    PTMFX_SLOT_DYNAMICS, 2, {0, 0},         /* slot, own knobs, reserved */
    "Ducker", "MT+ examples",
    {"", "", "Trigger"},                    /* CE's Attack and Release as they are; Threshold renamed */
    sizeof(Ducker),
    0, 0, 0, 0, 0, 0, 0,                    /* the code's place and the entries (the builder's) */
    {0, 0, 0},
    {
        PTMFX_KNOB("Depth", 0, 100, 60, 1, "%", PTMFX_PARAM_INT),
        PTMFX_KNOB("Curve", 0, 1, 1, 1, "", PTMFX_PARAM_LIST),
    },
};

PTMFX_INIT(void *st, const PtmfxMemory *mem, const PtmfxHost *host)
{
    (void)mem;                              /* no memory needed beyond the state */
    Ducker *d = (Ducker *)st;
    d->host = host;
    d->depth = 0.6f;
    d->smooth = 1;
    d->attackMs = -1.0f;                    /* worked out on the first block */
    d->releaseMs = -1.0f;
    d->threshold = -1.0f;
}

PTMFX_SET_PARAM(void *st, uint32_t index, float value)
{
    Ducker *d = (Ducker *)st;
    if (index == 0)
    {
        d->depth = value * 0.01f;
    }
    else
    {
        d->smooth = value > 0.5f;
    }
}

PTMFX_RESET(void *st)
{
    ((Ducker *)st)->level = 0.0f;
}

static void put(char *out, uint32_t size, const char *text)
{
    uint32_t i = 0;
    for (; i + 1 < size && text[i] != 0; i++)
    {
        out[i] = text[i];
    }
    if (size > 0)
    {
        out[i] = 0;
    }
}

/* The page's text for the Curve list; Depth keeps the host's ("60 %") */
PTMFX_FORMAT(const PtmfxHost *host, uint32_t index, float value, char *out, uint32_t size)
{
    (void)host;
    if (index == 1)
    {
        put(out, size, value > 0.5f ? "Smooth" : "Linear");
    }
}

/* A follower's coefficient for a time in ms */
static float coefficient(const PtmfxHost *host, float ms)
{
    if (!(ms > 0.1f))
    {
        ms = 0.1f;
    }
    return 1.0f - host->expf(-1000.0f / (ms * host->sample_rate));
}

static inline float absf(float x)
{
    return x < 0.0f ? -x : x;
}

PTMFX_PROCESS(void *st, const PtmfxBlock *b)
{
    Ducker *d = (Ducker *)st;
    const PtmfxDynamicsKnobs *k = (const PtmfxDynamicsKnobs *)b->knobs;
    if (k->attack != d->attackMs)
    {
        d->attackMs = k->attack;
        d->attack = coefficient(d->host, k->attack);
    }
    if (k->release != d->releaseMs)
    {
        d->releaseMs = k->release;
        d->release = coefficient(d->host, k->release);
    }
    if (k->threshold != d->threshold)
    {
        d->threshold = k->threshold;
        d->inverse = 1.0f / (k->threshold > 0.001f ? k->threshold : 0.001f);
    }

    float *left = b->fleft, *right = b->fright;
    const int keyed = b->fkey_left != 0 && b->fkey_right != 0;
    const float *keyL = keyed ? b->fkey_left : left;
    const float *keyR = keyed ? b->fkey_right : right;
    float *gain = b->gain;                  /* set: ducking chosen tracks, not the master */

    float level = d->level, sum = 0.0f;
    for (uint32_t i = 0; i < b->frames; i++)
    {
        const float a = absf(keyL[i]) > absf(keyR[i]) ? absf(keyL[i]) : absf(keyR[i]);
        level += (a > level ? d->attack : d->release) * (a - level);
        float amount = level * d->inverse;
        amount = amount > 1.0f ? 1.0f : amount;
        if (d->smooth)
        {
            amount = amount * amount * (3.0f - 2.0f * amount);
        }
        const float g = 1.0f - d->depth * amount;
        sum += g;
        if (gain != 0)
        {
            gain[i] = g;                    /* the host turns the chosen tracks down by it */
        }
        else if (keyed)
        {
            /* the key keeps its level, as with CE's limiter */
            left[i] = keyL[i] + g * (left[i] - keyL[i]);
            right[i] = keyR[i] + g * (right[i] - keyR[i]);
        }
        else
        {
            left[i] *= g;
            right[i] *= g;
        }
    }
    d->level = level;
    return sum * (1.0f / PTMFX_BLOCK);
}
