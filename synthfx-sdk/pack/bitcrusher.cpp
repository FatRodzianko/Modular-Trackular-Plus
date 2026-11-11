// Modular-Trackular Plus: the Synth FX default pack. SPDX-License-Identifier: MIT
//
// Bit Crusher: a sample-and-hold at Rate, rounded to Bits; no anti-aliasing: the aliasing is the sound
// (core/mods/synthfx/synthFxDsp.h).

#include "pack.h"

using namespace mtfx;

PTSFX_INFO = {
    0, 0, 0,
    "Bit Crusher", "MT+", pack::DK_COUNT, { 0, 0, 0 }, sizeof(pack::DistState),
    0, 0, 0, 0, 0, 0, 0,
    { PACK_DIST_KNOBS("Bits", "Rate") },
};

PTSFX_INIT(void *st, const PtsfxHost *) { pack::dist_init<DIST_CRUSHER>(st); }
PTSFX_SET_PARAM(void *st, uint32_t index, float value) { pack::dist_set(st, index, value); }
PTSFX_PROCESS(void *st, float *L, float *R, uint32_t, uint32_t in_channels) { pack::dist_process<DIST_CRUSHER>(st, L, R, in_channels); }
PTSFX_RESET(void *st) { pack::dist_restart(st); }

PTSFX_FORMAT(const PtsfxHost *h, uint32_t index, float value, char *out, uint32_t size)
{
    if (index == pack::DK_GAIN)
        h->print(out, size, "%.1f bit", dsp::crush_bits(value / 100.0f));
    else if (index == pack::DK_TONE)
        pack::hz_text(h, dsp::crush_hz(value / 100.0f), out, size);
    else
        pack::level_text(h, value, out, size);
}
