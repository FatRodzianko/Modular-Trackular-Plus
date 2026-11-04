// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the humanize mod: the rules for each step against CE's step layout, the
// windows, the project record, the 1.2.0 side file and the Pattern button list. Build
// and run with `make -C core/mods/tests`.

#include "humanize.h"
#include "modsPatternButtons.h"
#include "modsProject.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace mods;
using namespace mods::humanize;

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

// CE's strStep (mtSequencer.h)
struct Step
{
    int8_t note = -1;
    uint8_t instrument = 0;
    struct
    {
        uint8_t type = 0;
        uint8_t value = 0;
    } fx[2];
    int8_t volume = -1;
    int8_t micromove = -1;
};

constexpr FxTypes kTypes{ 2, 18 };
constexpr uint8_t kOtherFx = 5;

int testRandom(int lo, int hi)
{
    return lo + rand() % (hi - lo + 1);
}

// Every value one setting of a step can get over many presses, from the same start.
template <class Get>
std::set<int> outcomes(const Step& start, const Settings& s, bool sample, Get get)
{
    std::set<int> seen;
    for (int i = 0; i < 4000; i++)
    {
        Step step = start;
        humanizeStep(step, s, sample, kTypes, testRandom);
        seen.insert(get(step));
    }
    return seen;
}

std::set<int> span(int lo, int hi)
{
    std::set<int> values;
    for (int i = lo; i <= hi; i++)
    {
        values.insert(i);
    }
    return values;
}

int micromoveOf(const Step& s)
{
    return s.micromove;
}

int volumeOf(const Step& s)
{
    return s.volume;
}

int fx0Value(const Step& s)
{
    return s.fx[0].value;
}

void testSteps()
{
    Settings s;

    // not notes
    for (int note : { -1, -2, -3, -4 })
    {
        Step step;
        step.note = static_cast<int8_t>(note);
        step.volume = 40;
        step.micromove = 40;
        const Step before = step;
        humanizeStep(step, s, true, kTypes, testRandom);
        check(memcmp(&step, &before, sizeof(Step)) == 0, "a step without a note is left alone");
    }

    Step note;
    note.note = 60;

    // empty columns, the FX slots stay as they are
    check(outcomes(note, s, true, micromoveOf) == span(0, 10), "empty m: 0..T");
    check(outcomes(note, s, true, volumeOf) == span(45, 55), "empty V on a sample: around Sample volume");
    {
        Settings t = s;
        t.sampleVolume = 30 - 1;
        t.midiVelocity = 80 - 1;
        check(outcomes(note, t, true, volumeOf) == span(25, 35), "Sample volume sets the center");
        check(outcomes(note, t, false, volumeOf) == span(75, 85), "MIDI velocity sets the center");
    }
    for (int i = 0; i < 200; i++)
    {
        Step step = note;
        step.fx[1].type = kOtherFx;
        step.fx[1].value = 77;
        humanizeStep(step, s, true, kTypes, testRandom);
        check(step.fx[0].type == 0 && step.fx[0].value == 0, "a free FX slot stays free");
        check(step.fx[1].type == kOtherFx && step.fx[1].value == 77, "another FX is not touched");
        check(step.note == 60 && step.instrument == 0, "note and instrument are not touched");
    }

    // values already there
    {
        Step step = note;
        step.micromove = 50;
        step.volume = 70;
        check(outcomes(step, s, true, micromoveOf) == span(45, 55), "m: a window around it");
        check(outcomes(step, s, true, volumeOf) == span(65, 75), "V: a window around it");
        step.micromove = 1;
        step.volume = 98;
        check(outcomes(step, s, true, micromoveOf) == span(0, 10), "m slides at the low limit");
        check(outcomes(step, s, true, volumeOf) == span(90, 100), "V slides at the high limit");
        step.volume = 2;
        check(outcomes(step, s, true, volumeOf) == span(1, 11), "V stays above 0");
        step.micromove = 0;
        step.volume = 0;
        check(outcomes(step, s, true, volumeOf) == span(0, 0), "V 0 stays 0");
        check(outcomes(step, s, true, micromoveOf) == span(0, 10), "m 0 is a value, in a window");
    }

    // separate ranges
    {
        Settings t = s;
        t.timingRange = 20 - 1;
        t.volumeRange = 4 - 1;
        Step step = note;
        check(outcomes(step, t, true, micromoveOf) == span(0, 20), "timing range");
        check(outcomes(step, t, true, volumeOf) == span(48, 52), "volume range");
        t.timingRange = 100 - 1;
        t.volumeRange = 100 - 1;
        step.micromove = 50;
        step.volume = 50;
        check(outcomes(step, t, true, micromoveOf) == span(0, 100), "timing range 100 is everything");
        check(outcomes(step, t, true, volumeOf) == span(1, 100), "volume range 100 is 1..100");
    }

    // Volume/velocity off
    {
        Settings t = s;
        t.volumeOn = 0;
        Step step = note;
        check(outcomes(step, t, true, volumeOf) == span(-1, -1), "Volume off: empty V stays empty");
        step.volume = 70;
        check(outcomes(step, t, true, volumeOf) == span(70, 70), "Volume off: V is not changed");
        check(outcomes(step, t, true, micromoveOf) == span(0, 10), "Volume off: m still changes");
    }

    // an FX in a slot wins over the column when the step plays, so it's the one to change
    {
        Step step = note;
        step.fx[0].type = kTypes.micromove;
        step.fx[0].value = 50;
        step.micromove = 33;
        check(outcomes(step, s, true, fx0Value) == span(45, 55), "Micro-move FX: its value changes");
        check(outcomes(step, s, true, micromoveOf) == span(33, 33), "Micro-move FX: m stays");
        check(outcomes(step, s, true, volumeOf) == span(45, 55), "Micro-move FX: V still gets a value");

        step = note;
        step.fx[0].type = kTypes.volume;
        step.fx[0].value = 70;
        check(outcomes(step, s, true, fx0Value) == span(65, 75), "Volume FX: its value changes");
        check(outcomes(step, s, true, volumeOf) == span(-1, -1), "Volume FX: V stays empty");
        check(outcomes(step, s, true, micromoveOf) == span(0, 10), "Volume FX: m still gets a value");
        step.fx[0].value = 0;
        check(outcomes(step, s, true, fx0Value) == span(0, 0), "Volume FX 0 stays 0");
        step.fx[0].value = 70;
        Settings t = s;
        t.volumeOn = 0;
        check(outcomes(step, t, true, fx0Value) == span(70, 70), "Volume off: Volume FX stays");
    }

    // the columns are int8_t: every result fits
    {
        Settings t = s;
        t.timingRange = 100 - 1;
        t.volumeRange = 100 - 1;
        for (int v = -1; v <= 100; v++)
        {
            Step step = note;
            step.micromove = static_cast<int8_t>(v);
            step.volume = static_cast<int8_t>(v);
            for (int i = 0; i < 50; i++)
            {
                humanizeStep(step, t, false, kTypes, testRandom);
                check(step.micromove >= 0 && step.micromove <= 100, "m stays 0..100");
                check(step.volume >= (v == 0 ? 0 : 1) && step.volume <= 100, "V stays in range");
            }
        }
    }
}

