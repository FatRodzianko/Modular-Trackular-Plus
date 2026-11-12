// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for Synth FX's slot settings and the project record (synthfxSettings.cpp):
// version 3 (with each slot's Placement), version 2 and version 1 converted, damaged records, the
// one-use rule, and an old WAVE or PHASE patch's effects moved into the slots. The slots' plugins,
// the audio path and the page are tested in tools/emu/test_synthfx.py and test_synthfx_page.py.
// Build and run with `make -C core/mods/tests`.

#include "synthfx/synthfxMod.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

using namespace mods;
using namespace mods::synthfx;

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

bool isNoKnob(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    return bits == 0x7FC00000u;
}

bool slotEmpty(int part, int slot)
{
    const Slot& s = slots[part][slot];
    if (s.file[0] != 0)
    {
        return false;
    }
    for (float k : s.knobs)
    {
        if (!isNoKnob(k))
        {
            return false;
        }
    }
    return true;
}

bool allEmpty()
{
    for (int part = 0; part < kParts; part++)
    {
        for (int slot = 0; slot < kSlots; slot++)
        {
            if (!slotEmpty(part, slot))
            {
                return false;
            }
        }
    }
    return true;
}

void clearAll()
{
    for (int part = 0; part < kParts; part++)
    {
        for (int slot = 0; slot < kSlots; slot++)
        {
            clearSlot(part, slot);
        }
    }
}

bool knobsAre(int part, int slot, std::initializer_list<float> want)
{
    size_t k = 0;
    for (float w : want)
    {
        if (slots[part][slot].knobs[k++] != w)
        {
            return false;
        }
    }
    for (; k < kKnobs; k++)
    {
        if (!isNoKnob(slots[part][slot].knobs[k]))
        {
            return false;
        }
    }
    return true;
}

// As knobsAre, within float rounding (an old patch's values are scaled; the plugin snaps them
// onto its steps when it is made)
bool knobsNear(int part, int slot, std::initializer_list<float> want)
{
    size_t k = 0;
    for (float w : want)
    {
        if (std::fabs(slots[part][slot].knobs[k++] - w) > 1e-3f)
        {
            return false;
        }
    }
    for (; k < kKnobs; k++)
    {
        if (!isNoKnob(slots[part][slot].knobs[k]))
        {
            return false;
        }
    }
    return true;
}

// A record's slot bytes (versions 2 and 3 alike) from a slot list
void putSlot(uint8_t* record, int part, int slot, const char* file, std::initializer_list<float> knobs)
{
    uint8_t* p = record + (part * kSlots + slot) * kSlotBytes;
    memset(p, 0, kSlotBytes);
    memcpy(p, file, strnlen(file, kFileSize));
    size_t k = 0;
    const float none = std::numeric_limits<float>::quiet_NaN();
    for (; k < kKnobs; k++)
    {
        const float f = k < knobs.size() ? knobs.begin()[k] : none;
        memcpy(p + kFileSize + 4 * k, &f, 4);
    }
}

void testSlots()
{
    memset(slots, 0x5A, sizeof slots); // as SDRAM may hold at power-up
    init();
    check(allEmpty(), "init: every slot empty, every knob the default (NaN)");
    clearAll();
    setSlotFile(1, 0, "Chorus.ptsfx");
    check(strcmp(slots[1][0].file, "Chorus.ptsfx") == 0 && knobsAre(1, 0, {}), "a file set: its knobs the defaults");
    check(usedByOtherSlot(1, 1, "chorus.PTSFX") && !usedByOtherSlot(1, 0, "Chorus.ptsfx") &&
            !usedByOtherSlot(0, 1, "Chorus.ptsfx") && !usedByOtherSlot(1, 1, ""),
          "the one-use rule: the other slot of the same part, case aside");
    setSlotFile(1, 1, "../Chorus.ptsfx");
    check(slotEmpty(1, 1), "a path is no file name");
    setSlotFile(1, 1, "Delay.wav");
    check(slotEmpty(1, 1), "a name not ending .ptsfx");
    setSlotFile(1, 1, ".ptsfx");
    check(slotEmpty(1, 1), "a suffix alone");
    const std::string longName = std::string(42, 'a') + ".ptsfx"; // 48 with its 0: too long
    setSlotFile(1, 1, longName.c_str());
    check(slotEmpty(1, 1), "a name with no room for its 0");
    const std::string longest = std::string(41, 'a') + ".ptsfx"; // 47
    setSlotFile(1, 1, longest.c_str());
    check(longest == slots[1][1].file, "the longest name that fits");
    setSlotFile(-1, 0, "Chorus.ptsfx");
    setSlotFile(kParts, 0, "Chorus.ptsfx");
    setSlotFile(0, kSlots, "Chorus.ptsfx");
    clearSlot(-1, 0);
    check(slotEmpty(0, 0) && slotEmpty(0, 1) && slotEmpty(2, 0), "no part or slot out of range");
    clearSlot(1, 1);
    clearSlot(1, 0);
    check(allEmpty(), "cleared");
}

