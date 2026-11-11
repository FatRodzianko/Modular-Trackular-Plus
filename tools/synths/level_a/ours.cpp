// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Level A: WAVE's and PHASE's engines (core/mods/synths) behind plain C entry points, for
// tools/emu/test_synths_dsp.py. Only pointers and integers cross (the hard-float ABI passes
// floats in registers machine.call doesn't set): ctl is { tempo, bend in semitones, volume }.
// p is indexed by parameter id, as the CE voices keep it.

#include "phEngine.h"
#include "wtEngine.h"

using namespace mtsynth;

extern "C" {

extern const uint32_t a_wave_size = sizeof(struct wt_voice);
extern const uint32_t a_phase_size = sizeof(struct ph_voice);
extern const uint32_t a_fx_size = sizeof(struct mt_fx);
extern const uint32_t a_dist_size = sizeof(struct mt_dist);
extern const uint32_t a_params = P_COUNT;

void a_wave_init(struct wt_voice* v) { wt_voice_init(v); }

void a_wave_on(struct wt_voice* v, const float* p, uint32_t note, const float* ctl)
{
    v->volume = ctl[2];
    wt_note_on(v, p, note);
}

void a_wave_off(struct wt_voice* v) { wt_note_off(v); }

uint32_t a_wave_sounding(const struct wt_voice* v) { return wt_voice_sounding(v); }

void a_wave_render(struct wt_voice* v, const float* p, float* out, const float* ctl, uint32_t blocks)
{
    for (uint32_t b = 0; b < blocks; b++) {
        v->volume = ctl[2];
        v->bend = ctl[1];
        wt_voice_render(v, p, out + b * WT_BLOCK, ctl[0]);
    }
}

void a_phase_init(struct ph_voice* v) { ph_voice_init(v); }

void a_phase_on(struct ph_voice* v, const float* p, uint32_t note, const float* ctl)
{
    v->volume = ctl[2];
    ph_note_on(v, p, note);
}

void a_phase_off(struct ph_voice* v) { ph_note_off(v); }

uint32_t a_phase_sounding(const struct ph_voice* v) { return ph_voice_sounding(v); }

void a_phase_render(struct ph_voice* v, const float* p, float* out, const float* ctl, uint32_t blocks)
{
    for (uint32_t b = 0; b < blocks; b++) {
        v->volume = ctl[2];
        v->bend = ctl[1];
        ph_voice_render(v, p, out + b * WT_BLOCK, ctl[0]);
    }
}

/* the effects: blocks of L (mono in) to L and R */
void a_fx_run(struct mt_fx* s, const float* p, float* L, float* R, uint32_t distWanted, uint32_t blocks)
{
    for (uint32_t b = 0; b < blocks; b++)
        fx_block(s, p + P_EFFECT, L + b * WT_BLOCK, R + b * WT_BLOCK, (int)distWanted);
}

/* the same for a stereo part (Synth FX on PERC): L and R in and out */
void a_fx_run_stereo(struct mt_fx* s, const float* p, float* L, float* R, uint32_t distWanted, uint32_t blocks)
{
    for (uint32_t b = 0; b < blocks; b++)
        fx_block_stereo(s, p + P_EFFECT, L + b * WT_BLOCK, R + b * WT_BLOCK, (int)distWanted);
}

void a_dist_run(struct mt_dist* d, float* buf, const float* p, uint32_t want, uint32_t blocks)
{
    for (uint32_t b = 0; b < blocks; b++)
        dist_block(d, buf + b * WT_BLOCK, p + P_EFFECT, (int)want);
}

void a_dist_reset(struct mt_dist* d, const float* p, uint32_t on) { dist_reset(d, p + P_EFFECT, (int)on); }

struct mt_fx* a_fx_claim(const void* voice) { return fx_claim(voice); }

void a_fx_release(const void* voice) { fx_release(voice); }

/* rate_hz, crush_bits, crush_hz and lfo-free helpers: v in, result out, through memory */
void a_values(const float* in, float* out)
{
    out[0] = rate_hz(in[0]);
    out[1] = crush_bits(in[1]);
    out[2] = crush_hz(in[2]);
}

} // extern "C"
