// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the mods' settings files: the record store, the switches record,
// mods.cfg (defaults for new projects) and the per-project mods.bin flows (Open,
// workspace load, Save, Save As, New Project) with the project's mod set, on an
// in-memory SD card. Build and run with `make -C core/mods/tests`.

#include "mods.h"
#include "modsConfig.h"
#include "modsProject.h"
#include "modsStore.h"
#include "modsSwitches.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace mods;

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const char* name)
{
    checks++;
    if (!condition)
    {
        failures++;
        printf("FAIL %s\n", name);
    }
}

using Bytes = std::vector<uint8_t>;

class MemoryFileIo : public FileIo
{
  public:
    std::map<std::string, Bytes> files;
    std::set<std::string> dirs;
    int writes = 0;

    size_t read(const char* path, uint8_t* buffer, size_t capacity) override
    {
        auto it = files.find(path);
        if (it == files.end())
        {
            return kReadFailed;
        }
        const size_t n = it->second.size() < capacity ? it->second.size() : capacity;
        memcpy(buffer, it->second.data(), n);
        return it->second.size();
    }

    bool write(const char* path, const uint8_t* data, size_t length) override
    {
        writes++;
        files[path] = Bytes(data, data + length);
        return true;
    }

    void remove(const char* path) override { files.erase(path); }
    void makeDir(const char* path) override { dirs.insert(path); }

    bool has(const char* path) const { return files.count(path) != 0; }
};

struct TestStore
{
    uint8_t buffer[512];
    Store store;
    explicit TestStore(const char* magic, size_t capacity = 512)
      : store(buffer, capacity, magic)
    {
    }
};

Bytes image(Store& store)
{
    const size_t size = store.finalize();
    return Bytes(store.image(), store.image() + size);
}

// A valid file with the given records, built through a separate store.
Bytes makeFile(const char* magic, std::vector<std::pair<uint8_t, Bytes>> records)
{
    TestStore t(magic);
    for (auto& record : records)
    {
        t.store.put(record.first, 1, record.second.data(), record.second.size());
    }
    return image(t.store);
}

bool loadImage(Store& store, const Bytes& file)
{
    if (file.size() <= store.capacity())
    {
        memcpy(store.image(), file.data(), file.size());
    }
    return store.validate(file.size());
}

Bytes recordOf(Store& store, uint8_t id)
{
    uint16_t length = 0;
    const uint8_t* data = store.find(id, nullptr, &length);
    return data == nullptr ? Bytes{} : Bytes(data, data + length);
}

//------------------------------------------------------------------------------------------------

void testCrc()
{
    const char* text = "123456789";
    check(crc32(reinterpret_cast<const uint8_t*>(text), 9) == 0xCBF43926u, "crc32 check value");
    check(crc32(nullptr, 0) == 0, "crc32 of nothing");
}

void testStoreRecords()
{
    TestStore t("MTP1");
    Store& s = t.store;

    check(s.empty(), "new store is empty");
    check(s.finalize() == Store::kHeaderSize, "empty image is a header");

    const Bytes a = { 1, 2, 3 };
    const Bytes b = { 9 };
    check(s.put(5, 1, a.data(), a.size()), "put a");
    check(s.put(6, 2, b.data(), b.size()), "put b");
    check(recordOf(s, 5) == a && recordOf(s, 6) == b, "find a and b");

    uint8_t version = 0;
    uint16_t length = 0;
    check(s.find(6, &version, &length) != nullptr && version == 2 && length == 1,
          "find reports version and length");
    check(s.find(7, nullptr, nullptr) == nullptr, "missing record");

    const Bytes a2 = { 7, 7, 7, 7, 7 };
    check(s.put(5, 3, a2.data(), a2.size()), "replace a");
    check(recordOf(s, 5) == a2 && recordOf(s, 6) == b, "replace keeps b");

    s.remove(6);
    check(recordOf(s, 6).empty() && recordOf(s, 5) == a2, "remove b keeps a");
    s.remove(6);
    check(recordOf(s, 5) == a2, "removing a missing record is harmless");

    check(s.put(8, 1, nullptr, 0), "empty record");
    check(s.find(8, nullptr, &length) != nullptr && length == 0, "empty record found");
}

