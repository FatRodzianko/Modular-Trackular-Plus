/* Modular-Trackular mods. SPDX-License-Identifier: MIT
 *
 * GuardHang: a device test of the start marker (docs/mt/DEVICE_TEST_LADDER.md, the Synth FX
 * plugin set). Its first block never returns, so the unit hangs as soon as a part with it
 * renders. Only on the lockup recorder build, which restarts the unit; the marker
 * (/SynthFX/.starting) must then keep the project's plugins from loading at the next start.
 * Take it off the card after the test. Not for music.
 */
#include "ptsfx.h"

typedef struct
{
    uint32_t unused;
} GuardHang;

PTSFX_INFO = {
    0, 0, 0, "GuardHang", "MT+ tests", 0, {0, 0, 0}, sizeof(GuardHang), 0, 0, 0, 0, 0, 0, 0,
    {},
};

PTSFX_INIT(void *st, const PtsfxHost *host)
{
    (void)st;
    (void)host;
}

PTSFX_SET_PARAM(void *st, uint32_t index, float value)
{
    (void)st;
    (void)index;
    (void)value;
}

PTSFX_RESET(void *st)
{
    (void)st;
}

PTSFX_PROCESS(void *st, float *L, float *R, uint32_t frames, uint32_t in_channels)
{
    (void)st;
    (void)L;
    (void)R;
    (void)frames;
    (void)in_channels;
    for (;;)
        __asm__ volatile("");
}
