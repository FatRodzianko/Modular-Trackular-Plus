#include "ArpTrack.h"
#include <chrono>
#include <random>

namespace polysynth
{
uint8_t ArpTrack::getCurrentAftertouch() const
{
    auto currentStep = getCurrentStep();
    if (!currentStep)
    {
        assert(false);
        return 0;
    }

    // assumption that the id of the step is the index of pad
    return aftertouchValues[currentStep->get().id];
}

void ArpTrack::handleAftertouch(int polyAftertouch, int /*channelAftertouch*/, int padIdx)
{
    setPadAftertouch(polyAftertouch, padIdx);
}

void ArpTrack::onLoop()
{
    if (!loopCounter)
    {
        loopCounter = 0;
    }
    else
    {
        (*loopCounter)++;
    }
    regenerateOrder();
}

Step ArpTrack::processStep(const Step& step)
{
    if (!loopCounter)
    {
        loopCounter = 0;
    }
    auto generatedStep = step;
    auto octave = 0;
    auto arpConfig = this->sequencerDataProvider->getSequencerData(instrumentId).config.arp;
    if(lastConfig != arpConfig)
    {
        regenerateOrder();
        lastConfig = arpConfig;
    }
    switch (arpConfig.arpStyle)
    {
        case Patch::Sequencer::Config::Arp::ArpStyle::kUp:
        case Patch::Sequencer::Config::Arp::ArpStyle::kPlayOrder:
            octave = (*loopCounter) % arpConfig.octaveRange;
            break;
        case Patch::Sequencer::Config::Arp::ArpStyle::kDown:
            octave = (arpConfig.octaveRange - (*loopCounter) % arpConfig.octaveRange - 1);
            break;
        case Patch::Sequencer::Config::Arp::ArpStyle::kRandom:
            octave = rand() % arpConfig.octaveRange; // NOLINT(concurrency-mt-unsafe)
            break;
    }
    generatedStep.events.chord.rootNote += octave * kOneOctaveNotes;
    return generatedStep;
}

std::optional<std::reference_wrapper<Step>> ArpTrack::getCurrentStep()
{
    assert(sequence);
    if (currentStepIdx)
    {
        auto currestStepAfterJump = (*currentStepIdx + this->jumpSteps);
        if (currestStepAfterJump < sequence->ssize() && currestStepAfterJump >= 0 &&
            currestStepAfterJump < std::ssize(order))
        {
            return sequence->get(order[currestStepAfterJump]);
        }
    }
    return {};
}

std::optional<std::reference_wrapper<const Step>> ArpTrack::getCurrentStep() const
{
    assert(sequence);
    if (currentStepIdx)
    {
        auto currestStepAfterJump = (*currentStepIdx + this->jumpSteps);
        if (currestStepAfterJump < sequence->ssize() && currestStepAfterJump >= 0 &&
            currestStepAfterJump < std::ssize(order))
        {
            assert(currestStepAfterJump < std::ssize(order));
            return sequence->get(order[currestStepAfterJump]);
        }
    }
    return {};
}

bool ArpTrack::removeById(uint8_t stepId, StepSequence::seqpos_t shift)
{
    assert(sequence);

    auto removedIdx = sequence->removeById(stepId, shift);
    if (removedIdx)
    {
        int orderIdxToRemove = 0;

        for (int orderIdx = 0; orderIdx < std::ssize(order); orderIdx++)
        {
            if (order[orderIdx] > *removedIdx)
            {
                order[orderIdx]--;
            }
            else if (order[orderIdx] == removedIdx)
            {
                orderIdxToRemove = orderIdx;
            }
        }
        order.erase(order.begin() + orderIdxToRemove);
        if (currentStepIdx && orderIdxToRemove < currentStepIdx)
        {
            sequencerJumpBy(-1);
        }
        return true;
    }
    return false;
}

void ArpTrack::append(const Step::MidiEventsData& midiEvents, int length_, uint8_t stepId)
{
    assert(sequence);
    if (sequence->empty())
    {
        sequence->put(Step(length_, midiEvents, stepId), 0);
    }
    else
    {
        auto lastPosition = sequence->getLastPosition();
        sequence->put(Step(length_, midiEvents, stepId), lastPosition + kPulsesPerStep);
    }

    int orderIdx = sequence->ssize() - 1;

    auto arpConfig = this->sequencerDataProvider->getSequencerData(instrumentId).config.arp;
    switch (arpConfig.arpStyle)
    {
        case Patch::Sequencer::Config::Arp::ArpStyle::kUp:

        {
            auto currentStep = getCurrentStep();
            if (sequence->ssize() > 1 && currentStep &&
                currentStep->get().events.chord.rootNote > midiEvents.chord.rootNote)
            {
                sequencerJumpBy(1);
            }
            break;
        }
        case Patch::Sequencer::Config::Arp::ArpStyle::kDown:
        {
            auto currentStep = getCurrentStep();
            if (sequence->ssize() > 1 && currentStep &&
                currentStep->get().events.chord.rootNote < midiEvents.chord.rootNote)
            {
                sequencerJumpBy(1);
            }
            break;
        }
        case Patch::Sequencer::Config::Arp::ArpStyle::kPlayOrder:
        case Patch::Sequencer::Config::Arp::ArpStyle::kRandom:
            break;
    }

    order.insert(order.begin() + orderIdx, sequence->ssize() - 1);
    regenerateOrder();
}

void ArpTrack::regenerateOrder()
{
    if (std::ssize(order) < 2)
    {
        return;
    }
    auto arpConfig = this->sequencerDataProvider->getSequencerData(instrumentId).config.arp;
    switch (arpConfig.arpStyle)
    {
        case Patch::Sequencer::Config::Arp::ArpStyle::kUp:
            std::ranges::sort(order,
                              [this](const uint8_t& idxA, const uint8_t& idxB) {
                                  return sequence->get(idxA).events.chord.rootNote <=
                                         sequence->get(idxB).events.chord.rootNote;
                              });
            break;
        case Patch::Sequencer::Config::Arp::ArpStyle::kDown:
            std::ranges::sort(order,
                              [this](const uint8_t& idxA, const uint8_t& idxB) {
                                  return sequence->get(idxA).events.chord.rootNote >
                                         sequence->get(idxB).events.chord.rootNote;
                              });
            break;
        case Patch::Sequencer::Config::Arp::ArpStyle::kPlayOrder:
            std::ranges::generate(order, [n = 0]() mutable { return n++; });
            break;
        case Patch::Sequencer::Config::Arp::ArpStyle::kRandom:
            std::shuffle(order.begin(), order.end(), gen);
            break;
    }
}

} // wave
