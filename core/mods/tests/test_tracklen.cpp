// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for Variable Track Length: the rules on plain numbers, the table and its
// record, the 1.2.0 side file and its import. What CE's sequencer, grid and buttons do
// with them is in tools/emu/test_tracklen.py. Build and run with `make -C core/mods/tests`.

#include "modsProject.h"
#include "tracklen.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace mods;
using namespace mods::tracklen;

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

class MapIo : public FileIo
{
  public:
    std::map<std::string, std::vector<uint8_t>> files;

    size_t read(const char* path, uint8_t* buffer, size_t capacity) override
    {
        auto it = files.find(path);
        if (it == files.end())
        {
            return kReadFailed;
        }
        const size_t n = it->second.size() < capacity ? it->second.size() : capacity;
        if (n != 0)
        {
            memcpy(buffer, it->second.data(), n);
        }
        return it->second.size();
    }
    bool write(const char* path, const uint8_t* data, size_t length) override
    {
        files[path].assign(data, data + length);
        return true;
    }
    void remove(const char* path) override { files.erase(path); }
    void makeDir(const char*) override {}
};

uint32_t fnv(const uint8_t* data, size_t length)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < length; i++)
    {
        h = (h ^ data[i]) * 16777619u;
    }
    return h;
}

void put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int i = 0; i < 4; i++)
    {
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
}

std::vector<uint8_t> sideFile(const std::vector<std::vector<uint8_t>>& entries)
{
    std::vector<uint8_t> f = { 'T', 'L', 'N', '1' };
    put32(f, static_cast<uint32_t>(entries.size()));
    for (const auto& e : entries)
    {
        f.insert(f.end(), e.begin(), e.end());
    }
    put32(f, fnv(f.data() + 4, f.size() - 4));
    return f;
}

std::vector<uint8_t> entry(uint8_t pattern, uint8_t track, uint8_t length)
{
    std::vector<uint8_t> e(kEntrySize, 0);
    e[0] = pattern;
    e[1 + track] = length;
    return e;
}

void testRules()
{
    check(ownLength(0, 16) == 16, "none plays the pattern");
    check(ownLength(1, 16) == 1, "length 1 counts");
    check(ownLength(15, 16) == 15, "pattern - 1 counts");
    check(ownLength(16, 16) == 16, "the pattern's length is the pattern");
    check(ownLength(20, 16) == 16, "a length the pattern was shortened below plays the pattern");

    // Length 6 in a 16-step pattern plays 0-5, 0-5, 0-3.
    const int expected[16] = { 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3 };
    for (int pos = 0; pos < 16; pos++)
    {
        check(playedStep(pos, 6) == expected[pos], "played step, length 6, position " + std::to_string(pos));
        check(playedStep(pos, 16) == pos, "played step, full length, position " + std::to_string(pos));
        check(playedStep(pos, 1) == 0, "played step, length 1, position " + std::to_string(pos));
    }

    // Own loops: starts at 6 and 12 only; the pattern's start is CE's to count.
    for (int pos = 0; pos < 16; pos++)
    {
        check(startsOwnLoop(pos, 6, 16) == (pos == 6 || pos == 12), "own loop, length 6, position " + std::to_string(pos));
        check(!startsOwnLoop(pos, 16, 16), "no own loop at full length, position " + std::to_string(pos));
    }
    check(startsOwnLoop(1, 1, 16), "length 1 starts a loop every step");
    check(!startsOwnLoop(0, 1, 16), "not at the pattern's start");
}

