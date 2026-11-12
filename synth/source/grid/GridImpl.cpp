#include "GridImpl.h"
#include "PolyUtil/AudioCS.h"
#include "config.h"

#include <algorithm>
#include <cassert>
#include <memory>

namespace polysynth
{
const int Grid::PadEvent::kRows = kGridRows;
const int Grid::PadEvent::kColumns = kGridColumns;

void GridImpl::enqueuePadEvent(const Grid::PadEvent& event)
{
    assert(event.column >= 0 && event.column < event.kColumns);
    assert(event.row >= 0 && event.row < event.kRows);

    eventsQueue.push_back(event);
}

void GridImpl::subscribePadEvents(std::weak_ptr<PadEventCallback> callback)
{
    AudioCS cs;
    padEventsCallbacks.emplace_back(callback);
}

void GridImpl::updateAudio()
{
    for (const auto& event : eventsQueue)
    {
        for (auto callbackIt = padEventsCallbacks.begin(); callbackIt != padEventsCallbacks.end();)
        {
            if (auto callback = callbackIt->lock(); callback != nullptr)
            {
                (*callback)(event);
            }
            callbackIt++;
        }
    }

    eventsQueue.clear();
}

void GridImpl::updateMain()
{
    AudioCS cs;
    std::erase_if(padEventsCallbacks, [](auto& callback) { return callback.lock() == nullptr; });
}

}
