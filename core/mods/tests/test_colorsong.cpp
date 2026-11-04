// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for Color Song: how colours are handed out, when pattern files are read, the
// project record, the 1.2.0 file and its import. CE's Song screen drawing and file hooks are
// tested in tools/emu/test_colorsong.py. Build and run with `make -C core/mods/tests`.

#include "colorsong.h"
#include "modsProject.h"
#include "modsSwitches.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace mods;
using namespace mods::colorsong;

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

constexpr uint32_t kTheme = 0xFFFFFF;
constexpr uint32_t kMutedTheme = 0x444344;
constexpr uint32_t kEmpty = 0x121212; // CE's default theme

// CE's steps as bytes: note, instrument, FX 1, FX 2, volume, micro-move.
using Tracks = std::vector<uint8_t>;

Tracks emptyTracks(uint8_t last = 63)
{
    Tracks t(kTracks * kTrackSize);
    for (size_t track = 0; track < kTracks; track++)
    {
        uint8_t* p = t.data() + track * kTrackSize;
        p[0] = last;
        for (size_t s = 0; s < kSteps; s++)
        {
            uint8_t* step = p + 1 + s * kStepSize;
            memset(step, 0, kStepSize);
            step[0] = 0xFF;
            step[6] = 0xFF;
            step[7] = 0xFF;
        }
    }
    return t;
}

uint8_t* stepOf(Tracks& t, size_t track, size_t step)
{
    return t.data() + track * kTrackSize + 1 + step * kStepSize;
}

void note(Tracks& t, size_t track, size_t step, uint8_t value, uint8_t instrument)
{
    uint8_t* s = stepOf(t, track, step);
    s[0] = value;
    s[1] = instrument;
}

void fx(Tracks& t, size_t track, size_t step, size_t slot, uint8_t type, uint8_t value)
{
    uint8_t* s = stepOf(t, track, step);
    s[2 + slot * 2] = type;
    s[3 + slot * 2] = value;
}

class FakeSong : public Song
{
  public:
    uint8_t slot(size_t position) override
    {
        return position < playlist.size() ? playlist[position] : 0;
    }

    uint16_t mask(uint8_t pattern) override
    {
        const auto it = masks.find(pattern);
        return it == masks.end() ? 0 : it->second;
    }

    const uint8_t* unsavedTracks(uint8_t pattern) override
    {
        unsavedAsks++;
        if (pattern != ramPattern)
        {
            return nullptr;
        }
        if (swapAfterAsks > 0 && --swapAfterAsks == 0)
        {
            ramPattern = 0;
            return nullptr;
        }
        return ram.data();
    }

    bool mayReadFile() override { return filesAllowed; }

    const uint8_t* readTracks(uint8_t pattern) override
    {
        reads.push_back(pattern);
        const auto it = files.find(pattern);
        return it == files.end() ? nullptr : it->second.data();
    }

    // The file and the mask CE's updatePatternBitmask gives it.
    void setFile(uint8_t pattern, const Tracks& t)
    {
        files[pattern] = t;
        masks[pattern] = maskOf(t);
    }

    static uint16_t maskOf(const Tracks& t)
    {
        uint16_t m = 0;
        const size_t last = t[0];
        for (size_t track = 0; track < kTracks; track++)
        {
            for (size_t s = 0; s <= last; s++)
            {
                const uint8_t* step = t.data() + track * kTrackSize + 1 + s * kStepSize;
                if (step[0] != 0xFF || step[2] != 0 || step[4] != 0 || step[1] >= 48 ||
                    step[6] < 128 || step[7] < 128)
                {
                    m |= static_cast<uint16_t>(1u << track);
                    break;
                }
            }
        }
        return m;
    }

    std::vector<uint8_t> playlist;
    std::map<uint8_t, uint16_t> masks;
    std::map<uint8_t, Tracks> files;
    std::vector<uint8_t> reads;
    bool filesAllowed = true;
    uint8_t ramPattern = 0;
    Tracks ram;
    int unsavedAsks = 0;
    int swapAfterAsks = 0;
};

class FakeIo : public FileIo
{
  public:
    size_t read(const char* path, uint8_t* buffer, size_t capacity) override
    {
        const auto it = files.find(path);
        if (it == files.end())
        {
            return kReadFailed;
        }
        const size_t n = std::min(it->second.size(), capacity);
        memcpy(buffer, it->second.data(), n);
        return it->second.size();
    }
    bool write(const char* path, const uint8_t* data, size_t length) override
    {
        files[path] = std::vector<uint8_t>(data, data + length);
        return true;
    }
    void remove(const char* path) override { files.erase(path); }
    void makeDir(const char*) override {}

