// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the masterfx mod's settings: the project record, the 1.2.0 side file
// and its import. The effects and the screens are tested in tools/emu/test_masterfx.py.
// Build and run with `make -C core/mods/tests`.

#include "masterfx.h"
#include "modsProject.h"

#include <algorithm>
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

void testNames()
{
    check(strcmp(kReverbNames[0], "Stock") == 0 && strcmp(kDelayNames[0], "Stock") == 0,
          "the first choice is Stock");
    check(strcmp(kReverbNames[4], "Shimmer") == 0 && strcmp(kReverbNames[6], "Reverse") == 0,
          "reverbs in 1.2.0's order");
    check(strcmp(kDelayNames[1], "Tape") == 0 && strcmp(kDelayNames[2], "BBD") == 0,
          "delays in 1.2.0's order");
}

void testRecord()
{
    uint8_t buffer[256];
    Store store(buffer, sizeof(buffer), "TEST");
    store.clear();

    settings.reverb = 3;
    settings.delay = 2;
    load(store);
    check(settings.reverb == kStock && settings.delay == kStock, "no record: Stock and Stock");
    check(!storeIfEdited(store), "unchanged: nothing stored");
    check(store.empty(), "and no record written");

    settings.reverb = 4;
    settings.delay = 2;
    check(storeIfEdited(store), "edited: stored");
    check(!storeIfEdited(store), "stored once");
    settings = Settings{};
    load(store);
    check(settings.reverb == 4 && settings.delay == 2, "the record comes back");

    settings.reverb = 0;
    check(storeIfEdited(store), "going back to Stock is an edit");
    load(store);
    check(settings.reverb == 0 && settings.delay == 2, "and the delay stays");

    const auto loaded = [&](std::vector<uint8_t> record) {
        store.clear();
        store.put(kRecord, kRecordVersion, record.data(), static_cast<uint16_t>(record.size()));
        settings.reverb = 1;
        settings.delay = 1;
        load(store);
    };
    loaded({ kReverbCount, 2 });
    check(settings.reverb == kStock && settings.delay == 2, "a reverb out of range: Stock");
    loaded({ 3, kDelayCount });
    check(settings.reverb == 3 && settings.delay == kStock, "a delay out of range: Stock");
    loaded({ 3 });
    check(settings.reverb == kStock && settings.delay == kStock, "a short record: Stock and Stock");
    loaded({ 3, 1, 0 });
    check(settings.reverb == kStock && settings.delay == kStock, "a long record: Stock and Stock");

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
    check(settings.reverb == 4 && settings.delay == 2, "its choices");

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
    check(settings.reverb == 0 && settings.delay == 0, "with nothing chosen");

    store.clear();
    io.files[kPath] = sideFile(2, 3);
    check(importSideFile(io, "/Projects/Song", store), "old Gated delay with a reverb: On");
    load(store);
    check(settings.reverb == 2 && settings.delay == kStock, "keeps the reverb, plays Stock delay");

    store.clear();
    io.files[kPath] = sideFile(0, 3);
    check(!importSideFile(io, "/Projects/Song", store), "old Gated delay alone: stays Off");
    load(store);
    check(settings.reverb == 0 && settings.delay == kStock, "and is Stock");

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
        check(settings.reverb == 4 && settings.delay == 2, "with its choice");
        check(io.files.count(project::kWorkspaceFile) == 1, "written to the Workspace");

        // the next load finds Workspace/mods.bin and doesn't import again
        io.files[kPath] = sideFile(1, 1);
        std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
        settings = Settings{};
        project::loadFromWorkspace(io, store, "/Projects/Song", defaults, set, kModCount);
        load(store);
        check(set[index] == 1 && settings.reverb == 4 && settings.delay == 2,
              "with mods.bin there, no import");
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
        check(settings.reverb == 4 && settings.delay == 2, "Off keeps the record");
    }

    settings = Settings{};
}

} // namespace

int main()
{
    testNames();
    testRecord();
    testImportFile();
    testImportThroughProject();
    printf("masterfx: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