void testRecord()
{
    uint8_t buffer[256];
    Store store(buffer, sizeof(buffer), "TEST");
    store.clear();

    settings.timingRange = 77;
    load(store);
    check(settings.timingRange == 9 && settings.volumeOn == 1 && settings.volumeRange == 9 &&
            settings.midiVelocity == 49 && settings.sampleVolume == 49,
          "no record: the defaults, shown as 10, on, 10, 50, 50");

    check(!storeIfEdited(store), "unchanged defaults write nothing");
    check(store.find(kRecord, nullptr, nullptr) == nullptr, "no record for the defaults");

    settings.timingRange = 33;
    settings.volumeOn = 0;
    settings.volumeRange = 44;
    settings.midiVelocity = 55;
    settings.sampleVolume = 66;
    check(storeIfEdited(store), "an edit is stored");
    check(!storeIfEdited(store), "a second call has nothing to store");

    settings = Settings{};
    load(store);
    check(settings.timingRange == 33 && settings.volumeOn == 0 && settings.volumeRange == 44 &&
            settings.midiVelocity == 55 && settings.sampleVolume == 66,
          "the record comes back");

    settings = Settings{};
    check(storeIfEdited(store), "back to the defaults is an edit too");
    settings.timingRange = 12;
    load(store);
    check(settings.timingRange == 9, "defaults stored and loaded");

    for (const auto& bad : std::vector<std::vector<uint8_t>>{ { 100, 1, 10, 50, 50 },
                                                              { 10, 2, 10, 50, 50 },
                                                              { 10, 1, 100, 50, 50 },
                                                              { 10, 1, 10, 100, 50 },
                                                              { 10, 1, 10, 50, 100 },
                                                              { 10, 1, 10, 50 } })
    {
        store.put(kRecord, kRecordVersion, bad.data(), static_cast<uint16_t>(bad.size()));
        settings.timingRange = 99;
        load(store);
        check(settings.timingRange == 9 && settings.sampleVolume == 49, "a damaged record: the defaults");
    }
}

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
        const size_t n = it->second.size() < capacity ? it->second.size() : capacity;
        memcpy(buffer, it->second.data(), n);
        return it->second.size();
    }
    bool write(const char*, const uint8_t*, size_t) override { return true; }
    void remove(const char*) override {}
    void makeDir(const char*) override {}

    std::map<std::string, std::vector<uint8_t>> files;
};

std::vector<uint8_t> sideFile(const char* magic, const std::vector<uint8_t>& data)
{
    uint32_t h = 2166136261u;
    for (uint8_t b : data)
    {
        h = (h ^ b) * 16777619u;
    }
    std::vector<uint8_t> file(magic, magic + 4);
    file.insert(file.end(), data.begin(), data.end());
    for (int i = 0; i < 4; i++)
    {
        file.push_back(static_cast<uint8_t>(h >> (8 * i)));
    }
    return file;
}

