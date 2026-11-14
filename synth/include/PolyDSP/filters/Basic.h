#pragma once

#include "PolyDSP/Core.h"

#include <numbers>

namespace polydsp
{

// based on Will Pirkle book
class OnePoleFilter
{
  public:
    inline sample_t tick(sample_t s)
    {
        s = s + feedback;
        auto vn = (s - z1) * alpha;
        auto out = vn + z1;
        z1 = vn + out;

        feedback_out = beta * z1;

        return out;
    }

    inline void setFeedback(param_t fb) { feedback = fb; }
    [[nodiscard]] inline float getFeedbackOutput() const { return feedback_out; }
    inline void setAlpha(param_t a) { alpha = a; };
    inline void setBeta(param_t b) { beta = b; };

  private:
    float alpha = 1.0;
    float beta = 0.0;
    float feedback = 0.0;
    float z1 = 0.0;
    float feedback_out = 0.0;
};

/**
 * First order exponential averaging low-pass filter,
 * useful e.g. for smoothing our parameter changes.
 */
template<typename ValueType>
class AveragingFilter
{
  public:
    auto operator()(auto input, auto coeff)
    {
        input += coeff * (accumulator - input);
        accumulator = input;
        return input;
    };

    void reset() { accumulator = 0.0f; };

  private:
    ValueType accumulator = {};
};

/**
 * Simple HPF for DC blocking.
 *
 * @see https://www.musicdsp.org/en/latest/Filters/135-dc-filter.html
 */
class DCBlocker
{
  public:
    void setup(const RenderContext& ctx, float cutoffFreq)
    {
        R = 1.0f - (2.0f * std::numbers::pi_v<param_t> * cutoffFreq / ctx.samplingRate);
    }

    void render(std::span<sample_t> buf)
    {
        for (auto& sample : buf)
        {
            auto y = sample - x0 + R * y0;
            x0 = sample;
            y0 = y;
            sample = y;
        };
    }

  private:
    param_t R = 0.0f;
    sample_t x0 = 0.0f;
    sample_t y0 = 0.0f;
};
}