    std::map<std::string, std::vector<uint8_t>> files;
};

struct Rig
{
    Rig()
      : store(buffer, sizeof(buffer), project::kMagic)
    {
        store.clear();
        reset();
        switches[static_cast<size_t>(Mod::colorsong)] = 1;
    }

    // Song screen updates until nothing is left to read.
    void settle()
    {
        for (int i = 0; i < 300; i++)
        {
            changed = update(song, store) || changed;
        }
    }

    uint32_t color(uint8_t pattern, uint8_t track, bool muted = false)
    {
        return blockColor(pattern, track, song.mask(pattern), muted, kTheme, kEmpty);
    }

    uint8_t buffer[6144];
    Store store;
    FakeSong song;
    bool changed = false;
};

// A hue family from an RGB: the colours are made from HSL, so compare by hue.
int hueOf(uint32_t rgb)
{
    const int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    const int high = std::max({ r, g, b }), low = std::min({ r, g, b });
    const int d = high - low;
    if (d == 0)
    {
        return 0;
    }
    int h;
    if (high == r)
    {
        h = 60 * (g - b) / d;
    }
    else if (high == g)
    {
        h = 120 + 60 * (b - r) / d;
    }
    else
    {
        h = 240 + 60 * (r - g) / d;
    }
    return (h + 360) % 360;
}

bool sameHue(uint32_t a, uint32_t b)
{
    const int d = std::abs(hueOf(a) - hueOf(b));
    return std::min(d, 360 - d) <= 3;
}

int lightOf(uint32_t rgb)
{
    const int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    return (std::max({ r, g, b }) + std::min({ r, g, b })) * 100 / 510;
}

void testIds()
{
    check(kRecord == 1 && static_cast<int>(Mod::colorsong) == 0, "mod 0, record 1");
    check(kMaxRecordSize == 5124, "the largest record");
}

void testColors()
{
    Rig r;
    Tracks a = emptyTracks();
    note(a, 0, 0, 60, 1);
    Tracks copy = a; // pattern 2: the same in another column
    memmove(stepOf(copy, 3, 0), stepOf(copy, 0, 0), kStepSize);
    memset(stepOf(copy, 0, 0), 0, kStepSize);
    stepOf(copy, 0, 0)[0] = 0xFF;
    stepOf(copy, 0, 0)[6] = 0xFF;
    stepOf(copy, 0, 0)[7] = 0xFF;
    r.song.setFile(1, a);
    r.song.setFile(2, copy);
    r.song.playlist = { 1, 2 };

    check(r.color(1, 0) == kTheme, "before the song is read: the theme's colour");
    r.settle();
    check(r.color(1, 0) == 0xE31C1C, "the first family is red, in the main shade");
    check(r.color(2, 3) == r.color(1, 0), "identical content in another column: identical colour");
    check(r.changed, "colours go in the record");

    // shade: other content, same instruments
    Tracks fill = a;
    note(fill, 1, 0, 60, 1);
    note(fill, 1, 4, 60, 1);
    Tracks otherNote = a;
    note(otherNote, 1, 0, 61, 1);
    Tracks withFx = a;
    note(withFx, 1, 0, 60, 1);
    fx(withFx, 1, 0, 0, 30, 5);
    Tracks otherSample = a;
    note(otherSample, 1, 0, 60, 2);
    Tracks twoSamples = a;
    note(twoSamples, 1, 0, 60, 1);
    note(twoSamples, 1, 4, 60, 2);
    Tracks fxOnly = a;
    fx(fxOnly, 1, 0, 0, 30, 5);
    r.song.setFile(3, fill);
    r.song.setFile(4, otherNote);
    r.song.setFile(5, withFx);
    r.song.setFile(6, otherSample);
    r.song.setFile(7, twoSamples);
    r.song.setFile(8, fxOnly);
    r.song.playlist = { 1, 2, 3, 4, 5, 6, 7, 8 };
    r.settle();

    const uint32_t base = r.color(1, 0);
    check(r.color(1, 0) == 0xE31C1C, "what's there keeps its colour");
    std::set<uint32_t> shades = { base };
    for (uint8_t p : { 3, 4, 5 })
    {
        check(sameHue(r.color(p, 1), base), "same instruments, same hue: pattern " + std::to_string(p));
        shades.insert(r.color(p, 1));
    }
    check(shades.size() == 4, "and different shades");
    check(!sameHue(r.color(6, 1), base), "another instrument: another hue");
    check(!sameHue(r.color(7, 1), base) && !sameHue(r.color(7, 1), r.color(6, 1)),
          "a set of instruments is a family of its own");
    const uint32_t gray = r.color(8, 1);
    const int spread = std::max({ gray >> 16 & 255, gray >> 8 & 255, gray & 255 }) -
                       std::min({ gray >> 16 & 255, gray >> 8 & 255, gray & 255 });
    check(spread < 30, "FX only: nearly gray");

    // golden angle: second family 137.5 degrees on, third 275
    check(std::abs(hueOf(r.color(6, 1)) - 137) <= 2, "the second family a golden angle on");
    check(std::abs(hueOf(r.color(7, 1)) - 275) <= 2, "the third another one on");

    // muted
    const uint32_t muted = r.color(1, 0, true);
    check(muted != base && sameHue(muted, base), "muted keeps its hue");
    check(lightOf(muted) < lightOf(base), "and is darker");
    check(r.color(1, 0) == base, "unmuted again: the same colour");

    // what a block must not depend on
    check(blockColor(1, 0, 0x7777, false, kTheme, kEmpty) == kTheme, "another mask: the theme's colour");
    check(blockColor(0, 0, 0, false, kTheme, kEmpty) == kTheme, "no pattern: the theme's colour");
    check(blockColor(1, 16, r.song.mask(1), false, kTheme, kEmpty) == kTheme, "no such track: the theme's");
    check(blockColor(1, 0, r.song.mask(1), true, kMutedTheme, kEmpty) == muted,
          "a block with a colour doesn't use the theme's");
}

