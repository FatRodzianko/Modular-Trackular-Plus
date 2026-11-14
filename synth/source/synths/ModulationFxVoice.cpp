#include "PolySynth/Synth.h"
#include "config.h"
#include "instrument/InstrumentType.h"
#include "instrument/InstrumentVoice.h"
#include "synths_utils.h"

#include "PolyDSP/fx/ModulationFx.h"

#include "enum_util.h"

// NOLINTBEGIN(*-magic-numbers)

namespace polysynth
{

enum ParamId
{
    kMode,
    kDelay,
    kFeedback,
    kModDepth,
    kModFreq,
    kFilterFreq,
    kFilterBandwidth,
    kVariation,
};

class ModulationFxVoice : public BaseFxVoice<ModulationFxVoice, ParamId>
{
  public:
    explicit ModulationFxVoice(const Synth::Allocator& largeBuffersAlloc)
      : fx(std::span<sample_t>(static_cast<sample_t*>(largeBuffersAlloc(
                                 polydsp::ModulationFx::getBufferSize() * sizeof(sample_t))),
                               polydsp::ModulationFx::getBufferSize())){};

    void onMidiEvent(const InstrumentVoiceContext& /*ctx*/, const MidiEvent& /*event*/) override{};

    void onParamChange(int paramId, Patch::optional_paramvalue_t newValue) override
    {
        if (paramHandlers[paramId])
        {
            paramHandlers[paramId](*this, newValue);
        };
    };

#if defined(PRODUCT_WAVE)
    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef outputBuffer) override
#else
    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef outputBuffer,
                          std::span<AudioBufferRef> /* auxOutBuffer */) override
