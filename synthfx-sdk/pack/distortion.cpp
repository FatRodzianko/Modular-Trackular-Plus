// Modular-Trackular Plus: the Synth FX default pack. SPDX-License-Identifier: MIT
//
// Distortion: after the DS-1: a hard clip at 4x with anti-aliasing, then its tone stack
// (core/mods/synthfx/synthFxDsp.h).

#include "pack.h"

using namespace mtfx;

PTSFX_INFO = {
    0, 0, PTSFX_FLAG_PRE, /* may run in each WAVE voice, before its filter (Placement Pre) */
    "Distortion", "MT+", pack::DK_COUNT, { 0, 0, 0 }, sizeof(pack::DistState),
    0, 0, 0, 0, 0, 0, 0,
    { PACK_DIST_KNOBS("Gain", "Tone") },
};

PTSFX_INIT(void *st, const PtsfxHost *) { pack::dist_init<DIST_DISTORTION>(st); }
PTSFX_SET_PARAM(void *st, uint32_t index, float value) { pack::dist_set(st, index, value); }
PTSFX_PROCESS(void *st, float *L, float *R, uint32_t, uint32_t in_channels) { pack::dist_process<DIST_DISTORTION>(st, L, R, in_channels); }
PTSFX_RESET(void *st) { pack::dist_restart(st); }

PTSFX_FORMAT(const PtsfxHost *h, uint32_t index, float value, char *out, uint32_t size)
{
    if (index == pack::DK_GAIN)
        h->print(out, size, "+%.0f dB", 10.0f + (value / 100.0f) * 40.0f);
    else if (index == pack::DK_TONE)
        h->print(out, size, "%.0f %%", value);
    else
        pack::level_text(h, value, out, size);
}
