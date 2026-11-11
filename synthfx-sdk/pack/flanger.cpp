// Modular-Trackular Plus: the Synth FX default pack. SPDX-License-Identifier: MIT
//
// Flanger: two delay lines, one a side, swept in opposite directions, with Feedback
// (core/mods/synthfx/synthFxDsp.h). Stereo from mono: on a mono part it makes R.

#include "pack.h"

using namespace mtfx;

PTSFX_INFO = {
    0, 0, PTSFX_FLAG_WIDENS,
    "Flanger", "MT+", 4, { 0, 0, 0 }, sizeof(pack::ModState),
    0, 0, 0, 0, 0, 0, 0,
    { PACK_KNOB_RATE, PACK_KNOB_DEPTH, PACK_KNOB_FEEDBACK, PACK_KNOB_MIX },
};

PTSFX_INIT(void *st, const PtsfxHost *) { pack::mod_init<FX_FLANGER>(st); }
PTSFX_SET_PARAM(void *st, uint32_t index, float value) { pack::mod_set<true>(st, index, value); }
PTSFX_PROCESS(void *st, float *L, float *R, uint32_t, uint32_t in_channels) { pack::mod_process<FX_FLANGER>(st, L, R, in_channels); }
PTSFX_RESET(void *st) { pack::mod_restart(st); }
PTSFX_FORMAT(const PtsfxHost *h, uint32_t index, float value, char *out, uint32_t size) { pack::mod_format<true>(h, index, value, out, size); }
