#pragma once
#include "SequencerDataProvider.h"

namespace polysynth
{

class LambdaSequencerDataProvider : public SequencerDataProvider
{
  public:
    explicit LambdaSequencerDataProvider(
      std::function<const Patch::Sequencer&(int)> getDataFunction)
      : getDataFunction_(std::move(getDataFunction))
    {
    }
    [[nodiscard]] const Patch::Sequencer& getSequencerData(int instrumentId) const override
    {
        return getDataFunction_(instrumentId);
    }

  private:
    std::function<const Patch::Sequencer&(int)> getDataFunction_{};
};
} // polysynth
