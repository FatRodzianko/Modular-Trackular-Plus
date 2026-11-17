// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the Master FX plugins' runtime, the parts that don't run a plugin's code: the
// header check, the default pack, the start (the folder, the pack written where it is missing,
// the start marker), the catalog and each slot's list, read() and its refusals, safe mode, and
// the firmware's copy of ptmfx.h. A plugin's code is Arm: install(), the knobs, process() and the
// guards are tested on the emulated Cortex-M7 (tools/masterfx/). Build and run with
// `make -C core/mods/tests`.

#include "masterfx/masterfxPlugins.h"

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
using namespace mods::masterfx;
using Status = mods::masterfx::plugins::Status;

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

PtmfxHeader headerOf(const plugins::PackFile& f)
{
    PtmfxHeader h;
    memcpy(&h, f.data, sizeof(h));
    return h;
}

// ---- the header and the pack --------------------------------------------------------------

struct PackWant
{
    const char* file;
    const char* name;
    uint8_t slot;
    uint16_t flags;
    uint8_t knobs;
};

// masterfx-1.x's effects (Q15, CE's knobs only), then Glue (float, a bus, its own Attack, Release
// and Threshold, Ratio and Makeup) and Compressor (masterfx-2.0's Glue: float, can duck tracks,
// Ratio and Makeup)
const PackWant kPackWant[] = {
    { "Freeverb.ptmfx", "Freeverb", PTMFX_SLOT_REVERB, 0, 0 }, { "Plate.ptmfx", "Plate", PTMFX_SLOT_REVERB, 0, 0 },
    { "Spring.ptmfx", "Spring", PTMFX_SLOT_REVERB, 0, 0 },     { "Shimmer.ptmfx", "Shimmer", PTMFX_SLOT_REVERB, 0, 0 },
    { "Gated.ptmfx", "\"Gated\"", PTMFX_SLOT_REVERB, 0, 0 },   { "Reverse.ptmfx", "Reverse", PTMFX_SLOT_REVERB, 0, 0 },
    { "Tape.ptmfx", "Tape", PTMFX_SLOT_DELAY, 0, 0 },          { "BBD.ptmfx", "BBD", PTMFX_SLOT_DELAY, 0, 0 },
    { "Glue.ptmfx", "Glue", PTMFX_SLOT_DYNAMICS, PTMFX_FLAG_FLOAT | PTMFX_FLAG_OWN_DYNAMICS | PTMFX_FLAG_BUS, 5 },
    { "Compressor.ptmfx", "Compressor", PTMFX_SLOT_DYNAMICS, PTMFX_FLAG_FLOAT | PTMFX_FLAG_DUCK, 2 },
};
constexpr size_t kPackSize = sizeof(kPackWant) / sizeof(kPackWant[0]);

void testHeader()
{
    using plugins::checkHeader;
    check(plugins::packCount() == kPackSize, "pack: ten files");
    for (size_t i = 0; i < plugins::packCount() && i < kPackSize; i++)
    {
        const plugins::PackFile& f = plugins::packAt(i);
        const PtmfxHeader h = headerOf(f);
        const char* why = "unset";
        check(checkHeader(h, f.size, &why) && why == nullptr, std::string("pack: passes the check: ") + f.name);
        check(strcmp(f.name, kPackWant[i].file) == 0, std::string("pack: in masterfx-1.x's order: ") + f.name);
        check(h.format_version == PTMFX_VERSION && h.code_offset == sizeof(PtmfxHeader),
              std::string("pack: format version 2: ") + f.name);
        check(strcmp(h.name, kPackWant[i].name) == 0, std::string("pack: its name: ") + f.name);
        check(h.slot == kPackWant[i].slot, std::string("pack: its slot: ") + f.name);
        check(h.flags == kPackWant[i].flags && h.num_params == kPackWant[i].knobs,
              std::string("pack: its blocks and own knobs: ") + f.name);
    }

    // A header with one knob, to spoil
    const plugins::PackFile& f = plugins::packAt(0);
    PtmfxHeader good = headerOf(f);
    good.num_params = 1;
    good.params[0] = { "Mix", PTMFX_PARAM_INT, 0.0f, 100.0f, 50.0f, 1.0f, "%" };
    check(checkHeader(good, f.size, nullptr), "header: an own knob is fine");
    struct Plant
    {
        const char* what;
        void (*spoil)(PtmfxHeader&);
        int sizeChange;
    };
    const Plant plants[] = {
        { "magic", [](PtmfxHeader& h) { h.magic ^= 1; }, 0 },
        { "a .ptsfx's magic", [](PtmfxHeader& h) { h.magic = 0x58465350u; }, 0 },
        { "version 3", [](PtmfxHeader& h) { h.format_version = 3; }, 0 },
        { "version 0", [](PtmfxHeader& h) { h.format_version = 0; }, 0 },
        { "an unknown flag", [](PtmfxHeader& h) { h.flags |= 0x0010; }, 0 },
        { "a reverb on a bus", [](PtmfxHeader& h) { h.flags = PTMFX_FLAG_FLOAT | PTMFX_FLAG_BUS; }, 0 },
        { "a Q15 bus",
          [](PtmfxHeader& h) { h.slot = PTMFX_SLOT_DYNAMICS; h.state_size = 64; h.flags = PTMFX_FLAG_BUS; }, 0 },
        { "a bus that ducks",
          [](PtmfxHeader& h) {
              h.slot = PTMFX_SLOT_DYNAMICS;
              h.state_size = 64;
              h.flags = PTMFX_FLAG_FLOAT | PTMFX_FLAG_BUS | PTMFX_FLAG_DUCK;
          },
          0 },
        { "a reverb with own dynamics",
          [](PtmfxHeader& h) {
              h.flags = PTMFX_FLAG_OWN_DYNAMICS;
              h.num_params = 3;
              h.params[1] = h.params[0];
              h.params[2] = h.params[0];
          },
          0 },
        { "own dynamics with 2 knobs",
          [](PtmfxHeader& h) {
              h.slot = PTMFX_SLOT_DYNAMICS;
              h.state_size = 64;
              h.flags = PTMFX_FLAG_OWN_DYNAMICS;
              h.num_params = 2;
              h.params[1] = h.params[0];
          },
          0 },
        { "a reverb that ducks", [](PtmfxHeader& h) { h.flags = PTMFX_FLAG_FLOAT | PTMFX_FLAG_DUCK; }, 0 },
        { "a Q15 dynamics plugin that ducks",
          [](PtmfxHeader& h) { h.slot = PTMFX_SLOT_DYNAMICS; h.state_size = 64; h.flags = PTMFX_FLAG_DUCK; }, 0 },
        { "slot 0", [](PtmfxHeader& h) { h.slot = 0; }, 0 },
        { "slot 4", [](PtmfxHeader& h) { h.slot = 4; }, 0 },
        { "an empty name", [](PtmfxHeader& h) { h.name[0] = 0; }, 0 },
        { "a name without its 0", [](PtmfxHeader& h) { memset(h.name, 'x', sizeof(h.name)); }, 0 },
        { "an author without its 0", [](PtmfxHeader& h) { memset(h.author, 'x', sizeof(h.author)); }, 0 },
        { "3 knobs without own dynamics",
          [](PtmfxHeader& h) {
              h.num_params = 3;
              h.params[1] = h.params[0];
              h.params[2] = h.params[0];
          },
          0 },
        { "6 knobs",
          [](PtmfxHeader& h) {
              h.slot = PTMFX_SLOT_DYNAMICS;
              h.state_size = 64;
              h.flags = PTMFX_FLAG_OWN_DYNAMICS;
              h.num_params = 6;
          },
          0 },
        { "reserved bytes", [](PtmfxHeader& h) { h.reserved[1] = 1; }, 0 },
        { "reserved words", [](PtmfxHeader& h) { h.reserved2[2] = 1; }, 0 },
        { "no state", [](PtmfxHeader& h) { h.state_size = 0; }, 0 },
        { "state over the reverb's 1 KB", [](PtmfxHeader& h) { h.state_size = PTMFX_STATE_REVERB + 1; }, 0 },
        { "a delay's state over 512", [](PtmfxHeader& h) { h.slot = PTMFX_SLOT_DELAY; h.state_size = 513; }, 0 },
        { "code over 8 KB", [](PtmfxHeader& h) { h.code_size = PTMFX_MAX_CODE + 4; }, PTMFX_MAX_CODE + 4 },
        { "the code elsewhere", [](PtmfxHeader& h) { h.code_offset += 4; }, 4 },
        { "a file a byte short", [](PtmfxHeader&) {}, -1 },
        { "a file a byte long", [](PtmfxHeader&) {}, 1 },
        { "init past the code", [](PtmfxHeader& h) { h.init_offset = h.code_size; }, 0 },
        { "process on an odd byte", [](PtmfxHeader& h) { h.process_offset |= 1; }, 0 },
        { "reset missing", [](PtmfxHeader& h) { h.reset_offset = PTMFX_NO_ENTRY; }, 0 },
        { "format past the code", [](PtmfxHeader& h) { h.format_offset = h.code_size + 2; }, 0 },
        { "a label without its 0", [](PtmfxHeader& h) { memset(h.labels[1], 'x', 12); }, 0 },
        { "a reverb with a fifth label", [](PtmfxHeader& h) { strcpy(h.labels[4], "Mix"); }, 0 },
        { "a dynamics plugin with a fourth label",
          [](PtmfxHeader& h) { h.slot = PTMFX_SLOT_DYNAMICS; h.state_size = 64; strcpy(h.labels[3], "Mix"); }, 0 },
        { "a knob with no label", [](PtmfxHeader& h) { h.params[0].label[0] = 0; }, 0 },
        { "a display type", [](PtmfxHeader& h) { h.params[0].display_type = 'x'; }, 0 },
        { "step 0", [](PtmfxHeader& h) { h.params[0].step = 0.0f; }, 0 },
        { "max under min", [](PtmfxHeader& h) { h.params[0].max_val = -1.0f; }, 0 },
        { "a NaN default", [](PtmfxHeader& h) { h.params[0].default_val = __builtin_nanf(""); }, 0 },
        { "not whole steps", [](PtmfxHeader& h) { h.params[0].max_val = 100.5f; }, 0 },
        { "default between steps", [](PtmfxHeader& h) { h.params[0].default_val = 50.5f; }, 0 },
    };
    for (const Plant& p : plants)
    {
        PtmfxHeader h = good;
        p.spoil(h);
        const char* why = nullptr;
        const bool ok = checkHeader(h, static_cast<uint32_t>(static_cast<int>(f.size) + p.sizeChange), &why);
        check(!ok && why != nullptr, std::string("header refused: ") + p.what);
    }
    PtmfxHeader h = good;
    h.format_offset = PTMFX_NO_ENTRY;
    check(checkHeader(h, f.size, nullptr), "header: format() is optional");
    h = good;
    h.flags = PTMFX_FLAG_FLOAT;
    check(checkHeader(h, f.size, nullptr), "header: float blocks");
    h = good;
    h.slot = PTMFX_SLOT_DELAY;
    h.state_size = PTMFX_STATE_DELAY;
    strcpy(h.labels[5], "Tone");
    check(checkHeader(h, f.size, nullptr), "header: a delay may rename all six of CE's knobs");
    h = good;
    h.slot = PTMFX_SLOT_DYNAMICS;
    h.state_size = PTMFX_STATE_DYNAMICS;
    h.num_params = 2;
    h.params[1] = { "Ratio", PTMFX_PARAM_FLOAT, 1.0f, 20.0f, 4.0f, 0.5f, ":1" };
    check(checkHeader(h, f.size, nullptr), "header: a dynamics plugin with two own knobs");
    h.flags = PTMFX_FLAG_FLOAT | PTMFX_FLAG_DUCK;
    check(checkHeader(h, f.size, nullptr), "header: a float dynamics plugin that ducks");
    h.flags = PTMFX_FLAG_FLOAT | PTMFX_FLAG_BUS | PTMFX_FLAG_OWN_DYNAMICS;
    h.num_params = 5;
    for (uint32_t k = 2; k < 5; k++)
    {
        h.params[k] = h.params[1];
    }
    check(checkHeader(h, f.size, nullptr), "header: a float bus with own dynamics and five knobs");
    h.flags = PTMFX_FLAG_OWN_DYNAMICS;
    h.num_params = 3;
    check(checkHeader(h, f.size, nullptr), "header: own dynamics with three knobs, Q15, no bus");

    // Format version 1 (masterfx-2.0's files): a 240-byte header with two knobs, then the code
    PtmfxHeader v1 = good;
    v1.format_version = 1;
    v1.code_offset = PTMFX_V1_HEADER;
    const uint32_t v1Size = PTMFX_V1_HEADER + v1.code_size;
    check(plugins::headerSize(v1) == PTMFX_V1_HEADER && plugins::headerSize(good) == sizeof(PtmfxHeader),
          "header: where version 1's and 2's code starts");
    check(checkHeader(v1, v1Size, nullptr), "header: version 1 loads");
    PtmfxHeader bad1 = v1;
    bad1.code_offset = sizeof(PtmfxHeader);
    check(!checkHeader(bad1, sizeof(PtmfxHeader) + v1.code_size, nullptr),
          "header: version 1 with version 2's offset refused");
    bad1 = v1;
    bad1.slot = PTMFX_SLOT_DYNAMICS;
    bad1.state_size = 64;
    bad1.flags = PTMFX_FLAG_FLOAT | PTMFX_FLAG_BUS;
    check(!checkHeader(bad1, v1Size, nullptr), "header: version 1 with version 2's flags refused");
    bad1 = v1;
    bad1.num_params = 3;
    check(!checkHeader(bad1, v1Size, nullptr), "header: version 1 with 3 knobs refused");
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
    bool rename(const char* from, const char* to) override
    {
        if (!canRename || files.count(from) == 0 || files.count(to) != 0)
        {
            return false;
        }
        renames.push_back(std::string(from) + ">" + to);
        files[to] = files[from];
        files.erase(from);
        return true;
    }
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
    std::vector<std::string> writes, removed, dirs, renames;
    bool failReads = false;
    bool canRename = true;
    int reads = 0;
};

// A card whose writes of a .new file come out a byte short (as a full card would)
class ShortCard : public FakeCard
{
  public:
    bool write(const char* path, const uint8_t* data, size_t length) override
    {
        const std::string p(path);
        const bool fresh = p.size() > 4 && p.compare(p.size() - 4, 4, ".new") == 0;
        return FakeCard::write(path, data, fresh && length > 0 ? length - 1 : length);
    }
};

uint32_t nowMs = 0;
uint32_t clockMs()
{
    return nowMs;
}

alignas(4) uint8_t loadMemory[plugins::kMaxFile];

plugins::Env testEnv()
{
    plugins::Env e = {};
    e.loadBuffer = loadMemory;
    e.millis = clockMs;
    return e;
}

std::vector<uint8_t> packBytes(size_t i)
{
    const plugins::PackFile& f = plugins::packAt(i);
    return std::vector<uint8_t>(f.data, f.data + f.size);
}

std::vector<uint8_t> renamed(size_t i, const char* name)
{
    std::vector<uint8_t> b = packBytes(i);
    char* n = reinterpret_cast<char*>(b.data()) + offsetof(PtmfxHeader, name);
    memset(n, 0, 32);
    strncpy(n, name, 31);
    return b;
}

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

std::vector<std::string> slotFiles(plugins::Slot slot)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < plugins::slotCount(slot); i++)
    {
        out.push_back(plugins::slotAt(slot, i)->file);
    }
    return out;
}

