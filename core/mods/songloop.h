// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// songloop: Song Loop, loop a block of song positions. Designed and written by P-N-E-M for
// firmware 1.2.0 (Modular-Trackular's songloop patch, sl4); this is the same design on CE's
// source.
//
// In the Song screen's pattern column, with Rec on, Shift + Down selects a block of positions
// from the cursor and Shift + Up takes the bottom back (the top is the anchor). The left
// softkey then reads Play Selection:
//  - stopped: the song plays from the block's first position and repeats the block;
//  - playing, the block ahead: the song plays on and repeats the block once it reaches it
//    (armed, |: in the status bar);
//  - playing, the block behind: the song goes to the block's start at the end of the pattern.
// While the block repeats, the softkey reads Continue Song: the block plays out, then the
// song goes on and the selection is gone. During the loop, Shift + Up/Down resizes the block
// (down to one position), and Up/Down or the jog wheel move the whole block. Stop, on any
// screen, ends the loop. With a block, or during the loop, Undo, Add Slot, Delete Slot (and
// the Delete key) and << Pattern >> (and Shift + jog) do nothing and are greyed out.
//
// The loop is Loop Performance for a block: Performance stays on for all of the block's
// patterns, and Continue Song turns it off at the next pattern end as CE's Continue Song does.
// With a Performance play mode other than Fwd on track 1, CE asks for the next pattern on
// track 1's last row, which then comes too late or at random; the mod asks on the last two rows
// of track 16 instead, from the main loop. That also applies without a selection.
//
// Nothing is saved. Off: none of this, CE's Song screen.
//
// Where it runs: preload(), atPatternEnd(), noteEntered() and stopped() in the audio
// interrupt (they only read and write the state below). Everything else in the main loop.

#ifndef MODS_SONGLOOP_H_
#define MODS_SONGLOOP_H_

#include "mods.h"

#if MT_HAS_SONGLOOP

#include <cstddef>
#include <cstdint>

