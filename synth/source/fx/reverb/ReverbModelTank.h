#pragma once

// NOLINTBEGIN(*)

#include "ReverbModel.h"

#include "../mutable/fx_engine.h"
#include "util.h"

#include "config.h"

namespace polydsp
{

// Hits performance a bit but doesn't really improve sound that much
// #ifdef FEATURE_DENSE_OUT

/**
 * Reverb model topology based on ReverbTank in Will's Pirkle book
 * "Designing audio effect plugins in C++".
 * Added 4xAPF diffusion on input.
 * Parallel APFs instead of nested - seem to be ringing less
 * (note: nested APFs give more exponential decay).
 * Delay node outputs are mixed to inputs in FDN-like way.
 */
class ReverbModelTank : public ReverbModel
{
  public:
    void init(std::span<sample_t> buffer) override
    {
        assert(buffer.size() >= E::GetSize());

        engine_.Init(buffer.data());
        engine_.SetLFOFrequency(LFO_1, 0.15f / static_cast<float>(kReverbSampleRate));
        engine_.SetLFOFrequency(LFO_2, 0.33f / static_cast<float>(kReverbSampleRate));
        engine_.SetLFOFrequency(LFO_3, 0.57f / static_cast<float>(kReverbSampleRate));
        engine_.SetLFOFrequency(LFO_4, 0.73f / static_cast<float>(kReverbSampleRate));

        engine_.Reset();
        lpState = {};
    }

    static constexpr size_t getBufferSize() { return E::GetSize(); }