void testHumanizeIgnored()
{
    Rig r;
    Tracks a = emptyTracks();
    note(a, 0, 0, 60, 1);
    note(a, 0, 4, 62, 1);

    Tracks columns = a;
    stepOf(columns, 0, 0)[6] = 40; // volume column
    stepOf(columns, 0, 4)[7] = 12; // micro-move column
    Tracks asFx = a;
    fx(asFx, 0, 0, 0, 2, 9);  // m
    fx(asFx, 0, 4, 1, 18, 40); // V
    r.song.setFile(1, a);
    r.song.setFile(2, columns);
    r.song.setFile(3, asFx);

    Tracks onlyColumn = a;
    stepOf(onlyColumn, 1, 0)[6] = 40;
    r.song.setFile(4, onlyColumn);
    Tracks onlyFx = a;
    fx(onlyFx, 1, 0, 0, 2, 9);
    r.song.setFile(5, onlyFx);

    r.song.playlist = { 1, 2, 3, 4, 5 };
    r.settle();
    check(r.color(2, 0) == r.color(1, 0), "the Volume and Micro-move columns don't count");
    check(r.color(3, 0) == r.color(1, 0), "nor do m and V as FX, in either slot");
    check(r.color(4, 1) == r.color(5, 1), "a step holding only them counts as empty");
    check(r.color(4, 1) != r.color(1, 0), "and such a block is the FX-only kind");
}

void testPatternLength()
{
    Rig r;
    Tracks a = emptyTracks(15);
    note(a, 0, 0, 60, 1);
    Tracks longer = emptyTracks(31);
    note(longer, 0, 0, 60, 1);
    Tracks past = emptyTracks(15);
    note(past, 0, 0, 60, 1);
    note(past, 0, 20, 70, 1); // past the end: not played, not drawn, doesn't count
    r.song.setFile(1, a);
    r.song.setFile(2, longer);
    r.song.setFile(3, past);
    r.song.playlist = { 1, 2, 3 };
    r.settle();
    check(sameHue(r.color(1, 0), r.color(2, 0)) && r.color(1, 0) != r.color(2, 0),
          "another length: same hue, another shade");
    check(r.color(3, 0) == r.color(1, 0), "steps past the end don't count");
}

