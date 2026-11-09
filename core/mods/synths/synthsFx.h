// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// WAVE's and PHASE's effects: a Distortion, then one modulation effect (Chorus, Flanger or
// Phaser), on the instrument's summed output. The Distortion can also run in each WAVE
// voice before its filter (Placement Pre). The Chorus is sy9's (Modular-Trackular's
// patch/synths/wt_fx.h), unchanged, so with the Distortion Off it plays as sy9 did.
//
// Parameters (the engines' ids 62-71, read from the instrument's first voice):
//   62 Effect      None, Chorus, Flanger, Phaser
//   63 Rate        0..1: 0.05-5 Hz
//   64 Depth       0..1
//   65 Mix         0..1: dry to wet; 50 % equal (the deepest notches for Flanger and Phaser)
//   66 Feedback    -0.95..0.95 (Flanger and Phaser)
//   67 Distortion  Off, Overdrive, Distortion, Bit Crusher
//   68 Gain        0..1 (Bit Crusher: Bits, 4-16)
//   69 Tone        0..1 (Bit Crusher: Rate, 1-44.1 kHz)
//   70 Placement   Pre, Post (WAVE: before or after its filter; PHASE: no effect)
//   71 Level       -30..+6 dB
//
// Everything here runs in the audio interrupt: no heap, no SD, single precision.

#ifndef MODS_SYNTHS_SYNTHSFX_H_
#define MODS_SYNTHS_SYNTHSFX_H_

#include "synthsDsp.h"

#include <cstdint>

namespace mtsynth
{

enum : uint32_t
{
    P_EFFECT = 62,
    P_RATE,
    P_DEPTH,
    P_MIX,
    P_FEEDBACK,
    P_DIST,
    P_GAIN,
    P_TONE,
    P_PLACEMENT,
    P_LEVEL,
    P_COUNT // 72: every engine parameter
};

enum : uint32_t { FX_NONE, FX_CHORUS, FX_FLANGER, FX_PHASER, FX_TYPES };
enum : uint32_t { DIST_OFF, DIST_OVERDRIVE, DIST_DISTORTION, DIST_CRUSHER, DIST_TYPES };
enum : uint32_t { PLACE_PRE, PLACE_POST };

#define FX_LEN              512u            /* a delay line: 11.6 ms (a power of two) */
#define FX_STEP             0.05f           /* Mix, Depth and the Distortion's fade: the most they move per block */

/* A 2x half-band filter's state (the clippers' oversampling): each path's input one step
 * back, and each all-pass's output one step back (which is also the next all-pass's input).
 * HB_COEFS: 44.1 to 88.2 kHz and back; HB2_COEFS: 88.2 to 176.4 kHz (Distortion only). */
#define HB_COEFS            8u
#define HB2_COEFS           4u
template <uint32_t N>
struct mt_hb
{
    float in1[2];
    float y1[N];
};

/* One Distortion's running state: a voice's (Pre) or an instrument's (Post) */
struct mt_dist
{
    float x1;                               /* the input one sample back (Overdrive, Distortion: at 2x) */
    float hp;                               /* the high-pass into the clipper (Bit Crusher: the rate's phase) */
    float u1;                               /* the clipper's input one sample back (anti-aliasing) */
    float lp;                               /* Tone: the low-pass (Bit Crusher: the sample held) */
    float hp2;                              /* Tone (Distortion): the high-pass's state */
    float v1;                               /* ... and its input one sample back */
    float gain;                             /* the clipper's gain where the last block ended (Bit Crusher: the step) */
    float level;                            /* the output level where the last block ended */
    float amount;                           /* 0 dry .. 1 the distortion alone */
    uint32_t type;                          /* the type running */
    mt_hb<HB_COEFS> up, down;               /* Overdrive at 2x, Distortion at 4x: the first stage */
    mt_hb<HB2_COEFS> up2, down2;            /* Distortion: the second */
};

/* One block of a Distortion over buf (mono, in place). want: this place's Distortion is
 * on. Fades in and out over about 60 ms; while faded out it costs nothing. */
void dist_block(struct mt_dist *d, float *buf, const float *p, int want);

/* A Distortion from rest: its states cleared, at full if `on` (no fade in), else off. For
 * a voice whose note starts from idle, so a Pre Distortion is there from the first sample. */
void dist_reset(struct mt_dist *d, const float *p, int on);

/* An instrument's effects */
struct mt_fx
{
    uint32_t running;                       /* the modulation effect is in use */
    uint32_t active;                        /* ... which one */
    uint32_t pos;                           /* where the next sample goes */
    float phase;                            /* the LFO, 0..1 */
    float lp[2];                            /* the Chorus's taps' low-passes */
    float mix, depth;                       /* where the last block ended */
    float ap[2][4];                         /* the Phaser's all-passes, each side */
    float fb[2];                            /* the Phaser's last outputs (its feedback) */
    struct mt_dist dist;                    /* the Distortion, Post */
    /* the delay lines last: a new owner clears everything before them */
    float buf[FX_LEN];                      /* Chorus, Flanger L */
    float buf2[FX_LEN];                     /* Flanger R */
};

/* One block of an instrument's effects: L in (mono), L and R out. distWanted: the Post
 * Distortion is on (WAVE: Placement Post; PHASE: whenever it is on). */
void fx_block(struct mt_fx *s, const float *p, float *L, float *R, int distWanted);

/* The effect state of an instrument whose first voice is `voice`; nullptr if every state
 * is taken (the output is then left dry, R a copy of L). Audio interrupt only. */
struct mt_fx *fx_claim(const void *voice);
/* A voice is going: its state is free again. */
void fx_release(const void *voice);

/* Rate 0..1 in Hz (0.05-5 Hz), for the value display too */
float rate_hz(float v);

/* The Bit Crusher's Bits (Gain 0..1: 4-16) and Rate (Tone 0..1: 1-44.1 kHz, Hz), for the
 * value display too */
float crush_bits(float v);
float crush_hz(float v);

/* The Distortion's on in a place */
static inline int dist_on(const float *p)
{
    return list_pick(p[P_DIST], DIST_TYPES) != DIST_OFF;
}

static inline int dist_pre(const float *p)
{
    return dist_on(p) && list_pick(p[P_PLACEMENT], 2) == PLACE_PRE;
}

} // namespace mtsynth

#endif // MODS_SYNTHS_SYNTHSFX_H_
