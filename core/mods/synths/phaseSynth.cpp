// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// PHASE: a phase distortion synth (phEngine.h) as a CE Synth SDK engine, with the
// Distortion and a Chorus, Flanger or Phaser after its voices (synthsFx.h). PHASE has no
// filter, so Placement does nothing on it; it is there so the two engines' FX match.
//
// Parameter ids are sy9's (Modular-Trackular's PHASE), 0-61, then the effects' 62-71.

#include "modsBuild.h"

#if MT_HAS_SYNTHS

#include "config.h"
#ifdef FEATURE_SYNTH

#include "phEngine.h"
#include "synthsEngines.h"
#include "synthsSdk.h"

namespace polysynth
{

namespace
{

using namespace mtsynth;
using Curve = Patch::Metadata::Param::Curve;
using sdk::EditorElement;

enum class PhaseParamId : int
{
    kWave1Plus = 0,
    kLast = static_cast<int>(P_COUNT) - 1,
};

const char* const kShapes[PH_SHAPES] = {
    "Saw", "Square", "Pulse", "Double Sine", "Saw-Pulse", "Reso I", "Reso II", "Reso III",
};
const char* const kPairs[1 + PH_SHAPES] = {
    "Off", "Saw", "Square", "Pulse", "Double Sine", "Saw-Pulse", "Reso I", "Reso II", "Reso III",
};
const char* const kSrcs[PH_SRCS] = { "DCA Env", "DCW Env", "DCO Env", "LFO 1", "LFO 2", "Velocity", "Key" };
const char* const kDests[PH_DESTS] = {
    "Pitch", "Pitch 1", "Pitch 2", "DCW", "DCW 1", "DCW 2", "Mix", "Volume", "Detune",
};
const char* const kOffOn[] = { "Off", "On" };

void phaseSpecs(Spec* s)
{
    sharedSpecs(s, kSrcs, PH_SRCS, kDests, PH_DESTS);
    for (int k = 0; k < 2; k++) {
        static const char* const pairNames[2] = { "Wave 1+", "Wave 2+" };
        static const char* const waveNames[2] = { "Wave 1", "Wave 2" };
        static const char* const tuneNames[2] = { "Tune 1", "Tune 2" };
        static const char* const resetNames[2] = { "Reset 1", "Reset 2" };
        static const char* const dcwNames[2] = { "DCW 1", "DCW 2" };
        s[PP_WAVE1B + k] = Spec{ .name = pairNames[k], .max = PH_SHAPES, .fmt = Fmt::kList, .list = kPairs,
                                 .listLen = 1 + PH_SHAPES };
        s[PP_WAVE1 + k] = Spec{ .name = waveNames[k], .max = PH_SHAPES - 1, .fmt = Fmt::kList,
                                .list = kShapes, .listLen = PH_SHAPES, .cc = static_cast<uint8_t>(22 + k) };
        s[PP_TUNE1 + k] = Spec{ .name = tuneNames[k], .min = -36, .max = 36, .fmt = Fmt::kSemis };
        s[PP_RESET1 + k] = Spec{ .name = resetNames[k], .max = 1, .fmt = Fmt::kList, .list = kOffOn,
                                 .listLen = 2 };
        s[PP_DCW1 + k] = Spec{ .name = dcwNames[k], .def = 0.5f,
                               .cc = k == 0 ? *sdk::MidiCCs::kFilterCutoff : *sdk::MidiCCs::kFilterResonance };
    }
    s[PP_MIX] = Spec{ .name = "Mix", .min = -1, .max = 1, .cc = 24 };
    s[PP_DETUNE] = Spec{ .name = "Detune", .def = 8, .max = 100, .fmt = Fmt::kCents, .cc = 25 };
    s[PP_FINE] = Spec{ .name = "Finetune", .min = -100, .max = 100, .fmt = Fmt::kCents };
    s[PP_RING] = Spec{ .name = "Ring Mod", .max = 1, .fmt = Fmt::kList, .list = kOffOn, .listLen = 2 };
    s[PP_ENVAMT1] = Spec{ .name = "Env Amt 1", .def = 0.3f, .min = -1, .max = 1,
                          .cc = *sdk::MidiCCs::kFilterEnvAmount };
    s[PP_ENVAMT2] = Spec{ .name = "Env Amt 2", .def = 0.3f, .min = -1, .max = 1 };
    s[PP_KEYFOLLOW] = Spec{ .name = "Key Follow", .min = -1, .max = 1 };
    s[PP_NOISEMOD] = Spec{ .name = "Noise Mod" };
    s[PP_PENV_AMT] = Spec{ .name = "DCO Env Amt", .min = -48, .max = 48, .fmt = Fmt::kSemis };
    /* the envelopes: DCO (pitch) 15-18, DCA 19-22, DCW 23-26 */
    static const char* const envNames[3][4] = {
        { "DCO Attack", "DCO Decay", "DCO Sustain", "DCO Release" },
        { "DCA Attack", "DCA Decay", "DCA Sustain", "DCA Release" },
        { "DCW Attack", "DCW Decay", "DCW Sustain", "DCW Release" },
    };
    static const float envDefs[3][4] = { { 0.005f, 0.3f, 0.0f, 0.3f }, { 0.005f, 0.5f, 0.8f, 0.4f },
                                         { 0.005f, 0.6f, 0.3f, 0.5f } };
    static const uint8_t envCCs[3][4] = { { 46, 47, 48, 49 }, { 75, 72, 76, 73 }, { 80, 81, 82, 83 } };
    for (int e = 0; e < 3; e++) {
        const int b = PP_PENV_ATT + 4 * e;
        s[b] = Spec{ .name = envNames[e][0], .def = envDefs[e][0], .max = 10, .curve = Curve::kExponential,
                     .fmt = Fmt::kTime, .cc = envCCs[e][0] };
        s[b + 1] = Spec{ .name = envNames[e][1], .def = envDefs[e][1], .min = 0.001f, .max = 10,
                         .curve = Curve::kExponential, .fmt = Fmt::kTime, .cc = envCCs[e][1] };
        s[b + 2] = Spec{ .name = envNames[e][2], .def = envDefs[e][2], .cc = envCCs[e][2] };
        s[b + 3] = Spec{ .name = envNames[e][3], .def = envDefs[e][3], .min = 0.001f, .max = 10,
                         .curve = Curve::kExponential, .fmt = Fmt::kTime, .cc = envCCs[e][3] };
    }
    /* the Init matrix: LFO 1 to Pitch, LFO 2 to DCW, at Amt 0 (routings to try) */
    s[PP_MTX + 0].def = PH_SRC_LFO1;
    s[PP_MTX + 1].def = PH_DST_PITCH;
    s[PP_MTX + 3].def = PH_SRC_LFO2;
    s[PP_MTX + 4].def = PH_DST_DCW;
}

class PhaseSynthVoice : public MtSynthVoice<PhaseSynthVoice, PhaseParamId>
{
  public:
    PhaseSynthVoice() { ph_voice_init(&eng); }

