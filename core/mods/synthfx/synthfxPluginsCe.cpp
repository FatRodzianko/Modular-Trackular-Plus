// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The firmware's side of the Synth FX plugins (synthfxPlugins.h): their memory in SDRAM (which
// the MPU lets run code), CE's cache sync from the .ptfx loader, the cycle counter and the
// host table plugins call.

#include "modsBuild.h"

#if MT_HAS_SYNTHFX && !defined(MT_HOST_TEST)

#include "synthfxPlugins.h"

#include "Platform.h"
#include "Tracker.h"
#include "ptfx_loader.h"

#include <cmath>
#include <cr_section_macros.h>
#include <cstdio>

namespace mods
{
namespace synthfx
{

namespace
{

// The pools: SDRAM, not zeroed at startup (the runtime zeroes what it gives out)
__NOINIT(EXTERNAL_RAM) alignas(32) uint8_t codePool[plugins::kCodePoolSize];
__NOINIT(EXTERNAL_RAM) alignas(32) uint8_t statePool[plugins::kStatePoolSize];

Timer* timer()
{
    return Tracker::getInstance()->getPlatform()->getTimer();
}

uint32_t cycles()
{
    return timer()->clockCycles();
}

uint32_t millis()
{
    return timer()->millis();
}

void syncCode(void* addr, uint32_t size)
{
    PtfxLoader::syncInstructionCache(addr, size);
}

uint32_t randomState = 0x9E3779B9u;

uint32_t random()
{
    uint32_t x = randomState; // xorshift32; a block's interleaving with the main loop's only stirs it
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    randomState = x;
    return x;
}

float hostSin(float x) { return sinf(x); }
float hostCos(float x) { return cosf(x); }
float hostTan(float x) { return tanf(x); }
float hostTanh(float x) { return tanhf(x); }
float hostExp(float x) { return expf(x); }
float hostLog(float x) { return logf(x); }
float hostPow(float x, float y) { return powf(x, y); }
float hostSqrt(float x) { return sqrtf(x); }
float hostAbs(float x) { return fabsf(x); }
float hostMin(float x, float y) { return fminf(x, y); }
float hostMax(float x, float y) { return fmaxf(x, y); }

int print(char* out, uint32_t size, const char* format, float value)
{
    return snprintf(out, size, format, static_cast<double>(value)); // the main loop's (format())
}

const PtsfxHost host = {
    1u,
    PTSFX_RATE,
    PTSFX_BLOCK,
    hostSin,
    hostCos,
    hostTan,
    hostTanh,
    hostExp,
    hostLog,
    hostPow,
    hostSqrt,
    hostAbs,
    hostMin,
    hostMax,
    random,
    print,
};

} // namespace

void startPlugins(FileIo& io)
{
    // A quarter of a block: 128 frames at 44.1 kHz is 2,902 us
    const uint32_t budget = timer()->clockCyclesPerUs() * (2902u / 4u);
    const plugins::Env env = { codePool, statePool, syncCode, cycles, millis, budget, &host };
    plugins::start(io, env);
}

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX && !MT_HOST_TEST
