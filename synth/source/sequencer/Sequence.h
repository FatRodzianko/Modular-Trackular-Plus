#pragma once

#include "PolySynth/MidiSource.h"
#include "PolySynth/config.h"
#include "Step.h"
#include "boost/container/static_vector.hpp"

#include <algorithm>
#include <optional>

namespace polysynth
{

template<typename Item>
class Sequence
{
  public:
    using seqpos_t = int;

    bool operator==(const Sequence& rhs) const { return steps == rhs.steps; }
    bool operator!=(const Sequence& rhs) const { return !(rhs == *this); }

    [[nodiscard]] bool empty() const { return steps.empty(); }

    [[nodiscard]] int ssize() const { return std::ssize(steps); }

    Item& get(unsigned int index) { return steps.at(index).second; }

    [[nodiscard]] const Item& get(unsigned int index) const { return steps.at(index).second; }

    [[nodiscard]] int getPosition(unsigned int index) const { return steps.at(index).first; }

    [[nodiscard]] int getFirstPosition() const { return steps.front().first; }

    Item& getFirst() { return steps.front().second; }

    [[nodiscard]] int getLastPosition() const { return steps.back().first; }

    void popFirst() { steps.erase(steps.begin()); }

    void popLast() { steps.pop_back(); }

    bool removeByPos(seqpos_t pos)
    {
        for (int i = 0; i < std::ssize(steps); i++)
        {
            if (steps[i].first == pos)
            {
                steps.erase(steps.begin() + i);
                return true;
            }
        }
        return false;
    }

    void remove(unsigned int index)
    {
        assert(index < steps.size());
        steps.erase(steps.begin() + index);
    }

    void shift(seqpos_t fromPos, seqpos_t offset)
    {
        std::ranges::for_each(steps,
                              [fromPos, offset](std::pair<seqpos_t, Item>& item)
                              {
                                  if (item.first >= fromPos)
                                  {
                                      item.first += offset;
                                  }
                              });
    }

    /**
     * Gets prev item index by given position. O(N)
     * @param itemPos search position
     * @return index of item before 'itemPos'. std::nullopt if theres no such item
     */
    [[nodiscard]] tiny_optional<int> getPrevItemIdx(seqpos_t itemPos) const
    {
        // TODO(karol): bsearch can be implemented here
        int i = 0;
        for (i = 0; i < std::ssize(steps); i++)
        {
            if (steps[i].first >= itemPos)
            {
                return (i >= 1) ? tiny_optional<int>(i - 1) : std::nullopt;
            }
        }
        return (i > 0) ? tiny_optional<int>(i) : std::nullopt;
    }

    /**
     * Puts item into container sorted by 'itemPos'. Time complexity O(N)
     * @param item
     * @param itemPos
     */
    void put(Item item, seqpos_t itemPos)
    {
        if (steps.empty())
        {
            steps.emplace_back(itemPos, item);
            return;
        }

        int i = 0;
        for (; i < std::ssize(steps); i++)
        {
            if (steps[i].first == itemPos)
            {
                steps[i].second = std::move(item);
                return;
            }
            if (steps[i].first > itemPos)
            {
                if (i + 1 < kMaxSequenceLength)
                {
                    steps.insert(steps.begin() + i, { itemPos, item });
                }
                return;
            }
        }
        assert(i < kMaxSequenceLength);
        if (i == std::ssize(steps))
        {
            steps.push_back({ itemPos, item });
        }
    }

    template<typename Callback>
    void forEach(Callback func)
    {
        std::ranges::for_each(steps, [&func](auto& item) { func(item.second); });
    }

  protected:
    boost::container::static_vector<std::pair<seqpos_t, Item>, kMaxSequenceLength>& getSteps()
    {
        return steps;
    }

  private:
    boost::container::static_vector<std::pair<seqpos_t, Item>, kMaxSequenceLength> steps;
};

}