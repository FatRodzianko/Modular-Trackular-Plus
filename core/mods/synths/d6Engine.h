// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// DCO6: a virtual-analog voice after the Juno-106's design (docs/mt/VA_SYNTH_PLAN.md).
// PROTOTYPE for the plan's CPU measurement: the DSP is the plan's, the parameter set and the
// unison hook are not final. tools/synths/dco6_proto.py is the reference it follows.
//
// One voice: DCO (saw, pulse with PWM, square sub an octave down, white noise; saw, pulse and
// sub band-limited with a 4-point step correction from the integrated cubic B-spline, so the
// DCO's output is 2 samples late) -> Bass Boost (a +6 dB low shelf, the 106's HPF
// position 0, as a switch) -> HPF (a one-pole high-pass, 20 Hz = Off to 2 kHz) -> 4-pole ladder low-pass (trapezoidal one-poles, the
// feedback solved without a unit delay: Zavalishin's TPT; a soft clip on the ladder's input)
// -> VCA (the envelope, or a gate) -> pan. One ADSR for VCF and VCA; one LFO with a delay.
// Vintage (slop) and the unison place (detune, pan) are per voice.
//
// Audio interrupt: d6_note_on, d6_note_off, d6_render. Single precision, no library calls.

#ifndef MODS_SYNTHS_D6ENGINE_H_
#define MODS_SYNTHS_D6ENGINE_H_

#include "synthfx/fxDsp.h"

#include <cstdint>