void testStart()
{
    {
        FakeCard card;
        plugins::forget();
        check(plugins::read(plugins::kReverb, "Plate.ptmfx", false) == Status::missing, "start: nothing read before it");
        plugins::start(card, testEnv());
        check(plugins::started(), "start: started");
        check(card.dirs.size() == 1 && card.dirs[0] == "/MasterFX", "start: the folder made");
        check(card.writes.size() == kPackSize, "start: the pack written to an empty card");
        bool same = true;
        for (size_t i = 0; i < plugins::packCount(); i++)
        {
            same = same && card.files[std::string("/MasterFX/") + plugins::packAt(i).name] == packBytes(i);
        }
        check(same, "start: each file as the firmware has it");
        std::vector<std::string> pack;
        for (const PackWant& w : kPackWant)
        {
            pack.push_back(w.file);
        }
        check(catalogFiles() == pack, "catalog: the pack, in its order");
        bool allOk = true;
        for (size_t i = 0; i < plugins::catalogCount(); i++)
        {
            allOk = allOk && entryAt(i).status == Status::ok;
        }
        check(allOk, "catalog: every pack file usable");
        check(slotFiles(plugins::kReverb) ==
                std::vector<std::string>(pack.begin(), pack.begin() + 6),
              "the reverb's list: the six reverbs");
        check(slotFiles(plugins::kDelay) == std::vector<std::string>({ "Tape.ptmfx", "BBD.ptmfx" }),
              "the delay's list: Tape and BBD");
        check(slotFiles(plugins::kDynamics) == std::vector<std::string>({ "Glue.ptmfx", "Compressor.ptmfx" }),
              "the dynamics list: Glue, then Compressor");
        const plugins::CatalogEntry* comp = plugins::slotAt(plugins::kDynamics, 1);
        check(comp != nullptr && std::string(comp->name) == "Compressor" &&
                std::string(comp->name + strlen(comp->name) + 1) == "Comp",
              "Compressor: its name, then its short name Comp for the Master page's column");
        const plugins::CatalogEntry* glue = plugins::slotAt(plugins::kDynamics, 0);
        check(glue != nullptr && glue->name[strlen(glue->name) + 1] == 0, "Glue: no short name");
        check(plugins::slotAt(plugins::kDelay, 2) == nullptr, "a list's end");
        check(!plugins::safeMode(), "start: no marker, no safe mode");
        check(plugins::catalogFind("Nope.ptmfx") == -1 && plugins::catalogFind("BBD.ptmfx") == 7,
              "catalog: lookups");

        card.writes.clear();
        plugins::start(card, testEnv());
        check(card.writes.empty(), "start again: nothing written");

        // read(): into the load buffer, checked again
        memset(loadMemory, 0, sizeof(loadMemory));
        check(plugins::read(plugins::kReverb, "Plate.ptmfx", true) == Status::ok, "read: a reverb into the reverb");
        check(memcmp(loadMemory, plugins::packAt(1).data, plugins::packAt(1).size) == 0, "read: the whole file");
        check(plugins::read(plugins::kDelay, "Plate.ptmfx", false) == Status::wrongSlot, "read: a reverb into the delay");
        check(plugins::read(plugins::kReverb, "Gone.ptmfx", false) == Status::missing, "read: a file not listed");
        check(plugins::install(plugins::kReverb) == Status::notRead, "install: only after a read that worked");
        card.files["/MasterFX/Tape.ptmfx"].push_back(0);
        check(plugins::read(plugins::kDelay, "Tape.ptmfx", false) == Status::badFile, "read: changed since the list");
        card.failReads = true;
        check(plugins::read(plugins::kDelay, "BBD.ptmfx", false) == Status::readFailed, "read: the card fails");
        card.failReads = false;
        check(plugins::installed(plugins::kReverb) == false && plugins::file(plugins::kReverb)[0] == 0,
              "nothing installed by reading");
        check(plugins::label(plugins::kReverb, 0) == nullptr || plugins::label(plugins::kReverb, 0)[0] == 0,
              "no plugin: no label of its own");
    }
    {
        // Some of the pack there, one changed by the user: left as it is
        FakeCard card;
        std::vector<uint8_t> mine = renamed(1, "My Plate");
        card.files["/MasterFX/Plate.ptmfx"] = mine;
        card.files["/MasterFX/Tape.ptmfx"] = packBytes(6);
        plugins::forget();
        plugins::start(card, testEnv());
        check(card.writes.size() == kPackSize - 2, "start: only the missing pack files written");
        check(card.files["/MasterFX/Plate.ptmfx"] == mine, "start: a file of the same name left alone");
        check(strcmp(entryAt(1).name, "My Plate") == 0, "catalog: its own name");
    }
    {
        // The user's own plugins, bad files, other files, the case of the suffix
        FakeCard card;
        card.files["/MasterFX/Hall.PTMFX"] = renamed(0, "Hall");
        card.files["/MasterFX/Echo.ptmfx"] = renamed(6, "Echo");
        std::vector<uint8_t> bad = packBytes(0);
        bad[4] = 9; // version
        card.files["/MasterFX/Broken.ptmfx"] = bad;
        std::vector<uint8_t> big = packBytes(0);
        PtmfxHeader h;
        memcpy(&h, big.data(), sizeof(h));
        h.state_size = 1u << 20;
        memcpy(big.data(), &h, sizeof(h));
        card.files["/MasterFX/Huge.ptmfx"] = big;
        card.files["/MasterFX/Short.ptmfx"] = std::vector<uint8_t>(100, 0);
        card.files["/MasterFX/readme.txt"] = std::vector<uint8_t>(10, 'x');
        card.files["/MasterFX/Chorus.ptsfx"] = packBytes(0);
        card.files["/MasterFX/sub/Deep.ptmfx"] = packBytes(0);
        plugins::forget();
        plugins::start(card, testEnv());
        std::vector<std::string> want;
        for (const PackWant& w : kPackWant)
        {
            want.push_back(w.file);
        }
        for (const char* f : { "Broken.ptmfx", "Echo.ptmfx", "Hall.PTMFX", "Huge.ptmfx", "Short.ptmfx" })
        {
            want.push_back(f);
        }
        check(catalogFiles() == want, "catalog: the pack, then the rest sorted, any case of .ptmfx, no subfolders");
        const auto status = [](const char* file) { return entryOf(file).status; };
        check(status("Hall.PTMFX") == Status::ok && status("Echo.ptmfx") == Status::ok, "catalog: the user's plugins");
        check(status("Broken.ptmfx") == Status::badFile, "catalog: another version: bad file");
        check(status("Huge.ptmfx") == Status::tooBig, "catalog: too much state: too big");
        check(status("Short.ptmfx") == Status::badFile, "catalog: shorter than a header: bad file");
        check(strcmp(entryOf("Broken.ptmfx").name, "Broken.ptmfx") == 0, "catalog: a bad file shows its file name");
        check(plugins::slotCount(plugins::kReverb) == 7 && plugins::slotCount(plugins::kDelay) == 3,
              "the lists: only usable plugins, each in its slot");
        check(strcmp(plugins::slotAt(plugins::kReverb, 6)->file, "Hall.PTMFX") == 0 &&
                strcmp(plugins::slotAt(plugins::kDelay, 2)->file, "Echo.ptmfx") == 0,
              "the lists: the user's after the pack");
        check(plugins::read(plugins::kReverb, "Broken.ptmfx", false) == Status::badFile, "read: a bad file refused");
        check(plugins::read(plugins::kReverb, "Huge.ptmfx", false) == Status::tooBig, "read: too big refused");
    }
    {
        // 70 files: the list holds 64
        FakeCard card;
        for (int i = 0; i < 70; i++)
        {
            char name[48];
            snprintf(name, sizeof(name), "/MasterFX/P%02d.ptmfx", i);
            card.files[name] = packBytes(2);
        }
        plugins::forget();
        plugins::start(card, testEnv());
        check(plugins::catalogCount() == plugins::kCatalogSize, "catalog: full at 64");
    }
}

