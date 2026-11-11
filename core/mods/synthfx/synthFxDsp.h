// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The synth effects' arithmetic (synthFx.h), as inline functions in mtfx::dsp, so the
// firmware (synthFx.cpp) and the Synth FX plugins of the default pack (synthfx-sdk/pack/)
// build the same code: a plugin gives the same output, bit for bit. Single precision, no
// library calls, no globals but constant tables: it runs in the audio interrupt, and in a
// plugin wherever it is loaded.

#ifndef MODS_SYNTHFX_SYNTHFXDSP_H_
#define MODS_SYNTHFX_SYNTHFXDSP_H_

#include "synthFx.h"

#include <cstdint>

/* The helpers run once per block, outside the sample loops. The firmware (synthFx.cpp) builds
 * them as plain functions, as it always has, so dist_block's copy in ITCM stays as small as it
 * was; a plugin may inline them. */
#ifndef MTFX_SETUP
#define MTFX_SETUP static inline
#endif

namespace mtfx
{
namespace dsp
{

/* ---- the Distortion ------------------------------------------------------------------- */

/* Overdrive, after the Tube Screamer: the op-amp's gain stage feeds a 720 Hz high-pass into
 * diodes in its feedback, so the clipped part rides on the clean signal and the bass stays
 * clean (the mid hump). Distortion, after the DS-1: high gain into diodes to ground, a hard
 * clip of the whole signal, then the DS-1's tone stack, a low-pass and a high-pass blended
 * by Tone, which scoops the mids near the middle. Both clippers use first-order
 * antiderivative anti-aliasing (the clip of the line between two samples, averaged) and are
 * oversampled with polyphase IIR half-band filters: Overdrive at 2x (88.2 kHz), through one
 * of 8 all-passes, its images and aliases over 100 dB down and flat to about 20 kHz;
 * Distortion, whose hard clip at up to +50 dB makes far more harmonics, at 4x (176.4 kHz),
 * through that one and then one of 3 all-passes (76 dB or more on what would fold onto
 * 0-24 kHz; the outer one takes the rest; 4 until 2026-11-11, the same rejection).
 *
 * Bit Crusher: a sample-and-hold at Rate, each held sample rounded to Bits (2^(Bits - 1)
 * steps from 0 to 1, rounded to the nearest, so silence stays silent). No anti-aliasing:
 * the aliasing is the sound. Bits is continuous, so a macro sweeps it without jumps of a
 * whole bit; it is set once per block. */
#define OD_HP               0.950002f       /* exp(-2 pi 720 Hz / 88.2 kHz): at 2x */
#define DS_DC               0.998932f       /* exp(-2 pi 30 Hz / 176.4 kHz): the DS-1's coupling, at 4x */
#define OD_GAIN_MIN         2.0f            /* Gain 0: +6 dB */
#define OD_GAIN_OCT         5.8826430f      /* log2(118 / 2): Gain 1, +41.4 dB (the pedal's top) */
#define DS_GAIN_MIN         3.1622777f      /* +10 dB */
#define DS_GAIN_OCT         6.6438562f      /* log2(100): Gain 1, +50 dB */
#define CLIP_LEVEL          0.5f            /* the diodes' level against a held note's peak (about 0.35) */
#define OD_TONE_LO          700.0f          /* Overdrive Tone 0: the treble cut's corner, Hz */
#define OD_TONE_OCT         3.0995356f      /* log2(6000 / 700): Tone 1, 6 kHz */
#define DS_TONE_LP          0.03500f        /* 1 - exp(-2 pi 250 Hz / 44.1 kHz): the stack's low-pass */
#define DS_TONE_HP          0.84330f        /* exp(-2 pi 1200 Hz / 44.1 kHz): its high-pass */
#define ADAA_EPS            1.0e-5f
#define DB_TO_OCT           0.16609640f     /* 1 / 6.0206: dB to octaves of amplitude */
/* Each clipper's fixed output trim: at Level 0 dB and the default Gain (50 %) a held Init note
 * (note 60, WAVE and PHASE) is as loud as with the Distortion Off, within 1 dB. A constant per
 * type, not a make-up that follows Gain: more Gain still gets louder until the clip holds it.
 * Measured in tools/emu/test_synths_dsp.py (2026-11-09): Overdrive +10.9 dB, Distortion +8.5 dB
 * there before the trim. Bit Crusher needs none. */
#define OD_TRIM             0.28510f        /* -10.9 dB */
#define DS_TRIM             0.37584f        /* -8.5 dB */
#define CRUSH_BITS_MIN      4.0f            /* Gain 0: 4 bits (fewer round a normal note to silence) */
#define CRUSH_BITS_SPAN     12.0f           /* Gain 1: 16 bits */
#define CRUSH_HZ_MIN        1000.0f
#define CRUSH_HZ_OCT        5.4627571f      /* log2(44100 / 1000): Tone 1, 44.1 kHz */
#define CRUSH_IN            2.0f            /* the input's clamp: FX_OUT_LIMIT */
/* The Distortion runs a chunk of the block at a time, one stage after another (up, up, clip,
 * down, down), so one filter's states are live in a loop instead of all four. The arithmetic,
 * and its order for each value, is the same: the output is the same bit for bit. It saved 9 %
 * of the Distortion's instructions (44,220 to 40,248 a block, 2026-11-09). Overdrive, with two
 * filters, stays sample by sample: staged it cost 8 % more. Scratch: 6 x 32 floats on the
 * stack. */
#define DIST_CHUNK          32u

/* Overdrive's soft clip, f(u) = 1.5 (u - u^3 / 3), +-1 from |u| = 1, and its antiderivative */
static inline float od_f(float u)
{
    if (u >= 1.0f)
        return 1.0f;
    if (u <= -1.0f)
        return -1.0f;
    return 1.5f * u - 0.5f * u * u * u;
}

static inline float od_F(float u)
{
    float a = u < 0.0f ? -u : u;
    if (a >= 1.0f)
        return a - 0.375f;                  /* 0.625 at 1, then slope 1 */
    float u2 = u * u;
    return 0.75f * u2 - 0.125f * u2 * u2;
}

/* Distortion's hard clip and its antiderivative */
static inline float ds_f(float u)
{
    return u > 1.0f ? 1.0f : (u < -1.0f ? -1.0f : u);
}

static inline float ds_F(float u)
{
    float a = u < 0.0f ? -u : u;
    return a >= 1.0f ? a - 0.5f : 0.5f * u * u;
}

/* The anti-aliased clips: (F(u) - F(u1)) / (u - u1), the clip averaged over the line from
 * u1 to u. While both ends are on one side of a knee that has a closed form, so the
 * division is only needed while the signal crosses one. */
static inline float od_adaa(float u, float u1)
{
    if (u >= 1.0f && u1 >= 1.0f)
        return 1.0f;
    if (u <= -1.0f && u1 <= -1.0f)
        return -1.0f;
    float s = u + u1;
    if (u < 1.0f && u > -1.0f && u1 < 1.0f && u1 > -1.0f)
        return s * (0.75f - 0.125f * (u * u + u1 * u1));
    float du = u - u1;
    return (du > ADAA_EPS || du < -ADAA_EPS) ? (od_F(u) - od_F(u1)) / du : od_f(0.5f * s);
}

static inline float ds_adaa(float u, float u1)
{
    if (u >= 1.0f && u1 >= 1.0f)
        return 1.0f;
    if (u <= -1.0f && u1 <= -1.0f)
        return -1.0f;
    float s = u + u1;
    if (u < 1.0f && u > -1.0f && u1 < 1.0f && u1 > -1.0f)
        return 0.5f * s;
    float du = u - u1;
    return (du > ADAA_EPS || du < -ADAA_EPS) ? (ds_F(u) - ds_F(u1)) / du : ds_f(0.5f * s);
}

MTFX_SETUP float crush_bits(float v)
{
    return CRUSH_BITS_MIN + CRUSH_BITS_SPAN * clampf(v, 0.0f, 1.0f);
}

MTFX_SETUP float crush_hz(float v)
{
    return CRUSH_HZ_MIN * exp2f_fast(clampf(v, 0.0f, 1.0f) * CRUSH_HZ_OCT);
}

/* The half-band filters' all-pass coefficients (de Soras's polyphase IIR design;
 * tools/synths/halfband.py works them out and checks the responses). Even ones are the
 * first path, odd ones the second. hb_c: 8, transition 0.04; hb2_c: 3, transition 0.2. */
static const float hb_c[HB_COEFS] = {
    0.0406334609f, 0.1505051290f, 0.3007570560f, 0.4607745050f,
    0.6095243149f, 0.7385038411f, 0.8492238104f, 0.9497427837f,
};
static const float hb2_c[HB2_COEFS] = {
    0.0814300232f, 0.3156598402f, 0.7097708001f,
};

/* One path's state in registers: s[0] its input one step back, s[k] its k-th all-pass's output
 * one step back (all-passes first, first + 2, ... of the filter). Each all-pass: y = c (x - y1)
 * + x1, where x1, its input one step back, is the previous all-pass's output one step back.
 * A step reads one state and writes the other, so two steps (s to t, then t to s) leave it
 * where it was with no copies between them: the copies were 28 % of the Distortion's
 * instructions (2026-11-11). The arithmetic and its order are as before, bit for bit. */
template <uint32_t M>                       /* M: the all-passes on the path */
struct hb_path_regs
{
    float c[M];
    float s[M + 1u], t[M + 1u];
};

template <uint32_t M>
static inline __attribute__((always_inline)) float hb_step(const float *c, const float *s, float *t, float x)
{
    t[0] = x;
#pragma GCC unroll 8
    for (uint32_t k = 1; k <= M; k++)
        t[k] = c[k - 1u] * (t[k - 1u] - s[k]) + s[k - 1u];
    return t[M];
}

/* A filter's two paths, loaded from and stored back to its state. With N odd the first path
 * has one all-pass more. */
template <uint32_t N>
struct hb_regs
{
    static constexpr uint32_t M0 = (N + 1u) / 2u, M1 = N / 2u;
    hb_path_regs<M0> p0;
    hb_path_regs<M1> p1;
};

template <uint32_t M>
static inline __attribute__((always_inline)) void hb_path_load(hb_path_regs<M> *p, float in1, const float *y1,
                                                               const float *c)
{
    p->s[0] = in1;
#pragma GCC unroll 8
    for (uint32_t k = 0; k < M; k++) {
        p->c[k] = c[2u * k];
        p->s[k + 1u] = y1[2u * k];
    }
}

template <uint32_t M>
static inline __attribute__((always_inline)) void hb_path_store(const hb_path_regs<M> *p, float *in1, float *y1)
{
    *in1 = p->s[0];
#pragma GCC unroll 8
    for (uint32_t k = 0; k < M; k++)
        y1[2u * k] = p->s[k + 1u];
}

template <uint32_t N>
static inline __attribute__((always_inline)) void hb_load(hb_regs<N> *r, const mt_hb<N> *h, const float *c)
{
    hb_path_load(&r->p0, h->in1[0], &h->y1[0], &c[0]);
    hb_path_load(&r->p1, h->in1[1], &h->y1[1], &c[1]);
}

template <uint32_t N>
static inline __attribute__((always_inline)) void hb_store(const hb_regs<N> *r, mt_hb<N> *h)
{
    hb_path_store(&r->p0, &h->in1[0], &h->y1[0]);
    hb_path_store(&r->p1, &h->in1[1], &h->y1[1]);
}

/* 2x up over two samples in: four out (each sample's first path first). The state ends in s. */
template <uint32_t N>
static inline __attribute__((always_inline)) void hb_up2(hb_regs<N> *r, float x0, float x1, float *out)
{
    constexpr uint32_t M0 = hb_regs<N>::M0, M1 = hb_regs<N>::M1;
    out[0] = hb_step<M0>(r->p0.c, r->p0.s, r->p0.t, x0);
    out[1] = hb_step<M1>(r->p1.c, r->p1.s, r->p1.t, x0);
    out[2] = hb_step<M0>(r->p0.c, r->p0.t, r->p0.s, x1);
    out[3] = hb_step<M1>(r->p1.c, r->p1.t, r->p1.s, x1);
}

/* 2x down over four samples in (in order): two out. The state ends in s. */
template <uint32_t N>
static inline __attribute__((always_inline)) void hb_down2(hb_regs<N> *r, const float *in, float *y0, float *y1)
{
    constexpr uint32_t M0 = hb_regs<N>::M0, M1 = hb_regs<N>::M1;
    float b0 = hb_step<M0>(r->p0.c, r->p0.s, r->p0.t, in[1]);
    *y0 = 0.5f * (b0 + hb_step<M1>(r->p1.c, r->p1.s, r->p1.t, in[0]));
    float b1 = hb_step<M0>(r->p0.c, r->p0.t, r->p0.s, in[3]);
    *y1 = 0.5f * (b1 + hb_step<M1>(r->p1.c, r->p1.t, r->p1.s, in[2]));
}

template <uint32_t N>
static inline void hb_clear(mt_hb<N> *h)
{
    h->in1[0] = h->in1[1] = 0.0f;
    for (uint32_t i = 0; i < N; i++)
        h->y1[i] = 0.0f;
}

static inline void dist_clear(struct mt_dist *d)
{
    d->x1 = d->hp = d->u1 = d->lp = d->hp2 = d->v1 = 0.0f;
    hb_clear(&d->up);
    hb_clear(&d->down);
    hb_clear(&d->up2);
    hb_clear(&d->down2);
}

/* x rounded to the nearest whole number (|x| under 2^30; no library call) */
static inline float round_near(float x)
{
    return (float)(int32_t)(x + (x < 0.0f ? -0.5f : 0.5f));
}

/* Level, with the type's trim */
MTFX_SETUP float dist_level(uint32_t type, const float *fx)
{
    float trim = type == DIST_OVERDRIVE ? OD_TRIM : type == DIST_DISTORTION ? DS_TRIM : 1.0f;
    return trim * exp2f_fast(clampf(fx[FXP_LEVEL], -30.0f, 6.0f) * DB_TO_OCT);
}

MTFX_SETUP float dist_gain(uint32_t type, float g)
{
    g = clampf(g, 0.0f, 1.0f);
    if (type == DIST_CRUSHER)
        return exp2f_fast(1.0f - crush_bits(g));     /* the step: 2^(1 - Bits) */
    return type == DIST_OVERDRIVE ? OD_GAIN_MIN * exp2f_fast(g * OD_GAIN_OCT)
                                  : DS_GAIN_MIN * exp2f_fast(g * DS_GAIN_OCT);
}

MTFX_SETUP void dist_reset(struct mt_dist *d, const float *fx, int on)
{
    uint32_t type = list_pick(fx[FXP_DIST], DIST_TYPES);
    dist_clear(d);
    if (type == DIST_CRUSHER)
        d->hp = 1.0f;
    d->type = type;
    d->gain = dist_gain(type, fx[FXP_GAIN]);
    d->level = dist_level(type, fx);
    d->amount = (on && type != DIST_OFF) ? 1.0f : 0.0f;
}

/* Fixed: DIST_TYPES for any type (the firmware), or the one type a plugin runs, so only that
 * type's code is built (the default pack's Overdrive, Distortion and Bit Crusher). With a
 * fixed type, fx[FXP_DIST] and d->type must be it (dist_reset with it sets d->type). */
template <uint32_t Fixed = DIST_TYPES>
static inline __attribute__((always_inline)) void dist_block(struct mt_dist *d, float *buf, const float *fx, int want)
{
    uint32_t type = Fixed < DIST_TYPES ? Fixed : list_pick(fx[FXP_DIST], DIST_TYPES);
    if (type == DIST_OFF)
        want = 0;
    if (d->amount <= 0.0f) {
        if (!want)
            return;                         /* faded out: nothing to do */
        dist_clear(d);
        if (type == DIST_CRUSHER)
            d->hp = 1.0f;                   /* take the first sample at once */
        d->type = type;
        d->gain = dist_gain(type, fx[FXP_GAIN]);
        d->level = dist_level(type, fx);
    }
    const uint32_t dtype = Fixed < DIST_TYPES ? Fixed : d->type;
    if (type != dtype)                      /* another type: out first, then in */
        want = 0;
    float a0 = d->amount;
    float a1 = want ? (a0 + FX_STEP > 1.0f ? 1.0f : a0 + FX_STEP) : (a0 - FX_STEP < 0.0f ? 0.0f : a0 - FX_STEP);
    float g0 = d->gain, g1 = dist_gain(dtype, fx[FXP_GAIN]);
    float l0 = d->level, l1 = dist_level(dtype, fx);
    const float inv = 1.0f / FX_BLOCK;
    float da = (a1 - a0) * inv, dg = (g1 - g0) * inv, dl = (l1 - l0) * inv;
    float am = a0, g = g0, lv = l0;
    float tone = clampf(fx[FXP_TONE], 0.0f, 1.0f);
    float x1 = d->x1, hp = d->hp, u1 = d->u1, lp = d->lp, hp2 = d->hp2, v1 = d->v1;
    if (dtype == DIST_OVERDRIVE) {
        /* the treble cut: a one-pole low-pass, 700 Hz to 6 kHz */
        float fc = OD_TONE_LO * exp2f_fast(tone * OD_TONE_OCT);
        float c = 1.0f - exp2f_fast(-6.2831853f * fc / FX_RATE * 1.4426950f);
        hb_regs<HB_COEFS> up, down;                     /* held in registers for the block */
        hb_load(&up, &d->up, hb_c);
        hb_load(&down, &d->down, hb_c);
        for (uint32_t n = 0; n < FX_BLOCK; n += 2u) {   /* two samples a pass (hb_regs) */
            float xs[2] = { clampf(buf[n], -4.0f, 4.0f), clampf(buf[n + 1u], -4.0f, 4.0f) };
            float s[4], ys[4];
            hb_up2(&up, xs[0], xs[1], s);
#pragma GCC unroll 2
            for (uint32_t j = 0; j < 2u; j++) {
#pragma GCC unroll 2
                for (uint32_t k = 2u * j; k < 2u * j + 2u; k++) {     /* at 2x */
                    hp = OD_HP * (hp + s[k] - x1);
                    float u = g * hp;
                    ys[k] = 0.5f * (s[k] + x1) + CLIP_LEVEL * od_adaa(u, u1);  /* the clean signal, aligned with the clip's half sample */
                    x1 = s[k];
                    u1 = u;
                }
                g += dg;
            }
            float y[2];
            hb_down2(&down, ys, &y[0], &y[1]);
#pragma GCC unroll 2
            for (uint32_t j = 0; j < 2u; j++) {
                lp += c * (y[j] - lp);
                buf[n + j] = limit(xs[j] + am * (lp * lv - xs[j]));
                am += da;
                lv += dl;
            }
        }
        hb_store(&up, &d->up);
        hb_store(&down, &d->down);
    } else if (dtype == DIST_CRUSHER) {
        float q = g1, iq = 1.0f / g1;                  /* the step, set per block */
        float r = crush_hz(tone) * (1.0f / FX_RATE);    /* the hold's phase per sample, up to 1 */
        for (uint32_t n = 0; n < FX_BLOCK; n++) {
            float x = clampf(buf[n], -CRUSH_IN, CRUSH_IN);
            hp += r;
            if (hp >= 1.0f) {                           /* a new sample */
                hp -= 1.0f;
                lp = q * round_near(x * iq);
            }
            buf[n] = limit(x + am * (lp * lv - x));
            x1 = x;
            am += da;
            lv += dl;
        }
    } else {
        hb_regs<HB_COEFS> up, down;
        hb_regs<HB2_COEFS> up2, down2;
        hb_load(&up, &d->up, hb_c);
        hb_load(&down, &d->down, hb_c);
        hb_load(&up2, &d->up2, hb2_c);
        hb_load(&down2, &d->down2, hb2_c);
        for (uint32_t c0 = 0; c0 < FX_BLOCK; c0 += DIST_CHUNK) {
            float* in = buf + c0;
            float s2[2u * DIST_CHUNK], s4[4u * DIST_CHUNK];
            for (uint32_t n = 0; n < DIST_CHUNK; n += 2u)   /* up to 2x */
                hb_up2(&up, clampf(in[n], -4.0f, 4.0f), clampf(in[n + 1u], -4.0f, 4.0f), &s2[2u * n]);
            for (uint32_t m = 0; m < 2u * DIST_CHUNK; m += 2u)  /* up to 4x */
                hb_up2(&up2, s2[m], s2[m + 1u], &s4[2u * m]);
            for (uint32_t n = 0; n < DIST_CHUNK; n++) {     /* the clip, at 4x */
                for (uint32_t j = 0; j < 4u; j++) {
                    float q = s4[4u * n + j];
                    hp = DS_DC * (hp + q - x1);
                    float u = g * hp;
                    s4[4u * n + j] = ds_adaa(u, u1);
                    x1 = q;
                    u1 = u;
                }
                g += dg;
            }
            for (uint32_t m = 0; m < 2u * DIST_CHUNK; m += 2u)  /* down to 2x */
                hb_down2(&down2, &s4[2u * m], &s2[m], &s2[m + 1u]);
            float* w = s4;                                  /* free again: the filter's out, then the rest */
            for (uint32_t n = 0; n < DIST_CHUNK; n += 2u)   /* down to 1x */
                hb_down2(&down, &s2[2u * n], &w[n], &w[n + 1u]);
            for (uint32_t n = 0; n < DIST_CHUNK; n++) {     /* the tone stack, the mix */
                float v = CLIP_LEVEL * w[n];
                lp += DS_TONE_LP * (v - lp);                /* the stack: low-pass ... */
                hp2 = DS_TONE_HP * (hp2 + v - v1);          /* ... and high-pass, blended */
                float y = lp + tone * (hp2 - lp);
                float x = clampf(in[n], -4.0f, 4.0f);
                in[n] = limit(x + am * (y * lv * 2.0f - x)); /* x 2: the stack's loss in the middle */
                v1 = v;
                am += da;
                lv += dl;
            }
        }
        hb_store(&up, &d->up);
        hb_store(&down, &d->down);
        hb_store(&up2, &d->up2);
        hb_store(&down2, &d->down2);
    }
    d->x1 = x1;
    d->hp = hp;
    d->u1 = u1;
    d->lp = lp;
    d->hp2 = hp2;
    d->v1 = v1;
    d->gain = g1;
    d->level = l1;
    d->amount = a1;
}

/* ---- the modulation effects ------------------------------------------------------------- */

/* Chorus, after the Roland Juno's (sy9's): one delay line, read by two taps that a triangle
 * LFO moves in opposite directions, one for each side, around 3.5 ms; Depth 100 % swings
 * them +-1.85 ms. Each tap is read with linear interpolation and goes through a one-pole
 * low-pass at 9 kHz. Mix crossfades dry and wet keeping the power: each is scaled by
 * 1 / sqrt((1 - Mix)^2 + Mix^2).
 *
 * Flanger: two delay lines, one per side, each read at 0.25 ms plus up to Depth x 6 ms, the
 * sides swept in opposite directions, with Feedback from each tap into its own line.
 *
 * Phaser: four first-order all-passes per side, their corner swept from 200 Hz up by
 * Depth x 4 octaves (the sides in opposite directions), with Feedback from the last into
 * the first. With the dry signal at Mix 50 % that gives two notches.
 *
 * Mix and Depth move at most FX_STEP per block; switching the effect fades the old one
 * out and the new one in. */
#define CHORUS_CENTRE       (3.5e-3f * FX_RATE)
#define CHORUS_SWING        (1.85e-3f * FX_RATE)    /* at Depth 100 % */
#define CHORUS_LP           0.7226f         /* 1 - exp(-2 pi 9 kHz / 44.1 kHz) */
#define RATE_MIN            0.05f           /* Rate 0: 0.05 Hz; 1: 5 Hz (x 100) */
#define RATE_OCTAVES        6.6438562f      /* log2(100) */
#define FLANGE_MIN          (0.25e-3f * FX_RATE)    /* 11 samples */
#define FLANGE_SPAN         (6.0e-3f * FX_RATE)     /* at Depth 100 % */
#define PHASER_LO           200.0f
#define PHASER_OCT          4.0f
#define PHASER_STAGES       4u
#define PHASER_CHUNK        8u              /* the all-passes' coefficients: worked out every 8 samples, ramped */
#define FEEDBACK_MAX        0.95f

static inline float fx_sqrt(float x)
{
    return fsqrt(x);
}

/* Mix m -> the dry and wet gains */
static inline void fx_gains(float m, float *dry, float *wet)
{
    float k = 1.0f / fx_sqrt((1.0f - m) * (1.0f - m) + m * m);
    *dry = (1.0f - m) * k;
    *wet = m * k;
}

MTFX_SETUP float rate_hz(float v)
{
    return RATE_MIN * exp2f_fast(clampf(v, 0.0f, 1.0f) * RATE_OCTAVES);
}

MTFX_SETUP void fx_start(struct mt_fx *s, const float *fx, uint32_t type)
{
    for (uint32_t n = 0; n < FX_LEN; n++) {
        s->buf[n] = 0.0f;
        s->buf2[n] = 0.0f;
    }
    s->pos = 0;
    s->lp[0] = s->lp[1] = 0.0f;
    for (uint32_t k = 0; k < PHASER_STAGES; k++)
        s->ap[0][k] = s->ap[1][k] = 0.0f;
    s->fb[0] = s->fb[1] = 0.0f;
    s->mix = 0.0f;
    s->depth = clampf(fx[FXP_DEPTH], 0.0f, 1.0f);
    s->active = type;
    s->running = 1;
}

static inline float step_to(float from, float to)
{
    return to > from + FX_STEP ? from + FX_STEP : to < from - FX_STEP ? from - FX_STEP : to;
}

static inline float tap(const float *buf, uint32_t pos, float d)
{
    float at = (float)(pos + FX_LEN) - d;   /* d is 1..FX_LEN - 2 samples back */
    uint32_t i = (uint32_t)at;
    float f = at - (float)i;
    float a = buf[i & (FX_LEN - 1u)], b = buf[(i + 1u) & (FX_LEN - 1u)];
    return a + f * (b - a);
}

/* An all-pass's coefficient for a corner of fc Hz */
static inline float ap_coef(float fc)
{
    float t = tan_pade(3.14159265f * fc / FX_RATE);
    return (t - 1.0f) / (t + 1.0f);
}

/* The modulation effect running (s->active; type: the one chosen) over a block. Mono: L in,
 * L and R out. Stereo: L and R in and out, the effect fed (L + R) / 2. The mono form is
 * fx_block's arithmetic as it was; compiled with the stereo one beside it, the last bit of
 * some samples differs while an effect switch fades the Mix (2.4e-7 at most, 2026-11-09). */
template <bool Stereo>
static inline __attribute__((always_inline)) void fx_mod(struct mt_fx *s, const float *fx, uint32_t type,
                                                         float *L, float *R)
{
    float want = type == s->active ? clampf(fx[FXP_MIX], 0.0f, 1.0f) : 0.0f;
    float m0 = s->mix, m1 = step_to(m0, want);
    float d0 = s->depth, d1 = step_to(d0, clampf(fx[FXP_DEPTH], 0.0f, 1.0f));
    float g0, w0, g1, w1;
    fx_gains(m0, &g0, &w0);
    fx_gains(m1, &g1, &w1);
    float dg = (g1 - g0) * (1.0f / FX_BLOCK), dw = (w1 - w0) * (1.0f / FX_BLOCK);
    float gd = g0, gw = w0;
    float inc = rate_hz(fx[FXP_RATE]) * (1.0f / FX_RATE), ph = s->phase;
    uint32_t pos = s->pos;
    float fb = clampf(fx[FXP_FEEDBACK], -FEEDBACK_MAX, FEEDBACK_MAX);
    if (s->active == FX_CHORUS) {
        float dd = (d1 - d0) * (CHORUS_SWING / FX_BLOCK);
        float swing = d0 * CHORUS_SWING;
        float lpl = s->lp[0], lpr = s->lp[1];
        for (uint32_t n = 0; n < FX_BLOCK; n++) {
            float x = L[n], xr = Stereo ? R[n] : x;
            s->buf[pos] = Stereo ? 0.5f * (x + xr) : x;
            float tri = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
            ph += inc;
            if (ph >= 1.0f)
                ph -= 1.0f;
            float d = swing * tri;
            lpl += CHORUS_LP * (tap(s->buf, pos, CHORUS_CENTRE + d) - lpl);
            lpr += CHORUS_LP * (tap(s->buf, pos, CHORUS_CENTRE - d) - lpr);
            L[n] = gd * x + gw * lpl;
            R[n] = gd * xr + gw * lpr;
            pos = (pos + 1u) & (FX_LEN - 1u);
            gd += dg;
            gw += dw;
            swing += dd;
        }
        s->lp[0] = lpl;
        s->lp[1] = lpr;
    } else if (s->active == FX_FLANGER) {
        float dd = (d1 - d0) * (0.5f * FLANGE_SPAN / FX_BLOCK);
        float half = d0 * 0.5f * FLANGE_SPAN;
        for (uint32_t n = 0; n < FX_BLOCK; n++) {
            float x = L[n], xr = Stereo ? R[n] : x, in = Stereo ? 0.5f * (x + xr) : x;
            float tri = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
            ph += inc;
            if (ph >= 1.0f)
                ph -= 1.0f;
            float tl = tap(s->buf, pos, FLANGE_MIN + half * (1.0f + tri));
            float tr = tap(s->buf2, pos, FLANGE_MIN + half * (1.0f - tri));
            s->buf[pos] = limit(in + fb * tl);
            s->buf2[pos] = limit(in + fb * tr);
            L[n] = gd * x + gw * tl;
            R[n] = gd * xr + gw * tr;
            pos = (pos + 1u) & (FX_LEN - 1u);
            gd += dg;
            gw += dw;
            half += dd;
        }
    } else {                                /* the Phaser */
        float oct0 = d0 * PHASER_OCT, doct = (d1 - d0) * (PHASER_OCT / FX_BLOCK);
        /* the all-passes' states in registers for the block: through s->ap, each L[n] store
         * could alias them, so they were loaded and stored every sample (in SDRAM) */
        float ql[PHASER_STAGES], qr[PHASER_STAGES];
#pragma GCC unroll 4
        for (uint32_t k = 0; k < PHASER_STAGES; k++) {
            ql[k] = s->ap[0][k];
            qr[k] = s->ap[1][k];
        }
        float yl = s->fb[0], yr = s->fb[1];
        float tri = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
        float cl = ap_coef(PHASER_LO * exp2f_fast(oct0 * 0.5f * (1.0f + tri)));
        float cr = ap_coef(PHASER_LO * exp2f_fast(oct0 * 0.5f * (1.0f - tri)));
        for (uint32_t c = 0; c < FX_BLOCK; c += PHASER_CHUNK) {
            float phe = ph + inc * (float)PHASER_CHUNK;
            if (phe >= 1.0f)
                phe -= 1.0f;
            float oct = oct0 + doct * (float)(c + PHASER_CHUNK);
            float te = phe < 0.5f ? 4.0f * phe - 1.0f : 3.0f - 4.0f * phe;
            float el = ap_coef(PHASER_LO * exp2f_fast(oct * 0.5f * (1.0f + te)));
            float er = ap_coef(PHASER_LO * exp2f_fast(oct * 0.5f * (1.0f - te)));
            float sl = (el - cl) * (1.0f / PHASER_CHUNK), sr = (er - cr) * (1.0f / PHASER_CHUNK);
            for (uint32_t n = c; n < c + PHASER_CHUNK; n++) {
                float x = L[n], xr = Stereo ? R[n] : x, in = Stereo ? 0.5f * (x + xr) : x;
                float ul = limit(in + fb * yl), ur = limit(in + fb * yr);
#pragma GCC unroll 4
                for (uint32_t k = 0; k < PHASER_STAGES; k++) {
                    float a = cl * ul + ql[k];
                    ql[k] = ul - cl * a;
                    ul = a;
                    float b = cr * ur + qr[k];
                    qr[k] = ur - cr * b;
                    ur = b;
                }
                yl = ul;
                yr = ur;
                L[n] = gd * x + gw * yl;
                R[n] = gd * xr + gw * yr;
                gd += dg;
                gw += dw;
                cl += sl;
                cr += sr;
            }
            ph = phe;
            cl = el;
            cr = er;
        }
        s->fb[0] = yl;
        s->fb[1] = yr;
#pragma GCC unroll 4
        for (uint32_t k = 0; k < PHASER_STAGES; k++) {
            s->ap[0][k] = ql[k];
            s->ap[1][k] = qr[k];
        }
    }
    s->pos = pos;
    s->phase = ph;
    s->mix = m1;
    s->depth = d1;
    if (type != s->active && m1 <= 0.0f)   /* faded out: the next block starts the new one, or stops */
        s->running = 0;
}

} // namespace dsp
} // namespace mtfx

#endif // MODS_SYNTHFX_SYNTHFXDSP_H_