void testStoreCapacity()
{
    uint8_t buffer[32];
    Store s(buffer, sizeof(buffer), "MTP1");
    const Bytes twelve(12, 0xAA);
    check(s.put(1, 1, twelve.data(), twelve.size()), "fits exactly-ish");
    const size_t before = s.payloadSize();
    const Bytes big(20, 0xBB);
    check(!s.put(2, 1, big.data(), big.size()), "too big refused");
    check(s.payloadSize() == before && recordOf(s, 1) == twelve, "refused put changes nothing");
    check(!s.put(1, 1, big.data(), big.size()), "too big replace refused");
    check(recordOf(s, 1) == twelve, "refused replace keeps old record");
}

void testStoreValidate()
{
    const Bytes good = makeFile("MTP1", { { 1, { 1, 2 } }, { 200, { 3, 4, 5 } } });

    {
        TestStore t("MTP1");
        check(loadImage(t.store, good), "good file validates");
        check(recordOf(t.store, 200) == Bytes({ 3, 4, 5 }), "unknown record kept");
        check(image(t.store) == good, "round trip is byte-identical");
    }

    struct Case
    {
        const char* name;
        Bytes file;
    };
    std::vector<Case> bad;

    Bytes f = good;
    f[0] = 'X';
    bad.push_back({ "wrong magic", f });

    f = good;
    f[4] = 2;
    bad.push_back({ "wrong format", f });

    f = good;
    f.back() ^= 1;
    bad.push_back({ "bad crc", f });

    f = good;
    f.push_back(0);
    bad.push_back({ "trailing byte", f });

    f = good;
    f.pop_back();
    bad.push_back({ "truncated", f });

    bad.push_back({ "shorter than a header", Bytes(good.begin(), good.begin() + 8) });
    bad.push_back({ "other store's magic", makeFile("MTC1", { { 1, { 1 } } }) });

    {
        // A record claiming more data than the payload holds, with a correct CRC.
        TestStore t("MTP1");
        const Bytes data = { 1, 2, 3 };
        t.store.put(1, 1, data.data(), data.size());
        t.store.image()[Store::kHeaderSize + 2] = 10;
        t.store.image()[Store::kHeaderSize + 3] = 0;
        const size_t size = Store::kHeaderSize + t.store.payloadSize();
        const uint32_t crc = crc32(t.store.image() + Store::kHeaderSize, t.store.payloadSize());
        for (int i = 0; i < 4; i++)
        {
            t.store.image()[8 + i] = static_cast<uint8_t>(crc >> (8 * i));
        }
        bad.push_back({ "record overruns", Bytes(t.store.image(), t.store.image() + size) });
    }

    {
        // Two records with the same id, with a correct CRC.
        Bytes file = makeFile("MTP1", { { 1, { 1 } }, { 2, { 2 } } });
        file[Store::kHeaderSize + 5] = 1; // second record's id
        const uint32_t crc = crc32(file.data() + Store::kHeaderSize, file.size() - Store::kHeaderSize);
        for (int i = 0; i < 4; i++)
        {
            file[8 + i] = static_cast<uint8_t>(crc >> (8 * i));
        }
        bad.push_back({ "duplicate ids", file });
    }

    for (auto& c : bad)
    {
        TestStore t("MTP1");
        const Bytes data = { 9 };
        t.store.put(9, 1, data.data(), data.size());
        const bool ok = loadImage(t.store, c.file);
        char name[96];
        snprintf(name, sizeof(name), "rejects %s", c.name);
        check(!ok && t.store.empty(), name);
    }

    {
        uint8_t buffer[16];
        Store s(buffer, sizeof(buffer), "MTP1");
        check(!s.validate(good.size()), "rejects a file larger than the buffer");
    }
}

