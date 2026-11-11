// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Synth FX's slot settings (synthfxMod.h): the file and knobs of each part's two slots, and the
// project record, with version 1 (the built-in effects' ten settings) converted. Main loop only.
// No heap; host tests build this file (core/mods/tests/test_synthfx.cpp).

#include "modsBuild.h"

#if MT_HAS_SYNTHFX

#include "synthfxMod.h"

#include <cstring>

#if defined(MT_HOST_TEST)
#define MT_SLOTS_SDRAM
#else
#include "config.h"
#include <cr_section_macros.h>
#define MT_SLOTS_SDRAM __NOINIT(EXTERNAL_RAM) // main loop only; init() clears the slots
#endif

namespace mods
{
namespace synthfx
{

MT_SLOTS_SDRAM Slot slots[kParts][kSlots];

namespace
{

bool loadedSince = false;

bool validPart(int part)
{
    return part >= 0 && part < kParts;
}

bool validSlot(int part, int slot)
{
    return validPart(part) && slot >= 0 && slot < kSlots;
}

float noKnob()
{
    uint32_t bits = 0x7FC00000u; // the one NaN a knob ever holds, so records compare by bytes
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

bool finite(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    return (bits & 0x7F800000u) != 0x7F800000u;
}

char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool sameFile(const char* a, const char* b)
{
    for (; *a != 0 && lower(*a) == lower(*b); a++, b++)
    {
    }
    return lower(*a) == lower(*b);
}

// A name in /SynthFX/: printable, no path, ending in .ptsfx, with room for its 0
bool goodFile(const char* file, size_t size)
{
    const void* end = memchr(file, 0, size);
    if (end == nullptr)
    {
        return false;
    }
    const size_t length = static_cast<size_t>(static_cast<const char*>(end) - file);
    static const char kSuffix[] = ".ptsfx";
    const size_t suffix = sizeof(kSuffix) - 1;
    if (length <= suffix || !sameFile(file + length - suffix, kSuffix))
    {
        return false;
    }
    for (size_t i = 0; i < length; i++)
    {
        const unsigned char c = static_cast<unsigned char>(file[i]);
        if (c < 0x20 || c > 0x7E || c == '/' || c == '\\' || c == ':')
        {
            return false;
        }
    }
    return true;
}

void empty(Slot& s)
{
    memset(s.file, 0, sizeof s.file);
    for (float& k : s.knobs)
    {
        k = noKnob();
    }
}

void setFile(Slot& s, const char* file)
{
    empty(s);
    const size_t length = strnlen(file, kFileSize - 1);
    memcpy(s.file, file, length);
}

// ---- version 1: the ten settings' steps, part by part ---------------------------------------

enum V1
{
    V1_EFFECT, V1_RATE, V1_DEPTH, V1_MIX, V1_FEEDBACK, V1_DIST, V1_GAIN, V1_TONE, V1_PLACEMENT, V1_LEVEL, V1_COUNT
};
constexpr uint8_t kV1Count[V1_COUNT] = { 4, 101, 101, 101, 191, 4, 101, 101, 2, 73 };
constexpr uint8_t kV1Init[V1_COUNT] = { 0, 50, 50, 50, 145, 0, 50, 50, 1, 60 };

// The default pack's knobs take the old steps' values exactly (synthfx-sdk/pack/pack.h): a
// percent knob is its step, Feedback its step - 95, Level its step - 60 in 0.5 dB.
void fromV1(const uint8_t* data, Slot (&out)[kParts][kSlots])
{
    for (int part = 0; part < kParts; part++)
    {
        uint8_t v[V1_COUNT];
        for (int k = 0; k < V1_COUNT; k++)
        {
            const uint8_t b = data[part * V1_COUNT + k];
            v[k] = b < kV1Count[k] ? b : kV1Init[k];
        }
        Slot& fx1 = out[part][0];
        Slot& fx2 = out[part][1];
        empty(fx1);
        empty(fx2);
        static const char* const kDists[] = { nullptr, kOverdrive, kDistortion, kBitCrusher };
        if (kDists[v[V1_DIST]] != nullptr)
        {
            setFile(fx1, kDists[v[V1_DIST]]);
            fx1.knobs[0] = static_cast<float>(v[V1_GAIN]);
            fx1.knobs[1] = static_cast<float>(v[V1_TONE]);
            fx1.knobs[2] = static_cast<float>(v[V1_LEVEL] - 60) / 2.0f;
        }
        if (v[V1_EFFECT] == 1)
        {
            setFile(fx2, kChorus);
            fx2.knobs[0] = static_cast<float>(v[V1_RATE]);
            fx2.knobs[1] = static_cast<float>(v[V1_DEPTH]);
            fx2.knobs[2] = static_cast<float>(v[V1_MIX]);
        }
        else if (v[V1_EFFECT] == 2 || v[V1_EFFECT] == 3)
        {
            setFile(fx2, v[V1_EFFECT] == 2 ? kFlanger : kPhaser);
            fx2.knobs[0] = static_cast<float>(v[V1_RATE]);
            fx2.knobs[1] = static_cast<float>(v[V1_DEPTH]);
            fx2.knobs[2] = static_cast<float>(static_cast<int>(v[V1_FEEDBACK]) - 95);
            fx2.knobs[3] = static_cast<float>(v[V1_MIX]);
        }
    }
}

void fromV2(const uint8_t* data, Slot (&out)[kParts][kSlots])
{
    for (int part = 0; part < kParts; part++)
    {
        for (int slot = 0; slot < kSlots; slot++)
        {
            const uint8_t* p = data + (part * kSlots + slot) * kSlotBytes;
            Slot& s = out[part][slot];
            empty(s);
            const char* file = reinterpret_cast<const char*>(p);
            if (!goodFile(file, kFileSize) || (slot == 1 && sameFile(file, out[part][0].file)))
            {
                continue; // a bad name, or the plugin FX1 has: an empty slot
            }
            setFile(s, file);
            for (size_t k = 0; k < kKnobs; k++)
            {
                float f;
                memcpy(&f, p + kFileSize + 4 * k, sizeof f);
                s.knobs[k] = finite(f) ? f : noKnob();
            }
        }
    }
}

// The record as slots: empty if the project has none or a damaged one
void stored(const Store& store, Slot (&out)[kParts][kSlots])
{
    for (auto& part : out)
    {
        for (Slot& s : part)
        {
            empty(s);
        }
    }
    uint8_t version = 0;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, &version, &length);
    if (data == nullptr)
    {
        return;
    }
    if (version == kRecordVersion && length == kRecordSize)
    {
        fromV2(data, out);
    }
    else if (version == kRecordVersionV1 && length == kRecordSizeV1)
    {
        fromV1(data, out);
    }
}

void toBytes(const Slot (&in)[kParts][kSlots], uint8_t* bytes)
{
    for (int part = 0; part < kParts; part++)
    {
        for (int slot = 0; slot < kSlots; slot++)
        {
            uint8_t* p = bytes + (part * kSlots + slot) * kSlotBytes;
            const Slot& s = in[part][slot];
            memset(p, 0, kFileSize);
            memcpy(p, s.file, strnlen(s.file, kFileSize - 1));
            for (size_t k = 0; k < kKnobs; k++)
            {
                const float f = finite(s.knobs[k]) ? s.knobs[k] : noKnob();
                memcpy(p + kFileSize + 4 * k, &f, sizeof f);
            }
        }
    }
}

// Working space (main loop): two sets of slots and two records' bytes, not on the stack
MT_SLOTS_SDRAM Slot scratch[kParts][kSlots];
MT_SLOTS_SDRAM uint8_t nowBytes[kRecordSize];
MT_SLOTS_SDRAM uint8_t beforeBytes[kRecordSize];

} // namespace

void init()
{
    for (auto& part : slots)
    {
        for (Slot& s : part)
        {
            empty(s);
        }
    }
    loadedSince = false;
}

void load(const Store& store)
{
    stored(store, slots);
    loadedSince = true;
}

bool storeIfEdited(Store& store)
{
    toBytes(slots, nowBytes);
    stored(store, scratch);
    toBytes(scratch, beforeBytes);
    if (memcmp(nowBytes, beforeBytes, kRecordSize) == 0)
    {
        return false;
    }
    return store.put(kRecord, kRecordVersion, nowBytes, kRecordSize);
}

bool takeLoaded()
{
    const bool was = loadedSince;
    loadedSince = false;
    return was;
}

void clearSlot(int part, int slot)
{
    if (validSlot(part, slot))
    {
        empty(slots[part][slot]);
    }
}

void setSlotFile(int part, int slot, const char* file)
{
    if (!validSlot(part, slot))
    {
        return;
    }
    if (file == nullptr || !goodFile(file, strnlen(file, kFileSize - 1) + 1))
    {
        empty(slots[part][slot]);
        return;
    }
    setFile(slots[part][slot], file);
}

bool usedByOtherSlot(int part, int slot, const char* file)
{
    if (!validSlot(part, slot) || file == nullptr || file[0] == 0)
    {
        return false;
    }
    return sameFile(slots[part][1 - slot].file, file);
}

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX
