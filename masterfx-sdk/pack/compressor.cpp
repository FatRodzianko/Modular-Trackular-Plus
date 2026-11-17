// Modular-Trackular Plus: Master FX plugins (.ptmfx). SPDX-License-Identifier: MIT
//
// Compressor: a compressor for the dynamics slot on CE's Attack, Release and Threshold, in place of
// CE's limiter (tools/masterfx/build_pack.py builds it into the default pack). It was masterfx-2.0's
// Glue; Glue (glue.cpp) is now the bus compressor with its own knobs and the Tracks list.
//
// - The detector follows the louder side's peak (the two sides are linked, so the stereo image
//   stays put), after a gentle high-pass so the lowest bass doesn't pump it. With CE's Sidechain
//   list on a key, it follows the key instead.
// - Every 8 frames, the gain for the level over CE's Threshold, by the Ratio with a soft knee 6 dB
//   wide. The gain glides to it over those 8 frames.
// - CE's Attack and Release are the detector's times, in ms.
// - With a key, the key keeps its level, as with CE's limiter: out = key + gain * (master - key).
// - Makeup lifts the result. Nothing limits it after that: past full scale the host clips.
// - It can duck chosen tracks instead of the master (PTMFX_FLAG_DUCK): then it fills the block's
//   gain with makeup * gain for each frame, from the key.
//
// The meter gets the block's mean gain, without the makeup. Float blocks; about 50 bytes of state
// and none of the slot's memory.

#include "ptmfx.h"

#include <stdint.h>

namespace
{

constexpr float kKnee = 1.0f;          // log2 units: 6 dB
constexpr float kHighPass = 0.995f;    // the detector's one-pole high-pass, about 35 Hz
constexpr uint32_t kHop = 8;           // frames per gain step
constexpr float kLog2e = 1.44269504f;
constexpr float kMinLevel = 1.0e-9f;

struct State
{
    const PtmfxHost *host;
    float slope;                       // 1 - 1 / ratio
    float makeup;                      // linear
    float attackMs, releaseMs, threshold; // CE's knobs as last seen
    float attack, release;             // the detector's coefficients for them
    float thresholdLog2;
    float level;                       // the detector
    float hpIn, hpOut;
    float gain;
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

// A detector's coefficient for a time in ms: it covers about 63 % of a step in that time
float coefficient(const PtmfxHost *host, float ms)
{
    if (!(ms > 0.05f))
    {
        ms = 0.05f;
    }
    return 1.0f - host->expf(-1.0f / (ms * 0.001f * host->sample_rate));
}

void follow(State *s, const PtmfxDynamicsKnobs *k)
{
    if (k->attack != s->attackMs)
    {
        s->attackMs = k->attack;
        s->attack = coefficient(s->host, k->attack);
    }
    if (k->release != s->releaseMs)
    {
        s->releaseMs = k->release;
        s->release = coefficient(s->host, k->release);
    }
    if (k->threshold != s->threshold)
    {
        s->threshold = k->threshold;
        const float t = k->threshold > 1.0e-5f ? k->threshold : 1.0e-5f;
        s->thresholdLog2 = s->host->logf(t) * kLog2e;
    }
}

// How far to turn down, in log2 units, for a level `over` log2 units past the threshold
inline float reduction(float over, float slope)
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

} // namespace

PTMFX_INFO = {
    0, 0, PTMFX_FLAG_FLOAT | PTMFX_FLAG_DUCK, /* magic and version (the builder's), flags */
    PTMFX_SLOT_DYNAMICS, 2, { 0, 0 },  /* slot, own knobs, reserved */
    "Compressor\0Comp", "MT+",         /* Comp: the short name, for the Master page's column */
    {},                                /* CE's Attack, Release and Threshold as they are */
    sizeof(State),
    0, 0, 0, 0, 0, 0, 0,               /* the code's place and the entries (the builder's) */
    { 0, 0, 0 },
    {
        PTMFX_KNOB("Ratio", 1, 20, 4, 0.5, ":1", PTMFX_PARAM_FLOAT),
        PTMFX_KNOB("Makeup", 0, 12, 0, 0.5, "dB", PTMFX_PARAM_FLOAT),
    },
};

PTMFX_INIT(void *st, const PtmfxMemory *mem, const PtmfxHost *host)
{
    (void)mem;
    State *s = static_cast<State *>(st);
    s->host = host;
    s->slope = 0.75f;
    s->makeup = 1.0f;
    s->attackMs = -1.0f;               // worked out on the first block
    s->releaseMs = -1.0f;
    s->threshold = -1.0f;
    s->gain = 1.0f;
}

PTMFX_SET_PARAM(void *st, uint32_t index, float value)
{
    State *s = static_cast<State *>(st);
    if (index == 0)
    {
        s->slope = 1.0f - 1.0f / (value < 1.0f ? 1.0f : value);
    }
    else if (index == 1)
    {
        s->makeup = s->host->powf(10.0f, value * 0.05f);
    }
}

PTMFX_RESET(void *st)
{
    State *s = static_cast<State *>(st);
    s->level = 0.0f;
    s->hpIn = 0.0f;
    s->hpOut = 0.0f;
    s->gain = 1.0f;
}

// The page's text: "4.0:1" and "+3.5 dB"
PTMFX_FORMAT(const PtmfxHost *host, uint32_t index, float value, char *out, uint32_t size)
{
    host->print(out, size, index == 0 ? "%.1f:1" : "+%.1f dB", value);
}

PTMFX_PROCESS(void *st, const PtmfxBlock *b)
{
    State *s = static_cast<State *>(st);
    const PtmfxDynamicsKnobs *k = static_cast<const PtmfxDynamicsKnobs *>(b->knobs);
    follow(s, k);

    float *left = b->fleft;
    float *right = b->fright;
    const bool keyed = b->fkey_left != nullptr && b->fkey_right != nullptr;
    const float *keyLeft = keyed ? b->fkey_left : left;
    const float *keyRight = keyed ? b->fkey_right : right;
    float *out = b->gain;              // ducking tracks: the gain for them, frame by frame

    float level = s->level, hpIn = s->hpIn, hpOut = s->hpOut, gain = s->gain;
    const float attack = s->attack, release = s->release, makeup = s->makeup;
    float sum = 0.0f;
    for (uint32_t start = 0; start < b->frames; start += kHop)
    {
        for (uint32_t i = start; i < start + kHop; i++)
        {
            const float x = absf(keyLeft[i]) > absf(keyRight[i]) ? keyLeft[i] : keyRight[i];
            hpOut = kHighPass * (hpOut + x - hpIn);
            hpIn = x;
            const float a = absf(hpOut);
            level += (a > level ? attack : release) * (a - level);
        }
        const float over = quickLog2(level + kMinLevel) - s->thresholdLog2;
        const float target = quickExp2(-reduction(over, s->slope));
        const float step = (target - gain) * (1.0f / kHop);
        for (uint32_t i = start; i < start + kHop; i++)
        {
            gain += step;
            sum += gain;
            if (out != nullptr)
            {
                out[i] = makeup * gain;
            }
            if (keyed)
            {
                left[i] = makeup * (keyLeft[i] + gain * (left[i] - keyLeft[i]));
                right[i] = makeup * (keyRight[i] + gain * (right[i] - keyRight[i]));
            }
            else
            {
                left[i] = makeup * gain * left[i];
                right[i] = makeup * gain * right[i];
            }
        }
    }
    s->level = level;
    s->hpIn = hpIn;
    s->hpOut = hpOut;
    s->gain = gain;
    return sum * (1.0f / PTMFX_BLOCK);
}