//------------------------------------------------------------------------------------------------

void testSwitches()
{
    TestStore t(project::kMagic);
    uint8_t values[3] = { 9, 9, 9 };

    readSwitches(t.store, values, 3);
    check(Bytes(values, values + 3) == Bytes({ 0, 0, 0 }), "no record: every mod Off");

    check(writeSwitches(t.store, values, 0) && t.store.empty(),
          "a build without mods writes no record");

    const uint8_t on[3] = { 1, 0, 7 };
    check(writeSwitches(t.store, on, 3), "write");
    check(recordOf(t.store, kSwitchesRecord) == Bytes({ 1, 0, 1 }), "stored as 0 and 1");

    // An older build (two mods) reads and writes back a newer build's record.
    uint8_t two[2] = {};
    readSwitches(t.store, two, 2);
    check(Bytes(two, two + 2) == Bytes({ 1, 0 }), "older build reads its own mods");
    two[0] = 0;
    check(writeSwitches(t.store, two, 2) &&
            recordOf(t.store, kSwitchesRecord) == Bytes({ 0, 0, 1 }),
          "older build keeps the newer mods' bytes");

    // A newer build (four mods) reads an older record: the new mod is Off.
    uint8_t four[4] = { 9, 9, 9, 9 };
    t.store.put(kSwitchesRecord, kSwitchesVersion, on, 2);
    readSwitches(t.store, four, 4);
    check(Bytes(four, four + 4) == Bytes({ 1, 0, 0, 0 }), "mods past the record are Off");

    // A project saved with only the retired Chords On (id 14): no other mod turns On.
    uint8_t chordsEra[15] = {};
    chordsEra[14] = 1;
    t.store.put(kSwitchesRecord, kSwitchesVersion, chordsEra, 15);
    uint8_t loaded[15];
    memset(loaded, 9, sizeof loaded);
    readSwitches(t.store, loaded, 15);
    check(loaded[1] == 0 && loaded[14] == 1, "a retired mod's byte stays at its own id");
}

void testConfig()
{
    MemoryFileIo io;
    TestStore t(config::kMagic);
    uint8_t defaults[2] = { 9, 9 };

    config::load(io, t.store, defaults, 2);
    check(t.store.empty() && Bytes(defaults, defaults + 2) == Bytes({ 0, 0 }),
          "no mods.cfg: every default Off");

    // A newer build's file: defaults for a mod this build doesn't know, plus another
    // record. Saving must keep both.
    io.files[config::kPath] =
      makeFile(config::kMagic, { { kSwitchesRecord, { 1, 0, 1 } }, { 42, { 5, 6 } } });
    config::load(io, t.store, defaults, 2);
    check(Bytes(defaults, defaults + 2) == Bytes({ 1, 0 }), "defaults read");
    defaults[1] = 1;
    check(config::save(io, t.store, defaults, 2), "save");
    check(io.dirs.count(config::kDir) == 1, "save makes the Mods folder");

    TestStore back(config::kMagic);
    check(loadImage(back.store, io.files[config::kPath]), "saved mods.cfg validates");
    check(recordOf(back.store, kSwitchesRecord) == Bytes({ 1, 1, 1 }),
          "defaults saved, unknown mods' kept");
    check(recordOf(back.store, 42) == Bytes({ 5, 6 }), "other records kept");

    io.files[config::kPath][Store::kHeaderSize] ^= 0xFF;
    config::load(io, t.store, defaults, 2);
    check(t.store.empty() && Bytes(defaults, defaults + 2) == Bytes({ 0, 0 }),
          "damaged mods.cfg: every default Off");
    check(config::save(io, t.store, defaults, 2), "save after damage");
    check(loadImage(back.store, io.files[config::kPath]), "rewritten mods.cfg validates");
}

