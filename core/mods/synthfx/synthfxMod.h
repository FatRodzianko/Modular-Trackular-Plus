// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// synthfx: Synth FX on every synth part. Each part has two effect slots, FX1 then FX2, and each
// slot runs a plugin from /SynthFX/ (synthfxPlugins.h), or nothing. One part can't have the same
// plugin in both slots; two parts can. The slots run on what the part's engine has rendered, in
// PolySynth::update, before the sends and the fader, so both get the result, and inside CE's
// synth time budget (its check comes after them). Any engine works, and every engine is a .psyn
// plugin from /System/Synths/: CE's (closed, licence section 2(c)) and ours (WAVE, PHASE) alike are only
// heard from outside: the slots work on the part's output buffer, never inside an engine.
//
// A mono part stays mono until a slot holds a plugin that widens (WIDENS: the Chorus, Flanger and
// Phaser of the default pack), then it goes out as stereo (one more audio block). A stereo
// engine's plugins get both sides (CE's factory one: PERC; an engine says so in its
// metadata). An engine that renders its own sends (PERC) has only its main output in the slots.
//
// WAVE and PHASE (core/mods/synths) have no effects of their own: these slots are theirs. They go
// out as stereo, but their two sides are the same, so the slots take them as a mono part's. A
// patch of theirs saved before has its old effects (parameters 62-71) turned into the part's
// slots once, when it is the part's patch (convertOldPatches). Every slot runs on the part: the
// Placement Pre that WAVE had while it was built in (a slot in each voice, before its filter)
// can't reach inside a .psyn, so a record's or an old patch's Pre is Post now.
//
// A slot fades: in over the first block of a new plugin, out over the last block of the one it
// lets go. Off, the slots are bypassed and kept. Each part's slots follow the part, not its patch
// (a .psyn engine's patch can't hold them): they are in the project's mods.bin (record 16) and
// edited on the patch editor's Synth FX rows (button 3 with the mod On; on a sample slot's row
// button 3 is CE's Load WAV, and its label says which).

#ifndef MODS_SYNTHFX_SYNTHFXMOD_H_
#define MODS_SYNTHFX_SYNTHFXMOD_H_

#include "mods.h"

#if MT_HAS_SYNTHFX

#include "modsStore.h"

#include "ptsfx.h"

#include <cstddef>
#include <cstdint>

namespace polysynth
{
class Patch;
}

