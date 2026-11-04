// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// masterfx's effects, in the audio interrupt, one of each slot at most. The chosen effect
// lives in the object's own buffers. The inner loops of Shimmer and BBD, the costliest
// reverb and delay, are in ITCM, as these two dispatchers are; see
// docs/mt/MASTERFX_PORT_PLAN.md. The rest runs from flash.

#include "masterfxDsp.h"

#if MT_HAS_MASTERFX && !defined(MT_HOST_TEST)

#include <arm_acle.h> // __ssat
#include <stdint.h>

// Named on each loop: a static function with one caller would otherwise be inlined into
// the dispatcher and land in ITCM with it. Moving a loop is one word in its header.
#define MFX_ITCM __attribute__((section(".audio_ram_functions"), noinline))
#define MFX_FLASH __attribute__((noinline))

namespace mods
{
namespace masterfx
{

uint8_t activeReverb = 0;
uint8_t activeDelay = 0;

namespace
{

bool reverbReady = false;
bool delayReady = false;

#include "masterfx/freeverb_dsp.h"
#include "masterfx/shimmer_dsp.h"
#include "masterfx/plate_dsp.h"
#include "masterfx/spring_dsp.h"
#include "masterfx/tape_dsp.h"
#include "masterfx/bbd_dsp.h"
#include "masterfx/gated_dsp.h"

// The order of kReverbNames and kDelayNames.
enum : uint8_t
{
    kFreeverb = 1,
    kPlate = 2,
    kSpring = 3,
    kShimmer = 4,
    kGated = 5,
    kReverse = 6,
};
enum : uint8_t
{
    kBbd = 2,
};

MFX_FLASH void reverbInit(uint8_t which, uint32_t buffer0, uint32_t buffer1)
{
    switch (which)
    {
        case kFreeverb: fv_init(buffer0, buffer1); break;
        case kPlate: plate_init(buffer0, buffer1); break;
        case kSpring: spring_init(buffer0, buffer1); break;
        case kShimmer: shimmer_init(buffer0, buffer1); break;
        default: gated_init(buffer0, buffer1); break;
    }
}

MFX_FLASH void delayInit(uint8_t which, const DelayKnobs& knobs, uint32_t buffer0, uint32_t buffer1)
{
    if (which == kBbd)
    {
        bbd_init(&knobs, buffer0, buffer1);
    }
    else
    {
        tape_init(&knobs, buffer0, buffer1);
    }
}

} // namespace

MFX_ITCM void reverbProcess(const ReverbKnobs& knobs,
                            uint16_t* buffer0,
                            uint16_t* buffer1,
                            int16_t* left,
                            int16_t* right)
{
    const uint8_t which = activeReverb;
    if (!reverbReady)
    {
        reverbInit(which, reinterpret_cast<uint32_t>(buffer0), reinterpret_cast<uint32_t>(buffer1));
        reverbReady = true;
    }
    switch (which)
    {
        case kFreeverb: fv_process(&knobs, left, right, 0); break;
        case kPlate: plate_process(&knobs, left, right); break;
        case kSpring: spring_process(&knobs, left, right); break;
        case kShimmer: shimmer_process(&knobs, left, right); break;
        default: gated_process(&knobs, left, right, which == kReverse); break;
    }
}

MFX_ITCM bool delayProcess(const DelayKnobs& knobs,
                           int16_t* buffer0,
                           int16_t* buffer1,
                           const int16_t* in,
                           int16_t* outLeft,
                           int16_t* outRight)
{
    const uint8_t which = activeDelay;
    if (!delayReady)
    {
        delayInit(which, knobs, reinterpret_cast<uint32_t>(buffer0), reinterpret_cast<uint32_t>(buffer1));
        delayReady = true;
    }
    if (which == kBbd)
    {
        bbd_process(&knobs, in, outLeft, outRight);
    }
    else
    {
        tape_process(&knobs, in, outLeft, outRight);
    }

    int32_t all = 0;
    for (uint32_t i = 0; i < BLOCK; i++)
    {
        all |= in[i] | outLeft[i] | outRight[i];
    }
    return all == 0;
}

void reverbCleared()
{
    reverbReady = false;
}

void delayCleared()
{
    delayReady = false;
}

} // namespace masterfx
} // namespace mods

#endif // MT_HAS_MASTERFX && !MT_HOST_TEST
