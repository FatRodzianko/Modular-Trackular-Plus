// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// synths: WAVE and PHASE, two synth engines from Modular-Trackular's synths patch (sy9), and
// DCO6, a virtual analog after the Juno-106's design (docs/mt/VA_SYNTH_PLAN.md), as .psyn
// plugins built with CE's synth-plugin-sdk (psyn/: waveSynth.cpp, phaseSynth.cpp,
// dco6Synth.cpp, psyn/Makefile). The firmware has none of their code: PsynLoader finds
// WAVE.psyn, PHASE.psyn and DCO6.psyn in /System/Synths on the card with CE's engines, at the type
// ids below. WAVE's and PHASE's old effects' settings (synthsFx.h) become Synth FX's slots.
// DCO6's Unison needs one hook in CE's host (InstrumentImpl::mtUnisonNoteOn), given its
// parameters by unisonParams below.
//
// The switch only shows or hides them: Off, the synth model picker leaves them out, and a
// part already playing one keeps playing it (its own model stays in the picker so the
// selection has a place). A project or patch that uses them opens either way, as long as the
// files are on the card. Unison works either way too. Nothing goes in mods.bin.
//
// Main loop only, but unisonParams, which runs in the audio interrupt.

#ifndef MODS_SYNTHS_SYNTHSMOD_H_
#define MODS_SYNTHS_SYNTHSMOD_H_

#include "mods.h"

#if MT_HAS_SYNTHS

#include <cstddef>
#include <cstdint>

namespace mods
{
namespace synths
{

// The registry gives every mod a mods.bin record id; Synths keeps nothing there.
constexpr uint8_t kRecord = static_cast<uint8_t>(Mod::synths) + 1;
constexpr uint8_t kRecordVersion = 0;

// WAVE's, PHASE's and DCO6's InstrumentTypeIds (patch files store them). psyn/Makefile puts the
// same numbers in the .psyn headers; synthsSdk.h has WAVE's and PHASE's for the engines, and
// psyn/dco6Synth.cpp has DCO6's.
constexpr int kWaveTypeId = 64;
constexpr int kPhaseTypeId = 65;
constexpr int kDco6TypeId = 66;

// One of the mod's engines
constexpr bool ours(int instrumentTypeId)
{
    return instrumentTypeId == kWaveTypeId || instrumentTypeId == kPhaseTypeId || instrumentTypeId == kDco6TypeId;
}

// The CE engines and plugins come before and after ours in the synth's model list
// (Synth::getSynthTypes), so the picker filters it. Whether it shows a model: all with the mod
// on; Off, all but ours, unless it is the part's own (current).
constexpr bool pickerShows(bool on, int instrumentTypeId, bool current)
{
    return on || current || !ours(instrumentTypeId);
}

// True for our engines' metadata: the patch editor's tiles show their sections' short names,
// and a control that renames others (invalidatesOtherMetadata) rebuilds the list at once.
constexpr bool ownsLayout(int instrumentTypeId)
{
    return ours(instrumentTypeId);
}

// WAVE and PHASE: stereo out with both sides the same (Synth FX takes them as mono), and the
// effect parameters of the builds before Synth FX, which Synth FX converts once. Not DCO6: its
// sides differ (Unison's Width) and it never had those parameters.
constexpr bool sidesAlike(int instrumentTypeId)
{
    return instrumentTypeId == kWaveTypeId || instrumentTypeId == kPhaseTypeId;
}

// Unison (docs/mt/VA_SYNTH_PLAN.md, section 3.1): the engines whose notes CE's host gives to a
// stack of voices, with the parameter that sets the stack's size (1-8) and the one, in no
// section, that tells each member its place (slot + 16 * size) before its note-on.
// Set as polysynth::mtUnisonLookup at start (Synth::init); it runs in the audio interrupt.
constexpr int kDco6UnisonParam = 27;
constexpr int kDco6PlaceParam = 33;

inline bool unisonParams(int instrumentTypeId, int& unisonParam, int& placeParam)
{
    if (instrumentTypeId != kDco6TypeId)
    {
        return false;
    }
    unisonParam = kDco6UnisonParam;
    placeParam = kDco6PlaceParam;
    return true;
}

} // namespace synths
} // namespace mods

#endif // MT_HAS_SYNTHS

#endif // MODS_SYNTHS_SYNTHSMOD_H_
