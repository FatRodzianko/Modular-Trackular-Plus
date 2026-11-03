// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The switches record: one Off/On byte per mod, indexed by mod id. Record 0 of
// mods.bin holds the project's mod set; record 0 of mods.cfg the set New Project
// starts with.

#ifndef MODS_MODSSWITCHES_H_
#define MODS_MODSSWITCHES_H_

#include "modsStore.h"

namespace mods
{

constexpr uint8_t kSwitchesRecord = 0;
constexpr uint8_t kSwitchesVersion = 1;

// Fills values[0..count) from store's switches record. A mod the record doesn't
// cover is Off.
void readSwitches(const Store& store, uint8_t* values, size_t count);

// Writes values[0..count) into store's switches record, keeping the bytes past count
// (mods this build doesn't know). False (record unchanged) if it doesn't fit.
bool writeSwitches(Store& store, const uint8_t* values, size_t count);

} // namespace mods

#endif /* MODS_MODSSWITCHES_H_ */
