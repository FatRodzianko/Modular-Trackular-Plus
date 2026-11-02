// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Global mod switches, kept in Mods/mods.cfg on the SD card (not in CE's config, so
// upstream config changes never collide with ours). A missing or damaged file means
// every mod Off.

#ifndef MODS_MODSCONFIG_H_
#define MODS_MODSCONFIG_H_

#include "modsFileIo.h"
#include "modsStore.h"

namespace mods
{
namespace config
{

constexpr char kDir[] = "Mods";
constexpr char kPath[] = "Mods/mods.cfg";
constexpr char kMagic[] = "MTC1";

constexpr uint8_t kSwitchesRecord = 0;
constexpr uint8_t kSwitchesVersion = 1;

// Sets mods::switches from the file; leaves the file's records in store.
void load(FileIo& io, Store& store);

// Writes mods::switches, keeping any other records (and switches of mods this build
// doesn't know) as they were.
bool save(FileIo& io, Store& store);

} // namespace config
} // namespace mods

#endif /* MODS_MODSCONFIG_H_ */
