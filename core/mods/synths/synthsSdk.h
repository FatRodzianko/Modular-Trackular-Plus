// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// What WAVE's and PHASE's CE Synth SDK classes share (waveSynth.cpp, phaseSynth.cpp): the
// type ids, a table-driven parameter builder, the value displays, the LFOs' parameters and the
// old effects' (kept for patches saved before), and the voice base class. Their effects are
// Synth FX's slots (synthfx/synthfxMod.h), run on the part after the voices.
//
// Main loop: the metadata, the value displays (they allocate std::strings). Audio
// interrupt: the voice's handlers, renderVoiceAudio and renderPostProcessAudio (no heap).

#ifndef MODS_SYNTHS_SYNTHSSDK_H_
#define MODS_SYNTHS_SYNTHSSDK_H_

#include "PolySynth/sdk/SynthVoiceSDK.h"
#include "synthsFx.h"
#include "synthsMod.h"

#include <cstdint>
#include <utility>

namespace mtsynth
{

/* The InstrumentTypeIds: past CE's (0-8) with room for CE's next ones. Patch files store
 * them. */
constexpr int kWaveTypeId = mods::synths::kWaveTypeId;
constexpr int kPhaseTypeId = mods::synths::kPhaseTypeId;

/* ---- the parameter table ------------------------------------------------------------- */

enum class Fmt : uint8_t
{
    kPercent,   // 0..1 shown 0-100 % (a bipolar range shows its sign)
    kList,      // one of `list`
    kTime,      // seconds
    kHz,        // Hz / kHz
    kSemis,     // whole semitones
    kCents,     // cents
    kRatio,     // an LFO's Ratio, 0..1: Hz, or with Sync a division; aux: the LFO (0, 1)
    kFxRate,    // the effect's Rate, 0..1: 0.05-5 Hz
    kLevel,     // dB
    kDistGain,  // the Distortion's Gain, or the Bit Crusher's Bits
    kDistTone,  // the Distortion's Tone, or the Bit Crusher's Rate
};

struct Spec
{
    const char* name = nullptr;
    float def = 0.0f, min = 0.0f, max = 1.0f;
    polysynth::Patch::Metadata::Param::Curve curve = polysynth::Patch::Metadata::Param::Curve::kLinear;
    Fmt fmt = Fmt::kPercent;
    const char* const* list = nullptr;
    uint8_t listLen = 0;
    uint8_t cc = 0xff;          // 0xff: none
    uint8_t aux = 0;
    bool invalidates = false;   // invalidatesOtherMetadata: other names or displays follow it
    bool mult = false;          // MacroOperation::kMult
};

/* One parameter's definition from its spec (and, for the Distortion's two knobs, the
 * names for the type chosen). */
polysynth::Patch::Metadata::Param makeParam(const Spec& s);

/* The LFO and effect rows every engine has: 27-34 (the LFOs' Ratio, Waveform, Retrigger,
 * Sync), 35-36 (Glide), 39 (Volume), 42-59 (the Mod Matrix, with the engine's lists),
 * 60-61 (Polarity), 62-71 (the old effects: in no section, read only to convert them). */
void sharedSpecs(Spec* s, const char* const* srcs, uint8_t srcCount, const char* const* dests,
                 uint8_t destCount);

/* The LFO tile's controls */
extern const int kLfoSection[10];

/* An LFO's Ratio (0..1) as the engine reads it: Hz (0.01-100, exponential), or with Sync
 * the division's index (0-15) */
float lfoRatio(float v, bool sync);

/* ---- the voice ----------------------------------------------------------------------- */

/* Sets p[I] from the patch: one handler per parameter (ParamHelper takes stateless ones) */
template<class V, size_t I>
struct SetParam
{
    void operator()(V& v, polysynth::Patch::optional_paramvalue_t x) const
    {
        v.setParam(static_cast<int>(I), *x);
    }
};

template<class V, size_t... I>
void addAll(polysynth::sdk::ParamHelper<V>& h, const Spec* specs, std::index_sequence<I...>)
{
    (h.addParam(static_cast<int>(I), makeParam(specs[I]), SetParam<V, I>{}), ...);
}

/* A voice of WAVE or PHASE: the parameters as the engine reads them (p, by parameter id).
 * `Derived` supplies the engine. */
template<class Derived, class ParamEnum>
class MtSynthVoice : public polysynth::sdk::CustomSynthVoice<Derived, ParamEnum>
{
  public:
    using Ctx = polysynth::InstrumentVoice::InstrumentVoiceContext;

    /* Audio interrupt, or the main loop with it masked: a store, and the LFOs' Ratio */
    void setParam(int id, float v)
    {
        if (id < 0 || id >= static_cast<int>(P_COUNT))
            return;
        for (int k = 0; k < 2; k++) {
            const int base = 27 + 4 * k;            /* Ratio, Waveform, Retrigger, Sync */
            if (id == base) {
                ratio[k] = v;
                p[base] = lfoRatio(v, p[base + 3] > 0.5f);
                return;
            }
            if (id == base + 3) {
                p[id] = v;
                p[base] = lfoRatio(ratio[k], v > 0.5f);
                return;
            }
        }
        p[id] = v;
    }

    /* The part's sum of voices, mono, into both sides (as the old effects' dry path did); Synth
     * FX's slots run on it after this, from the left side (synthfxMod.h) */
    void renderPostProcessAudio(const Ctx& /*ctx*/, polysynth::AudioBufferStereoRef out) final
    {
        toStereo(out);
    }

  protected:
    MtSynthVoice()
    {
        for (auto& v : p)
            v = 0.0f;
        ratio[0] = ratio[1] = 0.0f;
    }

    /* the voice's mono block into both sides */
    static void toStereo(polysynth::AudioBufferStereoRef out)
    {
        const float* L = out[0].data();
        float* R = out[1].data();
        for (uint32_t n = 0; n < WT_BLOCK; n++)
            R[n] = L[n];
    }

    float p[P_COUNT];
    float ratio[2];                                 /* the LFOs' Ratio as set, 0..1 */
};

} // namespace mtsynth

#endif // MODS_SYNTHS_SYNTHSSDK_H_
