// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// synths: WAVE and PHASE, two synth engines, from Modular-Trackular's synths patch (sy9),
// as CE Synth SDK engines (waveSynth.cpp, phaseSynth.cpp), each with a Distortion and a
// Chorus, Flanger or Phaser (synthsFx.h).
//
// The switch only shows or hides them: Off, the synth model picker ends at CE's last
// model, and a part already playing WAVE or PHASE keeps playing it (its own model stays in
// the picker so the selection has a place). The engines are always registered, so a
// project or patch that uses them opens either way. Nothing goes in mods.bin.
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

// WAVE's and PHASE's InstrumentTypeIds (patch files store them)
constexpr int kWaveTypeId = 64;
constexpr int kPhaseTypeId = 65;

// The models the mod adds at the end of Synth::kSynthTypes
constexpr size_t kModels = 2;

// How many models the picker shows: all (total) with the mod on; Off, CE's (stock), or up
// to the part's own model if it is one of ours. current: the part's index into kSynthTypes.
constexpr size_t modelCount(bool on, size_t current, size_t stock, size_t total)
{
    if (on || total <= stock)
    {
        return total;
    }
    return current >= stock ? (current + 1 < total ? current + 1 : total) : stock;
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
