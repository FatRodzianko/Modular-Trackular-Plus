// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the resample mod's settings: the record, the 1.2.0 side file and its
// import, the record's trip through mods.bin, and the recorder's two buttons (which source
// shows which, a press with its coupling, the record a press stores, the labels' texts), and Trk
// Audio's rules (when the gates open, where a take goes), and what Shift+Rec and a count-in's
// Play arm. The other recorder hooks come with
// later steps (docs/mt/RESAMPLE_PORT_PLAN.md). Build and run
// with `make -C core/mods/tests`.

#include "modsProject.h"
#include "resample.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace mods;
using namespace mods::resample;

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

bool same(const Settings& a, bool trkAudio, bool trkRecord)
{
    return a.trkAudio == trkAudio && a.trkRecord == trkRecord;
}

void testBits()
{
    check(kRecord == 12 && static_cast<int>(Mod::resample) == 11, "mod 11, record 12");

    Settings s;
    check(fromBits(0, &s) && same(s, false, false), "0: both Off");
    check(fromBits(1, &s) && same(s, true, false), "1: Trk Audio");
    check(fromBits(3, &s) && same(s, true, true), "3: both On");
    check(fromBits(2, &s) && same(s, true, true), "2, Trk Record alone: both On");
    s = Settings{ true, false };
    check(!fromBits(4, &s) && same(s, true, false), "4 isn't a setting and changes nothing");
    check(!fromBits(0xFF, &s), "0xFF isn't a setting");

    check(toBits(Settings{ false, false }) == 0, "Off is 0");
    check(toBits(Settings{ true, false }) == 1, "Trk Audio is 1");
    check(toBits(Settings{ true, true }) == 3, "both are 3");
    check(toBits(Settings{ false, true }) == 0, "Trk Record without Trk Audio is stored Off");
}

