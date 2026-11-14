#pragma once

#include "../Sequence.h"
#include "../SequencerEngine.h"
#include "../Step.h"
#include "../StepSequence.h"
#include "MidiEventNotifier.h"
#include "SequencerDataProvider.h"
#include "config/config.h"
#include "sequencer/NoteOffEvents.h"

namespace polysynth
{

class Track
{
  public:
    enum class State
    {
        kNew,
        kScheduled,
        kStarted,
        kStopping,
        kStopped
    };

    Track(Track&& other) = default;
    Track(const Track& other) = delete;
    Track& operator=(Track&& other) = default;
    Track& operator=(const Track& other) = delete;
    virtual ~Track() = default;

    void start();
    void stop();
    void startOnNextStep();
    [[nodiscard]] bool isScheduled() const { return state == State::kScheduled; }
    [[nodiscard]] bool isStarted() const { return state == State::kStarted; }
    [[nodiscard]] bool isNew() const { return state == State::kNew; }
    [[nodiscard]] bool isStopped() const { return state == State::kStopped; }
    void setSequence(SequencerEngine::StepSequencePtr sequencePtr);
    [[nodiscard]] const StepSequence* getSequence() const { return this->sequence.get(); }
    [[nodiscard]] int getId() const { return id; }
    [[nodiscard]] bool empty() const { return sequence->empty(); }
    [[nodiscard]] int getStepsCount() const { return sequence->ssize(); }
    void setSequenceLength(int length_) { sequence->length = length_; }
    [[nodiscard]] int getSequenceLength() const { return sequence->length; }
    void put(const Step::MidiEventsData& midiEvents, int length, int stepIdx);
    void remove(unsigned int stepIndex);

    [[nodiscard]] virtual uint8_t getCurrentAftertouch() const { return lastAftertouch; }

    /**
     * Should be called externally once per pulse
     */
    void pulse();

    void setLoop(bool loop_) { this->loop = loop_; }

    void setInstrumentId(int instId) { this->instrumentId = instId; }

    virtual std::optional<std::reference_wrapper<Step>> getCurrentStep();
    [[nodiscard]] virtual std::optional<std::reference_wrapper<const Step>> getCurrentStep() const;

    virtual Step processStep(const Step& step) { return step; }

    virtual void handleAftertouch(int polyAftertouch, int channelAftertouch, int padIdx);

  protected:
    Track(int id,
          SequencerEngine::StepSequencePtr sequence,
          std::shared_ptr<MidiEventNotifier> midiEventNotifier,
          std::shared_ptr<SequencerDataProvider> sequencerDataProvider)
      : sequence(std::move(sequence))
      , sequencerDataProvider(std::move(sequencerDataProvider))
      , midiNotifier(std::move(midiEventNotifier))
      , id(id)
    {
        assert(this->midiNotifier);
    }

    virtual void onLoop() {}

    void sequencerJumpBy(int steps) { jumpSteps += steps; }

    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    SequencerEngine::StepSequencePtr sequence;
    tiny_optional<int> currentStepIdx = std::nullopt;
    int jumpSteps = 0;
    std::shared_ptr<SequencerDataProvider> sequencerDataProvider;
    int instrumentId = 0;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static inline constexpr int getSourceId(int trackId)
    {
        return MidiEventNotifier::kSequencerSourceIdsStart + trackId;
    }

  private:
    std::shared_ptr<MidiEventNotifier> midiNotifier;
    NoteOffEvents noteOffs{};

    int id;
    int lastLength = 0;

    int pulseCounter = 0;
    int absolutePulseCounter = 0;
    int loopPulseCounter = 0;
    bool loop = true;

    uint8_t lastAftertouch = 0;

    State state = State::kNew;
    State prevState = State::kNew;

    void changeState(State newState);
    [[nodiscard]] bool isStarting() const;

    [[nodiscard]] tiny_optional<int> getNextStepIdx() const;
    tiny_optional<int> getNextStepPos();
    void onSequenceEvent();
    void advanceCurrentStepIdx();
    void scheduleStepNoteOffEvents(const Step& step);
};

}
