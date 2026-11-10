// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// synthfx: Synth FX, the Synths effects (synthFx.h: a Distortion, then Chorus, Flanger or
// Phaser) on every synth part. They run on what the part's engine has rendered, in
// PolySynth::update, before the sends and the fader, so both get the result. CE's closed
// engines are only heard from outside: the insert works on their output buffer, never inside
// them. Parts playing WAVE or PHASE are skipped: those have the same effects inside, with
// Placement Pre and macros.
//
// A mono part (every CE engine but PERC) stays mono until a modulation effect plays, then it
// is sent as stereo (one more audio block while it does). PERC is stereo: the Distortion runs
// on each side. PERC renders its own sends, so only its main output gets the effects.
//
// Off, the insert is bypassed and the settings kept. Each part has its own settings, which
// follow the part, not its patch (a closed engine's patch can't hold them).

#ifndef MODS_SYNTHFX_SYNTHFXMOD_H_
#define MODS_SYNTHFX_SYNTHFXMOD_H_

#include "mods.h"

#if MT_HAS_SYNTHFX

#include <cstddef>
#include <cstdint>

namespace polysynth
{
class Patch;
}

namespace mtfx
{
struct mt_fx;
}

namespace mods
{
namespace synthfx
{

constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::synthfx) + 1;
constexpr uint8_t kRecordVersion = 0; // nothing in mods.bin yet

constexpr int kParts = 3;             // CE's kSynthParts
constexpr size_t kSettings = 10;      // synthFx.h's ten, FXP_EFFECT to FXP_LEVEL

// Each part's settings. The audio interrupt reads them; the main loop writes them, one
// 32-bit store at a time. Every effect starts Off, the rest at WAVE's Init values.
extern float settings[kParts][kSettings];

// What begin found for a part this block: its effect state (nullptr: the part plays dry)
// and whether the part goes out as stereo.
struct Insert
{
    mtfx::mt_fx* state;
    bool stereo;
};

// Audio interrupt (PolySynth::update), for each part before its audio blocks are taken.
// engineStereo: the engine renders stereo (PERC). With the mod Off, or nothing chosen and
// nothing fading out, it is one test and the part plays as CE's.
Insert begin(int part, const polysynth::Patch* patch, bool engineStereo);

// Audio interrupt, after the engine has rendered the part's block into left (and right, if
// engineStereo): the effects, in place. right is written whenever insert.stereo.
void run(const Insert& insert, int part, float* left, float* right, bool engineStereo);

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX

#endif // MODS_SYNTHFX_SYNTHFXMOD_H_
