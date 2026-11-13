// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Song Loop, after P-N-E-M's songloop patch for 1.2.0 (sl4). The functions keep that patch's
// order and names where they have one: peek_next -> preload, mt_sl_song_next -> atPatternEnd,
// mt_sl_labels -> labels, mt_sl_softkey0 -> playPatternPressed, slot_move -> move,
// mt_sl_ui_tick -> tick, block_y and mt_sl_frame_calc -> cursorFrame, grey_block -> greyFrame,
// add_glyphs -> apply.

#include "songloop.h"

#if MT_HAS_SONGLOOP

#ifndef MT_HOST_TEST
#include <cr_section_macros.h>

#include "FT812.h"
#include "FT81x.h"
#include "config.h"
#include "display.h"
#include "fileManager.h"
#include "interruptsControl.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "performanceMode/performanceMode.h"
#include "songEditor/songEditor.h"

void playSongFromSelectedPattern(); // fSongEdit.cpp
#endif

namespace mods
{
namespace songloop
{

volatile State state;

namespace
{

bool inBlock(uint8_t pos)
{
    return pos >= state.first && pos <= state.last;
}

void setLabel(Ce& ce, uint8_t st, const char* l1, const char* l2, const char* r1, const char* r2)
{
    if (ce.labelState() == st)
    {
        return;
    }
    ce.setLabels(st, l1, l2, r1, r2);
}

// The block's y range in the list: (top, bottom) or false when none of it shows. The block
// belongs to the slots, not the cursor: while the cursor glides to another row it stays, while
// the list scrolls it moves with the numbers.
bool blockY(const FrameIn& in, int a, int b, int& yTop, int& yBottom)
{
    const int base = (in.y - in.row * kRowHeight == in.top) ? in.top - in.anim : in.top;
    int ya = base + (a - in.first) * kRowHeight;
    int yb = base + (b - in.first + 1) * kRowHeight;
    if (ya < in.top)
    {
        ya = in.top;
    }
    if (yb > in.top + in.visible * kRowHeight)
    {
        yb = in.top + in.visible * kRowHeight;
    }
    if (ya >= yb)
    {
        return false;
    }
    yTop = ya;
    yBottom = yb;
    return true;
}

// |: :| 16 px wide from x0: thick bar 2, gap 1, thin bar 1, gap 1, dots 2, middle 2, mirrored.
constexpr uint8_t kRepeatCols[16] = { 2, 2, 0, 1, 0, 3, 3, 0, 0, 3, 3, 0, 1, 0, 2, 2 };

struct Canvas
{
    uint8_t* buffer;
    int stride;
    int width;
};

void pixel(const Canvas& c, int x, int y)
{
    uint8_t* g = c.buffer;
    if (x >= c.width) // the second glyph sits elsewhere in the buffer
    {
        x -= c.width;
        g += c.stride * 64;
    }
    g[y * c.stride + x / 2] |= (x & 1) ? 0x0F : 0xF0;
}

void drawRepeat(const Canvas& c, int x0, int y0, int y1, int dy1, int dy2, int ncols)
{
    for (int i = 0; i < ncols; i++)
    {
        const int kind = kRepeatCols[i];
        if (kind == 1 || kind == 2)
        {
            for (int y = y0; y <= y1; y++)
            {
                pixel(c, x0 + i, y);
            }
        }
        else if (kind == 3)
        {
            for (int y = 0; y < 2; y++)
            {
                pixel(c, x0 + i, dy1 + y);
                pixel(c, x0 + i, dy2 + y);
            }
        }
    }
}

} // namespace

// ---- the interrupt ---------------------------------------------------------------------------

uint8_t preload(uint8_t pos, const uint8_t* playlist, uint8_t stockNext)
{
    if (state.loop && state.sel && playlist[state.first])
    {
        const bool inside = inBlock(pos);
        if (inside)
        {
            state.entered = 1;
        }
        // the block's end, or (after a change during the loop) outside the block: on at the
        // block's start. Not yet in the block: the song plays on up to it.
        if ((inside && pos == state.last) || (!inside && state.entered))
        {
            state.wrapPending = 1;
            return playlist[state.first];
        }
    }
    state.wrapPending = 0;
    return stockNext;
}

bool atPatternEnd(uint8_t& pos)
{
    if (state.wrapPending)
    {
        state.wrapPending = 0;
        pos = state.first;
        return true;
    }
    return false;
}

uint8_t expectedNext(const uint8_t* playlist, uint8_t stockNext)
{
    return state.wrapPending ? playlist[state.first] : stockNext;
}

void noteEntered(uint8_t pos)
{
    if (state.loop && state.sel && inBlock(pos))
    {
        state.entered = 1;
    }
}

bool holdsPerformance()
{
    return state.loop != 0;
}

void stopped()
{
    if (state.loop)
    {
        state.statusDirty = 1;
    }
    state.loop = 0;
    state.wrapPending = 0;
}

// ---- the main loop ---------------------------------------------------------------------------

bool selectionUsable(uint16_t slots)
{
    return state.sel && state.first < state.last && state.last < slots;
}

bool locked(uint16_t slots)
{
    return selectionUsable(slots) || state.loop;
}

bool labels(Ce& ce)
{
    ce.followPlayhead();
    if (ce.stopped())
    {
        state.wrapPending = 0; // a decision taken while playing holds only then
    }
    const bool columnAndRec = ce.focus() == kFocusPatterns && ce.rec();
    if (state.sel && !state.loop && !columnAndRec)
    {
        state.sel = 0; // Rec off or another focus: no selection (not during the loop)
        ce.showCursor();
    }
    if (!columnAndRec)
    {
        state.snap = 0;
    }
    const uint16_t slots = ce.slots();
    const uint8_t isLocked = locked(slots) ? 1 : 0;
    if (isLocked != state.dimmed)
    {
        state.dimmed = isLocked;
        ce.recolorSoftkeys();
    }
    if (state.loop && ce.songPlaying())
    {
        setLabel(ce, kLabelContinue, "Continue", "Song", "Stop", "");
        return true;
    }
    if (selectionUsable(slots) && ce.stopped())
    {
        setLabel(ce, kLabelPlaySelection, "Play", "Selection", "Play", "Song");
        return true;
    }
    if (selectionUsable(slots) && ce.songPlaying())
    {
        setLabel(ce, kLabelPlaySelectionRunning, "Play", "Selection", "Stop", "");
        return true;
    }
    return false;
}

bool playPatternPressed(Ce& ce)
{
    const uint16_t slots = ce.slots();
    if (state.loop && ce.songPlaying()) // Continue Song: play the block out, then on
    {
        state.loop = 0;
        state.sel = 0;
        state.snap = 0;
        if (ce.performance()) // as CE's Continue Song: Performance off at the next pattern end
        {
            ce.performanceOffAtPatternEnd();
        }
        ce.setCursor(ce.playPosition()); // the red frame on the position playing now
        ce.showCursor();
        state.statusDirty = 1;
        labels(ce);
        return true;
    }
    if (selectionUsable(slots) && ce.songPlaying()) // play on, repeat the block at its end
    {
        state.wrapPending = 0;
        state.entered = 0;
        state.loop = 1;
        const uint8_t pos = ce.playPosition();
        noteEntered(pos);
        if (pos > state.last) // the block behind: to its start at the end of this pattern
        {
            state.entered = 1;
        }
        state.statusDirty = 1;
        labels(ce);
        return true;
    }
    if (selectionUsable(slots) && ce.stopped()) // from the block's start
    {
        ce.setCursor(state.first);
        state.wrapPending = 0;
        state.entered = 1;
        ce.playSong(); // as Play Song, from the cursor
        state.loop = 1;
        ce.setCursor(state.end); // the cursor back on the edge Shift + Up/Down moves
        ce.showCursor();
        state.statusDirty = 1;
        labels(ce);
        return true;
    }
    return false;
}

void move(Ce& ce, int delta)
{
    noteEntered(ce.playPosition());
    const int slots = ce.slots();
    if (ce.focus() == kFocusPatterns && ce.rec() && ce.shiftHeld() && slots)
    {
        // Selecting, downwards only: the anchor is the top.
        if (!state.sel)
        {
            state.anchor = ce.cursor();
            state.end = state.anchor;
        }
        int end = state.end + delta;
        if (end < state.anchor)
        {
            end = state.anchor;
        }
        if (end >= slots)
        {
            end = slots - 1;
        }
        state.first = state.anchor;
        state.end = static_cast<uint8_t>(end);
        state.last = state.end;
        state.sel = state.first != state.last || state.loop; // during the loop one row stays a block
        state.snap = 1;
        ce.setCursor(static_cast<uint8_t>(end));
        ce.showCursor();
        return;
    }
    if (state.loop)
    {
        // The whole block moves, at its size, within the song.
        if (state.sel && ce.focus() == kFocusPatterns && slots)
        {
            const int size = state.last - state.first;
            int first = state.first + delta;
            if (first > slots - 1 - size)
            {
                first = slots - 1 - size;
            }
            if (first < 0)
            {
                first = 0;
            }
            state.anchor = static_cast<uint8_t>(first);
            state.first = state.anchor;
            state.end = static_cast<uint8_t>(first + size);
            state.last = state.end;
            state.snap = 1;
            ce.setCursor(state.end);
            ce.showCursor();
        }
        return;
    }
    state.sel = 0; // without Shift: no selection
    state.snap = 0;
    ce.stockMove(static_cast<int16_t>(delta));
}

void tick(Ce& ce)
{
    if (!ce.songPlaying() || !ce.performance() || !ce.track1PlayMode() || ce.queued())
    {
        state.preloaded = 0;
        return;
    }
    if (ce.track16Position() < ce.lastRow() - 1)
    {
        state.preloaded = 0;
        return;
    }
    if (state.preloaded)
    {
        return;
    }
    state.preloaded = 1;
    ce.requestNextPattern();
}

const char* statusIcon(const char* stock, bool songPlaying, uint8_t pos)
{
    if (state.loop && songPlaying)
    {
        noteEntered(pos);
        return state.entered ? kIconLooping : kIconArmed;
    }
    return stock;
}

bool takeStatusDirty()
{
    if (!state.statusDirty)
    {
        return false;
    }
    state.statusDirty = 0;
    return true;
}

bool cursorFrame(const FrameIn& in, int& yTop, int& yBottom)
{
    if (in.focus != kFocusPatterns)
    {
        return false;
    }
    int a, b;
    if (state.sel && state.last < in.slots)
    {
        a = state.first;
        b = state.last;
    }
    else if (state.snap) // shrunk to one row: not the gliding cursor
    {
        a = b = in.cursor;
    }
    else
    {
        return false;
    }
    return blockY(in, a, b, yTop, yBottom);
}

bool greyFrame(const FrameIn& in, int& yTop, int& yBottom)
{
    if (in.focus == kFocusPatterns || !state.loop || !state.sel)
    {
        return false;
    }
    return blockY(in, state.first, state.last, yTop, yBottom);
}

void makeListIcon(uint8_t* icon, int ncols)
{
    for (size_t i = 0; i < kListIconSize; i++)
    {
        icon[i] = 0;
    }
    drawRepeat(Canvas{ icon, 10, 20 }, 2, 3, 16, 6, 12, ncols);
}

void drawGlyphPair(uint8_t* buffer, int stride, int width, int ncols)
{
    drawRepeat(Canvas{ buffer, stride, width }, width - 10, 4, 18, 8, 13, ncols); // where the loop icon is
}

void reset()
{
    state.sel = 0;
    state.anchor = 0;
    state.first = 0;
    state.last = 0;
    state.loop = 0;
    state.wrapPending = 0;
    state.snap = 0;
    state.end = 0;
    state.entered = 0;
    state.dimmed = 0;
    state.preloaded = 0;
    state.statusDirty = 1;
}

void init()
{
    reset();
}

// ---- CE's side -------------------------------------------------------------------------------

#ifndef MT_HOST_TEST

namespace
{

constexpr int kStatusFont = 3; // controlStyleFont4, the status bar's
constexpr uint8_t kPlayLeft = 0x1A, kPlayRight = 0x1B; // the play icon's two glyphs
constexpr uint8_t kGlyphs[4] = { 0x60, 0x5E, 0x7B, 0x7D }; // ` ^ { }
constexpr int kGlyphCols[2] = { 16, 7 };
constexpr int kMaxStride = 16, kMinWidth = 12, kMinHeight = 20, kMaxHeight = 40;
constexpr size_t kMaxGlyph = kMaxStride * kMaxHeight;
constexpr size_t kWorkSize = kMaxStride * 64 + kMaxGlyph;

// Main-loop only, so SDRAM; nothing reads them before apply() writes them.
__NOINIT(EXTERNAL_RAM) uint8_t work[kWorkSize];
__NOINIT(EXTERNAL_RAM) uint8_t backupGlyphs[4][kMaxGlyph];

// Whether the status font in RAM_G has the mod's glyphs. Zeroed by startup.
bool glyphsApplied = false;
uint8_t backupWidths[4];

bool songPlayingNow()
{
    const Sequencer::strPlayer* p = sequencer.ptrPlayer;
    return !p->isStop && p->isPlay && !p->selectionMode && p->songMode;
}

bool stoppedNow()
{
    const Sequencer::strPlayer* p = sequencer.ptrPlayer;
    return p->isStop || !p->isPlay;
}

// The status font's layout, and whether the glyphs fit it as on 1.2.0.
struct Font
{
    int first, chars, stride, width, height;
    uint32_t glyphLength;
};

bool statusFont(Font& f)
{
    const uint8_t* m = fonts[kStatusFont].data; // widths[128], format, stride, width, height
    f.first = 0;
    while (f.first < 128 && !m[f.first])
    {
        f.first++;
    }
    f.chars = 0;
    for (int c = 0; c < 128; c++)
    {
        f.chars += m[c] ? 1 : 0;
    }
    f.stride = fonts[kStatusFont].linestride;
    f.width = fonts[kStatusFont].width;
    f.height = fonts[kStatusFont].height;
    f.glyphLength = static_cast<uint32_t>(f.stride * f.height);
    if (!(f.first <= kPlayLeft && f.stride > 0 && f.stride <= kMaxStride && f.width >= kMinWidth &&
          f.height >= kMinHeight && f.height <= kMaxHeight))
    {
        return false;
    }
    for (uint8_t c : kGlyphs) // each glyph inside what the loader wrote to RAM_G
    {
        if (c - f.first >= f.chars)
        {
            return false;
        }
    }
    return true;
}

uint32_t glyphAddress(const Font& f, uint8_t c)
{
    return static_cast<uint32_t>(fonts[kStatusFont].source) + static_cast<uint32_t>(c - f.first) * f.glyphLength;
}

// The widths 0x5c..0x63 and 0x78..0x7f, in RAM_G as in fonts[].
void uploadWidths(DisplayDriver* driver)
{
    const uint8_t* m = fonts[kStatusFont].data;
    driver->writeDataRAMG(m + 0x5C, 8, fonts[kStatusFont].address + 0x5C);
    driver->writeDataRAMG(m + 0x78, 8, fonts[kStatusFont].address + 0x78);
}

void applyGlyphs()
{
    DisplayDriver* driver = display.getDisplayDriver();
    Font f;
    if (driver == nullptr || glyphsApplied || !statusFont(f))
    {
        return;
    }
    uint8_t* m = fonts[kStatusFont].data;
    for (int i = 0; i < 4; i++)
    {
        driver->readDataRAMG(backupGlyphs[i], f.glyphLength, glyphAddress(f, kGlyphs[i]));
        backupWidths[i] = m[kGlyphs[i]];
    }
    const uint32_t length = (f.glyphLength + 3) & ~3u;
    for (int p = 0; p < 2; p++)
    {
        for (size_t k = 0; k < static_cast<size_t>(f.stride) * 64 + f.glyphLength; k++)
        {
            work[k] = 0;
        }
        drawGlyphPair(work, f.stride, f.width, kGlyphCols[p]);
        driver->writeDataRAMG(work, length, glyphAddress(f, kGlyphs[2 * p]));
        driver->writeDataRAMG(work + f.stride * 64, length, glyphAddress(f, kGlyphs[2 * p + 1]));
        m[kGlyphs[2 * p]] = m[kPlayLeft];
        m[kGlyphs[2 * p + 1]] = m[kPlayRight];
    }
    uploadWidths(driver);
    makeListIcon(work, kGlyphCols[0]);
    driver->writeDataRAMG(work, kListIconSize, kListIconAddress);
    makeListIcon(work, kGlyphCols[1]);
    driver->writeDataRAMG(work, kListIconSize, kListIconArmedAddress);
    glyphsApplied = true;
}

void restoreGlyphs()
{
    DisplayDriver* driver = display.getDisplayDriver();
    Font f;
    if (driver == nullptr || !glyphsApplied)
    {
        return;
    }
    glyphsApplied = false;
    if (!statusFont(f))
    {
        return;
    }
    uint8_t* m = fonts[kStatusFont].data;
    for (int i = 0; i < 4; i++)
    {
        driver->writeDataRAMG(backupGlyphs[i], f.glyphLength, glyphAddress(f, kGlyphs[i]));
        m[kGlyphs[i]] = backupWidths[i];
    }
    uploadWidths(driver);
}

class CeSide : public Ce
{
  public:
    bool shiftHeld() override { return songEditor.buttons->isButtonPressed(interfaceButtonShift); }
    bool rec() override { return songEditor.editMode != 0; }
    uint8_t focus() override { return songEditor.selectedPlace; }
    uint16_t slots() override { return songEditor.songLength; }
    uint8_t cursor() override { return songEditor.selectedSongPosition; }
    void setCursor(uint8_t position) override { songEditor.selectedSongPosition = position; }
    void showCursor() override { songEditor.refreshSongPlayerSelectedSlot(); }
    void stockMove(int16_t delta) override { songEditor.changeSongPositionSelection(delta); }
    uint8_t playPosition() override { return mtProject.song.playlistPos; }
    bool songPlaying() override { return songPlayingNow(); }
    bool stopped() override { return stoppedNow(); }
    bool performance() override { return sequencer.isPerformanceMode(); }

