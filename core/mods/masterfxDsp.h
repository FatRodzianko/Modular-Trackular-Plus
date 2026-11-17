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
class AudioMux;

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

// ---- ducking chosen tracks instead of the master ---------------------------------------
// With a PTMFX_FLAG_DUCK plugin in the dynamics slot, a key and a set of tracks, the plugin works
// out its gain from the key once a block and the tracks' amplifiers multiply by it; the master
// passes through. A track that runs after the key's source this cycle gets this block's gain,
// one before it the last block's (one block, 2.9 ms, late).
// Destinations: 0 to 7 the sample tracks, 8 to 10 the synth parts.
constexpr uint8_t kDuckDestinations = 11;
extern uint16_t duckMask;   // main loop: bit d ducks destination d; 0 is the master
extern uint8_t duckLive;    // the interrupt's: the limiter found a key this block, so tracks duck
extern AudioMux* keyMuxes[2]; // CE's key muxes, left and right (set at the plugins' start)

inline bool ducking(uint8_t destination)
{
    return duckLive != 0 && ((duckMask >> destination) & 1u) != 0;
}

// A ducked track's block (one side), after its amplifier's own gain
void duckBlock(int16_t* data);

// ---- a bus plugin on chosen tracks ------------------------------------------------------
// With a PTMFX_FLAG_BUS plugin in the dynamics slot and a set of tracks, the plugin works on the
// sum of those tracks (the bus) in place of the master. It runs in CE's tracks mixer, where the
// tracks' and synth parts' blocks are summed (its inputs 0 to 10 are the destinations): the left
// mixer object takes both objects' blocks, sums the chosen inputs into the bus and the others into
// the mix, the plugin works on the bus, and the mix + the plugin's bus goes through CE's
// saturation as CE's sum would. So it is exact whatever the project's saturation. Sends and the
// other tracks are untouched. The limiter passes the master through and shows the plugin's gain.
// The bus is summed straight into the plugins' float scratch (no memory of its own), the mix on
// the mixer's stack, a side at a time, as CE's mixer sums.
constexpr uint8_t kBusInputs = kDuckDestinations; // CE's tracks mixer: 8 tracks, 3 synth parts
extern uint16_t busMask;    // main loop: bit d is destination d in the bus; 0: the plugin on the master
extern uint8_t busLive;     // the interrupt's: the limiter saw the bus last pass, so the mixer runs it
extern void* busMixers[2];  // CE's tracks mixer objects, left and right (set at the plugins' start)
// The bus, left then right, 128 floats each at 1.0 for full scale: the plugins' float scratch
// (set at the plugins' start). The mixer writes it whole before busRun.
extern float* busFloats;

// The tracks mixer's left object, with busLive: the bus in busFloats through the plugin, in place
// (left as it is if the plugin didn't run, held within +-11.0 if it did). The mixer then adds it
// to the mix.
void busRun();

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
// sidechain's blocks (null: no key). With a bus (above) the master passes through and the gain
// is the bus's. The gain for CE's meter (1.0 when it didn't run: then the master passes through).
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
