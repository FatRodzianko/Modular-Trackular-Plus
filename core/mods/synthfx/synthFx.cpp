// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The synth effects (synthFx.h): the firmware's entry points and the states. The
// arithmetic is synthFxDsp.h's. The Chorus is sy9's, sample for sample.

#include "modsBuild.h"

#if MT_HAS_SYNTHS || MT_HAS_SYNTHFX

#include "synthFx.h"
#define MTFX_SETUP static // as these were before synthFxDsp.h: dist_block's ITCM copy stays its size
#include "synthFxDsp.h"

#include "config.h"
#include <cr_section_macros.h>
#include <cstddef>
#include <cstring>

namespace mtfx
{
/* The arithmetic is in synthFxDsp.h, shared with the default pack's plugins. */

float crush_bits(float v)
{
    return dsp::crush_bits(v);
}

float crush_hz(float v)
{
    return dsp::crush_hz(v);
}

float rate_hz(float v)
{
    return dsp::rate_hz(v);
}

void dist_reset(struct mt_dist *d, const float *fx, int on)
{
    dsp::dist_reset(d, fx, on);
}

AUDIO_CORE_RAM void dist_block(struct mt_dist *d, float *buf, const float *fx, int want)
{
    dsp::dist_block(d, buf, fx, want);
}

/* In flash, not ITCM: it runs once per instrument or part (at most three), dist_block once
 * per voice with Placement Pre, and ITCM is short (CLAUDE.md). */
void fx_block(struct mt_fx *s, const float *fx, float *L, float *R, int distWanted)
{
    dist_block(&s->dist, L, fx, distWanted);

    uint32_t type = list_pick(fx[FXP_EFFECT], FX_TYPES);
    if (!s->running && type == FX_NONE) {
        for (uint32_t n = 0; n < FX_BLOCK; n++)
            R[n] = L[n];
        return;
    }
    if (!s->running)
        dsp::fx_start(s, fx, type);
    dsp::fx_mod<false>(s, fx, type, L, R);
}

void fx_block_stereo(struct mt_fx *s, const float *fx, float *L, float *R, int distWanted)
{
    dist_block(&s->dist, L, fx, distWanted);
    dist_block(&s->dist2, R, fx, distWanted);

    uint32_t type = list_pick(fx[FXP_EFFECT], FX_TYPES);
    if (!s->running && type == FX_NONE)
        return;
    if (!s->running)
        dsp::fx_start(s, fx, type);
    dsp::fx_mod<true>(s, fx, type, L, R);
}

/* ---- the instruments' states ------------------------------------------------------------ */

/* Up to three synth parts play at once, so three states. Who owns each (WAVE and PHASE: an
 * instrument's first voice; Synth FX: a part) is in DTCM (zeroed at start-up); the states
 * themselves, delay lines included, are in SDRAM, which start-up doesn't clear: a new owner
 * clears what it uses. A state whose owner hasn't run for a while (the instrument's first
 * voice changed, or it went away) can be taken over. */
static constexpr uint32_t kStates = 3;
static constexpr uint32_t kStale = 4 * kStates; /* calls by anyone since the owner's last */

struct Owner
{
    const void *who;
    uint32_t last;
};
static Owner owners[kStates];
static uint32_t calls;
__NOINIT(EXTERNAL_RAM) static struct mt_fx states[kStates];

struct mt_fx *fx_claim(const void *owner)
{
    calls++;
    for (uint32_t i = 0; i < kStates; i++)
        if (owners[i].who == owner) {
            owners[i].last = calls;
            return &states[i];
        }
    uint32_t pick = kStates;
    for (uint32_t i = 0; i < kStates && pick == kStates; i++)
        if (owners[i].who == nullptr)
            pick = i;
    for (uint32_t i = 0; i < kStates && pick == kStates; i++)
        if (calls - owners[i].last > kStale)
            pick = i;
    if (pick == kStates)
        return nullptr;
    owners[pick].who = owner;
    owners[pick].last = calls;
    memset(&states[pick], 0, offsetof(struct mt_fx, buf));
    return &states[pick];
}

void fx_release(const void *owner)
{
    for (uint32_t i = 0; i < kStates; i++)
        if (owners[i].who == owner)
            owners[i].who = nullptr;
}

} // namespace mtfx

#endif // MT_HAS_SYNTHS || MT_HAS_SYNTHFX
