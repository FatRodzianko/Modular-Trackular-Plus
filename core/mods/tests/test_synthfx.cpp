// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for Synth FX's settings: their steps, the project record and the patch editor's
// rows (synthfxSettings.cpp). The insert, the rows' text and the page are tested in
// tools/emu/test_synthfx.py and test_synthfx_page.py. Build and run with
// `make -C core/mods/tests`.

#include "synthfx/synthFx.h"
#include "synthfx/synthfxMod.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
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

constexpr int S = static_cast<int>(kSettings);

void setInit()
{
    for (int part = 0; part < kParts; part++)
    {
        for (int k = 0; k < S; k++)
        {
            settings[part][k] = valueOf(k, initStep(k));
        }
    }
}

bool allInit()
{
    for (int part = 0; part < kParts; part++)
    {
        for (int k = 0; k < S; k++)
        {
            if (settings[part][k] != valueOf(k, initStep(k)))
            {
                return false;
            }
        }
    }
    return true;
}

void testSteps()
{
    // the settings start at Init, and Init is WAVE's: every effect Off
    check(allInit(), "settings start at each setting's Init step");
    check(settings[0][mtfx::FXP_EFFECT] == 0.0f && settings[0][mtfx::FXP_DIST] == 0.0f,
          "Init: Effect None, Distortion Off");
    check(settings[1][mtfx::FXP_PLACEMENT] == 1.0f, "Init: Placement Post");
    check(valueOf(mtfx::FXP_FEEDBACK, initStep(mtfx::FXP_FEEDBACK)) == 0.5f &&
            valueOf(mtfx::FXP_LEVEL, initStep(mtfx::FXP_LEVEL)) == 0.0f &&
            valueOf(mtfx::FXP_MIX, initStep(mtfx::FXP_MIX)) == 0.5f,
          "Init: Feedback +50 %, Level 0 dB, Mix 50 %, exactly");

    // the ranges WAVE's FX section has
    check(valueOf(mtfx::FXP_FEEDBACK, 0) == -0.95f && valueOf(mtfx::FXP_FEEDBACK, 190) == 0.95f,
          "Feedback -95 % to +95 %");
    check(valueOf(mtfx::FXP_LEVEL, 0) == -30.0f && valueOf(mtfx::FXP_LEVEL, 72) == 6.0f &&
            valueOf(mtfx::FXP_LEVEL, 61) == 0.5f,
          "Level -30 dB to +6 dB in 0.5 dB");
    check(valueOf(mtfx::FXP_RATE, 0) == 0.0f && valueOf(mtfx::FXP_RATE, 100) == 1.0f &&
            stepCount(mtfx::FXP_RATE) == 101,
          "Rate 0-100 % in 1 %");
    check(stepCount(mtfx::FXP_EFFECT) == 4 && stepCount(mtfx::FXP_DIST) == 4, "four effects, four distortions");

    // every step is on the record's byte and comes back
    bool roundTrip = true;
    for (int k = 0; k < S; k++)
    {
        check(stepCount(k) >= 2 && stepCount(k) <= 256, "setting " + std::to_string(k) + " fits a byte");
        for (int i = 0; i < stepCount(k); i++)
        {
            roundTrip = roundTrip && stepOf(k, valueOf(k, i)) == i;
        }
    }
    check(roundTrip, "every step's value is on that step");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    check(stepOf(mtfx::FXP_MIX, nan) == initStep(mtfx::FXP_MIX), "NaN is Init");
    check(stepOf(mtfx::FXP_MIX, inf) == 100 && stepOf(mtfx::FXP_MIX, 1e30f) == 100, "+inf and 1e30 are the top");
    check(stepOf(mtfx::FXP_FEEDBACK, -inf) == 0 && stepOf(mtfx::FXP_FEEDBACK, -7.5f) == 0, "-inf is the bottom");
    check(stepOf(mtfx::FXP_LEVEL, 0.26f) == 61 && stepOf(mtfx::FXP_LEVEL, 0.24f) == 60, "to the nearest step");
    check(valueOf(mtfx::FXP_EFFECT, 99) == 3.0f && valueOf(mtfx::FXP_EFFECT, -4) == 0.0f, "valueOf clamps");
    check(stepOf(-1, 0.5f) == 0 && valueOf(S, 3) == 0.0f && stepCount(S) == 1, "no setting out of range");
}

