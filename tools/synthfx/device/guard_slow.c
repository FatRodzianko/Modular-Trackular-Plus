/* Modular-Trackular mods. SPDX-License-Identifier: MIT
 *
 * GuardSlow: a device test of the time guard (docs/mt/DEVICE_TEST_LADDER.md, the Synth FX
 * plugin set). It passes the sound through after a busy loop before each block:
 *   Load Light: 50,000 turns, well under a quarter of a block on the unit, so it keeps running;
 *   Load Heavy: 250,000 turns, over a quarter, so the host bypasses it after 8 blocks.
 * A turn is about 6 instructions (the emulator's count, tools/emu/test_synthfx.py's guards);
 * on the unit perhaps 2 to 6 cycles, which the Profiler reads with Light. Not for music.
 */
#include "ptsfx.h"

typedef struct
{
    uint32_t heavy;
} GuardSlow;

#define LIGHT_TURNS 50000u
#define HEAVY_TURNS 250000u

PTSFX_INFO = {
    0, 0, 0, "GuardSlow", "MT+ tests", 1, {0, 0, 0}, sizeof(GuardSlow), 0, 0, 0, 0, 0, 0, 0,
    {
        PTSFX_KNOB("Load", 0, 1, 0, 1, "", PTSFX_PARAM_LIST),
    },
};

PTSFX_INIT(void *st, const PtsfxHost *host)
{
    (void)st;
    (void)host;
}

PTSFX_SET_PARAM(void *st, uint32_t index, float value)
{
    if (index == 0u)
        ((GuardSlow *)st)->heavy = value >= 0.5f ? 1u : 0u;
}

PTSFX_RESET(void *st)
{
    (void)st;
}

PTSFX_PROCESS(void *st, float *L, float *R, uint32_t frames, uint32_t in_channels)
{
    const GuardSlow *g = (const GuardSlow *)st;
    volatile uint32_t spin = 0;
    const uint32_t turns = g->heavy ? HEAVY_TURNS : LIGHT_TURNS;
    for (uint32_t i = 0; i < turns; i++)
        spin++;
    if (R != 0 && in_channels == 1u)        /* a mono part sent as stereo: R as L */
        for (uint32_t i = 0; i < frames; i++)
            R[i] = L[i];
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
    if (index == 0u)
        put(out, size, value >= 0.5f ? "Heavy" : "Light");
}