void testRecording()
{
    // At full length it's CE's arithmetic, for every input.
    for (int length = 1; length <= 16; length++)
    {
        for (int pos = 0; pos < length; pos++)
        {
            for (int late = 0; late < 2; late++)
            {
                for (int off = 0; off < 2; off++)
                {
                    for (int on = 0; on < length; on++)
                    {
                        int ce = pos;
                        if (late)
                        {
                            ce = (ce + 1) % length;
                        }
                        if (off && ce == on)
                        {
                            ce = (ce + 1) % length;
                        }
                        check(recordingStep(pos, length, length, late, off, on) == ce, "full length matches CE");
                    }
                }
            }
        }
    }

    // A hit lands on the step the track is playing: length 6, position 10 is step 4.
    check(recordingStep(10, 6, 16, false, false, -1) == 4, "early hit");
    check(recordingStep(10, 6, 16, true, false, -1) == 5, "late hit: the next step");
    check(recordingStep(11, 6, 16, true, false, -1) == 0, "late hit on the loop's last step: its first");
    check(recordingStep(15, 6, 16, true, false, -1) == 0, "late hit on the pattern's last step: the pattern restarts");
    check(recordingStep(10, 6, 16, false, true, 4) == 5, "note-off moves off its note's step");
    check(recordingStep(11, 6, 16, false, true, 5) == 0, "note-off wraps in the loop");
    check(recordingStep(10, 6, 16, false, true, 2) == 4, "note-off elsewhere stays");
    check(recordingStep(5, 1, 16, true, true, 0) == 0, "length 1 has only one step");
}

void testNoteOffRule()
{
    struct Fx
    {
        int type;
    };
    struct Step
    {
        int note;
        Fx fx[2];
        int volume;
    };
    const int off = -4;

    check(holdsOnlyNoteOff(Step{ off, { { 0 }, { 0 } }, -1 }, off), "a note-off can go");
    check(!holdsOnlyNoteOff(Step{ 60, { { 0 }, { 0 } }, -1 }, off), "a note stays");
    check(!holdsOnlyNoteOff(Step{ -1, { { 0 }, { 0 } }, -1 }, off), "an empty step isn't a note-off");
    check(!holdsOnlyNoteOff(Step{ -5, { { 0 }, { 0 } }, -1 }, off), "nor a cut");
    check(!holdsOnlyNoteOff(Step{ off, { { 3 }, { 0 } }, -1 }, off), "an FX stays");
    check(!holdsOnlyNoteOff(Step{ off, { { 0 }, { 3 } }, -1 }, off), "either FX stays");
    check(!holdsOnlyNoteOff(Step{ off, { { 0 }, { 0 } }, 50 }, off), "a volume stays");
}

void testRoll()
{
    // CE's calcStepLength over a whole pattern, for comparing.
    const auto ce = [](int step, int last, const std::vector<int>& notes)
    {
        int b = 1;
        for (int a = step + 1; a <= last; a++, b++)
        {
            if (notes[a] != 0)
            {
                break;
            }
        }
        return b;
    };

    for (int bits = 0; bits < 256; bits++)
    {
        std::vector<int> notes(16, 0);
        for (int i = 0; i < 8; i++)
        {
            notes[i] = (bits >> i) & 1;
        }
        for (int length = 1; length <= 16; length++)
        {
            // The same pattern with the first `length` steps written out to the end.
            std::vector<int> written(16, 0);
            for (int i = 0; i < 16; i++)
            {
                written[i] = notes[i % length];
            }
            for (int step = 0; step < 16; step++)
            {
                const int got =
                  stepsToNextNote(step, 15, length, [&](int s) { return notes[s] != 0; });
                check(got == ce(step, 15, written), "Roll's look-ahead matches the loop written out");
            }
        }
    }
    int reads = 0;
    stepsToNextNote(0, 127, 3, [&](int) { reads++; return false; });
    check(reads == 3, "the look-ahead reads a loop once");
}

void testTable()
{
    init();
    check(stored(1, 0) == 0, "no lengths after init");
    check(!edit(1, 0, 5, 16), "turning up from the pattern's length changes nothing");
    check(edit(1, 0, -5, 16) && stored(1, 0) == 11, "16 - 5");
    check(edit(1, 0, -50, 16) && stored(1, 0) == 1, "stops at 1");
    check(!edit(1, 0, -1, 16), "stays at 1");
    check(edit(1, 0, 14, 16) && stored(1, 0) == 15, "1 + 14");
    check(edit(1, 0, 1, 16) && stored(1, 0) == 0, "up to the pattern's length is none");
    check(!edit(1, 16, -1, 16), "no such track");
    check(edit(1, 3, -1, 16) && stored(1, 3) == 15 && stored(2, 3) == 0, "per pattern");

    // The interrupt's copy follows the pattern and every edit.
    check(activeStored(1, 3) == 15, "the copy has the pattern's length");
    check(activeStored(2, 3) == 0, "another pattern plays");
    check(activeStored(1, 3) == 15, "back again");
    edit(1, 3, -1, 16);
    check(activeStored(1, 3) == 14, "an edit reaches the copy");
    check(activePattern == 1, "the copy is marked");
}

