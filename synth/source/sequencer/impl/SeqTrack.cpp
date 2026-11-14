#include "SeqTrack.h"

namespace polysynth
{

Step SeqTrack::processStep(const Step& step)
{
    auto processed = step;
    auto&& sequence = this->sequencerDataProvider->getSequencerData(instrumentId).sequences[0];
    auto&& sequenceScale =
      Musicology::Scale{ .scheme = sequence.scaleScheme, .rootNote = sequence.rootNote };
    if (Musicology::notesCount(sequenceScale.scheme) == scaleConfig.notesInScale)
    {
        processed.events.chord.rootNote = Musicology::transpose(
          processed.events.chord.rootNote, sequenceScale, scaleConfig.scale, scaleConfig.semitones);
    }
    else
    {
        auto octaveDiff = (scaleConfig.scale.rootNote / kOneOctaveNotes -
                           step.events.chord.rootNote / kOneOctaveNotes);
        processed.events.chord.rootNote = std::clamp(
          processed.events.chord.rootNote + scaleConfig.semitones + octaveDiff * kOneOctaveNotes,
          kNoteMin,
          kNoteMax);
        processed.events.chord =
          Musicology::alignToScale(processed.events.chord, scaleConfig.scale);
    }
    return processed;
}

void SeqTrack::setTransposition(const Musicology::Scale& scale_, int semitones)
{
    this->scaleConfig = { .semitones = semitones,
                          .scale = scale_,
                          .notesInScale = Musicology::notesCount(scale_.scheme) };
}
}