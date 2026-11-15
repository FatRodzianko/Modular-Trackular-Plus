// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// masterfx: plugins from the card (/MasterFX/*.ptmfx, masterfx/masterfxPlugins.h) in CE's own
// reverb, delay and limiter slots, chosen per project. Sends, returns, the Reverb, Delay and
// Limiter pages and CE's knobs stay CE's; a plugin may rename CE's knobs and add two of its
// own.
//
// Off, Stock chosen, or the chosen file missing: CE's own effect runs and nothing here does.
// Off keeps the project's choice, so On brings it back.
//
// The choice is made in the main loop (the Master page, Config, project load). apply() reads
// the plugin from the card while what plays keeps playing, then blocks the effect, puts the
// plugin in, clears the effect's buffers through CE's own clear and sets what plays. The
// effect is silent for that long and the dry sound isn't touched.

#ifndef MODS_MASTERFX_H_
#define MODS_MASTERFX_H_

#include "mods.h"

#if MT_HAS_MASTERFX

#include "modsFileIo.h"
#include "modsStore.h"

namespace mods
{
namespace masterfx
{

constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::masterfx) + 1;
constexpr uint8_t kRecordVersion = 2;

// The slots, as masterfx/masterfxPlugins.h numbers them
constexpr uint8_t kReverb = 0;
constexpr uint8_t kDelay = 1;
constexpr uint8_t kDynamics = 2;
constexpr size_t kSlots = 3;

constexpr size_t kFileSize = 48;    // a plugin's file name with its 0
constexpr size_t kOwnKnobs = 2;

// Version 1's choices (masterfx-1.x and 1.2.0's masterfx.bin): an index into each list, 0 is
// Stock. Each maps to the default pack's file, made from the same DSP.
constexpr uint8_t kStock = 0;
constexpr uint8_t kV1ReverbCount = 7;
constexpr uint8_t kV1DelayCount = 3;
extern const char* const kV1Reverbs[kV1ReverbCount];   // "" for Stock
extern const char* const kV1Delays[kV1DelayCount];

// One slot's choice: the file ("" is Stock) and the plugin's own knobs. A knob the plugin doesn't
// have is kept as it is.
struct SlotSetting
{
    char file[kFileSize];
    float knob[kOwnKnobs];
};

// What the dynamics slot ducks: bit d is destination d (0 to 7 the sample tracks, 8 to 10 the
// synth parts); 0 is the master, as CE.
constexpr uint8_t kDuckTracks = 8;
constexpr uint8_t kDuckSynths = 3;
constexpr uint16_t kDuckAll = (1u << (kDuckTracks + kDuckSynths)) - 1u;

// The record holds this.
struct Settings
{
    SlotSetting slot[kSlots];
    uint16_t ducks;      // kDuckAll's bits
    uint8_t reserved[2]; // 0
};

extern Settings settings; // the open project's

// The project's record into settings: Stock everywhere if it has none or a damaged one. A
// version 1 record is converted.
void load(const Store& store);

// Call after settings may have been edited: stores them if they differ from the record, and
// returns whether they did.
bool storeIfEdited(Store& store);

// A project from stock 1.2.0: its masterfx.bin into store's record. True if it picks anything
// but Stock and Stock, so the project starts with the mod On.
bool importSideFile(FileIo& io, const char* projectFolder, Store& store);

// ---- the lists the pages show ----------------------------------------------------------
// For each slot: Stock, the slot's plugins in the card's order (masterfx-sdk's pack first), then
// "Missing" when the project's file isn't usable (the page title names the file). Rebuilt by
// refreshChoices (after a load, a choice or a new list of the card). load lists the card again.
void refreshChoices();
uint8_t choiceCount(uint8_t slot);
const char* const* choiceNames(uint8_t slot);
uint8_t choice(uint8_t slot);                // the project's, in that list
const char* choiceName(uint8_t slot);
// The user's pick in the list: the file goes into settings (its own knobs at the plugin's
// defaults). Doesn't apply it.
void choose(uint8_t slot, uint8_t index);
// The project's file for the slot isn't on the card (or can't be used): CE's effect plays.
bool missing(uint8_t slot);

// ---- the slot's own page (Reverb, Delay, Limiter) ----------------------------------------
// Its title: "<word>: <plugin>", with " (bypassed)" once the time guard has stopped it, or
// "<word>: Missing: <file>" when the project's file can't play. nullptr: CE's own plays, CE's title.
const char* pageTitle(uint8_t slot, const char* word);
// The slot's plugin was stopped by the time guard (it plays silence, or the master untouched).
bool bypassed(uint8_t slot);
// A slot whose plugin the time guard has stopped since the last call, or -1.
int takeBypassed();

// The plugin's own knobs, in the page's columns 5 and 6. 0 when nothing of ours plays.
uint8_t ownKnobs(uint8_t slot);
const char* ownKnobLabel(uint8_t slot, uint8_t knob);
void ownKnobText(uint8_t slot, uint8_t knob, char* out, size_t size);
uint8_t ownKnobPercent(uint8_t slot, uint8_t knob);  // its step as 0 to 100, for the bar
// The encoder's steps, or the plugin's default: the plugin hears it at its next block and settings
// keep it. The caller stores the record (mods::masterfxKnobEdited).
void stepOwnKnob(uint8_t slot, uint8_t knob, int steps);
void defaultOwnKnob(uint8_t slot, uint8_t knob);

// The Limiter page's Ducks list (Shift + the Sidechain column's button): the master, or a set of
// tracks the dynamics plugin ducks by its gain from the key. Only while a plugin that can do it
// plays (PTMFX_FLAG_DUCK), on the Tracker+.
constexpr uint8_t kDuckItems = 1 + kDuckTracks + kDuckSynths; // "Master", then each destination
bool canDuck();
const char* const* duckNames();      // kDuckItems
bool ducked(uint8_t item);           // item 0: the master
// The item pressed: the master clears the set; a track goes in or out of it. Takes effect at once;
// the caller stores the record (mods::masterfxKnobEdited).
void toggleDuck(uint8_t item);
const char* ducksText();             // "Master", "Track 3", "Synth 1" or "3 tracks"

// Main loop only. Makes the effects that play match settings and the switch. Does nothing until
// started(). A plugin's own knobs are set from settings; one that settings leave unset plays at
// the plugin's default, and settings aren't changed.
void apply();

// Main loop only: the unit has started. Cheap to call again.
void started();

// Main loop only, when the Master page opens: lists /MasterFX again (files copied there since)
// and applies, so a project's file that was missing plays now. Not while the card is lent.
void pageOpened();

// The Master page's encoder: a switch reads the card and clears 1.5 MB, so it waits until the
// encoder has stopped on a choice for a moment. pollApply, from the page's update, does it then;
// flushApply, when leaving the page, does it now.
void applySoon();
void pollApply();
void flushApply();

// Config -> Project Settings -> Master FX: its lists, kept as refreshChoices leaves them
// (the firmware's; configMenu.cpp shows them).
const void* menuSetup(uint8_t slot);

// The firmware's start of the plugins (masterfx/masterfxPluginsCe.cpp): the pack onto the card,
// the list.
void startPlugins(FileIo& io);
// The dynamics slot's memory to zeros (main loop, with the slot stopped)
void clearDynamicsMemory();

} // namespace masterfx
} // namespace mods

#endif // MT_HAS_MASTERFX

#endif /* MODS_MASTERFX_H_ */
