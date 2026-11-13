#pragma once

#include "PolyDSP/Core.h"

#include <algorithm>
#include <bit>
#include <span>

namespace polydsp
{

/**
 * Fractional delay line using a circular buffer and linear interpolation.
 */
template<size_t kSize>
class DelayLine
{
  public:
    explicit DelayLine(std::span<sample_t, kSize> buffer)
      : buffer(buffer)
    {
        std::ranges::fill(buffer, 0);
    };

    static constexpr size_t getSize() { return kSize; };

    void write(sample_t sample);

    /**
     * @param delay in samples
     */
    sample_t read(param_t delay);

    /**
     * @param delay in samples
     */
    sample_t readHermite(param_t delay);

    /**
     * @param delay in samples
     */
    void prefetch(int delay);

    static_assert(std::has_single_bit(kSize), "Size needs to be power of 2");

  private:
    std::span<sample_t, kSize> buffer;
    int writeIdx = 0;

    sample_t read(int idx);
    sample_t readAndPrefetch(int idx);

    static constexpr size_t getSizeMask() { return kSize - 1; };
    static constexpr sample_t interpolate(auto fraction, auto x1, auto x2);
};

template<size_t kSize>
void DelayLine<kSize>::write(sample_t sample)
{
    buffer[writeIdx] = sample;
    writeIdx++;
    writeIdx &= getSizeMask();
}

template<size_t kSize>
sample_t DelayLine<kSize>::read(int idx)
{
    return buffer[idx & getSizeMask()];
}

template<size_t kSize>
sample_t DelayLine<kSize>::readAndPrefetch(int idx)
{
    auto out = buffer[idx & getSizeMask()];
    __builtin_prefetch(&buffer[(idx + 1) & getSizeMask()]);

    return out;
}

template<size_t kSize>
sample_t DelayLine<kSize>::read(param_t delay)
{
    auto delayInt = static_cast<int>(delay);
    assert(delayInt + 1 < static_cast<int>(kSize));
    auto idx = writeIdx - delayInt;
    auto sample2 = read(idx - 1);
    auto sample1 = readAndPrefetch(idx);

    return interpolate(delay - static_cast<param_t>(delayInt), sample1, sample2);
}

template<size_t kSize>
void DelayLine<kSize>::prefetch(int delay)
{
    __builtin_prefetch(&buffer[(writeIdx - delay) & getSizeMask()]);
}

template<size_t kSize>
constexpr sample_t DelayLine<kSize>::interpolate(auto fraction, auto x1, auto x2)
{
    return fraction * x2 + (1 - fraction) * x1;
}

template<size_t kSize>
inline sample_t DelayLine<kSize>::readHermite(param_t delay)
{
    auto delayInt = static_cast<int>(delay);
    assert(delayInt + 1 < static_cast<int>(kSize));
    auto idx = writeIdx - delayInt;
    auto sample3 = read(idx - 2);
    auto sample2 = read(idx - 1);
    auto sample1 = read(idx);
    auto sample0 = readAndPrefetch(idx + 1);

    return interpHermite(
      delay - static_cast<param_t>(delayInt), sample0, sample1, sample2, sample3);
}

}