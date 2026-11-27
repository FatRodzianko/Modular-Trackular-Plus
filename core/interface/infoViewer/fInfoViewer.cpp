#include "infoViewer.h"

#include "FileBrowser.h"
#include "SdTextFile.h"
#include "TrackerImpl.h"
#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/songTimer.h"
#include "fileManager/FileOpsWorker.h"
#include "infoViewerDefs.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtStructs.h"
#include "util_math.h"
#include "util_string.h"

#include <algorithm>
#include <cr_section_macros.h>
#include <cstdio>
#include <cstring>
#include <strings.h>

extern uint8_t externalRamScratchBuff[];

cInfoViewer infoViewer;
static cInfoViewer* IV = &infoViewer;

namespace
{
// Scratch layout inside top 188 KB of externalRamScratchBuff ([580 KB .. 768 KB]).
// Active only while cInfoViewer is on screen (mutually exclusive with FileManagerModule).
static constexpr uint32_t kNamesBytes = kMaxFilesListLength * kMaxFileNameLen;
static constexpr uint32_t kPtrsBytes = kMaxFilesListLength * sizeof(char*);
static constexpr uint32_t kSiblingScratchBytes = 24 * 1024;
static_assert(kNamesBytes + kPtrsBytes <= kSiblingScratchBytes,
              "InfoViewer sibling list exceeds 24 KB scratch");

static constexpr uint32_t kMaxTextFileBytes = 128 * 1024;
static constexpr int kMaxTextLines = 8192;
static constexpr uint32_t kLineOffsetsBytes = kMaxTextLines * sizeof(uint32_t); // 32 KB
static constexpr uint32_t kViewWindowBytes = 4 * 1024;

static constexpr uint32_t kTotalViewerScratchBytes =
  kSiblingScratchBytes + kMaxTextFileBytes + kLineOffsetsBytes + kViewWindowBytes; // 188 KB
static constexpr uint32_t kViewerScratchBase =
  SHARED_SCRATCH_MEMORY_SIZE - kTotalViewerScratchBytes;
static_assert(kViewerScratchBase >= 320 * 1024, "InfoViewer scratch overlaps sdStream buffer");

static char* viewWindowBuf()
{
    return reinterpret_cast<char*>(externalRamScratchBuff + kViewerScratchBase +
                                   kSiblingScratchBytes + kMaxTextFileBytes + kLineOffsetsBytes);
}

static constexpr int kPageScrollStep = 10;

static void buildPathWithoutJpgExt(const char* dir,
                                   const char* fileName,
                                   char* outPath,
                                   int outSize)
{
    if (!FileOpsWorker::joinPath(dir, fileName, outPath, outSize))
    {
        outPath[0] = '\0';
        return;
    }

    size_t len = strlen(outPath);
    if (hasFileExtension(outPath, "jpeg"))
    {
        outPath[len - 5] = '\0';
    }
    else if (hasFileExtension(outPath, "jpg"))
    {
        outPath[len - 4] = '\0';
    }
}
} // namespace

static void functLeft()
{
    IV->previousFile();
    IV->refreshCurrentFile();
}

static void functRight()
{
    IV->nextFile();
    IV->refreshCurrentFile();
}

static void functUp()
{
    IV->scrollText(-1);
}

static void functDown()
{
    IV->scrollText(1);
}

static void functEncoder(int16_t value)
{
    value = interfaceGlobals.invertValForList ? -value : value;
    if (IV->getCurrentFileType() == cInfoViewer::PageType::kText && IV->totalTextLines > 1)
    {
        IV->scrollText(value);
        return;
    }

    if (value > 0)
    {
        IV->nextFile();
    }
    else if (value < 0)
    {
        IV->previousFile();
    }
    IV->refreshCurrentFile();
}

static void functActionButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
    {
        return;
    }

    if (button == interfaceButton0)
    {
        IV->previousFile();
        IV->refreshCurrentFile();
    }
    else if (button == interfaceButton2)
    {
        IV->scrollText(-kPageScrollStep);
    }
    else if (button == interfaceButton3)
    {
        IV->scrollText(kPageScrollStep);
    }
    else if (button == interfaceButton6)
    {
        IV->nextFile();
        IV->refreshCurrentFile();
    }
    else if (button == interfaceButton7)
    {
        IV->closeViewer();
    }
}

static void functSwitchModule(uint8_t button)
{
    IV->eventFunct(eventSwitchModule, IV, &button, 0);
}

static void functMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        mtMenuPopup.showModulePopup(IV);
    }
    else if (state == buttonRelease)
    {
        mtMenuPopup.hideModulePopup(IV);
    }
}

bool cInfoViewer::isImageFile(const char* name)
{
    if (name == nullptr || name[0] == '\0')
    {
        return false;
    }
    return FileBrowser::checkFilenameExtension(name, "jpg") ||
           FileBrowser::checkFilenameExtension(name, "jpeg");
}

bool cInfoViewer::isTextFile(const char* name)
{
    if (name == nullptr || name[0] == '\0')
    {
        return false;
    }
    return FileBrowser::checkFilenameExtension(name, "txt") ||
           FileBrowser::checkFilenameExtension(name, "md") ||
           FileBrowser::checkFilenameExtension(name, "json") ||
           FileBrowser::checkFilenameExtension(name, "ini") ||
           FileBrowser::checkFilenameExtension(name, "cfg") ||
           FileBrowser::checkFilenameExtension(name, "log");
}

bool cInfoViewer::isViewableFile(const char* name)
{
    return isImageFile(name) || isTextFile(name);
}

void cInfoViewer::openFile(const char* dirPath, const char* fileName, ModuleBase* callerModule)
{
    FileOpsWorker::normalizePath(dirPath, currentDir, sizeof(currentDir));
    if (fileName != nullptr)
    {
        const char* clean = (fileName[0] == '/') ? (fileName + 1) : fileName;
        polystrncpy(initialFileName, clean, sizeof(initialFileName));
    }
    else
    {
        initialFileName[0] = '\0';
    }
    returnModule = callerModule;

    if (callerModule != nullptr && callerModule != this)
    {
        mtInterface.deactivateModule(callerModule);
    }
    mtInterface.activateModule(this, 0);
}

void cInfoViewer::closeViewer()
{
    ModuleBase* target = returnModule;
    returnModule = nullptr;
    mtInterface.deactivateModule(this);
    if (target != nullptr)
    {
        mtInterface.activateModule(target, 0);
    }
    else
    {
        mtInterface.activateModule(mtInterface.getPreviousModule(), 0);
    }
}

void cInfoViewer::scanSiblingFiles()
{
    uint8_t* base = externalRamScratchBuff + kViewerScratchBase;
    char* namesBuf = reinterpret_cast<char*>(base);
    siblingFiles = reinterpret_cast<char**>(base + kNamesBytes);
    textFileBuf = reinterpret_cast<char*>(base + kSiblingScratchBytes);
    lineOffsets = reinterpret_cast<uint32_t*>(base + kSiblingScratchBytes + kMaxTextFileBytes);

    filesCount = kMaxFilesListLength;
    currentFileIndex = 0;
    totalTextLines = 0;
    scrollLine = 0;

    FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fs == nullptr)
    {
        filesCount = 0;
        return;
    }

    fs->listDirContents(
      currentDir,
      FileSystem::DirContentType::kFiles,
      [](const char* name) { return isViewableFile(name); },
      namesBuf,
      static_cast<int>(kNamesBytes),
      kMaxFileNameLen,
      filesCount,
      siblingFiles);

    if (filesCount > 1)
    {
        std::sort(siblingFiles,
                  siblingFiles + filesCount,
                  [](const char* a, const char* b) { return strcasecmp(a, b) < 0; });
    }

    if (initialFileName[0] != '\0')
    {
        for (int i = 0; i < filesCount; ++i)
        {
            if (strcasecmp(siblingFiles[i], initialFileName) == 0)
            {
                currentFileIndex = i;
                break;
            }
        }
    }
}

