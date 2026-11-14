// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// What the plugin runtimes share (Synth FX's synthfxPlugins.cpp, Master FX's
// masterfxPlugins.cpp): text without a C library, the number helpers, a knob's steps and its
// header check, and the output guard. Header only and platform-free, so the host tests and the
// emulator rigs build it as it is.
//
// A knob is any struct with a plugin knob's fields (PtsfxParam, PtmfxParam): label, unit,
// display_type, min_val, max_val, default_val, step. The host keeps a knob on its steps: its
// value is always fmaf(i, step, min) for a whole i from 0 to its last step.

#ifndef MODS_PLUGINS_PLUGINKIT_H_
#define MODS_PLUGINS_PLUGINKIT_H_

#include <cstddef>
#include <cstdint>

namespace mods
{
namespace pluginkit
{

// ---- text (no C library) ------------------------------------------------------------------

inline size_t textLength(const char* s, size_t limit)
{
    size_t n = 0;
    while (n < limit && s[n] != 0)
    {
        n++;
    }
    return n;
}

inline bool sameText(const char* a, const char* b)
{
    while (*a != 0 && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

inline void copyText(char* out, size_t size, const char* in)
{
    size_t n = 0;
    for (; n + 1 < size && in[n] != 0; n++)
    {
        out[n] = in[n];
    }
    if (size > 0)
    {
        out[n] = 0;
    }
}

inline void appendText(char* out, size_t size, const char* in)
{
    const size_t n = textLength(out, size);
    if (n < size)
    {
        copyText(out + n, size - n, in);
    }
}

// dir/file into out, or false if it doesn't fit. fileLimit: the longest file name looked at.
inline bool joinPath(const char* dir, const char* file, size_t fileLimit, char* out, size_t size)
{
    copyText(out, size, dir);
    appendText(out, size, "/");
    appendText(out, size, file);
    return textLength(out, size) == textLength(dir, size) + 1 + textLength(file, fileLimit);
}

// A plugin's text field (a name, a label) is filled and ended inside its size
template <size_t N>
inline bool endedText(const char (&s)[N])
{
    return textLength(s, N) < N;
}

// ---- numbers ------------------------------------------------------------------------------

inline bool finite(float v)
{
    return v - v == 0.0f; // NaN and the infinities give NaN
}

// The nearest whole number, for |x| < 2^30
inline int32_t nearest(float x)
{
    return x >= 0.0f ? static_cast<int32_t>(x + 0.5f) : -static_cast<int32_t>(-x + 0.5f);
}

inline float absf(float x)
{
    return x < 0.0f ? -x : x;
}

// x is a whole number, near enough for steps worked out in single precision
inline bool whole(float x, int32_t* n)
{
    if (!(absf(x) < 1.0e9f))
    {
        return false;
    }
    *n = nearest(x);
    const float tolerance = 1.0e-3f * (absf(x) > 1.0f ? absf(x) : 1.0f);
    return absf(x - static_cast<float>(*n)) <= tolerance;
}

// ---- a knob's steps -----------------------------------------------------------------------

// Its last step (it has steps 0 to this)
template <typename Knob>
inline uint32_t lastStep(const Knob& p)
{
    int32_t n = 0;
    whole((p.max_val - p.min_val) / p.step, &n);
    return n < 0 ? 0u : static_cast<uint32_t>(n);
}

// min + step x p.step, rounded once
template <typename Knob>
inline float valueAt(const Knob& p, uint32_t step)
{
    return __builtin_fmaf(static_cast<float>(step), p.step, p.min_val);
}

template <typename Knob>
inline uint32_t stepNearest(const Knob& p, float value)
{
    const uint32_t last = lastStep(p);
    if (!(value > p.min_val)) // NaN too
    {
        return 0;
    }
    const float at = (value - p.min_val) / p.step;
    if (!(at < static_cast<float>(last)))
    {
        return last;
    }
    return static_cast<uint32_t>(nearest(at));
}

// The knob's display types, the same letters in both formats
constexpr uint8_t kKnobFloat = 'f';
constexpr uint8_t kKnobInt = 'd';
constexpr uint8_t kKnobList = 'l';

// Is the knob one the host can show and step: nullptr, or a short reason
template <typename Knob>
inline const char* checkKnob(const Knob& p, uint32_t maxSteps)
{
    int32_t steps = 0, at = 0;
    if (p.label[0] == 0 || !endedText(p.label) || !endedText(p.unit))
    {
        return "knob label";
    }
    if (p.display_type != kKnobFloat && p.display_type != kKnobInt && p.display_type != kKnobList)
    {
        return "knob display";
    }
    if (!finite(p.min_val) || !finite(p.max_val) || !finite(p.default_val) || !finite(p.step) ||
        !(p.step > 0.0f) || !(p.max_val >= p.min_val))
    {
        return "knob range";
    }
    if (!whole((p.max_val - p.min_val) / p.step, &steps) || static_cast<uint32_t>(steps) > maxSteps)
    {
        return "knob steps";
    }
    if (!whole((p.default_val - p.min_val) / p.step, &at) || at < 0 || at > steps)
    {
        return "knob default";
    }
    return nullptr;
}

// ---- the output guard ---------------------------------------------------------------------

// The block looked at without a branch per sample: v - v is 0 for every finite v and NaN
// otherwise, so the sums stay 0 unless one isn't; the peak is the largest |v| (VMAXNM, which
// passes over a NaN: the sums catch those). n is even. Inlined into the caller's interrupt
// code.
__attribute__((always_inline)) inline bool blockFine(const float* b, uint32_t n, float limit)
{
    float nan0 = 0.0f, nan1 = 0.0f, peak0 = 0.0f, peak1 = 0.0f;
    for (uint32_t i = 0; i < n; i += 2)
    {
        const float v0 = b[i], v1 = b[i + 1];
        nan0 += v0 - v0;
        nan1 += v1 - v1;
        peak0 = __builtin_fmaxf(peak0, __builtin_fabsf(v0));
        peak1 = __builtin_fmaxf(peak1, __builtin_fabsf(v1));
    }
    return nan0 + nan1 == 0.0f && __builtin_fmaxf(peak0, peak1) <= limit;
}

// The rare block that needs it: NaN or infinite becomes 0 (true: there was one), the rest is
// held within +-limit
inline bool fixBlock(float* b, uint32_t n, float limit)
{
    bool bad = false;
    for (uint32_t i = 0; i < n; i++)
    {
        const float v = b[i];
        if (!finite(v))
        {
            b[i] = 0.0f;
            bad = true;
        }
        else if (v > limit)
        {
            b[i] = limit;
        }
        else if (v < -limit)
        {
            b[i] = -limit;
        }
    }
    return bad;
}

// A Thumb entry point at offset in code
template <typename Entry>
inline Entry entryAt(const uint8_t* code, uint32_t offset)
{
    return reinterpret_cast<Entry>(reinterpret_cast<uintptr_t>(code + offset) | 1u);
}

} // namespace pluginkit
} // namespace mods

#endif // MODS_PLUGINS_PLUGINKIT_H_
