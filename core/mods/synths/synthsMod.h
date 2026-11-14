// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// synths: WAVE and PHASE, two synth engines, from Modular-Trackular's synths patch (sy9),
// as .psyn plugins built with CE's synth-plugin-sdk (psyn/: waveSynth.cpp, phaseSynth.cpp,
// psyn/Makefile). The firmware has none of their code: PsynLoader finds WAVE.psyn and
// PHASE.psyn in /Synths on the card with CE's engines, at the type ids below. Their old
// effects' settings (synthsFx.h) become Synth FX's slots.
//
// The switch only shows or hides them: Off, the synth model picker leaves them out, and a
// part already playing WAVE or PHASE keeps playing it (its own model stays in the picker so
// the selection has a place). A project or patch that uses them opens either way, as long as
// the two files are on the card. Nothing goes in mods.bin.
//
// Main loop only.

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

// WAVE's and PHASE's InstrumentTypeIds (patch files store them). psyn/Makefile puts the same
// numbers in the .psyn headers, and synthsSdk.h has them for the engines.
constexpr int kWaveTypeId = 64;
constexpr int kPhaseTypeId = 65;

// The CE engines and plugins come before and after WAVE and PHASE in the synth's model list
// (Synth::getSynthTypes), so the picker filters it. Whether it shows a model: all with the mod
// on; Off, all but WAVE and PHASE, unless it is the part's own (current).
constexpr bool pickerShows(bool on, int instrumentTypeId, bool current)
{
    return on || current || (instrumentTypeId != kWaveTypeId && instrumentTypeId != kPhaseTypeId);
}

// True for WAVE's and PHASE's metadata: the patch editor's tiles show their sections'
// short names, and a control that renames others (invalidatesOtherMetadata) rebuilds the
// list at once.
constexpr bool ownsLayout(int instrumentTypeId)
{
    return instrumentTypeId == kWaveTypeId || instrumentTypeId == kPhaseTypeId;
}

} // namespace synths
} // namespace mods

#endif // MT_HAS_SYNTHS

#endif // MODS_SYNTHS_SYNTHSMOD_H_
