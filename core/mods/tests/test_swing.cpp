// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the swing mod: what each hook does to CE's swing state, the project
// record, the 1.2.0 side file and its import. CE's own sequencer is tested in
// tools/emu/test_swing.py. Build and run with `make -C core/mods/tests`.

#include "modsProject.h"
#include "swing.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace mods;
using namespace mods::swing;

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

// CE's Sequencer::strPlayer::strSwing (mtSequencer.h)
struct CeSwing
{
    bool isActive = 0;
    bool stepToogle = 0;
    float value = 50.0f;

    float getTempoCorrection()
    {
        return stepToogle ? (100.0f - value) + 50.0f : value + 50.0f;
    }
    void setSwing(int16_t val)
    {
        isActive = 1;
        value = static_cast<float>(std::clamp<int16_t>(val, 25, 75));
    }
};

void testHooks()
{
    settings.amount = 66 - kMinAmount;
    check(amountOf(settings) == 66, "stored 41 is the amount 66");

    CeSwing s;
    s.stepToogle = true;
    s.value = 30.0f;
    restart(s);
    check(s.isActive && s.value == 66.0f && !s.stepToogle, "restart: on, the amount, the long step");
    check(s.getTempoCorrection() == 116.0f, "and the first step is the long one");

    s.stepToogle = true;
    s.value = 30.0f;
    followAmount(s);
    check(s.isActive && s.value == 66.0f && s.stepToogle, "an amount edit keeps the pair's position");

    followSwitch(s, false);
    check(!s.isActive, "switched off: no swing");
    check(s.stepToogle, "and the pair's position stays");
    s.value = 30.0f;
    followSwitch(s, true);
    check(s.isActive && s.value == 66.0f && s.stepToogle, "switched on: the amount from here on");

    settings.amount = 0;
    restart(s);
    check(s.value == 25.0f, "the lowest setting is 25");
    settings.amount = 50;
    restart(s);
    check(s.value == 75.0f, "the highest setting is 75");
    settings = Settings{};
    restart(s);
    check(s.value == 50.0f && s.getTempoCorrection() == 100.0f, "the default is no swing");
}

void testRecord()
{
    uint8_t buffer[256];
    Store store(buffer, sizeof(buffer), "TEST");
    store.clear();

    settings.amount = 7;
    load(store);
    check(settings.amount == 25 && amountOf(settings) == 50, "no record: the default, 50");

    check(!storeIfEdited(store), "unchanged default writes nothing");
    check(store.find(kRecord, nullptr, nullptr) == nullptr, "no record for the default");

    settings.amount = 41;
    check(storeIfEdited(store), "an edit is stored");
    check(!storeIfEdited(store), "and only once");

    settings.amount = 0;
    load(store);
    check(settings.amount == 41, "the record comes back");

    uint8_t version = 0;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, &version, &length);
    check(data != nullptr && version == kRecordVersion && length == 1 && data[0] == 41, "its bytes");
    check(kRecord == 5 && static_cast<int>(Mod::swing) == 4, "mod 4, record 5");

    settings.amount = 25;
    check(storeIfEdited(store), "back to the default is an edit too");
    load(store);
    check(settings.amount == 25, "and is what loads");

    uint8_t bad = 51;
    store.put(kRecord, kRecordVersion, &bad, 1);
    settings.amount = 0;
    load(store);
    check(settings.amount == 25, "an amount out of range: the default");
    uint8_t longer[2] = { 3, 3 };
    store.put(kRecord, kRecordVersion, longer, 2);
    settings.amount = 0;
    load(store);
    check(settings.amount == 25, "a record of another size: the default");
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

// As the 1.2.0 patch writes it.
std::vector<uint8_t> sideFile(uint8_t on, uint8_t amount)
{
    const std::vector<uint8_t> data = { on, amount, 0, 0 };
    uint32_t h = 2166136261u;
    for (uint8_t b : data)
    {
        h = (h ^ b) * 16777619u;
    }
    std::vector<uint8_t> file = { 'H', 'S', 'W', '1' };
    file.insert(file.end(), data.begin(), data.end());
    for (int i = 0; i < 4; i++)
    {
        file.push_back(static_cast<uint8_t>(h >> (8 * i)));
    }
    return file;
}

