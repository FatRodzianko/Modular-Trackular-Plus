// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The mod registry and the calls CE's code makes into the mods. Every hook in a CE
// file is marked "MT:" and sits behind MT_MODS, so other builds stay plain CE.

#ifndef MODS_MODS_H_
#define MODS_MODS_H_

#include "modsBuild.h"

#if MT_MODS

#include <array>
#include <cstddef>
#include <cstdint>

class FileSystem;

namespace mods
{

// Fixed ids, stored in mods.cfg; never reuse or renumber one.
enum class Mod : uint8_t
{
    kCount
};

constexpr size_t kModCount = static_cast<size_t>(Mod::kCount);

// One byte per mod, 0 Off or 1 On, so Config list items can point at them.
extern std::array<uint8_t, kModCount> switches;

inline bool on(Mod mod)
{
    return switches[static_cast<size_t>(mod)] != 0;
}

// Shown in Config -> Mods.
constexpr char kModCountText[] = "0";

// Boot, after the file manager: loads the switches from mods.cfg.
void init(FileSystem* fileSystem);

// Config action for every switch item: saves mods.cfg.
void switchChanged();

// Project file hooks, called from CE's file manager.
void onProjectOpenCopy();  // ProjectOpen, after the Workspace is filled from the project
void onWorkspaceLoaded();  // WorkspaceProjectLoad finish (boot, Open, New Project)
void onWorkspaceSaved();   // WorkspaceProjectSave finish
void onProjectSaved();     // ProjectSave finish (Save and Save As)

} // namespace mods

#endif // MT_MODS

#endif /* MODS_MODS_H_ */
