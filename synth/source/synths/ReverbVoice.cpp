#include "PolySynth/Synth.h"
#include "instrument/InstrumentType.h"
#include "instrument/InstrumentVoice.h"
#include "synths_utils.h"

#include "PolyDSP/fx/Reverb.h"

#include "enum_util.h"

// NOLINTBEGIN(*-magic-numbers)

namespace polysynth
{
enum ParamId
{
    kDecay,
    kDiffusion,
    kTone,
    kPredelay,
    kModel,
    kGeometry,
    kSpaceSize,
    kShelfFreq,
    kShelfGain,
};

class ReverbVoice : public BaseFxVoice<ReverbVoice, ParamId>
{
  public:
    using Reverb = polydsp::Reverb;

    explicit ReverbVoice(const Synth::Allocator& largeBuffersAlloc)
      : reverb(std::span<sample_t>(static_cast<sample_t*>(largeBuffersAlloc(
                                     polydsp::Reverb::getBufferSize() * sizeof(sample_t))),
                                   polydsp::Reverb::getBufferSize())){};

    void onMidiEvent(const InstrumentVoiceContext& /*ctx*/, const MidiEvent& /*event*/) override{};

    void onParamChange(int paramId, Patch::optional_paramvalue_t newValue) override
    {
        polydsp::RenderContext renderContext{ .samplingRate = kAudioSampleRate, .tempo = 0 };

        switch (static_cast<ParamId>(paramId))
        {
            case ParamId::kDecay:
                reverb.setDecay(static_cast<polydsp::param_t>(*newValue));
                break;
            case ParamId::kDiffusion:
                reverb.setDiffusion(static_cast<polydsp::param_t>(*newValue));
                break;
            case ParamId::kTone:
                reverb.setTone(static_cast<polydsp::param_t>(*newValue));
                break;
            case ParamId::kPredelay:
                reverb.setPredelay(static_cast<polydsp::param_t>(*newValue));
                break;
            case ParamId::kModel:
                reverb.setModel(static_cast<Reverb::Model>(std::lround(*newValue)));
                break;
            case ParamId::kGeometry:
                reverb.setGeometry(static_cast<polydsp::param_t>(*newValue));
                break;
            case ParamId::kSpaceSize:
                reverb.setSpaceSize(static_cast<polydsp::param_t>(*newValue));
                break;
            case ParamId::kShelfFreq:
                reverb.setShelfFilterFreq(renderContext, static_cast<polydsp::param_t>(*newValue));
                break;
            case ParamId::kShelfGain:
                reverb.setShelfFilterGain(renderContext, static_cast<polydsp::param_t>(*newValue));
                break;
        }
    };

#if defined(PRODUCT_WAVE)
    void renderVoiceAudio(const InstrumentVoiceContext& /*ctx*/,
                          std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef outputBuffer) override
#else
    void renderVoiceAudio(const InstrumentVoiceContext& /*ctx*/,
                          std::optional<AudioBufferStereoRef> inputBuffer,
                          AudioBufferStereoRef outputBuffer,
                          std::span<AudioBufferRef> /* auxOutBuffer */) override
#endif
    {
        assert(inputBuffer);

        reverb.render(inputBuffer->at(0), outputBuffer);
    };