const char kPath[] = "/Projects/Song/swing.bin";

void testImportFile()
{
    FakeIo io;
    uint8_t buffer[256];
    Store store(buffer, sizeof(buffer), "TEST");
    store.clear();

    check(!importSideFile(io, "/Projects/Song", store), "no side file: nothing to import");
    check(store.empty(), "no side file: no record");

    io.files[kPath] = sideFile(1, 41);
    check(importSideFile(io, "/Projects/Song", store), "Global swing On: the project used the mod");
    load(store);
    check(amountOf(settings) == 66, "its amount");

    store.clear();
    io.files[kPath] = sideFile(0, 11);
    check(!importSideFile(io, "/Projects/Song", store), "Global swing Off: the mod stays Off");
    load(store);
    check(amountOf(settings) == 36, "but the amount comes along");

    const auto refused = [&](const std::vector<uint8_t>& file, const char* name) {
        store.clear();
        io.files[kPath] = file;
        check(!importSideFile(io, "/Projects/Song", store), name);
        check(store.empty(), std::string(name) + ": no record");
    };
    std::vector<uint8_t> broken = sideFile(1, 41);
    broken[8] ^= 1;
    refused(broken, "a bad checksum is refused");
    refused(sideFile(2, 41), "a bad switch is refused");
    refused(sideFile(1, 51), "an amount out of range is refused");
    broken = sideFile(1, 41);
    broken[0] = 'X';
    refused(broken, "another magic is refused");
    broken = sideFile(1, 41);
    broken.pop_back();
    refused(broken, "a short file is refused");
    broken = sideFile(1, 41);
    broken.push_back(0);
    refused(broken, "a long file is refused");

    settings = Settings{};
}

void testImportThroughProject()
{
    const size_t swingIndex = static_cast<size_t>(Mod::swing);
    uint8_t buffer[1024];
    Store store(buffer, sizeof(buffer), project::kMagic);
    uint8_t defaults[kModCount] = {};
    uint8_t set[kModCount] = {};

    {
        FakeIo io;
        io.files[kPath] = sideFile(1, 41);
        project::loadFromWorkspace(io, store, "/Projects/Song", defaults, set, kModCount);
        load(store);
        check(set[swingIndex] == 1, "a 1.2.0 project with it On: the mod starts On");
        check(amountOf(settings) == 66, "with its amount");
        check(io.files.count(project::kWorkspaceFile) == 1, "written to the Workspace");

        // the next load finds Workspace/mods.bin and doesn't import again
        io.files[kPath] = sideFile(1, 11);
        std::fill(std::begin(set), std::end(set), uint8_t{ 0 });
        settings = Settings{};
        project::loadFromWorkspace(io, store, "/Projects/Song", defaults, set, kModCount);
        load(store);
        check(set[swingIndex] == 1 && amountOf(settings) == 66, "with mods.bin there, no import");
    }
    {
        FakeIo io;
        io.files[kPath] = sideFile(0, 11);
        std::fill(std::begin(set), std::end(set), uint8_t{ 1 });
        project::loadFromWorkspace(io, store, "/Projects/Song", defaults, set, kModCount);
        load(store);
        check(set[swingIndex] == 0, "a 1.2.0 project with it Off: the mod stays Off");
        check(amountOf(settings) == 36, "with its amount");
    }
    {
        FakeIo io;
        project::loadFromWorkspace(io, store, "/Projects/Song", defaults, set, kModCount);
        load(store);
        check(set[swingIndex] == 0 && amountOf(settings) == 50, "no files at all: Off, the default");
    }
    {
        FakeIo io;
        io.files[kPath] = sideFile(1, 41);
        defaults[swingIndex] = 0;
        project::loadFromWorkspace(io, store, nullptr, defaults, set, kModCount);
        load(store);
        check(set[swingIndex] == 0 && amountOf(settings) == 50, "a new project ignores the side file");
    }
    settings = Settings{};
}

} // namespace

int main()
{
    testHooks();
    testRecord();
    testImportFile();
    testImportThroughProject();
    printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
