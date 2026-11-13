#pragma once

#include "PolyDSP/Core.h"
#include "PolyDSP/filters/Basic.h"
#include "PolyUtil/math.h"

namespace polydsp
{
class ReverbModel
{
  public:
    virtual ~ReverbModel() = default;

    virtual void init(std::span<sample_t> buffer) = 0;
    virtual void render(const AudioBuffer& in, std::span<AudioBuffer, 2> out) = 0;

    void setDecay(float decay) { this->decay = decay; };
    void setDiffusion(float diffusion) { this->diffusion = diffusion; };
    void setTone(float tone) { this->tone = tone; };
    void setPredelay(float predelay) { this->predelay = predelay; };
    void setGeometry(float size) { this->geometry = size; };
    void setSpaceSize(float size) { this->spaceSize = size; };

  protected:
    // it's convenient and harmless
    // NOLINTBEGIN(*-non-private-member-variables-in-classes)
    float decay = 0.5f;
    float diffusion = 0.8f;
    float tone = 0.5f;
    float predelay = 0.0f;
    float geometry = 1.0f;
    float spaceSize = 1.0f;

    static constexpr float kSmootherCoeff = 0.95f;
    AveragingFilter<float> decaySmoother = {};
    AveragingFilter<float> diffusionSmoother = {};

    static constexpr int kBaselineSampleRate = 44100;
    static constexpr auto adjustSR(auto value)
    {
        return static_cast<decltype(value)>(static_cast<uint64_t>(value) * kReverbSampleRate /
                                            kBaselineSampleRate);
    }
    static auto adjustSR_klp(auto value)
    {
        return value * static_cast<float>(kBaselineSampleRate) /
               static_cast<float>(kReverbSampleRate);
    }
    static auto adjustSR_krt(auto value)
    {
        // established experimentally
        return value * scaleLin(static_cast<float>(kReverbSampleRate),
                                static_cast<float>(kBaselineSampleRate),
                                96000.0f,
                                1.0f,
                                0.9f);
    }
    static constexpr size_t adjustBufferSize(size_t baseline)
    {
        assert(baseline && !(baseline & (baseline - 1))); // needs to be power of 2
        if (kReverbSampleRate == 44100)
        {
            return baseline;
        }
        if (kReverbSampleRate == 88200)
        {
            return baseline * 2;
        }
        if (kReverbSampleRate == 96000)
        {
            return baseline * 4;
        }

        assert(false);
        return 0;
    }

    float getDecay()
    {
        if constexpr (kReverbParamSmoothing)
        {
            return decaySmoother(decay, kSmootherCoeff);
        }
        return decay;
    };

    float getDiffusion()
    {
        if constexpr (kReverbParamSmoothing)
        {
            return diffusionSmoother(diffusion, kSmootherCoeff);
        }
        return diffusion;
    };

    // NOLINTEND(*-non-private-member-variables-in-classes)
};
}