void testSticky()
{
    Rig r;
    Tracks a = emptyTracks();
    note(a, 0, 0, 60, 1);
    Tracks b = emptyTracks();
    note(b, 0, 0, 60, 2);
    r.song.setFile(1, a);
    r.song.setFile(2, b);
    r.song.playlist = { 1, 2 };
    r.settle();
    const uint32_t colorA = r.color(1, 0);
    const uint32_t colorB = r.color(2, 0);

    // an edit recolours only the edited block
    Tracks edited = b;
    note(edited, 0, 8, 64, 2);
    r.song.setFile(2, edited);
    patternChanged(2);
    r.settle();
    check(r.color(1, 0) == colorA, "an edit leaves other blocks alone");
    check(r.color(2, 0) != colorB && sameHue(r.color(2, 0), colorB), "the edited block: another shade");

    // every edit shows: back to the old content doesn't give the old shade at once
    const uint32_t editedColor = r.color(2, 0);
    r.song.setFile(2, b);
    patternChanged(2);
    r.settle();
    check(r.color(2, 0) != editedColor && r.color(2, 0) != colorB,
          "a colour an edit released doesn't come straight back");

    // a new family in the song doesn't move the old ones
    Tracks c = emptyTracks();
    note(c, 0, 0, 60, 3);
    r.song.setFile(3, c);
    r.song.playlist = { 3, 1, 2 };
    const uint32_t before = r.color(2, 0);
    r.settle();
    check(r.color(1, 0) == colorA && r.color(2, 0) == before, "a new first row doesn't move the others");
    check(!sameHue(r.color(3, 0), colorA) && !sameHue(r.color(3, 0), before), "it takes an unused hue");

    // twenty families have twenty hues
    Rig many;
    std::set<int> hues;
    std::vector<uint8_t> list;
    for (uint8_t i = 1; i <= 20; i++)
    {
        Tracks t = emptyTracks();
        note(t, 0, 0, 60, i);
        many.song.setFile(i, t);
        list.push_back(i);
    }
    many.song.playlist = list;
    many.settle();
    for (uint8_t i = 1; i <= 20; i++)
    {
        hues.insert(hueOf(many.color(i, 0)));
    }
    check(hues.size() == 20, "twenty families, twenty hues");
}

void testFileRules()
{
    Rig r;
    Tracks a = emptyTracks();
    note(a, 0, 0, 60, 1);
    Tracks empty = emptyTracks();
    for (uint8_t p = 1; p <= 4; p++)
    {
        r.song.setFile(p, a);
    }
    r.song.setFile(5, empty);
    r.song.playlist = { 1, 2, 3, 4, 5 };

    r.song.filesAllowed = false;
    update(r.song, r.store);
    check(r.song.reads.empty(), "no file while reading isn't allowed");
    r.song.filesAllowed = true;

    for (size_t i = 1; i <= 4; i++)
    {
        update(r.song, r.store);
        check(r.song.reads.size() == i, "one file per update, call " + std::to_string(i));
    }
    r.settle();
    check(r.song.reads.size() == 4, "an empty pattern isn't read");
    check(r.color(4, 0) != kTheme, "the song has colours");

    r.song.reads.clear();
    r.settle();
    check(r.song.reads.empty(), "with nothing changed, nothing more is read");

    patternChanged(2);
    check(r.color(2, 0) == kTheme, "a changed pattern draws plain until it's read");
    check(r.color(1, 0) != kTheme, "the others don't");
    r.settle();
    check(r.song.reads == std::vector<uint8_t>{ 2 }, "a change rereads only that pattern");
    check(r.color(2, 0) != kTheme, "and it has its colour again");

    // a pattern in the song that isn't in the playlist yet
    r.song.reads.clear();
    Tracks b = emptyTracks();
    note(b, 0, 0, 60, 2);
    r.song.setFile(9, b);
    r.settle();
    check(r.song.reads.empty(), "a pattern outside the song isn't read");
    r.song.playlist.push_back(9);
    r.settle();
    check(r.song.reads == std::vector<uint8_t>{ 9 }, "a slot added to the song is read at once");
    check(r.color(9, 0) != kTheme, "and gets a colour");

    // a mask changing behind our back
    r.song.reads.clear();
    Tracks wider = a;
    note(wider, 5, 0, 60, 1);
    r.song.setFile(3, wider);
    check(r.color(3, 0) == kTheme, "a new mask draws plain");
    r.settle();
    check(r.song.reads == std::vector<uint8_t>{ 3 }, "and the pattern is read");

    // a file that can't be read
    r.song.reads.clear();
    r.song.masks[10] = 1;
    r.song.playlist.push_back(10);
    r.settle();
    check(r.song.reads == std::vector<uint8_t>{ 10 }, "a missing file is tried once");
    check(r.color(10, 0) == kTheme, "and stays plain");
    check(r.color(1, 0) != kTheme, "the rest keep their colours");
}

