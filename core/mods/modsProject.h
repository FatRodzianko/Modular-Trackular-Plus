// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Per-project mods: one mods.bin per project, holding which mods are On (the
// switches record) and their settings, carried the way CE carries synth patches.
// While a project is open the file lives in Workspace/mods.bin and in RAM; Open copies
// the project's file in, Save writes it out to the project folder (Save As included),
// New Project starts from an empty Workspace and the defaults from mods.cfg.
//
// A project saved by this firmware always has a mods.bin. A project without one is
// from stock 1.2.0 or CE: it opens with every mod Off, except mods whose 1.2.0 side
// files are found; those are imported once, when it is loaded.

#ifndef MODS_MODSPROJECT_H_
#define MODS_MODSPROJECT_H_

#include "modsFileIo.h"
#include "modsStore.h"

namespace mods
{
namespace project
{

constexpr char kFileName[] = "mods.bin";
constexpr char kWorkspaceFile[] = "Workspace/mods.bin";
constexpr char kMagic[] = "MTP1";
constexpr size_t kPathSize = 256;

// Bit n set: mod n's importer has had its turn in this project.
constexpr uint8_t kImportsRecord = 240;

// Reads one 1.2.0 side file from projectFolder into store's records. True if the
// project used the mod, which then starts On.
using Importer = bool (*)(FileIo& io, const char* projectFolder, Store& store);

struct ImporterEntry
{
    uint8_t mod; // the mod's id
    Importer import;
};

// Called when settings change, so the project shows as unsaved.
void setChangedCallback(void (*callback)());

// Open: Workspace/mods.bin becomes a copy of the project's, or is removed if the
// project has none (or a damaged one).
void copyIn(FileIo& io, Store& store, const char* projectFolder);

// Workspace load (boot, Open, New Project): RAM from Workspace/mods.bin, and the
// project's set into switches (count mods). Without the file: a project never saved
// (projectFolder nullptr, so New Project) takes the defaults; a saved one starts all
// Off and runs the importers. The result is written to the Workspace. A mods.bin from a
// build that didn't have a mod yet: that mod's importer still gets its one turn.
void loadFromWorkspace(FileIo& io,
                       Store& store,
                       const char* projectFolder,
                       const uint8_t* defaults,
                       uint8_t* switches,
                       size_t count);

// Workspace save: writes RAM to Workspace/mods.bin if it changed.
void saveToWorkspace(FileIo& io, Store& store);

// Project save: writes RAM to <projectFolder>/mods.bin, replacing what was there.
bool saveToProject(FileIo& io, Store& store, const char* projectFolder);

// Project Settings -> Mods: records the project's set and marks the project changed.
bool setSwitches(Store& store, const uint8_t* switches, size_t count);

// For mods: call after changing a record.
void markChanged();
// The same for a record that follows other changes, which already marked the project: it goes
// out with the next Workspace save, without marking the project changed.
void markForWorkspaceSave();
bool isDirty();

// For tests: the importers run by loadFromWorkspace (default: the built-in list).
void setImporters(const ImporterEntry* importers, size_t count);

} // namespace project
} // namespace mods

#endif /* MODS_MODSPROJECT_H_ */
