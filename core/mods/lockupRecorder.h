// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Diagnostic builds only (CUSTOM_DEFINES=-DMT_LOCKUP_RECORDER=1): makes a lockup explain itself.
//
// A fault, or a main loop that has stopped running, is written down and the unit restarts, so nobody
// has to pull the plug. The next start puts what was written down in /mfx_log.txt on the card, along
// with one line for each time Play ran. See docs/mt/DEVICE_TEST_LADDER.md.
//
// The other builds compile none of this.

#ifndef MODS_LOCKUPRECORDER_H_
#define MODS_LOCKUPRECORDER_H_

#ifndef MT_LOCKUP_RECORDER
#define MT_LOCKUP_RECORDER 0
#endif

#if MT_LOCKUP_RECORDER

#include <cstdint>

extern "C"
{
    void mtLockupBeat();                 // the main loop, each pass
    void mtLockupTick(uint32_t* frame);  // SysTick, 1 kHz, above the audio interrupt
    void mtLockupFault(uint32_t kind, uint32_t* frame);
}

namespace mods
{
namespace lockup
{

void audioStart(); // the audio interrupt, first thing
void audioStop();  // the audio interrupt, last thing
void arm();        // once the Pattern screen is up; a main loop may stall before that
void update();     // main loop: writes the log

} // namespace lockup
} // namespace mods

#endif // MT_LOCKUP_RECORDER

#endif /* MODS_LOCKUPRECORDER_H_ */
