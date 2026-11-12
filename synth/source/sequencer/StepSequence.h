#pragma once

#include "Sequence.h"
#include "Step.h"

#include "config.h"

namespace polysynth
{
class StepSequence : public Sequence<Step>
{
  public:
    tiny_optional<int> removeById(uint8_t id, seqpos_t shift = 0)
    {
        tiny_optional<int> removedIdx = std::nullopt;
        for (int i = 0; i < ssize();)
        {
            if (removedIdx)
            {
                if (shift == 0)
                {
                    break;
                }
                assert(getSteps()[i].first >= shift);
                getSteps()[i].first -= shift;
            }
            else if (getSteps()[i].second.id == id)
            {
                removedIdx = i;
                remove(i);
                continue;
            }
            i++;
        }
        return removedIdx;
    }

    std::optional<std::reference_wrapper<Step>> getById(uint8_t id)
    {
        auto step =
          std::ranges::find_if(getSteps(), [id](auto& item) { return item.second.id == id; });
        if (step != getSteps().end())
        {
            return step->second;
        }
        return {};
    }

    int length = 0;
};
}
