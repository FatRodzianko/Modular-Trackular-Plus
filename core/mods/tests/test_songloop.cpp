// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for Song Loop, on a fake CE: the cases of the 1.2.0 patch's own tests (select,
// Play Selection, the preload and the pattern end, Continue Song, stop, the locks, the block's
// moves, the labels, the status icon, Performance kept on, the main loop's preload in
// Performance) plus the frames and the drawn icons. What CE's own code does with it is in
// tools/emu/test_songloop.py. Build and run with `make -C core/mods/tests`.

#include "songloop.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace mods;
using namespace mods::songloop;

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& name)
{
    checks++;
    if (!condition)
    {
        failures++;
        printf("FAIL %s\n", name.c_str());
    }
}

struct FakeCe : Ce
{
    bool shift = false;
    bool recOn = true;
    uint8_t place = kFocusPatterns;
    uint16_t length = 10;
    uint8_t cursorAt = 0;
    uint8_t playing = 0; // the play position
    bool songOn = false; // playing in song mode
    bool perf = false;
    uint8_t label = 0;
    std::vector<std::string> labelTexts;
    std::vector<std::string> calls;
    int8_t mode1 = 0;
    bool armed = false;
    int length1 = 63;
    int row16 = 0;

    bool shiftHeld() override { return shift; }
    bool rec() override { return recOn; }
    uint8_t focus() override { return place; }
    uint16_t slots() override { return length; }
    uint8_t cursor() override { return cursorAt; }
    void setCursor(uint8_t p) override { cursorAt = p; }
    void showCursor() override { calls.push_back("showCursor"); }
    void stockMove(int16_t d) override
    {
        calls.push_back("stockMove " + std::to_string(d));
        cursorAt = static_cast<uint8_t>(cursorAt + d);
    }
    uint8_t playPosition() override { return playing; }
    bool songPlaying() override { return songOn; }
    bool stopped() override { return !songOn; }
    bool performance() override { return perf; }
    void playSong() override
    {
        calls.push_back("playSong " + std::to_string(cursorAt));
        songOn = true;
        playing = cursorAt;
    }
    void performanceOffAtPatternEnd() override { calls.push_back("performanceOff"); }
    uint8_t labelState() override { return label; }
    void setLabels(uint8_t st, const char* l1, const char* l2, const char* r1, const char* r2) override
    {
        label = st;
        labelTexts = { l1, l2, r1, r2 };
        calls.push_back("setLabels");
    }
    void recolorSoftkeys() override { calls.push_back("recolor"); }
    void followPlayhead() override {}
    int8_t track1PlayMode() override { return mode1; }
    bool queued() override { return armed; }
    int lastRow() override { return length1; }
    int track16Position() override { return row16; }
    void requestNextPattern() override { calls.push_back("request"); }

    bool called(const std::string& what) const
    {
        for (const auto& c : calls)
        {
            if (c.rfind(what, 0) == 0)
            {
                return true;
            }
        }
        return false;
    }
};

// A block as the 1.2.0 tests set it.
void setState(uint8_t sel, uint8_t anchor, uint8_t first, uint8_t last, uint8_t loop, uint8_t end, uint8_t entered)
{
    reset();
    state.sel = sel;
    state.anchor = anchor;
    state.first = first;
    state.last = last;
    state.loop = loop;
    state.end = end;
    state.entered = entered;
}

// Pattern i + 1 at position i.
struct Playlist
{
    uint8_t p[255];
    Playlist()
    {
        for (int i = 0; i < 255; i++)
        {
            p[i] = static_cast<uint8_t>(i + 1);
        }
    }
};

// CE's next position at a pattern end, as switchNextPatternInSong does it, with the mod's
// atPatternEnd and noteEntered around it.
void patternEnd(uint8_t& pos, uint16_t slots)
{
    if (!atPatternEnd(pos))
    {
        pos = static_cast<uint8_t>((pos + 1) % slots);
    }
    noteEntered(pos);
}

