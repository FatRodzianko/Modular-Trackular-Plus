// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// masterfx: other reverbs and delays in CE's own reverb and delay slots, chosen per
// project. Sends, returns, the Reverb and Delay pages and their knobs stay CE's.
//
// Off, or Stock chosen: CE's own effect runs and nothing here does. Off keeps the
// project's choice, so On brings it back.
//
// The choice is made in the main loop (Config menu, project load). apply() blocks the
// effect, clears its buffers through CE's own clear and sets what plays; the next audio
// block lays the effect out in the clean buffers. The effect is silent for that long and
// the dry sound isn't touched. The interrupt side is in masterfxDsp.cpp.

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
constexpr uint8_t kRecordVersion = 1;

constexpr uint8_t kStock = 0;
constexpr uint8_t kReverbCount = 7;
constexpr uint8_t kDelayCount = 3;

extern const char* const kReverbNames[kReverbCount];
extern const char* const kDelayNames[kDelayCount];

// Edited directly by Config -> Project Settings -> Master FX. The record holds these
// two bytes.
struct Settings
{
    uint8_t reverb = kStock;
    uint8_t delay = kStock;
};

extern Settings settings; // the open project's

// The project's record into settings: Stock if it has none or a damaged one.
void load(const Store& store);

// Call after settings may have been edited: stores them if they differ from the
// record, and returns whether they did.
bool storeIfEdited(Store& store);

// A project from stock 1.2.0: its masterfx.bin into store's record. True if it picks
// anything but Stock and Stock, so the project starts with the mod On.
bool importSideFile(FileIo& io, const char* projectFolder, Store& store);

// Main loop only. Makes the effects that play match settings and the switch. Does
// nothing until started().
void apply();

// Main loop only: the unit has started. Cheap to call again.
void started();

// The Master page's encoder: a switch clears 1.5 MB, so it waits until the encoder has
// stopped on a choice for a moment. pollApply, from the page's update, does it then;
// flushApply, when leaving the page, does it now.
void applySoon();
void pollApply();
void flushApply();

} // namespace masterfx
} // namespace mods

#endif // MT_HAS_MASTERFX

#endif /* MODS_MASTERFX_H_ */
