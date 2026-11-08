// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// copypattern: Copy to New, a Pattern button that copies the pattern you're on, as it is in
// RAM (unsaved edits included), into the next empty pattern, playing or stopped. You stay on
// your pattern. With Variable Track Length On, the copy gets the pattern's track lengths too.
//
// A pattern counts as empty when its Song-screen block is empty, it isn't in the song, queued
// to play next or played by a performance-mode track, and it has no file or a file in which
// every step of every track, all 128, is as CE's clearStep leaves it. The search starts at the
// pattern after yours and wraps round.
//
// The press only starts a job. The work runs in the Pattern screen's update, main loop: at
// most one pattern file is checked on the card per update, and the update that finds an empty
// pattern also writes the copy, so nothing can change that pattern in between.
//
// Off: no button and no job; nothing is written. A copy is an ordinary CE pattern file.

#ifndef MODS_COPYPATTERN_H_
#define MODS_COPYPATTERN_H_

#include "mods.h"

#if MT_HAS_COPYPATTERN

#include <cstddef>
#include <cstdint>

namespace mods
{
namespace copypattern
{

// Its mods.bin record id, as the registry gives every mod one. Copy to New keeps nothing in
// mods.bin, so the record is never written.
constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::copypattern) + 1;
constexpr uint8_t kRecordVersion = 0;

constexpr uint8_t kFirstPattern = 1;
constexpr uint8_t kLastPattern = 255;
constexpr size_t kTracks = 16;
constexpr size_t kSteps = 128;
constexpr int8_t kNoteEmpty = -1;

// The file manager busy this long in a row: give up.
constexpr uint32_t kBusyLimitMs = 5000;
// While playing, at most one card check this often, as Color Song's reads.
constexpr uint32_t kPlayingCheckGapMs = 100;

constexpr size_t kMessageSize = 32;

// ---- the rules, on plain data ------------------------------------------------------------

// A step with nothing in it, as CE's clearStep leaves it. Anything else counts: a note-off,
// an instrument without a note, an FX type, a volume or a micro-move.
template <class Step>
inline bool stepIsEmpty(const Step& step)
{
    return step.note == kNoteEmpty && step.instrument == 0 && step.fx[0].type == 0 &&
           step.fx[1].type == 0 && step.volume < 0 && step.micromove < 0;
}

// Every step of every track, past the pattern's length too.
template <class Pattern>
inline bool patternIsEmpty(const Pattern& pattern)
{
    for (size_t t = 0; t < kTracks; t++)
    {
        for (size_t s = 0; s < kSteps; s++)
        {
            if (!stepIsEmpty(pattern.track[t].step[s]))
            {
                return false;
            }
        }
    }
    return true;
}

// The pattern searched after `pattern`, starting from `source`: up to 255, then from 1. 0 when
// the search is back at `source`.
inline uint8_t nextCandidate(uint8_t source, uint8_t pattern)
{
    const uint8_t next = pattern >= kLastPattern ? kFirstPattern : static_cast<uint8_t>(pattern + 1);
    return next == source ? 0 : next;
}

// What's in RAM that makes a pattern used, without the card.
struct InUse
{
    const uint8_t* blocks;     // the Song screen's track bits, by pattern - 1
    const uint8_t* blocksMidi; // the same for the MIDI tracks
    const uint8_t* playlist;   // the song, 0 = empty slot
    size_t playlistSize;
    uint16_t queued;           // the pattern queued to play next, 0 = none
    const uint16_t* performanceTracks; // the patterns performance mode plays, or nullptr
    size_t performanceTrackCount;
};

inline bool usedInRam(const InUse& use, uint8_t pattern)
{
    if (pattern < kFirstPattern)
    {
        return true;
    }
    if (use.blocks[pattern - 1] != 0 || use.blocksMidi[pattern - 1] != 0 || use.queued == pattern)
    {
        return true;
    }
    for (size_t i = 0; i < use.playlistSize; i++)
    {
        if (use.playlist[i] == pattern)
        {
            return true;
        }
    }
    for (size_t i = 0; use.performanceTracks != nullptr && i < use.performanceTrackCount; i++)
    {
        if (use.performanceTracks[i] == pattern)
        {
            return true;
        }
    }
    return false;
}

// "Copied to pattern N".
void formatCopied(char* text, size_t size, uint8_t pattern);

// ---- the job --------------------------------------------------------------------------------

enum class FileState : uint8_t
{
    none,  // no file: empty
    empty, // a file with nothing in it
    used,  // something in it, or a file CE can't read
};

enum class CopyResult : uint8_t
{
    copied,
    patternChanged, // the interrupt switched patterns during the snapshot
    writeFailed,    // the card; the file was removed again
};

// What the job needs from CE, so the host tests can drive it.
class Ce
{
  public:
    virtual uint8_t currentPattern() = 0;
    virtual bool switchedOn() = 0;
    virtual bool performanceChanged() = 0; // performance mode changed the pattern's tracks
    virtual bool fileManagerIdle() = 0;
    virtual bool playing() = 0;
    virtual uint32_t nowMs() = 0;
    virtual bool usedInRam(uint8_t pattern) = 0;
    virtual FileState fileState(uint8_t pattern) = 0; // one card check
    virtual CopyResult copy(uint8_t source, uint8_t target) = 0;
    virtual void message(const char* text) = 0; // keeps the pointer
    virtual ~Ce() = default;
};

// Boot: the snapshot and the job are in SDRAM, which startup doesn't zero.
void init();

// The button. Ignored while a job runs.
void press(Ce& ce);

// The Pattern screen's update.
void update(Ce& ce);

// Another project, or the switch turned Off: the job is dropped without a message.
void cancel();

bool running();

// CE's side, in the firmware.
Ce& ceSide();

} // namespace copypattern
} // namespace mods

#endif // MT_HAS_COPYPATTERN

#endif /* MODS_COPYPATTERN_H_ */
