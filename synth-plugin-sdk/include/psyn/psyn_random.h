#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace psyn
{

/**
 * Lightweight deterministic xorshift32 PRNG for .psyn synth patch randomization.
 * Zero heap allocations, zero libc rand() dependency, safe for Cortex-M7 plugins.
 */
class Rng
{
  public:
    explicit Rng(uint32_t seed) : state(seed != 0 ? seed : 0xA341316Cu)
    {
        // Mix initial seed thoroughly
        (void)nextU32();
        (void)nextU32();
    }

    uint32_t nextU32()
    {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    /// Uniform float in [0.0f, 1.0f]
    float nextFloat() { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777215.0f); }

    /// Uniform float in [minVal, maxVal]
    float range(float minVal, float maxVal) { return minVal + (maxVal - minVal) * nextFloat(); }

    /// Exponentially biased float in [minVal, maxVal] (minVal > 0, maxVal > minVal),
    /// useful for frequencies, envelope times, and LFO rates.
    float expRange(float minVal, float maxVal)
    {
        if (minVal <= 0.0f || maxVal <= minVal)
        {
            return range(minVal, maxVal);
        }
        return minVal * std::pow(maxVal / minVal, nextFloat());
    }

    /// Power-curved float in [minVal, maxVal] (power > 1 biases toward minVal, < 1 toward maxVal).
    float powRange(float minVal, float maxVal, float power)
    {
        return minVal + (maxVal - minVal) * std::pow(nextFloat(), power);
    }

    /// Uniform integer in [minVal, maxVal] inclusive
    int rangeInt(int minVal, int maxVal)
    {
        if (maxVal <= minVal)
        {
            return minVal;
        }
        const uint32_t span = static_cast<uint32_t>(maxVal - minVal) + 1u;
        return minVal + static_cast<int>(nextU32() % span);
    }

    /// Returns true with probability p in [0.0f, 1.0f]
    bool chance(float p) { return nextFloat() < p; }

    /// Uniform random element from an initializer_list
    template <typename T> T choice(std::initializer_list<T> items)
    {
        const auto idx = static_cast<size_t>(rangeInt(0, static_cast<int>(items.size()) - 1));
        return *(items.begin() + idx);
    }

  private:
    uint32_t state;
};

} // namespace psyn