void testRecord()
{
    uint8_t buffer[256];
    Store store(buffer, sizeof(buffer), "TEST");
    store.clear();

    settings = Settings{ true, true };
    load(store);
    check(same(settings, false, false), "no record: both Off");

    const auto loaded = [&](const void* data, uint16_t length) {
        store.put(kRecord, kRecordVersion, data, length);
        settings = Settings{ true, true };
        load(store);
        return settings;
    };
    uint8_t byte = 1;
    check(same(loaded(&byte, 1), true, false), "record 1: Trk Audio");
    byte = 3;
    check(same(loaded(&byte, 1), true, true), "record 3: both");
    byte = 2;
    check(same(loaded(&byte, 1), true, true), "record 2: both");
    byte = 0;
    check(same(loaded(&byte, 1), false, false), "record 0: both Off");
    byte = 4;
    check(same(loaded(&byte, 1), false, false), "a damaged record: both Off");
    const uint8_t longer[2] = { 1, 0 };
    check(same(loaded(longer, 2), false, false), "a record of another size: both Off");
    check(same(loaded(nullptr, 0), false, false), "an empty record: both Off");
    settings = Settings{};
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

// The 1.2.0 layout: magic, the settings byte, three zero bytes, FNV-1a over bytes 4 to 7.
std::vector<uint8_t> sideFile(uint8_t bits)
{
    std::vector<uint8_t> file = { 'R', 'S', 'M', '1', bits, 0, 0, 0 };
    uint32_t h = 2166136261u;
    for (size_t i = 4; i < 8; i++)
    {
        h = (h ^ file[i]) * 16777619u;
    }
    for (int i = 0; i < 4; i++)
    {
        file.push_back(static_cast<uint8_t>(h >> (8 * i)));
    }
    return file;
}

// A resample.bin a 1.2.0 unit with the MT patch saved, both buttons On.
const std::vector<uint8_t> kSavedOn120 = { 0x52, 0x53, 0x4D, 0x31, 0x03, 0x00,
                                           0x00, 0x00, 0x26, 0x34, 0xC2, 0x9B };

const char kFolder[] = "/Projects/Song";
const char kPath[] = "/Projects/Song/resample.bin";

void testImportFile()
{
    FakeIo io;
    uint8_t buffer[256];
    Store store(buffer, sizeof(buffer), "TEST");
    store.clear();

    check(sideFile(3) == kSavedOn120, "the test's layout is the one 1.2.0 wrote");

    check(!importSideFile(io, kFolder, store), "no side file: nothing to import");
    check(store.empty(), "no side file: no record");

    const auto imported = [&](const std::vector<uint8_t>& file, bool used, bool trkAudio, bool trkRecord,
                              const char* name) {
        store.clear();
        io.files[kPath] = file;
        check(importSideFile(io, kFolder, store) == used, name);
        uint16_t length = 0;
        uint8_t version = 0;
        const uint8_t* data = store.find(kRecord, &version, &length);
        check(data != nullptr && length == 1 && version == kRecordVersion, std::string(name) + ": a record");
        load(store);
        check(same(settings, trkAudio, trkRecord), std::string(name) + ": its settings");
    };
    imported(kSavedOn120, true, true, true, "a file from a 1.2.0 unit: used");
    imported(sideFile(1), true, true, false, "Trk Audio alone: used");
    imported(sideFile(2), true, true, true, "Trk Record alone: used, as both");
    imported(sideFile(0), false, false, false, "both Off: the mod stays Off");

    store.clear();
    io.files[kPath] = sideFile(2);
    importSideFile(io, kFolder, store);
    const uint8_t* data = store.find(kRecord, nullptr, nullptr);
    check(data != nullptr && data[0] == 3, "Trk Record alone is stored as 3");

    const auto refused = [&](const std::vector<uint8_t>& file, const char* name) {
        store.clear();
        io.files[kPath] = file;
        check(!importSideFile(io, kFolder, store), name);
        check(store.empty(), std::string(name) + ": no record");
    };
    std::vector<uint8_t> broken = sideFile(3);
    broken[8] ^= 1;
    refused(broken, "a bad checksum is refused");
    broken = sideFile(3);
    broken[6] = 1;
    refused(broken, "a changed zero byte fails the checksum");
    refused(sideFile(4), "a settings byte over 3 is refused");
    refused(sideFile(0xFF), "0xFF is refused");
    broken = sideFile(3);
    broken[3] = '2';
    refused(broken, "another magic is refused");
    broken = sideFile(3);
    broken.pop_back();
    refused(broken, "a short file is refused");
    broken = sideFile(3);
    broken.push_back(0);
    refused(broken, "a long file is refused");
    refused({}, "an empty file is refused");

    std::string longFolder(project::kPathSize, 'a');
    store.clear();
    check(!importSideFile(io, longFolder.c_str(), store) && store.empty(), "a path too long: nothing");

    settings = Settings{};
}

constexpr size_t kIndex = static_cast<size_t>(Mod::resample);

void testImportThroughProject()
{
    uint8_t buffer[1024];
    Store store(buffer, sizeof(buffer), project::kMagic);
    uint8_t defaults[kModCount] = {};
    uint8_t set[kModCount] = {};

    {
        FakeIo io;
        io.files[kPath] = kSavedOn120;
        project::loadFromWorkspace(io, store, kFolder, defaults, set, kModCount);
        load(store);
        check(set[kIndex] == 1, "a 1.2.0 project with it On: the mod starts On");
        check(same(settings, true, true), "with both buttons");
        check(io.files.count(project::kWorkspaceFile) == 1, "written to the Workspace");

        // the next load finds Workspace/mods.bin and doesn't import again
        io.files[kPath] = sideFile(1);
        std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
        settings = Settings{};
        project::loadFromWorkspace(io, store, kFolder, defaults, set, kModCount);
        load(store);
        check(set[kIndex] == 1 && same(settings, true, true), "with mods.bin there, no import");
    }
    {
        FakeIo io;
        io.files[kPath] = sideFile(0);
        std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
        project::loadFromWorkspace(io, store, kFolder, defaults, set, kModCount);
        load(store);
        check(set[kIndex] == 0 && same(settings, false, false), "a 1.2.0 project with both Off: the mod Off");
    }
    {
        // a mods.bin saved by a build without Resample: the importer still gets its turn
        FakeIo io;
        store.clear();
        const uint8_t imports[4] = { 0, 0, 0, 0 };
        store.put(project::kImportsRecord, 1, imports, sizeof(imports));
        io.files[project::kWorkspaceFile] = std::vector<uint8_t>(buffer, buffer + store.finalize());
        io.files[kPath] = sideFile(1);
        std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
        project::loadFromWorkspace(io, store, kFolder, defaults, set, kModCount);
        load(store);
        check(set[kIndex] == 1 && same(settings, true, false), "an older mods.bin: imported once");
    }
    {
        FakeIo io;
        io.files[kPath] = kSavedOn120;
        std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
        project::loadFromWorkspace(io, store, nullptr, defaults, set, kModCount);
        load(store);
        check(set[kIndex] == 0 && same(settings, false, false), "New Project: no import, both Off");
    }
    settings = Settings{};
}

void testRecordRoundTrip()
{
    uint8_t buffer[1024];
    Store store(buffer, sizeof(buffer), project::kMagic);
    uint8_t defaults[kModCount] = {};
    uint8_t set[kModCount] = {};
    FakeIo io;

    project::loadFromWorkspace(io, store, kFolder, defaults, set, kModCount);
    const uint8_t bits = 1;
    store.put(kRecord, kRecordVersion, &bits, 1);
    check(project::saveToProject(io, store, kFolder), "Save writes the project's mods.bin");

    // Open in another session: copy in, load
    uint8_t other[1024];
    Store opened(other, sizeof(other), project::kMagic);
    io.files.erase(project::kWorkspaceFile);
    project::copyIn(io, opened, kFolder);
    std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
    project::loadFromWorkspace(io, opened, kFolder, defaults, set, kModCount);
    uint8_t version = 0;
    uint16_t length = 0;
    const uint8_t* data = opened.find(kRecord, &version, &length);
    check(data != nullptr && version == kRecordVersion && length == 1 && data[0] == 1,
          "record 12 survives Save and Open");
    load(opened);
    check(same(settings, true, false), "and loads as Trk Audio");
    settings = Settings{};
}

} // namespace

