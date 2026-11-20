// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the resample mod's settings: the record, the 1.2.0 side file and its
// import, and the record's trip through mods.bin. The recorder hooks come with later steps
// (docs/mt/RESAMPLE_PORT_PLAN.md). Build and run with `make -C core/mods/tests`.

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

int main()
{
    testBits();
    testRecord();
    testImportFile();
    testImportThroughProject();
    testRecordRoundTrip();
    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
