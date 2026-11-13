#pragma once

#include <boost/predef/architecture/arm.h>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <type_traits>

constexpr inline auto scaleLin(auto value, auto inLow, auto inHi, auto outLow, auto outHi)
{
    return outLow + (value - inLow) * (outHi - outLow) / (inHi - inLow);
}

constexpr inline auto
scaleLin(auto value, auto inLow, auto inMid, auto inHi, auto outLow, auto outMid, auto outHi)
{
    return value <= inMid ? scaleLin(value, inLow, inMid, outLow, outMid)
                          : scaleLin(value, inMid, inHi, outMid, outHi);
}

/**
 * @deprecated use scaleLin instead, this is misleading name (not interpolation function)
 */
inline auto interpLin(auto x, auto x1, auto x2, auto y1, auto y2)
{
    auto dydx = (y2 - y1) / (x2 - x1);
    return y1 + dydx * (x - x1);
}

/**
 * Interpolate value between v1 and v2, at position offset.
 */
inline auto interpHermite(auto offset, auto v0, auto v1, auto v2, auto v3)
{
    auto slope0 = (v2 - v0) * 0.5f;
    auto slope1 = (v3 - v1) * 0.5f;
    auto v = v1 - v2;
    auto w = slope0 + v;
    auto a = w + v + slope1;
    auto b_neg = w + a;
    auto stage1 = a * offset - b_neg;
    auto stage2 = stage1 * offset + slope0;
    return stage2 * offset + v1;
}

// maps range [0..1] to [-1..1]
inline auto bipolar(auto value)
{
    return 2.0f * value - 1.0f;
}

// approximatio of a sine function
inline auto parabolicSine(auto angle)
{
    constexpr decltype(angle) B = 4.0f / std::numbers::pi_v<decltype(angle)>;
    constexpr decltype(angle) C =
      -4.0f / (std::numbers::pi_v<decltype(angle)> * std::numbers::pi_v<decltype(angle)>);
    constexpr decltype(angle) P = 0.225f;

    auto y = B * angle + C * angle * std::abs(angle);
    return P * (y * std::abs(y) - y) + y;
}

// tanh aproximation
inline auto fast_tanh(auto x)
{
    auto x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

template<class T>
inline constexpr const T& clamp(const T& v, const T& lo, const T& hi)
{
    assert(!(hi < lo));
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

constexpr inline int iround(auto value)
{
    return static_cast<int>(std::lround(value));
}

template<typename T>
inline bool isInfOrNan(T x);

// float (IEEE-754 32-bit)
template<>
inline bool isInfOrNan<float>(float x)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) == 0x7F800000u;
}

#if BOOST_ARCH_ARM
// __fp16 (IEEE-754 16-bit)
template<>
inline bool isInfOrNan<__fp16>(__fp16 x)
{
    uint16_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7C00u) == 0x7C00u;
}
#endif

// fallback - static_assert if unsupported type
template<typename T>
inline bool isInfOrNan([[maybe_unused]] T x)
{
    static_assert(std::is_same_v<T, float>
#if BOOST_ARCH_ARM
                    || std::is_same_v<T, __fp16>
#endif
                  ,
                  "Unsupported type for isInfOrNan()");
    return false;
}

namespace test
{
inline void testIsInfOrNan()
{
    // test against float/fp16 nan and inf
    assert(isInfOrNan(2.0f / 0.0f));
    assert(isInfOrNan(0.0f / 0.0f));
    assert(isInfOrNan(1e38f * 1e38f));
    assert(isInfOrNan(-1e38f * 1e38f));
    assert(!isInfOrNan(0.0f));
    assert(!isInfOrNan(1.0f));
    assert(!isInfOrNan(-1.0f));
    assert(!isInfOrNan(0.5f));
    assert(!isInfOrNan(std::numeric_limits<float>::max()));
    assert(!isInfOrNan(std::numeric_limits<float>::lowest()));
#if BOOST_ARCH_ARM
    assert(isInfOrNan(static_cast<__fp16>(2.0f / 0.0f)));
    assert(isInfOrNan(static_cast<__fp16>(0.0f / 0.0f)));
    assert(isInfOrNan(static_cast<__fp16>(1e38f * 1e38f)));
    assert(isInfOrNan(static_cast<__fp16>(-1e38f * 1e38f)));
    assert(!isInfOrNan(static_cast<__fp16>(0.0f)));
    assert(!isInfOrNan(static_cast<__fp16>(1.0f)));
    assert(!isInfOrNan(static_cast<__fp16>(-1.0f)));
    assert(!isInfOrNan(static_cast<__fp16>(0.5f)));
    assert(!isInfOrNan(static_cast<__fp16>(65504.0f)));
    assert(!isInfOrNan(static_cast<__fp16>(-65504.0f)));
#endif
}
}