    bool isActive() final { return ph_voice_sounding(&eng) != 0; }

    void onNoteOn(const Ctx& /*ctx*/, note_t note, float /*velocity*/, bool /*isLegatoStep*/) final
    {
        eng.volume = p[PP_VOLUME];
        ph_note_on(&eng, p, static_cast<uint32_t>(note));
    }

    void onNoteOff(const Ctx& /*ctx*/, note_t /*note*/) final { ph_note_off(&eng); }

    void renderVoiceAudio(const Ctx& ctx,
                          std::optional<AudioBufferStereoRef> /*input*/,
                          AudioBufferStereoRef out,
                          std::span<AudioBufferRef> /*aux*/) final
    {
        eng.volume = p[PP_VOLUME];
        eng.bend = pitchBendSemitones;
        ph_voice_render(&eng, p, out[0].data(), ctx.tempo);
        toStereo(out);
    }

    /* no filter: the Distortion always runs after the voices */
    int postDistortion() const { return dist_on(p + P_EFFECT); }

    static void createMetadata(Patch& patch, Patch::Metadata& metadata)
    {
        metadata.params.resize(P_COUNT);
        metadata.params.shrink_to_fit();
        if (paramHandlers.empty())
            paramHandlers.resize(P_COUNT);

        Spec specs[P_COUNT];
        phaseSpecs(specs);
        specs[P_GAIN].name = distKnobName(patch, false);
        specs[P_TONE].name = distKnobName(patch, true);
        sdk::ParamHelper<PhaseSynthVoice> helper(metadata, paramHandlers);
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
            mtx.push_back(PP_MTX + i);

        metadata.layoutFlat = Patch::Metadata::EditorLayout{
            .sections = {
              { .name = "DCO Oscillators", .shortName = "DCO",
                .elements = ids({ PP_WAVE1, PP_WAVE1B, PP_WAVE2, PP_WAVE2B, PP_MIX, PP_TUNE1, PP_TUNE2,
                                  PP_FINE, PP_DETUNE, PP_RESET1, PP_RESET2, PP_RING, PP_NOISEMOD }) },
              { .name = "DCW Shaper", .shortName = "DCW",
                .elements = ids({ PP_DCW1, PP_DCW2, PP_ENVAMT1, PP_ENVAMT2, PP_KEYFOLLOW }) },
              { .name = "DCA Envelope", .shortName = "DCA",
                .elements = ids({ PP_DCA_ATT, PP_DCA_DEC, PP_DCA_SUS, PP_DCA_REL }) },
              { .name = "DCW Envelope", .shortName = "DCW Env",
                .elements = ids({ PP_WENV_ATT, PP_WENV_DEC, PP_WENV_SUS, PP_WENV_REL }) },
              { .name = "DCO Envelope", .shortName = "DCO Env",
                .elements = ids({ PP_PENV_ATT, PP_PENV_DEC, PP_PENV_SUS, PP_PENV_REL, PP_PENV_AMT }) },
              { .name = "LFOs", .shortName = "LFO", .elements = range(kLfoSection, 10) },
              { .name = "Modulation Matrix", .shortName = "Matrix", .elements = range(mtx.data(), mtx.size()) },
              { .name = "Voice", .shortName = "Voice",
                .elements = ids({ PP_GLIDE_MODE, PP_GLIDE_TIME, PP_VOLUME }) },
              { .name = "FX", .shortName = "FX", .elements = range(kFxSection, P_COUNT - P_EFFECT) },
            },
        };
        metadata.layout23 = metadata.layoutFlat;

        metadata.initDefaultMacros = [](auto& macros)
        {
            macros[0] = Patch::Metadata::Macro{ .displayName = "Attack",
                                                .modifiers = { { PP_DCA_ATT, 0.5f } },
                                                .bipolar = true };
            macros[1] = Patch::Metadata::Macro{ .displayName = "Decay / Release",
                                                .modifiers = { { PP_DCA_DEC, 0.5f },
                                                               { PP_WENV_DEC, 0.5f },
                                                               { PP_DCA_REL, 0.5f },
                                                               { PP_WENV_REL, 0.5f } },
                                                .bipolar = true };
            macros[2] = Patch::Metadata::Macro{ .displayName = "DCW",
                                                .modifiers = { { PP_DCW1, 1.0f }, { PP_DCW2, 1.0f } },
                                                .bipolar = true };
            macros[3] = Patch::Metadata::Macro{ .displayName = "DCW Env Amt",
                                                .modifiers = { { PP_ENVAMT1, 0.5f }, { PP_ENVAMT2, 0.5f } },
                                                .bipolar = true };
            macros[4] = Patch::Metadata::Macro{ .displayName = "Detune",
                                                .modifiers = { { PP_DETUNE, 0.5f } },
                                                .bipolar = true };
            macros[5] = Patch::Metadata::Macro{ .displayName = "Mix",
                                                .modifiers = { { PP_MIX, 0.5f } },
                                                .bipolar = true };
            macros[*Patch::SystemMacroId::kVelocity].modifiers = { { PP_VOLUME, 1.0f } };
        };

        metadata.instrumentTypeId = kPhaseTypeId;
        metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo;
    }

  private:
    struct ph_voice eng;
};

} // namespace

std::unique_ptr<InstrumentType> createPhaseSynth()
{
    return std::make_unique<InstrumentType>(
      InstrumentType{ .name = "PHASE",
                      .createMetadata = &PhaseSynthVoice::createMetadata,
                      .createVoice = [](int /*voiceIdx*/) -> std::unique_ptr<InstrumentVoice>
                      { return std::make_unique<PhaseSynthVoice>(); } });
}

} // namespace polysynth

#endif // FEATURE_SYNTH
#endif // MT_HAS_SYNTHS
