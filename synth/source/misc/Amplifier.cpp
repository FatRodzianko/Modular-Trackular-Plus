#include "PolyDSP/misc/Amplifier.h"

#include "util.h"

#include "PolyUtil/math.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace polydsp
{

void Amplifier::render(const RenderContext& /*ctx*/,
                       AudioBuffer& buf,
                       ConstModulationBuffer gainModulationRaw) const
{
    auto modIt = gainModulationRaw.begin();

    for (auto& sample : buf)
    {
        auto modValue = *modIt++;

        if (modValue <= 0)
        {
            sample = 0;
        }
        else
        {
            sample *= modValue * volumeMult;
        }
    }
}

void Amplifier::renderAdd(AudioBuffer& inputbuf,
                          AudioBuffer& outputbuf,
                          ConstModulationBuffer gainModulationRaw) const
{
    auto modIt = gainModulationRaw.begin();

    auto inIt = inputbuf.begin();
    for (auto& outSample : outputbuf)
    {
        auto modValue = *modIt++;

        if (modValue > 0)
        {
            outSample += *(inIt++) * modValue * volumeMult;
        }
    }
}

void Amplifier::saturateHardClip(AudioBuffer& buf, float gainDb)
{
    float gainRaw = dbToAmplitude(gainDb);
    for (auto& element : buf)
    {
        element = std::clamp(element * gainRaw, -1.0f, 1.0f);
    };
}

void Amplifier::saturateSoftClip(AudioBuffer& buf, float gainDb)
{
    float gainRaw = dbToAmplitude(gainDb);
    for (auto& element : buf)
    {
        auto sample = element * gainRaw;
        sample = std::clamp(sample, -1.5f, 1.5f);
        // out = in - in^3/6.72
        // 6.72 set experimentally
        // https://www.desmos.com/calculator/rjynbskttb
        sample = sample - sample * sample * sample / 6.72f;
        element = std::clamp(sample, -1.0f, 1.0f);
    };
}

void Amplifier::saturateLight(AudioBuffer& buf, float gainDb)
{
    float gainRaw = dbToAmplitude(gainDb);
    float makeupRaw = scaleLin(gainRaw, 0.0f, 8.0f, 1.0f, 0.5f); // est. experimentally
    for (auto& element : buf)
    {
        auto sample = element * gainRaw;

        if (sample >= 1.0f)
        {
            sample = 1.0f;
        }
        else if (sample <= -1.0f)
        {
            sample = -1.0f;
        }
        else
        {
            // out = 3 * in / 2 - in^3 / 2
            // https://www.desmos.com/calculator/svadljg4am
            sample = 1.5f * sample - 0.5f * sample * sample * sample;
        }

        element = sample * makeupRaw;
    };
}

void Amplifier::saturateOverdrive(AudioBuffer& buf, float gainDb)
{
    float gainRaw = dbToAmplitude(gainDb);
    float makeupRaw = scaleLin(gainRaw, 0.0f, 8.0f, 0.5f, 0.3f); // est. experimentally
    for (auto& element : buf)
    {
        auto sample = element * gainRaw;

        if (sample > 0.5f)
        {
            sample = 1.0f;
        }
        else if (sample < -0.5f)
        {
            sample = -1.0f;
        }
        else if (sample > 0.25f)
        {
            // y = (3 - (2 - 3x)^2) / 3
            auto temp = 2.0f - 3.0f * sample;
            sample = (3.0f - temp * temp) * (1.0 / 3.0f);
        }
        else if (sample < -0.25f)
        {
            auto temp = 2.0f + 3.0f * sample;
            sample = (3.0f - temp * temp) * (-1.0 / 3.0f);
        }
        else
        {
            sample *= 2.0f;
        }

        element = sample * makeupRaw;
    };
}

void Amplifier::saturateOverdriveNoMakeup(AudioBuffer& buf, float gainDb)
{
    float gainRaw = dbToAmplitude(gainDb);
    for (auto& element : buf)
    {
        auto sample = element * gainRaw;

        if (sample > 0.5f)
        {
            sample = 1.0f;
        }
        else if (sample < -0.5f)
        {
            sample = -1.0f;
        }
        else if (sample > 0.25f)
        {
            // y = (3 - (2 - 3x)^2) / 3
            auto temp = 2.0f - 3.0f * sample;
            sample = (3.0f - temp * temp) * (1.0 / 3.0f);
        }
        else if (sample < -0.25f)
        {
            auto temp = 2.0f + 3.0f * sample;
            sample = (3.0f - temp * temp) * (-1.0 / 3.0f);
        }
        else
        {
            sample *= 2.0f;
        }

        element = sample;
    };
}

void Amplifier::saturateOverdrive2(AudioBuffer& buf, float gainDb)
{
    float gainRaw = dbToAmplitude(gainDb);
    float makeupRaw = scaleLin(gainRaw, 0.0f, 8.0f, 0.5f, 0.3f); // est. experimentally
    for (auto& element : buf)
    {
        auto sample = element * gainRaw;

        if (sample > 0.5f)
        {
            sample = 1.0f;
        }
        else if (sample < -0.5f)
        {
            sample = -1.0f;
        }
        else if (sample > 0.25f)
        {
            // y = (4 - (2 - 4x)^3) / 4
            // https://www.desmos.com/calculator/svadljg4am
            auto temp = 2.0f - 4.0f * sample;
            sample = (4.0f - temp * temp * temp) / 4.0f;
        }
        else if (sample < -0.25f)
        {
            auto temp = 2.0f + 4.0f * sample;
            sample = -(4.0f - temp * temp * temp) / 4.0f;
        }
        else
        {
            sample *= 3.0f;
        }

        element = sample * makeupRaw;
    };
}

void Amplifier::saturateDiode(AudioBuffer& buf, float multiplier)
{
    const auto makeupRaw = 1.0f;
    const auto coeff = makeupRaw / fast_tanh(multiplier);

    for (auto& element : buf)
    {
        element = fast_tanh(element * multiplier) * coeff;
    };
}

void Amplifier::saturateFuzz(AudioBuffer& buf, float multiplier)
{
    float normalizationCoeff = (1.0f - expf(-multiplier));
    for (auto& element : buf)
    {
        element = (element >= 0 ? 1.0f : -1.0f) *
                  ((1.0f - expf(-fabsf(multiplier * element))) / normalizationCoeff);
    };
}

void Amplifier::saturateAsymmetric(AudioBuffer& buf, float multiplier)
{
    const auto makeupRaw = 1.0f;
    const auto asymmmetry = 0.2f;
    const auto multiplier2 = multiplier * asymmmetry;
    const auto coeff = makeupRaw / fast_tanh(multiplier);
    const auto coeff2 = makeupRaw / fast_tanh(multiplier * asymmmetry);
    for (auto& element : buf)
    {
        element = element >= 0 ? fast_tanh(element * multiplier) * coeff
                               : fast_tanh(element * multiplier2) * coeff2;
    };
}

void Amplifier::saturateSin(AudioBuffer& buf, float multiplier)
{
    const auto makeupRaw = 0.5f;
    const auto z = std::numbers::pi_v<param_t> * (multiplier + 1) * 0.5f;

    for (auto& element : buf)
    {
        // parabolicSine does not work well, adds lots of noise
        element = std::sin(z * element) * makeupRaw;
    };
}

void Amplifier::saturateSin2(AudioBuffer& buf, float multiplier)
{
    // https://www.desmos.com/calculator/czk41vdvri

    const auto makeupRaw = 0.5f;
    const auto z = std::numbers::pi_v<param_t> * (multiplier + 1) * 0.5f;
    const auto b = std::sin(z);

    for (auto& element : buf)
    {
        element = ((1.0f - element) * std::sin(z * element) + element * b) * makeupRaw;
    };
}

/**
 * Note: introduces DC offset, use DCBlocker after this.
 */
void Amplifier::rectifier(AudioBuffer& buf, float amount)
{
    auto makeupRaw = scaleLin(amount, 0.0f, 1.0f, 8.0f, 16.0f);
    auto amountNeg = 1.0f - amount;
    for (auto& element : buf)
    {
        element = makeupRaw * (amount * std::abs(element) + amountNeg * element);
    };
}

void Amplifier::bitcrush(AudioBuffer& buf, float bits)
{
    auto values = std::pow(2.0f, bits);
    auto valuesInv = 1.0f / values;

    for (auto& element : buf)
    {
        element = std::round(element * values) * valuesInv;
    }
}

void Amplifier::bitcrush2(AudioBuffer& buf, float bits)
{
    auto values = std::pow(2.0f, bits);
    auto valuesInv = 1.0f / values;

    for (auto& element : buf)
    {
        element = std::trunc(element * values) * valuesInv;
    }
}

void Amplifier::sampleRateRedux(AudioBuffer& buf, int factor)
{
    assert(factor < static_cast<int>(sizeof(int) * 8));
    factor = 1 << factor;
    assert(factor <= std::ssize(buf));

    for (auto it = buf.begin(); it != buf.end();)
    {
        auto sample = *it;
        for (int i = 0; i < factor; i++)
        {
            *it++ = sample;
        }
        assert(it <= buf.end());
    }
}

void Amplifier::sampleRateRedux2(AudioBuffer& buf, int factor)
{
    assert(factor < static_cast<int>(sizeof(int) * 8));
    factor = 1 << factor;
    assert(factor <= std::ssize(buf));

    auto div = 1.0f / static_cast<float>(factor);
    for (auto it = buf.begin(); it != buf.end();)
    {
        auto sample = *it;
        auto nextSample = *(it + factor - 1);

        // do not interpolate, move in the opposite direction
        auto delta = (sample - nextSample) * div;
        for (int i = 0; i < factor - 1; i++)
        {
            *it = sample;
            it++;
            sample += delta;
        }
        *it = (*it + sample) * 0.5f; // smoothen last sample
        it++;
        assert(it <= buf.end());
    }
}

}
