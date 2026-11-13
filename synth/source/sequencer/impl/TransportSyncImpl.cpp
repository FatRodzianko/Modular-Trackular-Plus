#include "TransportSyncImpl.h"

namespace polysynth
{
void TransportSyncImpl::start()
{
    if ((!externalTransportRunning && !isWaitForExternalStart()) || isTransportInternal())
    {
        doStart();
    }
}

void TransportSyncImpl::stop()
{
    if ((!externalTransportRunning && !isWaitForExternalStart()) || isTransportInternal())
    {
        doStop();
    }
}

void TransportSyncImpl::subscribeMidiEvents(int instrumentId,
                                            std::weak_ptr<MidiEventCallback> callback)
{
    publisher.subscribeMidiEvents(instrumentId, std::move(callback));
}

void TransportSyncImpl::onMidiEvent(const MidiEvent& event)
{
    switch (event.type)
    {
        case MidiEvent::Type::kClock:
        {
            if (!isClockInternal())
            {
                extClockMeasure.onExternalPulse();
                for (int i = 0; i < kSeqPpqn / kMidiPpqn; i++)
                {
                    pulse();
                }
            }
            break;
        }
        case MidiEvent::Type::kStart:
        {
            if (!isTransportInternal())
            {
                externalTransportRunning = true;
                doStart();
            }
            break;
        }
        case MidiEvent::Type::kStop:
        {
            if (!isTransportInternal())
            {
                externalTransportRunning = false;
                doStop();
            }
            break;
        }
        default:
            break;
    }
}

void TransportSyncImpl::updateAudio()
{
    if (!isClockInternal())
    {
        extClockMeasure.onUpdate();
        tempoBpm = extClockMeasure.getIndicatedTempo();
        return;
    }
    pulseTime += kUpdatePeriod;
    float const pulsePeriod = (60.0f / tempoBpm.value_or(kDefaultTempo)) / kSeqPpqn;

    while (pulseTime >= pulsePeriod)
    {
        pulseTime -= pulsePeriod;
        pulse();
    }
}

void TransportSyncImpl::pulse()
{
    if (running)
    {
        if (pulseCounter % kPulsesPerStep == 0 && stepEvent)
        {
            stepEvent();
        }
    }
    pulseEvent();
    if (isClockOut() && pulseCounter % (kSeqPpqn / kMidiPpqn) == 0)
    {
        publisher.notifyAllSubscribers(kClockEvent, MidiEventNotifier::kClockSourceId);
        if (midiOutput)
        {
            midiOutput->onMidiEvent(kClockEvent);
        }
    }
    pulseCounter++;
}

void TransportSyncImpl::doStart()
{
    running = true;
    pulseCounter = 0;
    startEvent();
    if (isTransportOut())
    {
        publisher.notifyAllSubscribers(kStartEvent, MidiEventNotifier::kClockSourceId);
        if (midiOutput)
        {
            midiOutput->onMidiEvent(kStartEvent);
        }
    }
}

void TransportSyncImpl::doStop()
{
    running = false;
    stopEvent();
    if (isTransportOut())
    {
        publisher.notifyAllSubscribers(kStopEvent, MidiEventNotifier::kClockSourceId);
        if (midiOutput)
        {
            midiOutput->onMidiEvent(kStopEvent);
        }
    }
}

void TransportSyncImpl::setMidiOutput(std::shared_ptr<MidiSink> midiOutput_)
{
    this->midiOutput = std::move(midiOutput_);
}

} // polysynth