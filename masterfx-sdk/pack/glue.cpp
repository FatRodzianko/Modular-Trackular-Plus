// Modular-Trackular Plus: Master FX plugins (.ptmfx). SPDX-License-Identifier: MIT
//
// Glue: a bus compressor for the dynamics slot, in place of CE's limiter, with its own Attack,
// Release and Threshold in the Limiter page's first three columns (PTMFX_FLAG_OWN_DYNAMICS), and
// Ratio and Makeup in columns 5 and 6. The Tracks list (PTMFX_FLAG_BUS) picks what it works on:
// the master, or a set of tracks compressed together, the rest of the mix untouched
// (tools/masterfx/build_pack.py builds it into the default pack).
//
// - The detector: each side through a gentle high-pass (about 35 Hz, so the lowest bass doesn't
//   pump it), then the louder of the two (the sides are linked, so the stereo image stays put),
//   its peak over each 8 frames.
// - The gain computer, in log2 units: how far that peak is over the Threshold, turned down by the
//   Ratio, with a soft knee 6 dB wide.
// - Attack and Release smooth the turning down itself (not the level): it grows at the Attack's
//   time and shrinks at the Release's. Each time is about 63 % of a step.
// - The gain (with the Makeup) glides to the new value over the 8 frames.
// - Makeup lifts the result. Nothing limits it after that: past full scale the host clips.
// - No key: the Tracks list is where CE's Sidechain list was.
//
// Attack and Release are lists of steps, one a jog-wheel click, about 12 % apart: Attack 0.1 ms to
// 100 ms (20 a decade), Release 50 ms to 2 s (6 an octave, the last held at 2 s).
//
// The meter gets the block's mean gain, without the makeup. Float blocks; about 50 bytes of state
// and none of the slot's memory.

#include "ptmfx.h"

#include <stdint.h>

namespace
{

constexpr float kKnee = 1.0f;            // log2 units: 6 dB
constexpr float kHighPass = 0.995f;      // the detector's one-pole high-pass, about 35 Hz
constexpr uint32_t kHop = 8;             // frames per gain step
constexpr float kMinLevel = 1.0e-9f;
constexpr float kLog2PerDb = 0.16609640f; // 1 / 20log10(2)

// The knobs
constexpr uint32_t kAttack = 0, kRelease = 1, kThreshold = 2, kRatio = 3, kMakeup = 4;
constexpr float kAttackSteps = 60.0f;    // 0.1 ms * 10^(step / 20): 0.1 to 100 ms
constexpr float kAttackDefault = 40.0f;  // 10 ms
constexpr float kReleaseSteps = 32.0f;   // 50 ms * 2^(step / 6), held at 2000 ms
constexpr float kReleaseDefault = 12.0f; // 200 ms
constexpr float kReleaseMost = 2000.0f;

struct State
{
    const PtmfxHost *host;
    float attack, release;               // the smoothing's coefficients per 8 frames
    float thresholdLog2;
    float slope;                         // 1 - 1 / ratio
    float makeup;                        // linear
    float hpInL, hpOutL, hpInR, hpOutR;
    float reduction;                     // log2 units, the smoothed turning down
    float gain;                          // makeup * 2^-reduction, as the last frame had it
};

inline float absf(float x)
{
    return x < 0.0f ? -x : x;
}

// log2 and 2^x, quick: the float's exponent and a short polynomial for the rest. Within about 0.01
// of log2, plenty for a detector.
inline float quickLog2(float x)
{
    union
    {
        float f;
        uint32_t i;
    } u = { x };
    // The polynomial is 1 + log2(m) for the mantissa m (1 to 2), so the exponent's bias is 128
    const float exponent = static_cast<float>(static_cast<int32_t>((u.i >> 23) & 255u)) - 128.0f;
    u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
    const float m = u.f;
    return exponent + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}

inline float quickExp2(float x)
{
    if (x < -60.0f)
    {
        return 0.0f;
    }
    float whole = static_cast<float>(static_cast<int32_t>(x));
    if (whole > x)
    {
        whole -= 1.0f;
    }
    const float f = x - whole;
    union
    {
        uint32_t i;
        float f;
    } u;
    u.i = static_cast<uint32_t>(static_cast<int32_t>(whole) + 127) << 23;
    return u.f * (1.0f + f * (0.6960656f + f * (0.2244943f + f * 0.0794402f)));
}

float attackMs(const PtmfxHost *host, float step)
{
    return 0.1f * host->powf(10.0f, step * (1.0f / 20.0f));
}

float releaseMs(const PtmfxHost *host, float step)
{
    const float ms = 50.0f * host->powf(2.0f, step * (1.0f / 6.0f));
    return ms < kReleaseMost ? ms : kReleaseMost;
}

// The smoothing's coefficient for a time in ms, per 8 frames: about 63 % of a step in that time
float coefficient(const PtmfxHost *host, float ms)
{
    return 1.0f - host->expf(-static_cast<float>(kHop) / (ms * 0.001f * host->sample_rate));
}

// How far to turn down, in log2 units, for a level `over` log2 units past the threshold
inline float reductionFor(float over, float slope)
{
    const float half = 0.5f * kKnee;
    if (over <= -half)
    {
        return 0.0f;
    }
    if (over < half)
    {
        const float t = over + half;
        return slope * t * t * (0.5f / kKnee);
    }
    return slope * over;
}

void set(State *s, uint32_t index, float value)
{
    const PtmfxHost *host = s->host;
    switch (index)
    {
        case kAttack: s->attack = coefficient(host, attackMs(host, value)); break;
        case kRelease: s->release = coefficient(host, releaseMs(host, value)); break;
        case kThreshold: s->thresholdLog2 = value * kLog2PerDb; break;
        case kRatio: s->slope = 1.0f - 1.0f / (value < 1.0f ? 1.0f : value); break;
        case kMakeup: s->makeup = host->powf(10.0f, value * 0.05f); break;
        default: break;
    }
}

} // namespace

