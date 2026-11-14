#pragma once

#include "PolyDSP/Core.h"
#include "PolyDSP/filters/Basic.h"
#include "PolyDSP/filters/BiquadFilter.h"
#include "PolyDSP/modulators/SimpleLFO.h"

#include <memory>

namespace polydsp
{
template<size_t kSize>
class DelayLine;

class Delay
{
  public:
    static constexpr time_t kMaxDelayTime = polydsp::kMaxDelayTime;
    static constexpr time_t kMaxFilterFrequency = 20000.0f;
    static constexpr int kDelayLines = 2;

    static inline constexpr std::array kModeNames = { "Single",
                                                      "Dual Tap",
                                                      "Triple Tap",
                                                      "Stereo",
                                                      "Pingpong" };

    enum class Mode : uint8_t
    {
        kSingle,
        kDualTap,
        kTripleTap,
        kStereo,
        kPingpong,
    };

    explicit Delay(std::span<sample_t> buffer);
    ~Delay();

    static size_t getBufferSize();

    void render(const RenderContext& ctx, const AudioBuffer& in, std::span<AudioBuffer, 2> out);

    void setMode(Mode mode) { this->mode = mode; };

    /**
     * @param delay in seconds
     */
    void setTime(int delayLineIdx, time_t time);
    [[nodiscard]] time_t getTime(int delayLineIdx) const { return delay.at(delayLineIdx); };

    void setTempoSync(int delayIdx, bool sync_);
    [[nodiscard]] bool isTempoSync(int delayIdx) const;

    [[nodiscard]] int getTimeBasePulseCount(int delayLineIdx) const
    {
        return timeBasePulseCount.at(delayLineIdx);
    }
    void setTimeBasePulseCount(int delayLineIdx, int timeBasePulseCount_);

    void setFeedback(param_t feedback) { this->feedback = feedback; };
    void setModDepth(param_t modDepth) { this->modDepth = modDepth; };

    /**
     * @param modFreq in Hz, sin waveform for positive freq, triangle for negative
     */
    void setModFreq(param_t modFreq) { this->modFreq = modFreq; };

    void setStereoSpread(param_t spread) { this->stereoSpread = spread; };
    void setFilterFrequency(param_t freq);
    void setFilterBandwidth(param_t bw);

    Delay(const Delay& other) = delete;
    Delay(Delay&& other) = delete;
    Delay& operator=(const Delay& other) = delete;
    Delay& operator=(Delay&& other) = delete;

  private:
    std::unique_ptr<DelayLine<kDelayLineSize>> delayLine1;
    std::unique_ptr<DelayLine<kDelayLineSize>> delayLine2;
    SimpleLFO lfo = {};
    BiquadFilter filter = {};
    ModulationBufferArray modBuf1 = {};
    ModulationBufferArray modBuf2 = {};

    std::array<time_t, kDelayLines> delay = {};
    std::array<int, kDelayLines> timeBasePulseCount = {};
    param_t feedback = 0.4f;
    param_t modFreq = 1.0f;
    param_t modDepth = 0.0f;
    param_t filterFreq = kMaxFilterFrequency;
    param_t filterBandwidth = 2;
    param_t stereoSpread = 0.3f;
    Mode mode = Mode::kSingle;
    bool filterDirty = true;
    std::array<bool, kDelayLines> sync = {};
    std::array<AveragingFilter<param_t>, kDelayLines> delaySmoothing = {};

    float tempo = 120.0f;

    void updateTime(int delayIdx);

    template<SimpleLFO::Waveform waveform>
    void renderModulation(const RenderContext& ctx);
    void renderInternal(const AudioBuffer& in, std::span<AudioBuffer, 2> out);
};
}