void testButtonsFor()
{
    // buttonsFor(lineIn, stereo, mic), as cSampleRecorder::resampleButtons asks it
    check(buttonsFor(true, true, false) == Buttons::both, "Line In: both buttons");
    check(buttonsFor(true, false, false) == Buttons::trkAudio, "Line In (L), (R), (M): Trk Audio");
    check(buttonsFor(false, false, true) == Buttons::trkAudio, "a mic: Trk Audio");
    check(buttonsFor(false, true, false) == Buttons::none, "USB: none");
    check(buttonsFor(false, false, false) == Buttons::none, "Radio, USB (M): none");
}

void testPress()
{
    Settings s;
    check(press(kTrkAudioSlot, Buttons::both, &s) && same(s, true, false), "Trk Audio On");
    check(press(kTrkRecordSlot, Buttons::both, &s) && same(s, true, true), "Trk Record On");
    check(press(kTrkRecordSlot, Buttons::both, &s) && same(s, true, false),
          "Trk Record Off leaves Trk Audio On");
    check(press(kTrkRecordSlot, Buttons::both, &s) && press(kTrkAudioSlot, Buttons::both, &s) &&
            same(s, false, false),
          "Trk Audio Off turns Trk Record Off");

    s = Settings{};
    check(press(kTrkRecordSlot, Buttons::both, &s) && same(s, true, true),
          "Trk Record On from Off turns Trk Audio On");

    s = Settings{ true, true };
    check(!press(kTrkRecordSlot, Buttons::trkAudio, &s) && same(s, true, true),
          "no Trk Record button: the key does nothing");
    check(press(kTrkAudioSlot, Buttons::trkAudio, &s) && same(s, false, false),
          "Trk Audio Off on a mic turns the hidden Trk Record Off too");
    check(!press(kTrkAudioSlot, Buttons::none, &s) && !press(kTrkRecordSlot, Buttons::none, &s) &&
            same(s, false, false),
          "no buttons: neither key does anything");
    check(!press(0, Buttons::both, &s) && !press(3, Buttons::both, &s) && same(s, false, false),
          "other keys aren't Resample's");
}

void testStore()
{
    uint8_t buffer[256];
    Store records(buffer, sizeof(buffer), "TEST");
    records.clear();

    check(store(Settings{ true, false }, records), "a press with no record yet stores one");
    uint16_t length = 0;
    const uint8_t* data = records.find(kRecord, nullptr, &length);
    check(data != nullptr && length == 1 && data[0] == 1, "record 12 holds Trk Audio");
    check(!store(Settings{ true, false }, records), "the same settings: no change");
    check(store(Settings{ true, true }, records), "both On: a change");
    data = records.find(kRecord, nullptr, &length);
    check(data != nullptr && data[0] == 3, "record 12 holds both");
    check(store(Settings{}, records), "both Off: a change");
    data = records.find(kRecord, nullptr, &length);
    check(data != nullptr && length == 1 && data[0] == 0, "record 12 holds Off");

    settings = Settings{ true, true };
    load(records);
    check(same(settings, false, false), "and loads back as Off");
}

void testLabels()
{
    check(std::string(title(kTrkAudioSlot, Buttons::both)) == "Trk Audio", "slot 1: Trk Audio");
    check(std::string(title(kTrkRecordSlot, Buttons::both)) == "Trk Record", "slot 2: Trk Record");
    check(std::string(title(kTrkAudioSlot, Buttons::trkAudio)) == "Trk Audio",
          "a mic: slot 1 Trk Audio");
    check(title(kTrkRecordSlot, Buttons::trkAudio) == nullptr, "a mic: slot 2 empty");
    check(title(kTrkAudioSlot, Buttons::none) == nullptr &&
            title(kTrkRecordSlot, Buttons::none) == nullptr,
          "USB: both empty");

    check(std::string(value(kTrkAudioSlot, Settings{})) == "Off" &&
            std::string(value(kTrkRecordSlot, Settings{})) == "Off",
          "both Off");
    check(std::string(value(kTrkAudioSlot, Settings{ true, false })) == "On" &&
            std::string(value(kTrkRecordSlot, Settings{ true, false })) == "Off",
          "Trk Audio On, Trk Record Off");
    check(std::string(value(kTrkAudioSlot, Settings{ true, true })) == "On" &&
            std::string(value(kTrkRecordSlot, Settings{ true, true })) == "On",
          "both On");
}

void testVisit()
{
    visitEnded();
    check(!inVisit(), "no visit");
    visitStarted(false);
    check(!inVisit(), "a visit with the mod Off");
    visitStarted(true);
    check(inVisit(), "a visit with the mod On");
    visitEnded();
    check(!inVisit(), "the visit ended");
    check(!engineRuns(), "outside a visit the engine's update keeps CE's rule");
}

// The gates' rule (section 4.3): the case that opens them, then each thing that keeps them shut.
void testGates()
{
    const Settings on{ true, false };
    Visit v;
    v.entered = true;
    RecorderView setup;
    setup.onSetup = true;
    setup.buttonShown = true;

    check(gatesWanted(true, on, v, setup), "the setup, Trk Audio on: open");
    check(gatesWanted(true, Settings{ true, true }, v, setup), "Trk Record on too: open");
    check(!gatesWanted(false, on, v, setup), "the visit Off: shut");
    check(!gatesWanted(true, Settings{}, v, setup), "Trk Audio off: shut");

    RecorderView noButton = setup;
    noButton.buttonShown = false;
    check(!gatesWanted(true, on, v, noButton), "Radio or USB: shut");

    Visit notEntered;
    check(!gatesWanted(true, on, notEntered, setup), "CE's entry not run (stop-pattern question): shut");

    Visit overwritten = v;
    overwritten.samplesOverwritten = true;
    check(!gatesWanted(true, on, overwritten, setup), "a take of this visit went over the samples: shut");

    RecorderView busy = setup;
    busy.samplesBusy = true;
    check(!gatesWanted(true, on, v, busy), "CE saves a take or reloads the samples: shut");

    RecorderView crop = setup;
    crop.onSetup = false;
    check(!gatesWanted(true, on, v, crop), "the crop or save screen: shut");

    RecorderView take = crop;
    take.takeRunning = true;
    Visit moved = v;
    moved.takeMoved = true;
    check(gatesWanted(true, on, moved, take), "a take after the loaded samples: open");
    check(!gatesWanted(true, on, v, take), "a take in CE's buffer: shut");
    check(!gatesWanted(true, Settings{}, moved, take), "a moved take with Trk Audio off: shut");

    // a count-in is the setup with no take yet
    check(gatesWanted(true, on, v, setup), "counting in: open");

    // every combination: open only with all six conditions and the screen or a moved take
    int open = 0;
    for (int bits = 0; bits < 256; bits++)
    {
        Visit cv;
        cv.entered = bits & 1;
        cv.takeMoved = bits & 2;
        cv.samplesOverwritten = bits & 4;
        RecorderView cview;
        cview.onSetup = bits & 8;
        cview.takeRunning = bits & 16;
        cview.buttonShown = bits & 32;
        cview.samplesBusy = bits & 64;
        const bool visitOn = bits & 128;
        open += gatesWanted(visitOn, on, cv, cview) ? 1 : 0;
    }
    check(open == 4, "open in 4 of the 256 combinations: " + std::to_string(open));
}

