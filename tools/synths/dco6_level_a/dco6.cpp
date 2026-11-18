// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Level A for DCO6: the engine (core/mods/synths/d6Engine.h) behind plain C entry points, for
// tools/emu/test_dco6_dsp.py. Only pointers and integers cross (the hard-float ABI passes floats in
// registers machine.call doesn't set): ctl is { lfoHz, bend in semitones }. p is indexed by
// parameter id, as the plugin's voice keeps it.

#include "d6Engine.h"

using namespace mtsynth;

extern "C" {

extern const uint32_t a_d6_size = sizeof(struct d6_voice);
extern const uint32_t a_d6_params = DP_COUNT;

void a_d6_init(struct d6_voice* v, int index) { d6_voice_init(v, index); }

void a_d6_on(struct d6_voice* v, const float* p, uint32_t note, uint32_t slot, uint32_t size, const float* ctl)
{
    d6_note_on(v, p, note, slot, size, ctl[0]);
}

void a_d6_off(struct d6_voice* v) { d6_note_off(v); }

/* blocks of 128 into L and R (cleared first); returns 1 while the voice sounds */
int a_d6_render(struct d6_voice* v, const float* p, float* L, float* R, uint32_t blocks, const float* ctl)
{
    for (uint32_t b = 0; b < blocks; b++) {
        float* l = L + b * D6_BLOCK;
        float* r = R + b * D6_BLOCK;
        for (uint32_t n = 0; n < D6_BLOCK; n++)
            l[n] = r[n] = 0.0f;
        d6_render(v, p, l, r, ctl[0], ctl[1]);
    }
    return d6_sounding(v) ? 1 : 0;
}

/* what the tests read: env, gate, lfoPhase, (float) lfoCycle, drift[0], drift[1], phase */
void a_d6_state(const struct d6_voice* v, float* out)
{
    out[0] = v->env;
    out[1] = v->gate;
    out[2] = v->lfoPhase;
    out[3] = (float)v->lfoCycle;
    out[4] = v->drift[0];
    out[5] = v->drift[1];
    out[6] = v->phase;
}

/* LFO Trigger Free's table, emptied as at the plugin's load */
void a_d6_lfo_reset(void)
{
    for (uint32_t i = 0; i < D6_LFO_SLOTS; i++)
        d6_lfoTab[i] = d6_lfo_slot{};
    d6_lfoClock = 0;
}

} // extern "C"