void testSelect()
{
    FakeCe ce;
    memset(const_cast<State*>(&state), 0xAA, sizeof(State)); // as SDRAM or a stale project would leave it
    init();
    check(!state.sel && !state.loop, "init: no selection, no loop");
    ce.cursorAt = 4;
    ce.shift = true;
    for (int i = 0; i < 3; i++)
    {
        move(ce, +1);
    }
    check(state.sel && state.first == 4 && state.last == 7 && state.end == 7 && ce.cursorAt == 7,
          "Shift + Down x3 from 5: block 5-8, cursor on 8");
    move(ce, -1);
    check(state.first == 4 && state.last == 6 && ce.cursorAt == 6, "Shift + Up: block 5-7, cursor on the moved edge");
    for (int i = 0; i < 4; i++)
    {
        move(ce, -1);
    }
    check(state.first == 4 && state.last == 4 && !state.sel, "Shift + Up x4: never above the anchor (the top)");
    move(ce, +1);
    move(ce, +1);
    check(state.sel && state.first == 4 && state.last == 6, "Shift + Down again: from the same anchor");
    ce.shift = false;
    ce.calls.clear();
    move(ce, +1);
    check(!state.sel && ce.calls == std::vector<std::string>{ "stockMove 1" },
          "Down without Shift: the selection goes, stock moves the cursor (+1)");

    ce.recOn = false;
    ce.shift = true;
    ce.calls.clear();
    move(ce, +1);
    check(!state.sel && ce.called("stockMove"), "Rec off: Shift + Down doesn't select");

    ce.recOn = true;
    ce.cursorAt = 8;
    for (int i = 0; i < 5; i++)
    {
        move(ce, +1);
    }
    check(state.first == 8 && state.last == 9 && ce.cursorAt == 9, "Shift + Down at the song's end: stops at the last slot");

    FakeCe empty;
    empty.length = 0;
    empty.shift = true;
    reset();
    move(empty, +1);
    check(!state.sel && empty.called("stockMove"), "an empty song: no selection");
}

void testFocus()
{
    // The grid's Up/Down never reach move(): CE's functUp calls it only in the pattern column.
    // labels() drops a selection once the focus leaves the column or Rec goes off.
    FakeCe ce;
    setState(1, 2, 2, 4, 0, 4, 0);
    ce.place = kFocusGrid;
    labels(ce);
    check(!state.sel, "the grid focus: the selection goes");
    setState(1, 2, 2, 4, 0, 4, 0);
    ce.place = kFocusPatterns;
    ce.recOn = false;
    labels(ce);
    check(!state.sel, "Rec off: the selection goes");
    setState(1, 2, 2, 4, 1, 4, 1);
    ce.songOn = true;
    labels(ce);
    check(state.sel && state.loop, "during the loop the block stays without Rec");
}

void testPlaySelection()
{
    FakeCe ce;
    setState(1, 2, 2, 4, 0, 4, 0);
    check(playPatternPressed(ce), "stopped with a block: the mod's");
    check(ce.called("playSong 2") && state.loop && state.entered && ce.cursorAt == 4,
          "Play Selection, stopped: plays from position 3, loop on, the cursor back on the edge");

    FakeCe none;
    reset();
    check(!playPatternPressed(none) && none.calls.empty(), "no selection: stock");

    FakeCe one;
    setState(1, 2, 2, 2, 0, 2, 0);
    check(!playPatternPressed(one), "a block of one position: stock");

    FakeCe beyond;
    beyond.length = 4;
    setState(1, 2, 2, 4, 0, 4, 0);
    check(!playPatternPressed(beyond), "a block past the song's end (a slot deleted): stock");
}

void testPreload()
{
    Playlist pl;
    reset();
    check(preload(3, pl.p, 5) == 5 && !state.wrapPending, "no loop: the preload as stock");

    setState(1, 0, 1, 3, 1, 3, 1);
    check(preload(3, pl.p, 5) == 2 && state.wrapPending, "loop 2-4 at its end: preload pattern 2");
    uint8_t pos = 3;
    patternEnd(pos, 10);
    check(pos == 1 && !state.wrapPending, "pattern end: to position 2");

    setState(1, 5, 5, 7, 1, 7, 0);
    check(preload(2, pl.p, 4) == 4 && !state.wrapPending, "armed, block not reached: on as stock");
    setState(1, 5, 5, 7, 1, 7, 1);
    check(preload(2, pl.p, 4) == 6 && state.wrapPending, "outside the block after reaching it: back to its start");

    setState(1, 5, 5, 7, 1, 7, 0);
    check(preload(6, pl.p, 8) == 8 && state.entered && !state.wrapPending, "inside, not at the end: on as stock, entered");

    Playlist gap;
    gap.p[5] = 0;
    setState(1, 5, 5, 7, 1, 7, 1);
    check(preload(7, gap.p, 9) == 9 && !state.wrapPending, "the block's first position empty: stock");

    // Several rounds of a whole song: the block 3-5 of 8 positions, from position 1.
    setState(1, 2, 2, 4, 1, 4, 0);
    std::vector<int> order;
    pos = 0;
    for (int i = 0; i < 12; i++)
    {
        order.push_back(pos + 1);
        const uint8_t stock = pl.p[(pos + 1) % 8];
        const uint8_t next = preload(pos, pl.p, stock);
        patternEnd(pos, 8);
        check(pl.p[pos] == next, "the preloaded pattern is the one that plays next (" + std::to_string(i) + ")");
    }
    check(order == std::vector<int>{ 1, 2, 3, 4, 5, 3, 4, 5, 3, 4, 5, 3 }, "song order: 1 2, then 3-5 over and over");
}

