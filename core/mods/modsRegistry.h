// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The registry: one descriptor per mod in the build, in flash. mods.cpp walks it for the
// events any mod may use, instead of naming each mod under its own #if. A mod left out of
// the build (MT_HAS_<NAME>=0) has no descriptor.
//
// Within each event the mods run in the table's order, which keeps the order mods.cpp
// had before the registry: Humanize, Global Swing, Master FX, Color Song, Copy to New,
// Track Length.
// Main loop only, every event: none of them is called from the audio interrupt.

#ifndef MODS_MODSREGISTRY_H_
#define MODS_MODSREGISTRY_H_

#include "mods.h"

#if MT_MODS

#include "modsStore.h"

#include <array>
#include <cstdint>

namespace mods
{

struct ModDescriptor
{
    Mod id;               // fixed, mods.h
    const char* name;     // as Config shows it
    uint8_t record;       // its mods.bin record id: id + 1
    uint8_t recordVersion;

    // Events. nullptr: the mod doesn't use it.
    void (*init)();                          // boot, before mods.cfg is read
    void (*load)(const Store& project);      // Workspace loaded: the project's record in
    void (*apply)();                         // Workspace loaded, after load: takes effect
    void (*reset)();                         // Workspace loaded, after apply: the last project's state out
    bool (*storeIfEdited)(Store& project);   // after a Config edit: true if the record changed
    void (*stored)();                        // after storeIfEdited stored a change
};

namespace registry
{

extern const std::array<ModDescriptor, kBuiltModCount> kMods;

// Each walks kMods in order.
void init();
void workspaceLoaded(const Store& projectStore);
// For each record stored: the project is marked changed, then the mod's stored().
void settingEdited(Store& projectStore);

} // namespace registry
} // namespace mods

#endif // MT_MODS

#endif /* MODS_MODSREGISTRY_H_ */
