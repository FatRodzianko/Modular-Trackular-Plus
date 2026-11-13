#pragma once

#include "PolyDSP/Core.h"

namespace polydsp
{
class Ramp
{
  public:
    void setup(const RenderContext& ctx, time_t time, param_t startValue, param_t endValue);
    void start();
    void start(const RenderContext& ctx, time_t time, param_t startValue, param_t endValue);
    bool render(ModulationBuffer out);

  private:
    param_t delta = 0;
    param_t startValue = 0;
    param_t endValue = 0;
    param_t outValue = 0;

    param_t coeff = 0;
    param_t offset = 0;
};
}