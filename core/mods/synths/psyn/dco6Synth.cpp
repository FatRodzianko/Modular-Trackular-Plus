// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// DCO6: a virtual-analog synth after the Juno-106's design (../d6Engine.h), as a .psyn plugin
// (docs/mt/VA_SYNTH_PLAN.md). Built by psyn/Makefile; the Synths mod (../synthsMod.h) has its
// type id. Unison needs the MT hook in CE's host (InstrumentImpl::mtUnisonNoteOn), which gives a
// note to a stack of voices and tells each member its place through Unison Place, a parameter in
// no section. Without the hook (a build without the Synths mod) every voice plays alone.

#include "psyn/psyn_cpp_bridge.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include "d6Engine.h"

#include <string>

namespace polysynth
{

namespace
{

using namespace mtsynth;
using Curve = Patch::Metadata::Param::Curve;
using sdk::EditorElement;

constexpr int kDco6TypeId = 66;                     /* as in ../synthsMod.h and psyn/Makefile */

enum class Dco6ParamId : int
{
    kRange = 0,
    kLast = DP_COUNT - 1,
};

const char* const kRanges[] = { "16'", "8'", "4'" };
const char* const kPwmSrcs[] = { "LFO", "Env" };
const char* const kVcas[] = { "Env", "Gate" };
const char* const kShapes[D6_LFO_SHAPES] = { "Triangle", "Sine", "Square", "Ramp Up", "Ramp Down",
                                             "Random S&H", "Random Smooth" };
const char* const kOffOn[] = { "Off", "On" };
const char* const kTrigs[] = { "Note", "Free" };
const char* const kUnisons[] = { "Off", "2", "3", "4", "5", "6", "7", "8" };
const char* const kDivisions[16] = { "1",     "1/2 D", "1/2",  "1/4 D",  "1 T",  "1/4",
                                     "1/8 D", "1/2 T", "1/8",  "1/16 D", "1/4 T", "1/16",
                                     "1/8 T", "1/32",  "1/16 T", "1/32 T" };
const float kDivBeats[16] = { 4.0f, 3.0f, 2.0f, 1.5f, 4.0f / 3.0f, 1.0f, 0.75f, 2.0f / 3.0f,
                              0.5f, 0.375f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f, 1.0f / 12.0f, 1.0f / 24.0f };

constexpr float kRateMinHz = 0.05f;
constexpr float kRateOctaves = 9.643856f;          /* log2(40 / 0.05): Rate 1 is 40 Hz */

float valueOf(const Patch& patch, int id, float otherwise)
{
    auto v = patch.getParamValue(id);
    return v ? static_cast<float>(*v) : otherwise;
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

using Formatter = std::function<std::string(const Patch&, Patch::paramvalue_t)>;

Formatter listFmt(const char* const* list, uint32_t n)
{
    return [list, n](const Patch&, Patch::paramvalue_t v) { return std::string(list[list_pick(static_cast<float>(v), n)]); };
}

Formatter percentFmt(bool signedRange = false)
{
    return [signedRange](const Patch&, Patch::paramvalue_t v) { return percentText(static_cast<float>(v), signedRange); };
}

struct P
{
    const char* name;
    float def, min, max;
    Curve curve;
    Formatter fmt;
    bool isEnum = false;
    uint8_t cc = 0xff;
    bool invalidates = false;
    bool mult = false;
};

Patch::Metadata::Param makeParam(const P& s)
{
    Patch::Metadata::Param p{};
    p.displayName = s.name;
    p.defaultValue = s.def;
    p.minValue = static_cast<Patch::paramvalue_short_t>(s.min);
    p.maxValue = static_cast<Patch::paramvalue_short_t>(s.max);
    p.editCurve = s.curve;
    p.macroOperation = s.mult ? Patch::Metadata::Param::MacroOperation::kMult
                              : Patch::Metadata::Param::MacroOperation::kAdd;
    p.isEnum = s.isEnum;
    if (s.cc != 0xff)
        p.midiCC = s.cc;
    p.invalidatesOtherMetadata = s.invalidates;
    p.valueFormatter = s.fmt;
    return p;
}

float lfoHz(float v, bool sync, float tempo)
{
    v = clampf(v, 0.0f, 1.0f);
    if (sync)
        return clampf(tempo, 20.0f, 999.0f) * (1.0f / 60.0f) / kDivBeats[list_pick(v * 15.0f, 16)];
    return kRateMinHz * exp2f_fast(v * kRateOctaves);
}

void specs(P* s)
{
    const auto lin = Curve::kLinear;
    const auto expc = Curve::kExponential;
    s[DP_RANGE] = { "Range", 1, 0, 2, lin, listFmt(kRanges, 3), true };
    s[DP_TUNE] = { "Tune", 0, -12, 12, lin, sdk::createSemitoneFormatter(), true };
    s[DP_FINE] = { "Fine Tune", 0, -50, 50, lin, sdk::createCentsFormatter() };
    s[DP_SAW] = { "Saw", 1, 0, 1, lin, percentFmt(), false, 20 };
    s[DP_PULSE] = { "Pulse", 0, 0, 1, lin, percentFmt(), false, 21 };
    s[DP_SUB] = { "Sub", 0, 0, 1, lin, percentFmt(), false, 22 };
    s[DP_NOISE] = { "Noise", 0, 0, 1, lin, percentFmt(), false, 23 };
    s[DP_PW] = { "Pulse Width", 0, 0, 1, lin, percentFmt(), false, 24 };
    s[DP_PWM] = { "PWM Amt", 0, 0, 1, lin, percentFmt(), false, 25 };
    s[DP_PWM_SRC] = { "PWM Source", 0, 0, 1, lin, listFmt(kPwmSrcs, 2), true };
    s[DP_LFO_PITCH] = { "LFO to Pitch", 0, 0, 1, lin, percentFmt(), false, 26 };
    /* the HPF at its minimum is Off (the engine bypasses it) */
    s[DP_HPF] = { "HPF", D6_HPF_OFF_HZ, D6_HPF_OFF_HZ, D6_HPF_MAX_HZ, expc,
                  [](const Patch&, Patch::paramvalue_t v)
                  {
                      const float hz = static_cast<float>(v);
                      return hz <= D6_HPF_OFF_HZ + 0.5f ? std::string("Off") : hzText(hz);
                  },
                  false, 27 };
    /* kAdd, not kMult: the Cutoff macro is bipolar (see WAVE's note in waveSynth.cpp) */
    s[DP_CUTOFF] = { "Cutoff", 2400, 20, 20000, expc,
                     [](const Patch&, Patch::paramvalue_t v) { return hzText(static_cast<float>(v)); },
                     false, *sdk::MidiCCs::kFilterCutoff };
    s[DP_RES] = { "Resonance", 0.15f, 0, 1, lin, percentFmt(), false, *sdk::MidiCCs::kFilterResonance };
    s[DP_ENV_AMT] = { "Env Amt", 0.35f, -1, 1, lin, percentFmt(true), false, *sdk::MidiCCs::kFilterEnvAmount };
    s[DP_LFO_CUT] = { "LFO to Cutoff", 0, 0, 1, lin, percentFmt(), false, 28 };
    s[DP_KEY] = { "Key Track", 0.5f, 0, 1, lin, percentFmt(), false, 29 };
    s[DP_ATTACK] = { "Attack", 0.005f, 0.001f, 3, expc, sdk::createTimeFormatter(), false, 73 };
    s[DP_DECAY] = { "Decay", 0.6f, 0.001f, 12, expc, sdk::createTimeFormatter(), false, 75 };
    s[DP_SUSTAIN] = { "Sustain", 0.6f, 0, 1, lin, percentFmt(), false, 79 };
    s[DP_RELEASE] = { "Release", 0.35f, 0.001f, 12, expc, sdk::createTimeFormatter(), false, 72 };
    s[DP_VCA] = { "VCA Mode", 0, 0, 1, lin, listFmt(kVcas, 2), true };
    s[DP_LFO_WAVE] = { "LFO Wave", 0, 0, D6_LFO_SHAPES - 1, lin, listFmt(kShapes, D6_LFO_SHAPES), true };
    s[DP_LFO_RATE] = { "LFO Rate", 0.693f, 0, 1, lin,
                       [](const Patch& patch, Patch::paramvalue_t v)
                       {
                           if (valueOf(patch, DP_LFO_SYNC, 0.0f) > 0.5f)
                               return std::string(kDivisions[list_pick(static_cast<float>(v) * 15.0f, 16)]);
                           return hzText(lfoHz(static_cast<float>(v), false, 120.0f));
                       },
                       false, 3 };
    s[DP_LFO_DELAY] = { "LFO Delay", 0, 0, 5, Curve::kPower, sdk::createTimeFormatter() };
    s[DP_LFO_SYNC] = { "LFO Sync", 0, 0, 1, lin, listFmt(kOffOn, 2), true, 0xff, true };
    s[DP_LFO_TRIG] = { "LFO Trigger", 0, 0, 1, lin, listFmt(kTrigs, 2), true };
    s[DP_UNISON] = { "Unison", 1, 1, 8, lin,
                     [](const Patch&, Patch::paramvalue_t v) { return std::string(kUnisons[list_pick(static_cast<float>(v) - 1.0f, 8)]); },
                     true };
    s[DP_DETUNE] = { "Detune", 0, 0, 50, Curve::kPower, sdk::createCentsFormatter() };
    s[DP_WIDTH] = { "Width", 0.5f, 0, 1, lin, percentFmt() };
    s[DP_VINTAGE] = { "Vintage", 0, 0, 1, lin, percentFmt() };
    s[DP_GLIDE] = { "Glide", 0, 0, 10, Curve::kPower, sdk::createTimeFormatter(), false, 5 };
    s[DP_VOLUME] = { "Volume", 1, 0, 1, lin, percentFmt(), false, 0xff, false, true };
    s[DP_UNI_PLACE] = { "Unison Place", 0, 0, 255, lin, percentFmt(), true };
    s[DP_BOOST] = { "Bass Boost", 0, 0, 1, lin, listFmt(kOffOn, 2), true };
}

template<class V, size_t I>
void setAt(V& v, Patch::optional_paramvalue_t x)
{
    v.setParam(static_cast<int>(I), static_cast<float>(*x));
}

class Dco6Voice : public sdk::CustomSynthVoice<Dco6Voice, Dco6ParamId>
{
  public:
    explicit Dco6Voice(int voiceIdx)
    {
        for (float& x : p)
            x = 0.0f;
        d6_voice_init(&eng, voiceIdx);
    }

    void setParam(int id, float v)
    {
        if (id >= 0 && id < DP_COUNT)
            p[id] = v;
    }

    bool isActive() final { return d6_sounding(&eng); }

    void onNoteOn(const InstrumentVoiceContext& ctx, note_t note, float, bool) final
    {
        /* the host hook's place, set before each note-on: slot + 16 * size; 0 without it: alone */
        uint32_t size = 1, slot = 0;
        const uint32_t place = static_cast<uint32_t>(clampf(p[DP_UNI_PLACE], 0.0f, 255.0f));
        if (place >= 16 && (place >> 4) <= 8 && (place & 15u) < (place >> 4)) {
            size = place >> 4;
            slot = place & 15u;
        }
        d6_note_on(&eng, p, note, slot, size, lfoHz(p[DP_LFO_RATE], p[DP_LFO_SYNC] > 0.5f, ctx.tempo));
    }

    void onNoteOff(const InstrumentVoiceContext&, note_t) final { d6_note_off(&eng); }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx, AudioBufferStereoRef out)
    {
        float* L = out[0].data();
        float* R = out[1].data();
        for (uint32_t n = 0; n < D6_BLOCK; n++)
            L[n] = R[n] = 0.0f;
        d6_render(&eng, p, L, R, lfoHz(p[DP_LFO_RATE], p[DP_LFO_SYNC] > 0.5f, ctx.tempo), pitchBendSemitones);
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx, std::optional<AudioBufferStereoRef>,
                          AudioBufferStereoRef out, std::span<AudioBufferRef>) final
    {
        renderVoiceAudio(ctx, out);
    }

    static void createMetadata(Patch&, Patch::Metadata& metadata)
    {
        metadata.params.resize(DP_COUNT);
        metadata.params.shrink_to_fit();
        if (paramHandlers.empty())
            paramHandlers.resize(DP_COUNT);
        P s[DP_COUNT];
        specs(s);
        addAll(metadata, s, std::make_index_sequence<DP_COUNT>{});

        auto ids = [](std::initializer_list<int> list)
        {
            std::vector<EditorElement> v;
            v.reserve(list.size());
            for (int id : list)
                v.push_back({ .type = EditorElement::Type::kParam, .id = id });
            return v;
        };
        metadata.layoutFlat = Patch::Metadata::EditorLayout{
            .sections = {
              { .name = "Oscillator", .shortName = "DCO",
                .elements = ids({ DP_RANGE, DP_SAW, DP_PULSE, DP_SUB, DP_NOISE, DP_PW, DP_PWM, DP_PWM_SRC,
                                  DP_LFO_PITCH, DP_TUNE, DP_FINE }) },
              { .name = "Filter", .shortName = "Filter",
                .elements = ids({ DP_CUTOFF, DP_RES, DP_ENV_AMT, DP_LFO_CUT, DP_KEY, DP_HPF, DP_BOOST }) },
              { .name = "Envelope", .shortName = "Env",
                .elements = ids({ DP_ATTACK, DP_DECAY, DP_SUSTAIN, DP_RELEASE, DP_VCA }) },
              { .name = "LFO", .shortName = "LFO",
                .elements = ids({ DP_LFO_WAVE, DP_LFO_RATE, DP_LFO_SYNC, DP_LFO_DELAY, DP_LFO_TRIG }) },
              { .name = "Voice", .shortName = "Voice",
                .elements = ids({ DP_UNISON, DP_DETUNE, DP_WIDTH, DP_VINTAGE, DP_GLIDE, DP_VOLUME }) },
            },
        };
        metadata.initDefaultMacros = [](auto& macros)
        {
            macros[0] = Patch::Metadata::Macro{ .displayName = "Cutoff", .modifiers = { { DP_CUTOFF, 1.0f } },
                                                .bipolar = true };
            macros[1] = Patch::Metadata::Macro{ .displayName = "Resonance", .modifiers = { { DP_RES, 0.5f } },
                                                .bipolar = true };
            macros[2] = Patch::Metadata::Macro{ .displayName = "Env Amt", .modifiers = { { DP_ENV_AMT, 0.5f } },
                                                .bipolar = true };
            macros[3] = Patch::Metadata::Macro{ .displayName = "Attack", .modifiers = { { DP_ATTACK, 0.5f } },
                                                .bipolar = true };
            macros[4] = Patch::Metadata::Macro{ .displayName = "Decay / Release",
                                                .modifiers = { { DP_DECAY, 0.5f }, { DP_RELEASE, 0.5f } },
                                                .bipolar = true };
            macros[5] = Patch::Metadata::Macro{ .displayName = "Detune", .modifiers = { { DP_DETUNE, 1.0f } },
                                                .bipolar = false };
            macros[*Patch::SystemMacroId::kVelocity].modifiers = { { DP_VOLUME, 1.0f } };
        };
        metadata.instrumentTypeId = kDco6TypeId;
        metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo;
    }

  private:
    template<size_t... I>
    static void addAll(Patch::Metadata& metadata, const P* s, std::index_sequence<I...>)
    {
        static constexpr ParamHandlerFn kHandlers[] = { &setAt<Dco6Voice, I>... };
        for (size_t i = 0; i < sizeof...(I); i++) {
            metadata.params.at(i) = makeParam(s[i]);
            if (!paramHandlers[i])
                paramHandlers[i] = kHandlers[i];
        }
    }

    float p[DP_COUNT];
    d6_voice eng;
};

} // namespace

} // namespace polysynth

PSYN_REGISTER_CPP_VOICE(polysynth::Dco6Voice)
