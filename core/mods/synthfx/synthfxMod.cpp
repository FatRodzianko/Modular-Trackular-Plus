// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Synth FX's slots in the audio interrupt (synthfxMod.h): begin, run and runPre, and the per-slot
// state the main loop publishes to them; and the firmware's main-loop side that needs CE's synth
// (pageShown, partIsWave, convertOldPatches). Everything but those and the main loop's four
// (publish, retire, retired, published) runs in the audio interrupt: no heap, no SD, single
// precision. In flash: it runs once per part, or per WAVE voice for runPre; the plugins' own
// process is in ITCM.

#include "modsBuild.h"

#if MT_HAS_SYNTHFX

#include "synthfxMod.h"

#include "synthfxPlugins.h"

#include "PolySynth/Patch.h"
#include "PolySynth/config.h"
#if MT_HAS_SYNTHS
#include "synths/synthsMod.h"
#endif

#include "TrackerImpl.h"
#include "config.h"
#include "fileManager.h"
#include "interruptsControl.h"
#include <cr_section_macros.h>
#include <cstring>

namespace mods
{
namespace synthfx
{

static_assert(polysynth::kAudioBufferSize == PTSFX_BLOCK, "the plugins run on CE's synth block");

namespace
{

static_assert(kVoices == kSynthMaxVoices, "a Pre slot has an instance for each voice");

// A slot as the interrupt runs it. The main loop writes the ids, count and widens before phase;
// the interrupt moves phase on (fadeIn -> on, leaving -> gone). Single core: the main loop never
// runs while the interrupt does, and each field is one byte. id[0] runs on the part; a Pre slot
// (count kVoices) gives voice v id[v], on a WAVE part (and id[0] on the part elsewhere).
enum Phase : uint8_t
{
    kGone,      // nothing runs (the id may still be the last one, for the main loop to free)
    kFadeIn,    // runs, from dry to its output over this block
    kOn,
    kLeaving,   // runs, from its output to dry over this block, then gone
};

struct Run
{
    volatile int8_t id[kVoices];
    volatile uint8_t phase;
    volatile uint8_t widens;
    volatile uint8_t count;

