/* Modular-Trackular mods. SPDX-License-Identifier: MIT
 *
 * GuardSlow: a test of the Master FX time guard (tools/masterfx/test_guards.py, and the device
 * check in docs/mt/DEVICE_TEST_LADDER.md). A dynamics plugin with 16-bit blocks: it passes the
 * master through after a busy loop each block.
 *   Load Light: 20,000 turns, about 120,000 instructions, under the guard's eighth of a block
 *   (about 217,000 cycles), so it keeps running unless the unit takes more than about 10 cycles
 *   a turn;
 *   Load Heavy: 100,000 turns, about 600,000 instructions, over it on any count, so the host
 *   bypasses the slot after 8 blocks and the master passes through untouched.
 * A turn is about 6 instructions (the emulator's count); on the unit perhaps 2 to 6 cycles.
 * Not for music.
 */
#include "ptmfx.h"

typedef struct
{
    uint32_t heavy;
    uint32_t blocks;                        /* process() calls, for the test to read */
} GuardSlow;

#define LIGHT_TURNS 20000u
#define HEAVY_TURNS 100000u

PTMFX_INFO = {
    0, 0, 0,
    PTMFX_SLOT_DYNAMICS, 1, {0, 0},
    "GuardSlow", "MT+ tests",
    {"", "", ""},
    sizeof(GuardSlow),
    0, 0, 0, 0, 0, 0, 0,
    {0, 0, 0},
    {
        PTMFX_KNOB("Load", 0, 1, 0, 1, "", PTMFX_PARAM_LIST),
    },
};

PTMFX_INIT(void *st, const PtmfxMemory *mem, const PtmfxHost *host)
{
    (void)st;
    (void)mem;
    (void)host;
}

PTMFX_SET_PARAM(void *st, uint32_t index, float value)
{
    if (index == 0u)
    {
        ((GuardSlow *)st)->heavy = value >= 0.5f ? 1u : 0u;
    }
}

PTMFX_RESET(void *st)
{
    (void)st;
}

PTMFX_PROCESS(void *st, const PtmfxBlock *b)
{
    GuardSlow *g = (GuardSlow *)st;
    (void)b;                                /* the master in place: left as it is */
    volatile uint32_t spin = 0;
    const uint32_t turns = g->heavy ? HEAVY_TURNS : LIGHT_TURNS;
    for (uint32_t i = 0; i < turns; i++)
    {
        spin++;
    }
    g->blocks++;
    return 1.0f;
}

static void put(char *out, uint32_t size, const char *text)
{
    uint32_t n = 0;
    for (; n + 1u < size && text[n] != 0; n++)
    {
        out[n] = text[n];
    }
    if (size > 0u)
    {
        out[n] = 0;
    }
}

PTMFX_FORMAT(const PtmfxHost *host, uint32_t index, float value, char *out, uint32_t size)
{
    (void)host;
    if (index == 0u)
    {
        put(out, size, value >= 0.5f ? "Heavy" : "Light");
    }
}