void testRecord()
{
    static uint8_t buffer[4096];
    Store store(buffer, sizeof(buffer), "TEST");
    clearAll();
    load(store);
    check(takeLoaded() && !takeLoaded(), "load marks the slots to be made, once");
    check(!storeIfEdited(store) && store.find(kRecord, nullptr, nullptr) == nullptr,
          "all empty and no record: nothing written");

    setSlotFile(0, 0, "Overdrive.ptsfx");
    slots[0][0].knobs[0] = 80.0f;
    slots[0][0].knobs[2] = -4.5f;
    setSlotFile(2, 1, "Delay.ptsfx");
    slots[2][1].knobs[11] = 1.25f;
    setSlotPre(2, 1, true);
    setSlotPre(1, 0, true); // empty: stays Post
    check(slots[2][1].pre == 1 && slots[1][0].pre == 0, "Placement Pre set; an empty slot stays Post");
    check(storeIfEdited(store), "an edit is stored");
    uint8_t version = 0;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, &version, &length);
    check(data != nullptr && version == 3 && length == 582 && kRecordSize == 582 && kRecordSizeV2 == 576,
          "record 16, version 3, 582 bytes (version 2's 576, then the Placements)");
    if (data != nullptr)
    {
        check(data[576] == 0 && data[577] == 0 && data[578] == 0 && data[579] == 0 && data[580] == 0 &&
                data[581] == 1,
              "the Placements: a byte for each slot, part by part, FX1 then FX2 (1: Pre)");
    }
    if (data != nullptr)
    {
        float k0 = 0;
        float k11 = 0;
        uint32_t k1 = 0;
        memcpy(&k0, data + 48, 4);
        memcpy(&k1, data + 52, 4);
        memcpy(&k11, data + (2 * 2 + 1) * 96 + 48 + 44, 4);
        check(strcmp(reinterpret_cast<const char*>(data), "Overdrive.ptsfx") == 0 && data[47] == 0 && k0 == 80.0f &&
                k1 == 0x7FC00000u && strcmp(reinterpret_cast<const char*>(data + 5 * 96), "Delay.ptsfx") == 0 &&
                k11 == 1.25f && data[96] == 0,
              "the layout: part by part, FX1 then FX2, the name in 48 bytes, twelve floats");
        bool zeros = true;
        for (size_t i = strlen("Overdrive.ptsfx"); i < 48; i++)
        {
            zeros = zeros && data[i] == 0;
        }
        check(zeros, "zeros after the name");
    }
    check(!storeIfEdited(store), "nothing new: no write");

    clearAll();
    load(store);
    check(strcmp(slots[0][0].file, "Overdrive.ptsfx") == 0 && slots[0][0].knobs[0] == 80.0f &&
            isNoKnob(slots[0][0].knobs[1]) && slots[0][0].knobs[2] == -4.5f && slotEmpty(0, 1) &&
            strcmp(slots[2][1].file, "Delay.ptsfx") == 0 && slots[2][1].knobs[11] == 1.25f && slotEmpty(1, 0) &&
            slots[2][1].pre == 1 && slots[0][0].pre == 0,
          "it loads as it was saved, Placements too");
    setSlotPre(2, 1, false);
    check(storeIfEdited(store), "Placement back to Post: an edit");
    clearAll();
    load(store);
    check(slots[2][1].pre == 0 && strcmp(slots[2][1].file, "Delay.ptsfx") == 0, "and loads as Post");

    clearAll();
    check(storeIfEdited(store), "all cleared after a record: stored");
    load(store);
    check(allEmpty(), "and loads as all empty");

    Store empty(buffer, sizeof(buffer), "TEST");
    setSlotFile(0, 0, "Chorus.ptsfx");
    load(empty);
    check(allEmpty(), "no record: every slot empty");

    static uint8_t record[kRecordSize];
    memset(record, 0, sizeof record);
    putSlot(record, 0, 0, "Chorus.ptsfx", { 1, 2, 3 });
    empty.put(kRecord, 4, record, kRecordSize);
    load(empty);
    check(allEmpty(), "another version: every slot empty");
    empty.put(kRecord, kRecordVersion, record, kRecordSize - 1);
    load(empty);
    check(allEmpty(), "another length: every slot empty");
    empty.put(kRecord, kRecordVersionV2, record, kRecordSize);
    load(empty);
    check(allEmpty(), "version 2 at version 3's length: every slot empty");

    // version 2: the slots, every one Post
    memset(record, 0, sizeof record);
    putSlot(record, 0, 0, "Distortion.ptsfx", { 80, 40, -3 });
    putSlot(record, 1, 1, "Chorus.ptsfx", { 1, 2, 3 });
    record[kRecordSizeV2] = 1; // past version 2's end: not read
    empty.put(kRecord, kRecordVersionV2, record, kRecordSizeV2);
    for (int part = 0; part < kParts; part++)
    {
        for (int slot = 0; slot < kSlots; slot++)
        {
            slots[part][slot].pre = 1;
        }
    }
    load(empty);
    check(strcmp(slots[0][0].file, "Distortion.ptsfx") == 0 && knobsAre(0, 0, { 80, 40, -3 }) &&
            strcmp(slots[1][1].file, "Chorus.ptsfx") == 0 && slots[0][0].pre == 0 && slots[1][1].pre == 0 &&
            slots[2][0].pre == 0,
          "version 2: its slots, every one Post");
    check(!storeIfEdited(empty), "version 2 not edited: left as it is");
    setSlotPre(0, 0, true);
    check(storeIfEdited(empty), "edited: stored");
    empty.find(kRecord, &version, &length);
    check(version == kRecordVersion && length == kRecordSize, "as version 3");

    // version 3's Placement bytes: Pre only as 1, and only on a slot with a plugin
    memset(record, 0, sizeof record);
    putSlot(record, 0, 0, "Distortion.ptsfx", { 1 });
    putSlot(record, 0, 1, "Chorus.ptsfx", { 1 });
    putSlot(record, 1, 0, "Overdrive.ptsfx", { 1 });
    record[kRecordSizeV2 + 0] = 1;
    record[kRecordSizeV2 + 1] = 7;   // not 1: Post
    record[kRecordSizeV2 + 2] = 1;
    record[kRecordSizeV2 + 3] = 1;   // an empty slot: Post
    empty.put(kRecord, kRecordVersion, record, kRecordSize);
    load(empty);
    check(slots[0][0].pre == 1 && slots[0][1].pre == 0 && slots[1][0].pre == 1 && slots[1][1].pre == 0,
          "version 3: Pre only as 1, and only on a slot with a plugin");
}

