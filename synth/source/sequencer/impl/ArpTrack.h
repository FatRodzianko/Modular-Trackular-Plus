#pragma once

#include <chrono>
#include <random>

#include "Track.h"
namespace polysynth
{

class ArpTrack final : public Track
{
  public:
    ArpTrack(int id,
             SequencerEngine::StepSequencePtr sequence,
             std::shared_ptr<MidiEventNotifier> midiEventNotifier,
             std::shared_ptr<SequencerDataProvider> sequencerDataProvider)
      : Track(id,
              std::move(sequence),
              std::move(midiEventNotifier),
              std::move(sequencerDataProvider))
    {
        std::fill_n(std::back_inserter(aftertouchValues), kGridRows * kGridColumns, 0);
    }

    bool removeById(uint8_t stepId, StepSequence::seqpos_t shift = 0);
    void append(const Step::MidiEventsData& midiEvents, int length_, uint8_t stepId = 0);

    [[nodiscard]] uint8_t getCurrentAftertouch() const override;

    void setPadAftertouch(uint8_t aftertouch, uint8_t padIdx)
    {
        assert(padIdx >= 0 && padIdx < kGridRows * kGridColumns);
        aftertouchValues[padIdx] = aftertouch;
    }

    void handleAftertouch(int polyAftertouch, int channelAftertouch, int padIdx) override;

    [[nodiscard]] std::optional<std::reference_wrapper<const Step>> getCurrentStep() const override;
    std::optional<std::reference_wrapper<Step>> getCurrentStep() override;

  protected:
    Step processStep(const Step& step) override;
    void onLoop() override;

  private:
    boost::container::static_vector<uint8_t, static_cast<size_t>(kGridRows* kGridColumns)>
      aftertouchValues;

    boost::container::static_vector<uint8_t, kMaxSequenceLength> order;

    Patch::Sequencer::Config::Arp lastConfig;

    tiny_optional<int> loopCounter = std::nullopt;
    std::mt19937 gen = std::mt19937(std::chrono::system_clock::now().time_since_epoch().count());

    void regenerateOrder();
};

} // polysynth
