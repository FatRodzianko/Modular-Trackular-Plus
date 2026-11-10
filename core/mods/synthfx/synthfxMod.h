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
// follow the part, not its patch (a closed engine's patch can't hold them). They are kept in
// the project's mods.bin (record 16) and edited on the patch editor's Synth FX rows (button 3,
// on CE's engines with the mod On).

#ifndef MODS_SYNTHFX_SYNTHFXMOD_H_
#define MODS_SYNTHFX_SYNTHFXMOD_H_

#include "mods.h"

#if MT_HAS_SYNTHFX

#include "modsStore.h"

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
constexpr uint8_t kRecordVersion = 1;

constexpr int kParts = 3;             // CE's kSynthParts
constexpr size_t kSettings = 10;      // synthFx.h's ten, FXP_EFFECT to FXP_LEVEL

// Each part's settings. The audio interrupt reads them; the main loop writes them, one
// 32-bit store at a time. Every effect starts Off, the rest at WAVE's Init values.
extern float settings[kParts][kSettings];

// The record: kParts x kSettings bytes, part by part in synthFx.h's order, each a setting's
// step (stepCount below): a list's item, or 1 % for the 0-100 % ones and Feedback, or 0.5 dB
// for Level. Placement is always Post (the insert has no Pre).
constexpr uint16_t kRecordSize = kParts * kSettings;

// Main loop. The project's record into settings: every effect Off if it has none, a damaged
// one, or a value out of range (that value only).
void load(const Store& store);

// Main loop, after settings may have been edited: stores them if they differ from the
// record (a project without one counts as all Init), and returns whether they did.
bool storeIfEdited(Store& store);

// A setting's steps: how many, which one a value is on (nearest; NaN gives Init), its value,
// and Init's.
int stepCount(int setting);
int stepOf(int setting, float value);
float valueOf(int setting, int step);
int initStep(int setting);

// The patch editor's rows: the Distortion's four (Distortion, Gain, Tone, Level), then the
// effect's five (Effect, Rate, Depth, Feedback, FX Mix), as WAVE's FX section has them
// without Placement. Main loop.
constexpr int kRows = 9;
int rowSetting(int row);                    // its index in settings[part]
bool rowIsList(int row);                    // Distortion and Effect
bool rowBipolar(int row);                   // Feedback
bool rowRenames(int row);                   // Distortion: it renames Gain and Tone, and their texts
int rowStep(int part, int row);
void setRowStep(int part, int row, int step); // clamped to the row's steps

// Firmware only (synthfxPage.cpp). A row's name (Gain and Tone are Bits and Rate for the Bit
// Crusher, as on WAVE) and the text for one of its steps.
const char* rowName(int part, int row);
void rowText(int part, int row, int step, char* out, size_t size);

// Firmware only: whether the patch editor offers the rows for this part's patch (the mod On,
// and not WAVE or PHASE, whose own FX section has them).
bool pageShown(const polysynth::Patch& patch);

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
