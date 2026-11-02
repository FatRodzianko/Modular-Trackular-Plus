// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "modsBuild.h"

#if MT_MODS

#include "modsProject.h"

#include <cstdio>

namespace mods
{
namespace project
{

namespace
{

// 1.2.0 side-file importers (swing.bin, humanize.bin …), added with each mod.
constexpr Importer kBuiltInImporters[] = { nullptr };

const Importer* importers = kBuiltInImporters;
size_t importerCount = sizeof(kBuiltInImporters) / sizeof(kBuiltInImporters[0]);

bool dirty = false;
void (*changedCallback)() = nullptr;

bool projectFilePath(char* path, const char* projectFolder)
{
    const int length = snprintf(path, kPathSize, "%s/%s", projectFolder, kFileName);
    return length > 0 && static_cast<size_t>(length) < kPathSize;
}

} // namespace

void setChangedCallback(void (*callback)())
{
    changedCallback = callback;
}

void setImporters(const Importer* list, size_t count)
{
    importers = list;
    importerCount = count;
}

void copyIn(FileIo& io, Store& store, const char* projectFolder)
{
    char path[kPathSize];
    size_t size = FileIo::kReadFailed;
    if (projectFilePath(path, projectFolder))
    {
        size = io.read(path, store.image(), store.capacity());
    }

    if (size != FileIo::kReadFailed && store.validate(size))
    {
        io.write(kWorkspaceFile, store.image(), size);
    }
    else
    {
        io.remove(kWorkspaceFile);
    }
    store.clear();
}

void loadFromWorkspace(FileIo& io, Store& store, const char* projectFolder)
{
    dirty = false;

    const size_t size = io.read(kWorkspaceFile, store.image(), store.capacity());
    if (size != FileIo::kReadFailed && store.validate(size))
    {
        return;
    }

    store.clear();
    if (projectFolder != nullptr)
    {
        for (size_t i = 0; i < importerCount; i++)
        {
            if (importers[i] != nullptr)
            {
                importers[i](io, projectFolder, store);
            }
        }
    }

    io.write(kWorkspaceFile, store.image(), store.finalize());
}

void saveToWorkspace(FileIo& io, Store& store)
{
    if (!dirty)
    {
        return;
    }
    if (io.write(kWorkspaceFile, store.image(), store.finalize()))
    {
        dirty = false;
    }
}

bool saveToProject(FileIo& io, Store& store, const char* projectFolder)
{
    char path[kPathSize];
    if (!projectFilePath(path, projectFolder))
    {
        return false;
    }
    return io.write(path, store.image(), store.finalize());
}

void markChanged()
{
    dirty = true;
    if (changedCallback != nullptr)
    {
        changedCallback();
    }
}

bool isDirty()
{
    return dirty;
}

} // namespace project
} // namespace mods

#endif // MT_MODS
