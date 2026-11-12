// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the Synth FX plugins' runtime, the parts that don't run a plugin's code: the
// pools (synthfxPool.h), the header check, the start (the folder, the pack written where it
// is missing, the start marker), the catalog, safe mode, the firmware's copy of ptsfx.h. A
// plugin's code is Arm: making instances, knobs, process() and the guards are tested on the
// emulated Cortex-M7 (tools/synthfx/test_runtime.py). Build and run with
// `make -C core/mods/tests`.

#include "synthfx/synthfxPlugins.h"
#include "synthfx/synthfxPool.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <strings.h>
#include <vector>

using namespace mods;
using namespace mods::synthfx;
using Status = mods::synthfx::plugins::Status;

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

// ---- the pool -----------------------------------------------------------------------------

void testPool()
{
    alignas(32) static uint8_t memory[1024];
    Pool<4> pool;
    pool.init(memory, sizeof(memory));
    const uint32_t a = pool.alloc(100);
    const uint32_t b = pool.alloc(200);
    const uint32_t c = pool.alloc(1);
    check(a == 0 && b == 128 && c == 128 + 224, "pool: first fit from the start, 32-byte blocks");
    check(pool.used() == 128 + 224 + 32, "pool: used counts whole blocks");
    pool.free(b);
    check(pool.blocks() == 2 && pool.used() == 160, "pool: a block goes back");
    check(pool.alloc(300) == 384, "pool: too big for the hole: after the last block");
    check(pool.alloc(224) == 128, "pool: the hole is reused, nothing moved");
    check(pool.alloc(1) == Pool<4>::kNone, "pool: the table full");
    pool.free(12345);
    check(pool.blocks() == 4, "pool: freeing an offset never given does nothing");
    pool.free(0);
    check(pool.alloc(32) == 0, "pool: the first hole again");
    pool.init(memory, sizeof(memory));
    check(pool.alloc(1025) == Pool<4>::kNone && pool.alloc(0) == Pool<4>::kNone, "pool: 0 and over the size refused");
    check(pool.alloc(1024) == 0 && pool.alloc(1) == Pool<4>::kNone, "pool: all of it, then none");
    pool.init(memory, sizeof(memory));
    pool.alloc(256);
    const uint32_t mid = pool.alloc(256);
    pool.alloc(256);
    pool.free(mid);
    check(pool.largestFree() == 256, "pool: largest free is the biggest hole");
    check(pool.alloc(257) == Pool<4>::kNone, "pool: no hole fits");
    check(pool.at(mid) == memory + mid, "pool: at is the address");
}

// ---- the pack and the header --------------------------------------------------------------

PtsfxHeader headerOf(const plugins::PackFile& f)
{
    PtsfxHeader h;
    memcpy(&h, f.data, sizeof(h));
    return h;
}

void testHeader()
{
    using plugins::checkHeader;
    check(plugins::packCount() == 6, "pack: six files");
    const char* names[] = { "Overdrive", "Distortion", "Bit Crusher", "Chorus", "Flanger", "Phaser" };
    for (size_t i = 0; i < plugins::packCount(); i++)
    {
        const plugins::PackFile& f = plugins::packAt(i);
        const PtsfxHeader h = headerOf(f);
        const char* why = "unset";
        check(checkHeader(h, f.size, &why) && why == nullptr, std::string("pack: passes the check: ") + f.name);
        check(strcmp(h.name, names[i]) == 0, std::string("pack: its name: ") + f.name);
        check((h.flags == PTSFX_FLAG_WIDENS) == (i >= 3), std::string("pack: the modulation effects widen: ") + f.name);
    }

    const plugins::PackFile& f = plugins::packAt(4); // Flanger: four knobs and a format()
    const PtsfxHeader good = headerOf(f);
    struct Plant
    {
        const char* what;
        void (*spoil)(PtsfxHeader&);
        int sizeChange;
    };
    const Plant plants[] = {
        { "magic", [](PtsfxHeader& h) { h.magic ^= 1; }, 0 },
        { "version", [](PtsfxHeader& h) { h.format_version = 2; }, 0 },
        { "an unknown flag", [](PtsfxHeader& h) { h.flags |= 0x8000; }, 0 },
        { "PRE and WIDENS", [](PtsfxHeader& h) { h.flags = PTSFX_FLAG_PRE | PTSFX_FLAG_WIDENS; }, 0 },
        { "an empty name", [](PtsfxHeader& h) { h.name[0] = 0; }, 0 },
        { "a name without its 0", [](PtsfxHeader& h) { memset(h.name, 'x', sizeof(h.name)); }, 0 },
        { "an author without its 0", [](PtsfxHeader& h) { memset(h.author, 'x', sizeof(h.author)); }, 0 },
        { "13 knobs", [](PtsfxHeader& h) { h.num_params = 13; }, 0 },
        { "reserved bytes", [](PtsfxHeader& h) { h.reserved[1] = 1; }, 0 },
        { "no state", [](PtsfxHeader& h) { h.state_size = 0; }, 0 },
        { "state over 64 KB", [](PtsfxHeader& h) { h.state_size = 64 * 1024 + 1; }, 0 },
        { "code over 32 KB", [](PtsfxHeader& h) { h.code_size = 32 * 1024 + 4; }, 32 * 1024 + 4 },
        { "the code elsewhere", [](PtsfxHeader& h) { h.code_offset += 4; }, 4 },
        { "a file a byte short", [](PtsfxHeader&) {}, -1 },
        { "a file a byte long", [](PtsfxHeader&) {}, 1 },
        { "init past the code", [](PtsfxHeader& h) { h.init_offset = h.code_size; }, 0 },
        { "process on an odd byte", [](PtsfxHeader& h) { h.process_offset |= 1; }, 0 },
        { "reset missing", [](PtsfxHeader& h) { h.reset_offset = PTSFX_NO_ENTRY; }, 0 },
        { "format past the code", [](PtsfxHeader& h) { h.format_offset = h.code_size + 2; }, 0 },
        { "a knob with no label", [](PtsfxHeader& h) { h.params[1].label[0] = 0; }, 0 },
        { "a label without its 0", [](PtsfxHeader& h) { memset(h.params[0].label, 'x', 11); }, 0 },
        { "a unit without its 0", [](PtsfxHeader& h) { memset(h.params[3].unit, 'x', 4); }, 0 },
        { "a display type", [](PtsfxHeader& h) { h.params[2].display_type = 'x'; }, 0 },
        { "step 0", [](PtsfxHeader& h) { h.params[0].step = 0.0f; }, 0 },
        { "max under min", [](PtsfxHeader& h) { h.params[0].max_val = -1.0f; }, 0 },
        { "a NaN default", [](PtsfxHeader& h) { h.params[0].default_val = __builtin_nanf(""); }, 0 },
        { "an infinite max", [](PtsfxHeader& h) { h.params[0].max_val = __builtin_inff(); }, 0 },
        { "not whole steps", [](PtsfxHeader& h) { h.params[0].max_val = 100.5f; }, 0 },
        { "too many steps", [](PtsfxHeader& h) { h.params[0].step = 0.0001f; }, 0 },
        { "default between steps", [](PtsfxHeader& h) { h.params[0].default_val = 50.5f; }, 0 },
        { "default past max", [](PtsfxHeader& h) { h.params[0].default_val = 101.0f; }, 0 },
    };
    for (const Plant& p : plants)
    {
        PtsfxHeader h = good;
        p.spoil(h);
        const char* why = nullptr;
        const bool ok = plugins::checkHeader(h, static_cast<uint32_t>(static_cast<int>(f.size) + p.sizeChange), &why);
        check(!ok && why != nullptr, std::string("header refused: ") + p.what);
    }
    PtsfxHeader h = good;
    h.format_offset = PTSFX_NO_ENTRY;
    check(plugins::checkHeader(h, f.size, nullptr), "header: format() is optional");
    h = good;
    h.num_params = 0;
    check(plugins::checkHeader(h, f.size, nullptr), "header: no knobs is fine");
    h = good;
    h.params[0] = { "Mode", PTSFX_PARAM_LIST, 0.0f, 3.0f, 2.0f, 1.0f, "" };
    check(plugins::checkHeader(h, f.size, nullptr), "header: a list knob with no unit");
    h.params[0] = { "Fine", PTSFX_PARAM_FLOAT, -1.0f, 1.0f, 0.1f, 0.1f, "st" };
    check(plugins::checkHeader(h, f.size, nullptr), "header: tenth steps, worked out in single precision");
}

