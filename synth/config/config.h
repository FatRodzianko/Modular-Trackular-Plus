#pragma once

#include <array>
#include <string>

/*
 * Config file contains various definitions that PolySynth used internally and
 * that can vary between products using the module.
 * Product-specific definitions of these items are guarded by PRODUCT_XYZ
 * defines.
 */

#if defined(PRODUCT_WAVE)

#define FEATURE_SEQUENCER

#elif defined(PRODUCT_PLAY_PLUS) || defined(PRODUCT_TRACKER_PLUS)

#else
#error "No product configuration defined"
#endif

// shared definitions

extern void _debuglog(char* msg);
extern void _debuglog(const char* format, ...);
// Print msg to a debug console/log
#ifdef DEBUG
#define LOG(msg) (_debuglog(msg))
#define LOGF(format, ...) _debuglog(format, __VA_ARGS__)
#else
#define LOG(msg)
#define LOGF(format, ...)
#endif

static constexpr int kGridRows = 5;
static constexpr int kGridColumns = 12;

static inline const std::string kGridPresetsFolder = "";

static constexpr int kMidiOutInstrumentCCs = 12;

static constexpr auto kDefaultCCIds = std::array{
    74, 71, 22, 75, 17, 19, 23, 24, 25, 26, 27, 28,
};

static constexpr int kAudioSampleRate = 44100;
static constexpr int kSeqPpqn = 96;
static constexpr int kMidiPpqn = 24;
static constexpr int kPulsesPerStep = kSeqPpqn / 4;

static constexpr int kMaxSequencerTracks = 8;

static constexpr int kMaxInstrumentVoices = 8;

// Begin and end a critical section, usually implemented as irq disable/enable
extern void disableAudioInterrupt();
extern void enableAudioInterrupt();
#if !defined(UNIT_TESTS) && !defined(REGRESSION_TESTS)
#define AUDIO_CS_BEGIN() disableAudioInterrupt()
#define AUDIO_CS_END() enableAudioInterrupt()
#else
#define AUDIO_CS_BEGIN()
#define AUDIO_CS_END()
#endif