//------------------------------------------------------------------------------------------------

constexpr uint8_t kTestRecord = 77;
constexpr size_t kTestMods = 3;
constexpr uint8_t kImportedMod = 1;
int importerCalls = 0;
int changedCalls = 0;

// Stands in for a mod's 1.2.0 importer: turns "<folder>/old.bin" into a record.
bool testImporter(FileIo& io, const char* projectFolder, Store& store)
{
    importerCalls++;
    std::string path = std::string(projectFolder) + "/old.bin";
    uint8_t data[8];
    const size_t size = io.read(path.c_str(), data, sizeof(data));
    if (size != FileIo::kReadFailed && size <= sizeof(data))
    {
        store.put(kTestRecord, 1, data, static_cast<uint16_t>(size));
        return true;
    }
    return false;
}

const project::ImporterEntry kTestImporters[] = { { kImportedMod, testImporter } };

// The mods' arrays for a build with kTestMods mods.
uint8_t defaults[kTestMods];
uint8_t switches[kTestMods];

void load(MemoryFileIo& io, Store& store, const char* folder)
{
    project::loadFromWorkspace(io, store, folder, defaults, switches, kTestMods);
}

Bytes set()
{
    return Bytes(switches, switches + kTestMods);
}

std::string projectFile(const char* folder)
{
    return std::string(folder) + "/" + project::kFileName;
}

void setRecord(Store& store, const Bytes& data)
{
    store.put(kTestRecord, 1, data.data(), data.size());
    project::markChanged();
}

