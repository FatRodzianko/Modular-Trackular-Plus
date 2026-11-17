// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// MT: Unison for an engine of ours (core/mods/synths/synthsMod.h, docs/mt/VA_SYNTH_PLAN.md 3.1).
// This library is built once for every product, without the mods' build switches, so the hook in
// InstrumentImpl::handleNoteOn asks through this pointer. It stays null in CE's builds (Tracker
// Mini, the Original Tracker, the plain-CE reference build), and then every note goes CE's way.
// The Synths mod sets it once at start, in the main loop, before any note.
//
// The lookup runs in the audio interrupt (note-on): no heap, no blocking. It answers true for an
// engine whose notes take a stack of voices, with the parameter that sets the stack's size (1-8)
// and the one that tells each member its place (slot + 16 * size) before its note-on.

#ifndef POLYSYNTH_MTUNISON_H_
#define POLYSYNTH_MTUNISON_H_

namespace polysynth
{

using MtUnisonLookup = bool (*)(int instrumentTypeId, int& unisonParam, int& placeParam);

extern MtUnisonLookup mtUnisonLookup;

} // namespace polysynth

#endif // POLYSYNTH_MTUNISON_H_
