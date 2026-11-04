// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// swing: Global Swing. Every take and every pattern start uses the project's Swing
// amount (25..75, as the Swing FX; 50 is no swing), on the long step of the pair. A
// Swing FX still works for the rest of its pattern.
//
//  - CE's Stop already clears swing, but a pattern start doesn't, and a Swing FX on a
//    muted track never runs.
//  - Performance mode: only a take's start, not pattern starts, since its tracks can
//    have their own lengths and play backwards or at random.
//  - Changing the settings while playing takes effect at once. Turning the mod Off
//    leaves no swing until a Swing FX sets some.
//  - An external MIDI clock sets the steps' timing itself, so swing has no effect then
//    (Sequencer::tick), as with the Swing FX.
//
// Off: nothing here runs and the Swing FX works as in CE.
//
// restart runs in the audio interrupt (Sequencer::play can come from MIDI Start, and
// switchRowStep is the sequencer), so settings stay in RAM and there's no SD, heap or
// UI work there. switchEdited and amountEdited run in the main loop, from the Config
// menu's actions.

#ifndef MODS_SWING_H_
#define MODS_SWING_H_

#include "mods.h"

#if MT_HAS_SWING

#include "modsFileIo.h"
#include "modsStore.h"

namespace mods
{
namespace swing
{

constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::swing) + 1;
constexpr uint8_t kRecordVersion = 1;
constexpr uint8_t kMinAmount = 25;
constexpr uint8_t kMaxAmount = 75;
constexpr uint8_t kNoSwing = 50;

// Edited directly by Config -> Project Settings -> Global Swing. The record holds this
// byte. The amount is stored as 0..50 and shown and used as 25..75: CE's list editor
// takes the value as its row in the list.
struct Settings
{
    uint8_t amount = kNoSwing - kMinAmount;
};

extern Settings settings; // the open project's

inline int16_t amountOf(const Settings& s)
{
    return static_cast<int16_t>(s.amount + kMinAmount);
}

// The project's record into settings: the defaults if it has none or a damaged one.
void load(const Store& store);

// Call after settings may have been edited: stores them if they differ from the
// record, and returns whether they did.
bool storeIfEdited(Store& store);

// A project from stock 1.2.0: its swing.bin into store's record. True if Global swing
// was On in it, so the project starts with the mod On. A file with it Off still gives
// its amount.
bool importSideFile(FileIo& io, const char* projectFolder, Store& store);

// CeSwing is CE's Sequencer::strPlayer::strSwing.

// A take or a pattern starts: the amount, from the long step.
template <class CeSwing>
inline void restart(CeSwing& swing)
{
    swing.setSwing(amountOf(settings));
    swing.stepToogle = false;
}

// The switch was changed while playing: the amount from here on, or no swing. The long
// or short step stays as it is.
template <class CeSwing>
inline void followSwitch(CeSwing& swing, bool on)
{
    if (on)
    {
        swing.setSwing(amountOf(settings));
    }
    else
    {
        swing.isActive = false;
    }
}

// The amount was changed while playing, with the mod on.
template <class CeSwing>
inline void followAmount(CeSwing& swing)
{
    swing.setSwing(amountOf(settings));
}

// The firmware side of the two above: they check that the sequencer is playing and
// whether the mod is on.
void switchEdited();
void amountEdited();

} // namespace swing
} // namespace mods

#endif // MT_HAS_SWING

#endif /* MODS_SWING_H_ */