namespace mods
{
namespace synthfx
{

constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::synthfx) + 1;
constexpr uint8_t kRecordVersion = 3;

constexpr int kParts = 3;             // CE's kSynthParts
constexpr int kSlots = 2;             // FX1, FX2
constexpr size_t kKnobs = 12;         // PTSFX_MAX_PARAMS
constexpr size_t kFileSize = 48;      // plugins::kFileNameSize: a file name in /SynthFX/ with its 0

// A slot's setting: its plugin's file name ("" for none) and its knobs' values. A knob that is NaN
// takes the plugin's default when the plugin is made; knobs past the plugin's own count are kept
// as they are. Main loop only: the audio interrupt never reads these.
struct Slot
{
    char file[kFileSize];
    float knobs[kKnobs];
};
extern Slot slots[kParts][kSlots];

// The record, version 3: for each part, FX1 then FX2, the file name (kFileSize bytes, ended by a
// 0 and zeros after it) and the twelve knobs (little-endian floats); then each slot's placement,
// one byte in the same order (1 Pre, 0 Post). Placement Pre is gone: a 1 reads as Post, and every
// slot is written 0, so a record stays the size and version earlier builds read.
constexpr size_t kSlotBytes = kFileSize + 4 * kKnobs;
constexpr uint16_t kRecordSizeV2 = static_cast<uint16_t>(kParts * kSlots * kSlotBytes);
constexpr uint16_t kRecordSize = static_cast<uint16_t>(kRecordSizeV2 + kParts * kSlots);
// Version 2: version 3 without the placements (every slot Post).
constexpr uint8_t kRecordVersionV2 = 2;
// Version 1 (the built-in effects): for each part the ten settings' steps, in synthFx.h's order.
constexpr uint8_t kRecordVersionV1 = 1;
constexpr uint16_t kRecordSizeV1 = 30;

// The default pack's files, which a version 1 record turns into
constexpr char kOverdrive[] = "Overdrive.ptsfx";
constexpr char kDistortion[] = "Distortion.ptsfx";
constexpr char kBitCrusher[] = "BitCrusher.ptsfx";
constexpr char kChorus[] = "Chorus.ptsfx";
constexpr char kFlanger[] = "Flanger.ptsfx";
constexpr char kPhaser[] = "Phaser.ptsfx";

// Boot (the registry's init): every slot empty. The slots are in SDRAM, which startup doesn't clear.
void init();

// Main loop. The project's record into slots: both empty if it has none or a damaged one, a
// version 1 record converted (its Distortion in FX1, its modulation effect in FX2, the same
// values), version 2 as version 3. A bad file name empties its slot; a knob that isn't finite
// is NaN (the default); a plugin in both slots of a part stays in FX1 only. Marks the slots for
// the main loop to make their plugins again (takeLoaded).
void load(const Store& store);

// Main loop, after slots may have been edited: stores them if they differ from the record (a
// project without one counts as all empty), as version 3, and returns whether they did.
bool storeIfEdited(Store& store);

// Whether load ran since the last call.
bool takeLoaded();

// Main loop: a slot emptied, or set to a file with every knob NaN.
void clearSlot(int part, int slot);
void setSlotFile(int part, int slot, const char* file);

// WAVE's and PHASE's old effects: their patch parameters 62-71 (synthfx/synthFx.h's ten settings),
// as floats. Whether a patch has any (an Effect or a Distortion), and the part's two slots from
// them, as a version 1 record's are (the Distortion in FX1, whatever its Placement; the modulation
// effect in FX2). Main loop.
enum : int
{
    kOldEffect, kOldRate, kOldDepth, kOldMix, kOldFeedback, kOldDist, kOldGain, kOldTone, kOldPlacement,
    kOldLevel, kOldCount
};
constexpr int kOldFirstParam = 62;
bool oldPatchHasEffects(const float* fx);
void fromOldPatch(int part, const float* fx);
// Whether file (case aside) is the other slot's on this part.
bool usedByOtherSlot(int part, int slot, const char* file);

// What begin found for a part this block: whether its slots run, whether the part goes out as
// stereo, and whether its right side is the engine's own (PERC), not a copy of the left (the mono
// engines, and WAVE and PHASE).
struct Insert
{
    bool on;
    bool stereo;
    bool inStereo;
};

// Audio interrupt (PolySynth::update), for each part before its audio blocks are taken.
// engineStereo: the engine renders stereo (PERC). With the mod Off, or both slots empty, it is
// a few tests and the part plays as CE's.
Insert begin(int part, const polysynth::Patch* patch, bool engineStereo);

// Audio interrupt, before CE's silent-part check (PolySynth::update): whether a slot of the part
// still runs, a plugin or one fading out. CE's 0238786 skips rendering a part whose voices are
// silent; while a slot runs the part renders as before, so the plugin's tail plays out and its
// delay lines don't hold an old block for the next note.
bool holdsPart(int part);

// Audio interrupt, after the engine has rendered the part's block into left (and right, if
// engineStereo): the slots, in place. right is written whenever insert.stereo.
void run(const Insert& insert, int part, float* left, float* right, bool engineStereo);

// Audio interrupt, right after run, when CE's synth time limit (2.1 ms) has tripped with the
// part's block: each of its slots whose plugin was over the time guard's budget in that block
// is bypassed at once. Otherwise CE would silence the synth for 32 blocks each time, until the
// guard had seen kOverBlocks such blocks in a row.
void overSynthLimit(int part);

// Firmware only: whether the patch editor offers the rows (the mod On).
bool pageShown(const polysynth::Patch& patch);

// Firmware only, main loop (mods::update, before updateSlots): a WAVE or PHASE part whose patch
// has old effects gets them as its slots, and the patch's are cleared (the patch and the project
// marked for saving), so it happens once.
void convertOldPatches();

// ---- the slots' plugins (synthfxSlots.cpp, firmware and the emulator) ----------------------

// What a slot's plugin is doing, for the page
enum class SlotState : uint8_t
{
    empty,       // no file
    loading,     // being made, or the last one still fading out
    running,     // made and running
    bypassed,    // made, and stopped by the time guard
    failed,      // the file is set but no plugin is made (lastStatus says why)
};

// Main loop, every pass (mods::update): makes the slots' plugins after a load, and lets go of
// the ones no slot has. Returns whether a slot changed; takeSlotsChanged says so to the page
// (once), which redraws.
bool updateSlots();
bool takeSlotsChanged();
// Main loop: the slot's plugin made again now, from its setting (the page's pick: a user's
// choice, so safe mode doesn't skip it; a project's: fromProject). Soon: both of the part's
// slots, as a project's, on the next updateSlots once the plugins have started.
void remake(int part, int slot, bool fromProject);
void remakeSoon(int part);
// Main loop: the page's pick from a slot's list, made once the encoder has rested kPickRestMs (a
// user's choice), so turning past a plugin doesn't make it. Until then the old plugin plays on and
// the slot is loading, with no knob rows.
constexpr uint32_t kPickRestMs = 400;
void remakeWhenStill(int part, int slot);
// Main loop: the slots that failed are tried again (the page opens; the card may have changed).
void retryFailed();
SlotState slotState(int part, int slot);
uint8_t slotStatus(int part, int slot);    // plugins::Status of the last try
int slotInstance(int part, int slot);      // plugins' id, or -1

// ---- the patch editor's rows (synthfxPage.cpp, firmware and the emulator) -------------------
// FX1, its plugin's knobs; the same for FX2. Main loop only.

constexpr int kMaxRows = kSlots * (1 + static_cast<int>(kKnobs));
constexpr int kMaxChoices = 66;        // None, the catalog's 64, the slot's own file if not listed
constexpr uint32_t kListKnobSteps = 16; // a PTSFX_PARAM_LIST knob with up to this many is a list
constexpr int kStepsPerTurn = 200;     // a knob with more steps moves several per encoder tick

int rowCount(int part);
void rowName(int part, int row, char* out, size_t size);
void rowText(int part, int row, char* out, size_t size);   // the value shown in the row
bool rowIsList(int part, int row);                         // a slot, or a short list knob
bool rowBipolar(int part, int row);                        // a knob from -x to x
int rowSteps(int part, int row);                           // a slot's choices, or a knob's steps
int rowStep(int part, int row);
void rowItemText(int part, int row, int step, char* out, size_t size); // a list's item
int rowEncoderSteps(int part, int row, int ticks);
// An edit: a slot picks its choice (and its plugin is made again: a user's pick), a knob its
// step. Returns whether the rows changed (a slot's plugin and so its knob rows).
bool setRowStep(int part, int row, int step);
bool defaultRow(int part, int row);                        // a slot: None; a knob: its default
// The rows open: the card listed again, and the slots that failed tried again.
void pageOpened();

// Audio-interrupt side of a slot, the main loop's view (synthfxMod.cpp). id: the slot's plugin
// instance, or -1 for none.
void publish(int part, int slot, int id, bool widens); // fades in from its next block
void retire(int part, int slot);                       // fades out over its next block
bool retired(int part, int slot);                      // faded out, or never was in
int published(int part, int slot);                     // the id the interrupt runs, or -1

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX

#endif // MODS_SYNTHFX_SYNTHFXMOD_H_
