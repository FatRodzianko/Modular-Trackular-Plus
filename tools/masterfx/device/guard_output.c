/* Modular-Trackular mods. SPDX-License-Identifier: MIT
 *
 * GuardOutput: a test of the Master FX output guard (tools/masterfx/test_guards.py). A reverb
 * with float blocks whose return is its send, unchanged, except that the Fault knob puts a bad
 * value into frame 5 of the left side every block:
 *   None; NaN; Big (100.0, past the host's +-4); Inf.
 * The host must turn a NaN or an infinity into 0 and reset the plugin before its next block, and
 * hold 100.0 at 4.0 (full scale once it is back in 16 bits) without a reset. The state counts
 * the resets and the blocks for the test to read. Not for music.
 */
#include "ptmfx.h"

typedef struct
{
    uint32_t fault;
    uint32_t resets;
    uint32_t blocks;
} GuardOutput;

PTMFX_INFO = {
    0, 0, PTMFX_FLAG_FLOAT,
    PTMFX_SLOT_REVERB, 1, {0, 0},
    "GuardOutput", "MT+ tests",
    {"", "", "", ""},
    sizeof(GuardOutput),
    0, 0, 0, 0, 0, 0, 0,
    {0, 0, 0},
    {
        PTMFX_KNOB("Fault", 0, 3, 0, 1, "", PTMFX_PARAM_LIST),
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
        ((GuardOutput *)st)->fault = (uint32_t)(value + 0.5f);
    }
}

PTMFX_RESET(void *st)
{
    ((GuardOutput *)st)->resets++;
}

PTMFX_PROCESS(void *st, const PtmfxBlock *b)
{
    GuardOutput *g = (GuardOutput *)st;
    volatile float zero = 0.0f;             /* kept from the compiler, so the faults are made here */
    if (g->fault == 1u)
    {
        b->fleft[5] = zero / zero;
    }
    else if (g->fault == 2u)
    {
        b->fleft[5] = 100.0f;
    }
    else if (g->fault == 3u)
    {
        b->fleft[5] = 1.0f / zero;
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
    if (index != 0u)
    {
        return;
    }
    switch ((uint32_t)(value + 0.5f))   /* no table of pointers: it would need relocating */
    {
    case 1u: put(out, size, "NaN"); break;
    case 2u: put(out, size, "Big"); break;
    case 3u: put(out, size, "Inf"); break;
    default: put(out, size, "None"); break;
    }
}
