// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// WAVE's and PHASE's shared SDK glue (synthsSdk.h). Main loop only, except lfoRatio.

#include "modsBuild.h"

#if MT_HAS_SYNTHS

#include "config.h"
#ifdef FEATURE_SYNTH

#include "synthsSdk.h"

#include <fmt/format.h>
#include <string>

namespace mtsynth
{

using polysynth::Patch;
using Curve = Patch::Metadata::Param::Curve;

namespace
{

const char* const kOffOn[] = { "Off", "On" };
const char* const kWaveforms[] = { "Triangle", "Sin", "Ramp Up", "Ramp Down", "Square", "Random S&H" };
const char* const kRetrigger[] = { "Note", "One Shot", "Free Running" };
const char* const kPolarity[] = { "Bipolar", "Unipolar" };
const char* const kGlideModes[] = { "Always", "Overlap", "Legato", "Legato O." };
const char* const kDivisions[16] = { "1",     "1/2 D", "1/2",  "1/4 D",  "1 T",  "1/4",
                                     "1/8 D", "1/2 T", "1/8",  "1/16 D", "1/4 T", "1/16",
                                     "1/8 T", "1/32",  "1/16 T", "1/32 T" };
const char* const kEffects[FX_TYPES] = { "None", "Chorus", "Flanger", "Phaser" };
const char* const kDistortions[DIST_TYPES] = { "Off", "Overdrive", "Distortion", "Bit Crusher" };
const char* const kPlacements[] = { "Pre", "Post" };

constexpr float kRatioMinHz = 0.01f;
constexpr float kRatioOctaves = 13.287712f;        /* log2(100 / 0.01): Ratio 1 is 100 Hz */

float valueOf(const Patch& patch, int id, float otherwise)
{
    auto v = patch.getParamValue(id);
    return v ? static_cast<float>(*v) : otherwise;
}

uint32_t distType(const Patch& patch)
{
    return list_pick(valueOf(patch, P_DIST, 0.0f), DIST_TYPES);
}

std::string hzText(float hz)
{
    return hz < 10.0f    ? fmt::format("{:.2f} Hz", hz)
           : hz < 100.0f ? fmt::format("{:.1f} Hz", hz)
           : hz < 1000.0f ? fmt::format("{:.0f} Hz", hz)
                          : fmt::format("{:.1f} kHz", hz * 0.001f);
}

std::string percentText(float v, bool signedRange)
{
    const int pc = static_cast<int>(v * 100.0f + (v < 0.0f ? -0.5f : 0.5f));
    return (signedRange && pc > 0) ? fmt::format("+{:d} %", pc) : fmt::format("{:d} %", pc);
}

} // namespace

float lfoRatio(float v, bool sync)
{
    v = clampf(v, 0.0f, 1.0f);
    if (sync)
        return static_cast<float>(static_cast<int>(v * 15.0f + 0.5f));
    return kRatioMinHz * exp2f_fast(v * kRatioOctaves);
}

const char* distKnobName(const Patch& patch, bool tone)
{
    if (distType(patch) == DIST_CRUSHER)
        return tone ? "Rate" : "Bits";
    return tone ? "Tone" : "Gain";
}

Patch::Metadata::Param makeParam(const Spec& s)
{
    Patch::Metadata::Param p{};
    p.displayName = s.name ? s.name : "";
    p.defaultValue = s.def;
    p.minValue = static_cast<Patch::paramvalue_short_t>(s.min);
    p.maxValue = static_cast<Patch::paramvalue_short_t>(s.max);
    p.editCurve = s.curve;
    p.macroOperation = s.mult ? Patch::Metadata::Param::MacroOperation::kMult
                              : Patch::Metadata::Param::MacroOperation::kAdd;
    p.isEnum = s.fmt == Fmt::kList;
    if (s.cc != 0xff)
        p.midiCC = s.cc;
    p.invalidatesOtherMetadata = s.invalidates;

    const bool signedRange = s.min < 0.0f;
    const char* const* list = s.list;
    const uint8_t listLen = s.listLen;
    const int aux = s.aux;
    switch (s.fmt) {
    case Fmt::kList:
        p.valueFormatter = [list, listLen](const Patch&, Patch::paramvalue_t v)
        { return std::string(list[list_pick(static_cast<float>(v), listLen)]); };
        break;
    case Fmt::kTime:
        p.valueFormatter = polysynth::sdk::createTimeFormatter();
        break;
    case Fmt::kHz:
        p.valueFormatter = [](const Patch&, Patch::paramvalue_t v) { return hzText(static_cast<float>(v)); };
        break;
    case Fmt::kSemis:
        p.valueFormatter = polysynth::sdk::createSemitoneFormatter();
        break;
    case Fmt::kCents:
        p.valueFormatter = polysynth::sdk::createCentsFormatter();
        break;
    case Fmt::kRatio:
        p.valueFormatter = [aux](const Patch& patch, Patch::paramvalue_t v)
        {
            const bool sync = valueOf(patch, 27 + 4 * aux + 3, 0.0f) > 0.5f;
            const float r = lfoRatio(static_cast<float>(v), sync);
            return sync ? std::string(kDivisions[list_pick(r, 16)]) : hzText(r);
        };
        break;
    case Fmt::kFxRate:
        p.valueFormatter = [](const Patch&, Patch::paramvalue_t v) { return hzText(rate_hz(static_cast<float>(v))); };
        break;
    case Fmt::kLevel:
        p.valueFormatter = [](const Patch&, Patch::paramvalue_t v)
        { return fmt::format("{:+.1f} dB", static_cast<float>(v)); };
        break;
    case Fmt::kDistGain:
        p.valueFormatter = [](const Patch& patch, Patch::paramvalue_t v)
        {
            const float g = clampf(static_cast<float>(v), 0.0f, 1.0f);
            switch (distType(patch)) {
            case DIST_CRUSHER:
                return fmt::format("{:.1f} bit", crush_bits(g));
            case DIST_OVERDRIVE:
                return fmt::format("+{:.0f} dB", 6.0206f * (1.0f + g * 5.8826430f));
            case DIST_DISTORTION:
                return fmt::format("+{:.0f} dB", 10.0f + g * 40.0f);
            default:
                return percentText(g, false);
            }
        };
        break;
    case Fmt::kDistTone:
        p.valueFormatter = [](const Patch& patch, Patch::paramvalue_t v)
        {
            const float t = clampf(static_cast<float>(v), 0.0f, 1.0f);
            return distType(patch) == DIST_CRUSHER ? hzText(crush_hz(t)) : percentText(t, false);
        };
        break;
    case Fmt::kPercent:
    default:
        p.valueFormatter = [signedRange](const Patch&, Patch::paramvalue_t v)
        { return percentText(static_cast<float>(v), signedRange); };
        break;
    }
    return p;
}

void sharedSpecs(Spec* s, const char* const* srcs, uint8_t srcCount, const char* const* dests,
                 uint8_t destCount)
{
    static const char* const ratioNames[2] = { "LFO 1 Ratio", "LFO 2 Ratio" };
    static const char* const waveNames[2] = { "LFO 1 Wave", "LFO 2 Wave" };
    static const char* const retrigNames[2] = { "LFO 1 Retrigger", "LFO 2 Retrigger" };
    static const char* const syncNames[2] = { "LFO 1 Sync", "LFO 2 Sync" };
    static const char* const polNames[2] = { "LFO 1 Polarity", "LFO 2 Polarity" };
    static const char* const mtxNames[6][3] = {
        { "Mod 1 Src", "Mod 1 Dest", "Mod 1 Amt" }, { "Mod 2 Src", "Mod 2 Dest", "Mod 2 Amt" },
        { "Mod 3 Src", "Mod 3 Dest", "Mod 3 Amt" }, { "Mod 4 Src", "Mod 4 Dest", "Mod 4 Amt" },
        { "Mod 5 Src", "Mod 5 Dest", "Mod 5 Amt" }, { "Mod 6 Src", "Mod 6 Dest", "Mod 6 Amt" },
    };
    /* LFO 1: 1 Hz triangle; LFO 2: 0.25 Hz sine; both Free Running (sy9's Init) */
    const float ratioDef[2] = { 0.5f, 0.34948500f };
    for (int k = 0; k < 2; k++) {
        const int b = 27 + 4 * k;
        s[b] = Spec{ .name = ratioNames[k], .def = ratioDef[k], .fmt = Fmt::kRatio,
                     .cc = static_cast<uint8_t>(54 + k), .aux = static_cast<uint8_t>(k) };
        s[b + 1] = Spec{ .name = waveNames[k], .def = static_cast<float>(k), .max = 5,
                         .fmt = Fmt::kList, .list = kWaveforms, .listLen = 6 };
        s[b + 2] = Spec{ .name = retrigNames[k], .def = 2, .max = 2, .fmt = Fmt::kList,
                         .list = kRetrigger, .listLen = 3 };
        s[b + 3] = Spec{ .name = syncNames[k], .max = 1, .fmt = Fmt::kList, .list = kOffOn,
                         .listLen = 2, .invalidates = true };
        s[60 + k] = Spec{ .name = polNames[k], .max = 1, .fmt = Fmt::kList, .list = kPolarity,
                          .listLen = 2 };
    }
    s[35] = Spec{ .name = "Glide Mode", .max = 3, .fmt = Fmt::kList, .list = kGlideModes, .listLen = 4 };
    s[36] = Spec{ .name = "Glide Time", .max = 10, .curve = Curve::kExponential, .fmt = Fmt::kTime };
    s[39] = Spec{ .name = "Volume", .def = 1, .fmt = Fmt::kPercent, .mult = true };
    for (int k = 0; k < 6; k++) {
        const int b = 42 + 3 * k;
        s[b] = Spec{ .name = mtxNames[k][0], .max = static_cast<float>(srcCount - 1), .fmt = Fmt::kList,
                     .list = srcs, .listLen = srcCount };
        s[b + 1] = Spec{ .name = mtxNames[k][1], .max = static_cast<float>(destCount - 1),
                         .fmt = Fmt::kList, .list = dests, .listLen = destCount };
        s[b + 2] = Spec{ .name = mtxNames[k][2], .min = -4, .max = 4, .fmt = Fmt::kPercent };
    }
    s[P_EFFECT] = Spec{ .name = "Effect", .max = FX_TYPES - 1, .fmt = Fmt::kList, .list = kEffects,
                        .listLen = FX_TYPES };
    s[P_RATE] = Spec{ .name = "Rate", .def = 0.5f, .fmt = Fmt::kFxRate };
    s[P_DEPTH] = Spec{ .name = "Depth", .def = 0.5f };
    s[P_MIX] = Spec{ .name = "FX Mix", .def = 0.5f };
    s[P_FEEDBACK] = Spec{ .name = "Feedback", .def = 0.5f, .min = -0.95f, .max = 0.95f };
    s[P_DIST] = Spec{ .name = "Distortion", .max = DIST_TYPES - 1, .fmt = Fmt::kList,
                      .list = kDistortions, .listLen = DIST_TYPES, .invalidates = true };
    s[P_GAIN] = Spec{ .name = "Gain", .def = 0.5f, .fmt = Fmt::kDistGain };
    s[P_TONE] = Spec{ .name = "Tone", .def = 0.5f, .fmt = Fmt::kDistTone };
    s[P_PLACEMENT] = Spec{ .name = "Placement", .def = PLACE_POST, .max = 1, .fmt = Fmt::kList,
                           .list = kPlacements, .listLen = 2 };
    s[P_LEVEL] = Spec{ .name = "Level", .def = 0, .min = -30, .max = 6, .fmt = Fmt::kLevel };
}

const int kFxSection[P_COUNT - P_EFFECT] = {
    P_DIST, P_GAIN, P_TONE, P_LEVEL, P_PLACEMENT, P_EFFECT, P_RATE, P_DEPTH, P_FEEDBACK, P_MIX,
};

const int kLfoSection[10] = { 30, 27, 28, 29, 60, 34, 31, 32, 33, 61 };

} // namespace mtsynth

#endif // FEATURE_SYNTH
#endif // MT_HAS_SYNTHS
