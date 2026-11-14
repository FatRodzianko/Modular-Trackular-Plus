// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The Master FX plugins' runtime (masterfxPlugins.h). Platform-free: the firmware's memory,
// cache sync, clock and host table come in Env (masterfxPluginsCe.cpp); the host tests and the
// emulator rig give their own. No C library beyond memset and memcpy, so the rig links without
// one.

#include "modsBuild.h"

#if MT_HAS_MASTERFX

#include "masterfxPlugins.h"
#include "plugins/pluginKit.h"

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
namespace masterfx
{
namespace plugins
{

namespace
{

using pluginkit::appendText;
using pluginkit::copyText;
using pluginkit::endedText;
using pluginkit::entryAt;
using pluginkit::lastStep;
using pluginkit::sameText;
using pluginkit::stepNearest;
using pluginkit::textLength;
using pluginkit::valueAt;

// A slot's plugin. Read by the interrupt while active.
struct SlotState
{
    volatile uint8_t active;      // set last when installed, cleared first when it goes
    volatile uint8_t resetWanted;
    volatile uint8_t bypassed;    // by the time guard (the interrupt)
    uint8_t reported;             // takeBypassed has told the page
    uint8_t over;                 // blocks over budget in a row (the interrupt's)
    uint8_t isFloat;
    uint8_t knobs;
    uint8_t pad;
    volatile uint8_t dirty[PTMFX_MAX_PARAMS];
    volatile float value[PTMFX_MAX_PARAMS];
    PtmfxSetParam setParam;
    PtmfxProcess process;
    PtmfxReset reset;
    PtmfxFormat format;           // or nullptr
    PtmfxHeader header;           // the installed plugin's
    char file[kFileNameSize];
};

// What read() left for install()
struct Read
{
    uint8_t slot;                 // kSlots: nothing
    uint32_t size;
    char file[kFileNameSize];
};

// The tables and the load buffer's bookkeeping: SDRAM (main loop, except a slot, read once per
// block). Startup doesn't zero it: forget() does.
MT_PLUGINS_SDRAM SlotState slots[kSlots];
MT_PLUGINS_SDRAM CatalogEntry catalog[kCatalogSize];
MT_PLUGINS_SDRAM char listed[kCatalogSize][kFileNameSize];
MT_PLUGINS_SDRAM Read lastRead;

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

bool pathOf(const char* file, char* out, size_t size)
{
    return pluginkit::joinPath(kDir, file, kFileNameSize, out, size);
}

bool validSlot(Slot s)
{
    return static_cast<size_t>(s) < kSlots;
}

const PtmfxParam* paramOf(Slot s, uint32_t knob)
{
    if (!validSlot(s) || !slots[s].active || knob >= slots[s].knobs)
    {
        return nullptr;
    }
    return &slots[s].header.params[knob];
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
    PtmfxHeader h;
    const size_t size = pathOf(file, path, sizeof(path)) ? io->read(path, reinterpret_cast<uint8_t*>(&h), sizeof(h))
                                                          : FileIo::kReadFailed;
    const char* why = nullptr;
    if (size == FileIo::kReadFailed)
    {
        e.status = Status::readFailed;
    }
    else if (size < sizeof(h) || !checkHeader(h, static_cast<uint32_t>(size), &why))
    {
        e.status = size >= sizeof(h) && h.magic == PTMFX_MAGIC && sameText(why, "too big") ? Status::tooBig
                                                                                            : Status::badFile;
    }
    else
    {
        e.status = Status::ok;
        e.size = static_cast<uint32_t>(size);
        e.flags = h.flags;
        e.slot = h.slot;
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

MT_PLUGINS_ISR bool guard(float* b)
{
    if (pluginkit::blockFine(b, PTMFX_BLOCK, kOutputLimit))
    {
        return false;
    }
    return pluginkit::fixBlock(b, PTMFX_BLOCK, kOutputLimit);
}

constexpr float kFromQ15 = 1.0f / 32768.0f;

MT_PLUGINS_ISR void toFloat(const int16_t* in, float* out)
{
    for (uint32_t i = 0; i < PTMFX_BLOCK; i++)
    {
        out[i] = static_cast<float>(in[i]) * kFromQ15;
    }
}

// Rounded to nearest and held to 16 bits (VCVT rounds toward zero, so the half is added by sign)
MT_PLUGINS_ISR void toQ15(const float* in, int16_t* out)
{
    for (uint32_t i = 0; i < PTMFX_BLOCK; i++)
    {
        float v = in[i] * 32768.0f;
        v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
        out[i] = static_cast<int16_t>(v >= 0.0f ? v + 0.5f : v - 0.5f);
    }
}

} // namespace

// ---- the header ---------------------------------------------------------------------------

bool checkHeader(const PtmfxHeader& h, uint32_t fileSize, const char** why)
{
    const char* dummy = nullptr;
    const char*& w = why != nullptr ? *why : dummy;
    w = nullptr;
    const uint32_t slot = h.slot;
    if (h.magic != PTMFX_MAGIC)
    {
        w = "not a .ptmfx";
    }
    else if (h.format_version != PTMFX_VERSION)
    {
        w = "another format version";
    }
    else if ((h.flags & ~static_cast<uint32_t>(PTMFX_FLAG_FLOAT)) != 0)
    {
        w = "unknown flags";
    }
    else if (slot < PTMFX_SLOT_REVERB || slot > PTMFX_SLOT_DYNAMICS)
    {
        w = "slot";
    }
    else if (h.name[0] == 0 || !endedText(h.name) || !endedText(h.author))
    {
        w = "name or author";
    }
    else if (h.num_params > PTMFX_MAX_PARAMS || h.reserved[0] != 0 || h.reserved[1] != 0 || h.reserved2[0] != 0 ||
             h.reserved2[1] != 0 || h.reserved2[2] != 0)
    {
        w = "knob count";
    }
    else if (h.state_size == 0 || h.state_size > kStateSize[slot - 1] || h.code_size == 0 ||
             h.code_size > kCodeArea)
    {
        w = "too big";
    }
    else if (h.code_offset != sizeof(PtmfxHeader) || h.code_offset + h.code_size != fileSize)
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
        if (h.format_offset != PTMFX_NO_ENTRY && (h.format_offset >= h.code_size || (h.format_offset & 1u) != 0))
        {
            w = "entry";
        }
    }
    for (uint32_t i = 0; w == nullptr && i < PTMFX_MAX_LABELS; i++)
    {
        if (!endedText(h.labels[i]) || (i >= kLabels[slot - 1] && h.labels[i][0] != 0))
        {
            w = "label";
        }
    }
    static_assert(PTMFX_PARAM_FLOAT == pluginkit::kKnobFloat && PTMFX_PARAM_INT == pluginkit::kKnobInt &&
                      PTMFX_PARAM_LIST == pluginkit::kKnobList,
                  "the kit checks ptmfx.h's display types");
    for (uint32_t k = 0; w == nullptr && k < h.num_params; k++)
    {
        w = pluginkit::checkKnob(h.params[k], kMaxKnobSteps);
    }
    return w == nullptr;
}

// ---- the catalog --------------------------------------------------------------------------

void forget()
{
    memset(static_cast<void*>(slots), 0, sizeof(slots));
    memset(catalog, 0, sizeof(catalog));
    memset(listed, 0, sizeof(listed));
    memset(&lastRead, 0, sizeof(lastRead));
    lastRead.slot = kSlots;
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

size_t slotCount(Slot slot)
{
    size_t n = 0;
    for (size_t i = 0; i < catalogLength; i++)
    {
        n += catalog[i].status == Status::ok && catalog[i].slot == slotType(slot) ? 1 : 0;
    }
    return n;
}

const CatalogEntry* slotAt(Slot slot, size_t index)
{
    for (size_t i = 0; i < catalogLength; i++)
    {
        if (catalog[i].status == Status::ok && catalog[i].slot == slotType(slot) && index-- == 0)
        {
            return &catalog[i];
        }
    }
    return nullptr;
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

// ---- loading ------------------------------------------------------------------------------

Status read(Slot slot, const char* file, bool fromProject)
{
    lastRead.slot = kSlots;
    if (!isStarted || !validSlot(slot))
    {
        return Status::missing;
    }
    if (fromProject && inSafeMode)
    {
        return Status::skipped;
    }
    const int at = catalogFind(file);
    if (at < 0 || catalog[at].status != Status::ok)
    {
        return at < 0 ? Status::missing : catalog[at].status;
    }
    if (catalog[at].slot != slotType(slot))
    {
        return Status::wrongSlot;
    }
    const uint32_t size = catalog[at].size;
    char path[sizeof(kDir) + kFileNameSize];
    if (!pathOf(file, path, sizeof(path)) || size > kMaxFile)
    {
        return Status::missing;
    }
    const size_t got = io->read(path, env.loadBuffer, kMaxFile);
    const PtmfxHeader* const h = reinterpret_cast<const PtmfxHeader*>(env.loadBuffer);
    if (got == FileIo::kReadFailed)
    {
        return Status::readFailed;
    }
    if (got != size || !checkHeader(*h, size, nullptr) || h->slot != slotType(slot))
    {
        return Status::badFile; // changed since the scan
    }
    lastRead.slot = slot;
    lastRead.size = size;
    copyText(lastRead.file, sizeof(lastRead.file), file);
    return Status::ok;
}

Status install(Slot slot)
{
    if (!validSlot(slot) || lastRead.slot != slot)
    {
        return Status::notRead;
    }
    lastRead.slot = kSlots;
    SlotState& s = slots[slot];
    s.active = 0; // the caller has stopped the slot; this keeps the interrupt out of it too
    barrier();

    const PtmfxHeader* const h = reinterpret_cast<const PtmfxHeader*>(env.loadBuffer);
    memcpy(&s.header, h, sizeof(s.header));
    uint8_t* const code = env.code[slot];
    memcpy(code, env.loadBuffer + h->code_offset, h->code_size);
    env.syncCode(code, h->code_size);

    PtmfxInit init = entryAt<PtmfxInit>(code, h->init_offset);
    s.setParam = entryAt<PtmfxSetParam>(code, h->set_param_offset);
    s.process = entryAt<PtmfxProcess>(code, h->process_offset);
    s.reset = entryAt<PtmfxReset>(code, h->reset_offset);
    s.format = h->format_offset == PTMFX_NO_ENTRY ? nullptr : entryAt<PtmfxFormat>(code, h->format_offset);
    s.knobs = h->num_params;
    s.isFloat = (h->flags & PTMFX_FLAG_FLOAT) != 0 ? 1 : 0;
    s.over = 0;
    s.bypassed = 0;
    s.reported = 0;
    s.resetWanted = 1; // its memory is cleared after this, then it starts over
    copyText(s.file, sizeof(s.file), lastRead.file);

    // The marker first: init is the plugin's code too
    if (!markerUp)
    {
        const uint8_t one = '1';
        io->write(kMarker, &one, 1);
        markerUp = true;
    }
    markerSince = env.millis();

    memset(env.state[slot], 0, kStateSize[slot]);
    init(env.state[slot], &env.memory[slot], env.host);
    for (uint32_t k = 0; k < PTMFX_MAX_PARAMS; k++)
    {
        const PtmfxParam& param = s.header.params[k];
        s.value[k] = k < s.knobs ? valueAt(param, stepNearest(param, param.default_val)) : 0.0f; // on its grid
        s.dirty[k] = k < s.knobs ? 1 : 0;
    }
    barrier();
    s.active = 1;
    return Status::ok;
}

void remove(Slot slot)
{
    if (!validSlot(slot))
    {
        return;
    }
    slots[slot].active = 0; // the interrupt can't be running it now (single core), and won't again
    barrier();
    slots[slot].file[0] = 0;
    slots[slot].bypassed = 0;
}

bool installed(Slot slot)
{
    return validSlot(slot) && slots[slot].active;
}

const PtmfxHeader* header(Slot slot)
{
    return installed(slot) ? &slots[slot].header : nullptr;
}

const char* file(Slot slot)
{
    return installed(slot) ? slots[slot].file : "";
}

MT_PLUGINS_ISR void cleared(Slot slot)
{
    if (validSlot(slot))
    {
        slots[slot].resetWanted = 1;
    }
}

// ---- own knobs ----------------------------------------------------------------------------

uint32_t knobs(Slot slot)
{
    return installed(slot) ? slots[slot].knobs : 0u;
}

void setKnob(Slot slot, uint32_t knob, float value)
{
    const PtmfxParam* p = paramOf(slot, knob);
    if (p == nullptr)
    {
        return;
    }
    SlotState& s = slots[slot];
    s.value[knob] = valueAt(*p, stepNearest(*p, value));
    s.dirty[knob] = 1; // after the value: the interrupt clears this, then reads the value
}

float knob(Slot slot, uint32_t knob)
{
    return paramOf(slot, knob) != nullptr ? slots[slot].value[knob] : 0.0f;
}

void stepKnob(Slot slot, uint32_t knob, int steps)
{
    const PtmfxParam* p = paramOf(slot, knob);
    if (p == nullptr)
    {
        return;
    }
    const int32_t now = static_cast<int32_t>(stepNearest(*p, slots[slot].value[knob]));
    int32_t to = now + steps;
    const int32_t last = static_cast<int32_t>(lastStep(*p));
    to = to < 0 ? 0 : (to > last ? last : to);
    setKnob(slot, knob, valueAt(*p, static_cast<uint32_t>(to)));
}

uint32_t knobSteps(Slot slot, uint32_t knob)
{
    const PtmfxParam* p = paramOf(slot, knob);
    return p != nullptr ? lastStep(*p) + 1u : 0u;
}

uint32_t knobStep(Slot slot, uint32_t knob)
{
    const PtmfxParam* p = paramOf(slot, knob);
    return p != nullptr ? stepNearest(*p, slots[slot].value[knob]) : 0u;
}

void setKnobStep(Slot slot, uint32_t knob, uint32_t step)
{
    const PtmfxParam* p = paramOf(slot, knob);
    if (p != nullptr)
    {
        const uint32_t last = lastStep(*p);
        setKnob(slot, knob, valueAt(*p, step > last ? last : step));
    }
}

const char* knobLabel(Slot slot, uint32_t knob)
{
    const PtmfxParam* p = paramOf(slot, knob);
    return p != nullptr ? p->label : "";
}

void resetKnob(Slot slot, uint32_t knob)
{
    const PtmfxParam* p = paramOf(slot, knob);
    if (p != nullptr)
    {
        setKnob(slot, knob, p->default_val);
    }
}

void knobText(Slot slot, uint32_t knob, char* out, uint32_t size)
{
    const PtmfxParam* p = paramOf(slot, knob);
    knobTextAt(slot, knob, p != nullptr ? stepNearest(*p, slots[slot].value[knob]) : 0u, out, size);
}

void knobTextAt(Slot slot, uint32_t knob, uint32_t step, char* out, uint32_t size)
{
    if (size == 0)
    {
        return;
    }
    out[0] = 0;
    const PtmfxParam* p = paramOf(slot, knob);
    if (p == nullptr)
    {
        return;
    }
    const SlotState& s = slots[slot];
    const uint32_t last = lastStep(*p);
    const float v = valueAt(*p, step > last ? last : step);
    if (s.format != nullptr)
    {
        s.format(env.host, knob, v, out, size);
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
    env.host->print(out, size, p->display_type == PTMFX_PARAM_FLOAT ? "%.1f" : "%.0f", v);
    out[size - 1] = 0;
    if (p->unit[0] != 0)
    {
        appendText(out, size, " ");
        appendText(out, size, p->unit);
    }
}

const char* label(Slot slot, uint32_t index)
{
    if (!installed(slot) || index >= kLabels[slot] || slots[slot].header.labels[index][0] == 0)
    {
        return nullptr;
    }
    return slots[slot].header.labels[index];
}

uint32_t now()
{
    return env.millis != nullptr ? env.millis() : 0u;
}

int takeBypassed()
{
    for (size_t i = 0; i < kSlots; i++)
    {
        SlotState& s = slots[i];
        if (s.active && s.bypassed && !s.reported)
        {
            s.reported = 1;
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool bypassed(Slot slot)
{
    return installed(slot) && slots[slot].bypassed;
}

// ---- the interrupt ------------------------------------------------------------------------

MT_PLUGINS_ISR Ran process(Slot slot, PtmfxBlock& b, float* gain)
{
    if (static_cast<uint32_t>(slot) >= kSlots)
    {
        return Ran::none;
    }
    SlotState& s = slots[slot];
    if (!s.active)
    {
        return Ran::none;
    }
    if (s.bypassed)
    {
        return Ran::bypassed;
    }
    void* const st = env.state[slot];
    if (s.resetWanted)
    {
        s.resetWanted = 0;
        s.reset(st);
    }
    for (uint32_t k = 0; k < s.knobs; k++)
    {
        if (s.dirty[k])
        {
            s.dirty[k] = 0;
            s.setParam(st, k, s.value[k]);
        }
    }
    b.frames = PTMFX_BLOCK;
    float* const f = env.scratch;
    if (s.isFloat) // CE's blocks to floats and back: L, R, then the delay's send or the key
    {
        b.fleft = f;
        b.fright = f + PTMFX_BLOCK;
        b.fin = nullptr;
        b.fkey_left = nullptr;
        b.fkey_right = nullptr;
        if (b.in != nullptr)
        {
            toFloat(b.in, f + 2 * PTMFX_BLOCK);
            b.fin = f + 2 * PTMFX_BLOCK;
        }
        else
        {
            toFloat(b.left, f);
            toFloat(b.right, f + PTMFX_BLOCK);
            if (b.key_left != nullptr && b.key_right != nullptr)
            {
                toFloat(b.key_left, f + 2 * PTMFX_BLOCK);
                toFloat(b.key_right, f + 3 * PTMFX_BLOCK);
                b.fkey_left = f + 2 * PTMFX_BLOCK;
                b.fkey_right = f + 3 * PTMFX_BLOCK;
            }
        }
    }
    const uint32_t t0 = env.cycles();
    const float g = s.process(st, &b);
    const uint32_t spent = env.cycles() - t0;
    if (spent > env.budgetCycles)
    {
        if (++s.over >= kOverBlocks)
        {
            s.bypassed = 1;
        }
    }
    else
    {
        s.over = 0;
    }
    if (s.isFloat)
    {
        const bool bad = guard(f) | guard(f + PTMFX_BLOCK);
        if (bad)
        {
            s.resetWanted = 1;
        }
        toQ15(f, b.left);
        toQ15(f + PTMFX_BLOCK, b.right);
    }
    if (gain != nullptr)
    {
        *gain = pluginkit::finite(g) ? (g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g)) : 1.0f;
    }
    return Ran::ran;
}

} // namespace plugins
} // namespace masterfx
} // namespace mods

#endif // MT_HAS_MASTERFX
