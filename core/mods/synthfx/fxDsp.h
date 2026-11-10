// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The maths the synth effects need (synthFx.h), shared with WAVE's and PHASE's engines
// (synths/synthsDsp.h). Moved from synthsDsp.h with the arithmetic unchanged. Runs in the
// audio interrupt: single-precision only, no library calls.

#ifndef MODS_SYNTHFX_FXDSP_H_
#define MODS_SYNTHFX_FXDSP_H_

#include <cstdint>

namespace mtfx
{

#define FX_BLOCK            128u            /* samples per block, as CE's AUDIO_BLOCK_EXSAMPLES */
#define FX_RATE             44100.0f
#define FX_OUT_LIMIT        2.0f            /* what limit() holds a sample to */

/* x clamped to lo..hi; NaN gives lo */
static inline float clampf(float x, float lo, float hi)
{
    if (!(x >= lo))
        return lo;
    return x > hi ? hi : x;
}

/* 2^x for x in -100..100 (relative error under 2e-6) */
static inline float exp2f_fast(float x)
{
    x = clampf(x, -100.0f, 100.0f);
    int i = (int)(x + 128.0f) - 128;
    float f = x - (float)i;
    float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f
              + f * (0.00133336f + f * 0.00015404f)))));
    union { float f; int32_t i; } u = { p };
    u.i += i << 23;
    return u.f;
}

static inline uint32_t list_pick(float v, uint32_t count)  /* a stepped parameter's item; NaN gives 0 */
{
    return (uint32_t)(clampf(v, 0.0f, (float)(count - 1u)) + 0.5f);
}

/* tan(x) for 0 <= x <= 1.3 (cutoff up to 18.2 kHz), a Pade approximation (error under 0.3 %) */
static inline float tan_pade(float x)
{
    float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

/* x limited to +-FX_OUT_LIMIT; a NaN becomes the limit */
static inline float limit(float x)
{
#if defined(__ARM_ARCH)
    float lo = -FX_OUT_LIMIT, hi = FX_OUT_LIMIT;
    __asm("vmaxnm.f32 %0, %0, %1" : "+t"(x) : "t"(lo));
    __asm("vminnm.f32 %0, %0, %1" : "+t"(x) : "t"(hi));
    return x;
#else
    if (!(x >= -FX_OUT_LIMIT))              /* NaN too, as vmaxnm then vminnm give */
        return -FX_OUT_LIMIT;
    return x > FX_OUT_LIMIT ? FX_OUT_LIMIT : x;
#endif
}

static inline float fsqrt(float x)
{
#if defined(__ARM_ARCH)
    float r;
    __asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
#else
    return __builtin_sqrtf(x);
#endif
}

} // namespace mtfx

#endif // MODS_SYNTHFX_FXDSP_H_
