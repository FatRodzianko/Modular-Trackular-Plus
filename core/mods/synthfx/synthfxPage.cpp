// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Synth FX's rows on the patch editor (synthfxMod.h): their names and value texts, as WAVE's
// and PHASE's FX section shows them (synths/synthsSdk.cpp). Main loop only.

#include "modsBuild.h"

#if MT_HAS_SYNTHFX

#include "synthfxMod.h"

#include "synthFx.h"

#include <cstdio>

namespace mods
{
namespace synthfx
{

using namespace mtfx;

namespace
{

const char* const kEffects[FX_TYPES] = { "None", "Chorus", "Flanger", "Phaser" };
const char* const kDistortions[DIST_TYPES] = { "Off", "Overdrive", "Distortion", "Bit Crusher" };

bool crusher(int part)
{
    return part >= 0 && part < kParts && list_pick(settings[part][FXP_DIST], DIST_TYPES) == DIST_CRUSHER;
}

uint32_t distType(int part)
{
    return part >= 0 && part < kParts ? list_pick(settings[part][FXP_DIST], DIST_TYPES) : DIST_OFF;
}

void hzText(float hz, char* out, size_t size)
{
    if (hz < 10.0f)
        snprintf(out, size, "%.2f Hz", hz);
    else if (hz < 100.0f)
        snprintf(out, size, "%.1f Hz", hz);
    else if (hz < 1000.0f)
        snprintf(out, size, "%.0f Hz", hz);
    else
        snprintf(out, size, "%.1f kHz", hz * 0.001f);
}

void percentText(float v, bool signedRange, char* out, size_t size)
{
    const int pc = static_cast<int>(v * 100.0f + (v < 0.0f ? -0.5f : 0.5f));
    snprintf(out, size, signedRange && pc > 0 ? "+%d %%" : "%d %%", pc);
}

} // namespace

const char* rowName(int part, int row)
{
    switch (rowSetting(row))
    {
    case FXP_DIST: return "Distortion";
    case FXP_GAIN: return crusher(part) ? "Bits" : "Gain";
    case FXP_TONE: return crusher(part) ? "Rate" : "Tone";
    case FXP_LEVEL: return "Level";
    case FXP_EFFECT: return "Effect";
    case FXP_RATE: return "Rate";
    case FXP_DEPTH: return "Depth";
    case FXP_FEEDBACK: return "Feedback";
    case FXP_MIX: return "FX Mix";
    default: return "";
    }
}

void rowText(int part, int row, int step, char* out, size_t size)
{
    if (out == nullptr || size == 0)
    {
        return;
    }
    const int s = rowSetting(row);
    const float v = valueOf(s, step);
    switch (s)
    {
    case FXP_EFFECT: snprintf(out, size, "%s", kEffects[list_pick(v, FX_TYPES)]); break;
    case FXP_DIST: snprintf(out, size, "%s", kDistortions[list_pick(v, DIST_TYPES)]); break;
    case FXP_RATE: hzText(rate_hz(v), out, size); break;
    case FXP_LEVEL: snprintf(out, size, "%+.1f dB", v); break;
    case FXP_FEEDBACK: percentText(v, true, out, size); break;
    case FXP_GAIN:
        switch (distType(part))
        {
        case DIST_CRUSHER: snprintf(out, size, "%.1f bit", crush_bits(v)); break;
        case DIST_OVERDRIVE: snprintf(out, size, "+%.0f dB", 6.0206f * (1.0f + v * 5.8826430f)); break;
        case DIST_DISTORTION: snprintf(out, size, "+%.0f dB", 10.0f + v * 40.0f); break;
        default: percentText(v, false, out, size); break;
        }
        break;
    case FXP_TONE:
        if (crusher(part))
            hzText(crush_hz(v), out, size);
        else
            percentText(v, false, out, size);
        break;
    default: percentText(v, false, out, size); break;
    }
}

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX
