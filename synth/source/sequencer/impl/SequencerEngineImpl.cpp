#include "SequencerEngineImpl.h"

#include "TransportSyncImpl.h"
#include "grid/MidiUtils.h"

#include "instrument/InstrumentImpl.h"

namespace polysynth
{

SequencerEngineImpl::SequencerEngineImpl(
  Synth::Allocator& allocator,
  std::shared_ptr<SequencerDataProvider> sequencerDataProvider)
  : sync(std::make_unique<TransportSyncImpl>([this] { onStart(); },
                                             [this] { onStop(); },
                                             [this] { onPulse(); },
                                             [this] { onStep(); }))
  , sequencerDataProvider{ std::move(sequencerDataProvider) }
{
    auto* poolArena =
      static_cast<StepSequence*>(allocator(kMaxSequencerTracks * sizeof(StepSequence)));

    for (int i = 0; i < kMaxSequencerTracks; i++)
    {
        stepSequencePool.add(std::unique_ptr<StepSequence, AssertDeleter<StepSequence>>(
          new (poolArena) StepSequence()));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        poolArena++;
    }
}

void SequencerEngineImpl::subscribeMidiEvents(int instrumentId,
                                              std::weak_ptr<MidiEventCallback> callback)
{
    midiNotifier->subscribeMidiEvents(instrumentId, callback);
}

void SequencerEngineImpl::updateAudio()
{
    sync->updateAudio();
}

void SequencerEngineImpl::onStep()
{
    for (auto& trackVariant : tracks)
    {
        std::visit(
          [](Track& track)
          {
              if (track.isScheduled())
              {
                  track.start();
              }
          },
          trackVariant);
    }
}

void SequencerEngineImpl::onPulse()
{
    for (auto it = tracks.begin(); it != tracks.end();)
    {
        std::visit(
          [&](auto& track)
          {
              track.pulse();

              // remove if stopped:
              if (track.isStopped())
              {
                  it = tracks.erase(it);
              }
              else
              {
                  ++it;
              }
          },
          *it);
    }
}

template<typename TrackType>
TrackType& SequencerEngineImpl::addTrack(int trackId)
{
    if (std::ssize(tracks) == kMaxSequencerTracks)
    {
        assert(false);
        tracks.erase(tracks.begin());
    }
    assert(std::ssize(tracks) < kMaxSequencerTracks);
    tracks.emplace_back(TrackVariant(std::in_place_type<TrackType>,
                                     trackId,
                                     stepSequencePool.createNullPtr(),
                                     this->midiNotifier,
                                     sequencerDataProvider));
    assert(std::holds_alternative<TrackType>(tracks.back()));
    return std::get<TrackType>(tracks.back());
}

template<TrackConcept TrackType>
TrackType* SequencerEngineImpl::getTrack(int trackId)
{
    auto trackIt = std::ranges::find_if(tracks,
                                        [trackId](const auto& trackVariant)
                                        {
                                            return std::visit([trackId](const auto& track)
                                                              { return track.getId() == trackId; },
                                                              trackVariant);
                                        });

    if (trackIt != tracks.end())
    {
        if constexpr (std::is_same_v<TrackType, Track>)
        {
            return std::visit([](auto&& track) -> TrackType* { return &track; }, *trackIt);
        }
        else
        {
            if (std::holds_alternative<TrackType>(*trackIt))
            {
                return &std::get<TrackType>(*trackIt);
            }
        }
    }
    return nullptr;
}

template<TrackConcept TrackType>
[[nodiscard]] const TrackType* SequencerEngineImpl::getTrack(int trackId) const
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return const_cast<SequencerEngineImpl*>(this)->getTrack<TrackType>(trackId);
}

void SequencerEngineImpl::addToArp(const Step::MidiEventsData& seqMidiEvents,
                                   int instrument,
                                   int stepId)
{
    auto trackId = getInstrumentTrackId(instrument);
    auto* track = getTrack<ArpTrack>(trackId);
    if (track == nullptr)
    {
        if (tracks.empty())
        {
            sync->start();
        }
        track = &addTrack<ArpTrack>(trackId);
    }

    if (track->getSequence() == nullptr)
    {
        // track doesn't have any sequence yet
        auto sequence = SequencerEngineImpl::createSequence();
        if (sequence == nullptr)
        {
            // Too many tracks
            assert(false);
            track->stop();
            return;
        }
        track->setSequence(std::move(sequence));
    }
    if (!track->isStarted())
    {
        track->startOnNextStep();
    }
    track->setInstrumentId(instrument);

    track->append(seqMidiEvents, kArpStepLengthInPulses, stepId);
    track->setSequenceLength(track->getStepsCount() * kPulsesPerStep);
    assert(track->getStepsCount() > 0);
}

void SequencerEngineImpl::removeFromArp(int instrument, int stepId)
{
    auto* track = getTrack<ArpTrack>(getInstrumentTrackId(instrument));
    if (track == nullptr)
    {
        return;
    }
    auto removed = track->removeById(stepId, kPulsesPerStep);
    if (removed)
    {
        track->setSequenceLength(track->getStepsCount() * kPulsesPerStep);
    }
    if (track->empty())
    {
        stopSequence(track->getId());
    }
}

SequencerEngine::StepSequencePtr SequencerEngineImpl::createSequence()
{
    if (stepSequencePool.empty())
    {
        return stepSequencePool.createNullPtr();
    }
    auto sequence = stepSequencePool.acquire();

    *sequence = StepSequence();

    return sequence;
}