    constexpr Run() : id{ -1, -1, -1, -1, -1, -1, -1, -1 }, phase(kGone), widens(0), count(0) {}
};

Run runs[kParts][kSlots];

// What begin found, for runPre: the part being rendered, and whether each part plays WAVE with
// the mod On this block (so its Pre slots run in the voices)
volatile int8_t renderedPart = -1;
volatile bool preInVoices[kParts] = { false, false, false };

// The voices' seats (runPre): which voice holds each, and the part's block it last rendered in
struct Seat
{
    const void* voice;
    uint32_t block;
};
Seat seats[kParts][kVoices];
uint32_t partBlock[kParts] = { 0, 0, 0 }; // begin counts each part's blocks

// The voice's seat this block, and whether it is new to it; -1 if every seat is in use
int seatFor(int part, const void* voice, bool* taken)
{
    const uint32_t now = partBlock[part];
    Seat* s = seats[part];
    for (int i = 0; i < kVoices; i++)
    {
        if (s[i].voice == voice && now - s[i].block <= 1u)
        {
            s[i].block = now;
            *taken = false;
            return i;
        }
    }
    for (int i = 0; i < kVoices; i++)
    {
        if (s[i].voice == nullptr || now - s[i].block > 1u)
        {
            s[i].voice = voice;
            s[i].block = now;
            *taken = true;
            return i;
        }
    }
    return -1;
}

// The dry signal kept over a fading block (the interrupt only)
__NOINIT(EXTERNAL_RAM) float dryL[PTSFX_BLOCK];
__NOINIT(EXTERNAL_RAM) float dryR[PTSFX_BLOCK];

bool validSlot(int part, int slot)
{
    return part >= 0 && part < kParts && slot >= 0 && slot < kSlots;
}

// A setting the patch doesn't have: NaN (an Effect or Distortion of none, a knob at its default)
float noValue()
{
    const uint32_t bits = 0x7FC00000u;
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

// WAVE and PHASE: stereo out, but both sides the same (the slots take them as mono)
inline bool sidesAlike(const polysynth::Patch* patch)
{
#if MT_HAS_SYNTHS
    return synths::ownsLayout(patch->getMetadata().instrumentTypeId);
#else
    (void)patch;
    return false;
#endif
}

inline bool isWave(const polysynth::Patch* patch)
{
#if MT_HAS_SYNTHS
    return patch->getMetadata().instrumentTypeId == synths::kWaveTypeId;
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

bool pageShown(const polysynth::Patch& /*patch*/)
{
    return mods::on(Mod::synthfx);
}

bool partIsWave(int part)
{
    if (part < 0 || part >= kParts)
    {
        return false;
    }
    polysynth::Patch& patch = TrackerImpl::getInstance()->getSynth().getPatch(part);
    return isWave(&patch);
}

void convertOldPatches()
{
#if MT_HAS_SYNTHS
    // not while a project opens: its record may still be on its way (load), after the patches
    if (newFileManager.getStatus() != fmIdle || newFileManager.getCurrentOperation() != fmNoOperation)
    {
        return;
    }
    Synth& synth = TrackerImpl::getInstance()->getSynth();
    for (int part = 0; part < kParts; part++)
    {
        polysynth::Patch& patch = synth.getPatch(part);
        const int type = patch.getMetadata().instrumentTypeId;
        if (!synths::ownsLayout(type))
        {
            continue;
        }
        float fx[kOldCount];
        for (int k = 0; k < kOldCount; k++)
        {
            const auto v = patch.getParamValue(kOldFirstParam + k);
            fx[k] = v ? static_cast<float>(*v) : noValue();
        }
        if (!oldPatchHasEffects(fx))
        {
            continue;
        }
        fromOldPatch(part, fx, type == synths::kWaveTypeId);
        // the patch's own off, so this happens once: Effect None, Distortion Off
        DISABLE_AUDIO_INTERRUPTS();
        patch.setParamValue(kOldFirstParam + kOldEffect, 0, polysynth::Patch::ParamChangeSource::kPatchAssign);
        patch.setParamValue(kOldFirstParam + kOldDist, 0, polysynth::Patch::ParamChangeSource::kPatchAssign);
        ENABLE_AUDIO_INTERRUPTS();
        synth.setPatchDirty(part);
        remakeSoon(part);
        mods::projectSettingEdited();
    }
#endif
}

// ---- the main loop's side -------------------------------------------------------------------

void publish(int part, int slot, const int8_t* ids, int count, bool widens)
{
    if (!validSlot(part, slot))
    {
        return;
    }
    Run& r = runs[part][slot];
    r.phase = kGone; // first, so the interrupt never sees the new ids with the old phase
    count = ids == nullptr || count < 0 ? 0 : (count > kVoices ? kVoices : count);
    for (int v = 0; v < kVoices; v++)
    {
        r.id[v] = v < count ? ids[v] : static_cast<int8_t>(-1);
    }
    r.count = static_cast<uint8_t>(count);
    r.widens = widens ? 1 : 0;
    r.phase = count > 0 && ids[0] >= 0 ? kFadeIn : kGone;
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
    return validSlot(part, slot) ? runs[part][slot].id[0] : -1;
}

// ---- the audio interrupt --------------------------------------------------------------------

Insert begin(int part, const polysynth::Patch* patch, bool engineStereo)
{
    const Insert dry = { false, engineStereo, engineStereo };
    if (part < 0 || part >= kParts)
    {
        renderedPart = -1;
        return dry;
    }
    renderedPart = static_cast<int8_t>(part);
    preInVoices[part] = false;
    partBlock[part]++;
    const Run* r = runs[part];
    if (!running(r[0]) && !running(r[1]))
    {
        return dry;
    }
    if (!mods::on(Mod::synthfx) || patch == nullptr)
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
    preInVoices[part] = isWave(patch);
    // A mono part goes out as stereo while a widening plugin runs in a slot, fading out too
    const bool widens = (running(r[0]) && r[0].widens) || (running(r[1]) && r[1].widens);
    return { true, engineStereo || widens, engineStereo && !sidesAlike(patch) };
}

void run(const Insert& insert, int part, float* left, float* right, bool engineStereo)
{
    if (!insert.on)
    {
        return;
    }
    bool stereoNow = engineStereo && insert.inStereo; // whether right holds the part's right side yet
    for (int slot = 0; slot < kSlots; slot++)
    {
        Run& r = runs[part][slot];
        const uint8_t phase = r.phase;
        if (phase == kGone)
        {
            continue;
        }
        if (preInVoices[part] && r.count > 1) // the voices ran it (runPre): its fade moves on
        {
            if (phase == kFadeIn || phase == kLeaving)
            {
                r.phase = phase == kLeaving ? kGone : kOn;
            }
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
        const bool ran = plugins::process(r.id[0], left, R, stereoNow ? 2u : 1u);
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

void runPre(const void* voice, float* buf, bool fresh)
{
    const int part = renderedPart;
    if (part < 0 || part >= kParts || !preInVoices[part] || voice == nullptr || buf == nullptr)
    {
        return;
    }
    bool taken = false;
    const int seat = seatFor(part, voice, &taken);
    if (seat < 0)
    {
        return;
    }
    fresh = fresh || taken;
    for (int slot = 0; slot < kSlots; slot++)
    {
        const Run& r = runs[part][slot];
        const uint8_t phase = r.phase;
        if (phase == kGone || r.count <= 1)
        {
            continue;
        }
        const int id = r.id[seat];
        if (id < 0)
        {
            continue;
        }
        if (fresh)
        {
            plugins::reset(id);
        }
        const bool fading = phase == kFadeIn || phase == kLeaving;
        if (fading)
        {
            copy(dryL, buf);
        }
        plugins::process(id, buf, nullptr, 1u);
        if (fading)
        {
            fade(buf, dryL, phase == kLeaving);
        }
    }
}

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX
