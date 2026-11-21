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

#include <cstdint>

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

// -- Trk Audio: the gates ------------------------------------------------------------------------
// "Open" undoes the recorder's entry, so the tracker sounds as in normal playback; "closed" is
// CE's recorder. Docs: the plan's section 4.3.

// What the recorder shows, read on each main-loop pass of a visit.
struct RecorderView
{
    bool onSetup = false;     // the setup screen, a count-in included, no stop-pattern question
    bool takeRunning = false; // a take records (any count-in is over)
    bool buttonShown = false; // the source shows Trk Audio
    bool samplesBusy = false; // CE saves a take or reloads the samples
    bool countingIn = false;  // Rec pressed with Pre Roll on, the take not started yet
};

// Shift+Rec, or Play in a count-in: what starts with the take (section 2, step 4).
enum class Arm : uint8_t
{
    none,
    pattern,
    song,
};

// The visit's own state, apart from the switch and the settings.
struct Visit
{
    bool entered = false;            // CE's entry ran: notes blocked, the mix at its defaults
    bool takeMoved = false;          // the take that runs went after the loaded samples
    bool samplesOverwritten = false; // a take of this visit went where CE puts it, over them
    Arm arm = Arm::none;
};

// Open: the visit On, Trk Audio on, a source with the button, CE's entry done, the samples intact
// and not being saved or reloaded, and the setup on screen or a take after the samples running.
bool gatesWanted(bool visitOn, const Settings& s, const Visit& v, const RecorderView& view);

// Where a take goes with the gates open: after the loaded samples (section 4.4). An instrument's
// RAM data ends as CE's findFreeSampleRamScratchStereo has it.
uint32_t framesInRam(bool streamed, uint32_t ramFrames, uint32_t length);
uintptr_t sampleEnd(uintptr_t address, uint32_t frames, bool stereo);

constexpr uint32_t kMinTakeFrames = 10 * 44100; // under 10 s of room: CE's buffer instead
constexpr uint32_t kTakeStartAlign = 32;
constexpr uint32_t kTakeBlockBytes = 256; // one audio block of one channel

struct Placement
{
    uintptr_t address = 0;
    uint32_t size = 0; // bytes for all channels; 0: too little room
};

// The free space from loadedEnd (rounded up) to bankEnd, in whole blocks per channel.
Placement place(uintptr_t bankStart, uintptr_t bankEnd, uintptr_t loadedEnd, uint8_t channels);

// Hook 14, in the audio interrupt: the engine's per-block update also runs during a take while
// the gates are open. One byte, written only by the main loop.
extern volatile uint8_t engineOpen;
inline bool engineRuns()
{
    return engineOpen != 0;
}

// -- Shift+Rec ------------------------------------------------------------------------------------
// Rec on the setup (a count-in included) with Shift held and the gates open arms the pattern; an
// arm already set stays, so a plain Rec never disarms and Shift+Play's song is kept.
Arm armOnRec(Arm now, bool shift, bool gatesOpen, bool onSetup);
// Play in a count-in, the gates open and nothing playing: it arms (Shift: the song) instead of
// playing. Otherwise CE's Play.
bool playArms(bool gatesOpen, bool countingIn, bool playing);

#ifndef MT_HOST_TEST
// The recorder's hooks, main loop.
void recorderEntered();                          // hook 1b: CE's entry ran
void recorderUpdate(const RecorderView& view);   // hook 3: the gates; disarm with no count-in or take
bool playPressed(bool shift, bool countingIn);   // hook 4: true when it took the press
void recPressed(bool shift, bool onSetup);       // hook 5, before CE's Rec
void placeBuffer(int16_t** address, uint32_t* size, int channels); // hook 6, before setConfig
void takeStarted();                              // hook 7: the armed pattern or song from its start
#endif

} // namespace resample
} // namespace mods

#endif // MT_HAS_RESAMPLE

#endif /* MODS_RESAMPLE_H_ */
