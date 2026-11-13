#pragma once

#include "PolySynth/Patch.h"

namespace polysynth
{
class SequencerDataProvider
{
  public:
    [[nodiscard]] virtual const Patch::Sequencer& getSequencerData(int instrumentId) const = 0;

    virtual ~SequencerDataProvider() = default;
};

} // polysynth