void testUnsavedPattern()
{
    Rig r;
    Tracks saved = emptyTracks();
    note(saved, 0, 0, 60, 1);
    Tracks edits = saved;
    note(edits, 0, 4, 64, 1);
    r.song.setFile(1, saved);
    r.song.masks[1] = FakeSong::maskOf(edits);
    r.song.ramPattern = 1;
    r.song.ram = edits;
    r.song.playlist = { 1 };
    r.settle();
    check(r.song.reads.empty(), "the current pattern with unsaved edits comes from RAM");
    const uint32_t fromRam = r.color(1, 0);
    check(fromRam != kTheme, "and has a colour");

    // the audio interrupt moves on to the next pattern during the hash
    Rig s;
    s.song.setFile(1, edits);
    s.song.ramPattern = 1;
    s.song.ram = saved;
    s.song.swapAfterAsks = 2;
    s.song.playlist = { 1 };
    update(s.song, s.store);
    check(s.color(1, 0) == kTheme, "a pattern swap during the hash doesn't count");
    s.settle();
    check(s.song.reads == std::vector<uint8_t>{ 1 }, "the pattern's file is read instead");
    check(s.color(1, 0) != kTheme, "and it gets a colour");
}

void testRecord()
{
    Rig r;
    Tracks a = emptyTracks();
    note(a, 0, 0, 60, 1);
    note(a, 1, 0, 61, 1);
    Tracks b = emptyTracks();
    note(b, 0, 0, 60, 2);
    r.song.setFile(1, a);
    r.song.setFile(2, b);
    r.song.playlist = { 1, 2 };
    r.settle();
    check(r.changed, "the first colours change the record");
    r.changed = false;
    r.settle();
    check(!r.changed, "no change, no write");

    uint8_t version = 0;
    uint16_t length = 0;
    const uint8_t* image = r.store.find(kRecord, &version, &length);
    check(image != nullptr && version == kRecordVersion, "the record is there");
    check(length == kFileHeaderSize + 5 * kFileRecordSize, "2 families and 3 variants");
    check(image != nullptr && memcmp(image, "CSG1", 4) == 0, "in 1.2.0's format");

    // a project's colours come back, whatever the order of appearance is now
    Tracks c = emptyTracks();
    note(c, 0, 0, 60, 3);
    r.song.setFile(3, c);
    r.song.playlist = { 3, 2, 1 };
    r.settle();
    const uint32_t colorA = r.color(1, 0), colorA2 = r.color(1, 1), colorB = r.color(2, 0), colorC = r.color(3, 0);
    check(sameHue(colorA, 0xE31C1C), "the first family stays red, now in the last row");

    reset();
    r.changed = false;
    r.song.reads.clear();
    check(r.color(1, 0) == kTheme, "a project load forgets the table");
    r.settle();
    check(r.song.reads.size() == 3, "the song is read again");
    check(r.color(1, 0) == colorA && r.color(1, 1) == colorA2 && r.color(2, 0) == colorB &&
            r.color(3, 0) == colorC,
          "and the record brings the colours back");
    check(!r.changed, "with nothing new to store");

    // the rotation goes on after the loaded colours
    Tracks d = emptyTracks();
    note(d, 0, 0, 60, 4);
    r.song.setFile(4, d);
    r.song.playlist = { 3, 2, 1, 4 };
    r.settle();
    check(!sameHue(r.color(4, 0), colorA) && !sameHue(r.color(4, 0), colorB) && !sameHue(r.color(4, 0), colorC),
          "a new family after a load doesn't take a hue in use");
    check(r.changed, "and goes in the record");

    // leaving the song drops it from the record
    r.changed = false;
    r.song.playlist = { 3 };
    r.settle();
    image = r.store.find(kRecord, nullptr, &length);
    check(r.changed && length == kFileHeaderSize + 2 * kFileRecordSize, "only what's in the song is kept");

    // a damaged record is ignored
    Rig damaged;
    damaged.song.setFile(1, a);
    damaged.song.playlist = { 1 };
    damaged.settle();
    const uint32_t expected = damaged.color(1, 0);
    uint16_t size = 0;
    const uint8_t* record = damaged.store.find(kRecord, nullptr, &size);
    std::vector<uint8_t> bad(record, record + size);
    bad[kFileHeaderSize + 4] ^= 0x40;
    damaged.store.put(kRecord, kRecordVersion, bad.data(), size);
    reset();
    damaged.settle();
    check(damaged.color(1, 0) == expected, "a damaged record: colours from song order");
    check(damaged.store.find(kRecord, nullptr, &size) != nullptr && memcmp(bad.data(), damaged.store.find(kRecord, nullptr, nullptr), size) != 0,
          "and it is replaced");

    // no room in the store
    uint8_t tiny[16 + 8];
    Store small(tiny, sizeof(tiny), project::kMagic);
    small.clear();
    Rig full;
    full.song.setFile(1, a);
    full.song.playlist = { 1 };
    for (int i = 0; i < 20; i++)
    {
        update(full.song, small);
    }
    check(full.color(1, 0) != kTheme, "no room for the record: colours still work");
    check(small.empty(), "and the store is left alone");

    // as many as the record can hold: a song's worst case
    Rig most;
    std::vector<uint8_t> list;
    for (uint8_t i = 1; i <= 120; i++)
    {
        Tracks t = emptyTracks();
        for (uint8_t track = 0; track < 16; track++)
        {
            note(t, track, 0, static_cast<uint8_t>(track + i % 40), static_cast<uint8_t>(i % 48));
            note(t, track, 1 + track, 60, static_cast<uint8_t>(track));
        }
        most.song.setFile(i, t);
        list.push_back(i);
    }
    most.song.playlist = list;
    most.settle();
    size = 0;
    check(most.store.find(kRecord, nullptr, &size) != nullptr && size <= kMaxRecordSize,
          "a big song's record fits: " + std::to_string(size));
    check(size > 5000, "with the most variants it can hold");
    check(most.color(1, 0) != kTheme, "the first blocks have colours");
    check(most.color(120, 15) == kTheme, "past 512 variants, blocks stay plain");
    check(!most.changed || most.store.find(kRecord, nullptr, nullptr) != nullptr, "and nothing breaks");
}