void testDamaged()
{
    static uint8_t buffer[4096];
    Store store(buffer, sizeof(buffer), "TEST");
    static uint8_t record[kRecordSize];
    memset(record, 0, sizeof record);
    const float inf = std::numeric_limits<float>::infinity();
    putSlot(record, 0, 0, "Chorus.ptsfx", { 10, inf, -inf, std::numeric_limits<float>::quiet_NaN(), 5 });
    putSlot(record, 0, 1, "CHORUS.PTSFX", { 1 });   // the same as FX1
    putSlot(record, 1, 0, "a/b.ptsfx", { 1 });      // a path
    putSlot(record, 1, 1, "Phaser.ptsfx", { 7 });
    putSlot(record, 2, 0, "Flanger.ptsfx", { 2 });
    memset(record + (2 * kSlots + 0) * kSlotBytes, 'x', kFileSize); // no 0 in the name
    putSlot(record, 2, 1, "Tab\t.ptsfx", { 1 });    // a control character
    uint8_t* stray = record + (1 * kSlots + 1) * kSlotBytes;
    stray[strlen("Phaser.ptsfx") + 3] = 'z';        // a byte after the 0
    // a NaN with another payload: stored as the one NaN
    const uint32_t odd = 0x7FC12345u;
    memcpy(record + (1 * kSlots + 1) * kSlotBytes + kFileSize + 4, &odd, 4);
    store.put(kRecord, kRecordVersion, record, kRecordSize);
    load(store);
    check(strcmp(slots[0][0].file, "Chorus.ptsfx") == 0 && slots[0][0].knobs[0] == 10.0f && isNoKnob(slots[0][0].knobs[1]) && isNoKnob(slots[0][0].knobs[2]) &&
            isNoKnob(slots[0][0].knobs[3]) && slots[0][0].knobs[4] == 5.0f,
          "infinities and NaNs are the default");
    check(slotEmpty(0, 1), "the plugin FX1 has, in FX2 as well: FX2 empty");
    check(slotEmpty(1, 0), "a path: empty");
    check(strcmp(slots[1][1].file, "Phaser.ptsfx") == 0 && slots[1][1].knobs[0] == 7.0f &&
            isNoKnob(slots[1][1].knobs[1]),
          "a stray byte after the name's 0 is ignored; any NaN is the default");
    check(slotEmpty(2, 0), "a name with no 0: empty");
    check(slotEmpty(2, 1), "a control character: empty");
    check(!storeIfEdited(store), "a damaged record, loaded and not edited: left (it loads the same)");
    slots[1][1].knobs[0] = 8.0f;
    check(storeIfEdited(store), "edited: stored");
    uint8_t version = 0;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, &version, &length);
    check(data != nullptr && data[(1 * kSlots + 1) * kSlotBytes + strlen("Phaser.ptsfx") + 3] == 0,
          "stored clean: zeros after the name");
    check(!storeIfEdited(store), "then nothing new");
}

