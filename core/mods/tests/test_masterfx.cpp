// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the masterfx mod's settings: the project record (version 2: a plugin file and
// two own knobs per slot; version 1's indices converted), the 1.2.0 side file and its import, and
// the lists with no card. The runtime is tested in test_masterfx_plugins.cpp, the effects and the
// screens in tools/emu/test_masterfx.py.
//
// masterfx-2.0 changed the record on purpose (indices became file names), so the checks that
// read settings.reverb and settings.delay now read the slots' files.
// Build and run with `make -C core/mods/tests`.

#include "masterfx.h"
#include "modsProject.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace mods;
using namespace mods::masterfx;

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

// As the 1.2.0 patch writes it.
std::vector<uint8_t> sideFile(uint8_t reverb, uint8_t delay)
{
    const std::vector<uint8_t> data = { reverb, delay, 0, 0 };
    uint32_t h = 2166136261u;
    for (uint8_t b : data)
    {
        h = (h ^ b) * 16777619u;
    }
    std::vector<uint8_t> file = { 'H', 'M', 'F', '1' };
    file.insert(file.end(), data.begin(), data.end());
    for (int i = 0; i < 4; i++)
    {
        file.push_back(static_cast<uint8_t>(h >> (8 * i)));
    }
    return file;
}

const char kPath[] = "/Projects/Song/masterfx.bin";

// The slots' files as version 1's indices: -1 if a file isn't one of version 1's
int reverbIndex()
{
    for (int i = 0; i < kV1ReverbCount; i++)
    {
        if (strcmp(settings.slot[kReverb].file, kV1Reverbs[i]) == 0)
        {
            return i;
        }
    }
    return -1;
}

int delayIndex()
{
    for (int i = 0; i < kV1DelayCount; i++)
    {
        if (strcmp(settings.slot[kDelay].file, kV1Delays[i]) == 0)
        {
            return i;
        }
    }
    return -1;
}

bool is(int reverb, int delay)
{
    return reverbIndex() == reverb && delayIndex() == delay && settings.slot[kDynamics].file[0] == 0;
}

void setFile(uint8_t slot, const char* file)
{
    memset(settings.slot[slot].file, 0, kFileSize);
    strncpy(settings.slot[slot].file, file, kFileSize - 1);
}

void setV1(int reverb, int delay)
{
    setFile(kReverb, kV1Reverbs[reverb]);
    setFile(kDelay, kV1Delays[delay]);
}

bool knobsUnset(uint8_t slot)
{
    return std::isnan(settings.slot[slot].knob[0]) && std::isnan(settings.slot[slot].knob[1]);
}

void testNames()
{
    check(kV1Reverbs[0][0] == 0 && kV1Delays[0][0] == 0, "version 1's first choice is Stock");
    check(strcmp(kV1Reverbs[4], "Shimmer.ptmfx") == 0 && strcmp(kV1Reverbs[6], "Reverse.ptmfx") == 0,
          "reverbs in 1.2.0's order");
    check(strcmp(kV1Delays[1], "Tape.ptmfx") == 0 && strcmp(kV1Delays[2], "BBD.ptmfx") == 0,
          "delays in 1.2.0's order");
}

void testRecord()
{
    uint8_t buffer[512];
    Store store(buffer, sizeof(buffer), "TEST");
    store.clear();

    setV1(3, 2);
    load(store);
    check(is(kStock, kStock), "no record: Stock everywhere");
    check(knobsUnset(kReverb) && knobsUnset(kDelay) && knobsUnset(kDynamics), "with the knobs unset");
    check(!storeIfEdited(store), "unchanged: nothing stored");
    check(store.empty(), "and no record written");

    setV1(4, 2);
    check(storeIfEdited(store), "edited: stored");
    check(!storeIfEdited(store), "stored once");
    uint8_t version = 0;
    uint16_t length = 0;
    check(store.find(kRecord, &version, &length) != nullptr && version == 2 && length == 168,
          "as version 2, 168 bytes");
    settings = Settings{};
    load(store);
    check(is(4, 2), "the record comes back");
    check(knobsUnset(kReverb), "and the knobs stay unset");

    settings.slot[kDelay].knob[1] = 0.25f;
    check(storeIfEdited(store), "an own knob is an edit");
    settings = Settings{};
    load(store);
    check(settings.slot[kDelay].knob[1] == 0.25f && std::isnan(settings.slot[kDelay].knob[0]),
          "and comes back");

    setFile(kReverb, "");
    check(storeIfEdited(store), "going back to Stock is an edit");
    load(store);
    check(is(kStock, 2), "and the delay stays");

    setFile(kDynamics, "Glue.ptmfx");
    check(storeIfEdited(store), "a dynamics plugin is kept");
    settings = Settings{};
    load(store);
    check(strcmp(settings.slot[kDynamics].file, "Glue.ptmfx") == 0, "and comes back");
    setFile(kReverb, "My reverb with a long name.ptmfx");
    check(storeIfEdited(store), "any file name");
    load(store);
    check(strcmp(settings.slot[kReverb].file, "My reverb with a long name.ptmfx") == 0, "comes back");

    const auto loaded = [&](std::vector<uint8_t> record, uint8_t recordVersion) {
        store.clear();
        store.put(kRecord, recordVersion, record.data(), static_cast<uint16_t>(record.size()));
        setV1(1, 1);
        load(store);
    };
    // Version 1: two indices (masterfx-1.x)
    loaded({ 4, 2 }, 1);
    check(is(4, 2), "version 1: its indices as the pack's files");
    check(knobsUnset(kReverb) && knobsUnset(kDelay) && knobsUnset(kDynamics), "with the knobs unset");
    loaded({ kV1ReverbCount, 2 }, 1);
    check(is(kStock, 2), "version 1, a reverb out of range: Stock");
    loaded({ 3, kV1DelayCount }, 1);
    check(is(3, kStock), "version 1, a delay out of range: Stock");
    loaded({ 3 }, 1);
    check(is(kStock, kStock), "a short record: Stock everywhere");
    loaded({ 3, 1, 0 }, 1);
    check(is(kStock, kStock), "a long version 1 record: Stock everywhere");

    // Version 2, damaged
    std::vector<uint8_t> record(168, 0);
    memcpy(record.data(), "Plate.ptmfx", 11);
    loaded(record, 2);
    check(is(2, kStock), "version 2: its files");
    check(settings.slot[kReverb].knob[0] == 0.0f, "and its knobs as stored");
    std::fill(record.begin(), record.begin() + 48, uint8_t{ 'A' });
    memcpy(record.data() + 56, "Tape.ptmfx", 10);
    loaded(record, 2);
    check(is(kStock, 1) && knobsUnset(kReverb), "a file name with no end: that slot is Stock");
    record.push_back(0);
    loaded(record, 2);
    check(is(kStock, kStock), "a long record: Stock everywhere");
    record.resize(100);
    loaded(record, 2);
    check(is(kStock, kStock), "a short version 2 record: Stock everywhere");

    settings = Settings{};
}

// No card read (the host test's runtime has no catalog): Stock, then Missing for the project's
// file, which can't be chosen
void testChoices()
{
    uint8_t buffer[512];
    Store store(buffer, sizeof(buffer), "TEST");
    store.clear();
    load(store);
    check(choiceCount(kReverb) == 1 && strcmp(choiceNames(kReverb)[0], "Stock") == 0 && choice(kReverb) == 0,
          "no plugins: Stock alone");
    check(!missing(kReverb), "Stock isn't missing");

    setFile(kDelay, "Tape.ptmfx");
    refreshChoices();
    check(choiceCount(kDelay) == 2 && choice(kDelay) == 1 && strcmp(choiceName(kDelay), "Missing: Tape.ptmfx") == 0,
          "the project's file not on the card: Missing");
    check(missing(kDelay) && !missing(kReverb), "only that slot is missing");
    choose(kDelay, 1);
    check(strcmp(settings.slot[kDelay].file, "Tape.ptmfx") == 0, "choosing Missing keeps the file");
    choose(kDelay, 5);
    check(strcmp(settings.slot[kDelay].file, "Tape.ptmfx") == 0, "a choice out of range does nothing");
    settings.slot[kDelay].knob[0] = 3.0f;
    choose(kDelay, 0);
    check(settings.slot[kDelay].file[0] == 0 && knobsUnset(kDelay), "Stock: no file, knobs unset");
    check(choiceCount(kDelay) == 1 && !missing(kDelay), "and Missing is gone");

    settings = Settings{};
}