void testProjectFlows()
{
    project::setImporters(kTestImporters, 1);
    project::setChangedCallback([] { changedCalls++; });

    MemoryFileIo io;
    TestStore t(project::kMagic);
    Store& s = t.store;
    const char* a = "Projects/A";
    const char* b = "Projects/B";

    memset(defaults, 1, sizeof(defaults));

    // Open a project with a mods.bin: its own set, whatever the defaults.
    const Bytes fileA = makeFile(
      project::kMagic,
      { { kSwitchesRecord, { 0, 1, 0 } }, { kTestRecord, { 1, 1 } }, { 250, { 8 } } });
    io.files[projectFile(a)] = fileA;
    project::copyIn(io, s, a);
    check(io.files[project::kWorkspaceFile] == fileA, "open copies mods.bin into the Workspace");
    importerCalls = 0;
    load(io, s, a);
    check(recordOf(s, kTestRecord) == Bytes({ 1, 1 }), "load reads the Workspace copy");
    check(set() == Bytes({ 0, 1, 0 }), "the project's own mod set, not the defaults");
    check(importerCalls == 0, "no import when the project has mods.bin");
    check(!project::isDirty(), "freshly loaded is clean");

    // Changing the defaults leaves the open project alone.
    memset(defaults, 0, sizeof(defaults));
    load(io, s, a);
    check(set() == Bytes({ 0, 1, 0 }), "defaults don't touch an open project");
    memset(defaults, 1, sizeof(defaults));

    // Project Settings -> Mods: a switch change is a project change.
    changedCalls = 0;
    switches[2] = 1;
    check(project::setSwitches(s, switches, kTestMods), "set switches");
    check(changedCalls == 1 && project::isDirty(), "a switch change marks the project changed");
    check(recordOf(s, kSwitchesRecord) == Bytes({ 0, 1, 1 }), "switch change recorded");
    project::saveToWorkspace(io, s);

    // Workspace save writes only after a change.
    io.writes = 0;
    project::saveToWorkspace(io, s);
    check(io.writes == 0, "clean workspace save writes nothing");
    changedCalls = 0;
    setRecord(s, { 2, 2 });
    check(changedCalls == 1 && project::isDirty(), "a change marks the project changed");
    project::saveToWorkspace(io, s);
    check(!project::isDirty(), "workspace save clears dirty");
    {
        TestStore w(project::kMagic);
        check(loadImage(w.store, io.files[project::kWorkspaceFile]) &&
                recordOf(w.store, kTestRecord) == Bytes({ 2, 2 }),
              "workspace holds the change");
    }

    // Save writes the project folder; unknown records survive.
    check(project::saveToProject(io, s, a), "save");
    {
        TestStore p(project::kMagic);
        check(loadImage(p.store, io.files[projectFile(a)]), "saved mods.bin validates");
        check(recordOf(p.store, kTestRecord) == Bytes({ 2, 2 }), "saved setting");
        check(recordOf(p.store, kSwitchesRecord) == Bytes({ 0, 1, 1 }), "saved mod set");
        check(recordOf(p.store, 250) == Bytes({ 8 }), "unknown record survives a save");
    }

    // Save As over another project replaces its mods.bin.
    io.files[projectFile(b)] = makeFile(project::kMagic, { { kTestRecord, { 9, 9, 9 } } });
    project::saveToProject(io, s, b);
    check(io.files[projectFile(b)] == io.files[projectFile(a)],
          "Save As over a project replaces its mods.bin");

    // Open a project without mods.bin (stock 1.2.0 or CE): Workspace copy removed,
    // 1.2.0 side file imported once and its mod On, the rest Off whatever the
    // defaults; then saved with a mods.bin.
    const char* old = "Projects/Old";
    io.files[std::string(old) + "/old.bin"] = { 4, 5 };
    project::copyIn(io, s, old);
    check(!io.has(project::kWorkspaceFile), "open without mods.bin clears the Workspace copy");
    importerCalls = 0;
    load(io, s, old);
    check(importerCalls == 1 && recordOf(s, kTestRecord) == Bytes({ 4, 5 }),
          "1.2.0 side file imported");
    check(set() == Bytes({ 0, 1, 0 }), "only the imported mod is On");
    check(io.has(project::kWorkspaceFile), "imported settings written to the Workspace");
    {
        TestStore w(project::kMagic);
        check(loadImage(w.store, io.files[project::kWorkspaceFile]) &&
                recordOf(w.store, kSwitchesRecord) == Bytes({ 0, 1, 0 }),
              "imported mod set written to the Workspace");
    }

    // Back to defaults, save, reopen: the side file must not come back.
    s.remove(kTestRecord);
    switches[kImportedMod] = 0;
    project::setSwitches(s, switches, kTestMods);
    project::saveToWorkspace(io, s);
    project::saveToProject(io, s, old);
    project::copyIn(io, s, old);
    importerCalls = 0;
    load(io, s, old);
    check(importerCalls == 0 && recordOf(s, kTestRecord).empty(),
          "a saved project is never imported again");
    check(set() == Bytes({ 0, 0, 0 }), "the mod stays Off once turned off");

    // A plain CE project with no side files: every mod Off.
    const char* plain = "Projects/Plain";
    project::copyIn(io, s, plain);
    load(io, s, plain);
    check(set() == Bytes({ 0, 0, 0 }), "plain CE project: every mod Off");

    // A mods.bin from a build without this mod yet: the importer still gets its one turn.
    const char* older = "Projects/Older";
    io.files[projectFile(older)] =
      makeFile(project::kMagic, { { kSwitchesRecord, { 0, 0, 1 } }, { 250, { 8 } } });
    io.files[std::string(older) + "/old.bin"] = { 6, 7 };
    project::copyIn(io, s, older);
    importerCalls = 0;
    load(io, s, older);
    check(importerCalls == 1 && recordOf(s, kTestRecord) == Bytes({ 6, 7 }),
          "an older build's mods.bin: the side file is imported");
    check(set() == Bytes({ 0, 1, 1 }), "that mod On, the project's own switches kept");
    check(recordOf(s, 250) == Bytes({ 8 }), "and its other records kept");
    {
        TestStore w(project::kMagic);
        check(loadImage(w.store, io.files[project::kWorkspaceFile]) &&
                recordOf(w.store, kSwitchesRecord) == Bytes({ 0, 1, 1 }),
              "written to the Workspace");
    }
    switches[kImportedMod] = 0;
    s.remove(kTestRecord);
    project::setSwitches(s, switches, kTestMods);
    project::saveToProject(io, s, older);
    project::copyIn(io, s, older);
    importerCalls = 0;
    load(io, s, older);
    check(importerCalls == 0 && set() == Bytes({ 0, 0, 1 }), "and only once: turned off stays off");

    // That mod already set up in the project: the side file doesn't overwrite it.
    const char* setUp = "Projects/SetUp";
    io.files[projectFile(setUp)] = makeFile(project::kMagic, { { kSwitchesRecord, { 0, 1, 0 } } });
    io.files[std::string(setUp) + "/old.bin"] = { 1 };
    project::copyIn(io, s, setUp);
    importerCalls = 0;
    load(io, s, setUp);
    check(importerCalls == 0 && set() == Bytes({ 0, 1, 0 }), "a mod already On is not imported over");
    const char* withRecord = "Projects/WithRecord";
    io.files[projectFile(withRecord)] = makeFile(
      project::kMagic, { { kSwitchesRecord, { 0, 0, 0 } }, { kImportedMod + 1, { 3 } } });
    io.files[std::string(withRecord) + "/old.bin"] = { 1 };
    project::copyIn(io, s, withRecord);
    importerCalls = 0;
    load(io, s, withRecord);
    check(importerCalls == 0 && set() == Bytes({ 0, 0, 0 }), "nor a mod with settings of its own");

    // A damaged project file counts as none.
    io.files[projectFile(a)][Store::kHeaderSize] ^= 0xFF;
    project::copyIn(io, s, a);
    check(!io.has(project::kWorkspaceFile), "damaged mods.bin not copied");

    // New Project: empty Workspace, no folder. The defaults, nothing imported.
    io.files.erase(project::kWorkspaceFile);
    importerCalls = 0;
    defaults[0] = 1;
    defaults[1] = 0;
    defaults[2] = 1;
    load(io, s, nullptr);
    check(importerCalls == 0 && recordOf(s, kTestRecord).empty(), "new project: no settings");
    check(set() == Bytes({ 1, 0, 1 }), "new project starts with the defaults");
    {
        TestStore w(project::kMagic);
        check(loadImage(w.store, io.files[project::kWorkspaceFile]) &&
                recordOf(w.store, kSwitchesRecord) == Bytes({ 1, 0, 1 }),
              "new project's Workspace file holds the defaults");
    }

    // A build without mods: New Project writes an empty file, as in step 0.
    io.files.erase(project::kWorkspaceFile);
    project::loadFromWorkspace(io, s, nullptr, defaults, switches, 0);
    check(s.empty(), "no mods: new project starts empty");

    // Boot: the Workspace already has the project's settings and set.
    io.files.erase(project::kWorkspaceFile);
    load(io, s, nullptr);
    setRecord(s, { 6 });
    project::saveToWorkspace(io, s);
    s.clear();
    memset(switches, 0, sizeof(switches));
    memset(defaults, 0, sizeof(defaults));
    load(io, s, a);
    check(recordOf(s, kTestRecord) == Bytes({ 6 }), "boot loads the Workspace settings");
    check(set() == Bytes({ 1, 0, 1 }), "boot loads the Workspace mod set");

    // A too-long folder path fails safely.
    std::string longFolder(project::kPathSize, 'x');
    check(!project::saveToProject(io, s, longFolder.c_str()), "over-long path refused");
}

} // namespace

int main()
{
    testCrc();
    testStoreRecords();
    testStoreCapacity();
    testStoreValidate();
    testSwitches();
    testConfig();
    testProjectFlows();

    if (failures == 0)
    {
        printf("PASS %d checks\n", checks);
    }
    printf("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