void testImport()
{
    constexpr char kPath[] = "/Projects/Song/humanize.bin";
    uint8_t buffer[256];
    Store store(buffer, sizeof(buffer), "TEST");
    FakeIo io;

    store.clear();
    check(!importSideFile(io, "/Projects/Song", store), "no side file: nothing to import");
    check(store.empty(), "no side file: no record");

    // timing index 19, volume off, MIDI index 79, sample index 29, volume range index 3
    io.files[kPath] = sideFile("HHZ2", { 19, 0, 79, 29, 3, 0, 0, 0 });
    check(importSideFile(io, "/Projects/Song", store), "HHZ2 is imported");
    load(store);
    check(settings.timingRange == 19 && settings.volumeOn == 0 && settings.midiVelocity == 79 &&
            settings.sampleVolume == 29 && settings.volumeRange == 3,
          "HHZ2's values");

    store.clear();
    io.files[kPath] = sideFile("HHZ1", { 19, 1, 79, 29 });
    check(importSideFile(io, "/Projects/Song", store), "HHZ1 is imported");
    load(store);
    check(settings.timingRange == 19 && settings.volumeOn == 1 && settings.volumeRange == 19,
          "HHZ1's one range goes to both");

    store.clear();
    std::vector<uint8_t> broken = sideFile("HHZ2", { 19, 0, 79, 29, 3, 0, 0, 0 });
    broken[5] ^= 1;
    io.files[kPath] = broken;
    check(!importSideFile(io, "/Projects/Song", store), "a bad checksum is refused");
    io.files[kPath] = sideFile("HHZ2", { 100, 0, 79, 29, 3, 0, 0, 0 });
    check(!importSideFile(io, "/Projects/Song", store), "a value out of range is refused");
    io.files[kPath] = sideFile("HHZ2", { 19, 2, 79, 29, 3, 0, 0, 0 });
    check(!importSideFile(io, "/Projects/Song", store), "a bad switch is refused");
    io.files[kPath] = sideFile("HHZ3", { 19, 0, 79, 29, 3, 0, 0, 0 });
    check(!importSideFile(io, "/Projects/Song", store), "another magic is refused");
    io.files[kPath] = std::vector<uint8_t>(40, 0);
    check(!importSideFile(io, "/Projects/Song", store), "another size is refused");
    check(store.empty(), "a refused file leaves no record");

    // through the project loader: the mod starts On and the record is in the project
    store.clear();
    io.files[kPath] = sideFile("HHZ2", { 19, 0, 79, 29, 3, 0, 0, 0 });
    uint8_t defaults[kModCount] = {};
    uint8_t set[kModCount] = {};
    project::loadFromWorkspace(io, store, "/Projects/Song", defaults, set, kModCount);
    check(set[static_cast<size_t>(Mod::humanize)] == 1, "a project with humanize.bin opens with Humanize on");
    for (size_t i = 0; i < kModCount; i++)
    {
        if (i != static_cast<size_t>(Mod::humanize))
        {
            check(set[i] == 0, "the other mods stay off");
        }
    }
    load(store);
    check(settings.timingRange == 19, "its settings are in the record");

    io.files.erase(kPath);
    store.clear();
    project::loadFromWorkspace(io, store, "/Projects/Song", defaults, set, kModCount);
    check(set[static_cast<size_t>(Mod::humanize)] == 0, "a project without it opens with Humanize off");

    // a new project ignores the importers
    io.files[kPath] = sideFile("HHZ2", { 19, 0, 79, 29, 3, 0, 0, 0 });
    store.clear();
    project::loadFromWorkspace(io, store, nullptr, defaults, set, kModCount);
    check(set[static_cast<size_t>(Mod::humanize)] == 0, "a new project takes the defaults");
    settings = Settings{};
}

void press()
{
}

void testButtons()
{
    const PatternButton list[] = {
        { Mod::humanize, "H", press },
        { Mod::copypattern, "C", press },
        { Mod::kCount, nullptr, nullptr },
    };
    const size_t count = sizeof(list) / sizeof(list[0]);

    switches.fill(0);
    check(nthPatternButton(list, count, 0) == nullptr, "no mods on: no buttons");

    switches[static_cast<size_t>(Mod::humanize)] = 1;
    check(nthPatternButton(list, count, 0) == &list[0], "the first free button");
    check(nthPatternButton(list, count, 1) == nullptr, "and nothing after it");

    switches[static_cast<size_t>(Mod::copypattern)] = 1;
    check(nthPatternButton(list, count, 1) == &list[1], "a second mod takes the next one");

    switches[static_cast<size_t>(Mod::humanize)] = 0;
    check(nthPatternButton(list, count, 0) == &list[1], "with the first off, the second moves up");
    check(nthPatternButton(list, count, 1) == nullptr, "and nothing after it");

    switches.fill(0);
}

} // namespace

int main()
{
    srand(1);
    testSteps();
    testRecord();
    testImport();
    testButtons();
    printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
