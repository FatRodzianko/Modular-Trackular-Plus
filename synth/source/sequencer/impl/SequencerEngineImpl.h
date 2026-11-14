#pragma once

#include "Component.h"
#include "MidiEventNotifier.h"
#include "PolySynth/MidiSource.h"
#include "PolySynth/Synth.h"

#include <boost/container/static_vector.hpp>
#include <utility>

#include "../NoteOffEvents.h"
#include "../Sequence.h"
#include "../SequencerEngine.h"
#include "../Step.h"
#include "ArpTrack.h"
#include "ObjectPool.h"
#include "SeqTrack.h"
#include "SequencerDataProvider.h"
#include "SynthImpl.h"
#include "Track.h"
#include "config.h"
#include "sequencer/TransportSync.h"

namespace polysynth
{

template<typename T>
concept TrackConcept =
  std::is_same_v<T, ArpTrack> || std::is_same_v<T, SeqTrack> || std::is_same_v<T, Track>;

class SequencerEngineImpl : public SequencerEngine
{
  public:
    using TrackVariant = std::variant<ArpTrack, SeqTrack>;

    static constexpr int kArpStepLengthInPulses = 4;

    SequencerEngineImpl(Synth::Allocator& allocator,
                        std::shared_ptr<SequencerDataProvider> sequencerDataProvider);
    void subscribeMidiEvents(int instrumentId, std::weak_ptr<MidiEventCallback> callback) override;
    void updateAudio() override;
    void updateMain() override;

    void stopSequence(int trackId) override;
    void addToArp(const Step::MidiEventsData& seqMidiEvents, int instrument, int stepId) override;
    void removeFromArp(int instrument, int stepId) override;
    [[nodiscard]] StepSequencePtr createSequence() override;
    void startSequence(StepSequencePtr sequence, int instrument, int trackId);
    void startSequence(int seqIdx, int instrumentId, int trackId, uint8_t velocity) override;

    [[nodiscard]] bool isStarted(int trackId) const override;

    void setTempo(float tempo) override { sync->setTempo(tempo); }
    [[nodiscard]] TempoProvider getTempoProvider() const override;

    void setTrackLoop(int trackId, bool loop) override;

    void handleAftertouch(int polyAftertouch,
                          int channelAftertouch,
                          int trackId,
                          int padId) override;

    void onMidiEvent(const polysynth::MidiEvent& event) override;

    void connect(polysynth::MidiSource& midiSource) override;

    void setClockInternal(bool clockInternal) override;
    void setTransportInternal(bool transportInternal) override;
    void setClockOut(bool sendClock) override;
    void setTransportOut(bool sendTransport) override;

    void setWaitForExternalStart(bool wait) override { sync->setWaitForExternalStart(wait); }

    void setSyncMidiOutput(std::shared_ptr<MidiSink> midiOutput) override;

    void transpose(int trackId, int semitones, const polysynth::Musicology::Scale& scale) override;

    std::shared_ptr<MidiSource> getSyncMidiSource() override;

  private:
    boost::container::static_vector<TrackVariant, kMaxSequencerTracks> tracks;
    StepSequencePool stepSequencePool;

    std::shared_ptr<MidiEventNotifier> midiNotifier = std::make_shared<MidiEventNotifier>();

    std::shared_ptr<TransportSync> sync;
    std::shared_ptr<MidiEventCallback> midiEventsCallback;

    std::shared_ptr<SequencerDataProvider> sequencerDataProvider;

    /**
     * Gets track by trackId. Can retrieve specific types by providing TrackType.
     * To deal with tracks in a generic way you can provide TrackType = Track or no template param
     * @tparam TrackType this is defined by TrackConcept.
     * @param trackId id of the track
     * @return TrackType* or nullptr if not found
     */
    template<TrackConcept TrackType = Track>
    TrackType* getTrack(int trackId);

    template<TrackConcept TrackType = Track>
    const TrackType* getTrack(int trackId) const;

    /**
     * Creates a track with specific 'id'.
     * Drops first track if there's no room to create new. (kMaxTracks)
     * @tparam TrackType specific type of Track to be created
     * @param trackId Track id
     * @return reference to created Track
     */
    template<typename TrackType>
    TrackType& addTrack(int trackId);

    void onPulse();
    void onStart();
    void onStop();
    void onStep();
    StepSequencePtr createSequence(const Patch::Sequencer::Sequence& patchSequence,
                                   uint8_t velocity);
};

} // polysynth
