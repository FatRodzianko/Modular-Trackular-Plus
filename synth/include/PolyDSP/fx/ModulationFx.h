#pragma once

#include "PolyDSP/Core.h"
#include "PolyDSP/filters/BiquadFilter.h"
#include "PolyDSP/modulators/SimpleLFO.h"

#include <memory>

namespace polydsp
{
template<size_t kSize>
class DelayLine;

class ModulationFx
{
  public:
    static constexpr time_t kMaxFilterFrequency = 20000.0f;

    static inline constexpr std::array kModeNames = { "Single", "Dual", "Stereo", "Stereo X" };

    enum class Mode : uint8_t
    {
        kSingle,
        kDualTap,
        kStereo,
        kStereoCross,
    };

    explicit ModulationFx(std::span<sample_t> buffer);
    ~ModulationFx();

    static size_t getBufferSize();

    void render(const RenderContext& ctx, const AudioBuffer& in, std::span<AudioBuffer, 2> out);

    void setMode(Mode mode) { this->mode = mode; };

    /**
     * @param delay in seconds, enable output phase switch (180 rotation) when negative
     */
    void setDelay(time_t delay) { this->delay = delay; };

    void setFeedback(param_t feedback) { this->feedback = feedback; };
    void setModDepth(param_t modDepth) { this->modDepth = modDepth; };

    /**
     * @param modFreq in Hz, sin waveform for positive freq, triangle for negative
     */
    void setModFreq(param_t modFreq) { this->modFreq = modFreq; };

    /**
     * @param variation [0, 1] affects sound in various ways, depending on mode
     */
    void setVariation(param_t variation) { this->variation = variation; };
    void setFilterFrequency(param_t freq)
    {
        this->filterFreq = freq;
        filterDirty = true;
    };
    void setFilterBandwidth(param_t bw)
    {
        this->filterBandwidth = bw;
        filterDirty = true;
    };

    ModulationFx(const ModulationFx& other) = delete;
    ModulationFx(ModulationFx&& other) = delete;
    ModulationFx& operator=(const ModulationFx& other) = delete;
    ModulationFx& operator=(ModulationFx&& other) = delete;

  private:
    std::unique_ptr<DelayLine<kModFxDelayLineSize>> delayLine1;
    std::unique_ptr<DelayLine<kModFxDelayLineSize>> delayLine2;
    SimpleLFO lfo1 = {};
    SimpleLFO lfo2 = {};
    BiquadFilter filter = {};
    ModulationBufferArray modBuf1 = {};
    ModulationBufferArray modBuf2 = {};

    time_t delay = 0.007f;
    param_t feedback = 0.4f;
    param_t modFreq = 1.0f;
    param_t modDepth = 0.1f;
    param_t filterFreq = kMaxFilterFrequency;
    param_t filterBandwidth = 2;
    param_t variation = 0.3f;
    Mode mode = Mode::kSingle;
    bool filterDirty = true;
    int lastDelay1 = 0.0f;
    int lastDelay2 = 0.0f;

    void renderInternal(const RenderContext& ctx,
                        const AudioBuffer& in,
                        std::span<AudioBuffer, 2> out);
    template<SimpleLFO::Waveform waveform>
    void renderModulation(const RenderContext& ctx);
};
}