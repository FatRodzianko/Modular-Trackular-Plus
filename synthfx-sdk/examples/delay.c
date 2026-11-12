/* Modular-Trackular Plus: an example Synth FX plugin. SPDX-License-Identifier: MIT
 *
 * Delay: a short stereo echo, up to 180 ms, with feedback. It shows a plugin with a big state
 * (64 KB, the most a plugin may ask for), a stereo output from a mono part (WIDENS), a
 * smoothed delay time read between samples, and reset() clearing the lines.
 *
 *     python ptsfx_builder.py examples/delay.c -o Delay.ptsfx
 */
#include "ptsfx.h"

#define LINE 8000u                          /* samples per side: 181 ms at 44.1 kHz */

typedef struct
{
    float line[2][LINE];
    uint32_t pos;
    float time, time_now;                   /* in samples: the knob's, and the smoothed one */
    float feedback, mix;
} Delay;

PTSFX_INFO = {
    0, 0, PTSFX_FLAG_WIDENS, "Delay", "MT+ examples", 3, {0, 0, 0}, sizeof(Delay), 0, 0, 0, 0, 0, 0, 0,
    {
        PTSFX_KNOB("Time", 1, 180, 120, 1, "ms", PTSFX_PARAM_INT),
        PTSFX_KNOB("Feedback", 0, 90, 35, 1, "%", PTSFX_PARAM_INT),
        PTSFX_KNOB("Mix", 0, 100, 30, 1, "%", PTSFX_PARAM_INT),
    },
};

PTSFX_INIT(void *st, const PtsfxHost *host)
{
    (void)st;                               /* zeroed lines: silence; the knobs come next */
    (void)host;
}

PTSFX_SET_PARAM(void *st, uint32_t index, float value)
{
    Delay *d = (Delay *)st;
    if (index == 0) {
        d->time = value * (PTSFX_RATE / 1000.0f);
        if (d->time_now == 0.0f)
            d->time_now = d->time;          /* the first setting: no glide from 0 */
    } else if (index == 1) {
        d->feedback = value * 0.01f;
    } else {
        d->mix = value * 0.01f;
    }
}

PTSFX_RESET(void *st)
{
    Delay *d = (Delay *)st;
    for (uint32_t i = 0; i < LINE; i++) {
        d->line[0][i] = 0.0f;
        d->line[1][i] = 0.0f;
    }
    d->time_now = d->time;
}

/* The line read t samples back from pos, between samples */
static float tap(const float *line, uint32_t pos, float t)
{
    const uint32_t whole = (uint32_t)t;
    const float frac = t - (float)whole;
    uint32_t a = pos + LINE - whole;
    if (a >= LINE)
        a -= LINE;
    const uint32_t b = a == 0u ? LINE - 1u : a - 1u;
    return line[a] + frac * (line[b] - line[a]);
}

PTSFX_PROCESS(void *st, float *L, float *R, uint32_t frames, uint32_t in_channels)
{
    Delay *d = (Delay *)st;
    const float dry = 1.0f - d->mix, wet = d->mix, fb = d->feedback;
    for (uint32_t i = 0; i < frames; i++) {
        d->time_now += 0.001f * (d->time - d->time_now);
        const float xl = L[i];
        const float xr = (R != 0 && in_channels == 2u) ? R[i] : xl;
        const float yl = tap(d->line[0], d->pos, d->time_now);
        const float yr = tap(d->line[1], d->pos, d->time_now * 0.75f);  /* the right side sooner: width */
        d->line[0][d->pos] = xl + fb * yl;
        d->line[1][d->pos] = xr + fb * yr;
        d->pos = d->pos + 1u == LINE ? 0u : d->pos + 1u;
        L[i] = dry * xl + wet * yl;
        if (R != 0)
            R[i] = dry * xr + wet * yr;
    }
}