cInfoViewer::PageType cInfoViewer::getCurrentFileType() const
{
    if (filesCount <= 0 || currentFileIndex < 0 || currentFileIndex >= filesCount)
    {
        return PageType::kNone;
    }
    const char* name = siblingFiles[currentFileIndex];
    if (isImageFile(name))
    {
        return PageType::kImage;
    }
    if (isTextFile(name))
    {
        return PageType::kText;
    }
    return PageType::kNone;
}

void cInfoViewer::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;
    refreshImageState = 0;

    songTimer.hide();

    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonParams, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonGame, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMiniMenu, functMenu);

    scanSiblingFiles();
    showDefaultScreen();
    setDefaultScreenFunct();
    refreshCurrentFile();
}

void cInfoViewer::stop()
{
    moduleRefresh = 0;
    refreshImageState = 0;
    display.setControlHide(image);
    display.setControlHide(infoTextBox);
    mtPadBoard.releaseAllInstruments();
    padsBacklight.clearGlobalPads(1, 1, 1);

    songTimer.show();
}

void cInfoViewer::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);

    ip->setButtonObj(interfaceButton0, functActionButton);
    ip->setButtonObj(interfaceButton2, functActionButton);
    ip->setButtonObj(interfaceButton3, functActionButton);
    ip->setButtonObj(interfaceButton6, functActionButton);
    ip->setButtonObj(interfaceButton7, functActionButton);

    ip->setPotObj(interfacePot0, functEncoder);
}

void cInfoViewer::update()
{
    if (refreshImageState == 1)
    {
        if (display.getImgLoadState() == 0 || display.getImgLoadState() == 3)
        {
            if (filesCount <= 0 || currentFileIndex < 0 || currentFileIndex >= filesCount)
            {
                refreshImageState = 0;
                return;
            }

            char path[kMaxFilePathLen + 8]{};
            buildPathWithoutJpgExt(
              currentDir, siblingFiles[currentFileIndex], path, kMaxFilePathLen + 1);

            if (path[0] == '\0' || display.readImgFromSd(path, InfoViewerJpegAddress, 800, 390))
            {
                refreshImageState = 0;
                display.setControlHide(image);
            }
            else
            {
                refreshImageState = 2;
            }
        }
    }
    else if (refreshImageState == 2)
    {
        if (display.isImgLoaded())
        {
            display.setControlShow(image);
            display.refreshControl(image);
            refreshImageState = 0;
        }
    }
}

void cInfoViewer::buildLineIndex()
{
    totalTextLines = 0;
    scrollLine = 0;
    if (textFileBuf == nullptr || lineOffsets == nullptr || textFileBuf[0] == '\0')
    {
        return;
    }

    lineOffsets[totalTextLines++] = 0;
    for (uint32_t i = 0; textFileBuf[i] != '\0' && totalTextLines < kMaxTextLines; ++i)
    {
        if (textFileBuf[i] == '\n' && textFileBuf[i + 1] != '\0')
        {
            lineOffsets[totalTextLines++] = i + 1;
        }
    }
}

void cInfoViewer::refreshTextWindow()
{
    char* win = viewWindowBuf();
    constexpr size_t kWinMax = 3072;
    if (totalTextLines <= 0 || textFileBuf == nullptr || lineOffsets == nullptr)
    {
        polystrncpy(win, "(Empty file)", kWinMax);
        titleBar->showRightLabel(false);
    }
    else
    {
        scrollLine = clamp(scrollLine, 0, totalTextLines - 1);
        const char* src = textFileBuf + lineOffsets[scrollLine];
        polystrncpy(win, src, kWinMax);

        snprintf(scrollStatusText,
                 sizeof(scrollStatusText),
                 "L%d/%d",
                 scrollLine + 1,
                 totalTextLines);
        titleBar->showRightLabel(true);
        titleBar->setRightLabel(scrollStatusText);
    }

    display.setControlShow(infoTextBox);
    display.setControlText(infoTextBox, win);
    display.refreshControl(infoTextBox);
    updateActionButtons();
    display.synchronizeRefresh();
}

