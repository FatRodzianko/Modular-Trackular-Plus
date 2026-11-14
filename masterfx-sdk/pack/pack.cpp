// Modular-Trackular Plus: Master FX plugins (.ptmfx). SPDX-License-Identifier: MIT
//
// The default pack's eight effects, one plugin each, built from this file with one of -DFX_FREEVERB,
// -DFX_PLATE, -DFX_SPRING, -DFX_SHIMMER, -DFX_GATED, -DFX_REVERSE, -DFX_TAPE or -DFX_BBD
// (tools/masterfx/build_pack.py). The DSP is MT+'s Master FX effects as they were built into the
// firmware up to masterfx-1.x (the *_dsp.h headers here), unchanged: each header is included
// inside a struct, so its state is the struct's and lives in the memory the host gives
// (MFX_STATE and MFX_FN empty), and its tables are the struct's constants (MFX_TABLE).
//
// As in the firmware, an effect lays its lines out in the slot's cleared buffers on its first
// block after a clear: reset() asks for that, and process() does it with the block's knobs.

#include "ptmfx.h"

#include <arm_acle.h> // __ssat
#include <stddef.h>
#include <stdint.h>

#define MFX_ITCM __attribute__((noinline))
#define MFX_FLASH __attribute__((noinline))
#define MFX_FN
#define MFX_STATE
#define MFX_TABLE static constexpr

// The knobs as the headers name them: the same layout as ptmfx.h's
struct ReverbKnobs
{
    float amount, inputGain, size, diffusion, lp;
    int32_t predelay;
};
struct DelayKnobs
{
    uint32_t time;
    int32_t filterPole;
    uint8_t feedback;
    uint8_t pingpong;
};
static_assert(sizeof(ReverbKnobs) == sizeof(PtmfxReverbKnobs) && offsetof(ReverbKnobs, predelay) == offsetof(PtmfxReverbKnobs, predelay) &&
              offsetof(ReverbKnobs, lp) == offsetof(PtmfxReverbKnobs, lp));
static_assert(sizeof(DelayKnobs) == sizeof(PtmfxDelayKnobs) && offsetof(DelayKnobs, feedback) == offsetof(PtmfxDelayKnobs, feedback) &&
              offsetof(DelayKnobs, pingpong) == offsetof(PtmfxDelayKnobs, pingpong));

#if defined(FX_TAPE) || defined(FX_BBD)
#define IS_DELAY 1
#endif

namespace
{

struct Fx
{
#ifdef IS_DELAY
#define BLOCK 128u        // the one name the delays take from freeverb_dsp.h
#else
#include "freeverb_dsp.h" // BLOCK, BUFFER_SAMPLES, PREDELAY_MAX, sat16 and fclamp, which every reverb uses
#endif
#if defined(FX_SHIMMER) || defined(FX_PLATE)
#include "shimmer_dsp.h"  // the plate is built on the shimmer's passes
#endif
#if defined(FX_PLATE)
#include "plate_dsp.h"
#elif defined(FX_SPRING)
#include "spring_dsp.h"
#elif defined(FX_GATED) || defined(FX_REVERSE)
#include "tape_dsp.h"      // the tone filter's range (POLE_MIN, POLE_MAX)
#include "gated_dsp.h"
#elif defined(FX_TAPE)
#include "tape_dsp.h"
#elif defined(FX_BBD)
#include "tape_dsp.h"      // the target time and the saturation
#include "bbd_dsp.h"
#endif
};

struct State
{
    uint32_t buffer0, buffer1;    // the slot's first two areas
    uint32_t ready;               // laid out since the last clear
    Fx fx;
};

} // namespace

