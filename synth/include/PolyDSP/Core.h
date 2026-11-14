#pragma once

#include "config.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <span>

namespace polydsp
{
template<typename T>
using Buffer = std::span<T, kBufferSize>;

using AudioBuffer = Buffer<sample_t>;
using param_mod_t = param_t;
using ModulationBuffer = Buffer<param_short_t>;
using ConstModulationBuffer = Buffer<const param_short_t>;
using ModulationBufferArray = std::array<param_mod_t, kBufferSize>;

// ensure buffers can be copied by value in a single register
static_assert(sizeof(AudioBuffer) == sizeof(intptr_t));
static_assert(sizeof(ModulationBuffer) == sizeof(intptr_t));

struct RenderContext
{
    time_t samplingRate;
    time_t tempo;
};

constexpr note_t kNoteC4 = 60;
constexpr note_t kNoteA4 = 69;
constexpr note_t kOneOctaveNotes = 12;
constexpr time_t kNoteA4Freq = 440.0;

constexpr param_t kVelocityNeutral = 100.0f / 127.0f;

/**
 * @param detune detune in semitones
 */
constexpr time_t noteToFreq(note_t note, time_t detune = 0.0f)
{
    // NOLINTNEXTLINE(*-magic-numbers) come on, this is a formula
    return kNoteA4Freq *
           std::pow(2.0f, (static_cast<time_t>(note - kNoteA4) + detune) / kOneOctaveNotes);
}

/**
 * Calculate multiplier for frequency given specific detune value.
 *
 * @param detune detune in semitones
 */
constexpr time_t detuneMult(time_t detune)
{
    // NOLINTNEXTLINE(*-magic-numbers) come on, this is a formula
    return std::pow(2.0f, detune / kOneOctaveNotes);
}

constexpr param_t kDoubleLoundnessDb = 6.0f;

constexpr auto dbToAmplitude(auto db)
{
    // NOLINTNEXTLINE(*-magic-numbers) come on, this is a formula
    return db > kSilenceGain ? std::pow(static_cast<decltype(db)>(10), db * 0.05f) : 0;
}

constexpr auto amplitudeToDb(auto amplitude)
{
    // NOLINTNEXTLINE(*-magic-numbers) come on, this is a formula
    return amplitude > 0 ? std::log10(amplitude) * 20 : kSilenceGain;
}

using q31_t = int32_t;
constexpr float kQ31Max = static_cast<float>(std::numeric_limits<q31_t>::max());
constexpr float kInvQ31Max = 1.0f / kQ31Max;
inline constexpr q31_t floatToQ31(float value)
{
    return static_cast<q31_t>(value * kQ31Max);
}
inline constexpr float q31ToFloat(q31_t value)
{
    return static_cast<float>(value) * kInvQ31Max;
}
using sample_pos_t = q31_t;

}
