#pragma once

#include "PolyDSP/Core.h"
#include "PolyDSP/config.h"
#include "PolyDSP/misc/Noise.h"
#include "PolyUtil/ExternalClockTempoMeasure.h"
#include "PolyUtil/misc.h"
#include "enum_util.h"

#include <algorithm>

namespace polydsp
{
class LFO
{
  public:
    enum class Waveform
    {
        kTriangle,
        kSin,
        kRampUp,
        kRampDown,
        kSquare,
        kRSH,
    };
    enum class Mode
    {
        kNoteRetrigger,
        kOneShot,
        kFreeRun
    };

    void render(const RenderContext& ctx, ModulationBuffer out);
    void noteOn(const RenderContext& ctx);

    [[nodiscard]] param_t getDelay() const { return delay; }
    void setDelay(param_t delay_) { delay = delay_; }
    [[nodiscard]] param_t getFrequency() const { return frequency; }
    /**
     * Set direct frequency of LFO.
     * Turn tempo sync mode off.
     */
    void setFrequency(param_t frequency_);
    [[nodiscard]] param_t getAmplitude() const { return amplitude; }
    void setAmplitude(param_t amplitude_) { amplitude = amplitude_; }
    [[nodiscard]] param_t getOffset() const { return offset; }
    void setOffset(param_t offset_) { offset = offset_; }
    [[nodiscard]] Waveform getWaveform() const { return waveform; }
    void setWaveform(Waveform waveform_) { waveform = waveform_; }
    [[nodiscard]] Mode getMode() const { return mode; }
    void setMode(Mode mode_) { mode = mode_; }

    /**
     * Get LFO sync time base in pulses per quarter note
     */
    [[nodiscard]] int getTimeBasePulseCount() const { return timeBasePulseCount; }

    /**
     * Set LFO sync time base in pulses per quarter note
     * Example. Time base of 24 pulses is equivalent to quarter note.
     * Switches LFO to tempo sync mode.
     * @param timeBasePulseCount_ in pulses per quarter note
     */
    void setTimeBasePulseCount(int timeBasePulseCount_);

    /**
     * Turn tempo sync mode on/off.
     * When switching the mode, recalculate pulse count or frequency
     * for closest match.
     */
    void setTempoSync(bool tempoSync_);
    [[nodiscard]] bool isTempoSync() const { return tempoSync; }

    void clockPulse();
    void start();

  private:
    param_t frequency = 1.0f;
    param_t delay = 0.0f;
    param_t amplitude = 1.0f;
    param_t offset = 0.0f;
    Waveform waveform = Waveform::kSin;
    Mode mode = Mode::kNoteRetrigger;

    int timeBasePulseCount = 24;

    int delayCounter = 0;
    float phase = 0;
    WhiteNoise noise;
    float rshOutputValue = 0.0f;
    bool completed = false;
    param_t lastOutput = 0.0f;

    float tempo = 120.0f;
    int pulseCount = 0;

    bool updatePhaseInRender = false;
    int framesSinceClockPulse = 0;
    // offset in frames is used to schedule phase update at the right moment
    int phasePulseUpdateDelay = 0;
    bool tempoSync = false;
    float kMinFrequency = 0.01f;

    void syncFrequency();
};
}