// ---- the start and the catalog ------------------------------------------------------------

class FakeCard : public FileIo
{
  public:
    size_t read(const char* path, uint8_t* buffer, size_t capacity) override
    {
        reads++;
        const auto it = files.find(path);
        if (it == files.end() || failReads)
        {
            return kReadFailed;
        }
        const size_t n = std::min(it->second.size(), capacity);
        memcpy(buffer, it->second.data(), n);
        return it->second.size();
    }
    bool write(const char* path, const uint8_t* data, size_t length) override
    {
        writes.push_back(path);
        files[path] = std::vector<uint8_t>(data, data + length);
        return true;
    }
    void remove(const char* path) override
    {
        removed.push_back(path);
        files.erase(path);
    }
    void makeDir(const char* path) override { dirs.push_back(path); }
    bool exists(const char* path) override { return files.count(path) != 0; }
    size_t list(const char* dir, const char* suffix, char* names, size_t count, size_t nameSize) override
    {
        std::vector<std::string> found;
        const std::string prefix = std::string(dir) + "/";
        for (const auto& [path, data] : files)
        {
            if (path.compare(0, prefix.size(), prefix) != 0)
            {
                continue;
            }
            const std::string name = path.substr(prefix.size());
            const size_t s = strlen(suffix);
            if (name.find('/') == std::string::npos && name.size() > s &&
                strcasecmp(name.c_str() + name.size() - s, suffix) == 0 && name.size() < nameSize)
            {
                found.push_back(name);
            }
        }
        std::sort(found.begin(), found.end());
        const size_t n = std::min(found.size(), count);
        for (size_t i = 0; i < n; i++)
        {
            snprintf(names + i * nameSize, nameSize, "%s", found[i].c_str());
        }
        return n;
    }

    std::map<std::string, std::vector<uint8_t>> files;
    std::vector<std::string> writes, removed, dirs;
    bool failReads = false;
    int reads = 0;
};

uint32_t nowMs = 0;
uint32_t clockMs()
{
    return nowMs;
}

alignas(32) uint8_t codeMemory[plugins::kCodePoolSize];
alignas(32) uint8_t stateMemory[plugins::kStatePoolSize];

plugins::Env testEnv()
{
    plugins::Env e = {};
    e.codePool = codeMemory;
    e.statePool = stateMemory;
    e.millis = clockMs;
    return e;
}

std::vector<uint8_t> packBytes(size_t i)
{
    const plugins::PackFile& f = plugins::packAt(i);
    return std::vector<uint8_t>(f.data, f.data + f.size);
}

// A catalog entry, or an empty one if there is none (a broken catalog fails checks, not the test)
const plugins::CatalogEntry& entryAt(size_t i)
{
    static const plugins::CatalogEntry none = {};
    const plugins::CatalogEntry* e = plugins::catalogAt(i);
    return e != nullptr ? *e : none;
}

const plugins::CatalogEntry& entryOf(const char* file)
{
    const int i = plugins::catalogFind(file);
    return entryAt(i < 0 ? plugins::kCatalogSize : static_cast<size_t>(i));
}

std::vector<std::string> catalogFiles()
{
    std::vector<std::string> out;
    for (size_t i = 0; i < plugins::catalogCount(); i++)
    {
        out.push_back(plugins::catalogAt(i)->file);
    }
    return out;
}

