/* Modular-Trackular mods. SPDX-License-Identifier: MIT
 *
 * Level A: the reference, sy9's engines and chorus (Modular-Trackular's patch/synths, the
 * code the 1.2.0 synths patch ran) behind plain C entry points, as ours.cpp has the port.
 * Its parameters are sy9's slot copy: the test builds it from a parameter-id array with
 * sy9's own maps, r_wt_param_slot and r_ph_param_slot. ctl is { tempo, bend, volume }. */

#include <stddef.h>
#include <stdint.h>

#include "wt_engine.h"
#include "ph_engine.h"
#include "wt_fx.h"

const uint32_t r_wave_size = sizeof(struct wt_voice);
const uint32_t r_phase_size = sizeof(struct ph_voice);
const uint32_t r_fx_size = sizeof(struct wt_fx);
const uint32_t r_wave_params = WT_PARAMS;
const uint32_t r_phase_params = PH_PARAMS;
const uint32_t r_slot_params = WAVE_PARAMS;
const uint8_t *const r_wt_param_slot = wt_param_slot;
const uint8_t *const r_ph_param_slot = ph_param_slot;

void r_wave_init(struct wt_voice *v) { wt_voice_init(v, 0); }

void r_wave_on(struct wt_voice *v, const float *p, uint32_t note, const float *ctl)
{
    v->volume = ctl[2];
    wt_note_on(v, p, note);
}

void r_wave_off(struct wt_voice *v) { wt_note_off(v); }

uint32_t r_wave_sounding(const struct wt_voice *v) { return (uint32_t)wt_voice_sounding(v); }

void r_wave_render(struct wt_voice *v, const float *p, float *out, const float *ctl, uint32_t blocks)
{
    for (uint32_t b = 0; b < blocks; b++) {
        v->volume = ctl[2];
        v->bend = ctl[1];
        wt_voice_render(v, p, out + b * WT_BLOCK, ctl[0]);
    }
}

void r_phase_init(struct ph_voice *v) { ph_voice_init(v, 0); }

void r_phase_on(struct ph_voice *v, const float *p, uint32_t note, const float *ctl)
{
    v->volume = ctl[2];
    ph_note_on(v, p, note);
}

void r_phase_off(struct ph_voice *v) { ph_note_off(v); }

uint32_t r_phase_sounding(const struct ph_voice *v) { return (uint32_t)ph_voice_sounding(v); }

void r_phase_render(struct ph_voice *v, const float *p, float *out, const float *ctl, uint32_t blocks)
{
    for (uint32_t b = 0; b < blocks; b++) {
        v->volume = ctl[2];
        v->bend = ctl[1];
        ph_voice_render(v, p, out + b * WT_BLOCK, ctl[0]);
    }
}

/* sy9's chorus: fxp is its 4 values (Effect, Rate, Depth, Mix) */
void r_fx_run(struct wt_fx *s, const float *fxp, float *L, float *R, uint32_t blocks)
{
    for (uint32_t k = 0; k < FX_PARAMS; k++)
        s->p[k] = fxp[k];
    for (uint32_t b = 0; b < blocks; b++)
        wt_fx_run(s, L + b * WT_BLOCK, R + b * WT_BLOCK);
}