#if defined(FX_FREEVERB)
#define FX_NAME "Freeverb"
#elif defined(FX_PLATE)
#define FX_NAME "Plate"
#define FX_LABELS { "Decay" }
#elif defined(FX_SPRING)
#define FX_NAME "Spring"
#define FX_LABELS { "Decay", "", "", "Drip" }
#elif defined(FX_SHIMMER)
#define FX_NAME "Shimmer"
#define FX_LABELS { "Decay", "Tone", "", "Shimmer" }
#elif defined(FX_GATED)
#define FX_NAME "\"Gated\""
#define FX_LABELS { "Length", "Tone", "", "Release" }
#elif defined(FX_REVERSE)
#define FX_NAME "Reverse"
#define FX_LABELS { "Length", "Tone", "", "Release" }
#elif defined(FX_TAPE)
#define FX_NAME "Tape"
#elif defined(FX_BBD)
#include "tape_dsp.h"      // the target time and the saturation
#define FX_NAME "BBD"
#else
#error "build with one of the FX_ defines"
#endif
#ifndef FX_LABELS
#define FX_LABELS {}
#endif
// The labels: CE's knobs as masterfx-1.x's pages named them for each effect, in the page's order
// (reverb: Size, Damping, Predelay, Diffusion); "" keeps CE's.

#ifdef IS_DELAY
#define FX_SLOT PTMFX_SLOT_DELAY
#else
#define FX_SLOT PTMFX_SLOT_REVERB
#endif

PTMFX_INFO = {
    0, 0, 0,                      /* magic and version (the builder's), flags */
    FX_SLOT, 0, { 0, 0 },         /* slot, own knobs, reserved */
    FX_NAME, "MT+",
    FX_LABELS,
    sizeof(State),
    0, 0, 0, 0, 0, 0, 0,          /* the code's place and the entries (the builder's) */
    { 0, 0, 0 },
    {},
};

PTMFX_INIT(void *st, const PtmfxMemory *mem, const PtmfxHost *host)
{
    (void)host;
    State *s = static_cast<State *>(st);
    s->buffer0 = reinterpret_cast<uint32_t>(mem->area[0]);
    s->buffer1 = reinterpret_cast<uint32_t>(mem->area[1]);
}

PTMFX_SET_PARAM(void *st, uint32_t index, float value)
{
    (void)st;
    (void)index;
    (void)value;
}

PTMFX_RESET(void *st)
{
    static_cast<State *>(st)->ready = 0;
}

PTMFX_PROCESS(void *st, const PtmfxBlock *b)
{
    State *s = static_cast<State *>(st);
    Fx &fx = s->fx;
#ifdef IS_DELAY
    const DelayKnobs *k = static_cast<const DelayKnobs *>(b->knobs);
    if (!s->ready)
    {
#if defined(FX_TAPE)
        fx.tape_init(k, s->buffer0, s->buffer1);
#else
        fx.bbd_init(k, s->buffer0, s->buffer1);
#endif
        s->ready = 1;
    }
#if defined(FX_TAPE)
    fx.tape_process(k, b->in, b->left, b->right);
#else
    fx.bbd_process(k, b->in, b->left, b->right);
#endif
#else
    const ReverbKnobs *k = static_cast<const ReverbKnobs *>(b->knobs);
    if (!s->ready)
    {
#if defined(FX_SHIMMER)
        fx.shimmer_init(s->buffer0, s->buffer1);
#elif defined(FX_PLATE)
        fx.plate_init(s->buffer0, s->buffer1);
#elif defined(FX_SPRING)
        fx.spring_init(s->buffer0, s->buffer1);
#elif defined(FX_GATED) || defined(FX_REVERSE)
        fx.gated_init(s->buffer0, s->buffer1);
#else
        fx.fv_init(s->buffer0, s->buffer1);
#endif
        s->ready = 1;
    }
#if defined(FX_SHIMMER)
    fx.shimmer_process(k, b->left, b->right);
#elif defined(FX_PLATE)
    fx.plate_process(k, b->left, b->right);
#elif defined(FX_SPRING)
    fx.spring_process(k, b->left, b->right);
#elif defined(FX_GATED)
    fx.gated_process(k, b->left, b->right, 0);
#elif defined(FX_REVERSE)
    fx.gated_process(k, b->left, b->right, 1);
#else
    fx.fv_process(k, b->left, b->right, 0);
#endif
#endif
    return 1.0f;
}
