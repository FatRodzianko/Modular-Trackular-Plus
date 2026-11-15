/* Modular-Trackular mods. SPDX-License-Identifier: MIT
 *
 * GuardGain: a test of the Master FX ducking gain's and meter's guards
 * (tools/masterfx/test_guards.py). A float dynamics plugin with PTMFX_FLAG_DUCK: it halves the
 * master (or, ducking, fills the gain with 0.5) and returns 0.5 for the meter, except that the
 * Fault knob spoils one of them every block:
 *   None; Gain NaN, Gain 9 or Gain -1 (frame 5 of the gain); Meter NaN or Meter 2 (the value
 *   process() returns).
 * The host must hold the gain within 0 to 4, make a NaN 1 and reset the plugin, and hold the
 * meter within 0 to 1 (a NaN reads 1). The state counts the resets and the blocks. Not for music.
 */
#include "ptmfx.h"

typedef struct
{
    uint32_t fault;
    uint32_t resets;
    uint32_t blocks;
} GuardGain;

PTMFX_INFO = {
    0, 0, PTMFX_FLAG_FLOAT | PTMFX_FLAG_DUCK,
    PTMFX_SLOT_DYNAMICS, 1, {0, 0},
    "GuardGain", "MT+ tests",
    {"", "", ""},
    sizeof(GuardGain),
    0, 0, 0, 0, 0, 0, 0,
    {0, 0, 0},
    {
        PTMFX_KNOB("Fault", 0, 5, 0, 1, "", PTMFX_PARAM_LIST),
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
        ((GuardGain *)st)->fault = (uint32_t)(value + 0.5f);
    }
}

PTMFX_RESET(void *st)
{
    ((GuardGain *)st)->resets++;
}

PTMFX_PROCESS(void *st, const PtmfxBlock *b)
{
    GuardGain *g = (GuardGain *)st;
    volatile float zero = 0.0f;
    for (uint32_t i = 0; i < b->frames; i++)
    {
        if (b->gain != 0)
        {
            b->gain[i] = 0.5f;
        }
        else
        {
            b->fleft[i] *= 0.5f;
            b->fright[i] *= 0.5f;
        }
    }
    if (b->gain != 0)
    {
        if (g->fault == 1u)
        {
            b->gain[5] = zero / zero;
        }
        else if (g->fault == 2u)
        {
            b->gain[5] = 9.0f;
        }
        else if (g->fault == 3u)
        {
            b->gain[5] = -1.0f;
        }
    }
    g->blocks++;
    if (g->fault == 4u)
    {
        return zero / zero;
    }
    return g->fault == 5u ? 2.0f : 0.5f;
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
    case 1u: put(out, size, "Gain NaN"); break;
    case 2u: put(out, size, "Gain 9"); break;
    case 3u: put(out, size, "Gain -1"); break;
    case 4u: put(out, size, "Meter NaN"); break;
    case 5u: put(out, size, "Meter 2"); break;
    default: put(out, size, "None"); break;
    }
}