// The pack over files a release shipped: replaced when exactly one, else left
void testReplace()
{
    const size_t glue = 8; // Glue.ptmfx in the pack
    check(strcmp(plugins::packAt(glue).name, "Glue.ptmfx") == 0, "replace: Glue's place in the pack");
    const std::vector<uint8_t> old = renamed(9, "Old Glue"); // stands in for masterfx-2.0's Glue
    const plugins::ShippedFile shipped[] = {
        { "Glue.ptmfx", static_cast<uint32_t>(old.size()),
          plugins::fileHash(old.data(), static_cast<uint32_t>(old.size())) },
        { "Plate.ptmfx", 10, 0x12345678u },
    };
    plugins::shippedForTests(shipped, 2);
    const std::string path = "/MasterFX/Glue.ptmfx";
    const std::string fresh = path + ".new";
    {
        FakeCard card;
        card.files[path] = old;
        plugins::forget();
        plugins::start(card, testEnv());
        check(card.files[path] == packBytes(glue), "replace: a shipped file becomes the pack's");
        check(card.files.count(fresh) == 0, "replace: no .new left");
        check(std::find(card.writes.begin(), card.writes.end(), fresh) != card.writes.end() &&
                std::find(card.writes.begin(), card.writes.end(), path) == card.writes.end(),
              "replace: written beside it, not over it");
        check(card.renames == std::vector<std::string>({ fresh + ">" + path }), "replace: then renamed in place");
        check(entryOf("Glue.ptmfx").status == Status::ok && strcmp(entryOf("Glue.ptmfx").name, "Glue") == 0,
              "replace: listed as the new Glue");
        card.writes.clear();
        card.renames.clear();
        plugins::forget();
        plugins::start(card, testEnv());
        check(card.writes.empty() && card.renames.empty(), "replace: the next start leaves it");
    }
    {
        // A release that kept a file as it was: shipped before, and this pack's too. Left as it is,
        // not written again at every start
        const std::vector<uint8_t> same = packBytes(glue);
        const plugins::ShippedFile kept[] = {
            { "Glue.ptmfx", static_cast<uint32_t>(same.size()),
              plugins::fileHash(same.data(), static_cast<uint32_t>(same.size())) },
        };
        plugins::shippedForTests(kept, 1);
        FakeCard card;
        card.files[path] = same;
        plugins::forget();
        plugins::start(card, testEnv());
        check(card.files[path] == same && card.renames.empty() &&
                std::find(card.writes.begin(), card.writes.end(), fresh) == card.writes.end() &&
                std::find(card.writes.begin(), card.writes.end(), path) == card.writes.end(),
              "replace: a shipped file that is already the pack's isn't written again");
        // This pack's file with more after it: not the pack's, and not a shipped one either
        std::vector<uint8_t> longer = same;
        longer.push_back(0);
        card.files[path] = longer;
        plugins::forget();
        plugins::start(card, testEnv());
        check(card.files[path] == longer && card.renames.empty(), "replace: the pack's file with more after it left alone");
        plugins::shippedForTests(shipped, 2);
    }
    {
        // The same size, other bytes: the user's
        FakeCard card;
        std::vector<uint8_t> mine = old;
        mine[mine.size() - 1] ^= 1;
        card.files[path] = mine;
        plugins::forget();
        plugins::start(card, testEnv());
        check(card.files[path] == mine && card.renames.empty(), "replace: a changed file left alone");
    }
    {
        // A stop part way: the old one gone, the .new there
        FakeCard card;
        card.files[fresh] = std::vector<uint8_t>(100, 1);
        plugins::forget();
        plugins::start(card, testEnv());
        check(card.files.count(fresh) == 0 && card.files[path] == packBytes(glue),
              "replace: a stray .new removed, the missing file written");
    }
    {
        // A stop part way: the .new half written, the old one still there
        FakeCard card;
        card.files[fresh] = std::vector<uint8_t>(100, 1);
        card.files[path] = old;
        plugins::forget();
        plugins::start(card, testEnv());
        check(card.files.count(fresh) == 0 && card.files[path] == packBytes(glue),
              "replace: a stray .new removed, then the old file replaced");
    }
    {
        // A card that can't rename: written in place after the copy checked out
        FakeCard card;
        card.canRename = false;
        card.files[path] = old;
        plugins::forget();
        plugins::start(card, testEnv());
        check(card.files[path] == packBytes(glue) && card.files.count(fresh) == 0,
              "replace: without rename, in place");
    }
    {
        // The copy doesn't read back as written: the old one stays
        ShortCard card;
        card.files[path] = old;
        plugins::forget();
        plugins::start(card, testEnv());
        check(card.files[path] == old && card.files.count(fresh) == 0, "replace: a bad copy, the old file kept");
    }
    plugins::shippedForTests(nullptr, 0);

    // What the firmware ships: masterfx-2.0's nine files, each a pack file's name
    check(plugins::shippedCount() == 9, "shipped: masterfx-2.0's nine files");
    bool named = true;
    for (size_t i = 0; i < plugins::shippedCount(); i++)
    {
        const plugins::ShippedFile& s = plugins::shippedAt(i);
        bool found = false;
        for (size_t j = 0; j < plugins::packCount(); j++)
        {
            const plugins::PackFile& f = plugins::packAt(j);
            found = found || strcmp(s.name, f.name) == 0;
        }
        named = named && found;
    }
    check(named, "shipped: each is a pack file's name");
}