// Version 1: per part, the steps of Effect, Rate, Depth, Mix, Feedback, Distortion, Gain, Tone,
// Placement, Level
void testVersion1()
{
    static uint8_t buffer[4096];
    Store store(buffer, sizeof(buffer), "TEST");
    const uint8_t v1[kRecordSizeV1] = {
        1, 20, 30, 40, 0, 1, 70, 80, 1, 72,      // part 0: Chorus, Overdrive at +6 dB
        3, 0, 100, 37, 190, 3, 0, 100, 1, 0,     // part 1: Phaser at Feedback +95, Bit Crusher at -30 dB
        2, 50, 50, 50, 145, 2, 50, 50, 0, 60,    // part 2: Flanger, Distortion
    };
    store.put(kRecord, kRecordVersionV1, v1, kRecordSizeV1);
    clearAll();
    load(store);
    check(strcmp(slots[0][0].file, kOverdrive) == 0 && knobsAre(0, 0, { 70, 80, 6.0f }),
          "version 1: the Distortion in FX1, Gain, Tone and Level as the plugin's knobs");
    check(strcmp(slots[0][1].file, kChorus) == 0 && knobsAre(0, 1, { 20, 30, 40 }),
          "the Chorus in FX2: Rate, Depth, Mix (no Feedback)");
    check(strcmp(slots[1][0].file, kBitCrusher) == 0 && knobsAre(1, 0, { 0, 100, -30.0f }),
          "Bit Crusher at the ends");
    check(strcmp(slots[1][1].file, kPhaser) == 0 && knobsAre(1, 1, { 0, 100, 95, 37 }),
          "Phaser: Rate, Depth, Feedback (step - 95), Mix");
    check(strcmp(slots[2][0].file, kDistortion) == 0 && knobsAre(2, 0, { 50, 50, 0.0f }) &&
            strcmp(slots[2][1].file, kFlanger) == 0 && knobsAre(2, 1, { 50, 50, 50, 50 }),
          "Distortion and Flanger at Init: the pack's defaults");
    check(!storeIfEdited(store), "a version 1 record not edited: left as it is");
    slots[0][1].knobs[0] = 21.0f;
    check(storeIfEdited(store), "edited: stored");
    uint8_t version = 0;
    uint16_t length = 0;
    store.find(kRecord, &version, &length);
    check(version == kRecordVersion && length == kRecordSize, "as version 3");
    clearAll();
    load(store);
    check(strcmp(slots[0][1].file, kChorus) == 0 && knobsAre(0, 1, { 21, 30, 40 }) &&
            strcmp(slots[1][1].file, kPhaser) == 0 && knobsAre(1, 1, { 0, 100, 95, 37 }),
          "and loads back the same");

    const uint8_t off[kRecordSizeV1] = {
        0, 50, 50, 50, 145, 0, 50, 50, 1, 60,
        0, 9, 9, 9, 9, 0, 9, 9, 1, 9,            // nothing chosen, other values moved
        200, 255, 101, 101, 191, 4, 101, 101, 2, 73, // every byte out of range: Init, so nothing
    };
    store.put(kRecord, kRecordVersionV1, off, kRecordSizeV1);
    load(store);
    check(allEmpty(), "version 1 with nothing chosen, or out of range: every slot empty");
    const uint8_t outOfRange[kRecordSizeV1] = {
        1, 200, 50, 50, 145, 1, 50, 255, 1, 99,   // Rate and Tone and Level out of range: Init
        0, 50, 50, 50, 145, 0, 50, 50, 1, 60,
        0, 50, 50, 50, 145, 0, 50, 50, 1, 60,
    };
    store.put(kRecord, kRecordVersionV1, outOfRange, kRecordSizeV1);
    load(store);
    check(knobsAre(0, 1, { 50, 50, 50 }) && knobsAre(0, 0, { 50, 50, 0.0f }),
          "a version 1 value out of range is its Init");
}

