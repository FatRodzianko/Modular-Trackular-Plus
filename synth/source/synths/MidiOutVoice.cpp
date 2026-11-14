#include "PolySynth/Patch.h"
#include "PolySynth/Synth.h"
#include "config.h"
#include "instrument/InstrumentType.h"
#include "instrument/InstrumentVoice.h"

#include "enum_util.h"
#include "fmt/core.h"

// NOLINTBEGIN(*-magic-numbers)

namespace polysynth
{
using EditorSection = Patch::Metadata::EditorLayout::EditorSection;
using EditorSubsection = Patch::Metadata::EditorLayout::EditorSubsection;
using EditorElement = Patch::Metadata::EditorLayout::EditorElement;
class MidiOutVoice : public InstrumentVoice
{
  public:
    void onMidiEvent(const InstrumentVoiceContext& /*ctx*/, const MidiEvent& /*event*/) override{};

    void onParamChange(int /* paramId */, Patch::optional_paramvalue_t /* newValue */) override{};

    void renderVoiceAudio(const InstrumentVoiceContext& /*ctx*/,
                          std::optional<AudioBufferStereoRef> /*inputBuffer*/,
                          AudioBufferStereoRef /*outputBuffer*/,
                          std::span<AudioBufferRef> /* auxOutBuffer */) override{};

    bool isActive() final { return false; };
    bool handlesNote(note_t /* note */) final
    {
        assert(false);
        return false;
    };
    bool handlesAnyNote() final
    {
        assert(false);
        return false;
    };
    tiny_optional<note_t> getLastNote() final
    {
        assert(false);
        return std::nullopt;
    };
    tiny_optional<float> getCurrentNoteVelocity() final { return std::nullopt; };

    static void createMetadata(const Patch& patch, Patch::Metadata& metadata)
    {
        metadata.params.reserve(static_cast<size_t>(kMidiOutInstrumentCCs) * 2);
        metadata.params.shrink_to_fit();

        metadata.layout23 = Patch::Metadata::
          EditorLayout{ .sections = {
                          EditorSection{
                            .name = "CC 1",
                            .subsections = { EditorSubsection{ .name = "values", .elements = {} },
                                             EditorSubsection{ .name = "mapping",
                                                               .elements = {} } } },
                          EditorSection{
                            .name = "CC 2",
                            .subsections = { EditorSubsection{ .name = "values", .elements = {} },
                                             EditorSubsection{ .name = "mapping",
                                                               .elements = {} } } },
                        } };

        for (auto paramId = 0; paramId < 2 * kMidiOutInstrumentCCs; paramId++)
        {
            if (paramId % 2 == 0)
            {
                // CC value
                {
                    auto ccKnobId = paramId + 1;
                    auto ccId = patch.getParamFinalValue(ccKnobId);
                    metadata.params.emplace_back(Patch::Metadata::Param{
                      .displayName = ccId ? fmt::format("CC {}", *ccId) : "-",
                      .minValue = MidiEvent::Data::ControlChange::kMinValue,
                      .maxValue = MidiEvent::Data::ControlChange::kMaxValue,
                      .midiCC =
                        ccId ? decltype(Patch::Metadata::Param::midiCC)(*ccId) : std::nullopt,
                    });
                    metadata.layout23.sections[(paramId < kMidiOutInstrumentCCs) ? 0 : 1]
                      .subsections[0]
                      .elements.push_back(
                        EditorElement{ .type = EditorElement::Type::kParam, .id = paramId });
                }
            }
            else
            {
                // CC id mapping
                {
                    const auto ccId = paramId / 2;
                    metadata.params.emplace_back(Patch::Metadata::Param{
                      .displayName = fmt::format("CC mapping"),
                      .defaultValue = kDefaultCCIds.at(ccId),
                      .minValue = MidiEvent::Data::ControlChange::kMinId,
                      .maxValue = MidiEvent::Data::ControlChange::kMaxId,
                      .invalidatesOtherMetadata = true,
                    });
                    metadata.layout23.sections[(paramId < kMidiOutInstrumentCCs) ? 0 : 1]
                      .subsections[1]
                      .elements.push_back(
                        EditorElement{ .type = EditorElement::Type::kParam, .id = paramId });
                }
            }
        }

        metadata.instrumentTypeId = static_cast<int>(InstrumentTypeId::kMidiOut);
        metadata.macrosAffectParamCC = true;

        metadata.outputChannels = Patch::Metadata::OutputChannels::kNone;
    }
};

std::unique_ptr<InstrumentType> createMidiOut()
{
    return std::make_unique<InstrumentType>(
      InstrumentType{ .name = "Midi out",
                      .createMetadata = &MidiOutVoice::createMetadata,
                      .createVoice = [](int /* voiceIdx */) -> std::unique_ptr<InstrumentVoice>
                      { return std::make_unique<MidiOutVoice>(); } });
}

}

// NOLINTEND(*-magic-numbers)
