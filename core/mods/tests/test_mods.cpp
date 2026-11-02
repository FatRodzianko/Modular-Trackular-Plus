// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the mods' settings files: the record store, mods.cfg and the
// per-project mods.bin flows (Open, workspace load, Save, Save As, New Project), on an
// in-memory SD card. Build and run with `make -C core/mods/tests`.

#include "mods.h"
#include "modsConfig.h"
#include "modsProject.h"
#include "modsStore.h"

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

void testConfig()
{
    MemoryFileIo io;
    TestStore t(config::kMagic);

    config::load(io, t.store);
    check(t.store.empty(), "no mods.cfg: no records");

    // A newer build's file: switches for three mods this build doesn't know, plus
    // another record. Saving must keep both.
    io.files[config::kPath] =
      makeFile(config::kMagic, { { config::kSwitchesRecord, { 1, 0, 1 } }, { 42, { 5, 6 } } });
    config::load(io, t.store);
    check(config::save(io, t.store), "save");
    check(io.dirs.count(config::kDir) == 1, "save makes the Mods folder");

    TestStore back(config::kMagic);
    check(loadImage(back.store, io.files[config::kPath]), "saved mods.cfg validates");
    check(recordOf(back.store, config::kSwitchesRecord) == Bytes({ 1, 0, 1 }),
          "unknown mods' switches kept");
    check(recordOf(back.store, 42) == Bytes({ 5, 6 }), "other records kept");

    io.files[config::kPath][Store::kHeaderSize] ^= 0xFF;
    config::load(io, t.store);
    check(t.store.empty(), "damaged mods.cfg: defaults");
    check(config::save(io, t.store), "save after damage");
    check(loadImage(back.store, io.files[config::kPath]), "rewritten mods.cfg validates");
}

//------------------------------------------------------------------------------------------------

constexpr uint8_t kTestRecord = 77;
int importerCalls = 0;
int changedCalls = 0;

// Stands in for a mod's 1.2.0 importer: turns "<folder>/old.bin" into a record.
void testImporter(FileIo& io, const char* projectFolder, Store& store)
{
    importerCalls++;
    std::string path = std::string(projectFolder) + "/old.bin";
    uint8_t data[8];
    const size_t size = io.read(path.c_str(), data, sizeof(data));
    if (size != FileIo::kReadFailed && size <= sizeof(data))
    {
        store.put(kTestRecord, 1, data, static_cast<uint16_t>(size));
    }
}

const project::Importer kTestImporters[] = { testImporter };

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

    // Open a project with a mods.bin.
    const Bytes fileA = makeFile(project::kMagic, { { kTestRecord, { 1, 1 } }, { 250, { 8 } } });
    io.files[projectFile(a)] = fileA;
    project::copyIn(io, s, a);
    check(io.files[project::kWorkspaceFile] == fileA, "open copies mods.bin into the Workspace");
    importerCalls = 0;
    project::loadFromWorkspace(io, s, a);
    check(recordOf(s, kTestRecord) == Bytes({ 1, 1 }), "load reads the Workspace copy");
    check(importerCalls == 0, "no import when the project has mods.bin");
    check(!project::isDirty(), "freshly loaded is clean");

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
        check(recordOf(p.store, 250) == Bytes({ 8 }), "unknown record survives a save");
    }

    // Save As over another project replaces its mods.bin.
    io.files[projectFile(b)] = makeFile(project::kMagic, { { kTestRecord, { 9, 9, 9 } } });
    project::saveToProject(io, s, b);
    check(io.files[projectFile(b)] == io.files[projectFile(a)],
          "Save As over a project replaces its mods.bin");

    // Open a project without mods.bin (stock 1.2.0 or CE): Workspace copy removed,
    // 1.2.0 side file imported once, then saved with a mods.bin.
    const char* old = "Projects/Old";
    io.files[std::string(old) + "/old.bin"] = { 4, 5 };
    project::copyIn(io, s, old);
    check(!io.has(project::kWorkspaceFile), "open without mods.bin clears the Workspace copy");
    importerCalls = 0;
    project::loadFromWorkspace(io, s, old);
    check(importerCalls == 1 && recordOf(s, kTestRecord) == Bytes({ 4, 5 }),
          "1.2.0 side file imported");
    check(io.has(project::kWorkspaceFile), "imported settings written to the Workspace");

    // Back to defaults, save, reopen: the side file must not come back.
    s.remove(kTestRecord);
    project::markChanged();
    project::saveToWorkspace(io, s);
    project::saveToProject(io, s, old);
    project::copyIn(io, s, old);
    importerCalls = 0;
    project::loadFromWorkspace(io, s, old);
    check(importerCalls == 0 && recordOf(s, kTestRecord).empty(),
          "a saved project is never imported again");

    // A damaged project file counts as none.
    io.files[projectFile(a)][Store::kHeaderSize] ^= 0xFF;
    project::copyIn(io, s, a);
    check(!io.has(project::kWorkspaceFile), "damaged mods.bin not copied");

    // New Project: empty Workspace, no folder. Defaults, nothing imported.
    io.files.erase(project::kWorkspaceFile);
    importerCalls = 0;
    project::loadFromWorkspace(io, s, nullptr);
    check(importerCalls == 0 && s.empty(), "new project starts empty");
    {
        TestStore w(project::kMagic);
        check(loadImage(w.store, io.files[project::kWorkspaceFile]), "new project's Workspace file");
    }

    // Boot: the Workspace already has the project's settings.
    setRecord(s, { 6 });
    project::saveToWorkspace(io, s);
    s.clear();
    project::loadFromWorkspace(io, s, a);
    check(recordOf(s, kTestRecord) == Bytes({ 6 }), "boot loads the Workspace settings");

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
    testConfig();
    testProjectFlows();

    if (failures == 0)
    {
        printf("PASS %d checks\n", checks);
    }
    printf("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