namespace mods
{
namespace songloop
{

// Its mods.bin record id, as the registry gives every mod one. Song Loop keeps nothing in
// mods.bin, so the record is never written.
constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::songloop) + 1;
constexpr uint8_t kRecordVersion = 0;

// The softkey label states the mod sets in cSongEditor::playButtonsState; CE uses 0 to 5.
constexpr uint8_t kLabelPlaySelection = 0x20;
constexpr uint8_t kLabelContinue = 0x21;
constexpr uint8_t kLabelPlaySelectionRunning = 0x22;

constexpr uint8_t kFocusGrid = 0;
constexpr uint8_t kFocusPatterns = 1;

constexpr int kRowHeight = 27;
constexpr uint32_t kGreyBlockRgb = 0x333333;

// The status bar's repeat signs: two glyph pairs of the status bar's font, in place of
// characters the UI and its keyboard never show.
constexpr char kIconLooping[] = "`^";
constexpr char kIconArmed[] = "{}";

// The Song list's 20 x 20 L4 icons, in RAM_G between CE's last bitmap (128,780) and the controls'
// display lists (150,000).
constexpr uint32_t kListIconAddress = 140000;
constexpr uint32_t kListIconArmedAddress = 140200;
constexpr size_t kListIconSize = 200;

struct State
{
    uint8_t sel;         // a selection exists
    uint8_t anchor;      // where Shift-selecting started
    uint8_t first, last; // the block, inclusive
    uint8_t loop;        // Play Selection on; off after Continue Song
    uint8_t wrapPending; // decided at the preload: the next position is the block's first
    uint8_t snap;        // Shift-selecting: the frame is the mod's, even for one row
    uint8_t end;         // the edge Shift + Up/Down moves
    uint8_t entered;     // the loop reached the block
    uint8_t dimmed;      // the softkeys Undo..Pattern are greyed by the mod
    uint8_t preloaded;   // the main loop's own preload done for this pattern end
    uint8_t statusDirty; // the status bar must redraw its text
};

// Read and written by the interrupt and the main loop: small, so in DTCM.
extern volatile State state;

// ---- the interrupt -------------------------------------------------------------------------

// The pattern to preload after position `pos`; `stockNext` is CE's choice. At the block's end
// (or outside the block once it was reached) the block's first pattern, and the pattern end
// then goes to the block's start.
uint8_t preload(uint8_t pos, const uint8_t* playlist, uint8_t stockNext);

// The pattern end: true when the preload decided on the block's start, with `pos` set to it.
// False: CE's own next position, and the caller calls noteEntered after it.
bool atPatternEnd(uint8_t& pos);

// The play position is in the block: the loop follows it from now.
void noteEntered(uint8_t pos);

// While the loop is on, Performance stays on at the pattern end.
bool holdsPerformance();

// Every stop ends the loop.
void stopped();

// ---- the main loop -------------------------------------------------------------------------

// What the mod needs from CE's Song screen and sequencer, so the host tests can drive it.
class Ce
{
  public:
    virtual bool shiftHeld() = 0;
    virtual bool rec() = 0;     // Rec on: the red frame
    virtual uint8_t focus() = 0; // 0 grid, 1 pattern column, 2 tempo
    virtual uint16_t slots() = 0;
    virtual uint8_t cursor() = 0; // the pattern column's selected position
    virtual void setCursor(uint8_t position) = 0;
    virtual void showCursor() = 0;            // CE's refreshSongPlayerSelectedSlot
    virtual void stockMove(int16_t delta) = 0; // CE's changeSongPositionSelection
    virtual uint8_t playPosition() = 0;
    virtual bool songPlaying() = 0; // playing in song mode, not a selection preview
    virtual bool stopped() = 0;
    virtual bool performance() = 0;
    virtual void playSong() = 0;                     // CE's Play Song from the cursor
    virtual void performanceOffAtPatternEnd() = 0;   // CE's Continue Song, Performance part
    virtual uint8_t labelState() = 0;
    virtual void setLabels(uint8_t state, const char* l1, const char* l2, const char* r1, const char* r2) = 0;
    virtual void recolorSoftkeys() = 0;
    virtual void followPlayhead() = 0; // the play icon with Performance and Rec on
    // The main loop's preload in Performance.
    virtual int8_t track1PlayMode() = 0;
    virtual bool queued() = 0;
    virtual int lastRow() = 0;       // the playing pattern's length, counted from 0
    virtual int track16Position() = 0;
    virtual void requestNextPattern() = 0; // with the audio interrupt held off
    virtual ~Ce() = default;
};

// A block of 2 or more positions, inside the song.
bool selectionUsable(uint16_t slots);

// Undo, Add, Delete and Pattern do nothing.
bool locked(uint16_t slots);

// The Song screen's update, every 100 ms, in place of CE's refreshButtonsLabels. False: the
// caller runs CE's labels.
bool labels(Ce& ce);

// Softkey 0 and Shift + Play. False: the caller runs CE's own.
bool playPatternPressed(Ce& ce);

// Up (-1) / Down (+1) / jog in the pattern column, in place of CE's cursor move.
void move(Ce& ce, int delta);

// The main loop, once per pass.
void tick(Ce& ce);

// The status bar: the icon to show in place of CE's.
const char* statusIcon(const char* stock, bool songPlaying, uint8_t pos);
// True once after anything that changes the status bar's icon.
bool takeStatusDirty();

// The pattern column's frames. `top` is where the list starts, `first` its first visible slot,
// `row` the cursor frame's row, `anim` the animation offset, `y` the frame's top as CE draws it.
struct FrameIn
{
    uint8_t focus;
    uint8_t cursor;
    uint16_t slots;
    int visible;
    int top;
    int first;
    int row;
    int anim;
    int y;
};

// The cursor frame over the block: true with [yTop, yBottom); false leaves CE's.
bool cursorFrame(const FrameIn& in, int& yTop, int& yBottom);
// The grey frame over the looping block with the cursor in the grid: true with its range.
bool greyFrame(const FrameIn& in, int& yTop, int& yBottom);

// The two icons and the glyphs. `ncols` 16: |: :|, 7: |: alone.
void makeListIcon(uint8_t* icon, int ncols);
// Two glyphs of a font, the second `stride * 64` bytes after the first.
void drawGlyphPair(uint8_t* buffer, int stride, int width, int ncols);

// Another project: no selection, no loop.
void reset();

// Boot, before mods.cfg is read.
void init();
// The Workspace loaded or the switch changed: the glyphs on or back to CE's. Firmware only.
void apply();

// ---- CE's side -----------------------------------------------------------------------------

#ifndef MT_HOST_TEST
Ce& ceSide();

// The fonts were loaded: the glyphs again if the mod is on.
void fontsLoaded();
// The status font has the mod's glyphs and RAM_G the list icons. False (a font they don't fit):
// the status bar and the list keep CE's play icon.
bool glyphsReady();
// The project's switch changed.
void switchEdited();
// The Song list's icon next to the play position: true when the mod drew it.
bool drawListIcon(int x, int y);
// Both pattern-column frames, for cSongPlayer::showList: the grey one drawn here, the cursor's
// range in yTop/yBottom.
void drawFrames(int top, int first, int row, int anim, int x0, int x1, int& yTop, int& yBottom);
#endif

} // namespace songloop
} // namespace mods

#endif // MT_HAS_SONGLOOP

#endif /* MODS_SONGLOOP_H_ */
