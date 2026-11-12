#pragma once

// NOLINTBEGIN(*)

#include "ReverbModel.h"

#include "../mutable/fx_engine.h"
#include "util.h"

#include "config.h"

namespace polydsp
{

class ReverbModelDattorro : public ReverbModel
{
  public:
    void init(std::span<sample_t> buffer) override
    {
        assert(buffer.size() >= E::GetSize());

        engine_.Init(buffer.data());
        engine_.SetLFOFrequency(LFO_1, 0.5f / static_cast<float>(kReverbSampleRate));
        engine_.SetLFOFrequency(LFO_2, 0.3f / static_cast<float>(kReverbSampleRate));

        engine_.Reset();
        lp_decay_0_ = 0;
        lp_decay_1_ = 0;
        lp_decay_2_ = 0;
    }

    static constexpr size_t getBufferSize() { return E::GetSize(); }

    void render(const AudioBuffer& in, std::span<AudioBuffer, 2> out) override
    {
        // This is the Griesinger topology described in the Dattorro paper
        // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
        // Modulation is applied in the loop of the first diffuser AP for additional
        // smearing; and to the two long delays for a slow shimmer/chorus effect.
        typedef E::Reserve<
          predelay_buffer_size_,
          E::Reserve<
            adjustSR(210),
            E::Reserve<
              adjustSR(158),
              E::Reserve<
                adjustSR(561),
                E::Reserve<
                  adjustSR(410),
                  E::Reserve<
                    adjustSR(3931),
                    E::Reserve<
                      adjustSR(2664),
                      E::Reserve<adjustSR(1343 + 12), // map1
                                 E::Reserve<adjustSR(995 + 12),
                                            E::Reserve<adjustSR(6241), // del1
                                                       E::Reserve<adjustSR(6590),
                                                                  E::Reserve<adjustSR(4641),
                                                                             E::Reserve<adjustSR(
                                                                               5505)>>>>>>>>>>>>>
          Memory;
        E::DelayLine<Memory, 0> del0;
        E::DelayLine<Memory, 1> ap1;
        E::DelayLine<Memory, 2> ap2;
        E::DelayLine<Memory, 3> ap3;
        E::DelayLine<Memory, 4> ap4;
        E::DelayLine<Memory, 5> ap5;
        E::DelayLine<Memory, 6> ap6;
        E::DelayLine<Memory, 7> map1;
        E::DelayLine<Memory, 8> map2;
        E::DelayLine<Memory, 9> del1;
        E::DelayLine<Memory, 10> del2;
        E::DelayLine<Memory, 11> del3;
        E::DelayLine<Memory, 12> del4;
        E::Context c;

        const float ap5Size = geometry * adjustSR(3931) - 1;
        const float ap6Size = geometry * adjustSR(2664) - 1;
        const float del1Size = spaceSize * adjustSR(6241) - 1;
        const float del2Size = spaceSize * adjustSR(6590) - 1;
        const float del3Size = spaceSize * adjustSR(4641) - 1;
        const float del4Size = spaceSize * adjustSR(5505) - 1;
        const float map1Size = geometry * adjustSR(1343) - 1;
        const float map2Size = geometry * adjustSR(995) - 1;

        const std::array outPointsLeft = { adjustSR(394) * spaceSize,  adjustSR(2974) * spaceSize,
                                           adjustSR(2831) * geometry,  adjustSR(2954) * spaceSize,
                                           adjustSR(2945) * spaceSize, adjustSR(277) * geometry,
                                           adjustSR(1578) * spaceSize };

        const std::array outPointsRight = { adjustSR(522) * spaceSize,  adjustSR(5368) * spaceSize,
                                            adjustSR(1817) * geometry,  adjustSR(3956) * spaceSize,
                                            adjustSR(3124) * spaceSize, adjustSR(496) * geometry,
                                            adjustSR(179) * spaceSize };

        const int predelay = static_cast<int>(this->predelay * (predelay_buffer_size_ - 1));

        const float klp = adjustSR_klp(tone);

        const float g1 = 0.9375f * getDiffusion();
        const float g2 = 0.78125f * getDiffusion();
        const float g3 = 0.875f * getDiffusion();
        const float g4 = 0.625f * getDiffusion();
        const float g5 = adjustSR_krt(getDecay());

        float lp_0 = lp_decay_0_;
        float lp_1 = lp_decay_1_;
        float lp_2 = lp_decay_2_;

        auto inIt = in.begin();
        auto outItLeft = out[0].begin();
        auto outItRight = out[1].begin();
        for (size_t i = 0; i < in.size(); i++)
        {
            float outFloat;
            float apout = 0.0f;
            engine_.Start(&c);

            c.Read(*inIt++);

            c.Write(del0, 0.0f);
            c.Read(del0, predelay, 1.0f);
            c.Lp(lp_0, klp);

            // Diffuse through 4 allpasses.
            c.Read(ap1 TAIL, g1);
            c.WriteAllPass(ap1, -g1);
            c.Read(ap2 TAIL, g1);
            c.WriteAllPass(ap2, -g1);
            c.Read(ap3 TAIL, g2);
            c.WriteAllPass(ap3, -g2);
            c.Read(ap4 TAIL, g2);
            c.WriteAllPass(ap4, -g2);
            c.Write(apout);

            // Main reverb loop.

            // left
            c.Load(apout);
            c.Read(del4, del4Size, g5);
            c.Interpolate(map1, map1Size, LFO_1, 12.0f, g3);
            c.WriteAllPass(map1, -g3);

            c.Write(del1, 1.0f);
            c.Load(0);
            c.Read(del1, del1Size, g5);

            c.Lp(lp_1, klp);

            c.Read(ap5, ap5Size, g4);
            c.WriteAllPass(ap5, -g4);

            c.Write(del2, 1.0f);

            c.Load(0);
            c.Read(del3, outPointsLeft[0], 1.0f);
            c.Read(del3, outPointsLeft[1], 1.0f);
            c.Read(ap5, outPointsLeft[2], -1.0f);
            c.Read(del4, outPointsLeft[3], 1.0f);
            c.Read(del1, outPointsLeft[4], -1.0f);
            c.Read(ap6, outPointsLeft[5], -1.0f);
            c.Read(del2, outPointsLeft[6], -1.0f);
            c.Write(outFloat);
            *outItLeft++ = outFloat;

            // right
            c.Load(apout);
            c.Read(del2, del2Size, g5);
            c.Interpolate(map2, map2Size, LFO_2, 12.0f, g3);
            c.WriteAllPass(map2, -g3);

            c.Write(del3, 1.0f);
            c.Load(0);
            c.Read(del3, del3Size, g5);

            c.Lp(lp_2, klp);

            c.Read(ap6, ap6Size, g4);
            c.WriteAllPass(ap6, -g4);

            c.Write(del4, 1.0f);

            c.Load(0);
            c.Read(del1, outPointsRight[0], 1.0f);
            c.Read(del1, outPointsRight[1], 1.0f);
            c.Read(ap6, outPointsRight[2], -1.0f);
            c.Read(del2, outPointsRight[3], 1.0f);
            c.Read(del3, outPointsRight[4], -1.0f);
            c.Read(ap5, outPointsRight[5], -1.0f);
            c.Read(del4, outPointsRight[6], -1.0f);
            c.Write(outFloat);
            *outItRight++ = outFloat;
        }

        lp_decay_0_ = lp_0;
        lp_decay_1_ = lp_1;
        lp_decay_2_ = lp_2;
    }

  private:
    typedef FxEngine<adjustBufferSize(65536u), FORMAT_FLOAT> E;
    E engine_;

    static constexpr size_t predelay_buffer_size_ = adjustSR(5000);

    float lp_decay_0_;
    float lp_decay_1_;
    float lp_decay_2_;
};

}

// NOLINTEND(*)
