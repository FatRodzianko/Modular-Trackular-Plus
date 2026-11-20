// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// resample: the Sample Recorder can play the tracker while it records, and record the
// tracker's own sound. Two buttons on the recorder's setup, saved with the project:
//  - Trk Audio: the tracker sounds during the recorder visit as in normal playback, and a
//    take goes after the loaded samples instead of over them;
//  - Trk Record: the take also holds the tracker's sound, with the Line In. On turns Trk
//    Audio on too, and Trk Audio Off turns it off.
// Design and steps: docs/mt/RESAMPLE_PORT_PLAN.md.
//
// Off: the recorder is CE's.

#ifndef MODS_RESAMPLE_H_
#define MODS_RESAMPLE_H_

#include "mods.h"

#if MT_HAS_RESAMPLE

#include "modsFileIo.h"
#include "modsStore.h"

namespace mods
{
namespace resample
{

constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::resample) + 1;
constexpr uint8_t kRecordVersion = 1;

// The record's one byte, as 1.2.0's resample.bin had it.
constexpr uint8_t kTrkAudioBit = 1u << 0;
constexpr uint8_t kTrkRecordBit = 1u << 1;
constexpr uint8_t kMaxBits = kTrkAudioBit | kTrkRecordBit;

struct Settings
{
    bool trkAudio = false;
    bool trkRecord = false; // only with trkAudio
};

extern Settings settings; // the open project's

// The byte for settings, and settings for a byte: Trk Record without Trk Audio reads as both
// On; a byte over kMaxBits is not a setting (false).
uint8_t toBits(const Settings& s);
bool fromBits(uint8_t bits, Settings* s);

// The project's record into settings: both Off if it has none or a damaged one.
void load(const Store& store);

// A project from stock 1.2.0: its resample.bin into store's record. True if the file was good
// and a button was On, so the project used the mod.
bool importSideFile(FileIo& io, const char* projectFolder, Store& store);

// The recorder's buttons, in the two bottom labels CE leaves empty for every source but Radio:
// Trk Audio for the four Line Ins and the mics, Trk Record for the stereo Line In only (decision
// 9), none for Radio or USB.
enum class Buttons : uint8_t
{
    none,
    trkAudio,
    both,
};
constexpr uint8_t kTrkAudioSlot = 1;  // label[RADIO_COL1]
constexpr uint8_t kTrkRecordSlot = 2; // label[RADIO_COL2]

Buttons buttonsFor(bool lineIn, bool stereo, bool mic);

// A recorder visit: latched when the recorder starts and cleared when it stops, so the project's
// switch, changed in Config, takes effect at the next visit. Nothing runs while it's Off.
void visitStarted(bool modOn);
void visitEnded();
bool inVisit();

// The key under slot pressed with these buttons shown: toggles that setting in s, with Trk
// Record On turning Trk Audio On and Trk Audio Off turning Trk Record Off. False if the slot has
// no button.
bool press(uint8_t slot, Buttons buttons, Settings* s);

// s into store's record. True if the record changed.
bool store(const Settings& s, Store& store);

// A slot's two texts: its title and On/Off. nullptr if the slot has no button.
const char* title(uint8_t slot, Buttons buttons);
const char* value(uint8_t slot, const Settings& s);

} // namespace resample
} // namespace mods

#endif // MT_HAS_RESAMPLE

#endif /* MODS_RESAMPLE_H_ */
