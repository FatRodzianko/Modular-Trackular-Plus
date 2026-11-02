// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Per-project mod settings: one mods.bin per project, carried the way CE carries
// synth patches. While a project is open its settings live in Workspace/mods.bin and
// in RAM; Open copies the project's file in, Save writes it out to the project folder
// (Save As included), New Project starts from an empty Workspace.
//
// A project saved by this firmware always has a mods.bin, even with no settings in
// it. A project without one is from stock 1.2.0 or CE: its 1.2.0 side files are
// imported once, when it is loaded.

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

// Reads one 1.2.0 side file from projectFolder into store's records.
using Importer = void (*)(FileIo& io, const char* projectFolder, Store& store);

// Called when settings change, so the project shows as unsaved.
void setChangedCallback(void (*callback)());

// Open: Workspace/mods.bin becomes a copy of the project's, or is removed if the
// project has none (or a damaged one).
void copyIn(FileIo& io, Store& store, const char* projectFolder);

// Workspace load (boot, Open, New Project): RAM from Workspace/mods.bin. Without one,
// runs the importers on projectFolder (nullptr for a project never saved) and writes
// the result to the Workspace.
void loadFromWorkspace(FileIo& io, Store& store, const char* projectFolder);

// Workspace save: writes RAM to Workspace/mods.bin if it changed.
void saveToWorkspace(FileIo& io, Store& store);

// Project save: writes RAM to <projectFolder>/mods.bin, replacing what was there.
bool saveToProject(FileIo& io, Store& store, const char* projectFolder);

// For mods: call after changing a record.
void markChanged();
bool isDirty();

// For tests: the importers run by loadFromWorkspace (default: the built-in list).
void setImporters(const Importer* importers, size_t count);

} // namespace project
} // namespace mods

#endif /* MODS_MODSPROJECT_H_ */