void cInfoViewer::updateActionButtons()
{
    const bool isText = (getCurrentFileType() == PageType::kText && totalTextLines > 1);

    display.setControlText(label[2], isText ? "PgUp" : "");
    display.setControlColors(label[2],
                             (isText && scrollLine > 0)
                               ? display.getTheme().activeButtonLabelsColors
                               : display.getTheme().inactiveLabelsColors);
    display.refreshControl(label[2]);

    display.setControlText(label[3], isText ? "PgDn" : "");
    display.setControlColors(label[3],
                             (isText && scrollLine < totalTextLines - 1)
                               ? display.getTheme().activeButtonLabelsColors
                               : display.getTheme().inactiveLabelsColors);
    display.refreshControl(label[3]);
}

void cInfoViewer::scrollText(int deltaLines)
{
    if (getCurrentFileType() != PageType::kText || totalTextLines <= 1 || deltaLines == 0)
    {
        return;
    }

    int next = clamp(scrollLine + deltaLines, 0, totalTextLines - 1);
    if (next == scrollLine)
    {
        return;
    }
    scrollLine = next;
    refreshTextWindow();
}

void cInfoViewer::refreshCurrentFile()
{
    if (filesCount <= 0 || currentFileIndex < 0 || currentFileIndex >= filesCount)
    {
        polystrncpy(titleText, "Viewer (No files)", sizeof(titleText));
        display.setControlHide(image);
        display.setControlHide(infoTextBox);
        display.refreshControl(infoTextBox);
        titleBar->show();
        titleBar->showRightLabel(false);
        titleBar->setTitle(titleText);
        display.synchronizeRefresh();
        return;
    }

    const char* name = siblingFiles[currentFileIndex];
    if (filesCount > 1)
    {
        snprintf(titleText,
                 sizeof(titleText),
                 "%.36s (%d/%d)",
                 name,
                 currentFileIndex + 1,
                 filesCount);
    }
    else
    {
        snprintf(titleText, sizeof(titleText), "%.48s", name);
    }

    titleBar->show();
    titleBar->setTitle(titleText);

    PageType type = getCurrentFileType();
    if (type == PageType::kText)
    {
        refreshImageState = 0;
        display.setControlHide(image);
        display.refreshControl(image);

        FileSystem* fs = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
        char fullPath[kMaxFilePathLen + 1]{};
        textFileBuf[0] = '\0';
        if (fs != nullptr && FileOpsWorker::joinPath(currentDir, name, fullPath, sizeof(fullPath)))
        {
            if (SdTextFile::readAll(fs, fullPath, textFileBuf, kMaxTextFileBytes))
            {
                SdTextFile::stripCr(textFileBuf);
            }
        }

        buildLineIndex();
        refreshTextWindow();
    }
    else if (type == PageType::kImage)
    {
        totalTextLines = 0;
        scrollLine = 0;
        titleBar->showRightLabel(false);
        display.setControlHide(infoTextBox);
        display.refreshControl(infoTextBox);
        display.setControlHide(image);
        updateActionButtons();

        refreshImageState = 1;
        display.processDisplayUpdateImmediate();
        display.synchronizeRefresh();
    }
}

void cInfoViewer::nextFile()
{
    if (filesCount <= 1)
    {
        return;
    }
    currentFileIndex = (currentFileIndex + 1) % filesCount;
}

void cInfoViewer::previousFile()
{
    if (filesCount <= 1)
    {
        return;
    }
    currentFileIndex = (currentFileIndex - 1 + filesCount) % filesCount;
}