// 1.2.0's own colorsong.bin from a real project.
const uint8_t kRealFile[] = {
#include "colorsong_sample.inc"
};

std::vector<uint8_t> makeFile(uint32_t familyKey, uint8_t slot, uint16_t content, uint8_t shade)
{
    std::vector<uint8_t> r(kFileHeaderSize + 2 * kFileRecordSize);
    memcpy(r.data(), "CSG1", 4);
    r[4] = 1;
    r[6] = 1;
    uint8_t* p = r.data() + kFileHeaderSize;
    memcpy(p, &familyKey, 4);
    p[4] = slot;
    memcpy(p + 8, &familyKey, 4);
    p[12] = static_cast<uint8_t>(content);
    p[13] = static_cast<uint8_t>(content >> 8);
    p[14] = shade;
    uint32_t sum = 0;
    for (int i = 0; i < 2; i++)
    {
        uint32_t h = 2166136261u;
        for (size_t b = 0; b < kFileRecordSize; b++)
        {
            h = (h ^ p[i * kFileRecordSize + b]) * 16777619u;
        }
        sum += h;
    }
    memcpy(r.data() + 8, &sum, 4);
    return r;
}

const char kFolder[] = "/Projects/Song";
const char kPath[] = "/Projects/Song/colorsong.bin";

