// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// WAVE: a wavetable synth (wtEngine.h) as a CE Synth SDK engine. Its effects are Synth FX's
// two slots (synthfx/synthfxMod.h): after the voices, or with Placement Pre in each voice
// before its filter (the voice's Pre hook, here).
//
// Parameter ids are sy9's (Modular-Trackular's WAVE), 0-61, then the old effects' 62-71: kept
// so patches saved before still load, and Synth FX turns them into the part's slots; nothing
// here reads them, and no section shows them.

#include "modsBuild.h"

#if MT_HAS_SYNTHS

#include "config.h"
#ifdef FEATURE_SYNTH

#include "synthsEngines.h"
#include "synthsSdk.h"
#include "wtEngine.h"
#if MT_HAS_SYNTHFX
#include "synthfx/synthfxMod.h"
#endif

namespace polysynth
{

namespace
{

using namespace mtsynth;
using Curve = Patch::Metadata::Param::Curve;
using sdk::EditorElement;

enum class WaveParamId : int
{
    kPosition1 = 0,
    kLast = static_cast<int>(P_COUNT) - 1,
};

const char* const kWaves[WT_SLOTS] = {
    "Formant", "Resonant", "Harmonic", "Square/PWM", "Sin/Tri/Saw/Sqr", "Sync", "FM", "Super Saw", "Saw",
};
const char* const kTypes[15] = {
    "LP Ladder 24", "LP OB 24", "LP OB 12", "LP SVF 24", "LP SVF 12", "HP OB 24", "HP OB 12",
    "HP SVF 24", "HP SVF 12", "BP OB 24", "BP OB 12", "BP SVF 24", "BP SVF 12", "Notch 24", "Notch 12",
};
const char* const kSrcs[MOD_SRCS] = { "Amp Env", "Filter Env", "Mod Env", "LFO 1", "LFO 2", "Velocity", "Key" };
const char* const kDests[MOD_DESTS] = {
    "Position", "Position 1", "Position 2", "Pitch", "Pitch 1", "Pitch 2", "Mix", "Cutoff",
    "Resonance", "Volume", "Noise",
};
const char* const kOffOn[] = { "Off", "On" };

void waveSpecs(Spec* s)
{
    sharedSpecs(s, kSrcs, MOD_SRCS, kDests, MOD_DESTS);
    for (int k = 0; k < 2; k++) {
        static const char* const posNames[2] = { "Position 1", "Position 2" };
        static const char* const waveNames[2] = { "Wave 1", "Wave 2" };
        static const char* const tuneNames[2] = { "Tune 1", "Tune 2" };
        static const char* const resetNames[2] = { "Reset 1", "Reset 2" };
        s[WP_SHAPE1 + k] = Spec{ .name = posNames[k], .def = 0.6667f, .cc = static_cast<uint8_t>(20 + k) };
        s[WP_WAVE1 + k] = Spec{ .name = waveNames[k], .def = 4, .max = WT_SLOTS - 1, .fmt = Fmt::kList,
                                .list = kWaves, .listLen = WT_SLOTS, .cc = static_cast<uint8_t>(22 + k) };
        s[WP_TUNE1 + k] = Spec{ .name = tuneNames[k], .min = -36, .max = 36, .fmt = Fmt::kSemis };
        s[WP_RESET1 + k] = Spec{ .name = resetNames[k], .max = 1, .fmt = Fmt::kList, .list = kOffOn,
                                 .listLen = 2 };
    }
    s[WP_MIX] = Spec{ .name = "Mix", .min = -1, .max = 1, .cc = 24 };
    s[WP_DETUNE] = Spec{ .name = "Detune", .def = 8, .max = 100, .fmt = Fmt::kCents, .cc = 25 };
    s[WP_FINE] = Spec{ .name = "Finetune", .min = -100, .max = 100, .fmt = Fmt::kCents };
    s[WP_TYPE] = Spec{ .name = "Type", .max = 14, .fmt = Fmt::kList, .list = kTypes, .listLen = 15,
                       .cc = *sdk::MidiCCs::kFilterType };
    /* kAdd, not kMult: the Cutoff macro is bipolar, and CE starts a bipolar macro at its centre
     * (Synth::initSynthPartMacroValue), which under kMult left the voice at 20 Hz. kAdd works in
     * the exponential curve's normalized space, so the centre changes nothing. */
    s[WP_CUTOFF] = Spec{ .name = "Cutoff", .def = 2400, .min = 20, .max = 20000, .curve = Curve::kExponential,
                         .fmt = Fmt::kHz, .cc = *sdk::MidiCCs::kFilterCutoff };
    s[WP_RES] = Spec{ .name = "Resonance", .def = 0.2f, .max = 1.5f, .cc = *sdk::MidiCCs::kFilterResonance };
    s[WP_ENVAMT] = Spec{ .name = "Env Amt", .def = 0.15f, .min = -1, .max = 1,
                         .cc = *sdk::MidiCCs::kFilterEnvAmount };
    /* the envelopes: filter 15-18, amp 19-22, mod 23-26 */
    static const char* const envNames[3][4] = {
        { "Flt Attack", "Flt Decay", "Flt Sustain", "Flt Release" },
        { "Amp Attack", "Amp Decay", "Amp Sustain", "Amp Release" },
        { "Mod Attack", "Mod Decay", "Mod Sustain", "Mod Release" },
    };
    static const float envDefs[3][4] = { { 0.005f, 0.8f, 0.35f, 0.5f }, { 0.005f, 0.5f, 0.8f, 0.4f },
                                         { 0.5f, 1.0f, 0.0f, 0.5f } };
    static const uint8_t envCCs[3][4] = { { 80, 81, 82, 83 }, { 75, 72, 76, 73 }, { 46, 47, 48, 49 } };
    for (int e = 0; e < 3; e++) {
        const int b = WP_FENV_ATT + 4 * e;
        s[b] = Spec{ .name = envNames[e][0], .def = envDefs[e][0], .max = 10, .curve = Curve::kExponential,
                     .fmt = Fmt::kTime, .cc = envCCs[e][0] };
        s[b + 1] = Spec{ .name = envNames[e][1], .def = envDefs[e][1], .min = 0.001f, .max = 10,
                         .curve = Curve::kExponential, .fmt = Fmt::kTime, .cc = envCCs[e][1] };
        s[b + 2] = Spec{ .name = envNames[e][2], .def = envDefs[e][2], .cc = envCCs[e][2] };
        s[b + 3] = Spec{ .name = envNames[e][3], .def = envDefs[e][3], .min = 0.001f, .max = 10,
                         .curve = Curve::kExponential, .fmt = Fmt::kTime, .cc = envCCs[e][3] };
    }
    s[WP_TRACK] = Spec{ .name = "Key Track", .def = 0.5f, .min = -2, .max = 2 };
    s[WP_DRIVE] = Spec{ .name = "Drive" };
    s[WP_DIGITAL] = Spec{ .name = "Digital", .max = 1, .fmt = Fmt::kList, .list = kOffOn, .listLen = 2 };
    s[WP_NOISE] = Spec{ .name = "Noise" };
    /* the Init matrix: LFO 1 and the Mod Env to Position, at Amt 0 (routings to try) */
    s[WP_MTX + 0].def = SRC_LFO1;
    s[WP_MTX + 1].def = DST_POSITION;
    s[WP_MTX + 3].def = SRC_MOD_ENV;
    s[WP_MTX + 4].def = DST_POSITION;
}

class WaveSynthVoice : public MtSynthVoice<WaveSynthVoice, WaveParamId>
{
  public:
    WaveSynthVoice()
    {
        wt_voice_init(&eng);
#if MT_HAS_SYNTHFX
        eng.pre = &WaveSynthVoice::pre;
        eng.pre_ctx = this;
#endif
    }

