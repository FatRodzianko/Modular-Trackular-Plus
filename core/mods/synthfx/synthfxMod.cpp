// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Synth FX's slots in the audio interrupt (synthfxMod.h): begin and run, and the per-slot state
// the main loop publishes to them. Everything here but pageShown and the main loop's four
// (publish, retire, retired, published) runs in the audio interrupt: no heap, no SD, single
// precision. In flash: it runs once per part; the plugins' own process is in ITCM.

#include "modsBuild.h"

#if MT_HAS_SYNTHFX

#include "synthfxMod.h"

#include "synthfxPlugins.h"

#include "PolySynth/Patch.h"
#include "PolySynth/config.h"
#if MT_HAS_SYNTHS
#include "synths/synthsMod.h"
#endif

#include <cr_section_macros.h>

namespace mods
{
namespace synthfx
{

static_assert(polysynth::kAudioBufferSize == PTSFX_BLOCK, "the plugins run on CE's synth block");

namespace
{

// A slot as the interrupt runs it. The main loop writes id and widens before phase; the
// interrupt moves phase on (fadeIn -> on, leaving -> gone). Single core: the main loop never
// runs while the interrupt does, and each field is one byte.
enum Phase : uint8_t
{
    kGone,      // nothing runs (the id may still be the last one, for the main loop to free)
    kFadeIn,    // runs, from dry to its output over this block
    kOn,
    kLeaving,   // runs, from its output to dry over this block, then gone
};

struct Run
{
    volatile int8_t id;
    volatile uint8_t phase;
    volatile uint8_t widens;
};

Run runs[kParts][kSlots] = { { { -1, kGone, 0 }, { -1, kGone, 0 } },
                             { { -1, kGone, 0 }, { -1, kGone, 0 } },
                             { { -1, kGone, 0 }, { -1, kGone, 0 } } };

// The dry signal kept over a fading block (the interrupt only)
__NOINIT(EXTERNAL_RAM) float dryL[PTSFX_BLOCK];
__NOINIT(EXTERNAL_RAM) float dryR[PTSFX_BLOCK];

bool validSlot(int part, int slot)
{
    return part >= 0 && part < kParts && slot >= 0 && slot < kSlots;
}

// WAVE and PHASE have their effects inside
inline bool hasOwnEffects(const polysynth::Patch* patch)
{
#if MT_HAS_SYNTHS
    return synths::ownsLayout(patch->getMetadata().instrumentTypeId);
#else
    (void)patch;
    return false;
#endif
}

inline bool running(const Run& r)
{
    return r.phase != kGone;
}

void copy(float* to, const float* from)
{
    for (uint32_t n = 0; n < PTSFX_BLOCK; n++)
    {
        to[n] = from[n];
    }
}

// x from the plugin's output towards dry (out true) or from dry towards the output, over the block
void fade(float* x, const float* dry, bool out)
{
    const float step = 1.0f / static_cast<float>(PTSFX_BLOCK);
    for (uint32_t n = 0; n < PTSFX_BLOCK; n++)
    {
        const float t = static_cast<float>(n + 1u) * step; // the last sample is all the new side
        const float wet = out ? 1.0f - t : t;
        x[n] = dry[n] + wet * (x[n] - dry[n]);
    }
}

} // namespace

bool pageShown(const polysynth::Patch& patch)
{
    return mods::on(Mod::synthfx) && !hasOwnEffects(&patch);
}

// ---- the main loop's side -------------------------------------------------------------------

void publish(int part, int slot, int id, bool widens)
{
    if (!validSlot(part, slot))
    {
        return;
    }
    Run& r = runs[part][slot];
    r.phase = kGone; // first, so the interrupt never sees the new id with the old phase
    r.id = static_cast<int8_t>(id);
    r.widens = widens ? 1 : 0;
    r.phase = id >= 0 ? kFadeIn : kGone;
}

void retire(int part, int slot)
{
    if (validSlot(part, slot) && running(runs[part][slot]))
    {
        runs[part][slot].phase = kLeaving;
    }
}

bool retired(int part, int slot)
{
    return !validSlot(part, slot) || !running(runs[part][slot]);
}

int published(int part, int slot)
{
    return validSlot(part, slot) ? runs[part][slot].id : -1;
}

// ---- the audio interrupt --------------------------------------------------------------------

Insert begin(int part, const polysynth::Patch* patch, bool engineStereo)
{
    const Insert dry = { false, engineStereo };
    if (part < 0 || part >= kParts)
    {
        return dry;
    }
    const Run* r = runs[part];
    if (!running(r[0]) && !running(r[1]))
    {
        return dry;
    }
    if (!mods::on(Mod::synthfx) || patch == nullptr || hasOwnEffects(patch))
    {
        for (int slot = 0; slot < kSlots; slot++) // not run: one letting go is gone now
        {
            if (runs[part][slot].phase == kLeaving)
            {
                runs[part][slot].phase = kGone;
            }
        }
        return dry;
    }
    // A mono part goes out as stereo while a widening plugin runs in a slot, fading out too
    const bool widens = (running(r[0]) && r[0].widens) || (running(r[1]) && r[1].widens);
    return { true, engineStereo || widens };
}

void run(const Insert& insert, int part, float* left, float* right, bool engineStereo)
{
    if (!insert.on)
    {
        return;
    }
    bool stereoNow = engineStereo; // whether right holds the part's right side yet
    for (int slot = 0; slot < kSlots; slot++)
    {
        Run& r = runs[part][slot];
        const uint8_t phase = r.phase;
        if (phase == kGone)
        {
            continue;
        }
        const bool widens = r.widens != 0;
        float* R = (stereoNow || widens) ? right : nullptr;
        const bool fading = phase == kFadeIn || phase == kLeaving;
        if (fading)
        {
            copy(dryL, left);
            if (R != nullptr)
            {
                copy(dryR, stereoNow ? right : left);
            }
        }
        const bool ran = plugins::process(r.id, left, R, stereoNow ? 2u : 1u);
        if (R != nullptr && !stereoNow)
        {
            if (!ran) // bypassed or gone: the right side is still to be made
            {
                copy(right, left);
            }
            stereoNow = true;
        }
        if (fading)
        {
            fade(left, dryL, phase == kLeaving);
            if (R != nullptr)
            {
                fade(right, dryR, phase == kLeaving);
            }
            r.phase = phase == kLeaving ? kGone : kOn;
        }
    }
    if (insert.stereo && !stereoNow) // declared stereo, nothing widened this block
    {
        copy(right, left);
    }
}

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX
