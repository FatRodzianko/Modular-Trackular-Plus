// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Device-wide mods settings, kept in Mods/mods.cfg on the SD card (not in CE's config,
// so upstream config changes never collide with ours). Its switches record is the mod
// set New Project starts with (Config -> Mods). A missing or damaged file means every
// mod Off.

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

// Reads the file into store and the defaults from it (count mods).
void load(FileIo& io, Store& store, uint8_t* defaults, size_t count);

// Writes the defaults, keeping any other records (and defaults of mods this build
// doesn't know) as they were.
bool save(FileIo& io, Store& store, const uint8_t* defaults, size_t count);

} // namespace config
} // namespace mods

#endif /* MODS_MODSCONFIG_H_ */
