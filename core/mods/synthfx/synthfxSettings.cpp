// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Synth FX's settings (synthfxMod.h): the values the insert reads, their steps, the project
// record and the patch editor's rows. Main loop only, except that the audio interrupt reads
// settings. No heap; host tests build this file (core/mods/tests/test_synthfx.cpp).

#include "modsBuild.h"

#if MT_HAS_SYNTHFX

#include "synthfxMod.h"

#include "synthFx.h"

#include <cstring>

namespace mods
{
namespace synthfx
{

using namespace mtfx;

static_assert(kSettings == FXP_COUNT, "synthFx.h's settings");

// Effect None and Distortion Off; Rate, Depth, Mix, Feedback, Gain and Tone at half, Placement
// Post (the insert is always Post), Level 0 dB: WAVE's and PHASE's Init. Each is on its
// setting's steps (initStep; the host test checks).
#define SYNTHFX_INIT { 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 1.0f, 0.0f }
float settings[kParts][kSettings] = { SYNTHFX_INIT, SYNTHFX_INIT, SYNTHFX_INIT };
#undef SYNTHFX_INIT

namespace
{

// A setting's steps: count of them, the step that is 0, and steps per unit. Its value is
// (step - zero) / perUnit, so Init's values are exact.
struct Steps
{
    uint8_t count;
    uint8_t zero;
    uint8_t perUnit;
    uint8_t init;
};

constexpr Steps kSteps[kSettings] = {
    { FX_TYPES, 0, 1, FX_NONE },         // Effect: None, Chorus, Flanger, Phaser
    { 101, 0, 100, 50 },                 // Rate, 0-100 %
    { 101, 0, 100, 50 },                 // Depth
    { 101, 0, 100, 50 },                 // Mix
    { 191, 95, 100, 145 },               // Feedback, -95 % to +95 %
    { DIST_TYPES, 0, 1, DIST_OFF },      // Distortion: Off, Overdrive, Distortion, Bit Crusher
    { 101, 0, 100, 50 },                 // Gain
    { 101, 0, 100, 50 },                 // Tone
    { 2, 0, 1, PLACE_POST },             // Placement: always Post
    { 73, 60, 2, 60 },                   // Level, -30 dB to +6 dB in 0.5 dB
};

static_assert(FXP_EFFECT == 0 && FXP_FEEDBACK == 4 && FXP_DIST == 5 && FXP_PLACEMENT == 8 && FXP_LEVEL == 9,
              "kSteps is in synthFx.h's order");

constexpr int kRowSettings[kRows] = {
    FXP_DIST, FXP_GAIN, FXP_TONE, FXP_LEVEL, FXP_EFFECT, FXP_RATE, FXP_DEPTH, FXP_FEEDBACK, FXP_MIX,
};

bool validSetting(int setting)
{
    return setting >= 0 && setting < static_cast<int>(kSettings);
}

bool validPart(int part)
{
    return part >= 0 && part < kParts;
}

// The record's bytes, or every one at Init if the project has none or a damaged one. A byte
// out of its setting's range is Init on its own; Placement is always Post.
void stored(const Store& store, uint8_t* bytes)
{
    for (int part = 0; part < kParts; part++)
    {
        for (size_t k = 0; k < kSettings; k++)
        {
            bytes[part * kSettings + k] = kSteps[k].init;
        }
    }
    uint8_t version = 0;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, &version, &length);
    if (data == nullptr || version != kRecordVersion || length != kRecordSize)
    {
        return;
    }
    for (size_t i = 0; i < kRecordSize; i++)
    {
        const size_t k = i % kSettings;
        if (k != FXP_PLACEMENT && data[i] < kSteps[k].count)
        {
            bytes[i] = data[i];
        }
    }
}

} // namespace

int stepCount(int setting)
{
    return validSetting(setting) ? kSteps[setting].count : 1;
}

int initStep(int setting)
{
    return validSetting(setting) ? kSteps[setting].init : 0;
}

int stepOf(int setting, float value)
{
    if (!validSetting(setting))
    {
        return 0;
    }
    const Steps& s = kSteps[setting];
    if (!(value == value)) // NaN
    {
        return s.init;
    }
    float x = value * static_cast<float>(s.perUnit) + static_cast<float>(s.zero);
    const float top = static_cast<float>(s.count - 1);
    x = x < 0.0f ? 0.0f : (x > top ? top : x);
    return static_cast<int>(x + 0.5f);
}

float valueOf(int setting, int step)
{
    if (!validSetting(setting))
    {
        return 0.0f;
    }
    const Steps& s = kSteps[setting];
    step = step < 0 ? 0 : (step >= s.count ? s.count - 1 : step);
    return static_cast<float>(step - s.zero) / static_cast<float>(s.perUnit);
}

void load(const Store& store)
{
    uint8_t bytes[kRecordSize];
    stored(store, bytes);
    for (int part = 0; part < kParts; part++)
    {
        for (size_t k = 0; k < kSettings; k++)
        {
            settings[part][k] = valueOf(static_cast<int>(k), bytes[part * kSettings + k]);
        }
    }
}

bool storeIfEdited(Store& store)
{
    uint8_t now[kRecordSize];
    for (int part = 0; part < kParts; part++)
    {
        for (size_t k = 0; k < kSettings; k++)
        {
            now[part * kSettings + k] = static_cast<uint8_t>(
              k == FXP_PLACEMENT ? static_cast<int>(PLACE_POST) : stepOf(static_cast<int>(k), settings[part][k]));
        }
    }
    uint8_t before[kRecordSize];
    stored(store, before);
    if (memcmp(now, before, kRecordSize) == 0)
    {
        return false;
    }
    return store.put(kRecord, kRecordVersion, now, kRecordSize);
}

int rowSetting(int row)
{
    return row >= 0 && row < kRows ? kRowSettings[row] : static_cast<int>(FXP_EFFECT);
}

bool rowIsList(int row)
{
    const int s = rowSetting(row);
    return s == FXP_EFFECT || s == FXP_DIST;
}

bool rowBipolar(int row)
{
    return rowSetting(row) == FXP_FEEDBACK;
}

bool rowRenames(int row)
{
    return rowSetting(row) == FXP_DIST;
}

int rowStep(int part, int row)
{
    if (!validPart(part))
    {
        return 0;
    }
    const int s = rowSetting(row);
    return stepOf(s, settings[part][s]);
}

void setRowStep(int part, int row, int step)
{
    if (!validPart(part))
    {
        return;
    }
    const int s = rowSetting(row);
    settings[part][s] = valueOf(s, step);
}

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX
