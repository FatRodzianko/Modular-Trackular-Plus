// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Keeps the mods' own card access (Color Song's reads, Copy to New's check and write) away
// from the sequencer's next-pattern load during playback.
//
// When song mode or a queued pattern change is playing, the sequencer asks for the next
// pattern on track 0's last step, in the interrupt, and the main loop must load it from the
// card before that step ends; otherwise the sequencer stops the song. A mod access that is
// already running when the request comes holds the load back. So the mods start an access
// only when the request is at least kLoadGuardMs away, or no load is coming at all.
//
// Main loop only. Not covered: a break-pattern FX or a tempo FX that speeds up later in the
// pattern makes the request come sooner than estimated.

#ifndef MODS_MODSCARDGUARD_H_
#define MODS_MODSCARDGUARD_H_

#include <cstdint>

namespace mods
{

// Longer than one mod access takes: one 16 KB pattern read or write, with room for an SD
// card's slow writes.
constexpr float kLoadGuardMs = 200.0f;

// Swing makes a step as short as 75 % of the plain step (CE's strSwing, 25 to 75).
constexpr float kShortestStepShare = 0.75f;

// The pure part, host-tested. lastStep is track 0's length byte (the last step's index), pos
// its current step. True when the next-pattern request could come within guardMs.
inline bool loadRequestNear(uint8_t lastStep, uint8_t pos, float tempo, float guardMs)
{
    if (pos >= lastStep)
    {
        return true; // asked for on this step, or the length changed under it
    }
    if (!(tempo > 0.0f))
    {
        return true;
    }
    // Four steps per beat. The step playing now may be nearly over, so it counts for nothing.
    const float shortestStepMs = kShortestStepShare * 15000.0f / tempo;
    return static_cast<float>(lastStep - pos - 1) * shortestStepMs < guardMs;
}

// Firmware: true when a mod may start a card access now. False while a pattern load is
// requested or running, or when the next request is near. Treats performance mode and
// selection playback as always near when a load is coming, since their ends aren't
// estimated: the mods then wait until playback stops.
bool cardFreeOfPatternLoad();

} // namespace mods

#endif // MODS_MODSCARDGUARD_H_
