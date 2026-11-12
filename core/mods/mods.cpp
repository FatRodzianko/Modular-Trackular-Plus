// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The firmware side: CE's FileSystem behind mods::FileIo, the settings buffers, and the
// hook functions CE's file manager calls.

#include "modsBuild.h"

#if MT_MODS && !defined(MT_HOST_TEST)

#include "mods.h"

#include "colorsong.h"
#include "copypattern.h"
#include "lockupRecorder.h"
#include "masterfx.h"
#include "modsConfig.h"
#include "modsProject.h"
#include "modsRegistry.h"
#include "modsSwitches.h"
#include "songloop.h"
#include "swing.h"
#include "synthfx/synthfxMod.h"
#include "synthfx/synthfxPlugins.h"
#include "tracklen.h"

#include "Platform.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "mtStructs.h"

#include <algorithm>
#include <cr_section_macros.h>
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace mods
{

namespace
{

bool lentNow = false; // USB storage mode: the computer has the card (cardLent)

class CeFileIo : public FileIo
{
  public:
    FileSystem* fileSystem = nullptr;

    // the card, or none while it is lent
    bool usable() const
    {
        return fileSystem != nullptr && !lentNow;
    }

    size_t read(const char* path, uint8_t* buffer, size_t capacity) override
    {
        if (!usable() || !fileSystem->exists(path))
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
        if (!usable())
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
        if (usable() && fileSystem->exists(path))
        {
            fileSystem->remove(path);
        }
    }

    void makeDir(const char* path) override
    {
        if (usable() && !fileSystem->exists(path))
        {
            fileSystem->mkdir(0, path);
        }
    }

    bool exists(const char* path) override
    {
        return usable() && fileSystem->exists(path);
    }

    size_t list(const char* dir, const char* suffix, char* names, size_t count, size_t nameSize) override
    {
        if (!usable() || count == 0 || nameSize < 2 || !fileSystem->exists(dir))
        {
            return 0;
        }
        int found = static_cast<int>(std::min(count, kListMax));
        fileSystem->listDirContents(
          dir,
          FileSystem::DirContentType::kFiles,
          [suffix](const char* name) {
              const size_t length = strlen(name);
              const size_t suffixLength = strlen(suffix);
              return length > suffixLength && strcasecmp(name + length - suffixLength, suffix) == 0;
          },
          listNames,
          static_cast<int>(sizeof(listNames)),
          static_cast<int>(std::min(nameSize - 1, kListNameMax)),
          found,
          listEntries);
        const size_t n = found > 0 ? static_cast<size_t>(found) : 0;
        for (size_t i = 0; i < n; i++)
        {
            snprintf(names + i * nameSize, nameSize, "%s", listEntries[i]);
        }
        return n;
    }

    // listDirContents' working space (main loop): names packed one after another, and where
    // each starts
    static constexpr size_t kListMax = 64;
    static constexpr size_t kListNameMax = 63;
    static char listNames[kListMax * (kListNameMax + 1)];
    static char* listEntries[kListMax];
};

__NOINIT(EXTERNAL_RAM) char CeFileIo::listNames[CeFileIo::kListMax * (CeFileIo::kListNameMax + 1)];
__NOINIT(EXTERNAL_RAM) char* CeFileIo::listEntries[CeFileIo::kListMax];

constexpr size_t kConfigBufferSize = 512;
// Color Song's record is the largest, up to 5,124, then Variable Track Length's, up to 4,335.
constexpr size_t kProjectBufferSize = 10496;

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
    registry::init();
    project::setChangedCallback(markProjectChanged);
    config::load(fileIo, configStore, defaults.data(), kModCount);
#if MT_HAS_SYNTHFX
    if (defaults[static_cast<size_t>(Mod::synthfx)] != 0)
    {
        synthfx::startPlugins(fileIo); // the pack onto the card, the list
    }
#endif
}

void cardLent(bool lent)
{
    lentNow = lent;
}

bool cardIsLent()
{
    return lentNow;
}

void update()
{
    if (lentNow)
    {
        return; // USB storage mode: picks, the start marker and the slots wait for the card
    }
#if MT_HAS_SYNTHFX
    if (!synthfx::plugins::started())
    {
        if (on(Mod::synthfx)) // turned on since boot, or a project that has it
        {
            synthfx::startPlugins(fileIo);
        }
    }
    else
    {
        synthfx::plugins::update();
        synthfx::updateSlots(); // a project's slots made, old plugins let go
    }
    synthfx::convertOldPatches(); // WAVE's and PHASE's old patch effects into their slots, On or Off
#endif
}

void defaultsChanged()
{
    config::save(fileIo, configStore, defaults.data(), kModCount);
}

void projectSwitchChanged()
{
    project::setSwitches(projectStore, switches.data(), kModCount);
}

#if MT_HAS_COPYPATTERN
void copypatternPressed()
{
    copypattern::press(copypattern::ceSide());
}

void copypatternUpdate()
{
    copypattern::update(copypattern::ceSide());
}
#endif

#if MT_HAS_SWING
void projectSwingSwitchChanged()
{
    projectSwitchChanged();
    swing::switchEdited();
}
#endif

#if MT_HAS_MASTERFX
void projectMasterfxSwitchChanged()
{
    projectSwitchChanged();
    masterfx::apply();
}

void masterfxStarted()
{
    masterfx::started();
#if MT_LOCKUP_RECORDER
    lockup::arm();
#endif
}

void masterfxChosen()
{
    if (masterfx::storeIfEdited(projectStore))
    {
        project::markChanged();
    }
    masterfx::applySoon();
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

#if MT_HAS_TRACKLEN
void projectTracklenEdited()
{
    if (tracklen::store(projectStore))
    {
        project::markChanged();
    }
}

void tracklenTracksPasted(uint8_t sourcePattern, uint8_t destPattern, uint8_t sourceTrack, uint8_t destTrack, uint8_t tracks)
{
    if (on(Mod::tracklen) && tracklen::copyTracks(sourcePattern, destPattern, sourceTrack, destTrack, tracks))
    {
        projectTracklenEdited();
    }
}
#endif

#if MT_HAS_SONGLOOP
void projectSongloopSwitchChanged()
{
    projectSwitchChanged();
    songloop::switchEdited();
}
#endif

void projectSettingEdited()
{
    registry::settingEdited(projectStore);
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
    registry::workspaceLoaded(projectStore);
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
