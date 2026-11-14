// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// What CE's reverb and delay objects call, in the audio interrupt: the slot's plugin
// (masterfx/masterfxPlugins.h) on the object's block, in the object's own buffers. These two
// are in ITCM, as the runtime's process is; the plugin's code is in its slot's ITCM area.
// The effects masterfx-1.x built in are the default pack's plugins now (masterfx-sdk/pack/).

#include "masterfxDsp.h"

#if MT_HAS_MASTERFX && !defined(MT_HOST_TEST)

#include "masterfx/masterfxPlugins.h"

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

uint8_t activeReverb = 0;
uint8_t activeDelay = 0;

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
