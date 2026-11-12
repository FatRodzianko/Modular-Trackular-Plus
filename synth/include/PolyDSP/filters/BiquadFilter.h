#pragma once

#include "PolyDSP/Core.h"

#include <numbers>

// NOLINTBEGIN(readability-isolate-declaration)

namespace polydsp
{
class BiquadFilter
{
  public:
    enum class Shape
    {
        kLPF,
        kHPF,
        kBPF,  // 0db gain, variable bandwidth
        kBPF2, // constant bandwidth, variable gain
        kNotch,
        kPeakingBand,
        kLowShelf,
        kHighShelf
    };

    static constexpr float kFlatQ = 0.707f; // 1/√2, Q value for flat response (no resonance peak)

    /**
     * @param ctx
     * @param shape
     * @param dbGain used only for peaking and shelving filters
     * @param frequency
     * @param param for LP/HP/BP2 filter Q,
     *              BP/notch/peaking - bandwidth in octaves
     *              shelving - shelf slope (0.0-1.0]
     * @see https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
     */
    void setup(const RenderContext& ctx,
               Shape shape,
               param_t dbGain,
               time_t frequency,
               param_t param)
    {
        param_t a0 = 0, a1 = 0, a2 = 0, b0 = 0, b1 = 0, b2 = 0;

        param_t A = std::pow(10.0f, dbGain / 40.0f);
        param_t omega = 2.0f * std::numbers::pi_v<param_t> * frequency / ctx.samplingRate;
        param_t sn = std::sin(omega);
        param_t cs = std::cos(omega);
        param_t alpha = 0.0f;

        switch (shape)
        {
            case Shape::kLPF:
            case Shape::kHPF:
            case Shape::kBPF2:
                alpha = sn / (2 * std::max(param, 0.001f));
                break;
            case Shape::kBPF:
            case Shape::kNotch:
            case Shape::kPeakingBand:
                alpha = sn * std::sinh(std::numbers::ln2_v<param_t> / 2 * param * omega / sn);
                break;
            case Shape::kLowShelf:
            case Shape::kHighShelf:
                alpha = sn / 2 * std::sqrt((A + 1 / A) * (1 / param - 1) + 2);
                break;
        };

        param_t beta = std::sqrt(A + A);

        switch (shape)
        {
            case Shape::kLPF:
                b0 = (1 - cs) / 2;
                b1 = 1 - cs;
                b2 = (1 - cs) / 2;
                a0 = 1 + alpha;
                a1 = -2 * cs;
                a2 = 1 - alpha;
                break;
            case Shape::kHPF:
                b0 = (1 + cs) / 2;
                b1 = -(1 + cs);
                b2 = (1 + cs) / 2;
                a0 = 1 + alpha;
                a1 = -2 * cs;
                a2 = 1 - alpha;
                break;
            case Shape::kBPF:
                b0 = alpha;
                b1 = 0;
                b2 = -alpha;
                a0 = 1 + alpha;
                a1 = -2 * cs;
                a2 = 1 - alpha;
                break;
            case Shape::kBPF2:
                b0 = sn * 0.5f;
                b1 = 0;
                b2 = -sn * 0.5f;
                a0 = 1 + alpha;
                a1 = -2 * cs;
                a2 = 1 - alpha;
                break;
            case Shape::kNotch:
                b0 = 1;
                b1 = -2 * cs;
                b2 = 1;
                a0 = 1 + alpha;
                a1 = -2 * cs;
                a2 = 1 - alpha;
                break;
            case Shape::kPeakingBand:
                b0 = 1 + (alpha * A);
                b1 = -2 * cs;
                b2 = 1 - (alpha * A);
                a0 = 1 + (alpha / A);
                a1 = -2 * cs;
                a2 = 1 - (alpha / A);
                break;
            case Shape::kLowShelf:
                b0 = A * ((A + 1) - (A - 1) * cs + beta * sn);
                b1 = 2 * A * ((A - 1) - (A + 1) * cs);
                b2 = A * ((A + 1) - (A - 1) * cs - beta * sn);
                a0 = (A + 1) + (A - 1) * cs + beta * sn;
                a1 = -2 * ((A - 1) + (A + 1) * cs);
                a2 = (A + 1) + (A - 1) * cs - beta * sn;
                break;
            case Shape::kHighShelf:
                b0 = A * ((A + 1) + (A - 1) * cs + beta * sn);
                b1 = -2 * A * ((A - 1) + (A + 1) * cs);
                b2 = A * ((A + 1) + (A - 1) * cs - beta * sn);
                a0 = (A + 1) - (A - 1) * cs + beta * sn;
                a1 = 2 * ((A - 1) - (A + 1) * cs);
                a2 = (A + 1) - (A - 1) * cs - beta * sn;
                break;
        }

        this->a0 = b0 / a0;
        this->a1 = b1 / a0;
        this->a2 = b2 / a0;
        this->a3 = a1 / a0;
        this->a4 = a2 / a0;
    };

