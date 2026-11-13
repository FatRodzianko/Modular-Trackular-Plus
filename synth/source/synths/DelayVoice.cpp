#include "PolySynth/Synth.h"
#include "config.h"
#include "instrument/InstrumentType.h"
#include "instrument/InstrumentVoice.h"
#include "synths_utils.h"

#include "PolyDSP/fx/Delay.h"

#include "enum_util.h"

// NOLINTBEGIN(*-magic-numbers)

namespace polysynth
{

enum ParamId
{
    kMode,
    kTime1,
    kTime2,
    kTempoSync,
    kFeedback,
    kFilterFreq,
    kFilterBandwidth,
    kModulation,
    kStereoSpread,
};

static_assert(polydsp::Delay::kDelayLines == 2);

class DelayVoice : public BaseFxVoice<DelayVoice, ParamId>
{
  public:
    explicit DelayVoice(const Synth::Allocator& largeBuffersAlloc)
      : delay(std::span<sample_t>(static_cast<sample_t*>(largeBuffersAlloc(
                                    polydsp::Delay::getBufferSize() * sizeof(sample_t))),
                                  polydsp::Delay::getBufferSize())){};

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
        delay.render(renderContext, inputBuffer->at(0), outputBuffer);
    };

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    static void createMetadata(const Patch& patch, Patch::Metadata& metadata)
    {
        metadata.params.resize(enumutil::EnumMax<ParamId>::value + 1);
        metadata.params.shrink_to_fit();

        if (paramHandlers.empty())
        {
            paramHandlers.resize(enumutil::EnumMax<ParamId>::value + 1);
        }

        ParamHelper<DelayVoice> helper(metadata, paramHandlers);

        helper.addParam(
          ParamId::kMode,
          {
            .displayName = "Model",
            .defaultValue = *polydsp::Delay::Mode::kTripleTap,
            .minValue = 0,
            .maxValue = polydsp::Delay::kModeNames.size() - 1,
            .isEnum = true,
            .valueFormatter = [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
            { return polydsp::Delay::kModeNames.at(std::lround(value)); },
          },
          [](auto& voice, Patch::optional_paramvalue_t newValue)
          { voice.delay.setMode(static_cast<polydsp::Delay::Mode>(std::lround(*newValue))); });

        for (int timeIdx = 0; timeIdx < 2; timeIdx++)
        {
            bool isSync = patch.getParamValue(ParamId::kTempoSync).value_or(0) != 0;
            helper.addParam(
              ParamId::kTime1 + timeIdx,
              Param{ .displayName = fmt::format("Time {}", timeIdx + 1),
                     .defaultValue = timeIdx == 0 ? 4 : 8,
                     .minValue = static_cast<Patch::paramvalue_short_t>(isSync ? 0 : 10),
                     .maxValue =
                       isSync ? static_cast<Patch::paramvalue_short_t>(kTimeBaseNames.size() - 1)
                              : static_cast<Patch::paramvalue_short_t>(
                                  polydsp::Delay::kMaxDelayTime * 1000),
                     .isEnum = isSync,
                     .valueFormatter =
                       [](const Patch& patch, const Patch::optional_paramvalue_t& value)
                     {
                         bool sync =
                           static_cast<bool>(patch.getParamValue(ParamId::kTempoSync).value_or(0));
                         return (sync) ? std::string(kTimeBaseNames[std::clamp(
                                           static_cast<int>(std::lround(value.value_or(0))),
                                           0,
                                           static_cast<int>(kTimeBaseNames.size()) - 1)])
                                       : fmt::format("{:.0f} ms", value.value_or(0));
                     }

              },
              [timeIdx](DelayVoice& voice, const Patch::optional_paramvalue_t& value)
              {
                  auto* patch = voice.getPatch();
                  assert(patch != nullptr);
                  if (patch->getParamValue(ParamId::kTempoSync).value_or(0) > 0)
                  {
                      auto timeBase =
                        std::clamp<int>(static_cast<int>(std::lround(value.value_or(0))),
                                        0,
                                        static_cast<int>(kTimeBasePulseCount.size()) - 1);

                      voice.delay.setTimeBasePulseCount(timeIdx, kTimeBasePulseCount[timeBase]);
                  }
                  else
                  {
                      voice.delay.setTime(timeIdx, static_cast<float>(*value) / 1000.0f);
                  }
              });
        }

        helper.addParam(ParamId::kFeedback,
                        Param{ .displayName = "Feedback",
                               .defaultValue = 0.42f,
                               .minValue = 0,
                               .maxValue = 1.5f,
                               .valueFormatter = createPercentFormatter() },
                        [](DelayVoice& voice, const Patch::optional_paramvalue_t& value)
                        { voice.delay.setFeedback(static_cast<float>(value.value_or(0))); });

        helper.addParam(
          ParamId::kFilterFreq,
          Param{
            .displayName = "Filter Freq",
            .defaultValue = 4380,
            .minValue = 20,
            .maxValue = polydsp::Delay::kMaxFilterFrequency,
            .editCurve = Patch::Metadata::Param::Curve::kExponentialSteep,
            .valueFormatter = createFrequencyFormatter(polydsp::Delay::kMaxFilterFrequency),
          },
          [](auto& voice, const Patch::optional_paramvalue_t& value)
          { voice.delay.setFilterFrequency(value.value_or(0)); });

        helper.addParam(ParamId::kFilterBandwidth,
                        Param{
                          .displayName = "Filter Width",
                          .defaultValue = 1.0f,
                          .minValue = 0.01f,
                          .maxValue = 5,
                          .valueFormatter = createFormatter("{:.2f} Oct"),
                        },
                        [](auto& voice, const Patch::optional_paramvalue_t& value)
                        { voice.delay.setFilterBandwidth(value.value_or(0)); });

        helper.addParam(ParamId::kModulation,
                        Param{
                          .displayName = "Flutter",
                          .defaultValue = -0.02f,
                          .minValue = -1,
                          .maxValue = 1,
                          .valueFormatter = createPercentFormatter(),
                        },
                        [](auto& voice, const Patch::optional_paramvalue_t& value)
                        {
                            auto modulation = value.value_or(0);
                            voice.delay.setModDepth(std::abs(modulation));
                            voice.delay.setModFreq(modulation >= 0 ? 0.2f : 3.0f);
                        });

        helper.addParam(ParamId::kStereoSpread,
                        Param{ .displayName = "Stereo",
                               .defaultValue = 1.0f,
                               .minValue = 0,
                               .maxValue = 1.0f,
                               .valueFormatter = createPercentFormatter(false) },
                        [](DelayVoice& voice, const Patch::optional_paramvalue_t& value)
                        { voice.delay.setStereoSpread(static_cast<float>(value.value_or(0))); });

        helper.addParam(
          ParamId::kTempoSync,
          Param{
            .displayName = "Tempo sync",
            .defaultValue = 1,
            .minValue = 0,
            .maxValue = 1,
            .isEnum = true,
            .invalidatesOtherMetadata = true,
            .valueFormatter = [](const Patch& /*patch*/, const Patch::optional_paramvalue_t& value)
            { return value > 0 ? "On" : "Off"; } },
          [](DelayVoice& voice, const Patch::optional_paramvalue_t& value)
          {
              bool tempoSync = static_cast<bool>(value.value_or(0));
              if (tempoSync != voice.delay.isTempoSync(0))
              {
                  auto* patch = voice.getPatch();
                  assert(patch != nullptr);
                  for (int timeIdx = 0; timeIdx < polydsp::Delay::kDelayLines; timeIdx++)
                  {
                      voice.delay.setTempoSync(timeIdx, tempoSync);
                      if (tempoSync)
                      {
                          auto currentTimeBasePulseCount =
                            voice.delay.getTimeBasePulseCount(timeIdx);
                          auto timeBasePulseCountIdx =
                            closestDefinedPulseCountIdx(currentTimeBasePulseCount);
                          patch->setParamValue(
                            ParamId::kTime1 + timeIdx,
                            static_cast<Patch::paramvalue_t>(timeBasePulseCountIdx),
                            Patch::ParamChangeSource::kPatchAssign);
                      }
                      else
                      {
                          patch->setParamValue(
                            ParamId::kTime1 + timeIdx,
                            static_cast<Patch::paramvalue_t>(voice.delay.getTime(timeIdx) * 1000),
                            Patch::ParamChangeSource::kPatchAssign);
                      }
                  }
              }
          });

        metadata.instrumentTypeId = static_cast<int>(InstrumentTypeId::kDelay);
        metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo;

        // clang-format off
#if defined(PRODUCT_WAVE)
        metadata.layout33 = Patch::Metadata::
#else
        metadata.layout23 = Patch::Metadata::
#endif
          EditorLayout{ .sections = { EditorSection{
                          .name = "Delay",
                          .subsections = { EditorSubsection{
                            .elements = { EditorElement{ .type = EditorElement::Type::kParam, .id = ParamId::kMode },
                                          EditorElement{ .type = EditorElement::Type::kParam, .id = ParamId::kTime1 },
                                          EditorElement{ .type = EditorElement::Type::kParam, .id = ParamId::kTime2 },
                                          EditorElement{ .type = EditorElement::Type::kParam, .id = ParamId::kFeedback },
                                          EditorElement{ .type = EditorElement::Type::kParam, .id = ParamId::kFilterFreq },
                                          EditorElement{ .type = EditorElement::Type::kParam, .id = ParamId::kFilterBandwidth },
                                          EditorElement{ .type = EditorElement::Type::kParam, .id = ParamId::kModulation },
                                          EditorElement{ .type = EditorElement::Type::kParam, .id = ParamId::kStereoSpread },
                                          EditorElement{ .type = EditorElement::Type::kParam, .id = ParamId::kTempoSync }

                            } } } } } };
        // clang-format on
    }

  private:
    polydsp::Delay delay;
};

std::unique_ptr<InstrumentType> createDelay(const Synth::Allocator& largeBuffersAlloc)
{
    return std::make_unique<InstrumentType>(InstrumentType{
      .name = "DEL",
      .createMetadata = &DelayVoice::createMetadata,
      .createVoice = [&largeBuffersAlloc](int /* voiceIdx */) -> std::unique_ptr<InstrumentVoice>
      { return std::make_unique<DelayVoice>(largeBuffersAlloc); } });
}
}

// NOLINTEND(*-magic-numbers)
