// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// What CE's reverb and delay objects call to run masterfx's plugins, in the audio
// interrupt: no SD, heap, blocking or UI here. The plugins' runtime is
// masterfx/masterfxPlugins.h.

#ifndef MODS_MASTERFXDSP_H_
#define MODS_MASTERFXDSP_H_

#include "modsBuild.h"

#if MT_HAS_MASTERFX

#include <cstdint>

class AudioEffectLimiter;

namespace mods
{
namespace masterfx
{

// CE's Reverb fields, as its setters leave them.
struct ReverbKnobs
{
    float amount;    // return = send + (wet - send) * amount
    float inputGain;
    float size;      // 0..1 (Size)
    float diffusion; // 0..0.85 (Diffusion)
    float lp;        // 1 - Damping
    int32_t predelay; // samples, 0..5000 (Predelay)
};

// CE's AudioEffectShortDelay fields, as its setters leave them.
struct DelayKnobs
{
    uint32_t time;      // samples (Time, or Sync and Rate)
    int32_t filterPole; // -pole * 1023 (Filter)
    uint8_t feedback;   // 0..252 (Feedback)
    uint8_t pingpong;
};

// CE's limiter fields, as its setters were given them.
struct DynamicsKnobs
{
    float attack;    // ms (Attack)
    float release;   // ms (Release)
    float threshold; // 0..1 of full scale, CE's curve applied (Threshold)
    uint32_t keyOn;  // the Sidechain list picked a key
};

// What plays: 0 is CE's own, 1 the slot's plugin. Set by masterfx::apply while the object is
// blocked.
extern uint8_t activeReverb;
extern uint8_t activeDelay;
// The limiter isn't blocked (the dry master goes through it): kDynamicsDry passes the master
// through untouched while the main loop puts a plugin in or clears its memory.
constexpr uint8_t kDynamicsDry = 2;
extern uint8_t activeDynamics;
// CE's two limiter objects, left and right (set at the plugins' start)
extern AudioEffectLimiter* limiters[2];

// One block of the stereo send, in place, through the reverb slot's plugin. The buffers are
// its memory (given at start); a bypassed plugin returns silence.
void reverbProcess(const ReverbKnobs& knobs,
                   uint16_t* buffer0,
                   uint16_t* buffer1,
                   int16_t* left,
                   int16_t* right);

// One block from the mono send to the stereo return, the same way. True if the send and
// the return were all zero.
bool delayProcess(const DelayKnobs& knobs,
                  int16_t* buffer0,
                  int16_t* buffer1,
                  const int16_t* in,
                  int16_t* outLeft,
                  int16_t* outRight);

// One block of the stereo master, in place, through the dynamics slot's plugin, keyed by the
// sidechain's blocks (null: no key). The gain for CE's meter (1.0 when it didn't run: then the
// master passes through).
float dynamicsProcess(const DynamicsKnobs& knobs,
                      int16_t* left,
                      int16_t* right,
                      const int16_t* keyLeft,
                      const int16_t* keyRight);

// CE's clear of the object's buffers ran, with the object blocked: the slot's plugin starts
// over in them (its reset, before its next block).
void reverbCleared();
void delayCleared();
// Main loop: CE's limiter was reset (AudioEffectLimiter::begin). The dynamics slot's memory is
// cleared with the master passed through, and its plugin starts over.
void dynamicsReset();

} // namespace masterfx
} // namespace mods

#endif // MT_HAS_MASTERFX

#endif /* MODS_MASTERFXDSP_H_ */
