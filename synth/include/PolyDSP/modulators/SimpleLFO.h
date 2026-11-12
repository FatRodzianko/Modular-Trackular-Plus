#pragma once

#include "PolyDSP/Core.h"
#include "PolyDSP/config.h"
#include "PolyDSP/misc/Noise.h"
#include "PolyUtil/ExternalClockTempoMeasure.h"
#include "PolyUtil/math.h"
#include "PolyUtil/misc.h"
#include "enum_util.h"

#include <algorithm>
#include <cmath>

namespace polydsp
{
/**
 * Simplified implementation, faster and more precise than the LFO class.
 * Renders to param_t buffer, instead of param_mod_t.
 *
 */
class SimpleLFO
{
  public:
    enum class Waveform
    {
        kTriangle,
        kSin,
        kRampUp,
        kRampDown,
        kSquare,
        kRSH,
    };

    template<Waveform waveform>
    void render(Buffer<param_t> out);
    template<Waveform waveform>
    param_t render();

    void setFrequency(const RenderContext& ctx, param_t frequency)
    {
        phaseDelta = frequency / ctx.samplingRate;
    }

  private:
    float phase = 0;
    float phaseDelta = 0;
    WhiteNoise noise;
    float rshOutputValue = 0.0f;
};

template<SimpleLFO::Waveform waveform>
void SimpleLFO::render(Buffer<param_t> outBuffer)
{
    for (auto& output : outBuffer)
    {
        param_t out = 0;

        switch (waveform)
        {
            case Waveform::kSin:
            {
                param_t angle =
                  phase * std::numbers::pi_v<param_t> * 2 - std::numbers::pi_v<param_t>;
                out = parabolicSine(-angle);
                break;
            }
            case Waveform::kTriangle:
            {
                out = 1.0f - 2.0f * std::abs(bipolar(phase));
                break;
            }
            case Waveform::kRampUp:
            {
                out = bipolar(phase);
                break;
            }
            case Waveform::kRampDown:
            {
                out = -bipolar(phase);
                break;
            }
            case Waveform::kSquare:
            {
                out = phase <= 0.5f ? +1.0f : -1.0f;
                break;
            }
            case Waveform::kRSH:
            {
                out = rshOutputValue;
                break;
            }
        }

        output = out;
        phase += phaseDelta;

        while (phase >= 1.0f) [[unlikely]]
        {
            phase -= 1.0f;
            if (waveform == Waveform::kRSH)
            {
                rshOutputValue = noise.next();
            }
        }
    }
}

template<SimpleLFO::Waveform waveform>
param_t SimpleLFO::render()
{
    param_t out; // NOLINT(cppcoreguidelines-init-variables)
    switch (waveform)
    {
        case Waveform::kSin:
        {
            param_t angle = phase * std::numbers::pi_v<param_t> * 2 - std::numbers::pi_v<param_t>;
            out = parabolicSine(-angle);
            break;
        }
        case Waveform::kTriangle:
        {
            out = 1.0f - 2.0f * std::abs(bipolar(phase));
            break;
        }
        case Waveform::kRampUp:
        {
            out = bipolar(phase);
            break;
        }
        case Waveform::kRampDown:
        {
            out = -bipolar(phase);
            break;
        }
        case Waveform::kSquare:
        {
            out = phase <= 0.5f ? +1.0f : -1.0f;
            break;
        }
        case Waveform::kRSH:
        {
            out = rshOutputValue;
            break;
        }
    }

    phase += phaseDelta;

    if (phase >= 1.0f) [[unlikely]]
    {
        phase -= 1.0f;
        assert(phase < 1.0f);
        if (waveform == Waveform::kRSH)
        {
            rshOutputValue = noise.next();
        }
    }

    return out;
}
}