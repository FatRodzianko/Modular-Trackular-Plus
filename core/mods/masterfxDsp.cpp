// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// What CE's reverb and delay objects call, in the audio interrupt: the slot's plugin
// (masterfx/masterfxPlugins.h) on the object's block, in the object's own buffers. These two
// are in ITCM, as the runtime's process is; the plugin's code is in its slot's ITCM area.
// The effects masterfx-1.x built in are the default pack's plugins now (masterfx-sdk/pack/).

#include "masterfxDsp.h"

#if MT_HAS_MASTERFX && !defined(MT_HOST_TEST)

#include "masterfx/masterfxPlugins.h"
#include "mux.h"

#include <cstddef>
#include <stdint.h>

#define MFX_ITCM __attribute__((section(".audio_ram_functions"), noinline))

namespace mods
{
namespace masterfx
{

// The plugins get CE's knobs as they are
static_assert(sizeof(ReverbKnobs) == sizeof(PtmfxReverbKnobs) && offsetof(ReverbKnobs, lp) == offsetof(PtmfxReverbKnobs, lp) &&
              offsetof(ReverbKnobs, predelay) == offsetof(PtmfxReverbKnobs, predelay));
static_assert(sizeof(DelayKnobs) == sizeof(PtmfxDelayKnobs) && offsetof(DelayKnobs, filterPole) == offsetof(PtmfxDelayKnobs, filter_pole) &&
              offsetof(DelayKnobs, feedback) == offsetof(PtmfxDelayKnobs, feedback) &&
              offsetof(DelayKnobs, pingpong) == offsetof(PtmfxDelayKnobs, pingpong));
static_assert(sizeof(DynamicsKnobs) == sizeof(PtmfxDynamicsKnobs) &&
              offsetof(DynamicsKnobs, threshold) == offsetof(PtmfxDynamicsKnobs, threshold) &&
              offsetof(DynamicsKnobs, keyOn) == offsetof(PtmfxDynamicsKnobs, key_on));

uint8_t activeReverb = 0;
uint8_t activeDelay = 0;
uint8_t activeDynamics = 0;
AudioEffectLimiter* limiters[2] = {};
uint16_t duckMask = 0;
uint8_t duckLive = 0;
AudioMux* keyMuxes[2] = {};

namespace
{

// Every field, so the compiler has no struct to clear with a call
inline void blockFor(PtmfxBlock& b, int16_t* left, int16_t* right, const int16_t* in, const void* knobs)
{
    b.left = left;
    b.right = right;
    b.in = in;
    b.key_left = nullptr;
    b.key_right = nullptr;
    b.fleft = nullptr;
    b.fright = nullptr;
    b.fin = nullptr;
    b.fkey_left = nullptr;
    b.fkey_right = nullptr;
    b.knobs = knobs;
    b.frames = PTMFX_BLOCK;
    b.gain = nullptr;
}

// The ducking gain: this block's (worked out from this block's key) and the last block's. DTCM,
// written before it is read (duckPrevValid). duckFresh: this block's is worked out.
__attribute__((section(".noinit"), aligned(8))) float duckGain[2][PTMFX_BLOCK];
uint8_t duckCur = 0;
uint8_t duckFresh = 0;
uint8_t duckPrevValid = 0;
DynamicsKnobs duckKnobs; // CE's knobs as the limiter last saw them

MFX_ITCM void duckCompute(const int16_t* keyLeft, const int16_t* keyRight)
{
    float* const g = duckGain[duckCur];
    PtmfxBlock b;
    blockFor(b, nullptr, nullptr, nullptr, &duckKnobs);
    b.key_left = keyLeft;
    b.key_right = keyRight;
    b.gain = g;
    if (plugins::process(plugins::kDynamics, b, nullptr) != plugins::Ran::ran)
    {
        for (uint32_t i = 0; i < PTMFX_BLOCK; i++)
        {
            g[i] = 1.0f; // bypassed: nothing ducked
        }
    }
    duckFresh = 1;
}

inline void silence(int16_t* left, int16_t* right)
{
    for (uint32_t i = 0; i < PTMFX_BLOCK; i++)
    {
        left[i] = 0;
        right[i] = 0;
    }
}

} // namespace

MFX_ITCM void reverbProcess(const ReverbKnobs& knobs, uint16_t*, uint16_t*, int16_t* left, int16_t* right)
{
    PtmfxBlock b;
    blockFor(b, left, right, nullptr, &knobs);
    if (plugins::process(plugins::kReverb, b, nullptr) != plugins::Ran::ran)
    {
        silence(left, right); // bypassed: no return (the send would come back dry)
    }
}

MFX_ITCM bool delayProcess(const DelayKnobs& knobs,
                           int16_t*,
                           int16_t*,
                           const int16_t* in,
                           int16_t* outLeft,
                           int16_t* outRight)
{
    PtmfxBlock b;
    blockFor(b, outLeft, outRight, in, &knobs);
    if (plugins::process(plugins::kDelay, b, nullptr) != plugins::Ran::ran)
    {
        silence(outLeft, outRight);
    }

    int32_t all = 0;
    for (uint32_t i = 0; i < PTMFX_BLOCK; i++)
    {
        all |= in[i] | outLeft[i] | outRight[i];
    }
    return all == 0;
}

MFX_ITCM float dynamicsProcess(const DynamicsKnobs& knobs,
                               int16_t* left,
                               int16_t* right,
                               const int16_t* keyLeft,
                               const int16_t* keyRight)
{
    float gain = 1.0f;
    if (activeDynamics != 1)
    {
        duckLive = 0;
        duckFresh = 0;
        duckPrevValid = 0;
        return gain; // the main loop is changing the slot: the master as it is
    }
    if (duckMask != 0 && keyLeft != nullptr && keyRight != nullptr)
    {
        // Ducking tracks: this block's gain from this block's key, if no track asked for it yet;
        // then it is the last block's for the next. The master passes through.
        duckKnobs = knobs;
        if (!duckFresh)
        {
            duckCompute(keyLeft, keyRight);
        }
        const float* g = duckGain[duckCur];
        float sum = 0.0f;
        for (uint32_t i = 0; i < PTMFX_BLOCK; i++)
        {
            sum += g[i] < 1.0f ? g[i] : 1.0f; // the meter shows the turning down only
        }
        duckCur ^= 1u;
        duckFresh = 0;
        duckPrevValid = 1;
        duckLive = 1;
        return sum * (1.0f / PTMFX_BLOCK);
    }
    duckLive = 0;
    duckFresh = 0;
    duckPrevValid = 0;
    PtmfxBlock b;
    blockFor(b, left, right, nullptr, &knobs);
    b.key_left = keyLeft;
    b.key_right = keyRight;
    plugins::process(plugins::kDynamics, b, &gain); // bypassed: the master passes through
    return gain;
}

MFX_ITCM void duckBlock(int16_t* data)
{
    const float* g = nullptr;
    const audio_block_t* keyLeft = keyMuxes[0] != nullptr ? keyMuxes[0]->modsPeek() : nullptr;
    const audio_block_t* keyRight = keyMuxes[1] != nullptr ? keyMuxes[1]->modsPeek() : nullptr;
    if (keyLeft != nullptr && keyRight != nullptr && activeDynamics == 1)
    {
        if (!duckFresh)
        {
            duckCompute(keyLeft->data, keyRight->data); // the key has run before this track
        }
        g = duckGain[duckCur];
    }
    else if (duckPrevValid)
    {
        g = duckGain[duckCur ^ 1u]; // the key runs after this track: the last block's
    }
    if (g == nullptr)
    {
        return;
    }
    for (uint32_t i = 0; i < PTMFX_BLOCK; i++)
    {
        float v = static_cast<float>(data[i]) * g[i];
        v = v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
        data[i] = static_cast<int16_t>(v >= 0.0f ? v + 0.5f : v - 0.5f);
    }
}

void reverbCleared()
{
    plugins::cleared(plugins::kReverb);
}

void delayCleared()
{
    plugins::cleared(plugins::kDelay);
}

} // namespace masterfx
} // namespace mods

#endif // MT_HAS_MASTERFX && !MT_HOST_TEST