    sample_t render(sample_t in)
    {
        auto out = a0 * in + a1 * x1[0] + a2 * x2[0] - a3 * y1[0] - a4 * y2[0];

        x2[0] = x1[0];
        x1[0] = in;
        y2[0] = y1[0];
        y1[0] = out;

        return out;
    }

    std::pair<sample_t, sample_t> render(std::pair<sample_t, sample_t> in)
    {
        auto out1 = a0 * in.first + a1 * x1[0] + a2 * x2[0] - a3 * y1[0] - a4 * y2[0];
        x2[0] = x1[0];
        x1[0] = in.first;
        y2[0] = y1[0];
        y1[0] = out1;

        auto out2 = a0 * in.second + a1 * x1[1] + a2 * x2[1] - a3 * y1[1] - a4 * y2[1];
        x2[1] = x1[1];
        x1[1] = in.second;
        y2[1] = y1[1];
        y1[1] = out2;

        return { out1, out2 };
    }

    void render(std::span<sample_t> buf)
    {
        for (auto& sample : buf)
        {
            sample = render(sample);
        };
    }

    void render(const AudioBuffer inBuf, AudioBuffer outBuf)
    {
        auto outIt = outBuf.begin();
        for (auto in : inBuf)
        {
            *outIt++ = render(in);
        }
    }

    void render(std::span<const AudioBuffer, 2>& inBuf, std::span<AudioBuffer, 2>& outBuf)
    {
        auto outItL = outBuf[0].begin();
        auto outItR = outBuf[1].begin();
        auto inItL = inBuf[0].begin();
        auto inItR = inBuf[1].begin();

        while (inItL != inBuf[0].end())
        {
            auto out = render({ *inItL++, *inItR++ });
            *outItL++ = out.first;
            *outItR++ = out.second;
        }
    }

    void renderAdd(const AudioBuffer inBuf, AudioBuffer outBuf)
    {
        auto outIt = outBuf.begin();
        for (auto in : inBuf)
        {
            *outIt++ += render(in);
        }
    }

    void renderAdd(std::span<const AudioBuffer, 2>& inBuf, std::span<AudioBuffer, 2>& outBuf)
    {
        auto outItL = outBuf[0].begin();
        auto outItR = outBuf[1].begin();
        auto inItL = inBuf[0].begin();
        auto inItR = inBuf[1].begin();

        while (inItL != inBuf[0].end())
        {
            auto out = render({ *inItL++, *inItR++ });
            *outItL++ += out.first;
            *outItR++ += out.second;
        }
    }

  private:
    param_t a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0;
    param_t x1[2] = { 0 }, x2[2] = { 0 }, y1[2] = { 0 }, y2[2] = { 0 }; // NOLINT(*-avoid-c-arrays)
};

}

// NOLINTEND(readability-isolate-declaration)