void testMarker()
{
    FakeCard card;
    card.files["/MasterFX/.starting"] = { '1' };
    nowMs = 1000;
    plugins::forget();
    plugins::start(card, testEnv());
    check(plugins::safeMode(), "marker: found at start: safe mode");
    check(plugins::read(plugins::kReverb, "Plate.ptmfx", true) == Status::skipped,
          "safe mode: a project's plugin skipped");
    check(plugins::read(plugins::kReverb, "Plate.ptmfx", false) == Status::ok, "safe mode: the user's pick is read");
    nowMs += plugins::kSettleMs * 3;
    plugins::update();
    check(plugins::safeMode() && card.files.count("/MasterFX/.starting") == 1,
          "safe mode: lasts until a plugin goes in, however long");
}

// ---- the firmware's ptmfx.h is the SDK's --------------------------------------------------

void testHeaderCopy()
{
    std::ifstream a("../masterfx/ptmfx.h", std::ios::binary), b("../../../masterfx-sdk/ptmfx.h", std::ios::binary);
    const std::string ta((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
    const std::string tb((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
    const auto strip = [](std::string t) {
        t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());
        return t;
    };
    check(!ta.empty() && strip(ta) == strip(tb), "ptmfx.h: the firmware's copy is the SDK's");
}

} // namespace

int main()
{
    testHeader();
    testStart();
    testReplace();
    testMarker();
    testHeaderCopy();
    printf("masterfx plugins: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
