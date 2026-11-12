#include "PolyDSP/fx/Delay.h"

#include "misc/DelayLine.h"

namespace polydsp
{

// value established experimentally
static constexpr auto kSmoothFilterFactor = 0.9999f;

Delay::Delay(std::span<sample_t> buffer)
  : delayLine1(std::make_unique<std::remove_reference<decltype(*delayLine1)>::type>(
      std::span<sample_t, kDelayLineSize>(buffer)))
  , delayLine2(std::make_unique<std::remove_reference<decltype(*delayLine2)>::type>(
      std::span<sample_t, kDelayLineSize>(buffer.subspan(kDelayLineSize, kDelayLineSize))))
{
    assert(buffer.data() != nullptr);
}

Delay::~Delay() = default;

size_t Delay::getBufferSize()
{
    return kDelayLineSize * kDelayLines;
}

template<SimpleLFO::Waveform waveform>
void Delay::renderModulation(const RenderContext& ctx)
{
    auto delay1 = std::abs(this->delay[0]) * ctx.samplingRate;

    auto delay2 = std::abs(this->delay[1]) * ctx.samplingRate;
    assert(static_cast<size_t>(delay1) + 2 < getBufferSize() / kDelayLines); // too small delay line
    assert(static_cast<size_t>(delay2) + 2 < getBufferSize() / kDelayLines); // too small delay line

    if ((mode == Mode::kDualTap || mode == Mode::kTripleTap) && delay2 > delay1)
    {
        std::swap(delay1, delay2);
    }

    // ensure modulated delay does not get to close to 0 for interpolation to work
    auto modDepth1 = std::min(delay1 - 3.0f, delay1 * this->modDepth * 0.1f);
    auto modDepth2 = std::min(delay2 - 3.0f, delay2 * this->modDepth * 0.1f);

    // NOLINTNEXTLINE(readability-qualified-auto)
    for (auto modBuf1It = modBuf1.begin(), modBuf2It = modBuf2.begin(); modBuf1It != modBuf1.end();
         modBuf1It++, modBuf2It++)
    {
        auto lfoValue = lfo.render<waveform>();
        *modBuf1It = delaySmoothing[0](delay1, kSmoothFilterFactor) + lfoValue * modDepth1;
        *modBuf2It = delaySmoothing[1](delay2, kSmoothFilterFactor) + lfoValue * modDepth2;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void Delay::renderInternal(const AudioBuffer& in, std::span<AudioBuffer, 2> out)
{
    // for performance reasons implementation is hard coded for 2 delay lines,
    // eventhough the number is configurable
    static_assert(kDelayLines == 2);

    bool filterOn = filterFreq < kMaxFilterFrequency;

    auto out1It = out[0].begin();
    auto out2It = out[1].begin();
    auto mod1It = modBuf1.begin(); // NOLINT(readability-qualified-auto)
    auto mod2It = modBuf2.begin(); // NOLINT(readability-qualified-auto)
    auto feedback = this->feedback;

    auto saturateFb = [feedback](auto input)
    {
        if (feedback >= 1.0f)
        {
            // see Amplifier::saturateSoftClip
            input = std::clamp(input * feedback, -1.5f, 1.5f);
            input = input - input * input * input / 6.72f;
            return std::clamp(input, -1.0f, 1.0f);
        }

        return input * feedback;
    };

    auto stereoInv = 1.0f - stereoSpread;

    switch (mode)
    {
        case Mode::kSingle:
            for (auto inIt = in.begin(); inIt != in.end();)
            {
                auto delayOut = delayLine1->read(*mod1It++);

                if (filterOn)
                {
                    delayOut = filter.render(delayOut);
                }

                delayLine1->write(*inIt++ + saturateFb(delayOut));

                *out1It++ = *out2It++ = delayOut;
            }
            break;
        case Mode::kDualTap:
            for (auto inIt = in.begin(); inIt != in.end();)
            {
                auto delayOut1 = delayLine1->read(*mod1It++);
                auto delayOut2 = delayLine1->read(*mod2It++);

                if (filterOn)
                {
                    std::tie(delayOut1, delayOut2) = filter.render({ delayOut1, delayOut2 });
                }

                delayLine1->write(*inIt++ + saturateFb(delayOut1));

                *out1It++ = delayOut1 + delayOut2 * stereoInv;
                *out2It++ = delayOut2 + delayOut1 * stereoInv;
            }
            break;
        case Mode::kTripleTap:
            for (auto inIt = in.begin(); inIt != in.end();)
            {
                auto delayMod1 = *mod1It++;
                auto delayMod2 = *mod2It++;
                auto delayOut1 = delayLine1->read(delayMod1);
                auto delayOut15 = delayLine1->read((delayMod1 + delayMod2) / 2);
                auto delayOut2 = delayLine1->read(delayMod2);

                auto input = *inIt++;
                auto filteredOut1 = delayOut1;
                if (filterOn)
                {
                    std::tie(filteredOut1, input) = filter.render({ filteredOut1, input });
                }
                delayLine1->write(input + saturateFb(filteredOut1));

                *out1It++ = delayOut1 + delayOut15 + delayOut2 * stereoInv;
                *out2It++ = delayOut2 + delayOut15 + delayOut1 * stereoInv;
            }
            break;
        case Mode::kStereo:
        case Mode::kPingpong:
            for (auto inIt = in.begin(); inIt != in.end();)
            {
                auto delayOut1 = delayLine1->read(*mod1It++);
                auto delayOut2 = delayLine2->read(*mod2It++);

                if (filterOn)
                {
                    std::tie(delayOut1, delayOut2) = filter.render({ delayOut1, delayOut2 });
                }

                auto input = *inIt++;
                delayLine1->write(input +
                                  saturateFb((mode == Mode::kStereo ? delayOut1 : delayOut2)));
                delayLine2->write(input +
                                  saturateFb((mode == Mode::kStereo ? delayOut2 : delayOut1)));

                *out1It++ = delayOut1 + delayOut2 * stereoInv;
                *out2It++ = delayOut2 + delayOut1 * stereoInv;
            }
            break;
    }
}

void Delay::render(const RenderContext& ctx, const AudioBuffer& in, std::span<AudioBuffer, 2> out)
{
    this->tempo = ctx.tempo;

    for (int delayIdx = 0; delayIdx < kDelayLines; delayIdx++)
    {
        updateTime(delayIdx);
    }

    if (filterDirty)
    {
        filter.setup(ctx, BiquadFilter::Shape::kBPF, 0, filterFreq, filterBandwidth);
        filterDirty = false;
    }

    auto modFreq = std::abs(this->modFreq);
    lfo.setFrequency(ctx, modFreq);

    auto lastDelay1 = static_cast<int>(modBuf1.back());
    auto lastDelay2 = static_cast<int>(modBuf2.back());

    if (this->modFreq >= 0)
    {
        renderModulation<SimpleLFO::Waveform::kSin>(ctx);
    }
    else
    {
        renderModulation<SimpleLFO::Waveform::kTriangle>(ctx);
    }

    // prefetch 2 words ahead of last delay, for interpolation needs
    // and to take modulation into account
    switch (mode)
    {
        case Mode::kTripleTap:
            delayLine1->prefetch((lastDelay1 + lastDelay2) / 2 - 2);
            [[fallthrough]];
        case Mode::kDualTap:
            delayLine1->prefetch(lastDelay1 - 2);
            delayLine1->prefetch(lastDelay2 - 2);
            break;
        case Mode::kSingle:
            delayLine1->prefetch(lastDelay1 - 2);
            [[fallthrough]];
        case Mode::kStereo:
        case Mode::kPingpong:
            delayLine2->prefetch(lastDelay2 - 2);
            break;
    }

    renderInternal(in, out);
}

void Delay::setTempoSync(int delayIdx, bool sync_)
{
    if (sync[delayIdx] == sync_)
    {
        return;
    }

    sync[delayIdx] = sync_;
    if (sync[delayIdx])
    {
        setTimeBasePulseCount(
          delayIdx,
          static_cast<int>(tempo * (static_cast<float>(kMidiPpqn) / 60.f) * delay[delayIdx]));
    }
    else
    {
        updateTime(delayIdx);
    }
}

bool Delay::isTempoSync(int delayIdx) const
{
    return sync[delayIdx];
};

void Delay::updateTime(int delayIdx)
{
    if (sync[delayIdx])
    {
        assert(tempo > 0);
        delay[delayIdx] = std::min(static_cast<float>(timeBasePulseCount[delayIdx]) *
                                     (60.f / static_cast<float>(kMidiPpqn)) / tempo,
                                   kMaxDelayTime);
    }
}

void Delay::setTimeBasePulseCount(int delayLineIdx, int timeBasePulseCount_)
{
    timeBasePulseCount.at(delayLineIdx) = timeBasePulseCount_;
    sync[delayLineIdx] = true;
}

void Delay::setTime(int delayLineIdx, time_t time)
{
    assert(time <= kMaxDelayTime);

    delay.at(delayLineIdx) = time;
    sync[delayLineIdx] = false;
};

void Delay::setFilterFrequency(param_t freq)
{
    filterFreq = freq;
    filterDirty = true;
};

void Delay::setFilterBandwidth(param_t bw)
{
    filterBandwidth = bw * 4.0f;
    filterDirty = true;
};

}