void testRecord()
{
    uint8_t buffer[512];
    Store store(buffer, sizeof(buffer), "TEST");

    setInit();
    check(!storeIfEdited(store) && store.find(kRecord, nullptr, nullptr) == nullptr,
          "Init settings write no record into a project without one");

    settings[0][mtfx::FXP_EFFECT] = 3.0f;
    settings[0][mtfx::FXP_FEEDBACK] = -0.95f;
    settings[2][mtfx::FXP_DIST] = 2.0f;
    settings[2][mtfx::FXP_LEVEL] = 6.0f;
    settings[1][mtfx::FXP_MIX] = 0.37f;
    check(storeIfEdited(store), "an edit is stored");
    uint8_t version = 0;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, &version, &length);
    check(data != nullptr && version == 1 && length == 30, "record 16, version 1, 30 bytes");
    if (data != nullptr)
    {
        check(data[0] == 3 && data[4] == 0 && data[20 + 5] == 2 && data[20 + 9] == 72 && data[10 + 3] == 37 &&
                data[8] == 1 && data[10 + 1] == 50,
              "each setting's step, part by part");
    }
    check(!storeIfEdited(store), "nothing new: no write");

    setInit();
    load(store);
    check(settings[0][mtfx::FXP_EFFECT] == 3.0f && settings[0][mtfx::FXP_FEEDBACK] == -0.95f &&
            settings[2][mtfx::FXP_DIST] == 2.0f && settings[2][mtfx::FXP_LEVEL] == 6.0f &&
            settings[1][mtfx::FXP_MIX] == 0.37f && settings[1][mtfx::FXP_RATE] == 0.5f,
          "load gives the settings back");

    setInit();
    check(storeIfEdited(store), "back to Init after a record: stored");
    load(store);
    check(allInit(), "and loads as Init");

    // projects without the record, or a damaged one
    Store empty(buffer, sizeof(buffer), "TEST");
    empty.clear();
    settings[0][mtfx::FXP_EFFECT] = 2.0f;
    load(empty);
    check(allInit(), "no record: every effect Off");

    uint8_t bytes[kRecordSize];
    for (size_t i = 0; i < kRecordSize; i++)
    {
        bytes[i] = static_cast<uint8_t>(initStep(static_cast<int>(i % kSettings)));
    }
    bytes[0] = 1;
    empty.put(kRecord, 2, bytes, kRecordSize);
    settings[0][mtfx::FXP_EFFECT] = 2.0f;
    load(empty);
    check(allInit(), "another version: every effect Off");
    empty.put(kRecord, kRecordVersion, bytes, kRecordSize - 1);
    load(empty);
    check(allInit(), "another length: every effect Off");

    bytes[0] = 4;            // Effect out of range
    bytes[1] = 77;           // Rate 77 %
    bytes[8] = 0;            // Placement Pre: the insert has none
    bytes[10 + 4] = 191;     // Feedback out of range
    bytes[20 + 9] = 0;       // Level -30 dB
    empty.put(kRecord, kRecordVersion, bytes, kRecordSize);
    load(empty);
    check(settings[0][mtfx::FXP_EFFECT] == 0.0f && settings[0][mtfx::FXP_RATE] == 0.77f &&
            settings[0][mtfx::FXP_PLACEMENT] == 1.0f && settings[1][mtfx::FXP_FEEDBACK] == 0.5f &&
            settings[2][mtfx::FXP_LEVEL] == -30.0f,
          "a value out of range is Init on its own; Placement is always Post");

    // garbage in RAM (the audio side copes with it; the record holds steps)
    settings[1][mtfx::FXP_DEPTH] = std::numeric_limits<float>::quiet_NaN();
    settings[1][mtfx::FXP_GAIN] = 1e30f;
    check(storeIfEdited(empty), "garbage stored as steps");
    load(empty);
    check(settings[1][mtfx::FXP_DEPTH] == 0.5f && settings[1][mtfx::FXP_GAIN] == 1.0f,
          "NaN as Init, 1e30 as the top");
    setInit();
}

void testRows()
{
    std::set<int> seen;
    bool listsRight = true;
    for (int row = 0; row < kRows; row++)
    {
        const int s = rowSetting(row);
        seen.insert(s);
        listsRight = listsRight && rowIsList(row) == (s == mtfx::FXP_EFFECT || s == mtfx::FXP_DIST);
    }
    check(seen.size() == kRows && seen.count(mtfx::FXP_PLACEMENT) == 0, "nine rows, each setting once, no Placement");
    check(listsRight, "Distortion and Effect are lists");
    check(rowSetting(0) == mtfx::FXP_DIST && rowSetting(4) == mtfx::FXP_EFFECT && rowSetting(8) == mtfx::FXP_MIX,
          "WAVE's order: the Distortion's rows, then the effect's");
    check(rowBipolar(7) && rowSetting(7) == mtfx::FXP_FEEDBACK, "Feedback is bipolar");

    setInit();
    setRowStep(1, 4, 2);
    check(settings[1][mtfx::FXP_EFFECT] == 2.0f && rowStep(1, 4) == 2, "a row's step sets its setting");
    setRowStep(1, 4, 99);
    check(rowStep(1, 4) == 3, "clamped at the top");
    setRowStep(1, 7, -5);
    check(settings[1][mtfx::FXP_FEEDBACK] == -0.95f, "and at the bottom");
    setRowStep(-1, 4, 1);
    setRowStep(kParts, 4, 1);
    check(rowStep(-1, 4) == 0 && rowStep(kParts, 0) == 0 && settings[0][mtfx::FXP_EFFECT] == 0.0f &&
            settings[2][mtfx::FXP_EFFECT] == 0.0f,
          "no part out of range");
    setInit();
}

} // namespace

int main()
{
    testSteps();
    testRecord();
    testRows();
    printf("synthfx: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
