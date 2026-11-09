// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// WAVE's and PHASE's factories, for core/synth/registry.cpp, and their InstrumentTypeIds.

#ifndef MODS_SYNTHS_SYNTHSENGINES_H_
#define MODS_SYNTHS_SYNTHSENGINES_H_

#include "PolySynth/sdk/InstrumentType.h"
#include <memory>

namespace polysynth
{
std::unique_ptr<InstrumentType> createWaveSynth();
std::unique_ptr<InstrumentType> createPhaseSynth();
} // namespace polysynth

#endif // MODS_SYNTHS_SYNTHSENGINES_H_