void testContinue()
{
    FakeCe ce;
    ce.songOn = true;
    ce.playing = 3;
    setState(1, 2, 2, 4, 1, 4, 1);
    check(playPatternPressed(ce), "looping: the mod's");
    check(!state.loop && !state.sel && ce.cursorAt == 3 && !ce.called("performanceOff"),
          "Continue Song: loop and selection off, the cursor on the position playing");
    Playlist pl;
    uint8_t pos = 4;
    check(preload(pos, pl.p, 6) == 6, "then the song plays on after the block");

    FakeCe behind;
    behind.songOn = true;
    behind.playing = 7;
    setState(1, 2, 2, 4, 0, 4, 0);
    check(playPatternPressed(behind), "playing with a block: the mod's");
    check(state.loop && state.entered && !behind.called("playSong"), "the block behind the play position: |: :| at once");
    const uint8_t next = preload(7, pl.p, 9);
    pos = 7;
    patternEnd(pos, 10);
    check(next == 3 && pos == 2, "to its start at the pattern end");

    FakeCe ahead;
    ahead.songOn = true;
    ahead.playing = 1;
    setState(1, 5, 5, 7, 0, 7, 0);
    playPatternPressed(ahead);
    check(state.loop && !state.entered && !ahead.called("playSong"), "Play Selection while playing: armed, the song plays on");
    check(statusIcon("\x1a\x1b", true, 1) == std::string(kIconArmed), "armed: {} in the status bar");
    check(statusIcon("\x1a\x1b", true, 5) == std::string(kIconLooping), "reached: `^");

    FakeCe inside;
    inside.songOn = true;
    inside.playing = 6;
    setState(1, 5, 5, 7, 0, 7, 0);
    playPatternPressed(inside);
    check(state.loop && state.entered, "Play Selection inside the block: looping at once");
}

void testStop()
{
    setState(1, 2, 2, 4, 1, 4, 1);
    state.wrapPending = 1;
    takeStatusDirty();
    stopped();
    check(!state.loop && !state.wrapPending && state.sel, "stop: the loop ends, the block stays selected");
    check(takeStatusDirty() && !takeStatusDirty(), "stop: the status bar redraws once");
    stopped();
    check(!takeStatusDirty(), "stop without a loop: nothing to redraw");

    FakeCe ce;
    setState(1, 2, 2, 4, 0, 4, 0);
    state.wrapPending = 1;
    labels(ce);
    check(!state.wrapPending, "stopped: a decision from playing is dropped");
}

void testLocks()
{
    setState(1, 2, 2, 4, 0, 4, 0);
    check(locked(10), "a block: locked");
    reset();
    check(!locked(10), "nothing: not locked");
    setState(0, 0, 0, 0, 1, 0, 0);
    check(locked(10), "the loop: locked");
    setState(1, 2, 2, 2, 0, 2, 0);
    check(!locked(10), "one position: not locked");

    FakeCe ce;
    setState(1, 2, 2, 4, 0, 4, 0);
    labels(ce);
    check(state.dimmed && ce.called("recolor"), "a block: the softkeys greyed");
    ce.calls.clear();
    labels(ce);
    check(!ce.called("recolor"), "greyed once, not every update");
    state.sel = 0;
    labels(ce);
    check(!state.dimmed && ce.called("recolor"), "no block: the softkeys back");
}

void testMoves()
{
    FakeCe ce;
    ce.songOn = true;
    setState(1, 2, 2, 4, 1, 4, 1);
    ce.cursorAt = 4;
    std::vector<std::vector<int>> moves;
    for (int d : { +1, -1, +9, +1, -20 })
    {
        move(ce, d);
        moves.push_back({ state.first + 1, state.last + 1, ce.cursorAt + 1 });
    }
    check(!ce.called("stockMove") &&
              moves == std::vector<std::vector<int>>{ { 4, 6, 6 }, { 3, 5, 5 }, { 8, 10, 10 }, { 8, 10, 10 }, { 1, 3, 3 } },
          "during a loop, Up/Down/jog move the whole block within the song");

    ce.shift = true;
    move(ce, -1);
    move(ce, -1);
    check(state.first == 0 && state.last == 0 && state.sel, "during the loop Shift + Up shrinks the block to one position");
    move(ce, +1);
    check(state.first == 0 && state.last == 1, "and Shift + Down grows it again");

    FakeCe grid;
    grid.songOn = true;
    grid.place = kFocusGrid;
    setState(1, 2, 2, 4, 1, 4, 1);
    move(grid, +1);
    check(state.first == 2 && !grid.called("stockMove"), "during the loop outside the column: nothing moves");

    FakeCe stock;
    reset();
    stock.cursorAt = 2;
    move(stock, +2);
    check(stock.calls == std::vector<std::string>{ "stockMove 2" }, "jog without a loop: stock cursor move");
}