    void playSong() override
    {
        ::playSongFromSelectedPattern();
    }

    void performanceOffAtPatternEnd() override
    {
        DISABLE_AUDIO_INTERRUPTS();
        performanceMode.turnOffAtEndOfPattern();
        ENABLE_AUDIO_INTERRUPTS();
    }

    uint8_t labelState() override { return songEditor.playButtonsState; }

    void setLabels(uint8_t st, const char* l1, const char* l2, const char* r1, const char* r2) override
    {
        display.setControlText(songEditor.label[0], l1);
        display.setControlText2(songEditor.label[0], l2);
        display.setControlText(songEditor.label[1], r1);
        display.setControlText2(songEditor.label[1], r2);
        display.refreshControl(songEditor.label[0]);
        display.refreshControl(songEditor.label[1]);
        songEditor.playButtonsState = st;
    }

    void recolorSoftkeys() override
    {
        const bool grey = locked(songEditor.songLength) || !songEditor.editMode;
        for (uint8_t i = 2; i < 6; i++)
        {
            display.setControlColors(songEditor.label[i],
                                     grey ? display.getTheme().inactiveButtonLabelsColors
                                          : display.getTheme().activeButtonLabelsColors);
            display.refreshControl(songEditor.label[i]);
        }
        display.synchronizeRefresh();
    }

