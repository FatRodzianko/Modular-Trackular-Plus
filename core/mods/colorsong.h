// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// colorsong: Color Song. Song screen blocks get a colour from their content, so the same
// material can be followed through a song.
//
//  - Hue: the set of instruments the track's steps use. Shade: everything else (steps,
//    notes, instruments, FX, the pattern's length). Identical content has an identical
//    colour in any column.
//  - The first time a song is seen, colours are handed out in order of appearance: the
//    first family is red and each new one a golden angle further round the wheel. After
//    that they stay put: only edited blocks change, to a colour nothing else in the song
//    uses, and a colour an edit just released doesn't come straight back.
//  - Micro-move (m) and Volume (V) don't count, in their own columns or as FX, so
//    humanizing keeps a block's colour.
//  - Only the fill of filled blocks changes. A theme's muted and empty blocks, frames and
//    text stay as they are. Blocks stay readable on any theme: their lightness keeps a
//    distance from the theme's empty-block colour (see blockColor).
//
// Saved in the project's mods.bin. The record is the image of 1.2.0's colorsong.bin, so
// that file imports as it is and a project keeps its colours; content keys are the same
// on both firmwares. Colours following an edit don't mark the project changed: the edit
// already has, and a song seen for the first time gets the same colours again.
//
// Off: nothing here runs and the Song screen is CE's.
//
// Threads: patternChanged can run in the audio interrupt (a pattern save from MIDI Start
// in Song mode), so it only sets flags. Everything else runs in the main loop: update from
// the Song screen, blockColor from the display, the rest on project load and from Config.

#ifndef MODS_COLORSONG_H_
#define MODS_COLORSONG_H_

#include "mods.h"

#if MT_HAS_COLORSONG

#include "modsFileIo.h"
#include "modsStore.h"

namespace mods
{
namespace colorsong
{

constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::colorsong) + 1;
constexpr uint8_t kRecordVersion = 1;

constexpr size_t kPatternCount = 255; // numbered 1..255
constexpr size_t kTracks = 16;
constexpr size_t kSteps = 128;
constexpr size_t kStepSize = 8;
constexpr size_t kTrackSize = 1 + kSteps * kStepSize; // CE's strTrack: length, then steps

constexpr size_t kMaxFamilies = 127;
constexpr size_t kMaxVariants = 512;
constexpr size_t kFileHeaderSize = 12;
constexpr size_t kFileRecordSize = 8;
constexpr size_t kMaxRecordSize =
  kFileHeaderSize + kFileRecordSize * (kMaxFamilies + kMaxVariants);

// What the mod asks of CE. A pattern's tracks are CE's strTrack array as bytes.
class Song
{
  public:
    virtual ~Song() = default;

    // The playlist's pattern at a position, 0 at the end of the song.
    virtual uint8_t slot(size_t position) = 0;
    // The pattern's used-track mask, as the Song screen draws it.
    virtual uint16_t mask(uint8_t pattern) = 0;
    // The pattern's tracks if RAM holds a newer copy than its file, or nullptr.
    virtual const uint8_t* unsavedTracks(uint8_t pattern) = 0;
    // Whether a pattern file may be read just now.
    virtual bool mayReadFile() = 0;
    // The pattern's tracks from its file, or nullptr. Valid until the next call.
    virtual const uint8_t* readTracks(uint8_t pattern) = 0;
};

// Boot, before any project: the tables are in SDRAM, which startup doesn't zero.
void init();

// A project was loaded: forgets everything. The colours come from the record once the song
// has been read.
void reset();

// Song screen update. Reads at most one pattern file per call, then, with the song read,
// hands out colours and stores them in the record. True if the record changed.
bool update(Song& song, Store& store);

// A pattern's content may have changed. Safe in the audio interrupt.
void patternChanged(uint8_t pattern);

// The mod's switch was changed in Config: turned on, it hears nothing from while it was
// Off, so every pattern is read again on the next visit. Colours stay.
void switchEdited();

// The colour of a filled block. mask is what the Song screen is drawing for this pattern,
// themeColor what CE would draw (returned while the block has no colour yet), emptyColor
// the theme's empty-block colour.
uint32_t blockColor(uint8_t pattern,
                    uint8_t track,
                    uint16_t mask,
                    bool muted,
                    uint32_t themeColor,
                    uint32_t emptyColor);

// A project from stock 1.2.0: its colorsong.bin into store's record. True if it has one,
// so the project starts with the mod On.
bool importSideFile(FileIo& io, const char* projectFolder, Store& store);

// CE's side of Song. Lives with the firmware glue; host tests have their own.
Song& ceSong();

} // namespace colorsong
} // namespace mods

#endif // MT_HAS_COLORSONG

#endif /* MODS_COLORSONG_H_ */
