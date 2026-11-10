// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Where WAVE's and PHASE's effect settings sit: their parameters 62-71, read from the
// instrument's first voice, in synthfx/synthFx.h's order. The effects themselves are the
// shared ones there; each call takes p + P_EFFECT, the first of the ten.
//   62 Effect, 63 Rate, 64 Depth, 65 Mix, 66 Feedback, 67 Distortion, 68 Gain, 69 Tone,
//   70 Placement (WAVE: before or after its filter; PHASE: no effect), 71 Level

#ifndef MODS_SYNTHS_SYNTHSFX_H_
#define MODS_SYNTHS_SYNTHSFX_H_

#include "synthsDsp.h"
#include "synthfx/synthFx.h"

#include <cstdint>

namespace mtsynth
{

enum : uint32_t
{
    P_EFFECT = 62,
    P_RATE = P_EFFECT + FXP_RATE,
    P_DEPTH = P_EFFECT + FXP_DEPTH,
    P_MIX = P_EFFECT + FXP_MIX,
    P_FEEDBACK = P_EFFECT + FXP_FEEDBACK,
    P_DIST = P_EFFECT + FXP_DIST,
    P_GAIN = P_EFFECT + FXP_GAIN,
    P_TONE = P_EFFECT + FXP_TONE,
    P_PLACEMENT = P_EFFECT + FXP_PLACEMENT,
    P_LEVEL = P_EFFECT + FXP_LEVEL,
    P_COUNT = P_EFFECT + FXP_COUNT // 72: every engine parameter
};

} // namespace mtsynth

#endif // MODS_SYNTHS_SYNTHSFX_H_