// Where a take goes (section 4.4).
void testPlacement()
{
    check(framesInRam(false, 0, 1000) == 1000, "a sample all in RAM: its length");
    check(framesInRam(false, 300, 1000) == 300, "ramFrames set: ramFrames, as CE's rule");
    check(framesInRam(true, 32768, 900000) == 32768, "a streamed sample: its RAM prefix");
    check(framesInRam(true, 0, 900000) == 0, "a streamed sample with no prefix: nothing");
    check(sampleEnd(0x1000, 100, false) == 0x1000 + 200, "mono: 2 bytes a frame");
    check(sampleEnd(0x1000, 100, true) == 0x1000 + 400, "stereo: 4 bytes a frame");

    constexpr uintptr_t bank = 0x80000000u;
    constexpr uintptr_t bankSize = 60u * 1024 * 1024;
    constexpr uintptr_t end = bank + bankSize;

    Placement p = place(bank, end, bank, 2);
    check(p.address == bank && p.size == bankSize, "nothing loaded, stereo: CE's buffer exactly");
    p = place(bank, end, bank, 1);
    check(p.address == bank && p.size == bankSize, "nothing loaded, mono: CE's buffer exactly");
    p = place(bank, end, bank - 64, 1);
    check(p.address == bank, "a loaded end below the bank: the bank's start");

    p = place(bank, end, bank + 1001, 2);
    check(p.address == bank + 1024, "the start rounded up to 32 bytes");
    check(p.size % (2 * kTakeBlockBytes) == 0 && p.address + p.size <= end,
          "stereo: whole blocks per channel, inside the bank");
    check(end - (p.address + p.size) < 2 * kTakeBlockBytes, "stereo: under a block per channel left over");

    p = place(bank, end, bank + 4096, 1);
    check(p.address == bank + 4096 && p.size == bankSize - 4096, "mono, aligned: all the rest");

    // the 10 s limit, at whole blocks per channel: 882,000 bytes round up to 3,446 blocks
    constexpr uint32_t tenSeconds = 3446 * kTakeBlockBytes;
    p = place(bank, end, end - 2 * tenSeconds, 2);
    check(p.size == 2 * tenSeconds, "stereo, just 10 s of room: placed");
    p = place(bank, end, end - 2 * tenSeconds + 32, 2);
    check(p.size == 0, "stereo, a little under 10 s: CE's buffer");
    p = place(bank, end, end - tenSeconds, 1);
    check(p.size == tenSeconds, "mono, just 10 s of room: placed");
    p = place(bank, end, end - tenSeconds + 32, 1);
    check(p.size == 0, "mono, a little under 10 s: CE's buffer");
    p = place(bank, end, end - tenSeconds, 2);
    check(p.size == 0, "stereo with mono's 10 s: CE's buffer");

    check(place(bank, end, end, 2).size == 0, "a full bank: CE's buffer");
    check(place(bank, end, end + 100, 1).size == 0, "past the bank: CE's buffer");
    check(place(bank, end, bank, 0).size == 0, "no channels: nothing");
}

void testArm()
{
    check(armOnRec(Arm::none, true, true, true) == Arm::pattern, "Shift+Rec on the setup, open: the pattern armed");
    check(armOnRec(Arm::none, false, true, true) == Arm::none, "a plain Rec arms nothing");
    check(armOnRec(Arm::pattern, false, true, true) == Arm::pattern, "a plain Rec in a count-in never disarms");
    check(armOnRec(Arm::song, true, true, true) == Arm::song, "Shift+Rec after Shift+Play keeps the song");
    check(armOnRec(Arm::none, true, false, true) == Arm::none, "Shift+Rec with the gates shut: CE's Rec");
    check(armOnRec(Arm::none, true, true, false) == Arm::none, "Shift+Rec off the setup (a take, the crop screen): nothing");

    check(playArms(true, true, false), "Play in a count-in, open, nothing playing: arms");
    check(!playArms(true, false, false), "Play on the setup with no count-in: CE's Play");
    check(!playArms(false, true, false), "Play in a count-in with the gates shut: CE's Play");
    check(!playArms(true, true, true), "Play in a count-in over a playing pattern: CE's stop");
}

int main()
{
    testBits();
    testRecord();
    testImportFile();
    testImportThroughProject();
    testRecordRoundTrip();
    testButtonsFor();
    testPress();
    testStore();
    testLabels();
    testVisit();
    testGates();
    testPlacement();
    testArm();
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