PTMFX_INFO = {
    0, 0, PTMFX_FLAG_FLOAT | PTMFX_FLAG_OWN_DYNAMICS | PTMFX_FLAG_BUS, /* magic and version (the builder's), flags */
    PTMFX_SLOT_DYNAMICS, 5, { 0, 0 },    /* slot, own knobs, reserved */
    "Glue", "MT+",
    {},                                  /* CE's labels: its own knobs take those columns */
    sizeof(State),
    0, 0, 0, 0, 0, 0, 0,                 /* the code's place and the entries (the builder's) */
    { 0, 0, 0 },
    {
        PTMFX_KNOB("Attack", 0, kAttackSteps, kAttackDefault, 1, "", PTMFX_PARAM_LIST),
        PTMFX_KNOB("Release", 0, kReleaseSteps, kReleaseDefault, 1, "", PTMFX_PARAM_LIST),
        PTMFX_KNOB("Threshold", -40, 0, -12, 0.5, "dB", PTMFX_PARAM_FLOAT),
        PTMFX_KNOB("Ratio", 1, 10, 4, 0.1, ":1", PTMFX_PARAM_FLOAT),
        PTMFX_KNOB("Makeup", 0, 12, 0, 0.5, "dB", PTMFX_PARAM_FLOAT),
    },
};

PTMFX_INIT(void *st, const PtmfxMemory *mem, const PtmfxHost *host)
{
    (void)mem;
    State *s = static_cast<State *>(st);
    s->host = host;
    set(s, kAttack, kAttackDefault);     // the host sets every knob again before the first block
    set(s, kRelease, kReleaseDefault);
    set(s, kThreshold, -12.0f);
    set(s, kRatio, 4.0f);
    set(s, kMakeup, 0.0f);
    s->gain = s->makeup;
}

PTMFX_SET_PARAM(void *st, uint32_t index, float value)
{
    set(static_cast<State *>(st), index, value);
}

PTMFX_RESET(void *st)
{
    State *s = static_cast<State *>(st);
    s->hpInL = 0.0f;
    s->hpOutL = 0.0f;
    s->hpInR = 0.0f;
    s->hpOutR = 0.0f;
    s->reduction = 0.0f;
    s->gain = s->makeup;
}

// The page's text: "10.0 ms", "200 ms", "1.80 s", "-12.0 dB", "4.0:1" and "+3.5 dB"
PTMFX_FORMAT(const PtmfxHost *host, uint32_t index, float value, char *out, uint32_t size)
{
    if (index == kAttack)
    {
        const float ms = attackMs(host, value);
        host->print(out, size, ms < 1.0f ? "%.2f ms" : (ms < 99.95f ? "%.1f ms" : "%.0f ms"), ms);
    }
    else if (index == kRelease)
    {
        const float ms = releaseMs(host, value);
        if (ms < 999.5f)
        {
            host->print(out, size, "%.0f ms", ms);
        }
        else
        {
            host->print(out, size, "%.2f s", ms * 0.001f);
        }
    }
    else if (index == kThreshold)
    {
        host->print(out, size, "%.1f dB", value);
    }
    else if (index == kRatio)
    {
        host->print(out, size, "%.1f:1", value);
    }
    else
    {
        host->print(out, size, "+%.1f dB", value);
    }
}

PTMFX_PROCESS(void *st, const PtmfxBlock *b)
{
    State *s = static_cast<State *>(st);
    float *left = b->fleft;
    float *right = b->fright;

    float hpInL = s->hpInL, hpOutL = s->hpOutL, hpInR = s->hpInR, hpOutR = s->hpOutR;
    float reduction = s->reduction, gain = s->gain;
    const float attack = s->attack, release = s->release, makeup = s->makeup;
    const float threshold = s->thresholdLog2, slope = s->slope;
    float sum = 0.0f;
    for (uint32_t start = 0; start < b->frames; start += kHop)
    {
        float peak = 0.0f;
        for (uint32_t i = start; i < start + kHop; i++)
        {
            hpOutL = kHighPass * (hpOutL + left[i] - hpInL);
            hpInL = left[i];
            hpOutR = kHighPass * (hpOutR + right[i] - hpInR);
            hpInR = right[i];
            const float l = absf(hpOutL);
            const float r = absf(hpOutR);
            const float a = l > r ? l : r;
            peak = a > peak ? a : peak;
        }
        const float target = reductionFor(quickLog2(peak + kMinLevel) - threshold, slope);
        reduction += (target > reduction ? attack : release) * (target - reduction);
        const float down = quickExp2(-reduction);
        sum += down * static_cast<float>(kHop);
        const float step = (makeup * down - gain) * (1.0f / kHop);
        for (uint32_t i = start; i < start + kHop; i++)
        {
            gain += step;
            left[i] *= gain;
            right[i] *= gain;
        }
    }
    s->hpInL = hpInL;
    s->hpOutL = hpOutL;
    s->hpInR = hpInR;
    s->hpOutR = hpOutR;
    s->reduction = reduction;
    s->gain = gain;
    return sum * (1.0f / PTMFX_BLOCK);
}
