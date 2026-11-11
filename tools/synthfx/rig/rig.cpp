// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The Synth FX plugins' rig: core/mods/synthfx/synthfxPlugins.cpp and the firmware's pack
// (synthfxPack.cpp) built as the firmware builds them, at the Tracker+'s addresses, with a
// card in memory and plain C entry points for tools/synthfx/test_runtime.py. The pools are in
// SDRAM as on the unit. The host table, the clock and the cache sync are the test's (stubs
// whose addresses it writes into rig_hooks before rig_start).

#include "synthfxPlugins.h"

#include <cr_section_macros.h>
#include <cstring>

using namespace mods;
using namespace mods::synthfx;

namespace
{

constexpr size_t kFiles = 96;
constexpr size_t kCardSize = 2u << 20;

struct CardFile
{
    char path[72];
    uint32_t offset; // in card
    uint32_t size;
    uint32_t used;
};

size_t length(const char* s)
{
    size_t n = 0;
    while (s[n] != 0)
    {
        n++;
    }
    return n;
}

char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool endsWith(const char* s, const char* suffix)
{
    const size_t a = length(s), b = length(suffix);
    if (a <= b)
    {
        return false;
    }
    for (size_t i = 0; i < b; i++)
    {
        if (lower(s[a - b + i]) != lower(suffix[i]))
        {
            return false;
        }
    }
    return true;
}

int compare(const char* a, const char* b)
{
    while (*a != 0 && *a == *b)
    {
        a++;
        b++;
    }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

} // namespace

extern "C" {

// The card, filled and read by the test
__NOINIT(EXTERNAL_RAM) CardFile rig_files[kFiles];
__NOINIT(EXTERNAL_RAM) alignas(4) uint8_t rig_card[kCardSize];
uint32_t rig_card_used;
uint32_t rig_writes, rig_removes, rig_dirs, rig_lists;
uint32_t rig_fail_writes;

// The test's stubs (Thumb addresses), and the budget
struct
{
    uint32_t syncCode, cycles, millis, host, budget;
} rig_hooks;

__NOINIT(EXTERNAL_RAM) alignas(32) uint8_t rig_code_pool[plugins::kCodePoolSize];
__NOINIT(EXTERNAL_RAM) alignas(32) uint8_t rig_state_pool[plugins::kStatePoolSize];

uint32_t rig_status;
}

namespace
{

CardFile* find(const char* path)
{
    for (CardFile& f : rig_files)
    {
        if (f.used && compare(f.path, path) == 0)
        {
            return &f;
        }
    }
    return nullptr;
}

class Card : public FileIo
{
  public:
    size_t read(const char* path, uint8_t* buffer, size_t capacity) override
    {
        const CardFile* f = find(path);
        if (f == nullptr)
        {
            return kReadFailed;
        }
        memcpy(buffer, rig_card + f->offset, f->size < capacity ? f->size : capacity);
        return f->size;
    }
    bool write(const char* path, const uint8_t* data, size_t size) override
    {
        rig_writes++;
        if (rig_fail_writes || rig_card_used + size > kCardSize || length(path) >= sizeof(CardFile::path))
        {
            return false;
        }
        CardFile* f = find(path);
        for (size_t i = 0; f == nullptr && i < kFiles; i++)
        {
            f = rig_files[i].used ? nullptr : &rig_files[i];
        }
        if (f == nullptr)
        {
            return false;
        }
        memcpy(f->path, path, length(path) + 1);
        f->offset = rig_card_used;
        f->size = size;
        f->used = 1;
        memcpy(rig_card + rig_card_used, data, size);
        rig_card_used += (size + 3u) & ~3u;
        return true;
    }
    void remove(const char* path) override
    {
        rig_removes++;
        CardFile* f = find(path);
        if (f != nullptr)
        {
            f->used = 0;
        }
    }
    void makeDir(const char*) override { rig_dirs++; }
    bool exists(const char* path) override { return find(path) != nullptr; }
    size_t list(const char* dir, const char* suffix, char* names, size_t count, size_t nameSize) override
    {
        rig_lists++;
        const size_t d = length(dir);
        size_t n = 0;
        for (const CardFile& f : rig_files)
        {
            if (!f.used || n >= count)
            {
                continue;
            }
            bool inDir = length(f.path) > d + 1 && f.path[d] == '/';
            for (size_t i = 0; inDir && i < d; i++)
            {
                inDir = f.path[i] == dir[i];
            }
            const char* name = f.path + d + 1;
            bool plain = true;
            for (const char* c = name; *c != 0; c++)
            {
                plain = plain && *c != '/';
            }
            if (!inDir || !plain || !endsWith(name, suffix) || length(name) + 1 > nameSize)
            {
                continue;
            }
            size_t at = n++; // sorted, as CE's listDirContents gives them
            while (at > 0 && compare(names + (at - 1) * nameSize, name) > 0)
            {
                memcpy(names + at * nameSize, names + (at - 1) * nameSize, nameSize);
                at--;
            }
            memcpy(names + at * nameSize, name, length(name) + 1);
        }
        return n;
    }
};

Card card; // constant-initialised: its vtable pointer is in the image (the rig runs no constructors)

template <typename Fn>
Fn fn(uint32_t address)
{
    return reinterpret_cast<Fn>(address);
}

float fromBits(uint32_t bits)
{
    float v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

uint32_t toBits(float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof(v));
    return bits;
}

} // namespace

// Entry points. Floats go as their bits (the test sets only r0-r3).
extern "C" {

void rig_forget()
{
    plugins::forget();
    memset(rig_files, 0, sizeof(rig_files));
    rig_card_used = rig_writes = rig_removes = rig_dirs = rig_lists = rig_fail_writes = 0;
}

void rig_start()
{
    const plugins::Env env = {
        rig_code_pool,
        rig_state_pool,
        fn<void (*)(void*, uint32_t)>(rig_hooks.syncCode),
        fn<uint32_t (*)()>(rig_hooks.cycles),
        fn<uint32_t (*)()>(rig_hooks.millis),
        rig_hooks.budget,
        reinterpret_cast<const PtsfxHost*>(rig_hooks.host),
    };
    plugins::start(card, env);
}

void rig_scan() { plugins::scan(); }
uint32_t rig_catalog_count() { return plugins::catalogCount(); }
const plugins::CatalogEntry* rig_catalog_at(uint32_t i) { return plugins::catalogAt(i); }
uint32_t rig_safe_mode() { return plugins::safeMode(); }
void rig_update() { plugins::update(); }

int rig_create(const char* file, uint32_t fromProject)
{
    plugins::Status s = plugins::Status::ok;
    const int id = plugins::create(file, fromProject != 0, &s);
    rig_status = static_cast<uint32_t>(s);
    return id;
}

void rig_destroy(int id) { plugins::destroy(id); }
uint32_t rig_valid(int id) { return plugins::valid(id); }
const PtsfxHeader* rig_header(int id) { return plugins::header(id); }
uint32_t rig_instances() { return plugins::instances(); }
uint32_t rig_loaded() { return plugins::loaded(); }
void rig_set_knob(int id, uint32_t k, uint32_t bits) { plugins::setKnob(id, k, fromBits(bits)); }
uint32_t rig_knob(int id, uint32_t k) { return toBits(plugins::knob(id, k)); }
void rig_step_knob(int id, uint32_t k, int steps) { plugins::stepKnob(id, k, steps); }
void rig_knob_text(int id, uint32_t k, char* out, uint32_t size) { plugins::knobText(id, k, out, size); }
void rig_reset_soon(int id) { plugins::resetSoon(id); }
int rig_take_bypassed() { return plugins::takeBypassed(); }
uint32_t rig_bypassed(int id) { return plugins::bypassed(id); }
uint32_t rig_process(int id, float* L, float* R, uint32_t inChannels) { return plugins::process(id, L, R, inChannels); }
void rig_reset(int id) { plugins::reset(id); }
uint32_t rig_pack_count() { return plugins::packCount(); }
const plugins::PackFile* rig_pack_at(uint32_t i) { return &plugins::packAt(i); }

// What a -nostdlib link lacks for the card's destructor, which never runs
void* __dso_handle = nullptr;
int __aeabi_atexit(void*, void (*)(void*), void*)
{
    return 0;
}
}

void operator delete(void*) {}
void operator delete(void*, unsigned int) {}
