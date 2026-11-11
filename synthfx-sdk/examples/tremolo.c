/* Modular-Trackular Plus: an example Synth FX plugin. SPDX-License-Identifier: MIT
 *
 * Tremolo: the level rises and falls with a low-frequency oscillator. The smallest useful
 * plugin: a few knobs, one of them a list shown by format(), the host's sinf, state kept in
 * the instance, and the knobs smoothed so a move doesn't click.
 *
 *     python ptsfx_builder.py examples/tremolo.c -o Tremolo.ptsfx
 */
#include "ptsfx.h"

typedef struct
{
    const PtsfxHost *host;
    float phase;                            /* the LFO, 0 to 1 */
    float rate, depth;                      /* where the knobs are */
    float depth_now;                        /* where Depth has got to (smoothed) */
    uint32_t shape;                         /* 0 sine, 1 square, 2 triangle */
    uint32_t started;                       /* a block has run since init or reset */
} Tremolo;

PTSFX_INFO = {
    0, 0, 0, "Tremolo", "MT+ examples", 3, {0, 0, 0}, sizeof(Tremolo), 0, 0, 0, 0, 0, 0, 0,
    {
        PTSFX_KNOB("Rate", 0.1, 20, 4, 0.1, "Hz", PTSFX_PARAM_FLOAT),
        PTSFX_KNOB("Depth", 0, 100, 50, 1, "%", PTSFX_PARAM_INT),
        PTSFX_KNOB("Shape", 0, 2, 0, 1, "", PTSFX_PARAM_LIST),
    },
};

PTSFX_INIT(void *st, const PtsfxHost *host)
{
    Tremolo *t = (Tremolo *)st;             /* zeroed by the host; every knob is set after this */
    t->host = host;
}

PTSFX_SET_PARAM(void *st, uint32_t index, float value)
{
    Tremolo *t = (Tremolo *)st;
    if (index == 0)
        t->rate = value;
    else if (index == 1)
        t->depth = value * 0.01f;
    else
        t->shape = (uint32_t)value;
}

/* reset() must leave the instance as a fresh one: the next block starts it again, from the
 * knobs as they are then (check_ptsfx.py checks) */
PTSFX_RESET(void *st)
{
    Tremolo *t = (Tremolo *)st;
    t->phase = 0.0f;
    t->started = 0;
}

/* The LFO at phase p (0 to 1), from -1 to 1 */
static float lfo(const Tremolo *t, float p)
{
    if (t->shape == 1u)
        return p < 0.5f ? 1.0f : -1.0f;
    if (t->shape == 2u)
        return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
    return t->host->sinf(6.2831853f * p);
}

PTSFX_PROCESS(void *st, float *L, float *R, uint32_t frames, uint32_t in_channels)
{
    Tremolo *t = (Tremolo *)st;
    (void)in_channels;
    /* The gain once per 16 frames: sinf per sample would cost 8 times as much, unheard */
    const float step = t->rate * (1.0f / PTSFX_RATE);
    if (!t->started) {                      /* no glide from 0 to the first Depth */
        t->depth_now = t->depth;
        t->started = 1;
    }
    for (uint32_t n = 0; n < frames; n += 16u) {
        t->depth_now += 0.05f * (t->depth - t->depth_now);
        const float gain = 1.0f - t->depth_now * 0.5f * (1.0f - lfo(t, t->phase));
        for (uint32_t i = n; i < n + 16u && i < frames; i++) {
            L[i] *= gain;
            if (R != 0)
                R[i] *= gain;
        }
        t->phase += 16.0f * step;
        if (t->phase >= 1.0f)
            t->phase -= 1.0f;
    }
}

static void put(char *out, uint32_t size, const char *text)
{
    uint32_t n = 0;
    for (; n + 1u < size && text[n] != 0; n++)
        out[n] = text[n];
    out[n] = 0;
}

PTSFX_FORMAT(const PtsfxHost *host, uint32_t index, float value, char *out, uint32_t size)
{
    (void)host;
    if (index != 2u)
        return;                             /* left empty: the host shows the value and unit */
    const uint32_t shape = (uint32_t)value;
    put(out, size, shape == 1u ? "Square" : shape == 2u ? "Triangle" : "Sine");
}
