// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// What WAVE's and PHASE's engines and the effects share: maths, envelopes, the LFOs'
// tables, the output limit. Ported from Modular-Trackular's patch/synths (sy9) with the
// arithmetic unchanged, so a render matches sy9's sample for sample. Runs in the audio
// interrupt: single-precision only, no library calls.

#ifndef MODS_SYNTHS_SYNTHSDSP_H_
#define MODS_SYNTHS_SYNTHSDSP_H_

#include <cstdint>

namespace mtsynth
{

#define WT_BLOCK            128u
#define WT_RATE             44100.0f
#define WT_MAX_HZ           20000.0f        /* the highest harmonic a level may reach */

#define ENV_IDLE            0u
#define ENV_ATTACK          1u
#define ENV_DECAY           2u
#define ENV_SUSTAIN         3u
#define ENV_RELEASE         4u
struct wt_env { uint32_t stage; float level; };

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

static inline float note_hz(float note)
{
    return 440.0f * exp2f_fast((note - 69.0f) * (1.0f / 12.0f));
}

/* ---- the LFOs (Ratio, Waveform, Retrigger, Sync) ------------------------------------- */

#define LFO_NOTE            0u              /* Retrigger: from the start at each note */
#define LFO_ONE_SHOT        1u              /* ... once, then hold */
#define LFO_FREE            2u              /* ... never restarted */
#define LFO_SH              5u              /* Waveform: Random S&H */

static inline uint32_t list_pick(float v, uint32_t count)  /* a stepped parameter's item; NaN gives 0 */
{
    return (uint32_t)(clampf(v, 0.0f, (float)(count - 1u)) + 0.5f);
}

/* the length of each Sync division, in beats (1, 1/2 D, 1/2, 1/4 D, 1 T, 1/4, 1/8 D, 1/2 T,
 * 1/8, 1/16 D, 1/4 T, 1/16, 1/8 T, 1/32, 1/16 T, 1/32 T) */
static const float lfo_beats[16] = {
    4.0f, 3.0f, 2.0f, 1.5f, 4.0f / 3.0f, 1.0f, 0.75f, 2.0f / 3.0f, 0.5f, 0.375f, 1.0f / 3.0f, 0.25f,
    1.0f / 6.0f, 0.125f, 1.0f / 12.0f, 1.0f / 24.0f,
};

/* sin(2 pi t) for t in [0, 1): a parabola, refined (error under 0.1 %) */
static inline float sin_cycle(float t)
{
    float x = 2.0f * t - 1.0f;              /* -1..1 over the cycle: sin(2 pi t) = -sin(pi x) */
    float y = 4.0f * x * (1.0f - (x < 0.0f ? -x : x));
    y = 0.225f * (y * (y < 0.0f ? -y : y) - y) + y;
    return -y;
}

/* ---- envelopes --------------------------------------------------------------------- */

#define ENV_DT              (WT_BLOCK / WT_RATE)
#define ENV_OCTAVES_DOWN    9.9657843f      /* a decay or release takes its time to fall 60 dB */
#define ENV_END             1.0e-3f         /* -60 dB: the release has ended */
#define ENV_NEAR            1.0e-4f         /* the decay has reached the sustain level */

/* the per-block factor of an exponential segment that falls 60 dB in t seconds */
static inline float env_factor(float t)
{
    return exp2f_fast(-ENV_OCTAVES_DOWN * ENV_DT / clampf(t, 0.001f, 10.0f));
}

/* One block of an ADSR (adsr: attack, decay, sustain, release: times in seconds, sustain
 * 0..1); returns the level at the block's end */
static inline float env_step(struct wt_env *v, const float *adsr)
{
    float s = clampf(adsr[2], 0.0f, 1.0f);
    switch (v->stage) {
    case ENV_ATTACK: {
        float a = clampf(adsr[0], 0.0f, 10.0f);
        v->level = clampf(v->level, 0.0f, 1.0f) + (a > ENV_DT ? ENV_DT / a : 1.0f);
        if (v->level >= 1.0f) {
            v->level = 1.0f;
            v->stage = ENV_DECAY;
        }
        break;
    }
    case ENV_DECAY:
        v->level = s + (v->level - s) * env_factor(adsr[1]);
        if (v->level - s < ENV_NEAR && s - v->level < ENV_NEAR) {
            v->level = s;
            v->stage = ENV_SUSTAIN;
        }
        break;
    case ENV_SUSTAIN:
        v->level = s;
        break;
    case ENV_RELEASE:
        v->level *= env_factor(adsr[3]);
        if (!(v->level >= ENV_END)) {       /* NaN ends it too */
            v->level = 0.0f;
            v->stage = ENV_IDLE;
        }
        break;
    default:
        v->level = 0.0f;
        v->stage = ENV_IDLE;
        break;
    }
    return v->level;
}

/* a soft clip close to tanh: linear near 0, +-1 from +-3 (a rational approximation) */
static inline float sat(float x)
{
    x = x > 3.0f ? 3.0f : (x < -3.0f ? -3.0f : x);
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* tan(x) for 0 <= x <= 1.3 (cutoff up to 18.2 kHz), a Pade approximation (error under 0.3 %) */
static inline float tan_pade(float x)
{
    float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

#define WT_OUT_LIMIT        2.0f

/* x limited to +-WT_OUT_LIMIT; a NaN becomes the limit */
static inline float limit(float x)
{
#if defined(__ARM_ARCH)
    float lo = -WT_OUT_LIMIT, hi = WT_OUT_LIMIT;
    __asm("vmaxnm.f32 %0, %0, %1" : "+t"(x) : "t"(lo));
    __asm("vminnm.f32 %0, %0, %1" : "+t"(x) : "t"(hi));
    return x;
#else
    if (!(x >= -WT_OUT_LIMIT))              /* NaN too, as vmaxnm then vminnm give */
        return -WT_OUT_LIMIT;
    return x > WT_OUT_LIMIT ? WT_OUT_LIMIT : x;
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

/* The Mod Matrix's common sizes */
#define MOD_SLOTS           6u
#define MOD_SEMITONES       12.0f           /* Pitch: 100 % is an octave */
#define MOD_OCTAVES         4.0f            /* Cutoff: 100 % is 4 octaves */

} // namespace mtsynth

#endif // MODS_SYNTHS_SYNTHSDSP_H_