void testImportFile()
{
    FakeIo io;
    uint8_t buffer[256];
    Store store(buffer, sizeof(buffer), "TEST");
    store.clear();

    check(!importSideFile(io, "/Projects/Song", store), "no side file: nothing to import");
    check(store.empty(), "no side file: no record");

    io.files[kPath] = sideFile(4, 2);
    check(importSideFile(io, "/Projects/Song", store), "a choice: the project used the mod");
    load(store);
    check(is(4, 2), "its choices");
    uint8_t version = 0;
    uint16_t length = 0;
    check(store.find(kRecord, &version, &length) != nullptr && version == 2 && length == 168,
          "imported as version 2");

    store.clear();
    io.files[kPath] = sideFile(0, 1);
    check(importSideFile(io, "/Projects/Song", store), "only a delay chosen: On too");
    store.clear();
    io.files[kPath] = sideFile(6, 0);
    check(importSideFile(io, "/Projects/Song", store), "only a reverb chosen: On too");

    store.clear();
    io.files[kPath] = sideFile(0, 0);
    check(!importSideFile(io, "/Projects/Song", store), "Stock and Stock: the mod stays Off");
    load(store);
    check(is(kStock, kStock), "with nothing chosen");

    store.clear();
    io.files[kPath] = sideFile(2, 3);
    check(importSideFile(io, "/Projects/Song", store), "old Gated delay with a reverb: On");
    load(store);
    check(is(2, kStock), "keeps the reverb, plays Stock delay");

    store.clear();
    io.files[kPath] = sideFile(0, 3);
    check(!importSideFile(io, "/Projects/Song", store), "old Gated delay alone: stays Off");
    load(store);
    check(is(kStock, kStock), "and is Stock");

    const auto refused = [&](const std::vector<uint8_t>& file, const char* name) {
        store.clear();
        io.files[kPath] = file;
        check(!importSideFile(io, "/Projects/Song", store), name);
        check(store.empty(), std::string(name) + ": no record");
    };
    std::vector<uint8_t> broken = sideFile(4, 2);
    broken[8] ^= 1;
    refused(broken, "a bad checksum is refused");
    refused(sideFile(7, 0), "a reverb out of range is refused");
    refused(sideFile(0, 4), "a delay out of range is refused");
    broken = sideFile(4, 2);
    broken[0] = 'X';
    refused(broken, "another magic is refused");
    broken = sideFile(4, 2);
    broken.pop_back();
    refused(broken, "a short file is refused");
    broken = sideFile(4, 2);
    broken.push_back(0);
    refused(broken, "a long file is refused");

    settings = Settings{};
}

void testImportThroughProject()
{
    const size_t index = static_cast<size_t>(Mod::masterfx);
    uint8_t buffer[1024];
    Store store(buffer, sizeof(buffer), project::kMagic);
    uint8_t defaults[kModCount] = {};
    uint8_t set[kModCount] = {};

    {
        FakeIo io;
        io.files[kPath] = sideFile(4, 2);
        project::loadFromWorkspace(io, store, "/Projects/Song", defaults, set, kModCount);
        load(store);
        check(set[index] == 1, "a 1.2.0 project with a choice: the mod starts On");
        check(is(4, 2), "with its choice");
        check(io.files.count(project::kWorkspaceFile) == 1, "written to the Workspace");

        // the next load finds Workspace/mods.bin and doesn't import again
        io.files[kPath] = sideFile(1, 1);
        std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
        settings = Settings{};
        project::loadFromWorkspace(io, store, "/Projects/Song", defaults, set, kModCount);
        load(store);
        check(set[index] == 1 && is(4, 2), "with mods.bin there, no import");
    }
    {
        FakeIo io;
        io.files[kPath] = sideFile(0, 0);
        std::fill(std::begin(set), std::end(set), uint8_t{ 1 });
        project::loadFromWorkspace(io, store, "/Projects/Song", defaults, set, kModCount);
        load(store);
        check(set[index] == 0, "a 1.2.0 project with Stock and Stock: the mod stays Off");
    }
    {
        FakeIo io;
        io.files[kPath] = sideFile(4, 2);
        std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
        settings = Settings{};
        project::loadFromWorkspace(io, store, "/Projects/Song", defaults, set, kModCount);
        load(store);
        set[index] = 0;
        project::setSwitches(store, set, kModCount);
        settings = Settings{};
        load(store);
        check(is(4, 2), "Off keeps the record");
    }

    settings = Settings{};
}

} // namespace

int main()
{
    testNames();
    testRecord();
    testChoices();
    testImportFile();
    testImportThroughProject();
    printf("masterfx: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