void testStart()
{
    {
        FakeCard card;
        plugins::forget();
        check(plugins::create("Chorus.ptsfx", false, nullptr) == -1, "start: nothing made before it");
        plugins::start(card, testEnv());
        check(plugins::started(), "start: started");
        check(card.dirs.size() == 1 && card.dirs[0] == "/SynthFX", "start: the folder made");
        check(card.writes.size() == 6, "start: the pack written to an empty card");
        bool same = true;
        for (size_t i = 0; i < plugins::packCount(); i++)
        {
            same = same && card.files[std::string("/SynthFX/") + plugins::packAt(i).name] == packBytes(i);
        }
        check(same, "start: each file as the firmware has it");
        const std::vector<std::string> pack = { "Overdrive.ptsfx", "Distortion.ptsfx", "BitCrusher.ptsfx",
                                                "Chorus.ptsfx",    "Flanger.ptsfx",    "Phaser.ptsfx" };
        check(catalogFiles() == pack, "catalog: the pack, in its order");
        bool allOk = true;
        for (size_t i = 0; i < plugins::catalogCount(); i++)
        {
            allOk = allOk && entryAt(i).status == plugins::Status::ok;
        }
        check(allOk, "catalog: every pack file usable");
        const plugins::CatalogEntry& chorus = entryAt(3);
        check(strcmp(chorus.name, "Chorus") == 0 && chorus.flags == PTSFX_FLAG_WIDENS && chorus.knobs == 3 &&
                chorus.size == plugins::packAt(3).size,
              "catalog: the header's name, flags, knobs and the size");
        check(!plugins::safeMode(), "start: no marker, no safe mode");
        check(plugins::catalogAt(6) == nullptr && plugins::catalogFind("Nope.ptsfx") == -1 &&
                plugins::catalogFind("Phaser.ptsfx") == 5,
              "catalog: lookups");

        // Started again: only the list
        card.writes.clear();
        plugins::start(card, testEnv());
        check(card.writes.empty(), "start again: nothing written");
    }
    {
        // Some of the pack there, one changed by the user: left as it is
        FakeCard card;
        std::vector<uint8_t> mine = packBytes(1);
        strcpy(reinterpret_cast<char*>(mine.data()) + 8, "My Distortion");
        card.files["/SynthFX/Distortion.ptsfx"] = mine;
        card.files["/SynthFX/Chorus.ptsfx"] = packBytes(3);
        plugins::forget();
        plugins::start(card, testEnv());
        check(card.writes.size() == 4, "start: only the missing pack files written");
        check(card.files["/SynthFX/Distortion.ptsfx"] == mine, "start: a file of the same name left alone");
        check(strcmp(entryAt(1).name, "My Distortion") == 0, "catalog: its own name");
    }
    {
        // The user's own plugins, a bad file, a file not a plugin, the case of the suffix
        FakeCard card;
        std::vector<uint8_t> tremolo = packBytes(0);
        strcpy(reinterpret_cast<char*>(tremolo.data()) + 8, "Tremolo");
        card.files["/SynthFX/Tremolo.PTSFX"] = tremolo;
        card.files["/SynthFX/Autopan.ptsfx"] = tremolo;
        std::vector<uint8_t> bad = packBytes(0);
        bad[4] = 9; // version
        card.files["/SynthFX/Broken.ptsfx"] = bad;
        std::vector<uint8_t> big = packBytes(0);
        PtsfxHeader h;
        memcpy(&h, big.data(), sizeof(h));
        h.state_size = 1u << 20;
        memcpy(big.data(), &h, sizeof(h));
        card.files["/SynthFX/Huge.ptsfx"] = big;
        card.files["/SynthFX/Short.ptsfx"] = std::vector<uint8_t>(100, 0);
        card.files["/SynthFX/readme.txt"] = std::vector<uint8_t>(10, 'x');
        card.files["/SynthFX/sub/Deep.ptsfx"] = tremolo;
        plugins::forget();
        plugins::start(card, testEnv());
        const std::vector<std::string> want = { "Overdrive.ptsfx", "Distortion.ptsfx", "BitCrusher.ptsfx",
                                                "Chorus.ptsfx",    "Flanger.ptsfx",    "Phaser.ptsfx",
                                                "Autopan.ptsfx",   "Broken.ptsfx",     "Huge.ptsfx",
                                                "Short.ptsfx",     "Tremolo.PTSFX" };
        check(catalogFiles() == want, "catalog: the pack, then the rest sorted, any case of .ptsfx, no subfolders");
        const auto status = [](const char* file) { return entryOf(file).status; };
        check(status("Tremolo.PTSFX") == plugins::Status::ok && status("Autopan.ptsfx") == plugins::Status::ok,
              "catalog: the user's plugins usable");
        check(status("Broken.ptsfx") == plugins::Status::badFile, "catalog: another version: bad file");
        check(status("Huge.ptsfx") == plugins::Status::tooBig, "catalog: too much state: too big");
        check(status("Short.ptsfx") == plugins::Status::badFile, "catalog: shorter than a header: bad file");
        check(strcmp(entryOf("Broken.ptsfx").name, "Broken.ptsfx") == 0,
              "catalog: a bad file shows its file name");

        Status s = Status::ok;
        check(plugins::create("Broken.ptsfx", false, &s) == -1 && s == plugins::Status::badFile,
              "create: a bad file refused before loading");
        check(plugins::create("Gone.ptsfx", false, &s) == -1 && s == plugins::Status::missing,
              "create: a file not listed");
        check(plugins::loaded() == 0 && plugins::instances() == 0, "create: nothing left loaded");

        // A file changed since the list: refused when loading
        card.files["/SynthFX/Tremolo.PTSFX"].push_back(0);
        check(plugins::create("Tremolo.PTSFX", false, &s) == -1 && s == plugins::Status::badFile,
              "create: a file changed since the list refused");
        card.files.erase("/SynthFX/Autopan.ptsfx");
        check(plugins::create("Autopan.ptsfx", false, &s) == -1 && s == plugins::Status::readFailed,
              "create: a file gone since the list");
        check(plugins::loaded() == 0, "create: the code pool given back");
    }
    {
        // 70 files: the list holds 64
        FakeCard card;
        for (int i = 0; i < 70; i++)
        {
            char name[48];
            snprintf(name, sizeof(name), "/SynthFX/P%02d.ptsfx", i);
            card.files[name] = packBytes(2);
        }
        plugins::forget();
        plugins::start(card, testEnv());
        check(plugins::catalogCount() == plugins::kCatalogSize, "catalog: full at 64");
    }
}

