// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// synthfx: Synth FX on every synth part. Each part has two effect slots, FX1 then FX2, and each
// slot runs a plugin from /SynthFX/ (synthfxPlugins.h), or nothing. One part can't have the same
// plugin in both slots; two parts can. The slots run on what the part's engine has rendered, in
// PolySynth::update, before the sends and the fader, so both get the result. CE's closed engines
// are only heard from outside: the slots work on their output buffer, never inside them. Parts
// playing WAVE or PHASE are skipped: those have their effects inside, with Placement Pre and
// macros (phase 3 moves them onto the slots).
//
// A mono part (every CE engine but PERC) stays mono until a slot holds a plugin that widens
// (WIDENS: the Chorus, Flanger and Phaser of the default pack), then it goes out as stereo (one
// more audio block). PERC is stereo: its plugins get both sides. PERC renders its own sends, so
// only its main output gets the slots.
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
constexpr uint8_t kRecordVersion = 2;

constexpr int kParts = 3;             // CE's kSynthParts
constexpr int kSlots = 2;             // FX1, FX2
constexpr size_t kKnobs = 12;         // PTSFX_MAX_PARAMS
constexpr size_t kFileSize = 48;      // plugins::kFileNameSize: a file name in /SynthFX/ with its 0

// A slot's setting: its plugin's file name ("" for none) and its knobs' values. A knob that is
// NaN takes the plugin's default when the plugin is made; knobs past the plugin's own count are
// kept as they are. Main loop only: the audio interrupt never reads these.
struct Slot
{
    char file[kFileSize];
    float knobs[kKnobs];
};
extern Slot slots[kParts][kSlots];

// The record, version 2: for each part, FX1 then FX2, the file name (kFileSize bytes, ended by a
// 0 and zeros after it) and the twelve knobs (little-endian floats).
constexpr size_t kSlotBytes = kFileSize + 4 * kKnobs;
constexpr uint16_t kRecordSize = static_cast<uint16_t>(kParts * kSlots * kSlotBytes);
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
// values). A bad file name empties its slot; a knob that isn't finite is NaN (the default); a
// plugin in both slots of a part stays in FX1 only. Marks the slots for the main loop to make
// their plugins again (takeLoaded).
void load(const Store& store);

// Main loop, after slots may have been edited: stores them if they differ from the record (a
// project without one counts as all empty), as version 2, and returns whether they did.
bool storeIfEdited(Store& store);

// Whether load ran since the last call.
bool takeLoaded();

// Main loop: a slot emptied, or set to a file with every knob NaN.
void clearSlot(int part, int slot);
void setSlotFile(int part, int slot, const char* file);
// Whether file (case aside) is the other slot's on this part.
bool usedByOtherSlot(int part, int slot, const char* file);

// What begin found for a part this block: whether its slots run, and whether the part goes
// out as stereo.
struct Insert
{
    bool on;
    bool stereo;
};

// Audio interrupt (PolySynth::update), for each part before its audio blocks are taken.
// engineStereo: the engine renders stereo (PERC). With the mod Off, or both slots empty, it is
// a few tests and the part plays as CE's.
Insert begin(int part, const polysynth::Patch* patch, bool engineStereo);

// Audio interrupt, after the engine has rendered the part's block into left (and right, if
// engineStereo): the slots, in place. right is written whenever insert.stereo.
void run(const Insert& insert, int part, float* left, float* right, bool engineStereo);

// Firmware only: whether the patch editor offers the rows for this part's patch (the mod On,
// and not WAVE or PHASE, whose own FX section has them).
bool pageShown(const polysynth::Patch& patch);

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
// choice, so safe mode doesn't skip it; a project's: fromProject).
void remake(int part, int slot, bool fromProject);
// Main loop: the slots that failed are tried again (the page opens; the card may have changed).
void retryFailed();
SlotState slotState(int part, int slot);
uint8_t slotStatus(int part, int slot);    // plugins::Status of the last try
int slotInstance(int part, int slot);      // plugins' id, or -1

// ---- the patch editor's rows (synthfxPage.cpp, firmware and the emulator) -------------------
// FX1, its plugin's knobs, FX2, its plugin's knobs. Main loop only.

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

// Audio-interrupt side of a slot, the main loop's view (synthfxMod.cpp)
void publish(int part, int slot, int id, bool widens); // fades in from its next block
void retire(int part, int slot);                       // fades out over its next block
bool retired(int part, int slot);                      // faded out, or never was in
int published(int part, int slot);                     // the id the interrupt runs, or -1

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX

#endif // MODS_SYNTHFX_SYNTHFXMOD_H_