void testLabels()
{
    FakeCe ce;
    setState(1, 2, 2, 4, 0, 4, 0);
    check(labels(ce) && ce.labelTexts == std::vector<std::string>{ "Play", "Selection", "Play", "Song" } &&
              ce.label == kLabelPlaySelection,
          "stopped with a block: Play Selection / Play Song");
    ce.calls.clear();
    labels(ce);
    check(!ce.called("setLabels"), "the same labels again: not rewritten");

    ce.songOn = true;
    check(labels(ce) && ce.labelTexts == std::vector<std::string>{ "Play", "Selection", "Stop", "" } &&
              ce.label == kLabelPlaySelectionRunning,
          "playing with a block: Play Selection / Stop");

    state.loop = 1;
    check(labels(ce) && ce.labelTexts == std::vector<std::string>{ "Continue", "Song", "Stop", "" } &&
              ce.label == kLabelContinue,
          "looping: Continue Song / Stop");

    FakeCe plain;
    plain.songOn = true;
    reset();
    check(!labels(plain) && !plain.called("setLabels"), "no selection, playing: CE's labels");
    plain.songOn = false;
    check(!labels(plain), "no selection, stopped: CE's labels");
}

void testStatus()
{
    reset();
    takeStatusDirty();
    check(statusIcon("\x1a\x1b", true, 3) == std::string("\x1a\x1b"), "no loop: the play icon");
    setState(1, 5, 5, 7, 1, 7, 0);
    check(statusIcon("\x1a\x1b", false, 3) == std::string("\x1a\x1b"), "a loop, not playing in song mode: the stock icon");
    check(statusIcon("\x1a\x1b", true, 3) == std::string(kIconArmed), "armed: {}");
    state.entered = 1;
    check(statusIcon("\x1a\x1b", true, 3) == std::string(kIconLooping), "looping: `^");
}

void testHold()
{
    reset();
    check(!holdsPerformance(), "no loop: CE's Performance off at the pattern end");
    setState(1, 2, 2, 4, 1, 4, 1);
    check(holdsPerformance(), "the loop: Performance stays on");

    FakeCe ce;
    ce.songOn = true;
    ce.perf = true;
    ce.playing = 3;
    playPatternPressed(ce);
    check(!state.loop && ce.called("performanceOff"), "Continue Song with Performance: CE's Continue Song schedules it off");
}

void testPerformancePreload()
{
    auto rig = [](int8_t mode, bool perf, bool queued) {
        FakeCe ce;
        ce.songOn = true;
        ce.perf = perf;
        ce.mode1 = mode;
        ce.armed = queued;
        ce.length1 = 63;
        return ce;
    };
    FakeCe ce = rig(1, true, false);
    reset();
    std::vector<int> requests;
    for (int row : { 10, 61, 62, 63, 0, 62 })
    {
        ce.row16 = row;
        ce.calls.clear();
        tick(ce);
        requests.push_back(ce.called("request") ? 1 : 0);
    }
    check(requests == std::vector<int>{ 0, 0, 1, 0, 0, 1 }, "track 1 Bwd: one request on track 16's last two rows");

    struct Case
    {
        const char* what;
        int8_t mode;
        bool perf, queued, playing;
    };
    for (const Case& c : { Case{ "Fwd", 0, true, false, true }, Case{ "no Performance", 1, false, false, true },
                           Case{ "queued", 1, true, true, true }, Case{ "stopped", 1, true, false, false } })
    {
        FakeCe k = rig(c.mode, c.perf, c.queued);
        k.songOn = c.playing;
        k.row16 = 62;
        reset();
        tick(k);
        check(!k.called("request"), std::string(c.what) + ": no own request");
    }
}