namespace mtsynth
{

using namespace mtfx;

/* Parameter ids. Patches store values by id: new ones only ever go at the end. */
enum : int
{
    DP_RANGE = 0, DP_TUNE, DP_FINE, DP_SAW, DP_PULSE, DP_SUB, DP_NOISE, DP_PW, DP_PWM, DP_PWM_SRC,
    DP_LFO_PITCH,                                                           /* 0-10  DCO */
    DP_HPF, DP_CUTOFF, DP_RES, DP_ENV_AMT, DP_LFO_CUT, DP_KEY,              /* 11-16 Filter */
    DP_ATTACK, DP_DECAY, DP_SUSTAIN, DP_RELEASE, DP_VCA,                    /* 17-21 Env */
    DP_LFO_WAVE, DP_LFO_RATE, DP_LFO_DELAY, DP_LFO_SYNC, DP_LFO_TRIG,       /* 22-26 LFO */
    DP_UNISON, DP_DETUNE, DP_WIDTH, DP_VINTAGE, DP_GLIDE, DP_VOLUME,        /* 27-32 Voice */
    DP_UNI_PLACE,               /* 33: in no section; the host hook sets it per voice: slot + 16 * size */
    DP_BOOST,                   /* 34: Filter section, Bass Boost Off/On */
    DP_COUNT
};

#define D6_BLOCK            128u
#define D6_CTRL             8u                      /* control-rate step, samples */
#define D6_RATE             44100.0f
#define D6_LEVEL            0.2f
#define D6_HPF_OFF_HZ       20.0f                   /* HPF at its minimum: Off (bypassed) */
#define D6_HPF_MAX_HZ       2000.0f
#define D6_BOOST_HZ         110.0f                  /* Bass Boost's shelf corner: by ear */
#define D6_LFO_SHAPES       7u                      /* Triangle, Sine, Square, Ramp Up, Ramp Down, S&H, Smooth */

struct d6_voice
{
    float phase, dt, sub;
    float z[4];                 /* the DCO's 4-point correction: samples n-2, n-1, n, n+1 */
    float s[4];                 /* ladder */
    float hs, hb;               /* HPF, Bass Boost shelf */
    float env, gate;
    float pitch;                /* semitones, gliding */
    float lfoPhase, lfoAge;
    uint32_t lfoSeed, lfoCycle; /* the random shapes' values: d6_hash(seed + cycle) */
    float drift[2];             /* Vintage: pitch, cutoff random walks */
    float noteOff[2];           /* Vintage: per note, cents and octaves */
    float tol[6];               /* Vintage: per voice, -1..1: pitch, cutoff, attack, decay, pw, level */
    uint32_t rnd;
    uint8_t stage;              /* 0 idle, 1 attack, 2 decay/sustain, 3 release */
    uint8_t gateOn, note, slot, size, index, glideFrom;
};

static inline float d6_rand(d6_voice* v)    /* xorshift32, -1..1 */
{
    uint32_t x = v->rnd;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v->rnd = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

static inline uint32_t d6_rand_u32(d6_voice* v)
{
    d6_rand(v);
    return v->rnd;
}

/* An integer hash (Chris Wellons's "lowbias32"), -1..1: the random LFO shapes' value for a cycle, so
 * voices that share an LFO cycle also share its random value. */
static inline float d6_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

/* LFO Trigger Free: the 106 has one LFO for all its voices. A plugin has no clock and no way to tell
 * which part a voice is in, so voices whose LFO runs at the same rate and shape share one entry here:
 * each sounding voice writes its phase at the end of a block, and a voice starting a note takes the
 * phase from the entry (note-ons come between blocks). Two parts with the same LFO settings share an
 * entry, which sounds the same as two parts locked to one LFO. While no voice sounds, the entry
 * stands still. Plugin memory (the .psyn arena), touched only from the audio interrupt. */
#define D6_LFO_SLOTS        8u
#define D6_FREE_SEED        0x51ED270Bu
struct d6_lfo_slot
{
    uint32_t key, cycle, age;
    float phase;
};
static d6_lfo_slot d6_lfoTab[D6_LFO_SLOTS];
static uint32_t d6_lfoClock;

static inline uint32_t d6_lfo_key(const float* p, float rateHz)
{
    union { float f; uint32_t u; } r = { rateHz };
    const uint32_t k = r.u ^ (list_pick(p[DP_LFO_WAVE], D6_LFO_SHAPES) * 0x9E3779B9u);
    return k | 1u;                                  /* 0 is an empty slot */
}

static inline d6_lfo_slot* d6_lfo_find(uint32_t key)
{
    for (uint32_t i = 0; i < D6_LFO_SLOTS; i++)
        if (d6_lfoTab[i].key == key)
            return &d6_lfoTab[i];
    return nullptr;
}

static inline void d6_lfo_store(const d6_voice* v, uint32_t key)
{
    d6_lfo_slot* s = d6_lfo_find(key);
    if (!s) {                                       /* the slot written longest ago */
        s = &d6_lfoTab[0];
        for (uint32_t i = 1; i < D6_LFO_SLOTS; i++)
            if (d6_lfoClock - d6_lfoTab[i].age > d6_lfoClock - s->age)
                s = &d6_lfoTab[i];
        s->key = key;
    }
    s->phase = v->lfoPhase;
    s->cycle = v->lfoCycle;
    s->age = ++d6_lfoClock;
}

/* The 4-point step correction: the integrated cubic B-spline minus a unit step (Valimaki, Pekonen and
 * Nam, JASA 2012). A step of size `step`, d samples (0..1) before sample n, adds to samples n-2, n-1,
 * n and n+1 (z[0..3]); the DCO's output is z[0], 2 samples late. */
static inline void d6_blep4(float* z, float d, float step)
{
    d = clampf(d, 0.0f, 1.0f);
    const float u = 1.0f - d;
    const float d2 = d * d, u2 = u * u;
    z[0] += step * (d2 * d2 * (1.0f / 24.0f));
    z[1] += step * (0.5f - u * (2.0f / 3.0f) + u2 * u * (1.0f / 3.0f) - u2 * u2 * 0.125f);
    z[2] -= step * (0.5f - d * (2.0f / 3.0f) + d2 * d * (1.0f / 3.0f) - d2 * d2 * 0.125f);
    z[3] -= step * (u2 * u2 * (1.0f / 24.0f));
}

static inline float d6_clip(float x)        /* tanh-like, +-1 from |x| = 3 */
{
    x = clampf(x, -3.0f, 3.0f);
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* One-pole coefficient going tc2 / ln 2 time constants in t seconds. D6_TC_99: 99 % of the way (decay,
 * release). D6_TC_ATTACK: the attack aims at 1.3 and reaches its peak, 1.0, in t (ln(1.3 / 0.3)). */
#define D6_TC_99            6.6365f                 /* 4.6 / ln 2 */
#define D6_TC_ATTACK        2.1155f                 /* ln(1.3 / 0.3) / ln 2 */
/* 1 - 2^-x. For small x (long times) 1 - exp2f_fast(-x) loses the answer to exp2f_fast's error (2e-6
 * against an answer of 1e-5 for a 1 s decay), so it's a series in y = x ln 2 there (error < y^6 / 720). */
static inline float d6_one_minus_exp2(float x)
{
    const float y = x * 0.69314718f;
    if (y < 0.25f)
        return y * (1.0f - y * (0.5f - y * ((1.0f / 6.0f) - y * ((1.0f / 24.0f) - y * (1.0f / 120.0f)))));
    return 1.0f - exp2f_fast(-x);
}

static inline float d6_coef(float t, float tc2 = D6_TC_99)
{
    t = clampf(t, 0.0005f, 30.0f);
    return d6_one_minus_exp2(tc2 / (t * D6_RATE));
}

static inline void d6_voice_init(d6_voice* v, int index)
{
    *v = d6_voice{};
    v->index = (uint8_t)index;
    v->rnd = (uint32_t)(1000 + index) * 2654435761u + 0x9E3779B9u;
    if (!v->rnd)
        v->rnd = 1;
    for (float& t : v->tol)
        t = d6_rand(v);
    v->phase = (d6_rand(v) + 1.0f) * 0.5f;
    v->sub = 1.0f;
    v->size = 1;
}

/* lfoHz: the LFO's rate as the caller works it out for the next block (for LFO Trigger Free) */
static inline void d6_note_on(d6_voice* v, const float* p, uint32_t note, uint32_t slot, uint32_t size,
                              float lfoHz)
{
    v->glideFrom = v->stage != 0 && p[DP_GLIDE] > 0.0f;
    if (!v->glideFrom)
        v->pitch = (float)note;
    v->note = (uint8_t)note;
    v->slot = (uint8_t)slot;
    v->size = (uint8_t)(size ? size : 1);
    v->stage = 1;
    v->gateOn = 1;
    v->lfoAge = 0.0f;
    const float vin = clampf(p[DP_VINTAGE], 0.0f, 1.0f);
    v->noteOff[0] = vin * 1.5f * d6_rand(v);
    v->noteOff[1] = vin * 0.05f * d6_rand(v);
    if (p[DP_LFO_TRIG] < 0.5f) {                    /* Note: the LFO starts again, its own randoms */
        v->lfoPhase = 0.0f;
        v->lfoCycle = 0;
        v->lfoSeed = d6_rand_u32(v);
    } else {                                        /* Free: join the others at this rate and shape */
        v->lfoSeed = D6_FREE_SEED;
        if (const d6_lfo_slot* s = d6_lfo_find(d6_lfo_key(p, lfoHz))) {
            v->lfoPhase = s->phase;
            v->lfoCycle = s->cycle;
        }
    }
}

static inline void d6_note_off(d6_voice* v)
{
    if (v->stage)
        v->stage = 3;
    v->gateOn = 0;
}

static inline bool d6_sounding(const d6_voice* v)
{
    return v->stage != 0;
}

static inline float d6_lfo(d6_voice* v, const float* p, float rateHz)
{
    const float ph = v->lfoPhase;
    float y;
    switch (list_pick(p[DP_LFO_WAVE], D6_LFO_SHAPES)) {
    case 0: y = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
    case 1: {                               /* sine from a parabola pair (error under 0.1 %) */
        float x = ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f;   /* triangle, -1..1 */
        y = x * (1.5f - 0.5f * x * x);                                /* smooth: a sine-like curve */
        break;
    }
    case 2: y = ph < 0.5f ? 1.0f : -1.0f; break;
    case 3: y = 2.0f * ph - 1.0f; break;
    case 4: y = 1.0f - 2.0f * ph; break;
    case 5: y = d6_hash(v->lfoSeed + v->lfoCycle); break;
    default: {
        const float a = d6_hash(v->lfoSeed + v->lfoCycle), b = d6_hash(v->lfoSeed + v->lfoCycle + 1u);
        y = a + (b - a) * (ph * ph * (3.0f - 2.0f * ph));
        break;
    }
    }
    float n = ph + rateHz * ((float)D6_CTRL / D6_RATE);
    if (n >= 1.0f) {
        const uint32_t whole = (uint32_t)n;
        n -= (float)whole;
        v->lfoCycle += whole;
    }
    v->lfoPhase = n;
    const float d = p[DP_LFO_DELAY];
    v->lfoAge += (float)D6_CTRL / D6_RATE;
    const float fade = d > 0.0f ? clampf(v->lfoAge / d, 0.0f, 1.0f) : 1.0f;
    return y * fade;
}

/* One block into L and R (added to what is there). lfoHz: the LFO's rate now (Hz, or the synced
 * rate from the tempo; worked out per block by the caller). bend: pitch bend in semitones. */
static void d6_render(d6_voice* v, const float* p, float* L, float* R, float lfoHz, float bend)
{
    if (!v->stage)
        return;
    const float vin = clampf(p[DP_VINTAGE], 0.0f, 1.0f);
    const float* tol = v->tol;
    const float ca = d6_coef(p[DP_ATTACK] * (1.0f + 0.08f * vin * tol[2]), D6_TC_ATTACK);
    const float cd = d6_coef(p[DP_DECAY] * (1.0f + 0.08f * vin * tol[3]));
    const float cr = d6_coef(p[DP_RELEASE] * (1.0f + 0.08f * vin * tol[3]));
    const float sus = clampf(p[DP_SUSTAIN], 0.0f, 1.0f);
    /* Vintage drift: a leaky random walk per voice, once a block: corner ~0.2 Hz, sd ~0.5 */
    for (int k = 0; k < 2; k++)
        v->drift[k] = 0.99635f * v->drift[k] + 0.074f * d6_rand(v);

    /* unison: this voice's detune (cents) and pan */
    float detC = 0.0f, gl = 1.0f, gr = 1.0f;
    const uint32_t size = v->size;
    if (size > 1) {
        const uint32_t slot = v->slot < size ? v->slot : size - 1;
        const float inv = 1.0f / (float)(size - 1);
        const float spread = 2.0f * (float)slot * inv - 1.0f;
        const uint32_t order = slot < (size + 1) / 2 ? 2 * slot : 2 * (size - 1 - slot) + 1;
        detC = p[DP_DETUNE] * (2.0f * (float)order * inv - 1.0f);
        const float pan = clampf(p[DP_WIDTH], 0.0f, 1.0f) * spread;
        /* equal power, centre 1 each side: sqrt2 * cos / sin of (pan + 1) * pi/4, by a parabola */
        const float a = (pan + 1.0f) * 0.5f;                       /* 0..1 */
        const float stack = 1.0f / fsqrt((float)size);
        gl = fsqrt(2.0f * (1.0f - a)) * stack;
        gr = fsqrt(2.0f * a) * stack;
    }
    const float level = clampf(p[DP_VOLUME], 0.0f, 1.0f) * D6_LEVEL * (1.0f + 0.046f * vin * tol[5]);
    gl *= level;
    gr *= level;

    /* Bass Boost and the HPF: coefficients once a block (they aren't modulated inside a block) */
    const float hpfHz = clampf(p[DP_HPF], D6_HPF_OFF_HZ, D6_HPF_MAX_HZ);
    const bool hpfOn = hpfHz > D6_HPF_OFF_HZ + 0.5f;
    const bool boost = p[DP_BOOST] >= 0.5f;
    const bool lowEnd = hpfOn || boost;     /* one check per sample when both are off */
    float hG = 0.0f, bG = 0.0f;
    if (hpfOn) {
        const float g = tan_pade(3.14159265f * hpfHz / D6_RATE);
        hG = g / (1.0f + g);
    }
    if (boost) {
        const float g = tan_pade(3.14159265f * D6_BOOST_HZ / D6_RATE);
        bG = g / (1.0f + g);
    }
    const float kq = 4.1f * clampf(p[DP_RES], 0.0f, 1.0f);
    const float comp = 1.0f + 0.25f * kq;
    const float lfoPitch = 12.0f * p[DP_LFO_PITCH] * p[DP_LFO_PITCH];
    const float lfoCut = 4.0f * p[DP_LFO_CUT] * p[DP_LFO_CUT];
    const bool pwmEnv = p[DP_PWM_SRC] >= 0.5f;
    const bool gateVca = p[DP_VCA] >= 0.5f;
    const float mixSaw = p[DP_SAW], mixPulse = p[DP_PULSE], mixSub = p[DP_SUB], mixNoise = p[DP_NOISE];
    /* glide: a one-pole on the pitch at control rate, ~99 % of the way in Glide's time */
    const float glideC = d6_one_minus_exp2(D6_TC_99 * (float)D6_CTRL / (clampf(p[DP_GLIDE], 0.001f, 30.0f) * D6_RATE));
    const float cutoff = clampf(p[DP_CUTOFF], 10.0f, 20000.0f);

    float phase = v->phase, sub = v->sub, env = v->env, gate = v->gate, hs = v->hs, hb = v->hb;
    float z[4] = { v->z[0], v->z[1], v->z[2], v->z[3] };
    float s0 = v->s[0], s1 = v->s[1], s2 = v->s[2], s3 = v->s[3];
    uint8_t stage = v->stage;
    for (uint32_t c0 = 0; c0 < D6_BLOCK; c0 += D6_CTRL) {
        const float lfo = d6_lfo(v, p, lfoHz);
        if (v->glideFrom)
            v->pitch += ((float)v->note - v->pitch) * glideC;
        else
            v->pitch = (float)v->note;
        const float cents = p[DP_FINE] + detC + vin * (4.0f * tol[0] + 3.0f * v->drift[0]) + v->noteOff[0];
        const float semis = v->pitch + 12.0f * (list_pick(p[DP_RANGE], 3) - 1.0f) + p[DP_TUNE]
                            + cents * 0.01f + lfoPitch * lfo + bend;
        const float dt = clampf(440.0f / D6_RATE * exp2f_fast((semis - 69.0f) * (1.0f / 12.0f)), 1e-6f, 0.45f);
        const float idt = 1.0f / dt;
        const float pwv = clampf(p[DP_PW] + p[DP_PWM] * (pwmEnv ? env : 0.5f * lfo) + 0.03f * vin * tol[4],
                                 0.0f, 1.0f);
        const float duty = 0.5f - 0.45f * pwv;
        /* cutoff: Hz, moved in octaves by the envelope, the LFO, the key and Vintage */
        const float oct = 8.0f * p[DP_ENV_AMT] * env + lfoCut * lfo
                          + p[DP_KEY] * (v->pitch - 60.0f) * (1.0f / 12.0f)
                          + vin * (0.15f * tol[1] + 0.08f * v->drift[1]) + v->noteOff[1];
        const float fc = clampf(cutoff * exp2f_fast(oct), 10.0f, 16000.0f);
        const float g = tan_pade(3.14159265f * fc / D6_RATE);
        const float G = g / (1.0f + g), G1 = 1.0f - G;
        const float G2 = G * G, G4 = G2 * G2;
        const float fbDen = 1.0f / (1.0f + kq * G4);
        for (uint32_t i = c0; i < c0 + D6_CTRL; i++) {
            /* envelope */
            if (stage == 1) {
                env += (1.3f - env) * ca;
                if (env >= 1.0f) {
                    env = 1.0f;
                    stage = 2;
                }
            } else if (stage == 2) {
                env += (sus - env) * cd;
            } else {
                env -= env * cr;
            }
            gate += ((v->gateOn ? 1.0f : 0.0f) - gate) * 0.0113f;
            const float amp = gateVca ? gate : env;
            /* DCO */
            /* naive saw, pulse and sub; each step gets the 4-point correction (z: n-2, n-1, n, n+1) */
            const float prev = phase;
            phase += dt;
            z[2] = z[3];
            z[3] = 0.0f;
            if (phase >= 1.0f) {
                phase -= 1.0f;
                sub = -sub;
                d6_blep4(z, phase * idt, 2.0f * (mixPulse - mixSaw + mixSub * sub));
                if (prev < duty)                            /* fell before the wrap */
                    d6_blep4(z, (phase + 1.0f - duty) * idt, -2.0f * mixPulse);
                else if (phase >= duty)                     /* fell after it (duty under dt) */
                    d6_blep4(z, (phase - duty) * idt, -2.0f * mixPulse);
            } else if (prev < duty && phase >= duty) {
                d6_blep4(z, (phase - duty) * idt, -2.0f * mixPulse);
            }
            z[2] += mixSaw * (2.0f * phase - 1.0f) + mixPulse * (phase < duty ? 1.0f : -1.0f) + mixSub * sub;
            float x = z[0] + mixNoise * d6_rand(v);
            z[0] = z[1];
            z[1] = z[2];
            /* Bass Boost, then the HPF (trapezoidal one-poles) */
            if (lowEnd) {
                if (boost) {
                    const float vb = (x - hb) * bG;
                    const float lb = vb + hb;
                    hb = lb + vb;
                    x += lb;
                }
                if (hpfOn) {
                    const float vh = (x - hs) * hG;
                    const float lp = vh + hs;
                    hs = lp + vh;
                    x -= lp;
                }
            }
            /* ladder */
            const float S = G1 * (G * G2 * s0 + G2 * s1 + G * s2 + s3);
            const float y4 = (G4 * x + S) * fbDen;
            float u = 2.0f * d6_clip(0.5f * (x - kq * y4));
            float vv = (u - s0) * G; u = vv + s0; s0 = u + vv;
            vv = (u - s1) * G; u = vv + s1; s1 = u + vv;
            vv = (u - s2) * G; u = vv + s2; s2 = u + vv;
            vv = (u - s3) * G; u = vv + s3; s3 = u + vv;
            const float y = u * comp * amp;
            L[i] += y * gl;
            R[i] += y * gr;
        }
    }
    if (stage == 3 && (gateVca ? gate < 1e-4f : env < 1e-4f))     /* Gate: silent once the gate closes */
        stage = 0;
    if (!(s0 == s0 && s3 == s3 && hs == hs && hb == hb)) {      /* NaN: start the filters again */
        s0 = s1 = s2 = s3 = hs = hb = 0.0f;
    }
    v->phase = phase; v->sub = sub; v->env = env; v->gate = gate; v->hs = hs; v->hb = hb;
    for (int k = 0; k < 4; k++)
        v->z[k] = z[k];
    v->s[0] = s0; v->s[1] = s1; v->s[2] = s2; v->s[3] = s3;
    v->stage = stage;
    if (!stage)
        v->env = 0.0f;
    if (p[DP_LFO_TRIG] >= 0.5f)
        d6_lfo_store(v, d6_lfo_key(p, lfoHz));
}

} // namespace mtsynth

#endif // MODS_SYNTHS_D6ENGINE_H_