// An old WAVE or PHASE patch's ten effect parameters (62-71, as the patch holds them) into the slots
void testOldPatch()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // Effect, Rate, Depth, Mix, Feedback, Distortion, Gain, Tone, Placement, Level
    const float none[kOldCount] = { 0, 0.5f, 0.5f, 0.5f, 0.5f, 0, 0.5f, 0.5f, 1, 0 };
    check(!oldPatchHasEffects(none) && !oldPatchHasEffects(nullptr), "Effect None and Distortion Off: nothing to move");
    const float missing[kOldCount] = { nan, nan, nan, nan, nan, nan, nan, nan, nan, nan };
    check(!oldPatchHasEffects(missing), "no values (a patch without them): nothing to move");
    const float low[kOldCount] = { 0.4f, 0.5f, 0.5f, 0.5f, 0.5f, 0.4f, 0.5f, 0.5f, 1, 0 };
    check(!oldPatchHasEffects(low), "values that round to None and Off: nothing");

    const float both[kOldCount] = { 3, 0.2f, 0.7f, 1.0f, -0.6f, 2, 1.0f, 0.3f, 0, -4.5f };
    check(oldPatchHasEffects(both), "a Distortion and a Phaser: to move");
    clearAll();
    setSlotFile(0, 0, "Chorus.ptsfx");
    fromOldPatch(0, both, true);
    check(strcmp(slots[0][0].file, kDistortion) == 0 && knobsNear(0, 0, { 100, 30, -4.5f }) && slots[0][0].pre == 1,
          "WAVE: the Distortion in FX1, Gain and Tone in percent, Level in dB, Placement Pre kept");
    check(strcmp(slots[0][1].file, kPhaser) == 0 && knobsNear(0, 1, { 20, 70, -60, 100 }),
          "the Phaser in FX2: Rate, Depth, Feedback, Mix in percent (the slot's Chorus replaced)");
    fromOldPatch(1, both, false);
    check(strcmp(slots[1][0].file, kDistortion) == 0 && slots[1][0].pre == 0, "PHASE: the same, Post");
    const float post[kOldCount] = { 0, 0.5f, 0.5f, 0.5f, 0.5f, 1, 0.5f, 0.5f, 1, 0 };
    fromOldPatch(2, post, true);
    check(strcmp(slots[2][0].file, kOverdrive) == 0 && slots[2][0].pre == 0 && slotEmpty(2, 1),
          "WAVE, Placement Post: FX1 Post; Effect None: FX2 empty");

    const float effectOnly[kOldCount] = { 1, 0.1f, 0.2f, 0.3f, 0.5f, 0, 0.5f, 0.5f, 0, 0 };
    clearAll();
    fromOldPatch(0, effectOnly, true);
    check(slotEmpty(0, 0) && slots[0][0].pre == 0 && strcmp(slots[0][1].file, kChorus) == 0 &&
            knobsNear(0, 1, { 10, 20, 30 }),
          "the Chorus alone: FX1 empty (and Post, though Placement was Pre), FX2 Rate, Depth, Mix");
    const float odd[kOldCount] = { 2, nan, 0.5f, 0.5f, std::numeric_limits<float>::infinity(), 3, 0.5f, 0.5f, 0, 0 };
    fromOldPatch(1, odd, true);
    check(strcmp(slots[1][0].file, kBitCrusher) == 0 && strcmp(slots[1][1].file, kFlanger) == 0 &&
            isNoKnob(slots[1][1].knobs[0]) && isNoKnob(slots[1][1].knobs[2]) && slots[1][1].knobs[1] == 50.0f,
          "a value not finite: the plugin's default");
    fromOldPatch(kParts, both, true);
    fromOldPatch(-1, both, true);
    fromOldPatch(0, nullptr, true);
    check(slotEmpty(0, 0) && strcmp(slots[0][1].file, kChorus) == 0 && slotEmpty(2, 0),
          "no part out of range, no values: nothing");
}

} // namespace

int main()
{
    testSlots();
    testRecord();
    testDamaged();
    testVersion1();
    testOldPatch();
    printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
