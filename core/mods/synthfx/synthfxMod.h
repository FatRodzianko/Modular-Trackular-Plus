// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// synthfx: Synth FX on every synth part. Each part has two effect slots, FX1 then FX2, and each
// slot runs a plugin from /SynthFX/ (synthfxPlugins.h), or nothing. One part can't have the same
// plugin in both slots; two parts can. The slots run on what the part's engine has rendered, in
// PolySynth::update, before the sends and the fader, so both get the result. CE's closed engines
// are only heard from outside: the slots work on their output buffer, never inside them.
//
// A mono part (every CE engine but PERC) stays mono until a slot holds a plugin that widens
// (WIDENS: the Chorus, Flanger and Phaser of the default pack), then it goes out as stereo (one
// more audio block). PERC is stereo: its plugins get both sides. PERC renders its own sends, so
// only its main output gets the slots.
//
// WAVE and PHASE (core/mods/synths) have no effects of their own: these slots are theirs. They go
// out as stereo, but their two sides are the same, so the slots take them as a mono part's.
// On WAVE a slot whose plugin allows it (PRE) can have Placement Pre: then it runs in each voice,
// before the filter, one instance per voice (runPre), not on the part. A patch of theirs saved
// before has its old effects (parameters 62-71) turned into the part's slots once, when it is
// the part's patch (convertOldPatches).
//
// A slot fades: in over the first block of a new plugin, out over the last block of the one it
// lets go. Off, the slots are bypassed and kept. Each part's slots follow the part, not its patch
// (a closed engine's patch can't hold them): they are in the project's mods.bin (record 16) and
// edited on the patch editor's Synth FX rows (button 3, on CE's engines with the mod On).

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
constexpr int kVoices = 8;            // CE's kSynthMaxVoices: a Pre slot's instances, one per voice

// A slot's setting: its plugin's file name ("" for none), its knobs' values and its placement. A
// knob that is NaN takes the plugin's default when the plugin is made; knobs past the plugin's
// own count are kept as they are. pre: Placement Pre, which runs only on WAVE and only for a
// plugin that allows it (elsewhere the slot runs on the part, as Post). Main loop only: the audio
// interrupt never reads these.
struct Slot
{
    char file[kFileSize];
    float knobs[kKnobs];
    uint8_t pre;
};
extern Slot slots[kParts][kSlots];

// The record, version 3: for each part, FX1 then FX2, the file name (kFileSize bytes, ended by a
// 0 and zeros after it) and the twelve knobs (little-endian floats); then each slot's placement,
// one byte in the same order (1 Pre, 0 Post).
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
// values), version 2's slots all Post. A bad file name empties its slot; a knob that isn't finite
// is NaN (the default); a plugin in both slots of a part stays in FX1 only. Marks the slots for
// the main loop to make their plugins again (takeLoaded).
void load(const Store& store);

// Main loop, after slots may have been edited: stores them if they differ from the record (a
// project without one counts as all empty), as version 2, and returns whether they did.
bool storeIfEdited(Store& store);

// Whether load ran since the last call.
bool takeLoaded();

// Main loop: a slot emptied, or set to a file with every knob NaN and Post.
void clearSlot(int part, int slot);
void setSlotFile(int part, int slot, const char* file);
void setSlotPre(int part, int slot, bool pre);    // an empty slot stays Post

// WAVE's and PHASE's old effects: their patch parameters 62-71 (synthfx/synthFx.h's ten settings),
// as floats. Whether a patch has any (an Effect or a Distortion), and the part's two slots from
// them, as a version 1 record's are (the Distortion in FX1, Pre if wave and its Placement is Pre;
// the modulation effect in FX2). Main loop.
enum : int
{
    kOldEffect, kOldRate, kOldDepth, kOldMix, kOldFeedback, kOldDist, kOldGain, kOldTone, kOldPlacement,
    kOldLevel, kOldCount
};
constexpr int kOldFirstParam = 62;
bool oldPatchHasEffects(const float* fx);
void fromOldPatch(int part, const float* fx, bool wave);
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

// Audio interrupt, after the engine has rendered the part's block into left (and right, if
// engineStereo): the slots, in place. right is written whenever insert.stereo. A WAVE part's Pre
// slots are not run here (the voices ran them): only their fades move on.
void run(const Insert& insert, int part, float* left, float* right, bool engineStereo);

// Audio interrupt, from a WAVE voice's render (between begin and run, for the part begin was
// last called for): the part's Pre slots on the voice's mono block, in place, with the voice's
// own instances. voice: the voice (its address names it). fresh: it starts a note from idle.
// Each part has kVoices seats, each seat one instance of every Pre slot: a voice that rendered
// last block keeps its seat; any other takes a seat no voice rendered in last block, and that
// seat's instances start again (as they do on fresh).
void runPre(const void* voice, float* buf, bool fresh);

// Firmware only: whether the patch editor offers the rows (the mod On).
bool pageShown(const polysynth::Patch& patch);

// Firmware only, main loop: whether the part plays WAVE (Placement Pre runs there).
bool partIsWave(int part);

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
// the ones no slot has. A Pre slot with a PRE plugin gets one instance per voice (kVoices); its
// first serves as the part's when the part doesn't play WAVE. Returns whether a slot changed; takeSlotsChanged says so to the page
// (once), which redraws.
bool updateSlots();
bool takeSlotsChanged();
// Main loop: the slot's plugin made again now, from its setting (the page's pick: a user's
// choice, so safe mode doesn't skip it; a project's: fromProject). Soon: both of the part's
// slots, as a project's, on the next updateSlots once the plugins have started.
void remake(int part, int slot, bool fromProject);
void remakeSoon(int part);
// Main loop: the slots that failed are tried again (the page opens; the card may have changed).
void retryFailed();
SlotState slotState(int part, int slot);
uint8_t slotStatus(int part, int slot);    // plugins::Status of the last try
int slotInstance(int part, int slot);      // plugins' id, or -1 (a Pre slot: its first voice's)
bool slotRunsPre(int part, int slot);      // made with one instance per voice
// Main loop: a knob set on the slot's instance (slotInstance) copied to its others (a Pre slot's)
void knobEdited(int part, int slot, uint32_t knob);

// ---- the patch editor's rows (synthfxPage.cpp, firmware and the emulator) -------------------
// FX1, its Placement (on WAVE, for a PRE plugin), its plugin's knobs; the same for FX2. Main loop
// only.

constexpr int kMaxRows = kSlots * (2 + static_cast<int>(kKnobs));
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

// Audio-interrupt side of a slot, the main loop's view (synthfxMod.cpp). ids: count instances
// (1, or kVoices for a Pre slot); the first also runs on the part.
void publish(int part, int slot, const int8_t* ids, int count, bool widens); // fades in from its next block
void retire(int part, int slot);                       // fades out over its next block
bool retired(int part, int slot);                      // faded out, or never was in
int published(int part, int slot);                     // the id the interrupt runs, or -1

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX

#endif // MODS_SYNTHFX_SYNTHFXMOD_H_