void testImportFile()
{
    FakeIo io;
    uint8_t buffer[6144];
    Store store(buffer, sizeof(buffer), project::kMagic);
    store.clear();

    check(!importSideFile(io, kFolder, store), "no file: nothing to import");
    check(store.empty(), "and no record");

    io.files[kPath] = std::vector<uint8_t>(std::begin(kRealFile), std::end(kRealFile));
    check(importSideFile(io, kFolder, store), "a real 1.2.0 file imports");
    uint16_t length = 0;
    const uint8_t* record = store.find(kRecord, nullptr, &length);
    check(length == sizeof(kRealFile) && memcmp(record, kRealFile, length) == 0, "byte for byte");

    const auto refused = [&](std::vector<uint8_t> file, const char* name) {
        store.clear();
        io.files[kPath] = std::move(file);
        check(!importSideFile(io, kFolder, store), name);
        check(store.empty(), std::string(name) + ": no record");
    };
    const std::vector<uint8_t> real(std::begin(kRealFile), std::end(kRealFile));
    std::vector<uint8_t> broken = real;
    broken[20] ^= 1;
    refused(broken, "a bad checksum is refused");
    broken = real;
    broken[0] = 'X';
    refused(broken, "another magic is refused");
    broken = real;
    broken.pop_back();
    refused(broken, "a short file is refused");
    broken = real;
    broken.push_back(0);
    refused(broken, "a long file is refused");
    broken = real;
    broken[5] = 1;
    refused(broken, "too many families are refused");
    broken = real;
    broken[7] = 3;
    refused(broken, "too many variants are refused");
    refused({ 'C', 'S', 'G', '1' }, "only a magic is refused");
    refused(std::vector<uint8_t>(kFileHeaderSize, 0), "zeros are refused");
    broken.assign(kFileHeaderSize, 0);
    memcpy(broken.data(), "CSG1", 4);
    refused(broken, "no records is refused");
    broken.assign(kMaxRecordSize + 8, 0);
    memcpy(broken.data(), "CSG1", 4);
    refused(broken, "a huge file is refused");

    // No room: nothing is imported.
    uint8_t tiny[64];
    Store small(tiny, sizeof(tiny), project::kMagic);
    small.clear();
    io.files[kPath] = real;
    check(!importSideFile(io, kFolder, small), "no room: refused");
    check(small.empty(), "and the store is left alone");
}

// A pattern with one note, instrument 1 on track 0. Its keys, from 1.2.0's fingerprint code on
// the same steps (checked against a real project in tools/emu/test_colorsong.py).
constexpr uint32_t kGoldenFamily = 0x94230D47u;
constexpr uint16_t kGoldenContent = 0x1AF;

void testImportedColors()
{
    Rig r;
    Tracks a = emptyTracks(15);
    note(a, 0, 0, 60, 1);
    r.song.setFile(1, a);
    r.song.playlist = { 1 };
    r.settle();
    // Learn this block's content key from the record it wrote.
    uint16_t length = 0;
    const uint8_t* image = r.store.find(kRecord, nullptr, &length);
    check(length == kFileHeaderSize + 2 * kFileRecordSize, "one family, one variant");
    uint32_t family = 0;
    memcpy(&family, image + kFileHeaderSize, 4);
    uint16_t content = 0;
    memcpy(&content, image + kFileHeaderSize + kFileRecordSize + 4, 2);
    check(family == kGoldenFamily, "the instrument set hashes as on 1.2.0");
    check(content == kGoldenContent, "the content hashes as on 1.2.0");

    // Hue slot 3, shade 2: on 1.2.0 this block's colour is hsl(3 * 137.5 = 52, 70, 34).
    Rig imported;
    FakeIo io;
    io.files[kPath] = makeFile(family, 3, content, 2);
    check(importSideFile(io, kFolder, imported.store), "the file imports");
    imported.song.setFile(1, a);
    imported.song.playlist = { 1 };
    imported.settle();
    check(imported.color(1, 0) == 0x93831A, "the colour the file gives, as on 1.2.0");
    check(imported.store.find(kRecord, nullptr, &length) != nullptr, "the record stays");

    // A new family after the import takes the slot after the file's highest.
    Tracks b = emptyTracks(15);
    note(b, 0, 0, 60, 2);
    imported.song.setFile(2, b);
    imported.song.playlist = { 1, 2 };
    imported.settle();
    check(std::abs(hueOf(imported.color(2, 0)) - 190) <= 2, "the next family continues the rotation");
}