void testCopyTracks()
{
    init();
    edit(1, 0, -10, 16); // 6
    edit(1, 1, -13, 16); // 3
    edit(2, 0, -4, 16);  // 12
    edit(2, 5, -2, 16);  // 14

    check(copyTracks(1, 3, 0, 0, 16) && stored(3, 0) == 6 && stored(3, 1) == 3 && stored(3, 2) == 0,
          "a whole pattern's lengths");
    check(stored(1, 0) == 6 && stored(1, 1) == 3, "the source keeps its own");
    check(!copyTracks(1, 3, 0, 0, 16), "again changes nothing");

    check(copyTracks(1, 2, 0, 0, 16) && stored(2, 0) == 6 && stored(2, 5) == 0,
          "a track with no length clears the one it's pasted over");

    init();
    edit(1, 2, -10, 16); // 6
    edit(1, 3, -13, 16); // 3
    edit(4, 8, -4, 16);  // 12
    edit(4, 11, -2, 16); // 14
    check(copyTracks(1, 4, 2, 9, 2) && stored(4, 9) == 6 && stored(4, 10) == 3, "a block, to other tracks");
    check(stored(4, 8) == 12 && stored(4, 11) == 14, "tracks outside the block stay");

    // One pattern, overlapping: every length is from before the paste.
    init();
    edit(1, 0, -15, 16); // 1
    edit(1, 1, -14, 16); // 2
    edit(1, 2, -13, 16); // 3
    edit(1, 3, -12, 16); // 4
    check(copyTracks(1, 1, 0, 1, 3) && stored(1, 0) == 1 && stored(1, 1) == 1 && stored(1, 2) == 2 &&
            stored(1, 3) == 3,
          "overlapping, onto later tracks");
    init();
    edit(1, 0, -15, 16);
    edit(1, 1, -14, 16);
    edit(1, 2, -13, 16);
    edit(1, 3, -12, 16);
    check(copyTracks(1, 1, 1, 0, 3) && stored(1, 0) == 2 && stored(1, 1) == 3 && stored(1, 2) == 4 &&
            stored(1, 3) == 4,
          "overlapping, onto earlier tracks");
    check(!copyTracks(1, 1, 1, 1, 3), "onto itself changes nothing");

    init();
    edit(1, 15, -10, 16);
    check(!copyTracks(1, 2, 15, 15, 2) && stored(2, 15) == 0, "past track 16: nothing");
    check(!copyTracks(1, 2, 0, 15, 2) && !copyTracks(1, 2, 15, 0, 2), "either side past track 16: nothing");
    check(!copyTracks(1, 2, 0, 0, 0), "no tracks: nothing");

    // The interrupt's copy.
    init();
    edit(1, 0, -10, 16);
    check(activeStored(2, 0) == 0, "the copy holds the destination");
    copyTracks(1, 2, 0, 0, 1);
    check(activeStored(2, 0) == 6, "a paste reaches the copy");
    check(activePattern == 2, "the copy is marked");

    // The record.
    uint8_t buffer[6144 + 4352];
    init();
    edit(1, 0, -10, 16);
    copyTracks(1, 9, 0, 0, 1);
    Store s(buffer, sizeof(buffer), "MTP1");
    s.clear();
    check(store(s), "a pasted length stores");
    init();
    load(s);
    check(stored(9, 0) == 6 && stored(1, 0) == 6, "and comes back");
    init();
}