    bool isActive() final { return wt_voice_sounding(&eng) != 0; }

    void onNoteOn(const Ctx& /*ctx*/, note_t note, float /*velocity*/, bool /*isLegatoStep*/) final
    {
        eng.volume = p[WP_VOLUME];
        wt_note_on(&eng, p, static_cast<uint32_t>(note));
    }

    void onNoteOff(const Ctx& /*ctx*/, note_t /*note*/) final { wt_note_off(&eng); }

    void renderVoiceAudio(const Ctx& ctx,
                          std::optional<AudioBufferStereoRef> /*input*/,
                          AudioBufferStereoRef out,
                          std::span<AudioBufferRef> /*aux*/) final
    {
        eng.volume = p[WP_VOLUME];
        eng.bend = pitchBendSemitones;
        wt_voice_render(&eng, p, out[0].data(), ctx.tempo);
        toStereo(out);
    }

    static void createMetadata(Patch& /*patch*/, Patch::Metadata& metadata)
    {
        metadata.params.resize(P_COUNT);
        metadata.params.shrink_to_fit();
        if (paramHandlers.empty())
            paramHandlers.resize(P_COUNT);

        Spec specs[P_COUNT];
        waveSpecs(specs);
        sdk::ParamHelper<WaveSynthVoice> helper(metadata, paramHandlers);
        addAll(helper, specs, std::make_index_sequence<P_COUNT>{});

        auto ids = [](std::initializer_list<int> list)
        {
            std::vector<EditorElement> v;
            v.reserve(list.size());
            for (int id : list)
                v.push_back({ .type = EditorElement::Type::kParam, .id = id });
            return v;
        };
        auto range = [](const int* first, size_t n)
        {
            std::vector<EditorElement> v;
            v.reserve(n);
            for (size_t i = 0; i < n; i++)
                v.push_back({ .type = EditorElement::Type::kParam, .id = first[i] });
            return v;
        };
        std::vector<int> mtx;
        for (int i = 0; i < 18; i++)
            mtx.push_back(WP_MTX + i);

        metadata.layoutFlat = Patch::Metadata::EditorLayout{
            .sections = {
              { .name = "Oscillators", .shortName = "Osc",
                .elements = ids({ WP_WAVE1, WP_SHAPE1, WP_WAVE2, WP_SHAPE2, WP_MIX, WP_TUNE1, WP_TUNE2,
                                  WP_FINE, WP_DETUNE, WP_NOISE, WP_DIGITAL, WP_RESET1, WP_RESET2 }) },
              { .name = "Filter", .shortName = "Filter",
                .elements = ids({ WP_CUTOFF, WP_RES, WP_TYPE, WP_ENVAMT, WP_TRACK, WP_DRIVE }) },
              { .name = "Amp Envelope", .shortName = "Amp Env",
                .elements = ids({ WP_AENV_ATT, WP_AENV_ATT + 1, WP_AENV_ATT + 2, WP_AENV_ATT + 3 }) },
              { .name = "Filter Envelope", .shortName = "Flt Env",
                .elements = ids({ WP_FENV_ATT, WP_FENV_ATT + 1, WP_FENV_ATT + 2, WP_FENV_ATT + 3 }) },
              { .name = "Mod Envelope", .shortName = "Mod Env",
                .elements = ids({ WP_MENV_ATT, WP_MENV_ATT + 1, WP_MENV_ATT + 2, WP_MENV_ATT + 3 }) },
              { .name = "LFOs", .shortName = "LFO", .elements = range(kLfoSection, 10) },
              { .name = "Modulation Matrix", .shortName = "Matrix", .elements = range(mtx.data(), mtx.size()) },
              { .name = "Voice", .shortName = "Voice", .elements = ids({ 35, 36, WP_VOLUME }) },
            },
        };
        metadata.layout23 = metadata.layoutFlat;

        metadata.initDefaultMacros = [](auto& macros)
        {
            macros[0] = Patch::Metadata::Macro{ .displayName = "Attack",
                                                .modifiers = { { WP_AENV_ATT, 0.5f } },
                                                .bipolar = true };
            macros[1] = Patch::Metadata::Macro{ .displayName = "Decay / Release",
                                                .modifiers = { { WP_AENV_ATT + 1, 0.5f },
                                                               { WP_FENV_ATT + 1, 0.5f },
                                                               { WP_AENV_ATT + 3, 0.5f },
                                                               { WP_FENV_ATT + 3, 0.5f } },
                                                .bipolar = true };
            macros[2] = Patch::Metadata::Macro{ .displayName = "Wave Position",
                                                .modifiers = { { WP_SHAPE1, 1.0f }, { WP_SHAPE2, 1.0f } },
                                                .bipolar = true };
            macros[3] = Patch::Metadata::Macro{ .displayName = "Cutoff",
                                                .modifiers = { { WP_CUTOFF, 1.0f } },
                                                .bipolar = true };
            macros[4] = Patch::Metadata::Macro{ .displayName = "Resonance",
                                                .modifiers = { { WP_RES, 0.5f } },
                                                .bipolar = true };
            macros[5] = Patch::Metadata::Macro{ .displayName = "Filter Env Amt",
                                                .modifiers = { { WP_ENVAMT, 0.5f } },
                                                .bipolar = true };
            macros[*Patch::SystemMacroId::kVelocity].modifiers = { { WP_VOLUME, 1.0f } };
        };

        metadata.instrumentTypeId = kWaveTypeId;
        metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo;
    }

  private:
#if MT_HAS_SYNTHFX
    /* Audio interrupt, inside the voice's render, before Drive: Synth FX's Pre slots, with this
     * voice's own instances; a note from idle starts them afresh */
    static void pre(void* ctx, float* buf)
    {
        auto* v = static_cast<WaveSynthVoice*>(ctx);
        mods::synthfx::runPre(v, buf, v->eng.fresh != 0);
    }
#endif

    struct wt_voice eng;
};

} // namespace

std::unique_ptr<InstrumentType> createWaveSynth()
{
    return std::make_unique<InstrumentType>(
      InstrumentType{ .name = "WAVE",
                      .createMetadata = &WaveSynthVoice::createMetadata,
                      .createVoice = [](int /*voiceIdx*/) -> std::unique_ptr<InstrumentVoice>
                      { return std::make_unique<WaveSynthVoice>(); } });
}

} // namespace polysynth

#endif // FEATURE_SYNTH
#endif // MT_HAS_SYNTHS
