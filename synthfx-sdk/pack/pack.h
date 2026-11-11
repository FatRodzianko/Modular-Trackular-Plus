// Modular-Trackular Plus: the Synth FX default pack. SPDX-License-Identifier: MIT
//
// The six effects that used to be built in (core/mods/synthfx), as plugins: the same
// arithmetic (synthFxDsp.h), so the same output, bit for bit
// (tools/synthfx/test_pack.py). Built with -I core/mods/synthfx (tools/synthfx/build_pack.py).
//
// Each knob is on whole steps the old settings had, and its value turns into the old
// setting's exactly: a percent knob's value v is the step, and the setting is v / 100.0f, as
// (step - zero) / perUnit was (synthfxSettings.cpp); Level is in 0.5 dB steps, already the
// setting. The texts are the old rows' (synthfxPage.cpp).

#ifndef SYNTHFX_PACK_H_
#define SYNTHFX_PACK_H_

#include "ptsfx.h"
#include "synthFxDsp.h"

namespace pack
{

using namespace mtfx;

/* "%.2f Hz" up to "%.1f kHz", as the rows showed rates */
static inline void hz_text(const PtsfxHost *h, float hz, char *out, uint32_t size)
{
    if (hz < 10.0f)
        h->print(out, size, "%.2f Hz", hz);
    else if (hz < 100.0f)
        h->print(out, size, "%.1f Hz", hz);
    else if (hz < 1000.0f)
        h->print(out, size, "%.0f Hz", hz);
    else
        h->print(out, size, "%.1f kHz", hz * 0.001f);
}

/* ---- the Distortions: Overdrive, Distortion, Bit Crusher ------------------------------- */

/* One Distortion per side: R's runs only on a stereo part */
struct DistState
{
    float fx[FXP_COUNT];                    /* the old ten settings: Gain, Tone and Level move */
    uint32_t started;                       /* the first block after init or reset has run */
    struct mt_dist d[2];
};

enum { DK_GAIN, DK_TONE, DK_LEVEL, DK_COUNT };

template <uint32_t Type>
static inline void dist_init(void *st)
{
    DistState *s = static_cast<DistState *>(st);
    s->fx[FXP_DIST] = (float)Type;
    s->fx[FXP_GAIN] = 0.5f;
    s->fx[FXP_TONE] = 0.5f;
    s->fx[FXP_PLACEMENT] = (float)PLACE_POST;
    s->fx[FXP_LEVEL] = 0.0f;
}

static inline void dist_set(void *st, uint32_t index, float value)
{
    DistState *s = static_cast<DistState *>(st);
    if (index == DK_GAIN)
        s->fx[FXP_GAIN] = value / 100.0f;
    else if (index == DK_TONE)
        s->fx[FXP_TONE] = value / 100.0f;
    else if (index == DK_LEVEL)
        s->fx[FXP_LEVEL] = value;
}

template <uint32_t Type>
static inline void dist_process(void *st, float *L, float *R, uint32_t in_channels)
{
    DistState *s = static_cast<DistState *>(st);
    if (!s->started) {                      /* from the knobs set since: no ramp from the defaults */
        dsp::dist_reset(&s->d[0], s->fx, 1);    /* on from the first sample: the host fades slots */
        dsp::dist_reset(&s->d[1], s->fx, 1);
        s->started = 1;
    }
    dsp::dist_block<Type>(&s->d[0], L, s->fx, 1);
    if (R != nullptr && in_channels == 2u)
        dsp::dist_block<Type>(&s->d[1], R, s->fx, 1);
}

static inline void dist_restart(void *st)
{
    static_cast<DistState *>(st)->started = 0;  /* the next block starts it again, cleared */
}

/* Level, as "%+.1f dB" */
static inline void level_text(const PtsfxHost *h, float value, char *out, uint32_t size)
{
    h->print(out, size, "%+.1f dB", value);
}

#define PACK_DIST_KNOBS(gainLabel, toneLabel)                                    \
    PTSFX_KNOB(gainLabel, 0, 100, 50, 1, "%", PTSFX_PARAM_INT),                  \
    PTSFX_KNOB(toneLabel, 0, 100, 50, 1, "%", PTSFX_PARAM_INT),                  \
    PTSFX_KNOB("Level", -30, 6, 0, 0.5, "dB", PTSFX_PARAM_FLOAT)

/* ---- the modulation effects: Chorus, Flanger, Phaser ------------------------------------ */

struct ModState
{
    float fx[FXP_COUNT];                    /* Rate, Depth, Mix and Feedback move */
    struct mt_fx s;                         /* its delay lines and all-passes (its Distortions unused) */
};

/* The knobs' indexes: Chorus has no Feedback, so Mix is its third */
enum { MK_RATE, MK_DEPTH, MK_FEEDBACK, MK_MIX };

template <uint32_t Type>
static inline void mod_init(void *st)
{
    ModState *s = static_cast<ModState *>(st);
    s->fx[FXP_EFFECT] = (float)Type;
    s->fx[FXP_RATE] = 0.5f;
    s->fx[FXP_DEPTH] = 0.5f;
    s->fx[FXP_MIX] = 0.5f;
    s->fx[FXP_FEEDBACK] = 0.5f;
    s->fx[FXP_PLACEMENT] = (float)PLACE_POST;
}

template <bool HasFeedback>
static inline void mod_set(void *st, uint32_t index, float value)
{
    ModState *s = static_cast<ModState *>(st);
    if (!HasFeedback && index >= MK_FEEDBACK)
        index++;                            /* Chorus: its third knob is Mix */
    if (index == MK_RATE)
        s->fx[FXP_RATE] = value / 100.0f;
    else if (index == MK_DEPTH)
        s->fx[FXP_DEPTH] = value / 100.0f;
    else if (index == MK_FEEDBACK)
        s->fx[FXP_FEEDBACK] = value / 100.0f;
    else if (index == MK_MIX)
        s->fx[FXP_MIX] = value / 100.0f;
}

template <uint32_t Type>
static inline void mod_process(void *st, float *L, float *R, uint32_t in_channels)
{
    ModState *s = static_cast<ModState *>(st);
    if (R == nullptr)                       /* never, for a WIDENS plugin: the host gives R */
        return;
    if (!s->s.running)                      /* the first block: Mix rises from 0, as when it was picked */
        dsp::fx_start(&s->s, s->fx, Type);
    if (in_channels == 2u)
        dsp::fx_mod<true>(&s->s, s->fx, Type, L, R);
    else
        dsp::fx_mod<false>(&s->s, s->fx, Type, L, R);
}

static inline void mod_restart(void *st)
{
    ModState *s = static_cast<ModState *>(st);
    s->s.running = 0;                       /* the next block starts it again, cleared ... */
    s->s.phase = 0.0f;                      /* ... with the LFO from the start, as a new instance */
}

template <bool HasFeedback>
static inline void mod_format(const PtsfxHost *h, uint32_t index, float value, char *out, uint32_t size)
{
    if (!HasFeedback && index >= MK_FEEDBACK)
        index++;
    if (index == MK_RATE)
        hz_text(h, dsp::rate_hz(value / 100.0f), out, size);
    else if (index == MK_FEEDBACK)
        h->print(out, size, value > 0.0f ? "+%.0f %%" : "%.0f %%", value);
    else
        h->print(out, size, "%.0f %%", value);
}

#define PACK_KNOB_RATE PTSFX_KNOB("Rate", 0, 100, 50, 1, "%", PTSFX_PARAM_INT)
#define PACK_KNOB_DEPTH PTSFX_KNOB("Depth", 0, 100, 50, 1, "%", PTSFX_PARAM_INT)
#define PACK_KNOB_FEEDBACK PTSFX_KNOB("Feedback", -95, 95, 50, 1, "%", PTSFX_PARAM_INT)
#define PACK_KNOB_MIX PTSFX_KNOB("Mix", 0, 100, 50, 1, "%", PTSFX_PARAM_INT)

} // namespace pack

#endif // SYNTHFX_PACK_H_
