#include "Track.h"
#include <grid/MidiUtils.h>

namespace polysynth
{

void Track::put(const Step::MidiEventsData& midiEvents, int length_, int stepIdx)
{
    assert(sequence);
    sequence->put(Step(length_, midiEvents), stepIdx * kPulsesPerStep);
}

void Track::remove(unsigned int stepIndex)
{
    assert(sequence);
    sequence->remove(stepIndex);
}

void Track::pulse()
{
    assert(sequence);

    if (state == State::kStarted)
    {
        if ((lastLength != sequence->length) && !isStarting())
        {
            // TODO(karol): check if the line below is necessary
            pulseCounter = (loopPulseCounter + 1) % (sequence->length);
            currentStepIdx = sequence->getPrevItemIdx(pulseCounter);
        }
        if (jumpSteps != 0)
        {
            pulseCounter += jumpSteps * kPulsesPerStep;
            currentStepIdx = sequence->getPrevItemIdx(pulseCounter);
            jumpSteps = 0;
        }
        lastLength = sequence->length;
        loopPulseCounter = (loop) ? pulseCounter % (sequence->length) : pulseCounter;

        if (loopPulseCounter == 0)
        {
            onLoop();
            currentStepIdx = std::nullopt;
        }
        auto nextStepPos = getNextStepPos();
        if (isStarted() && nextStepPos && loopPulseCounter >= *nextStepPos)
        {
            advanceCurrentStepIdx();
            if (getCurrentStep())
            {
                onSequenceEvent();
            }
        }
    }

    while (!noteOffs.empty() && absolutePulseCounter >= noteOffs.getFirstPosition())
    {
        const auto& noteOffsPerInstrument = noteOffs.getFirst();
        midiNotifier->notifySubscribers(
          instrumentId,
          MidiUtils::createMidiNoteOffEvents<Step::kMaxMidiEventsPerStep>(
            noteOffsPerInstrument.events),
          getSourceId(id));
        noteOffs.popFirst();
    }

    if (state == State::kStopping && noteOffs.empty())
    {
        state = State::kStopped;
    }

    pulseCounter++;
    absolutePulseCounter++;
    prevState = state;
}

std::optional<std::reference_wrapper<Step>> Track::getCurrentStep()
{
    assert(sequence);
    if (currentStepIdx && *currentStepIdx < sequence->ssize())
    {
        return sequence->get(*currentStepIdx);
    }
    return {};
}

std::optional<std::reference_wrapper<const Step>> Track::getCurrentStep() const
{
    assert(sequence);
    if (currentStepIdx && *currentStepIdx < sequence->ssize())
    {
        return sequence->get(*currentStepIdx);
    }
    return {};
}

[[nodiscard]] tiny_optional<int> Track::getNextStepIdx() const
{
    assert(sequence);
    if (sequence->empty())
    {
        return std::nullopt;
    }
    if (!currentStepIdx)
    {
        if (sequence->getPosition(0) < sequence->length)
        {
            return 0;
        }
    }
    if (!currentStepIdx || *currentStepIdx + 1 >= sequence->ssize() ||
        sequence->length <= sequence->getPosition(*currentStepIdx + 1))
    {
        return std::nullopt;
    }
    return *currentStepIdx + 1;
}

tiny_optional<int> Track::getNextStepPos()
{
    assert(sequence);
    auto nextStepIdx = getNextStepIdx();
    if (nextStepIdx)
    {
        return sequence->getPosition(*nextStepIdx);
    }
    return {};
}

void Track::advanceCurrentStepIdx()
{
    assert(sequence);
    if (sequence->empty())
    {
        return;
    }
    if (!currentStepIdx.has_value())
    {
        currentStepIdx = 0;
        return;
    }
    (*currentStepIdx)++;
    if (currentStepIdx >= sequence->ssize() ||
        sequence->length <= sequence->getPosition(*currentStepIdx))
    {
        currentStepIdx = std::nullopt;
    }
}

void Track::changeState(Track::State newState)
{
    state = newState;
}

bool Track::isStarting() const
{
    return isStarted() && (prevState == State::kNew || prevState == State::kScheduled);
}

void Track::start()
{
    assert(sequence);
    changeState(State::kStarted);
    pulseCounter = 0;
    loopPulseCounter = 0;
    lastLength = sequence->length;
    currentStepIdx = std::nullopt;
}

void Track::stop()
{
    changeState(State::kStopping);

    noteOffs.forEach(
      [this](const NoteOffEvent& noteOff)
      {
          midiNotifier->notifySubscribers(
            instrumentId,
            MidiUtils::createMidiNoteOffEvents<NoteOffEvent::kMaxNumberOfNoteOffs>(noteOff.events),
            getSourceId(id));
      });
    noteOffs.clear();
}

void Track::startOnNextStep()
{
    changeState(State::kScheduled);
}

void Track::onSequenceEvent()
{
    auto currentStep = getCurrentStep();
    if (!currentStep)
    {
        assert(false);
        return;
    }
    auto&& step = processStep(currentStep->get());
    int sourceId = MidiEventNotifier::kSequencerSourceIdsStart + id;
    midiNotifier->notifySubscribers(instrumentId,
                                    MidiUtils::createMidiNoteOnEvents<Step::kMaxMidiEventsPerStep>(
                                      step.events.chord, step.events.velocity),
                                    sourceId);

    auto aftertouch = getCurrentAftertouch();
    midiNotifier->notifySubscribers(
      instrumentId,
      MidiUtils::createMidiAftertouchEvents<Step::kMaxMidiEventsPerStep>(
        step.events.chord, aftertouch, aftertouch),
      sourceId);

    scheduleStepNoteOffEvents(step);
}

void Track::handleAftertouch(int polyAftertouch, int channelAftertouch, int /*padIdx*/)
{
    auto currentStep = getCurrentStep();
    lastAftertouch = polyAftertouch;
    if (currentStep)
    {
        auto events = MidiUtils::createMidiAftertouchEvents<Step::kMaxMidiEventsPerStep>(
          currentStep->get().events.chord, polyAftertouch, channelAftertouch);
        int sourceId = MidiEventNotifier::kSequencerSourceIdsStart + id;
        midiNotifier->notifySubscribers(instrumentId, events, sourceId);
    }
}

void Track::scheduleStepNoteOffEvents(const Step& step)
{
    boost::container::static_vector<uint8_t, NoteOffEvent::kMaxNumberOfNoteOffs> notes;
    std::ranges::copy(Musicology::getNotesView(step.events.chord), std::back_inserter(notes));
    noteOffs.insert({ notes }, absolutePulseCounter + step.length);
}

void Track::setSequence(SequencerEngine::StepSequencePtr sequencePtr)
{
    this->sequence = std::move(sequencePtr);
}
}