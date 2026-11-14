// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The firmware's side of the Master FX plugins (masterfxPlugins.h): each slot's code area in
// ITCM (which the MPU lets run code), its state and the float scratch in DTCM, the slots'
// buffers (CE's reverb and delay buffers, and the dynamics slot's own in SDRAM), CE's cache
// sync from the .ptfx loader, the cycle counter and the host table plugins call.

#include "modsBuild.h"

#if MT_HAS_MASTERFX && !defined(MT_HOST_TEST)

#include "masterfx.h"
#include "masterfxPlugins.h"

#include "Platform.h"
#include "Tracker.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "ptfx_loader.h"

#include <cmath>
#include <cr_section_macros.h>
#include <cstdio>

extern int16_t externalRamDelayBuff[];

namespace mods
{
namespace masterfx
{

namespace
{

constexpr uint32_t kDynamicsMemory = 64u * 1024u;
constexpr uint32_t kDelayLine = 262144u; // CE's MAX_DELAYLINE_LENGTH: three lines in externalRamDelayBuff

// Not zeroed at startup: the runtime copies a plugin's code in and zeroes its state
__NOINIT(SRAM_ITC) alignas(32) uint8_t codeAreas[plugins::kSlots][plugins::kCodeArea];
__attribute__((section(".noinit"), aligned(8))) uint8_t reverbState[PTMFX_STATE_REVERB];
__attribute__((section(".noinit"), aligned(8))) uint8_t delayState[PTMFX_STATE_DELAY];
__attribute__((section(".noinit"), aligned(8))) uint8_t dynamicsState[PTMFX_STATE_DYNAMICS];
__attribute__((section(".noinit"), aligned(8))) float scratch[plugins::kScratchFloats];
__NOINIT(EXTERNAL_RAM) alignas(32) uint8_t dynamicsMemory[kDynamicsMemory];
__NOINIT(EXTERNAL_RAM) alignas(4) uint8_t loadBuffer[plugins::kMaxFile];

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

uint32_t randomState = 0x2545F491u;

uint32_t random()
{
    uint32_t x = randomState; // xorshift32
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

float tempo()
{
    return sequencer.getCurrentTempo();
}

const PtmfxHost host = {
    1u,
    PTMFX_RATE,
    PTMFX_BLOCK,
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
    tempo,
};

} // namespace

void startPlugins(FileIo& io)
{
    if (plugins::started())
    {
        plugins::scan();
        refreshChoices();
        return;
    }
    AudioEffectPolyverb* reverb = engine.getMixerAndFx()->getPolyverb();
    plugins::Env env = {};
    for (size_t s = 0; s < plugins::kSlots; s++)
    {
        env.code[s] = codeAreas[s];
    }
    env.state[plugins::kReverb] = reverbState;
    env.state[plugins::kDelay] = delayState;
    env.state[plugins::kDynamics] = dynamicsState;
    env.memory[plugins::kReverb] = { { reverb->modsBuffer(0), reverb->modsBuffer(1), nullptr },
                                     { 32768u, 32768u, 0u } };
    env.memory[plugins::kDelay] = { { &externalRamDelayBuff[0], &externalRamDelayBuff[kDelayLine],
                                      &externalRamDelayBuff[2 * kDelayLine] },
                                    { 2u * kDelayLine, 2u * kDelayLine, 2u * kDelayLine } };
    env.memory[plugins::kDynamics] = { { dynamicsMemory, nullptr, nullptr }, { kDynamicsMemory, 0u, 0u } };
    env.loadBuffer = loadBuffer;
    env.scratch = scratch;
    env.syncCode = syncCode;
    env.cycles = cycles;
    env.millis = millis;
    // An eighth of a block: 128 frames at 44.1 kHz is 2,902 us
    env.budgetCycles = timer()->clockCyclesPerUs() * (2902u / 8u);
    env.host = &host;
    plugins::start(io, env);
    refreshChoices();
}

} // namespace masterfx
} // namespace mods

#endif // MT_HAS_MASTERFX && !MT_HOST_TEST