void testMarker()
{
    FakeCard card;
    card.files["/SynthFX/.starting"] = { '1' };
    nowMs = 1000;
    plugins::forget();
    plugins::start(card, testEnv());
    check(plugins::safeMode(), "marker: found at start: safe mode");
    Status s = Status::ok;
    check(plugins::create("Chorus.ptsfx", true, &s) == -1 && s == Status::skipped,
          "safe mode: a project's plugin skipped");
    check(plugins::loaded() == 0, "safe mode: nothing loaded for it");
    nowMs += plugins::kSettleMs * 3;
    plugins::update();
    check(plugins::safeMode() && card.files.count("/SynthFX/.starting") == 1,
          "safe mode: lasts until the user picks a plugin, however long");
}

// ---- the firmware's ptsfx.h is the SDK's --------------------------------------------------

void testHeaderCopy()
{
    std::ifstream a("../synthfx/ptsfx.h", std::ios::binary), b("../../../synthfx-sdk/ptsfx.h", std::ios::binary);
    const std::string ta((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
    const std::string tb((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
    const auto strip = [](std::string t) {
        t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());
        return t;
    };
    check(!ta.empty() && strip(ta) == strip(tb), "ptsfx.h: the firmware's copy is the SDK's");
}

} // namespace

int main()
{
    testPool();
    testHeader();
    testStart();
    testMarker();
    testHeaderCopy();
    printf("synthfx plugins: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
