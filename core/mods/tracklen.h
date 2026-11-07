// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Variable Track Length: each track of a pattern can have its own length. A short track
// plays its own steps over and over, and every track starts again together when the
// pattern ends: in a 16-step pattern a track of length 6 plays steps 1-6, 1-6, 1-4.
//
// The sequencer's positions are left alone: every track's position still counts the
// whole pattern, so the pattern's end, song mode, the metronome and the play row work as
// in CE. What changes is which step a track reads at its position: position mod length.
//
// A length counts when it is 1 to the pattern's length - 1. Anything else (0 = none, or
// a length the pattern has since been shortened below) plays the whole pattern, so a
// pattern that grows back gets its length back.
//
// A short track's pass ends at its own length, so CE's conditions on the Chance FX count
// the track's own loops.
//
// Lengths are per pattern number, in one record of the project's mods.bin, and go with
// the project like any other mod setting. Pattern files aren't touched.

#ifndef MODS_TRACKLEN_H_
#define MODS_TRACKLEN_H_

#include "mods.h"

#if MT_HAS_TRACKLEN

#include "modsFileIo.h"
#include "modsStore.h"

namespace mods
{
namespace tracklen
{

constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::tracklen) + 1;
constexpr uint8_t kRecordVersion = 1;
constexpr size_t kTracks = 16;
constexpr size_t kPatterns = 256; // indexed by pattern number, 1..255
constexpr size_t kEntrySize = 1 + kTracks; // the pattern's number, its 16 lengths
constexpr size_t kMaxRecordSize = (kPatterns - 1) * kEntrySize;

// ---- the rules, on plain numbers -----------------------------------------------------

// The length a track plays: `stored` if it counts, else the pattern's.
inline int ownLength(uint8_t stored, int patternLength)
{
    return stored >= 1 && stored < patternLength ? stored : patternLength;
}

// The step a track of length `length` reads at position `position`.
inline int playedStep(int position, int length)
{
    return position < length ? position : position % length;
}

// A short track starts its own loop again at this position. Position 0 is the pattern's
// own start, which CE counts itself.
inline bool startsOwnLoop(int position, int length, int patternLength)
{
    return length < patternLength && position > 0 && position % length == 0;
}

// Where live recording writes, as CE's getDestRecordingStep with a track of `length`
// steps. `late`: a hit in the second half of a step goes to the step played next.
// `noteOff`: not on the step its own note went to, but the one after it in the loop.
inline int recordingStep(int position, int length, int patternLength, bool late, bool noteOff, int noteOnStep)
{
    if (late)
    {
        position = (position + 1) % patternLength;
    }
    int step = playedStep(position, length);
    if (noteOff && step == noteOnStep)
    {
        step = (step + 1) % length;
    }
    return step;
}

// Live recording over several loops: a short track's loops share its steps, so the
// note-offs of its first loop would soon leave no step free. A new note may replace a
// step holding only a note-off. A note is never replaced.
template <class Step>
inline bool holdsOnlyNoteOff(const Step& step, int noteOff)
{
    return step.note == noteOff && step.fx[0].type == 0 && step.fx[1].type == 0 && step.volume < 0;
}

// Roll's look-ahead, as CE's calcStepLength: how many steps from `step` to the next one
// holding a note (1 = the next), or to the pattern's end when none comes first. A short
// track's steps are read mod length, and repeat, so `length` reads settle it.
template <class HasNote>
inline int stepsToNextNote(int step, int lastStep, int length, HasNote hasNote)
{
    int distance = 1;
    int reads = 0;
    for (int position = step + 1; position <= lastStep && reads < length; position++, distance++, reads++)
    {
        if (hasNote(playedStep(position, length)))
        {
            return distance;
        }
    }
    return lastStep - step + 1;
}

// ---- the lengths ---------------------------------------------------------------------

// Boot. The table is in SDRAM, which startup doesn't zero.
void init();

// The stored length (0 = none). Any context: one byte read.
uint8_t stored(uint8_t pattern, uint8_t track);

// The audio interrupt's copy of one pattern's lengths: DTCM, since it's read per track
// per step. Refilled when another pattern plays, or after activeChanged().
extern uint8_t activeLengths[kTracks];
extern volatile uint16_t activePattern;
void fillActive(uint8_t pattern);

inline uint8_t activeStored(uint8_t pattern, uint8_t track)
{
    if (activePattern != pattern)
    {
        fillActive(pattern);
    }
    return activeLengths[track];
}

// Trk Len's encoder: the length of `track` in `pattern` plus `delta`, 1 to the pattern's
// length, where the pattern's length means none. True if it changed.
bool edit(uint8_t pattern, uint8_t track, int delta, int patternLength);

// Song's paste: each pasted track's length goes with its notes, none included, so a track
// pasted over a short one plays whole. True if a length changed.
bool copyTracks(uint8_t sourcePattern, uint8_t destPattern, uint8_t sourceTrack, uint8_t destTrack, uint8_t tracks);

// The record into the table: none if the project has none. A damaged record gives none.
void load(const Store& store);

// Call after a length changed: the table into the record. False if it doesn't fit.
bool store(Store& target);

// A project from stock 1.2.0: its tracklen.bin into store's record. True if it holds a
// length, so the project starts with the mod On.
bool importSideFile(FileIo& io, const char* projectFolder, Store& store);

} // namespace tracklen
} // namespace mods

#endif // MT_HAS_TRACKLEN

#endif /* MODS_TRACKLEN_H_ */
