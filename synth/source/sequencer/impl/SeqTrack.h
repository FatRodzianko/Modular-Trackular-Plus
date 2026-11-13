#pragma once

#include "Track.h"
namespace polysynth
{

class SeqTrack final : public Track
{
  public:
    SeqTrack(int id,
             SequencerEngine::StepSequencePtr sequence,
             std::shared_ptr<MidiEventNotifier> midiEventNotifier,
             std::shared_ptr<SequencerDataProvider> sequencerDataProvider)
      : Track(id,
              std::move(sequence),
              std::move(midiEventNotifier),
              std::move(sequencerDataProvider))
    {
        auto&& seq = this->sequencerDataProvider->getSequencerData(instrumentId).sequences[0];
        setTransposition({ seq.scaleScheme, seq.rootNote }, 0);
    }
    void setTransposition(const Musicology::Scale& scale_, int semitones);

  protected:
    Step processStep(const polysynth::Step& step) override;

  private:
    struct
    {
        int semitones = 0;
        Musicology::Scale scale = Musicology::createScale(0, 0);
        size_t notesInScale = 0;
    } scaleConfig{};
};

} // polysynth
