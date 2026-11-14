// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Adapted for Polyverb by Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)
//
// -----------------------------------------------------------------------------
//
// Reverb.

#pragma once

// NOLINTBEGIN(*)

#include "ReverbModel.h"

#include "../mutable/fx_engine.h"
#include "util.h"

#include "config.h"

namespace polydsp
{

class ReverbModelMutable : public ReverbModel
{
  public:
    void init(std::span<sample_t> buffer) override
    {
        assert(buffer.size() >= E::GetSize());

        engine_.Init(buffer.data());
        engine_.SetLFOFrequency(LFO_1, 0.5f / static_cast<float>(kReverbSampleRate));
        engine_.SetLFOFrequency(LFO_2, 1.3f / static_cast<float>(kReverbSampleRate));

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
          adjustSR(207),
          E::Reserve<
            adjustSR(295),
            E::Reserve<
              adjustSR(440),
              E::Reserve<
                adjustSR(726),
                E::Reserve<
                  adjustSR(3007),
                  E::Reserve<
                    adjustSR(3707),
                    E::Reserve<adjustSR(6203),
                               E::Reserve<predelay_buffer_size_,
                                          E::Reserve<adjustSR(3480),
                                                     E::Reserve<adjustSR(3028),
                                                                E::Reserve<adjustSR(8699)>>>>>>>>>>>
          Memory;
        E::DelayLine<Memory, 0> ap1;
        E::DelayLine<Memory, 1> ap2;
        E::DelayLine<Memory, 2> ap3;
        E::DelayLine<Memory, 3> ap4;
        E::DelayLine<Memory, 4> dap1a;
        E::DelayLine<Memory, 5> dap1b;
        E::DelayLine<Memory, 6> del1;
        E::DelayLine<Memory, 7> del0;
        E::DelayLine<Memory, 8> dap2a;
        E::DelayLine<Memory, 9> dap2b;
        E::DelayLine<Memory, 10> del2;
        E::Context c;

        const float kap = getDiffusion();
        const float klp = adjustSR_klp(tone);
        const float krt = adjustSR_krt(getDecay());
        const int predelay = static_cast<int>(this->predelay * (predelay_buffer_size_ - 1));

        float lp_0 = lp_decay_0_;
        float lp_1 = lp_decay_1_;
        float lp_2 = lp_decay_2_;

        const std::array delSizes = {
            adjustSR(8699 - 201) * spaceSize, adjustSR(3006) * spaceSize,
            adjustSR(3706) * spaceSize,       adjustSR(6203 - 201) * spaceSize,
            adjustSR(3479) * spaceSize,       adjustSR(3027) * spaceSize
        };
        auto lfo2Range = 200.0f * spaceSize * geometry;

        auto inIt = in.begin();
        auto outItLeft = out[0].begin();
        auto outItRight = out[1].begin();
        for (size_t i = 0; i < in.size(); i++)
        {
            float outFloat;
            float apout = 0.0f;
            engine_.Start(&c);

            // Smear AP1 inside the loop.
            c.Interpolate(ap1, 100.0f, LFO_1, 100.0f, 1.0f);
            c.Write(ap1, 100, 0.0f);

            c.Read(*inIt++);

            c.Write(del0, 0.0f);
            c.Read(del0, predelay, 1.0f);
            c.Lp(lp_0, klp);

            // Diffuse through 4 allpasses.
            c.Read(ap1 TAIL, kap);
            c.WriteAllPass(ap1, -kap);
            c.Read(ap2 TAIL, kap);
            c.WriteAllPass(ap2, -kap);
            c.Read(ap3 TAIL, kap);
            c.WriteAllPass(ap3, -kap);
            c.Read(ap4 TAIL, kap);
            c.WriteAllPass(ap4, -kap);
            c.Write(apout);

            // Main reverb loop.
            c.Load(apout);
            c.Interpolate(del2, delSizes[0], LFO_2, lfo2Range, krt);
            c.Lp(lp_1, klp);
            c.Read(dap1a, delSizes[1], -kap);
            c.WriteAllPass(dap1a, kap);
            c.Read(dap1b, delSizes[2], kap);
            c.WriteAllPass(dap1b, -kap);
            c.Write(del1, 2.0f);
            c.Write(outFloat, 0.0f);
            *outItLeft++ = outFloat;

            c.Load(apout);
            c.Interpolate(del1, delSizes[3], LFO_2, lfo2Range, krt);
            c.Lp(lp_2, klp);
            c.Read(dap2a, delSizes[4], kap);
            c.WriteAllPass(dap2a, -kap);
            c.Read(dap2b, delSizes[5], -kap);
            c.WriteAllPass(dap2b, kap);
            c.Write(del2, 2.0f);
            c.Write(outFloat, 0.0f);
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
