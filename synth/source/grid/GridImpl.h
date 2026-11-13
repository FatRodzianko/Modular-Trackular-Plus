#pragma once

#include "Component.h"
#include "PolySynth/Grid.h"

#include <vector>

namespace polysynth
{

class GridImpl
  : public Grid
  , public Component
{
  public:
    void enqueuePadEvent(const Grid::PadEvent& event) override;
    void subscribePadEvents(std::weak_ptr<PadEventCallback> callback) override;

    void updateAudio() override;
    void updateMain() override;

  private:
    std::vector<std::weak_ptr<PadEventCallback>> padEventsCallbacks;
    std::vector<Grid::PadEvent> eventsQueue;
};
}
