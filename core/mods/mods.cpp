// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The firmware side: CE's FileSystem behind mods::FileIo, the settings buffers, and the
// hook functions CE's file manager calls.

#include "modsBuild.h"

#if MT_MODS && !defined(MT_HOST_TEST)

#include "mods.h"

#include "colorsong.h"
#include "humanize.h"
#include "modsConfig.h"
#include "modsProject.h"
#include "modsSwitches.h"
#include "swing.h"

#include "Platform.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "mtStructs.h"

#include <algorithm>
#include <cr_section_macros.h>
#include <cstdio>

namespace mods
{

namespace
{

class CeFileIo : public FileIo
{
  public:
    FileSystem* fileSystem = nullptr;

    size_t read(const char* path, uint8_t* buffer, size_t capacity) override
    {
        if (fileSystem == nullptr || !fileSystem->exists(path))
        {
            return kReadFailed;
        }

        auto [isOpen, file] = fileSystem->openFile(path);
        if (!isOpen)
        {
            return kReadFailed;
        }

        const size_t size = fileSystem->getFileSize(file);
        const size_t wanted = std::min(size, capacity);
        const int32_t got = fileSystem->readFile(buffer, wanted, file); // < 0: error
        fileSystem->closeFile(file);

        return got >= 0 && static_cast<size_t>(got) == wanted ? size : kReadFailed;
    }

    bool write(const char* path, const uint8_t* data, size_t length) override
    {
        if (fileSystem == nullptr)
        {
            return false;
        }

        auto [isOpen, file] =
          fileSystem->openFile(path, FileSystem::FileOpenMode::kWriteCreateNew);
        if (!isOpen)
        {
            return false;
        }

        const int32_t written = fileSystem->writeFile(data, length, file); // < 0: error
        fileSystem->closeFile(file);
        return written >= 0 && static_cast<size_t>(written) == length;
    }

    void remove(const char* path) override
    {
        if (fileSystem != nullptr && fileSystem->exists(path))
        {
            fileSystem->remove(path);
        }
    }

    void makeDir(const char* path) override
    {
        if (fileSystem != nullptr && !fileSystem->exists(path))
        {
            fileSystem->mkdir(0, path);
        }
    }
};

constexpr size_t kConfigBufferSize = 512;
constexpr size_t kProjectBufferSize = 6144; // Color Song's record is the largest, up to 5,124

__NOINIT(EXTERNAL_RAM) uint8_t configBuffer[kConfigBufferSize];
__NOINIT(EXTERNAL_RAM) uint8_t projectBuffer[kProjectBufferSize];

CeFileIo fileIo;
Store configStore(configBuffer, kConfigBufferSize, config::kMagic);
Store projectStore(projectBuffer, kProjectBufferSize, project::kMagic);

void markProjectChanged()
{
    mtProject.values.projectNotSavedFlag |= 1;
    newFileManager.setProjectStructChanged();
}

// The open project's folder, or nullptr for a project never saved (empty path).
const char* currentProjectFolder(char* path)
{
    if (newFileManager.getCurrentProjectPath()[0] == 0)
    {
        return nullptr;
    }
    const int length = snprintf(path,
                                project::kPathSize,
                                cProjectsPathFormat,
                                newFileManager.getCurrentProjectPath(),
                                newFileManager.getCurrentProjectName());
    if (length <= 0 || static_cast<size_t>(length) >= project::kPathSize)
    {
        return nullptr;
    }
    return path;
}

} // namespace

void init(FileSystem* fileSystem)
{
    fileIo.fileSystem = fileSystem;
    configStore.clear();
    projectStore.clear();
#if MT_HAS_COLORSONG
    colorsong::init();
#endif
    project::setChangedCallback(markProjectChanged);
    config::load(fileIo, configStore, defaults.data(), kModCount);
}

void defaultsChanged()
{
    config::save(fileIo, configStore, defaults.data(), kModCount);
}

void projectSwitchChanged()
{
    project::setSwitches(projectStore, switches.data(), kModCount);
}

#if MT_HAS_SWING
void projectSwingSwitchChanged()
{
    projectSwitchChanged();
    swing::switchEdited();
}
#endif

#if MT_HAS_COLORSONG
void projectColorsongSwitchChanged()
{
    projectSwitchChanged();
    colorsong::switchEdited();
}

void colorsongUpdate()
{
    if (colorsong::update(colorsong::ceSong(), projectStore))
    {
        project::markForWorkspaceSave();
    }
}
#endif

void projectSettingEdited()
{
#if MT_HAS_HUMANIZE
    if (humanize::storeIfEdited(projectStore))
    {
        project::markChanged();
    }
#endif
#if MT_HAS_SWING
    if (swing::storeIfEdited(projectStore))
    {
        project::markChanged();
        swing::amountEdited();
    }
#endif
}

void onProjectOpenCopy()
{
    char path[project::kPathSize];
    const char* folder = currentProjectFolder(path);
    if (folder == nullptr)
    {
        fileIo.remove(project::kWorkspaceFile);
        return;
    }
    project::copyIn(fileIo, projectStore, folder);
}

void onWorkspaceLoaded()
{
    char path[project::kPathSize];
    project::loadFromWorkspace(fileIo,
                               projectStore,
                               currentProjectFolder(path),
                               defaults.data(),
                               switches.data(),
                               kModCount);
#if MT_HAS_HUMANIZE
    humanize::load(projectStore);
#endif
#if MT_HAS_SWING
    swing::load(projectStore);
#endif
#if MT_HAS_COLORSONG
    colorsong::reset();
#endif
}

void onWorkspaceSaved()
{
    project::saveToWorkspace(fileIo, projectStore);
}

void onProjectSaved()
{
    char path[project::kPathSize];
    const char* folder = currentProjectFolder(path);
    if (folder != nullptr)
    {
        project::saveToProject(fileIo, projectStore, folder);
    }
}

} // namespace mods

#endif // MT_MODS && !MT_HOST_TEST
