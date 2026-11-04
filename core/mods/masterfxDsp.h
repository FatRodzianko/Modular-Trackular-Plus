// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// What CE's reverb and delay objects call to run masterfx's effects, in the audio
// interrupt: no SD, heap, blocking or UI here. The DSP is in masterfx/*_dsp.h.

#ifndef MODS_MASTERFXDSP_H_
#define MODS_MASTERFXDSP_H_

#include "modsBuild.h"

#if MT_HAS_MASTERFX

#include <cstdint>

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

// What plays: 0 is CE's own, the rest index kReverbNames and kDelayNames. Set by
// masterfx::apply while the object is blocked.
extern uint8_t activeReverb;
extern uint8_t activeDelay;

// One block of the stereo send, in place. Lays the effect out first if its buffers were
// cleared since it last ran.
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

// CE's clear of the object's buffers ran, with the object blocked: whichever effect is
// chosen now starts over in them.
void reverbCleared();
void delayCleared();

} // namespace masterfx
} // namespace mods

#endif // MT_HAS_MASTERFX

#endif /* MODS_MASTERFXDSP_H_ */