void SequencerEngineImpl::startSequence(StepSequencePtr sequence, int instrument, int trackId)
{
    if (sequence == nullptr)
    {
        // Too many tracks
        assert(false);
        return;
    }
    auto* track = getTrack<SeqTrack>(trackId);
    if (track == nullptr)
    {
        if (tracks.empty())
        {
            sync->start();
        }
        track = &addTrack<SeqTrack>(trackId);
    }

    track->setInstrumentId(instrument);
    track->setSequence(std::move(sequence));

    if (!track->isStarted())
    {
        track->startOnNextStep();
    }
}

void SequencerEngineImpl::startSequence(int seqIdx, int instrumentId, int trackId, uint8_t velocity)
{
    (void)seqIdx;
    startSequence(
      createSequence(sequencerDataProvider->getSequencerData(instrumentId).sequences[seqIdx],
                     velocity),
      instrumentId,
      trackId);
}

void SequencerEngineImpl::stopSequence(int trackId)
{
    auto* track = getTrack(trackId);
    if (track != nullptr)
    {
        track->stop();
    }

    if (tracks.empty())
    {
        sync->stop();
    }
}

void SequencerEngineImpl::handleAftertouch(int polyAftertouch,
                                           int channelAftertouch,
                                           int trackId,
                                           int padId)
{
    auto* track = getTrack(trackId);
    if (track == nullptr)
    {
        return;
    }
    track->handleAftertouch(polyAftertouch, channelAftertouch, padId);
}

void SequencerEngineImpl::setTrackLoop(int trackId, bool loop)
{
    auto* track = getTrack(trackId);
    if (track != nullptr)
    {
        track->setLoop(loop);
    }
}

void SequencerEngineImpl::updateMain()
{
    midiNotifier->updateMain();
}

void SequencerEngineImpl::onStart()
{
    for (auto& trackVariant : tracks)
    {
        std::visit([](Track& track) { track.startOnNextStep(); }, trackVariant);
    }
}

void SequencerEngineImpl::onStop()
{
    for (auto& trackVariant : tracks)
    {
        std::visit([](Track& track) { track.stop(); }, trackVariant);
    }
}

void SequencerEngineImpl::onMidiEvent(const MidiEvent& event)
{
    sync->onMidiEvent(event);
}

void SequencerEngineImpl::connect(MidiSource& midiSource)
{
    midiEventsCallback = std::make_shared<MidiSource::MidiEventCallback>(
      [this, inputType = midiSource.getType()](const MidiEvent& event, int sourceId)
      {
          (void)sourceId;
          if (inputType == MidiSource::Type::kExternalPort)
          {
              this->onMidiEvent(event);
          }
      });
    midiSource.subscribeMidiEvents(123, midiEventsCallback);
}

void SequencerEngineImpl::setClockInternal(bool clockInternal)
{
    sync->setClockInternal(clockInternal);
}

void SequencerEngineImpl::setTransportInternal(bool transportInternal)
{
    sync->setTransportInternal(transportInternal);
}

void SequencerEngineImpl::setClockOut(bool sendClock)
{
    sync->setClockOut(sendClock);
}

void SequencerEngineImpl::setTransportOut(bool sendTransport)
{
    sync->setTransportOut(sendTransport);
}

void SequencerEngineImpl::setSyncMidiOutput(std::shared_ptr<MidiSink> midiOutput)
{
    sync->setMidiOutput(std::move(midiOutput));
}

static const int kDefaultStepLength = 7;
SequencerEngine::StepSequencePtr SequencerEngineImpl::createSequence(
  const Patch::Sequencer::Sequence& patchSequence,
  uint8_t velocity)
{
    auto sequence = createSequence();
    auto stepCount = static_cast<int>(patchSequence.steps.size());
    sequence->length = stepCount * kPulsesPerStep;
    for (int i = 0; i < stepCount; i++)
    {
        const auto& padStep = patchSequence.steps[i];
        if (padStep.chord.scheme != Musicology::kEmptyChordScheme)
        {
            assert(padStep.length > 0);
            sequence->put(
              Step((padStep.length - 1) * kPulsesPerStep + kDefaultStepLength,
                   Step::MidiEventsData{
                     .chord = padStep.chord,
                     .velocity = static_cast<uint8_t>(std::clamp<int>(
                       (velocity * padStep.velocity) / GridMapper::PadConfig::kDefaultVelocity,
                       0,
                       MidiEvent::Data::Note::kMaxVelocity)) }),
              i * kPulsesPerStep);
        }
    }
    return sequence;
}

void SequencerEngineImpl::transpose(int trackId,
                                    int semitones,
                                    const polysynth::Musicology::Scale& scale)
{
    auto* track = getTrack<SeqTrack>(trackId);
    if (track != nullptr)
    {
        track->setTransposition(scale, semitones);
    }
}

bool SequencerEngineImpl::isStarted(int trackId) const
{
    const auto* track = getTrack(trackId);
    if (track != nullptr)
    {
        return track->isStarted() || track->isScheduled();
    }
    return false;
}

std::shared_ptr<MidiSource> SequencerEngineImpl::getSyncMidiSource()
{
    return sync;
}

TempoProvider SequencerEngineImpl::getTempoProvider() const
{
    return [sync = this->sync]() {
        return sync->isClockInternal() ? sync->getTempo().value_or(kDefaultTempo)
                                       : sync->getTempo();
    };
}

} // polysynth