// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// chords: chord inversions for the MIDI Chord FX. CE's Chord FX values 0..29 are its
// own chords; this mod gives values 30..69 chords of their own (the same numbers as
// the 1.2.0 patch, since they are saved in patterns): the major and minor triad
// inversions, the maj7, min7 and dom7 inversions, then the other inversions of CE's
// chords. 70..255 stay free and play as CE plays them (the note alone).
//
// A chord is an interval mask like CE's chordDef: bit i set means "also play the
// step's note + i". CE plays the notes, ends them and arpeggiates them from the mask,
// so the hooks only hand CE the mask (getChordBitmask, getChordBitmaskForArp). The
// encoder goes on past 29 into these chords; pads, Fill and random values keep CE's
// range. The grid shows a chord's intervals the way CE labels its own chords.
//
// Off: CE exactly. The encoder stops at 29, values past it play the note alone.

#ifndef MODS_CHORDS_H_
#define MODS_CHORDS_H_

#include "mods.h"

#if MT_HAS_CHORDS

namespace mods
{
namespace chords
{

constexpr uint8_t kFirst = 30; // after CE's chords 0..29
constexpr uint8_t kLast = 69;
constexpr size_t kCount = kLast - kFirst + 1;

extern const uint16_t kLibrary[kCount];

// A value's mask, or 0 when CE plays it (mod Off, or a value outside kFirst..kLast).
inline uint16_t bitmask(uint8_t value)
{
    if (value < kFirst || value > kLast || !on(Mod::chords))
    {
        return 0;
    }
    return kLibrary[value - kFirst];
}

// The cell text for a value with a mask (" 59", "368"): the intervals in hex digits,
// right-aligned in 3 characters. False when CE labels the value itself.
bool label(char* text, int length, uint8_t value);

} // namespace chords
} // namespace mods

#endif // MT_HAS_CHORDS

#endif /* MODS_CHORDS_H_ */