    // With Performance on, the Song update takes the play position over only with Rec off; with
    // Rec on (needed for a selection) the play icon stays where it was. This takes it over, as
    // the update does without Performance.
    void followPlayhead() override
    {
        if (!sequencer.isPerformanceMode() || stoppedNow() || !songEditor.editMode)
        {
            return;
        }
        const uint8_t pos = mtProject.song.playlistPos;
        songEditor.lastSongListPos = pos;
        const text_icon_t& icon = songEditor.patternsList.icon;
        if (!icon.useSpecialIcons || static_cast<uint8_t>(icon.iconPositionInList) != pos)
        {
            songEditor.showIcon(sequencer.isSongMode() ? iconPlay : iconLoop, pos);
        }
    }

    int8_t track1PlayMode() override { return sequencer.ptrPlayer->track[0].performancePlayMode; }
    bool queued() override { return sequencer.ptrPlayer->sequencialSwitch_isArmed; }
    int lastRow() override { return sequencer.getActualPattern()->track[0].length; }
    int track16Position() override { return sequencer.ptrPlayer->track[kAllTracks - 1].actual_pos_absolute; }

    void requestNextPattern() override
    {
        DISABLE_AUDIO_INTERRUPTS(); // the same request as the sequencer's, not at the same time
        const uint8_t next = newFileManager.getNextSongPattern();
        if (next)
        {
            newFileManager.loadWorkspacePattern(next);
        }
        ENABLE_AUDIO_INTERRUPTS();
    }
};

CeSide ceSideObject;

} // namespace

Ce& ceSide()
{
    return ceSideObject;
}

void fontsLoaded()
{
    glyphsApplied = false; // CE's own glyphs again
    apply();
}

bool glyphsReady()
{
    return glyphsApplied;
}

void apply()
{
    if (on(Mod::songloop))
    {
        applyGlyphs();
    }
    else
    {
        restoreGlyphs();
    }
}

void switchEdited()
{
    reset();
    apply();
}

bool drawListIcon(int x, int y)
{
    if (!glyphsApplied || !state.loop || !songPlayingNow())
    {
        return false;
    }
    noteEntered(mtProject.song.playlistPos);
    API_BITMAP_SOURCE(state.entered ? kListIconAddress : kListIconArmedAddress);
    API_BITMAP_LAYOUT(L4, 10, 20);
    API_BITMAP_SIZE(NEAREST, BORDER, BORDER, 20, 20);
    API_BEGIN(BITMAPS);
    API_VERTEX2F(x, y);
    API_END();
    return true;
}

void drawFrames(int top, int first, int row, int anim, int x0, int x1, int& yTop, int& yBottom)
{
    const FrameIn in{ songEditor.selectedPlace,
                      songEditor.selectedSongPosition,
                      songEditor.songLength,
                      songEditor.patternsList.linesCount,
                      top,
                      first,
                      row,
                      anim,
                      yTop };
    int ya, yb;
    if (greyFrame(in, ya, yb)) // the looping block, with the cursor in the grid
    {
        API_COLOR(kGreyBlockRgb);
        API_LINE_WIDTH(kRedFrameWidth);
        API_BEGIN(LINE_STRIP);
        API_VERTEX2F(x0, ya);
        API_VERTEX2F(x1, ya);
        API_VERTEX2F(x1, yb);
        API_VERTEX2F(x0, yb);
        API_VERTEX2F(x0, ya);
        API_END();
    }
    if (cursorFrame(in, ya, yb))
    {
        yTop = ya;
        yBottom = yb;
    }
}

#endif // MT_HOST_TEST

} // namespace songloop
} // namespace mods

#endif // MT_HAS_SONGLOOP
