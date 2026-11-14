#pragma once

#include <cstdint>
#include <limits>

#include <boost/predef/architecture/arm.h>
#include <boost/predef/architecture/x86.h>

namespace polydsp
{
#if PRODUCT == WAVE

using sample_t = float;

using time_t = float; // time in seconds
using param_t = float;
#if BOOST_ARCH_ARM
using param_short_t = __fp16;
constexpr float kParamShortMax = 65504.0f;        // largest normal number for half-precision
constexpr float kParamShortMin = -kParamShortMax; // smallest normal number for half-precision
#elif BOOST_ARCH_X86
using param_short_t = float;
constexpr float kParamShortMax = std::numeric_limits<float>::max();
constexpr float kParamShortMin = std::numeric_limits<float>::lowest();
#endif
using note_t = int; // midi note values (https://en.wikipedia.org/wiki/MIDI_tuning_standard)

using wavetable_sample_t = int16_t;
inline constexpr sample_t wavetableToSample(wavetable_sample_t value)
{
    return static_cast<sample_t>(value) / static_cast<float>(0x8000);
};

using store_sample_t = int16_t;
inline constexpr sample_t storeSampleToInternal(store_sample_t value)
{
    return static_cast<sample_t>(value) / static_cast<float>(0x8000);
};

constexpr float kSilenceGain = -120;
constexpr sample_t kSilenceThreshold = 0.000001f;

constexpr int kBufferSize = 128;

constexpr int kMidiPpqn = 24;

constexpr int kMixerMaxSends = 2;

constexpr int kModMatrixBuffersPoolSize = 6 + 15; // PDSynth ModSources + ModDestinations count

constexpr int kReverbSampleRate = 44100.0f;
constexpr bool kReverbParamSmoothing = false;
constexpr std::size_t kDelayLineSize = 131072;
constexpr time_t kMaxDelayTime = 2.5f;
constexpr std::size_t kModFxDelayLineSize = 4096;

#else
#error "No product configuration defined"
#endif

}
