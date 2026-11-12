/* Modular-Trackular mods. SPDX-License-Identifier: MIT
 *
 * A plugin for tools/synthfx/test_runtime.py: it tells the test what the host did to it, and
 * misbehaves on request, so the runtime's guards have something to catch.
 *
 * Mode 0 (Probe): the block passes through, except L[0..3] are the knobs as set_param gave
 * them, L[4] the set_param calls, L[5] the init calls, L[6] the resets, L[7] in_channels,
 * L[8] 1 if R is null, L[9] the blocks run, each divided by 64 (exact, and inside the host's
 * +-4). With R and in_channels 1, R is a copy of L.
 * Mode 1: L[5] is NaN. Mode 2: L[5] is infinite. Mode 3 (Loud): L 100, R -100.
 * Mode 4: L[0] is the resets / 64, the rest passes.
 * Spin: a busy loop of Spin x 1000 turns before the block, for the time guard.
 */
#include "ptsfx.h"

typedef struct
{
    float knob[4];
    uint32_t sets, inits, resets, blocks;
    const PtsfxHost *host;
} State;

PTSFX_INFO = {
    0, 0, 0, "Probe", "MT+ tests", 4, {0, 0, 0}, sizeof(State), 0, 0, 0, 0, 0, 0, 0,
    {
        PTSFX_KNOB("A", 0, 100, 25, 1, "%", PTSFX_PARAM_INT),
        PTSFX_KNOB("Fine", -1, 1, 0.1, 0.1, "st", PTSFX_PARAM_FLOAT),
        PTSFX_KNOB("Mode", 0, 4, 0, 1, "", PTSFX_PARAM_LIST),
        PTSFX_KNOB("Spin", 0, 100, 0, 1, "", PTSFX_PARAM_INT),
    },
};

PTSFX_INIT(void *st, const PtsfxHost *host)
{
    State *s = (State *)st;
    s->inits++;
    s->host = host;
}

PTSFX_SET_PARAM(void *st, uint32_t index, float value)
{
    State *s = (State *)st;
    if (index < 4u)
        s->knob[index] = value;
    s->sets++;
}

PTSFX_RESET(void *st)
{
    ((State *)st)->resets++;
}

PTSFX_PROCESS(void *st, float *L, float *R, uint32_t frames, uint32_t in_channels)
{
    State *s = (State *)st;
    volatile uint32_t spin = 0;
    for (uint32_t i = 0; i < (uint32_t)s->knob[3] * 1000u; i++)
        spin++;
    s->blocks++;
    const uint32_t mode = (uint32_t)s->knob[2];
    if (R != 0 && in_channels == 1u)
        for (uint32_t i = 0; i < frames; i++)
            R[i] = L[i];
    if (mode == 0u) {
        const float k64 = 1.0f / 64.0f;
        for (uint32_t k = 0; k < 4u; k++)
            L[k] = s->knob[k] * k64;
        L[4] = (float)s->sets * k64;
        L[5] = (float)s->inits * k64;
        L[6] = (float)s->resets * k64;
        L[7] = (float)in_channels * k64;
        L[8] = (R == 0 ? 1.0f : 0.0f) * k64;
        L[9] = (float)s->blocks * k64;
    } else if (mode == 1u) {
        L[5] = __builtin_nanf("");
    } else if (mode == 2u) {
        L[5] = __builtin_inff();
    } else if (mode == 3u) {
        for (uint32_t i = 0; i < frames; i++) {
            L[i] = 100.0f;
            if (R != 0)
                R[i] = -100.0f;
        }
    } else {
        L[0] = (float)s->resets * (1.0f / 64.0f);
    }
}

static void put(char *out, uint32_t size, const char *text)
{
    uint32_t n = 0;
    for (; n + 1u < size && text[n] != 0; n++)
        out[n] = text[n];
    if (size > 0u)
        out[n] = 0;
}

PTSFX_FORMAT(const PtsfxHost *host, uint32_t index, float value, char *out, uint32_t size)
{
    (void)host;
    if (index != 2u)
        return;                             /* the host's text */
    switch ((uint32_t)value) {
    case 0: put(out, size, "Probe"); break;
    case 1: put(out, size, "NaN"); break;
    case 2: put(out, size, "Inf"); break;
    case 3: put(out, size, "Loud"); break;
    default: put(out, size, "Resets"); break;
    }
}