    static void createMetadata(const Patch& /* patch */, Patch::Metadata& metadata)
    {
        using Param = Patch::Metadata::Param;
        auto& params = metadata.params;

        params.resize(enumutil::EnumMax<ParamId>::value + 1);
        metadata.params.shrink_to_fit();

        params[ParamId::kDecay] = Param{
            .displayName = "Time",
            .defaultValue = 0.6f,
            .minValue = 0,
            .maxValue = 1.0f,
            .valueFormatter = createPercentFormatter(false),
        };
        params[ParamId::kDiffusion] = Param{
            .displayName = "Diffusion",
            .defaultValue = 0.85f,
            .minValue = 0,
            .maxValue = 1,
            .valueFormatter = createPercentFormatter(false),
        };
        params[ParamId::kTone] = Param{
            .displayName = "Brightness",
            .defaultValue = 0.72f,
            .minValue = 0,
            .maxValue = 1,
            .valueFormatter = createPercentFormatter(false),
        };
        params[ParamId::kPredelay] = Param{
            .displayName = "Predelay",
            .defaultValue = 0.03f,
            .minValue = 0,
            .maxValue = 1,
            .valueFormatter = createPercentFormatter(false),
        };
        params[ParamId::kModel] = Param{
            .displayName = "Model",
            .defaultValue = Reverb::Model::kModelMutable,
            .minValue =
              static_cast<polysynth::Patch::paramvalue_t>(enumutil::EnumMin<Reverb::Model>::value),
            .maxValue =
              static_cast<polysynth::Patch::paramvalue_t>(enumutil::EnumMax<Reverb::Model>::value),
            .isEnum = true,
            .valueFormatter = [](const Patch& /* patch */, polysynth::Patch::paramvalue_t value)
            { return Reverb::kModelNames.at(std::lround(value)); },
        };
        params[ParamId::kGeometry] = Param{
            .displayName = "Geometry",
            .defaultValue = 0.73f,
            .minValue = 0.01f,
            .maxValue = 1,
            .valueFormatter = createPercentFormatter(false),
        };
        params[ParamId::kSpaceSize] = Param{
            .displayName = "Size",
            .defaultValue = 0.7f,
            .minValue = 0.01f,
            .maxValue = 1,
            .valueFormatter = createPercentFormatter(false),
        };
        params[ParamId::kShelfFreq] = Param{
            .displayName = "Filter Freq",
            .defaultValue = 5300.0f,
            .minValue = 100.0f,
            .maxValue = 10000.0f,
            .valueFormatter = createFrequencyFormatter(),
        };
        params[ParamId::kShelfGain] = Param{
            .displayName = "Filter Gain",
            .defaultValue = 6.0f,
            .minValue = -24,
            .maxValue = 24,
            .valueFormatter = createFormatter("{:.0f} dB"),
        };

        metadata.instrumentTypeId = static_cast<int>(InstrumentTypeId::kReverb);
        metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo;

#if defined(PRODUCT_WAVE)
        metadata.layout33 = Patch::
#else
        metadata.layout23 = Patch::
#endif
          Metadata::EditorLayout{ .sections = { EditorSection{
                                    .name = "Reverb",
                                    .subsections = {
                                      EditorSubsection{ .elements = {
                                                          EditorElement{
                                                            .type = EditorElement::Type::kParam,
                                                            .id = ParamId::kModel },
                                                          EditorElement{
                                                            .type = EditorElement::Type::kParam,
                                                            .id = ParamId::kDecay },
                                                          EditorElement{ .type = EditorElement::Type::kParam,
                                                                         .id = ParamId::kPredelay },
                                                          EditorElement{ .type = EditorElement::
                                                                           Type::kParam,
                                                                         .id = ParamId::kTone },
                                                          EditorElement{ .type = EditorElement::
                                                                           Type::kParam,
                                                                         .id =
                                                                           ParamId::kSpaceSize },
                                                          EditorElement{ .type = EditorElement::
                                                                           Type::kParam,
                                                                         .id = ParamId::kGeometry },
                                                          EditorElement{ .type = EditorElement::
                                                                           Type::kParam,
                                                                         .id =
                                                                           ParamId::kDiffusion },
                                                          EditorElement{ .type = EditorElement::
                                                                           Type::kParam,
                                                                         .id =
                                                                           ParamId::kShelfFreq },
                                                          EditorElement{ .type = EditorElement::
                                                                           Type::kParam,
                                                                         .id =
                                                                           ParamId::kShelfGain },
                                                        } } } } } };
    }

  private:
    polydsp::Reverb reverb;
};

std::unique_ptr<InstrumentType> createReverb(const Synth::Allocator& largeBuffersAlloc)
{
    return std::make_unique<InstrumentType>(InstrumentType{
      .name = "REV",
      .createMetadata = &ReverbVoice::createMetadata,
      .createVoice = [&largeBuffersAlloc](int /* voiceIdx */) -> std::unique_ptr<InstrumentVoice>
      { return std::make_unique<ReverbVoice>(largeBuffersAlloc); } });
}
}

// NOLINTEND(*-magic-numbers)
