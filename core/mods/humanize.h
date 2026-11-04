// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// humanize: a Pattern button that gives the notes in the selection small random
// changes in timing and level, in CE's Micro-move (m) and Volume (V) columns. The FX
// slots stay free. A step that already has a Micro-move or Volume FX in a slot keeps
// it and gets that FX's value changed instead, since that FX overrides the column when
// the step plays. Undo reverts a press.
//
// With timing range T and volume range R, for every step with a note (not empty, note
// off, cut or fade):
//  - a value already there gets a random value in a window of T or R around it, within
//    0..100 for m and 1..100 for V; a V of 0 stays 0;
//  - an empty m gets 0..T;
//  - an empty V gets a window of R around Sample volume (sample instruments) or MIDI
//    velocity (MIDI and synth instruments).
// A window is value - R/2 .. value - R/2 + R; near a limit it slides to stay inside.
// With Volume/velocity off, V is left alone.
//
// Off: the button isn't there.

#ifndef MODS_HUMANIZE_H_
#define MODS_HUMANIZE_H_

#include "mods.h"

#if MT_HAS_HUMANIZE

#include "modsFileIo.h"
#include "modsStore.h"

namespace mods
{
namespace humanize
{

constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::humanize) + 1;
constexpr uint8_t kRecordVersion = 1;

// Edited directly by Config -> Project Settings -> Humanize. The record holds these
// five bytes in this order. Numbers are 0..99, shown and used as 1..100: CE's list
// editor takes the value as its row in the list.
struct Settings
{
    uint8_t timingRange = 10 - 1;
    uint8_t volumeOn = 1; // 0 or 1
    uint8_t volumeRange = 10 - 1;
    uint8_t midiVelocity = 50 - 1;
    uint8_t sampleVolume = 50 - 1;
};

extern Settings settings; // the open project's

// The project's record into settings: the defaults if it has none or a damaged one.
void load(const Store& store);

// Call after settings may have been edited: stores them if they differ from the
// record, and returns whether they did.
bool storeIfEdited(Store& store);

// A project from stock 1.2.0: its humanize.bin into store's record. True if the file
// was good, so the project used the mod.
bool importSideFile(FileIo& io, const char* projectFolder, Store& store);

// lo .. hi, inclusive.
using Random = int (*)(int lo, int hi);

// A random value in the window of range around center, slid to stay in lo..hi.
int pick(int center, int range, int lo, int hi, Random random);

struct FxTypes
{
    uint8_t micromove;
    uint8_t volume;
};

// Step is CE's strStep: note, fx[].type, fx[].value, volume and micromove.
template <class Step>
void humanizeStep(Step& step,
                  const Settings& s,
                  bool sampleInstrument,
                  const FxTypes& types,
                  Random random)
{
    if (step.note < 0)
    {
        return;
    }

    const int timingRange = s.timingRange + 1;
    const int volumeRange = s.volumeRange + 1;

    bool micromoveFx = false;
    bool volumeFx = false;
    for (auto& fx : step.fx)
    {
        if (fx.type == types.micromove)
        {
            micromoveFx = true;
            fx.value = static_cast<uint8_t>(pick(fx.value, timingRange, 0, 100, random));
        }
        else if (fx.type == types.volume)
        {
            volumeFx = true;
            if (s.volumeOn != 0 && fx.value != 0)
            {
                fx.value = static_cast<uint8_t>(pick(fx.value, volumeRange, 1, 100, random));
            }
        }
    }

    if (!micromoveFx)
    {
        step.micromove = static_cast<int8_t>(step.micromove < 0
                                               ? random(0, timingRange)
                                               : pick(step.micromove, timingRange, 0, 100, random));
    }

    if (s.volumeOn != 0 && !volumeFx && step.volume != 0)
    {
        const int center =
          step.volume > 0 ? step.volume
                          : (sampleInstrument ? s.sampleVolume : s.midiVelocity) + 1;
        step.volume = static_cast<int8_t>(pick(center, volumeRange, 1, 100, random));
    }
}

// Humanizes sequencer.selection in the current pattern. The caller stores the undo
// revision and refreshes the screen.
void applyToSelection();

} // namespace humanize
} // namespace mods

#endif // MT_HAS_HUMANIZE

#endif /* MODS_HUMANIZE_H_ */
