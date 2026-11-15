/* Modular-Trackular mods. SPDX-License-Identifier: MIT
 *
 * GuardHang: a test of the Master FX start marker (tools/masterfx/test_guards.py). A dynamics
 * plugin whose first block never returns, so a unit hangs as soon as the slot runs it. The
 * marker (/MasterFX/.starting) is on the card before that block, and at the next start it must
 * keep the project's plugins from loading. On a unit only with the lockup recorder build, which
 * restarts it; take the file off the card after the test. Not for music.
 */
#include "ptmfx.h"

typedef struct
{
    uint32_t unused;
} GuardHang;

PTMFX_INFO = {
    0, 0, 0,
    PTMFX_SLOT_DYNAMICS, 0, {0, 0},
    "GuardHang", "MT+ tests",
    {"", "", ""},
    sizeof(GuardHang),
    0, 0, 0, 0, 0, 0, 0,
    {0, 0, 0},
    {},
};

PTMFX_INIT(void *st, const PtmfxMemory *mem, const PtmfxHost *host)
{
    (void)st;
    (void)mem;
    (void)host;
}

PTMFX_SET_PARAM(void *st, uint32_t index, float value)
{
    (void)st;
    (void)index;
    (void)value;
}

PTMFX_RESET(void *st)
{
    (void)st;
}

PTMFX_PROCESS(void *st, const PtmfxBlock *b)
{
    (void)st;
    (void)b;
    volatile uint32_t spin = 0;
    for (;;)
    {
        spin++;
    }
    return 1.0f;
}
