#include "PolyDSP/fx/ModulationFx.h"

#include "misc/DelayLine.h"

namespace polydsp
{
ModulationFx::ModulationFx(std::span<sample_t> buffer)
  : delayLine1(std::make_unique<std::remove_reference<decltype(*delayLine1)>::type>(
      std::span<sample_t, kModFxDelayLineSize>(buffer)))
  , delayLine2(std::make_unique<std::remove_reference<decltype(*delayLine2)>::type>(
      std::span<sample_t, kModFxDelayLineSize>(
        buffer.subspan(kModFxDelayLineSize, kModFxDelayLineSize))))
{
    assert(buffer.data() != nullptr);
}

ModulationFx::~ModulationFx() = default;

size_t ModulationFx::getBufferSize()
{
    return kModFxDelayLineSize * 2;
}

template<SimpleLFO::Waveform waveform>
void ModulationFx::renderModulation(const RenderContext& /* ctx */)
{
    // NOLINTNEXTLINE(readability-qualified-auto)
    for (auto modBuf1It = modBuf1.begin(), modBuf2It = modBuf2.begin(); modBuf1It != modBuf1.end();
         modBuf1It++, modBuf2It++)
    {
        *modBuf1It = lfo1.render<waveform>();
        *modBuf2It = lfo2.render<waveform>();
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void ModulationFx::renderInternal(const RenderContext& ctx,
                                  const AudioBuffer& in,
                                  std::span<AudioBuffer, 2> out)
{
    bool switchPhase = this->delay < 0;
    bool filterOn = filterFreq < kMaxFilterFrequency;
    auto delay = std::abs(this->delay) * ctx.samplingRate;
    // ensure modulated delay does not get to close to 0 for interpolation to work
    auto modDepth = std::min(delay - 3.0f, delay * this->modDepth);

    assert(delay + 2 < getBufferSize()); // too small delay line

    auto out1It = out[0].begin();
    auto out2It = out[1].begin();
    auto mod1It = modBuf1.begin(); // NOLINT(readability-qualified-auto)
    auto mod2It = modBuf2.begin(); // NOLINT(readability-qualified-auto)
    auto delayMod1 = 0.0f;
    auto delayMod2 = 0.0f;
    auto feedback = this->feedback;
    switch (mode)
    {
        case Mode::kSingle:
            for (auto inIt = in.begin(); inIt != in.end();)
            {
                auto mod1 = *mod1It++ * modDepth;
                auto mod2 = *mod2It++;
                delayMod1 = delay + mod1 * (variation * mod2 + (1.0f - variation));
                auto delayOut = delayLine1->readHermite(delayMod1);

                if (filterOn)
                {
                    delayOut = filter.render(delayOut);
                }

                delayLine1->write(*inIt++ + delayOut * feedback);

                *out1It++ = *out2It++ = switchPhase ? -delayOut : delayOut;
            }
            break;
        case Mode::kDualTap:
            feedback *= 0.5f;
            for (auto inIt = in.begin(); inIt != in.end();)
            {
                delayMod1 = delay + *mod1It++ * modDepth;
                delayMod2 = delay + *mod2It++ * modDepth;
                auto delayOut =
                  delayLine1->readHermite(delayMod1) - delayLine1->readHermite(delayMod2);
                if (filterOn)
                {
                    delayOut = filter.render(delayOut);
                }

                delayLine1->write(*inIt++ + delayOut * feedback);

                *out1It++ = *out2It++ = switchPhase ? -delayOut : delayOut;
            }
            break;
        case Mode::kStereo:
            for (auto inIt = in.begin(); inIt != in.end();)
            {
                delayMod1 = delay + *mod1It++ * modDepth;
                delayMod2 = delay + *mod2It++ * modDepth;
                auto delayOut1 = delayLine1->readHermite(delayMod1);
                auto delayOut2 = delayLine2->readHermite(delayMod2);

                if (filterOn)
                {
                    std::tie(delayOut1, delayOut2) = filter.render({ delayOut1, delayOut2 });
                }

                auto input = *inIt++;
                delayLine1->write(input + delayOut1 * feedback);
                delayLine2->write(input + delayOut2 * feedback);

                *out1It++ = switchPhase ? -delayOut1 : delayOut1;
                *out2It++ = switchPhase ? -delayOut2 : delayOut2;
            }
            break;
        case Mode::kStereoCross:
            for (auto inIt = in.begin(); inIt != in.end();)
            {
                delayMod1 = delay + *mod1It++ * modDepth;
                delayMod2 = delay + *mod2It++ * modDepth;
                auto delayOut1 = delayLine1->readHermite(delayMod1);
                auto delayOut2 = delayLine2->readHermite(delayMod2);

                auto [filteredDelayOut1, filteredDelayOut2] =
                  filterOn ? filter.render({ -delayOut1, -delayOut2 })
                           : std::make_pair(-delayOut1, -delayOut2);

                auto input = *inIt++;
                delayLine1->write(input + delayOut1 * feedback);
                delayLine2->write(input + delayOut2 * feedback);

                *out1It++ = delayOut1 + variation * filteredDelayOut2;
                *out2It++ = delayOut2 + variation * filteredDelayOut1;
            }
            break;
    }

    lastDelay1 = static_cast<int>(delayMod1);
    lastDelay2 = static_cast<int>(delayMod2);
}

void ModulationFx::render(const RenderContext& ctx,
                          const AudioBuffer& in,
                          std::span<AudioBuffer, 2> out)
{
    if (std::abs(delay) < 0.0001f)
    {
        std::ranges::copy(in, out[0].begin());
        std::ranges::copy(in, out[1].begin());
        return;
    }

    if (filterDirty)
    {
        filter.setup(ctx, BiquadFilter::Shape::kBPF, 0, filterFreq, filterBandwidth);
        filterDirty = false;
    }

    auto modFreq = std::abs(this->modFreq);
    lfo1.setFrequency(ctx, modFreq);
    lfo2.setFrequency(ctx,
                      modFreq * ((mode == Mode::kStereoCross || mode == Mode::kSingle)
                                   ? 0.9f
                                   : scaleLin(variation, 0.0f, 1.0f, 0.5f, 2.0f)));

    // prefetch 2 words ahead of last delay, for interpolation needs
    // and to take modulation into account
    switch (mode)
    {
        case Mode::kSingle:
            delayLine1->prefetch(lastDelay1 - 2);
            break;
        case Mode::kDualTap:
            delayLine1->prefetch(lastDelay1 - 2);
            delayLine1->prefetch(lastDelay2 - 2);
            break;
        case Mode::kStereo:
        case Mode::kStereoCross:
            delayLine1->prefetch(lastDelay1 - 2);
            delayLine2->prefetch(lastDelay2 - 2);
            break;
    }

    if (this->modFreq >= 0)
    {
        renderModulation<SimpleLFO::Waveform::kSin>(ctx);
    }
    else
    {
        renderModulation<SimpleLFO::Waveform::kTriangle>(ctx);
    }
    renderInternal(ctx, in, out);
}
}