#endif
    {
        assert(inputBuffer);

        polydsp::RenderContext renderContext{ .samplingRate = kAudioSampleRate,
                                              .tempo = ctx.tempo };
        fx.render(renderContext, inputBuffer->at(0), outputBuffer);
    };

    static void createMetadata(const Patch& /* patch */, Patch::Metadata& metadata)
    {
        metadata.params.resize(enumutil::EnumMax<ParamId>::value + 1);
        metadata.params.shrink_to_fit();

        if (paramHandlers.empty())
        {
            paramHandlers.resize(enumutil::EnumMax<ParamId>::value + 1);
        }

        ParamHelper<ModulationFxVoice> helper(metadata, paramHandlers);

        helper.addParam(
          ParamId::kMode,
          {
            .displayName = "Model",
            .defaultValue = *polydsp::ModulationFx::Mode::kStereo,
            .minValue = 0,
            .maxValue = polydsp::ModulationFx::kModeNames.size() - 1,
            .isEnum = true,
            .valueFormatter = [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
            { return polydsp::ModulationFx::kModeNames.at(std::lround(value)); },
          },
          [](auto& voice, Patch::optional_paramvalue_t newValue)
          { voice.fx.setMode(static_cast<polydsp::ModulationFx::Mode>(std::lround(*newValue))); });

        helper.addParam(
          ParamId::kDelay,
          Param{
            .displayName = "Time Shift",
            .defaultValue = 12.8f,
            .minValue = -45,
            .maxValue = 45,
            .valueFormatter = [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
            { return fmt::format("{:.1f} ms", value); },
          },
          [](ModulationFxVoice& voice, const Patch::optional_paramvalue_t& value)
          { voice.fx.setDelay(static_cast<float>(*value) / 1000.0f); });

        helper.addParam(ParamId::kFeedback,
                        Param{
                          .displayName = "Feedback",
                          .defaultValue = 0.11f,
                          .minValue = -0.9f,
                          .maxValue = 0.9f,
                          .valueFormatter = createPercentFormatter(),
                        },
                        [](ModulationFxVoice& voice, const Patch::optional_paramvalue_t& value)
                        { voice.fx.setFeedback(static_cast<float>(value.value_or(0))); });

        helper.addParam(ParamId::kModDepth,
                        Param{
                          .displayName = "Mod Depth",
                          .defaultValue = 0.14f,
                          .minValue = 0,
                          .maxValue = 1,
                          .valueFormatter = createPercentFormatter(),
                        },
                        [](ModulationFxVoice& voice, const Patch::optional_paramvalue_t& value)
                        { voice.fx.setModDepth(static_cast<float>(value.value_or(0))); });

        helper.addParam(ParamId::kModFreq,
                        Param{
                          .displayName = "Mod Freq",
                          .defaultValue = 2.0f,
                          .minValue = -100,
                          .maxValue = 100,
                          .editCurve = Patch::Metadata::Param::Curve::kExponentialSteep,
                          .valueFormatter =
                            [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value) {
                                return (value < 10) ? fmt::format("{:.2f} Hz", value)
                                                    : fmt::format("{:.1f} Hz", value);
                            },

                        },
                        [](ModulationFxVoice& voice, const Patch::optional_paramvalue_t& value)
                        { voice.fx.setModFreq(value.value_or(0)); });

        helper.addParam(
          ParamId::kFilterFreq,
          Param{
            .displayName = "Filter Freq",
            .defaultValue = polydsp::ModulationFx::kMaxFilterFrequency,
            .minValue = 20,
            .maxValue = polydsp::ModulationFx::kMaxFilterFrequency,
            .editCurve = Patch::Metadata::Param::Curve::kExponentialSteep,
            .valueFormatter = createFrequencyFormatter(polydsp::ModulationFx::kMaxFilterFrequency),
          },
          [](ModulationFxVoice& voice, const Patch::optional_paramvalue_t& value)
          { voice.fx.setFilterFrequency(value.value_or(0)); });

        helper.addParam(ParamId::kFilterBandwidth,
                        Param{
                          .displayName = "Filter Width",
                          .defaultValue = 2.0f,
                          .minValue = 0,
                          .maxValue = 5,
                          .valueFormatter = createFormatter("{:.2f} Oct"),
                        },
                        [](ModulationFxVoice& voice, const Patch::optional_paramvalue_t& value)
                        { voice.fx.setFilterBandwidth(value.value_or(0)); });

        helper.addParam(ParamId::kVariation,
                        Param{
                          .displayName = "Variation",
                          .defaultValue = 0.0f,
                          .minValue = 0,
                          .maxValue = 1,
                          .valueFormatter = createPercentFormatter(),
                        },
                        [](ModulationFxVoice& voice, const Patch::optional_paramvalue_t& value)
                        { voice.fx.setVariation(value.value_or(0)); });

        metadata.instrumentTypeId = static_cast<int>(InstrumentTypeId::kModulationFx);
        metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo;

#if defined(PRODUCT_WAVE)
        metadata.layout33 = Patch::Metadata::
#else
        metadata.layout23 = Patch::Metadata::
#endif
          EditorLayout{ .sections = { EditorSection{
                          .name = "Modulation FX",
                          .subsections = { EditorSubsection{
                            .elements = {
                              EditorElement{ .type = EditorElement::Type::kParam,
                                             .id = ParamId::kMode },
                              EditorElement{ .type = EditorElement::Type::kParam,
                                             .id = ParamId::kModDepth },
                              EditorElement{ .type = EditorElement::Type::kParam,
                                             .id = ParamId::kModFreq },
                              EditorElement{ .type = EditorElement::Type::kParam,
                                             .id =
                                               ParamId::kVariation },
                              EditorElement{ .type = EditorElement::Type::kParam,
                                             .id =
                                               ParamId::kFilterFreq },
                              EditorElement{ .type = EditorElement::Type::kParam,
                                             .id =
                                               ParamId::kFilterBandwidth },
                              EditorElement{ .type = EditorElement::Type::kParam,
                                             .id =
                                               ParamId::kDelay },
                              EditorElement{ .type = EditorElement::Type::kParam, .id = ParamId::kFeedback },
                            } } } } } };
    }

  private:
    polydsp::ModulationFx fx;
};

std::unique_ptr<InstrumentType> createModulationFx(const Synth::Allocator& largeBuffersAlloc)
{
    return std::make_unique<InstrumentType>(InstrumentType{
      .name = "MOD",
      .createMetadata = &ModulationFxVoice::createMetadata,
      .createVoice = [&largeBuffersAlloc](int /* voiceIdx */) -> std::unique_ptr<InstrumentVoice>
      { return std::make_unique<ModulationFxVoice>(largeBuffersAlloc); } });
}
}

// NOLINTEND(*-magic-numbers)