    void render(const AudioBuffer& in, std::span<AudioBuffer, 2> out) override
    {
        typedef E::Reserve<
          kMaxPredelaySize,
          E::Reserve<
            kMaxAPFSize,
            E::Reserve<
              kMaxAPFSize,
              E::Reserve<
                kMaxAPFSize,
                E::Reserve<
                  kMaxAPFSize,
                  E::Reserve<
                    kMaxAPFSize,
                    E::Reserve<
                      kMaxAPFSize,
                      E::Reserve<
                        kMaxAPFSize,
                        E::Reserve<
                          kMaxAPFSize,
                          E::Reserve<
                            kMaxDelaySize,
                            E::Reserve<
                              kMaxDelaySize,
                              E::Reserve<
                                kMaxDelaySize,
                                E::Reserve<kMaxDelaySize,
                                           E::Reserve<adjustSR(210),
                                                      E::Reserve<adjustSR(158),
                                                                 E::Reserve<adjustSR(561),
                                                                            E::Reserve<adjustSR(
                                                                              410)>>>>>>>>>>>>>>>>>
          Memory;
        E::DelayLine<Memory, 0> del0;
        E::DelayLine<Memory, 1> ap1;
        E::DelayLine<Memory, 2> ap1b;
        E::DelayLine<Memory, 3> ap2;
        E::DelayLine<Memory, 4> ap2b;
        E::DelayLine<Memory, 5> ap3;
        E::DelayLine<Memory, 6> ap3b;
        E::DelayLine<Memory, 7> ap4;
        E::DelayLine<Memory, 8> ap4b;
        E::DelayLine<Memory, 9> del1;
        E::DelayLine<Memory, 10> del2;
        E::DelayLine<Memory, 11> del3;
        E::DelayLine<Memory, 12> del4;
        E::DelayLine<Memory, 13> preAp1;
        E::DelayLine<Memory, 14> preAp2;
        E::DelayLine<Memory, 15> preAp3;
        E::DelayLine<Memory, 16> preAp4;
        E::Context c;

        const int predelay = static_cast<int>(this->predelay * (kMaxPredelaySize - 1));

        const float klp = adjustSR_klp(tone);
        const float krt = adjustSR_krt(getDecay() * 0.85f);
        const float apg = getDiffusion();
        const float kLFORange = 12.0f;
        const float g1 = 0.9375f * getDiffusion();
        const float g2 = 0.78125f * getDiffusion();

        auto geometry = this->geometry * kMaxAPFSize;
        auto spaceSize = this->spaceSize * kMaxDelaySize;
        std::array apfSizes = { 0.317f * geometry - kLFORange, 0.873f * geometry,
                                0.477f * geometry - kLFORange, 0.291f * geometry,
                                0.993f * geometry - kLFORange, 0.757f * geometry,
                                0.179f * geometry - kLFORange, 0.575f * geometry };
        std::array delaySizes = {
            1.0f * spaceSize - 1, 0.873f * spaceSize, 0.707f * spaceSize, 0.667f * spaceSize
        };
        std::array preApSizes = { adjustSR(209) * this->geometry,
                                  adjustSR(157) * this->geometry,
                                  adjustSR(560) * this->geometry,
                                  adjustSR(409) * this->geometry };

        std::array outPointsLeft = {
            delaySizes[0] * 0.23f, delaySizes[1] * 0.41f,
            delaySizes[2] * 0.59f, delaySizes[3] * 0.73f,
#ifdef FEATURE_DENSE_OUT
            delaySizes[0] * 0.31f, delaySizes[1] * 0.47f,
            delaySizes[2] * 0.67f, delaySizes[3] * 0.84f,
#endif
        };
        std::array outPointsRight = {
            delaySizes[0] * 0.29f, delaySizes[1] * 0.43f,
            delaySizes[2] * 0.61f, delaySizes[3] * 0.79f,
#ifdef FEATURE_DENSE_OUT
            delaySizes[0] * 0.37f, delaySizes[1] * 0.53f,
            delaySizes[2] * 0.71f, delaySizes[3] * 0.89f,
#endif
        };

        std::array<float, kNodes> lpState = this->lpState;

        const float krtD3 =
          0.1f * static_cast<float>(kBaselineSampleRate) / static_cast<float>(kReverbSampleRate);

        auto inIt = in.begin();
        auto outItLeft = out[0].begin();
        auto outItRight = out[1].begin();
        for (size_t i = 0; i < in.size(); i++)
        {
            float outFloat;
            float preOut = 0.0f;
            engine_.Start(&c, 4);

            c.Read(*inIt++);

            c.Write(del0, 0.0f);
            c.Read(del0, predelay, 1.0f);
            c.Lp(preLpState, klp);

            // Diffuse through 4 allpasses.
            c.Read(preAp1, preApSizes[0], g1);
            c.WriteAllPass(preAp1, -g1);
            c.Read(preAp2, preApSizes[1], g1);
            c.WriteAllPass(preAp2, -g1);
            c.Read(preAp3, preApSizes[2], g2);
            c.WriteAllPass(preAp3, -g2);
            c.Read(preAp4, preApSizes[3], g2);
            c.WriteAllPass(preAp4, -g2);
            c.Write(preOut);

            // node 1
            c.Load(0);
            c.Read(del4, delaySizes[3], krt);
            c.Write(lastOut4);
            c.Read(lastOut2, krtD3);
            c.Read(lastOut3, -krtD3);
            c.Read(preOut);
            c.Interpolate(ap1, apfSizes[0], LFO_1, kLFORange, apg);
            c.WriteAllPass(ap1, -apg);
            c.Read(ap1b, apfSizes[1], apg);
            c.WriteAllPass(ap1b, -apg);
            c.Lp(lpState[0], klp);
            c.Write(del1, 1.0f);
            c.Load(0);
            c.Read(del1, delaySizes[0], krt);
            c.Write(lastOut1);

            // node 2
            c.Read(preOut);
            c.Read(lastOut3, krtD3);
            c.Read(lastOut4, -krtD3);
            c.Interpolate(ap2, apfSizes[2], LFO_2, kLFORange, apg);
            c.WriteAllPass(ap2, -apg);
            c.Read(ap2b, apfSizes[3], apg);
            c.WriteAllPass(ap2b, -apg);
            c.Lp(lpState[1], klp);
            c.Write(del2, 1.0f);
            c.Load(0);
            c.Read(del2, delaySizes[1], krt);
            c.Write(lastOut2);

            // node 3
            c.Read(preOut);
            c.Read(lastOut1, -krtD3);
            c.Read(lastOut4, krtD3);
            c.Interpolate(ap3, apfSizes[4], LFO_3, kLFORange, apg);
            c.WriteAllPass(ap3, -apg);
            c.Read(ap3b, apfSizes[5], apg);
            c.WriteAllPass(ap3b, -apg);
            c.Lp(lpState[2], klp);
            c.Write(del3, 1.0f);
            c.Load(0);
            c.Read(del3, delaySizes[2], krt);
            c.Write(lastOut3);

            // node 4
            c.Read(preOut);
            c.Read(lastOut1, krtD3);
            c.Read(lastOut2, -krtD3);
            c.Interpolate(ap4, apfSizes[6], LFO_4, kLFORange, apg);
            c.WriteAllPass(ap4, -apg);
            c.Read(ap4b, apfSizes[7], apg);
            c.WriteAllPass(ap4b, -apg);
            c.Lp(lpState[3], klp);
            c.Write(del4, 1.0f);

            // left out
            c.Load(0);
            c.Interpolate(del1, outPointsLeft[0], LFO_4, kLFORange, 1.0f);
            c.Interpolate(del2, outPointsLeft[1], LFO_3, kLFORange, -1.0f);
            c.Interpolate(del3, outPointsLeft[2], LFO_2, kLFORange, 1.0f);
            c.Interpolate(del4, outPointsLeft[3], LFO_1, kLFORange, -1.0f);
#ifdef FEATURE_DENSE_OUT
            c.Interpolate(del1, outPointsLeft[4], LFO_4, -kLFORange, 1.0f);
            c.Interpolate(del2, outPointsLeft[5], LFO_3, -kLFORange, -1.0f);
            c.Interpolate(del3, outPointsLeft[6], LFO_2, -kLFORange, 1.0f);
            c.Interpolate(del4, outPointsLeft[7], LFO_1, -kLFORange, -1.0f);
#endif
            c.Write(outFloat);
            *outItLeft++ = outFloat;

            // right out
            c.Load(0);
            c.Interpolate(del1, outPointsRight[0], LFO_4, kLFORange, 1.0f);
            c.Interpolate(del2, outPointsRight[1], LFO_3, kLFORange, -1.0f);
            c.Interpolate(del3, outPointsRight[2], LFO_2, kLFORange, 1.0f);
            c.Interpolate(del4, outPointsRight[3], LFO_1, kLFORange, -1.0f);
#ifdef FEATURE_DENSE_OUT
            c.Interpolate(del1, outPointsRight[4], LFO_4, -kLFORange, 1.0f);
            c.Interpolate(del2, outPointsRight[5], LFO_3, -kLFORange, -1.0f);
            c.Interpolate(del3, outPointsRight[6], LFO_2, -kLFORange, 1.0f);
            c.Interpolate(del4, outPointsRight[7], LFO_1, -kLFORange, -1.0f);
#endif
            c.Write(outFloat);
            *outItRight++ = outFloat;
        }

        this->lpState = lpState;
    }

  private:
    typedef FxEngine<adjustBufferSize(65536u), FORMAT_FLOAT> E;
    E engine_;

    static constexpr size_t kNodes = 4;

    static constexpr size_t kMaxAPFSize = adjustSR(3000u);
    static constexpr size_t kMaxDelaySize = adjustSR(6000u);
    static constexpr size_t kMaxPredelaySize = adjustSR(5000u);

    float preLpState = {};
    std::array<float, kNodes> lpState = {};
    float lastOut1 = 0, lastOut2 = 0, lastOut3 = 0, lastOut4 = 0;
};
}

// NOLINTEND(*)