void testImportThroughProject()
{
    const size_t index = static_cast<size_t>(Mod::colorsong);
    const size_t humanizeIndex = static_cast<size_t>(Mod::humanize);
    uint8_t buffer[6144];
    Store store(buffer, sizeof(buffer), project::kMagic);
    uint8_t defaults[kModCount] = {};
    uint8_t set[kModCount] = {};
    const std::vector<uint8_t> real(std::begin(kRealFile), std::end(kRealFile));

    {
        FakeIo io;
        io.files[kPath] = real;
        project::loadFromWorkspace(io, store, kFolder, defaults, set, kModCount);
        check(set[index] == 1, "a 1.2.0 project with the file: the mod starts On");
        check(store.find(kRecord, nullptr, nullptr) != nullptr, "with its colours");
        check(io.files.count(project::kWorkspaceFile) == 1, "written to the Workspace");
    }
    {
        // A project an older CE build saved: a mods.bin with Humanize On, no import marker.
        FakeIo io;
        io.files[kPath] = real;
        Store other(buffer, sizeof(buffer), project::kMagic);
        other.clear();
        uint8_t oldSet[15] = {};
        oldSet[humanizeIndex] = 1;
        other.put(kSwitchesRecord, kSwitchesVersion, oldSet, sizeof(oldSet));
        const uint8_t humanizeRecord[5] = { 7, 1, 3, 49, 49 };
        other.put(static_cast<uint8_t>(humanizeIndex + 1), 1, humanizeRecord, sizeof(humanizeRecord));
        const size_t size = other.finalize();
        io.files[project::kWorkspaceFile] = std::vector<uint8_t>(buffer, buffer + size);

        std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
        project::loadFromWorkspace(io, store, kFolder, defaults, set, kModCount);
        check(set[index] == 1, "with a mods.bin from before Color Song: the import still runs");
        check(set[humanizeIndex] == 1, "Humanize stays On");
        check(store.find(static_cast<uint8_t>(humanizeIndex + 1), nullptr, nullptr) != nullptr,
              "and keeps its settings");
        uint16_t length = 0;
        const uint8_t* record = store.find(kRecord, nullptr, &length);
        check(record != nullptr && length == real.size() && memcmp(record, real.data(), length) == 0,
              "the colours come in");

        // Once only: a change of mind (mod Off, colours kept) isn't undone by the file.
        set[index] = 0;
        project::setSwitches(store, set, kModCount);
        project::saveToWorkspace(io, store);
        std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
        project::loadFromWorkspace(io, store, kFolder, defaults, set, kModCount);
        check(set[index] == 0, "turned Off, it stays Off");
    }
    {
        // The largest record, with room left for the other mods.
        FakeIo io;
        Store big(buffer, sizeof(buffer), project::kMagic);
        big.clear();
        uint8_t* image = big.reserve(kRecord, kRecordVersion, kMaxRecordSize);
        check(image != nullptr, "the largest record fits in the project buffer");
        check(big.capacity() - big.finalize() > 512, "with room to spare");
    }
    {
        FakeIo io;
        io.files[kPath] = real;
        std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
        project::loadFromWorkspace(io, store, nullptr, defaults, set, kModCount);
        check(set[index] == 0, "a new project ignores the file");
    }
}

void testThemes()
{
    Rig r;
    uint8_t shadeCount = 0;
    std::vector<uint8_t> list;
    for (uint8_t p = 1; p <= 6; p++)
    {
        Tracks t = emptyTracks();
        note(t, 0, 0, static_cast<uint8_t>(60 + p), 1);
        r.song.setFile(p, t);
        list.push_back(p);
        shadeCount++;
    }
    r.song.playlist = list;
    r.settle();
    check(shadeCount == 6, "six shades");

    // CE's themes: the empty blocks are grey_12 (default, anniversary) and 0x3E3E3E (whiteout).
    for (uint8_t p = 1; p <= 6; p++)
    {
        const uint16_t mask = r.song.mask(p);
        const uint32_t plain = blockColor(p, 0, mask, false, kTheme, 0x121212);
        const uint32_t dimmed = blockColor(p, 0, mask, true, kTheme, 0x121212);
        check(lightOf(plain) >= 14 && lightOf(dimmed) >= 13, "dark theme: stands clear of empty blocks");
        const uint32_t whiteout = blockColor(p, 0, mask, false, kTheme, 0x3E3E3E);
        const uint32_t whiteoutMuted = blockColor(p, 0, mask, true, kTheme, 0x3E3E3E);
        check(lightOf(whiteout) >= 30 && lightOf(whiteoutMuted) >= 30, "whiteout: lighter than its empty blocks");
        const uint32_t light = blockColor(p, 0, mask, false, kTheme, 0xE8E8E8);
        check(lightOf(light) <= 84, "a light theme: darker than its empty blocks");
    }
}

} // namespace

int main()
{
    testIds();
    testColors();
    testHumanizeIgnored();
    testPatternLength();
    testSticky();
    testFileRules();
    testUnsavedPattern();
    testRecord();
    testImportFile();
    testImportedColors();
    testImportThroughProject();
    testThemes();
    printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