void testRecord()
{
    uint8_t buffer[6144 + 4352];
    Store s(buffer, sizeof(buffer), "MTP1");
    s.clear();

    init();
    check(store(s), "nothing to store");
    check(s.find(kRecord, nullptr, nullptr) == nullptr, "no lengths: no record");

    edit(7, 2, -3, 16);
    edit(200, 15, -1, 128);
    check(store(s), "stored");
    uint16_t length = 0;
    const uint8_t* data = s.find(kRecord, nullptr, &length);
    check(data != nullptr && length == 2 * kEntrySize && data[0] == 7 && data[kEntrySize] == 200, "one entry per pattern with a length");

    init();
    load(s);
    check(stored(7, 2) == 13 && stored(200, 15) == 127 && stored(7, 3) == 0, "the record brings them back");

    edit(7, 2, 3, 16);
    edit(200, 15, 1, 128);
    check(store(s), "stored again");
    check(s.find(kRecord, nullptr, nullptr) == nullptr, "no length left: no record");

    // Every pattern with lengths on every track: the largest record fits beside Color Song's.
    init();
    for (int p = 1; p < 256; p++)
    {
        for (int t = 0; t < 16; t++)
        {
            edit(static_cast<uint8_t>(p), static_cast<uint8_t>(t), -(t + 1), 128);
        }
    }
    std::vector<uint8_t> colors(5124, 1);
    check(s.put(1, 1, colors.data(), 5124), "Color Song's largest record");
    check(store(s), "the largest record fits");
    s.find(kRecord, nullptr, &length);
    check(length == kMaxRecordSize, "255 entries");
    init();
    load(s);
    check(stored(255, 15) == 112 && stored(1, 0) == 127, "all of it comes back");

    // Damaged records give no lengths, and don't touch the table past it.
    const uint8_t bad[kEntrySize + 1] = { 3 };
    s.put(kRecord, kRecordVersion, bad, sizeof(bad));
    load(s);
    check(stored(3, 0) == 0 && stored(255, 15) == 0, "a partial entry: no lengths");
    uint8_t zero[kEntrySize] = {};
    zero[1] = 5;
    s.put(kRecord, kRecordVersion, zero, sizeof(zero));
    load(s);
    check(stored(0, 0) == 0, "pattern 0 doesn't exist: no lengths");
}

void testImport()
{
    uint8_t buffer[10496];
    Store s(buffer, sizeof(buffer), "MTP1");
    MapIo io;
    const char* folder = "Projects/song";
    const std::string path = std::string(folder) + "/tracklen.bin";

    s.clear();
    check(!importSideFile(io, folder, s), "no file");

    io.files[path] = sideFile({ entry(1, 0, 6), entry(4, 3, 12) });
    check(importSideFile(io, folder, s), "a real file");
    init();
    load(s);
    check(stored(1, 0) == 6 && stored(4, 3) == 12, "its lengths");

    s.clear();
    io.files[path] = sideFile({ entry(1, 0, 0) });
    check(!importSideFile(io, folder, s), "no length in it: the mod stays Off");
    check(s.find(kRecord, nullptr, nullptr) == nullptr, "and no record");

    const std::vector<uint8_t> good = sideFile({ entry(1, 0, 6) });
    for (size_t i = 0; i < good.size(); i++)
    {
        std::vector<uint8_t> bad = good;
        bad[i] ^= 0x40;
        io.files[path] = bad;
        s.clear();
        check(!importSideFile(io, folder, s), "a damaged byte " + std::to_string(i));
    }
    for (size_t n = 0; n < good.size(); n++)
    {
        io.files[path] = std::vector<uint8_t>(good.begin(), good.begin() + static_cast<long>(n));
        s.clear();
        check(!importSideFile(io, folder, s), "cut short at " + std::to_string(n));
    }
    std::vector<uint8_t> longer = good;
    longer.push_back(0);
    io.files[path] = longer;
    check(!importSideFile(io, folder, s), "too long");

    io.files[path] = sideFile({ entry(0, 0, 6) });
    check(!importSideFile(io, folder, s), "pattern 0");

    std::vector<std::vector<uint8_t>> all;
    for (int p = 1; p < 256; p++)
    {
        all.push_back(entry(static_cast<uint8_t>(p), 15, 3));
    }
    io.files[path] = sideFile(all);
    s.clear();
    check(importSideFile(io, folder, s), "the largest file");
    init();
    load(s);
    check(stored(255, 15) == 3, "all of it");
}

} // namespace

int main()
{
    testRules();
    testRecording();
    testNoteOffRule();
    testRoll();
    testTable();
    testCopyTracks();
    testRecord();
    testImport();
    printf("test_tracklen: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
