#pragma once

#include <algorithm>
#include <cassert>
#include <ranges>

#include <boost/container/static_vector.hpp>

#include "PolySynth/MidiSource.h"
#include "PolyUtil/AudioCS.h"

namespace polysynth
{
class MidiEventNotifier
{
  public:
    static constexpr int kGridSourceIdsStart = 0;
    static constexpr int kClockSourceId = 1234;
    static constexpr int kSequencerSourceIdsStart = 1000;
    static_assert(MidiSource::kExternalPortSourceIdsStart > kSequencerSourceIdsStart);

    void subscribeMidiEvents(int instrumentId,
                             std::weak_ptr<MidiSource::MidiEventCallback> callback)
    {
        midiEventsCallbacks.emplace_back(instrumentId, std::move(callback));
    }

    template<typename MidiEventsContainer>
    void notifySubscribers(int instrument,
                           const MidiEventsContainer& midiEvents,
                           int sourceId) const
    {
        for (const auto& event : midiEvents)
        {
            for (auto&& callbackIt = midiEventsCallbacks.begin();
                 callbackIt != midiEventsCallbacks.end();)
            {
                if (callbackIt->first != instrument)
                {
                    callbackIt++;
                    continue;
                }

                if (auto&& callback = callbackIt->second.lock(); callback != nullptr)
                {
                    (*callback)(event, sourceId);
                }
                callbackIt++;
            }
        };
    }

    void notifyAllSubscribers(const MidiEvent& event, int sourceId) const
    {
        for (const auto& callbackPair : midiEventsCallbacks)
        {
            if (auto&& callback = callbackPair.second.lock(); callback != nullptr)
            {
                (*callback)(event, sourceId);
            }
        }
    }

    void updateMain()
    {
        AudioCS cs;
        std::erase_if(midiEventsCallbacks,
                      [](auto& callback) { return callback.second.lock() == nullptr; });
    }

  private:
    std::vector<std::pair<int, std::weak_ptr<MidiSource::MidiEventCallback>>> midiEventsCallbacks;
};

} // polysynth
