#pragma once

#include <array>
#include <limits>
#include <random>

namespace polydsp
{
class WhiteNoise
{
  public:
    float next();

  private:
    static constexpr float kScale = 2.0f / static_cast<float>(std::numeric_limits<uint32_t>::max());

    int x1 = 0x12345678;
    int x2 = static_cast<int>(0xdeadbeed);
};

class GaussianWhiteNoise
{
  public:
    float next(float mean = 0.0, float variance = 1.0);

  private:
    std::default_random_engine defaultGeneratorEngine;
};

class PinkNoise : WhiteNoise
{
  public:
    float next();

  private:
    static constexpr float kScale = 0.25f;

    std::array<float, 3> coeffs = { 0.0f, 0.0f, 0.0f };

    float filter(float white);
};

inline float WhiteNoise::next()
{
    x1 ^= x2; // NOLINT(hicpp-signed-bitwise)
    float output = static_cast<float>(x2) * kScale;
    x2 += x1;
    return output;
}

inline float GaussianWhiteNoise::next(float mean, float variance)
{
    std::normal_distribution<float> normalDistribution(mean, variance);
    return normalDistribution(defaultGeneratorEngine);
}

inline float PinkNoise::next()
{
    return filter(WhiteNoise::next()) * kScale;
}

inline float PinkNoise::filter(float white)
{
    coeffs[0] = 0.99765f * coeffs[0] + white * 0.0990460f;
    coeffs[1] = 0.96300f * coeffs[1] + white * 0.2965164f;
    coeffs[2] = 0.57000f * coeffs[2] + white * 1.0526913f;
    return coeffs[0] + coeffs[1] + coeffs[2] + white * 0.1848f;
}

}