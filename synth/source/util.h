#pragma once

#include "PolyDSP/Core.h"

namespace polydsp
{

class UtilBuffers
{
  public:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static ModulationBuffer modulationZero;
};

inline float calculatePolyBLEP_2(float phase, float phaseInc)
{
    float blepCorrection = 0.0f;

    if (phase > 1.0f - phaseInc)
    {
        float t = (phase - 1.0f) / phaseInc;
        blepCorrection = t * t + 2.0f * t + 1.0f;
    }
    else if (phase < phaseInc)
    {
        float t = phase / phaseInc;
        blepCorrection = 2.0f * t - t * t - 1.0f;
    }
    else
    {
        return 0.0f;
    }

    return -blepCorrection;
}

}