void testFrames()
{
    // The list from y 91, slot 2 the first shown, 12 rows; the cursor's frame on row 3 (slot 5).
    FrameIn in{ kFocusPatterns, 5, 10, 12, 91, 2, 3, 0, 91 + 3 * kRowHeight };
    int a = 0, b = 0;
    setState(1, 5, 5, 8, 0, 8, 0);
    check(cursorFrame(in, a, b) && a == 91 + 3 * kRowHeight && b == 91 + 7 * kRowHeight, "still frame over the block 6-9");
    check(!greyFrame(in, a, b), "no grey frame with the cursor in the column");

    FrameIn gliding = in;
    gliding.y = 91 + 3 * kRowHeight + 9; // the cursor's frame on its way: the block stays
    check(cursorFrame(gliding, a, b) && a == 91 + 3 * kRowHeight, "the block doesn't glide with the cursor");

    FrameIn scrolling = in;
    scrolling.anim = 9; // the list scrolling: the block moves with the numbers
    check(cursorFrame(scrolling, a, b) && a == 91 + 3 * kRowHeight - 9, "the block scrolls with the list");

    setState(1, 0, 0, 3, 0, 3, 0);
    check(cursorFrame(in, a, b) && a == 91 && b == 91 + 2 * kRowHeight, "the block's top above the list: cut at the top");
    setState(1, 13, 13, 15, 0, 15, 0);
    FrameIn longSong = in;
    longSong.slots = 20;
    check(cursorFrame(longSong, a, b) && b == 91 + 12 * kRowHeight, "the block's bottom below the list: cut at the bottom");
    setState(1, 15, 15, 17, 0, 17, 0);
    check(!cursorFrame(longSong, a, b), "the block all below the list: none");

    reset();
    check(!cursorFrame(in, a, b), "no block: CE's frame");
    state.snap = 1;
    check(cursorFrame(in, a, b) && a == 91 + 3 * kRowHeight && b == 91 + 4 * kRowHeight, "selecting, one row: the cursor's row");

    FrameIn grid = in;
    grid.focus = kFocusGrid;
    setState(1, 5, 5, 8, 1, 8, 1);
    check(!cursorFrame(grid, a, b), "the grid: no cursor frame in the column");
    check(greyFrame(grid, a, b) && a == 91 + 3 * kRowHeight && b == 91 + 7 * kRowHeight, "the grid, looping: the grey block");
    state.loop = 0;
    check(!greyFrame(grid, a, b), "the grid, not looping: no grey block");
}

int countPixels(const uint8_t* b, size_t n)
{
    int count = 0;
    for (size_t i = 0; i < n; i++)
    {
        count += ((b[i] >> 4) ? 1 : 0) + ((b[i] & 15) ? 1 : 0);
    }
    return count;
}

void testIcons()
{
    uint8_t icon[kListIconSize];
    memset(icon, 0x55, sizeof(icon));
    makeListIcon(icon, 16);
    // per side: thick bar 2 x 14, thin bar 1 x 14, two dots 2 x 2 each
    check(countPixels(icon, sizeof(icon)) == 2 * (2 * 14 + 14 + 2 * 2 * 2), "|: :| list icon: every pixel");
    check(icon[3 * 10 + 1] == 0xFF && icon[2 * 10 + 1] == 0, "the thick bar from x 2, y 3");
    makeListIcon(icon, 7);
    check(countPixels(icon, sizeof(icon)) == 2 * 14 + 14 + 2 * 2 * 2, "|: list icon: the left half only");

    // A status-bar font as CE's: 10 bytes a row (20 px), 24 rows.
    const int stride = 10, width = 20, height = 24;
    std::vector<uint8_t> buffer(stride * 64 + stride * height, 0);
    drawGlyphPair(buffer.data(), stride, width, 16);
    const int left = countPixels(buffer.data(), stride * height);
    const int right = countPixels(buffer.data() + stride * 64, stride * height);
    // From x 10 of the first glyph, 15 rows: its columns 0..9 (bars 0, 1, 3, dots 5, 6, 9) in the
    // first glyph, 10..15 (dots 10, bars 12, 14, 15) in the second.
    check(left == 3 * 15 + 3 * 4 && right == 3 * 15 + 4 && left + right == countPixels(buffer.data(), buffer.size()),
          "glyph pair: |: :| across both glyphs, nothing elsewhere in the buffer");
    std::fill(buffer.begin(), buffer.end(), 0);
    drawGlyphPair(buffer.data(), stride, width, 7);
    check(countPixels(buffer.data() + stride * 64, stride * height) == 0, "|: alone: the second glyph empty");
}

} // namespace

int main()
{
    testSelect();
    testFocus();
    testPlaySelection();
    testPreload();
    testContinue();
    testStop();
    testLocks();
    testMoves();
    testLabels();
    testStatus();
    testHold();
    testPerformancePreload();
    testFrames();
    testIcons();
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
