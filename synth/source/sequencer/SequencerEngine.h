#pragma once

#include "Component.h"
#include "ObjectPool.h"
#include "PolySynth/GridMapper.h"
#include "PolySynth/MidiSink.h"
#include "PolySynth/Patch.h"
#include "PolySynth/Sequencer.h"
#include "PolySynth/Synth.h"
#include "StepSequence.h"
#include "config.h"

namespace polysynth
{
class SequencerEngine
  : public Sequencer
  , public MidiSource
  , public MidiSink
  , public Component
{
  public:
    using StepSequencePool =
      ObjectPool<StepSequence, kMaxSequencerTracks, AssertDeleter<StepSequence>>;
    using StepSequencePtr = StepSequencePool::ptr_type;

    SequencerEngine()
      : MidiSource(MidiSource::Type::kInternalKeyboard)
    {
        this->impl = this;
    }

    /**
     * Function gets a track id that is reserved for an instrument
     * @param instrumentId id of instrument
     * @return Track id that is specific for particular instrument and distinct from padIdx
     */
    static constexpr int getInstrumentTrackId(int instrumentId)
    {
        return kGridColumns * kGridRows + instrumentId;
    }

    /**
     * Appends the seqMidiEvents to the existing arpeggio for instrument or plays a new arpeggio if
     * there's no such yet.
     * @param seqMidiEvents midiEvents to append to arpeggio
     * @param instrument id of an instrument
     * @param stepId used to identify this particular step in arpeggio
     */
    virtual void addToArp(const Step::MidiEventsData& seqMidiEvents,
                          int instrument,
                          int stepId) = 0;

    /**
     * Removes arpeggio step by given stepId and instrument.
     * @param instrument used to determine which arpeggio shall be altered
     * @param stepId id of step to be removed
     */
    virtual void removeFromArp(int instrument, int stepId) = 0;

    /**
     * Creates sequence object using internal pool stored in external RAM.
     */
    [[nodiscard]] virtual StepSequencePtr createSequence() = 0;
    /**
     * Passes aftertouch value to the currently playing sequence
     * @param polyAftertouch poly aftertouch value to be passed
     * @param channelAftertouch channel aftertouch value to be passed
     * @param trackId id of a track
     */
    virtual void handleAftertouch(int polyAftertouch,
                                  int channelAftertouch,
                                  int trackId,
                                  int padId) = 0;

    virtual void setTrackLoop(int trackId, bool loop) = 0;

    virtual void connect(MidiSource& midiSource) = 0;

    virtual void setSyncMidiOutput(std::shared_ptr<MidiSink> midiOutput) = 0;

    virtual void transpose(int trackId,
                           int semitones,
                           const polysynth::Musicology::Scale& scale) = 0;

    virtual std::shared_ptr<MidiSource> getSyncMidiSource() = 0;
};

}
