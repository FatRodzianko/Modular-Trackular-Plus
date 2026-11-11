// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The Synth FX plugins' runtime (synthfxPlugins.h). Platform-free: the firmware's memory,
// cache sync, clock and host table come in Env (synthfxPluginsCe.cpp); the host tests and the
// emulator rig (tools/synthfx/rig) give their own. No C library beyond memset and memcpy, so
// the rig links without one.

#include "modsBuild.h"

#if MT_HAS_SYNTHFX

#include "synthfxPlugins.h"
#include "synthfxPool.h"

#include <cstring>

#if defined(MT_HOST_TEST)
#define MT_PLUGINS_SDRAM
#define MT_PLUGINS_ISR
#else
#include "config.h"
#include <cr_section_macros.h>
#define MT_PLUGINS_SDRAM __NOINIT(EXTERNAL_RAM)
#define MT_PLUGINS_ISR AUDIO_CORE_RAM
#endif

namespace mods
{
namespace synthfx
{
namespace plugins
{

namespace
{

// A plugin whose code is in the code pool. Read by the interrupt while users > 0.
struct Plugin
{
    uint32_t users;               // its instances
    uint32_t block;               // its code pool block: the file at block + 4, the code 32-aligned
    const PtsfxHeader* header;    // the copy in the pool
    PtsfxInit init;
    PtsfxSetParam setParam;
    PtsfxProcess process;
    PtsfxReset reset;
    PtsfxFormat format;           // or nullptr
    uint32_t knobs;
    char file[kFileNameSize];
};

struct Instance
{
    volatile uint8_t active;      // set last when made, cleared first when it goes
    volatile uint8_t resetWanted;
    volatile uint8_t bypassed;    // by the time guard (the interrupt)
    uint8_t reported;             // takeBypassed has told the page
    uint8_t plugin;               // in plugins
    uint8_t over;                 // blocks over budget in a row (the interrupt's)
    volatile uint8_t dirty[PTSFX_MAX_PARAMS];
    volatile float value[PTSFX_MAX_PARAMS];
    uint32_t stateBlock;
    void* state;
};

// The pools' bookkeeping, the catalog and the tables: SDRAM (main loop, except an instance
// and its plugin, read once per block). Startup doesn't zero it: forget() does.
MT_PLUGINS_SDRAM Pool<kLoadedSize> codePool;
MT_PLUGINS_SDRAM Pool<kInstanceCount> statePool;
MT_PLUGINS_SDRAM Plugin plugins[kLoadedSize];
MT_PLUGINS_SDRAM Instance instanceTable[kInstanceCount];
MT_PLUGINS_SDRAM CatalogEntry catalog[kCatalogSize];
MT_PLUGINS_SDRAM char listed[kCatalogSize][kFileNameSize];

// Zeroed at startup
FileIo* io = nullptr;
Env env = {};
bool isStarted = false;
bool inSafeMode = false;
bool markerUp = false;            // written by this run, and its clock running
uint32_t markerSince = 0;
size_t catalogLength = 0;

inline void barrier()
{
    __asm__ volatile("" ::: "memory");
}

// ---- small string helpers (no C library) ------------------------------------------------

size_t textLength(const char* s, size_t limit)
{
    size_t n = 0;
    while (n < limit && s[n] != 0)
    {
        n++;
    }
    return n;
}

bool sameText(const char* a, const char* b)
{
    while (*a != 0 && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

void copyText(char* out, size_t size, const char* in)
{
    size_t n = 0;
    for (; n + 1 < size && in[n] != 0; n++)
    {
        out[n] = in[n];
    }
    if (size > 0)
    {
        out[n] = 0;
    }
}

void appendText(char* out, size_t size, const char* in)
{
    const size_t n = textLength(out, size);
    if (n < size)
    {
        copyText(out + n, size - n, in);
    }
}

// kDir/file, or false if it doesn't fit
bool pathOf(const char* file, char* out, size_t size)
{
    copyText(out, size, kDir);
    appendText(out, size, "/");
    appendText(out, size, file);
    return textLength(out, size) == textLength(kDir, sizeof(kDir)) + 1 + textLength(file, kFileNameSize);
}

// ---- numbers ------------------------------------------------------------------------------

inline bool finite(float v)
{
    return v - v == 0.0f; // NaN and the infinities give NaN
}

// The nearest whole number, for |x| < 2^30
inline int32_t nearest(float x)
{
    return x >= 0.0f ? static_cast<int32_t>(x + 0.5f) : -static_cast<int32_t>(-x + 0.5f);
}

inline float absf(float x)
{
    return x < 0.0f ? -x : x;
}

// x is a whole number, near enough for steps worked out in single precision
inline bool whole(float x, int32_t* n)
{
    if (!(absf(x) < 1.0e9f))
    {
        return false;
    }
    *n = nearest(x);
    const float tolerance = 1.0e-3f * (absf(x) > 1.0f ? absf(x) : 1.0f);
    return absf(x - static_cast<float>(*n)) <= tolerance;
}

// A knob's last step (it has steps 0 to this)
uint32_t lastStep(const PtsfxParam& p)
{
    int32_t n = 0;
    whole((p.max_val - p.min_val) / p.step, &n);
    return n < 0 ? 0u : static_cast<uint32_t>(n);
}

// min + step x p.step, rounded once (ptsfx.h)
float valueAt(const PtsfxParam& p, uint32_t step)
{
    return __builtin_fmaf(static_cast<float>(step), p.step, p.min_val);
}

uint32_t stepNearest(const PtsfxParam& p, float value)
{
    const uint32_t last = lastStep(p);
    if (!(value > p.min_val)) // NaN too
    {
        return 0;
    }
    const float at = (value - p.min_val) / p.step;
    if (!(at < static_cast<float>(last)))
    {
        return last;
    }
    return static_cast<uint32_t>(nearest(at));
}

// ---- the tables ------------------------------------------------------------------------

Instance* instanceOf(int id)
{
    if (id < 0 || static_cast<size_t>(id) >= kInstanceCount || !instanceTable[id].state)
    {
        return nullptr;
    }
    return &instanceTable[id];
}

const PtsfxParam* paramOf(int id, uint32_t knob)
{
    const Instance* in = instanceOf(id);
    if (in == nullptr)
    {
        return nullptr;
    }
    const Plugin& p = plugins[in->plugin];
    return knob < p.knobs ? &p.header->params[knob] : nullptr;
}

template <typename Entry>
Entry entryAt(const uint8_t* code, uint32_t offset)
{
    return reinterpret_cast<Entry>(reinterpret_cast<uintptr_t>(code + offset) | 1u); // Thumb
}

// The plugin in file loaded (or already), or -1 with *status set
int load(const char* file, Status* status)
{
    for (size_t i = 0; i < kLoadedSize; i++)
    {
        if (plugins[i].users > 0 && sameText(plugins[i].file, file))
        {
            return static_cast<int>(i);
        }
    }
    size_t slot = 0;
    while (slot < kLoadedSize && plugins[slot].users > 0)
    {
        slot++;
    }
    if (slot == kLoadedSize)
    {
        *status = Status::tooMany;
        return -1;
    }

    const int at = catalogFind(file);
    if (at < 0 || catalog[at].status != Status::ok)
    {
        *status = at < 0 ? Status::missing : catalog[at].status;
        return -1;
    }
    const uint32_t size = catalog[at].size;
    char path[sizeof(kDir) + kFileNameSize];
    if (!pathOf(file, path, sizeof(path)))
    {
        *status = Status::missing;
        return -1;
    }
    const uint32_t block = codePool.alloc(size + 4u);
    if (block == codePool.kNone)
    {
        *status = Status::noCodeRoom;
        return -1;
    }
    uint8_t* const at4 = codePool.at(block) + 4; // the header at +4 puts the code (476 on) 32-aligned
    const size_t got = io->read(path, at4, size);
    const PtsfxHeader* const h = reinterpret_cast<const PtsfxHeader*>(at4);
    const char* why = nullptr;
    if (got == FileIo::kReadFailed || got != size || !checkHeader(*h, size, &why))
    {
        codePool.free(block);
        *status = got == FileIo::kReadFailed ? Status::readFailed : Status::badFile; // changed since the scan
        return -1;
    }
    const uint8_t* const code = at4 + h->code_offset;
    env.syncCode(const_cast<uint8_t*>(code), h->code_size);

    Plugin& p = plugins[slot];
    p.block = block;
    p.header = h;
    p.init = entryAt<PtsfxInit>(code, h->init_offset);
    p.setParam = entryAt<PtsfxSetParam>(code, h->set_param_offset);
    p.process = entryAt<PtsfxProcess>(code, h->process_offset);
    p.reset = entryAt<PtsfxReset>(code, h->reset_offset);
    p.format = h->format_offset == PTSFX_NO_ENTRY ? nullptr : entryAt<PtsfxFormat>(code, h->format_offset);
    p.knobs = h->num_params;
    copyText(p.file, sizeof(p.file), file);
    p.users = 0; // create counts the instance
    return static_cast<int>(slot);
}

void unload(size_t slot)
{
    Plugin& p = plugins[slot];
    if (p.users > 0 && --p.users == 0)
    {
        codePool.free(p.block);
        p.header = nullptr;
    }
}

void listInto(size_t* length, const char* file)
{
    if (*length >= kCatalogSize)
    {
        return;
    }
    CatalogEntry& e = catalog[*length];
    memset(&e, 0, sizeof(e));
    copyText(e.file, sizeof(e.file), file);
    char path[sizeof(kDir) + kFileNameSize];
    PtsfxHeader h;
    const size_t size = pathOf(file, path, sizeof(path)) ? io->read(path, reinterpret_cast<uint8_t*>(&h), sizeof(h))
                                                          : FileIo::kReadFailed;
    const char* why = nullptr;
    if (size == FileIo::kReadFailed)
    {
        e.status = Status::readFailed;
    }
    else if (size < sizeof(h) || !checkHeader(h, static_cast<uint32_t>(size), &why))
    {
        e.status = size >= sizeof(h) && h.magic == PTSFX_MAGIC &&
                       (h.code_size > kMaxCode || h.state_size > kMaxState)
                     ? Status::tooBig
                     : Status::badFile;
    }
    else
    {
        e.status = Status::ok;
        e.size = static_cast<uint32_t>(size);
        e.flags = h.flags;
        e.knobs = h.num_params;
        copyText(e.name, sizeof(e.name), h.name);
    }
    if (e.status != Status::ok)
    {
        copyText(e.name, sizeof(e.name), file); // the file's name, for the list's "can't use"
    }
    (*length)++;
}

// ---- the interrupt's ----------------------------------------------------------------------

// The rare block that needs it: NaN or infinite → 0 (true: there was one), else held within
// ±kOutputLimit
bool fix(float* b)
{
    bool bad = false;
    for (uint32_t i = 0; i < PTSFX_BLOCK; i++)
    {
        const float v = b[i];
        if (!finite(v))
        {
            b[i] = 0.0f;
            bad = true;
        }
        else if (v > kOutputLimit)
        {
            b[i] = kOutputLimit;
        }
        else if (v < -kOutputLimit)
        {
            b[i] = -kOutputLimit;
        }
    }
    return bad;
}

// The block looked at without a branch per sample: v - v is 0 for every finite v and NaN
// otherwise, so the sums stay 0 unless one isn't; the peak is the largest |v| (VMAXNM, which
// passes over a NaN: the sums catch those). Only a block that needs it goes through fix().
MT_PLUGINS_ISR bool guard(float* b)
{
    float nan0 = 0.0f, nan1 = 0.0f, peak0 = 0.0f, peak1 = 0.0f;
    for (uint32_t i = 0; i < PTSFX_BLOCK; i += 2)
    {
        const float v0 = b[i], v1 = b[i + 1];
        nan0 += v0 - v0;
        nan1 += v1 - v1;
        peak0 = __builtin_fmaxf(peak0, __builtin_fabsf(v0));
        peak1 = __builtin_fmaxf(peak1, __builtin_fabsf(v1));
    }
    if (nan0 + nan1 == 0.0f && __builtin_fmaxf(peak0, peak1) <= kOutputLimit)
    {
        return false;
    }
    return fix(b);
}

} // namespace

// ---- the header ---------------------------------------------------------------------------

bool checkHeader(const PtsfxHeader& h, uint32_t fileSize, const char** why)
{
    const char* dummy = nullptr;
    const char*& w = why != nullptr ? *why : dummy;
    w = nullptr;
    if (h.magic != PTSFX_MAGIC)
    {
        w = "not a .ptsfx";
    }
    else if (h.format_version != PTSFX_VERSION)
    {
        w = "another format version";
    }
    else if ((h.flags & ~(PTSFX_FLAG_WIDENS | PTSFX_FLAG_PRE)) != 0 ||
             (h.flags & (PTSFX_FLAG_WIDENS | PTSFX_FLAG_PRE)) == (PTSFX_FLAG_WIDENS | PTSFX_FLAG_PRE))
    {
        w = "unknown flags";
    }
    else if (h.name[0] == 0 || textLength(h.name, sizeof(h.name)) == sizeof(h.name) ||
             textLength(h.author, sizeof(h.author)) == sizeof(h.author))
    {
        w = "name or author";
    }
    else if (h.num_params > PTSFX_MAX_PARAMS || h.reserved[0] != 0 || h.reserved[1] != 0 || h.reserved[2] != 0)
    {
        w = "knob count";
    }
    else if (h.state_size == 0 || h.state_size > kMaxState || h.code_size == 0 || h.code_size > kMaxCode)
    {
        w = "too big";
    }
    else if (h.code_offset != sizeof(PtsfxHeader) || h.code_offset + h.code_size != fileSize)
    {
        w = "file size";
    }
    else
    {
        const uint32_t entries[] = { h.init_offset, h.set_param_offset, h.process_offset, h.reset_offset };
        for (uint32_t e : entries)
        {
            if (e >= h.code_size || (e & 1u) != 0)
            {
                w = "entry";
            }
        }
        if (h.format_offset != PTSFX_NO_ENTRY && (h.format_offset >= h.code_size || (h.format_offset & 1u) != 0))
        {
            w = "entry";
        }
    }
    for (uint32_t k = 0; w == nullptr && k < h.num_params; k++)
    {
        const PtsfxParam& p = h.params[k];
        int32_t steps = 0, at = 0;
        if (p.label[0] == 0 || textLength(p.label, sizeof(p.label)) == sizeof(p.label) ||
            textLength(p.unit, sizeof(p.unit)) == sizeof(p.unit))
        {
            w = "knob label";
        }
        else if (p.display_type != PTSFX_PARAM_FLOAT && p.display_type != PTSFX_PARAM_INT &&
                 p.display_type != PTSFX_PARAM_LIST)
        {
            w = "knob display";
        }
        else if (!finite(p.min_val) || !finite(p.max_val) || !finite(p.default_val) || !finite(p.step) ||
                 !(p.step > 0.0f) || !(p.max_val >= p.min_val))
        {
            w = "knob range";
        }
        else if (!whole((p.max_val - p.min_val) / p.step, &steps) || static_cast<uint32_t>(steps) > kMaxKnobSteps)
        {
            w = "knob steps";
        }
        else if (!whole((p.default_val - p.min_val) / p.step, &at) || at < 0 || at > steps)
        {
            w = "knob default";
        }
    }
    return w == nullptr;
}

// ---- the catalog --------------------------------------------------------------------------

void forget()
{
    memset(plugins, 0, sizeof(plugins));
    memset(static_cast<void*>(instanceTable), 0, sizeof(instanceTable));
    memset(catalog, 0, sizeof(catalog));
    memset(listed, 0, sizeof(listed));
    codePool.init(nullptr, 0);
    statePool.init(nullptr, 0);
    io = nullptr;
    env = Env{};
    isStarted = false;
    inSafeMode = false;
    markerUp = false;
    markerSince = 0;
    catalogLength = 0;
}

void start(FileIo& fileIo, const Env& platform)
{
    if (isStarted)
    {
        scan();
        return;
    }
    forget();
    io = &fileIo;
    env = platform;
    codePool.init(env.codePool, kCodePoolSize);
    statePool.init(env.statePool, kStatePoolSize);

    io->makeDir(kDir);
    inSafeMode = io->exists(kMarker); // the last run stopped while a new plugin ran
    char path[sizeof(kDir) + kFileNameSize];
    for (size_t i = 0; i < packCount(); i++)
    {
        const PackFile& f = packAt(i);
        if (pathOf(f.name, path, sizeof(path)) && !io->exists(path))
        {
            io->write(path, f.data, f.size);
        }
    }
    isStarted = true;
    scan();
}

bool started()
{
    return isStarted;
}

bool safeMode()
{
    return inSafeMode;
}

void scan()
{
    if (!isStarted)
    {
        return;
    }
    const size_t found = io->list(kDir, kSuffix, &listed[0][0], kCatalogSize, kFileNameSize);
    size_t length = 0;
    for (size_t i = 0; i < packCount(); i++) // the pack first, in its order
    {
        for (size_t j = 0; j < found; j++)
        {
            if (sameText(listed[j], packAt(i).name))
            {
                listInto(&length, listed[j]);
            }
        }
    }
    for (size_t j = 0; j < found; j++) // then the rest, as listed (sorted)
    {
        bool inPack = false;
        for (size_t i = 0; i < packCount(); i++)
        {
            inPack = inPack || sameText(listed[j], packAt(i).name);
        }
        if (!inPack)
        {
            listInto(&length, listed[j]);
        }
    }
    catalogLength = length;
}

size_t catalogCount()
{
    return catalogLength;
}

const CatalogEntry* catalogAt(size_t index)
{
    return index < catalogLength ? &catalog[index] : nullptr;
}

int catalogFind(const char* file)
{
    for (size_t i = 0; i < catalogLength; i++)
    {
        if (sameText(catalog[i].file, file))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void update()
{
    if (markerUp && env.millis() - markerSince >= kSettleMs)
    {
        io->remove(kMarker);
        markerUp = false;
        inSafeMode = false;
    }
}

// ---- instances ----------------------------------------------------------------------------

int create(const char* file, bool fromProject, Status* status)
{
    Status dummy = Status::ok;
    Status& s = status != nullptr ? *status : dummy;
    s = Status::ok;
    if (!isStarted)
    {
        s = Status::missing;
        return -1;
    }
    if (fromProject && inSafeMode)
    {
        s = Status::skipped;
        return -1;
    }
    size_t id = 0;
    while (id < kInstanceCount && instanceTable[id].state != nullptr)
    {
        id++;
    }
    if (id == kInstanceCount)
    {
        s = Status::tooMany;
        return -1;
    }
    const int slot = load(file, &s);
    if (slot < 0)
    {
        return -1;
    }
    Plugin& p = plugins[slot];
    const uint32_t block = statePool.alloc(p.header->state_size);
    if (block == statePool.kNone)
    {
        p.users++;
        unload(static_cast<size_t>(slot)); // frees the code if no other instance has it
        s = Status::noStateRoom;
        return -1;
    }
    p.users++;

    // The marker first: init is the plugin's code too
    if (!markerUp)
    {
        const uint8_t one = '1';
        io->write(kMarker, &one, 1);
        markerUp = true;
    }
    markerSince = env.millis();

    Instance& in = instanceTable[id];
    in.active = 0;
    in.plugin = static_cast<uint8_t>(slot);
    in.stateBlock = block;
    in.state = statePool.at(block);
    in.over = 0;
    in.bypassed = 0;
    in.reported = 0;
    in.resetWanted = 0;
    memset(in.state, 0, p.header->state_size);
    p.init(in.state, env.host);
    for (uint32_t k = 0; k < PTSFX_MAX_PARAMS; k++)
    {
        const PtsfxParam& param = p.header->params[k];
        in.value[k] = k < p.knobs ? valueAt(param, stepNearest(param, param.default_val)) : 0.0f; // on its grid
        in.dirty[k] = k < p.knobs ? 1 : 0;
    }
    barrier();
    in.active = 1;
    return static_cast<int>(id);
}

void destroy(int id)
{
    Instance* in = instanceOf(id);
    if (in == nullptr)
    {
        return;
    }
    in->active = 0; // the interrupt can't be running it now (single core), and won't again
    barrier();
    statePool.free(in->stateBlock);
    unload(in->plugin);
    in->state = nullptr;
}

bool valid(int id)
{
    return instanceOf(id) != nullptr;
}

const PtsfxHeader* header(int id)
{
    const Instance* in = instanceOf(id);
    return in != nullptr ? plugins[in->plugin].header : nullptr;
}

size_t instances()
{
    size_t n = 0;
    for (const Instance& in : instanceTable)
    {
        n += in.state != nullptr ? 1 : 0;
    }
    return n;
}

size_t loaded()
{
    size_t n = 0;
    for (const Plugin& p : plugins)
    {
        n += p.users > 0 ? 1 : 0;
    }
    return n;
}

void setKnob(int id, uint32_t knob, float value)
{
    const PtsfxParam* p = paramOf(id, knob);
    if (p == nullptr)
    {
        return;
    }
    Instance& in = instanceTable[id];
    in.value[knob] = valueAt(*p, stepNearest(*p, value));
    in.dirty[knob] = 1; // after the value: the interrupt clears this, then reads the value
}

float knob(int id, uint32_t knob)
{
    return paramOf(id, knob) != nullptr ? instanceTable[id].value[knob] : 0.0f;
}

void stepKnob(int id, uint32_t knob, int steps)
{
    const PtsfxParam* p = paramOf(id, knob);
    if (p == nullptr)
    {
        return;
    }
    const int32_t now = static_cast<int32_t>(stepNearest(*p, instanceTable[id].value[knob]));
    int32_t to = now + steps;
    const int32_t last = static_cast<int32_t>(lastStep(*p));
    to = to < 0 ? 0 : (to > last ? last : to);
    setKnob(id, knob, valueAt(*p, static_cast<uint32_t>(to)));
}

uint32_t knobSteps(int id, uint32_t knob)
{
    const PtsfxParam* p = paramOf(id, knob);
    return p != nullptr ? lastStep(*p) + 1u : 0u;
}

uint32_t knobStep(int id, uint32_t knob)
{
    const PtsfxParam* p = paramOf(id, knob);
    return p != nullptr ? stepNearest(*p, instanceTable[id].value[knob]) : 0u;
}

void setKnobStep(int id, uint32_t knob, uint32_t step)
{
    const PtsfxParam* p = paramOf(id, knob);
    if (p != nullptr)
    {
        const uint32_t last = lastStep(*p);
        setKnob(id, knob, valueAt(*p, step > last ? last : step));
    }
}

void knobText(int id, uint32_t knob, char* out, uint32_t size)
{
    const PtsfxParam* p = paramOf(id, knob);
    knobTextAt(id, knob, p != nullptr ? stepNearest(*p, instanceTable[id].value[knob]) : 0u, out, size);
}

void knobTextAt(int id, uint32_t knob, uint32_t step, char* out, uint32_t size)
{
    if (size == 0)
    {
        return;
    }
    out[0] = 0;
    const PtsfxParam* p = paramOf(id, knob);
    if (p == nullptr)
    {
        return;
    }
    const Plugin& plugin = plugins[instanceTable[id].plugin];
    const uint32_t last = lastStep(*p);
    const float v = valueAt(*p, step > last ? last : step);
    if (plugin.format != nullptr)
    {
        plugin.format(env.host, knob, v, out, size);
        out[size - 1] = 0;
        if (out[0] != 0)
        {
            return;
        }
    }
    if (env.host == nullptr || env.host->print == nullptr)
    {
        return;
    }
    env.host->print(out, size, p->display_type == PTSFX_PARAM_FLOAT ? "%.1f" : "%.0f", v);
    out[size - 1] = 0;
    if (p->unit[0] != 0)
    {
        appendText(out, size, " ");
        appendText(out, size, p->unit);
    }
}

uint32_t now()
{
    return env.millis != nullptr ? env.millis() : 0u;
}

void resetSoon(int id)
{
    Instance* in = instanceOf(id);
    if (in != nullptr)
    {
        in->resetWanted = 1;
    }
}

int takeBypassed()
{
    for (size_t i = 0; i < kInstanceCount; i++)
    {
        Instance& in = instanceTable[i];
        if (in.state != nullptr && in.bypassed && !in.reported)
        {
            in.reported = 1;
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool bypassed(int id)
{
    const Instance* in = instanceOf(id);
    return in != nullptr && in->bypassed;
}

// ---- the interrupt ------------------------------------------------------------------------

MT_PLUGINS_ISR bool process(int id, float* L, float* R, uint32_t inChannels)
{
    if (static_cast<uint32_t>(id) >= kInstanceCount)
    {
        return false;
    }
    Instance& in = instanceTable[id];
    if (!in.active)
    {
        return false;
    }
    const Plugin& p = plugins[in.plugin];
    void* const st = in.state;
    if (in.resetWanted)
    {
        in.resetWanted = 0;
        p.reset(st);
    }
    for (uint32_t k = 0; k < p.knobs; k++)
    {
        if (in.dirty[k])
        {
            in.dirty[k] = 0;
            p.setParam(st, k, in.value[k]);
        }
    }
    const uint32_t t0 = env.cycles();
    p.process(st, L, R, PTSFX_BLOCK, inChannels);
    const uint32_t spent = env.cycles() - t0;
    if (spent > env.budgetCycles)
    {
        if (++in.over >= kOverBlocks)
        {
            in.active = 0;
            in.bypassed = 1;
        }
    }
    else
    {
        in.over = 0;
    }
    bool bad = guard(L);
    if (R != nullptr)
    {
        bad = guard(R) || bad;
    }
    if (bad)
    {
        in.resetWanted = 1;
    }
    return true;
}

MT_PLUGINS_ISR void reset(int id)
{
    if (static_cast<uint32_t>(id) >= kInstanceCount)
    {
        return;
    }
    Instance& in = instanceTable[id];
    if (in.active)
    {
        in.resetWanted = 0;
        plugins[in.plugin].reset(in.state);
    }
}

} // namespace plugins
} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX
