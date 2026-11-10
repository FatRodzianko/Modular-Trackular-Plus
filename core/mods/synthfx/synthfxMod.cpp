// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Synth FX's insert (synthfxMod.h). Everything here runs in the audio interrupt: no heap, no
// SD, single precision. In flash: it runs once per part.

#include "modsBuild.h"

#if MT_HAS_SYNTHFX

#include "synthfxMod.h"

#include "synthFx.h"

#include "PolySynth/Patch.h"
#include "PolySynth/config.h"
#if MT_HAS_SYNTHS
#include "synths/synthsMod.h"
#endif

namespace mods
{
namespace synthfx
{

using namespace mtfx;

static_assert(kSettings == FXP_COUNT, "synthFx.h's settings");
static_assert(polysynth::kAudioBufferSize == FX_BLOCK, "the effects run on CE's synth block");

// Effect None and Distortion Off; Rate, Depth, Mix, Feedback, Gain and Tone at half, Placement
// Post (the insert is always Post), Level 0 dB: WAVE's and PHASE's Init.
#define SYNTHFX_INIT { 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 1.0f, 0.0f }
float settings[kParts][kSettings] = { SYNTHFX_INIT, SYNTHFX_INIT, SYNTHFX_INIT };
#undef SYNTHFX_INIT

// Whether each part holds an effect state. Each one's address is also its owner id for
// fx_claim, which can't be any voice's.
static uint8_t held[kParts];

static inline bool wanted(const float* fx)
{
    return list_pick(fx[FXP_EFFECT], FX_TYPES) != FX_NONE || dist_on(fx);
}

// WAVE and PHASE have these effects inside
static inline bool hasOwnEffects(const polysynth::Patch* patch)
{
#if MT_HAS_SYNTHS
    return synths::ownsLayout(patch->getMetadata().instrumentTypeId);
#else
    (void)patch;
    return false;
#endif
}

static void drop(int part)
{
    if (held[part])
    {
        fx_release(&held[part]);
        held[part] = 0;
    }
}

Insert begin(int part, const polysynth::Patch* patch, bool engineStereo)
{
    const Insert dry = { nullptr, engineStereo };
    if (part < 0 || part >= kParts)
    {
        return dry;
    }
    const float* fx = settings[part];
    const bool on = mods::on(Mod::synthfx);
    if (!held[part] && !(on && wanted(fx)))
    {
        return dry;
    }
    if (!on || patch == nullptr || hasOwnEffects(patch))
    {
        drop(part);
        return dry;
    }
    mt_fx* s = fx_claim(&held[part]);
    if (s == nullptr) // every state taken (three parts with effects already): dry
    {
        held[part] = 0;
        return dry;
    }
    held[part] = 1;
    // A mono part goes out as stereo while a modulation effect runs, from the block it starts
    // in (fx_block starts it whenever one is chosen) until it has faded out.
    return { s, engineStereo || s->running || list_pick(fx[FXP_EFFECT], FX_TYPES) != FX_NONE };
}

void run(const Insert& insert, int part, float* left, float* right, bool engineStereo)
{
    mt_fx* s = insert.state;
    if (s == nullptr)
    {
        return;
    }
    const float* fx = settings[part];
    const int dist = dist_on(fx);
    if (engineStereo)
    {
        fx_block_stereo(s, fx, left, right, dist);
    }
    else
    {
        s->dist2.amount = 0.0f; // a mono part has no right side: off, so fx_idle can be true
        fx_block(s, fx, left, right, dist);
    }
    if (!wanted(fx) && fx_idle(s)) // everything faded out: the state goes back
    {
        drop(part);
    }
}

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX
