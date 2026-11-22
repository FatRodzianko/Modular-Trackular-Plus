#include "FileManagerModule.h"

#include "Platform.h"
#include "ampLogarythmicValues.h"
#include "config.h"
#include "core/interface.h"
#include "core/interfaceMenuPopup.h"
#include "core/interfacePopups.h"
#include "core/titleBar.h"
#include "display.h"
#include "fileManager.h"
#include "infoViewer/infoViewer.h"
#include "mtAudioEngine.h"
#include "mtConfig.h"
#include "mtStructs.h"
#include "projectEditor/projectEditor.h"
#include "util_math.h"
#include "util_string.h"

#include <algorithm>
#include <cmath>
#include <cr_section_macros.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <strings.h>

extern uint8_t externalRamScratchBuff[];

cFileManagerModule fileManagerModule;

__NOINIT(EXTERNAL_RAM) static char s_savedPanePath[2][kMaxFilePathLen + 1];

namespace
{
// Scratch layout inside top 440 KB of externalRamScratchBuff ([328 KB .. 768 KB]),
// strictly above sdStream's [0 .. 320 KB] audio preview window.
static constexpr uint32_t kSdStreamReservedBytes = 320 * 1024;

static constexpr int kFmMaxEntries = 4096;
static constexpr uint32_t kPaneBrowserScratchBytes = 160 * 1024;
static constexpr uint32_t kBrowserPtrsBytes = kFmMaxEntries * sizeof(char*); // 16 KB
static constexpr uint32_t kSelectionBytes = kFmMaxEntries * sizeof(char);    // 4 KB
static constexpr uint32_t kNamesBufBytes =
  kPaneBrowserScratchBytes - kBrowserPtrsBytes - kSelectionBytes; // 140 KB packed string pool

static constexpr uint32_t kWorkerScratchBytes = 36 * 1024;
static_assert(sizeof(FileOpsWorker::ScratchLayout) <= kWorkerScratchBytes,
              "FileOpsWorker scratch exceeds 36 KB");

static constexpr uint32_t kBatchListBytes = 24 * 1024;
static_assert(kMaxFilesListLength * sizeof(FileOpsWorker::BatchItem) <= kBatchListBytes,
              "Batch item list exceeds 24 KB");

static constexpr uint32_t kCopyIoBytes = FileOpsWorker::kCopyIoBufBytes; // 32 KB
static constexpr uint32_t kActiveStateBytes = 4 * 1024;
static_assert(sizeof(cFileManagerModule::ActiveScratchState) <= kActiveStateBytes,
              "ActiveScratchState exceeds 4 KB");

// Total: 2*160 + 36 + 24 (clipboard) + 24 (pending) + 32 (io) + 4 (state) = 440 KB
static constexpr uint32_t kTotalFmScratchBytes = 440 * 1024;
static constexpr uint32_t kFmScratchBaseOffset = SHARED_SCRATCH_MEMORY_SIZE - kTotalFmScratchBytes;

static_assert(kFmScratchBaseOffset >= kSdStreamReservedBytes,
              "FileManager scratch overlaps sdStream audio preview buffer");
static_assert(2 * kPaneBrowserScratchBytes + kWorkerScratchBytes + 2 * kBatchListBytes +
                  kCopyIoBytes + kActiveStateBytes <=
                kTotalFmScratchBytes,
              "FileManager scratch sub-allocations exceed 440 KB");

static const uint16_t s_fmFramesPlaces[2][4] = {
    { 0 + 1, 31, 400 - 3, 391 },
    { 400 + 1, 31, 400 - 3, 391 },
};

static void formatSizeShort(size_t bytes, char* out, size_t outSize)
{
    if (bytes < 1024)
    {
        snprintf(out, outSize, "%u B", static_cast<unsigned>(bytes));
    }
    else if (bytes < 1024u * 1024u)
    {
        snprintf(out, outSize, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    }
    else
    {
        snprintf(out, outSize, "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
}

static bool hasInvalidFilenameChars(const char* name)
{
    if (name == nullptr || name[0] == '\0')
    {
        return true;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    {
        return true;
    }
    for (const char* p = name; *p != '\0'; ++p)
    {
        char c = *p;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|')
        {
            return true;
        }
    }
    return false;
}

// =========================================================================
// Static input trampolines
// =========================================================================

static void fmEncoderTurn(int16_t dir)
{
    fileManagerModule.handleEncoderOrArrows(dir, true);
}

static void fmArrowUp()
{
    fileManagerModule.handleEncoderOrArrows(-1, false);
}

static void fmArrowDown()
{
    fileManagerModule.handleEncoderOrArrows(1, false);
}

static void fmArrowLeft()
{
    fileManagerModule.handleLeftRight(-1);
}

static void fmArrowRight()
{
    fileManagerModule.handleLeftRight(1);
}

static void fmEnterButton()
{
    fileManagerModule.handleEnter();
}

static void fmShiftButton(uint8_t state)
{
    fileManagerModule.handleShiftState(state);
}

static void fmCopyButton()
{
    fileManagerModule.handleCopyButton();
}

static void fmDeleteButton()
{
    fileManagerModule.handleDeleteButton();
}

static void fmPadInput(uint8_t pad, uint8_t state, int16_t velo)
{
    fileManagerModule.handlePadInput(pad, state, velo);
}

template <uint8_t ButtonIdx>
static void fmActionButton(uint8_t state)
{
    if (state == buttonPress)
    {
        fileManagerModule.handleActionButtonPress(ButtonIdx);
    }
    else if (state == buttonRelease)
    {
        fileManagerModule.handleActionButtonRelease(ButtonIdx);
    }
}

static void fmSwitchModule(uint8_t button)
{
    if (fileManagerModule.getModalState() == cFileManagerModule::ModalState::kProgress)
    {
        return;
    }
    fileManagerModule.stopFilePreview();
    fileManagerModule.eventFunct(eventSwitchModule, &fileManagerModule, &button, 0);
}

static void fmMiniMenu(uint8_t state)
{
    if (state == buttonPress)
    {
        mtMenuPopup.showModulePopup(&fileManagerModule, fmSwitchModule);
    }
    else if (state == buttonRelease)
    {
        mtMenuPopup.hideModulePopup(&fileManagerModule);
    }
}

} // namespace

// =========================================================================
// ModuleBase lifecycle
// =========================================================================

void cFileManagerModule::initDisplayControls()
{
    strControlProperties prop{};
    frameData.placesCount = 2;
    frameData.startPlace = 0;
    frameData.places[0] = &s_fmFramesPlaces[0][0];
    frameData.places[1] = &s_fmFramesPlaces[1][0];
    prop.style = 0;
    prop.value = 0;
    prop.data = &frameData;
    frameControl = display.createControl<cFrame>(&prop);

    strControlProperties prop2{};
    for (uint8_t i = 0; i < 8; i++)
    {
        prop2.value = 0;
        prop2.colors = display.getTheme().activeButtonLabelsColors;
        prop2.style = controlStyleCenterX | controlStyleCenterY | controlStyleFont3;
        prop2.x = (800 / 8) * i + (800 / 16);
        prop2.w = 800 / 8 - 6;
        prop2.y = 424;
        prop2.h = 55;
        label[i] = display.createControl<cLabel>(&prop2);
    }

    prop2.text = nullptr;
    prop2.colors = nullptr;
    prop2.value = 255;
    prop2.style = controlStyleNoTransparency | controlStyleShow;
    prop2.x = 0;
    prop2.w = 800;
    prop2.y = 425;
    prop2.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop2);

    for (int p = 0; p < 2; ++p)
    {
        const uint16_t baseX = (p == 0) ? 1 : 401;
        const uint16_t paneW = 400 - 3;

        strControlProperties hdrProp{};
        hdrProp.value = 0;
        hdrProp.colors = display.getTheme().topBarLabelColors;
        hdrProp.style = controlStyleShow | controlStyleBackground | controlStyleCenterY |
                        controlStyleCenterX | controlStyleFont4;
        hdrProp.x = baseX + (paneW / 2);
        hdrProp.w = paneW;
        hdrProp.y = 32;
        hdrProp.h = 26;
        hdrProp.text = panes[p].headerText;
        panes[p].headerControl = display.createControl<cLabel>(&hdrProp);

        panes[p].listControlData.start = 0;
        panes[p].listControlData.length = 0;
        panes[p].listControlData.linesCount = 13;
        panes[p].listControlData.data = nullptr;
        panes[p].listControlData.selectionActive = 1;
        panes[p].listControlData.selectTab = nullptr;
        panes[p].listControlData.lineWidthMax = paneW - 24;
        panes[p].listControlData.lineTextLenMax = 35;

        strControlProperties listProp{};
        listProp.x = baseX;
        listProp.y = 29 + 32;
        listProp.w = paneW;
        listProp.h = 394 - 32;
        listProp.value = 0;
        listProp.style = controlStyleBackground |
                         (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
        listProp.data = &panes[p].listControlData;
        listProp.colors = display.getTheme().projectActiveListColors;
        panes[p].listControl = display.createControl<cList>(&listProp);
    }
}

void cFileManagerModule::setupPaneScratch(int paneIdx, uint8_t* browserBase)
{
    PaneState& pane = panes[paneIdx];
    memset(&pane.browserData, 0, sizeof(pane.browserData));
    pane.browserData.mode = kAllFilesBrowser;
    pane.browserData.selectionEnabled = true;
    pane.browserData.maxFileNameLen = kMaxFileNameLen;
    pane.browserData.maxBrowserListLen = kFmMaxEntries;
    pane.browserData.fileNamesBufferSize = static_cast<int>(kNamesBufBytes);
    pane.browserData.fileNamesBuffer = reinterpret_cast<char*>(browserBase);
    pane.browserData.browserList = reinterpret_cast<char**>(browserBase + kNamesBufBytes);
    pane.browserData.selectionList =
      reinterpret_cast<char*>(browserBase + kNamesBufBytes + kBrowserPtrsBytes);
    pane.browserData.defaultPath = "";

    pane.selectionCount = 0;
    pane.shiftAnchor = -1;
}

void cFileManagerModule::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;

    modalState = ModalState::kBrowse;
    shiftHeld = false;
    previewHeld = false;
    applyCollisionToAll = false;
    lastReportedProgress = -1;
    clipboardCount = 0;
    pendingOpType = FileOpsWorker::OpType::kNone;
    pendingOpItems = nullptr;
    pendingOpCount = 0;
    pendingOpHasDst = false;

    // Partition top 440 KB of externalRamScratchBuff
    uint8_t* cursor = externalRamScratchBuff + kFmScratchBaseOffset;
    uint8_t* leftBrowserBuf = cursor;
    cursor += kPaneBrowserScratchBytes;
    uint8_t* rightBrowserBuf = cursor;
    cursor += kPaneBrowserScratchBytes;
    uint8_t* workerBuf = cursor;
    cursor += kWorkerScratchBytes;
    clipboardItems = reinterpret_cast<FileOpsWorker::BatchItem*>(cursor);
    cursor += kBatchListBytes;
    pendingBatchScratch = reinterpret_cast<FileOpsWorker::BatchItem*>(cursor);
    cursor += kBatchListBytes;
    uint8_t* copyIoBuf = cursor;
    cursor += kCopyIoBytes;
    scratchState = new (cursor) ActiveScratchState{};

    setupPaneScratch(0, leftBrowserBuf);
    setupPaneScratch(1, rightBrowserBuf);

    FileSystem* fs = newFileManager.getFileSystem();
    scratchState->worker.init(fs, workerBuf, kWorkerScratchBytes, copyIoBuf);

    if (!hasSavedPaths)
    {
        s_savedPanePath[0][0] = '\0';
        s_savedPanePath[1][0] = '\0';
        savedPanePos[0] = 0;
        savedPanePos[1] = 0;
        savedActivePane = 0;
        hasSavedPaths = true;
    }

    activePane = (savedActivePane == 1) ? 1 : 0;

    for (int p = 0; p < 2; ++p)
    {
        bool restoredDir = false;
        if (s_savedPanePath[p][0] != '\0' && fs != nullptr && fs->exists(s_savedPanePath[p]))
        {
            polystrncpy(panes[p].browserData.currentPath, s_savedPanePath[p], kMaxFilePathLen + 1);
            // Ensure trailing slash for FileBrowser convention when non-root
            size_t len = strlen(panes[p].browserData.currentPath);
            if (len > 0 && panes[p].browserData.currentPath[len - 1] != '/' &&
                len < kMaxFilePathLen)
            {
                panes[p].browserData.currentPath[len] = '/';
                panes[p].browserData.currentPath[len + 1] = '\0';
            }
            restoredDir = true;
        }
        else
        {
            panes[p].browserData.currentPath[0] = '\0';
        }
        panes[p].browserData.browserListPosition = 0;
        if (fs != nullptr)
        {
            panes[p].browser.start(fs, &panes[p].browserData);
        }
        if ((restoredDir || s_savedPanePath[p][0] == '\0') && savedPanePos[p] > 0 &&
            panes[p].browserData.browserListLength > 0)
        {
            panes[p].browserData.browserListPosition =
              clamp(savedPanePos[p], 0, panes[p].browserData.browserListLength - 1);
        }
        clearPaneSelection(p);
        syncPaneListControl(p);
        updatePaneHeaderText(p);
    }

    // Prepare header color tables from theme
    memcpy(headerColorsActive, display.getTheme().topBarLabelColors, sizeof(headerColorsActive));
    memcpy(headerColorsInactive, display.getTheme().topBarLabelColors, sizeof(headerColorsInactive));
    headerColorsInactive[1] = display.getTheme().inactiveButtonLabelsColors[1];

    setMainFunctions();
    showBrowseScreen();
}

void cFileManagerModule::stop()
{
    stopFilePreview();
    if (scratchState != nullptr)
    {
        scratchState->worker.reset();
        scratchState = nullptr;
    }
    mtPopups.hideProgressPopup();
    mtPopups.hideInfoPopup();
    keyboardManager.deactivateKeyboard();

    savedActivePane = activePane;
    for (int p = 0; p < 2; ++p)
    {
        FileOpsWorker::normalizePath(
          panes[p].browserData.currentPath, s_savedPanePath[p], sizeof(s_savedPanePath[p]));
        savedPanePos[p] = panes[p].browserData.browserListPosition;
    }
}

void cFileManagerModule::update()
{
    if (modalState != ModalState::kProgress)
    {
        return;
    }

    // Process up to 2 chunks per tick to keep UI & cancel responsive
    for (int i = 0; i < 2; ++i)
    {
        if (scratchState->worker.getState() != FileOpsWorker::State::kRunning)
        {
            break;
        }
        scratchState->worker.step();
    }

    FileOpsWorker::State st = scratchState->worker.getState();
    if (st == FileOpsWorker::State::kRunning)
    {
        int pct = scratchState->worker.getProgressPercent();
        int curIdx = scratchState->worker.getDisplayCurrentIndex();
        int totCnt = scratchState->worker.getDisplayTotalCount();
        bool scanning = scratchState->worker.isScanning();

        bool textChanged = (curIdx != lastReportedCurIdx) || (totCnt != lastReportedTotal) ||
                           (scanning != lastReportedScanning);
        if (textChanged)
        {
            lastReportedCurIdx = curIdx;
            lastReportedTotal = totCnt;
            lastReportedScanning = scanning;
            formatProgressTitle();
            mtPopups.setProgressPopupText(scratchState->progressTitleText);
        }
        if (pct != lastReportedProgress || textChanged)
        {
            lastReportedProgress = pct;
            mtPopups.changePopupProgress(static_cast<int8_t>(pct));
        }
        return;
    }

    if (st == FileOpsWorker::State::kWaitCollision)
    {
        mtPopups.hideProgressPopup();
        showCollisionPopup();
        return;
    }

    mtPopups.hideProgressPopup();

    if (st == FileOpsWorker::State::kDone)
    {
        const char* opVerb = "Done";
        switch (scratchState->worker.getOpType())
        {
            case FileOpsWorker::OpType::kCopy: opVerb = "Copied"; break;
            case FileOpsWorker::OpType::kMove: opVerb = "Moved"; break;
            case FileOpsWorker::OpType::kDelete: opVerb = "Deleted"; break;
            default: break;
        }
        int doneCnt = scratchState->worker.getCompletedCount() - scratchState->worker.getSkippedCount();
        if (scratchState->worker.getSkippedCount() > 0)
        {
            snprintf(scratchState->toastText,
                     sizeof(scratchState->toastText),
                     "%s %d (%d skipped)",
                     opVerb,
                     doneCnt,
                     scratchState->worker.getSkippedCount());
        }
        else
        {
            snprintf(scratchState->toastText, sizeof(scratchState->toastText), "%s %d item(s)", opVerb, doneCnt);
        }
        mtPopups.show(0, scratchState->toastText);
    }
    else if (st == FileOpsWorker::State::kCancelled)
    {
        mtPopups.show(0, "Operation cancelled");
    }
    else if (st == FileOpsWorker::State::kError)
    {
        snprintf(scratchState->toastText, sizeof(scratchState->toastText), "%s", scratchState->worker.getErrorMessage());
        mtPopups.show(0, scratchState->toastText);
    }

    scratchState->worker.reset();
    dismissPopupToBrowse(true);
}

// =========================================================================
// Pane & selection helpers
// =========================================================================

bool cFileManagerModule::isParentEntry(int paneIdx, int pos) const
{
    const PaneState& p = panes[paneIdx];
    return p.browserData.currentPath[0] != '\0' && pos == 0;
}

bool cFileManagerModule::isFolderAt(int paneIdx, int pos) const
{
    PaneState& p = const_cast<PaneState&>(panes[paneIdx]);
    if (pos < 0 || pos >= p.browserData.browserListLength)
    {
        return false;
    }
    if (isParentEntry(paneIdx, pos))
    {
        return false;
    }
    return p.browser.isFolder(pos);
}

bool cFileManagerModule::isWavFileAt(int paneIdx, int pos) const
{
    const PaneState& p = panes[paneIdx];
    if (pos < 0 || pos >= p.browserData.browserListLength)
    {
        return false;
    }
    if (isParentEntry(paneIdx, pos) || isFolderAt(paneIdx, pos))
    {
        return false;
    }
    const char* name = p.browserData.browserList[pos];
    return FileBrowser::checkFilenameExtension(name, "wav");
}

bool cFileManagerModule::isViewableFileAt(int paneIdx, int pos) const
{
    const PaneState& p = panes[paneIdx];
    if (pos < 0 || pos >= p.browserData.browserListLength)
    {
        return false;
    }
    if (isParentEntry(paneIdx, pos) || isFolderAt(paneIdx, pos))
    {
        return false;
    }
    return cInfoViewer::isViewableFile(p.browserData.browserList[pos]);
}

void cFileManagerModule::openSelectedInViewer()
{
    const PaneState& p = panes[activePane];
    int pos = p.browserData.browserListPosition;
    if (!isViewableFileAt(activePane, pos))
    {
        return;
    }
    stopFilePreview();
    infoViewer.openFile(p.browserData.currentPath, p.browserData.browserList[pos], this);
}

bool cFileManagerModule::getItemFullPath(int paneIdx, int pos, char* outPath, int outSize) const
{
    const PaneState& p = panes[paneIdx];
    if (pos < 0 || pos >= p.browserData.browserListLength || isParentEntry(paneIdx, pos))
    {
        return false;
    }
    return FileOpsWorker::joinPath(
      p.browserData.currentPath, p.browserData.browserList[pos], outPath, outSize);
}

void cFileManagerModule::getActiveProjectPath(char* outPath, int outSize) const
{
    if (outPath == nullptr || outSize <= 0)
    {
        return;
    }
    outPath[0] = '\0';
    newFileManager.getCurrentProjectPathWithName(outPath, outSize);
}

void cFileManagerModule::refreshPane(int paneIdx, const char* selectName)
{
    PaneState& p = panes[paneIdx];
    FileSystem* fs = newFileManager.getFileSystem();
    if (fs == nullptr)
    {
        return;
    }

    // If current directory was deleted/moved, walk up to root
    if (p.browserData.currentPath[0] != '\0')
    {
        char norm[kMaxFilePathLen + 1];
        FileOpsWorker::normalizePath(p.browserData.currentPath, norm, sizeof(norm));
        if (!fs->exists(norm))
        {
            p.browserData.currentPath[0] = '\0';
            p.browserData.browserListPosition = 0;
        }
    }

    p.browser.refresh();
    clearPaneSelection(paneIdx);

    if (selectName != nullptr && selectName[0] != '\0')
    {
        const char* cleanTarget = (selectName[0] == '/') ? (selectName + 1) : selectName;
        for (int i = 0; i < p.browserData.browserListLength; ++i)
        {
            if (isParentEntry(paneIdx, i))
            {
                continue;
            }
            const char* entry = p.browserData.browserList[i];
            const char* cleanEntry = (entry[0] == '/') ? (entry + 1) : entry;
            if (strcasecmp(cleanEntry, cleanTarget) == 0)
            {
                p.browserData.browserListPosition = i;
                break;
            }
        }
    }

    if (p.browserData.browserListPosition >= p.browserData.browserListLength)
    {
        p.browserData.browserListPosition =
          (p.browserData.browserListLength > 0) ? (p.browserData.browserListLength - 1) : 0;
    }

    syncPaneListControl(paneIdx);
    updatePaneHeaderText(paneIdx);
}

void cFileManagerModule::syncPaneListControl(int paneIdx)
{
    PaneState& p = panes[paneIdx];
    int count = std::min<int>(p.browserData.browserListLength, kFmMaxEntries);
    p.listControlData.data = p.browserData.browserList;
    p.listControlData.length = static_cast<uint16_t>(count);
    p.listControlData.start = static_cast<int16_t>(p.browserData.browserListPosition);
    p.listControlData.selectionActive = 1;
    p.listControlData.selectTab = reinterpret_cast<uint8_t*>(p.browserData.selectionList);
    if (p.listControl >= 0)
    {
        display.setControlData(p.listControl, &p.listControlData);
    }
}

void cFileManagerModule::updatePaneHeaderText(int paneIdx)
{
    PaneState& p = panes[paneIdx];
    char norm[kMaxFilePathLen + 1];
    FileOpsWorker::normalizePath(p.browserData.currentPath, norm, sizeof(norm));
    if (norm[0] == '\0')
    {
        polystrncpy(p.headerText, "/", sizeof(p.headerText));
        return;
    }

    constexpr size_t kMaxHeaderChars = 34;
    size_t len = strlen(norm);
    if (len + 1 <= kMaxHeaderChars)
    {
        snprintf(p.headerText, sizeof(p.headerText), "/%s", norm);
    }
    else
    {
        snprintf(
          p.headerText, sizeof(p.headerText), "...%s", norm + (len - (kMaxHeaderChars - 3)));
    }
}

void cFileManagerModule::clearPaneSelection(int paneIdx)
{
    PaneState& p = panes[paneIdx];
    if (p.browserData.selectionList != nullptr)
    {
        memset(p.browserData.selectionList, 0, kFmMaxEntries);
    }
    p.selectionCount = 0;
    p.shiftAnchor = -1;
}

void cFileManagerModule::recountPaneSelection(int paneIdx)
{
    PaneState& p = panes[paneIdx];
    int cnt = 0;
    for (int i = 0; i < p.browserData.browserListLength; ++i)
    {
        if (isParentEntry(paneIdx, i))
        {
            p.browserData.selectionList[i] = 0;
            continue;
        }
        if (p.browserData.selectionList[i] != 0)
        {
            ++cnt;
        }
    }
    p.selectionCount = cnt;
}

void cFileManagerModule::toggleItemSelection(int paneIdx, int pos)
{
    PaneState& p = panes[paneIdx];
    if (pos < 0 || pos >= p.browserData.browserListLength || isParentEntry(paneIdx, pos))
    {
        return;
    }
    p.browserData.selectionList[pos] = p.browserData.selectionList[pos] ? 0 : 1;
    recountPaneSelection(paneIdx);
}

void cFileManagerModule::selectAllInPane(int paneIdx)
{
    PaneState& p = panes[paneIdx];
    for (int i = 0; i < p.browserData.browserListLength; ++i)
    {
        p.browserData.selectionList[i] = isParentEntry(paneIdx, i) ? 0 : 1;
    }
    recountPaneSelection(paneIdx);
}

void cFileManagerModule::applyShiftRangeSelection(int paneIdx, int prevPos, int newPos)
{
    PaneState& p = panes[paneIdx];
    if (p.shiftAnchor < 0)
    {
        p.shiftAnchor = prevPos;
    }

    int lo = std::min(prevPos, newPos);
    int hi = std::max(prevPos, newPos);
    for (int i = lo; i <= hi; ++i)
    {
        if (!isParentEntry(paneIdx, i))
        {
            p.browserData.selectionList[i] = 1;
        }
    }
    recountPaneSelection(paneIdx);
}

void cFileManagerModule::rewindPaneToTop(int paneIdx)
{
    PaneState& p = panes[paneIdx];
    p.browserData.browserListPosition = 0;
    p.listControlData.start = 0;
    if (p.listControl >= 0)
    {
        display.setControlData(p.listControl, &p.listControlData);
    }
    clearPaneSelection(paneIdx);
}

int cFileManagerModule::gatherTargetItems(int paneIdx,
                                          FileOpsWorker::BatchItem* outItems,
                                          int maxItems) const
{
    const PaneState& p = panes[paneIdx];
    int count = 0;

    if (p.selectionCount > 0)
    {
        for (int i = 0; i < p.browserData.browserListLength && count < maxItems; ++i)
        {
            if (isParentEntry(paneIdx, i) || p.browserData.selectionList[i] == 0)
            {
                continue;
            }
            const char* raw = p.browserData.browserList[i];
            const char* clean = (raw[0] == '/') ? (raw + 1) : raw;
            polystrncpy(outItems[count].name, clean, kMaxFileNameLen + 1);
            outItems[count].isFolder = isFolderAt(paneIdx, i);
            ++count;
        }
    }
    else
    {
        int pos = p.browserData.browserListPosition;
        if (pos >= 0 && pos < p.browserData.browserListLength && !isParentEntry(paneIdx, pos) &&
            maxItems > 0)
        {
            const char* raw = p.browserData.browserList[pos];
            const char* clean = (raw[0] == '/') ? (raw + 1) : raw;
            polystrncpy(outItems[0].name, clean, kMaxFileNameLen + 1);
            outItems[0].isFolder = isFolderAt(paneIdx, pos);
            count = 1;
        }
    }

    return count;
}

// =========================================================================
// Audio Preview
// =========================================================================

void cFileManagerModule::startFilePreview()
{
    PaneState& p = panes[activePane];
    int pos = p.browserData.browserListPosition;
    if (!isWavFileAt(activePane, pos))
    {
        return;
    }

    char fullPath[kMaxFilePathLen + 1];
    if (!getItemFullPath(activePane, pos, fullPath, sizeof(fullPath)))
    {
        return;
    }

    auto sdPlayer = engine.getSdPlayer();
    if (sdPlayer != nullptr)
    {
        sdPlayer->stop();
        sdPlayer->play(fullPath, 0, MAX_16BIT);
    }
}

void cFileManagerModule::stopFilePreview()
{
    auto sdPlayer = engine.getSdPlayer();
    if (sdPlayer != nullptr)
    {
        sdPlayer->stop();
    }
}

// =========================================================================
// Display & UI rendering
// =========================================================================

void cFileManagerModule::updateTitleBarStatus()
{
    titleBar->show(true);
    titleBar->setTitle("File Manager");

    const PaneState& p = panes[activePane];
    if (p.selectionCount > 0)
    {
        snprintf(scratchState->titleRightText, sizeof(scratchState->titleRightText), "%d selected", p.selectionCount);
        titleBar->showRightLabel(true);
        titleBar->setRightLabel(scratchState->titleRightText);
    }
    else if (clipboardCount > 0)
    {
        snprintf(scratchState->titleRightText, sizeof(scratchState->titleRightText), "Clip: %d", clipboardCount);
        titleBar->showRightLabel(true);
        titleBar->setRightLabel(scratchState->titleRightText);
    }
    else
    {
        titleBar->showRightLabel(false);
    }
}

void cFileManagerModule::showBrowseScreen()
{
    modalState = ModalState::kBrowse;
    shiftHeld = (buttons != nullptr) && buttons->isButtonPressed(interfaceButtonShift);
    if (!shiftHeld)
    {
        panes[activePane].shiftAnchor = -1;
    }
    keyboardManager.deactivateKeyboard();
    mtPopups.hideInfoPopup();
    mtPopups.hideProgressPopup();

    for (int p = 0; p < 2; ++p)
    {
        display.setControlShow(panes[p].headerControl);
        display.setControlShow(panes[p].listControl);
    }
    display.setControlShow(frameControl);
    display.setControlShow(bgLabel);
    for (uint8_t i = 0; i < 8; ++i)
    {
        display.setControlShow(label[i]);
    }

    refreshBrowseUI();
}

void cFileManagerModule::refreshBrowseUI()
{
    updateTitleBarStatus();

    for (int p = 0; p < 2; ++p)
    {
        const bool isActive = (p == activePane);
        display.setControlValue(panes[p].listControl, panes[p].browserData.browserListPosition);
        display.setControlColors(panes[p].headerControl,
                                 isActive ? headerColorsActive : headerColorsInactive);
        display.setControlText(panes[p].headerControl, panes[p].headerText);
        display.setControlColors(panes[p].listControl,
                                 isActive ? display.getTheme().projectActiveListColors
                                          : display.getTheme().projectInactiveListColors);
        display.refreshControl(panes[p].headerControl);
        display.refreshControl(panes[p].listControl);
    }

    display.setControlValue(frameControl, activePane);
    display.refreshControl(frameControl);
    display.refreshControl(bgLabel);

    refreshActionButtonLabels();
    display.synchronizeRefresh();
}

void cFileManagerModule::refreshActionButtonLabels()
{
    const PaneState& p = panes[activePane];
    const int pos = p.browserData.browserListPosition;
    const bool hasItem = (pos >= 0 && pos < p.browserData.browserListLength);
    const bool onParent = hasItem && isParentEntry(activePane, pos);
    const bool onFolder = hasItem && isFolderAt(activePane, pos);
    const bool onFile = hasItem && !onParent && !onFolder;
    const bool hasActionable = (p.selectionCount > 0) || (hasItem && !onParent);
    const bool wavSelected = hasItem && isWavFileAt(activePane, pos);
    const bool viewableSelected = hasItem && isViewableFileAt(activePane, pos);

    for (uint8_t i = 0; i < 8; ++i)
    {
        display.setControlValue(label[i], 0);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlData(label[i], nullptr);
        display.setControlStyle(label[i],
                                controlStyleShow | controlStyleCenterX | controlStyleCenterY |
                                  controlStyleFont3);
        display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
    }

    if (modalState == ModalState::kPreviewVolume)
    {
        float db = 20.0f * log10f(static_cast<float>(mixAmpLogValues[mtConfig.general.previewVolume]));
        snprintf(scratchState->previewVolText, sizeof(scratchState->previewVolText), "%0.1f dB", static_cast<double>(db));
        display.setControlText(label[2], "Preview Vol");
        display.setControlText2(label[2], scratchState->previewVolText);
        display.setControlValue(label[2], 1);
        display.setControlColors(label[2], display.getTheme().highlightedButtonLabelsColors);
        display.setControlText(label[7], "Done");
        for (uint8_t i = 0; i < 8; ++i)
        {
            if (i != 2 && i != 7)
            {
                display.setControlColors(label[i], display.getTheme().inactiveButtonLabelsColors);
            }
            display.refreshControl(label[i]);
        }
        return;
    }

    if (!shiftHeld)
    {
        // Btn 1: Enter (folder / parent) or Select (file)
        if (onParent || onFolder)
        {
            display.setControlText(label[0], "Enter");
        }
        else if (onFile)
        {
            display.setControlText(label[0],
                                   p.browserData.selectionList[pos] ? "Unselect" : "Select");
        }
        else
        {
            display.setControlText(label[0], "Enter");
            display.setControlColors(label[0], display.getTheme().inactiveButtonLabelsColors);
        }

        // Btn 2: New Folder
        display.setControlText(label[1], "New Folder");

        // Btn 3: Preview (WAV) or View (image/text)
        if (viewableSelected)
        {
            display.setControlText(label[2], "View");
        }
        else
        {
            display.setControlText(label[2], "Preview");
            if (!wavSelected)
            {
                display.setControlColors(label[2], display.getTheme().inactiveButtonLabelsColors);
            }
        }

        // Btn 4 & 5: Copy / Move with native anti-aliased directional arrow bitmap
        const uint8_t arrowBitmap =
          (activePane == 0) ? displayDoubleArrowR : displayDoubleArrowL;

        transferArrowData[0].bitmaps[0].bitmapIndex = arrowBitmap;
        transferArrowData[0].bitmaps[0].xValue = (800 / 8) * 3 + (800 / 16);
        transferArrowData[0].bitmaps[0].yValue = 456;
        display.setControlText(label[3], "Copy");
        display.setControlValue(label[3], 1);
        display.setControlData(label[3], &transferArrowData[0]);
        display.setControlStyle(label[3],
                                controlStyleShow | controlStyleCenterX | controlStyleCenterY |
                                  controlStyleFont3 | controlStyleShowBitmap);
        if (!hasActionable)
        {
            display.setControlColors(label[3], display.getTheme().inactiveButtonLabelsColors);
        }

        transferArrowData[1].bitmaps[0].bitmapIndex = arrowBitmap;
        transferArrowData[1].bitmaps[0].xValue = (800 / 8) * 4 + (800 / 16);
        transferArrowData[1].bitmaps[0].yValue = 456;
        display.setControlText(label[4], "Move");
        display.setControlValue(label[4], 1);
        display.setControlData(label[4], &transferArrowData[1]);
        display.setControlStyle(label[4],
                                controlStyleShow | controlStyleCenterX | controlStyleCenterY |
                                  controlStyleFont3 | controlStyleShowBitmap);
        if (!hasActionable)
        {
            display.setControlColors(label[4], display.getTheme().inactiveButtonLabelsColors);
        }

        // Btn 6: Delete (Sample Loader slot)
        display.setControlText(label[5], "Delete");
        if (!hasActionable)
        {
            display.setControlColors(label[5], display.getTheme().inactiveButtonLabelsColors);
        }

        // Btn 7: Rename
        display.setControlText(label[6], "Rename");
        if (!hasItem || onParent || p.selectionCount > 1)
        {
            display.setControlColors(label[6], display.getTheme().inactiveButtonLabelsColors);
        }

        // Btn 8: Back
        display.setControlText(label[7], "Back");
    }
    else
    {
        // Shift layer
        display.setControlText(label[0], "Select");
        if (!hasItem || onParent)
        {
            display.setControlColors(label[0], display.getTheme().inactiveButtonLabelsColors);
        }

        display.setControlText(label[1], (p.selectionCount > 0) ? "Clear Sel" : "Select All");

        display.setControlText(label[2], "Preview Vol");

        display.setControlText(label[3], "Sync Dir");

        display.setControlText(label[4], "Info");
        if (!hasItem || onParent)
        {
            display.setControlColors(label[4], display.getTheme().inactiveButtonLabelsColors);
        }

        display.setControlText(label[5], "Clear Sel");
        if (p.selectionCount == 0)
        {
            display.setControlColors(label[5], display.getTheme().inactiveButtonLabelsColors);
        }

        display.setControlText(label[6], "Swap Panes");

        display.setControlText(label[7], "Back");
    }

    for (uint8_t i = 0; i < 8; ++i)
    {
        display.refreshControl(label[i]);
    }
}

// =========================================================================
// Input binding
// =========================================================================

void cFileManagerModule::setMainFunctions()
{
    ip->clearAll();

    ip->setButtonObj(interfaceButtonParams, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonPerformance, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonFile, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonGame, buttonPress, fmSwitchModule);
    ip->setButtonObj(interfaceButtonMiniMenu, fmMiniMenu);

    ip->setPotObj(interfacePot0, fmEncoderTurn);
    ip->setButtonObj(interfaceButtonUp, buttonPress, fmArrowUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, fmArrowDown);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, fmArrowLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, fmArrowRight);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, fmEnterButton);
    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, fmEnterButton);
    ip->setButtonObj(interfaceButtonShift, fmShiftButton);
    ip->setButtonObj(interfaceButtonCopy, buttonPress, fmCopyButton);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, fmDeleteButton);

    ip->setButtonObj(interfaceButton0, fmActionButton<0>);
    ip->setButtonObj(interfaceButton1, fmActionButton<1>);
    ip->setButtonObj(interfaceButton2, fmActionButton<2>);
    ip->setButtonObj(interfaceButton3, fmActionButton<3>);
    ip->setButtonObj(interfaceButton4, fmActionButton<4>);
    ip->setButtonObj(interfaceButton5, fmActionButton<5>);
    ip->setButtonObj(interfaceButton6, fmActionButton<6>);
    ip->setButtonObj(interfaceButton7, fmActionButton<7>);

    ip->setPadsGlobal(fmPadInput);
}

// =========================================================================
// Input handlers
// =========================================================================

void cFileManagerModule::handleEncoderOrArrows(int16_t delta, bool isEncoder)
{
    if (modalState == ModalState::kProgress)
    {
        return;
    }

    if (isKeyboardActive())
    {
        if (!isEncoder)
        {
            keyboardManager.makeMove((delta < 0) ? 'w' : 's');
        }
        return;
    }

    if (modalState == ModalState::kPreviewVolume)
    {
        int step = isEncoder ? delta : -delta;
        int vol = static_cast<int>(mtConfig.general.previewVolume) + step;
        vol = clamp(vol, 0, static_cast<int>(PREVIEW_VOLUME_MAX));
        mtConfig.general.previewVolume = static_cast<uint8_t>(vol);
        saveConfig();
        refreshActionButtonLabels();
        display.synchronizeRefresh();
        return;
    }

    if (modalState != ModalState::kBrowse)
    {
        return;
    }

    PaneState& p = panes[activePane];
    if (p.browserData.browserListLength <= 0)
    {
        return;
    }

    int step = (isEncoder && interfaceGlobals.invertValForList) ? -delta : delta;
    int prevPos = p.browserData.browserListPosition;
    int nextPos = clamp(prevPos + step, 0, p.browserData.browserListLength - 1);
    if (nextPos == prevPos)
    {
        return;
    }

    p.browserData.browserListPosition = nextPos;

    if (shiftHeld)
    {
        applyShiftRangeSelection(activePane, prevPos, nextPos);
    }

    if (previewHeld)
    {
        stopFilePreview();
        if (isWavFileAt(activePane, nextPos))
        {
            startFilePreview();
        }
    }

    refreshBrowseUI();
}

void cFileManagerModule::handleLeftRight(int dir)
{
    if (modalState == ModalState::kProgress)
    {
        return;
    }

    if (isKeyboardActive())
    {
        keyboardManager.makeMove((dir < 0) ? 'a' : 'd');
        return;
    }

    if (modalState != ModalState::kBrowse)
    {
        return;
    }

    stopFilePreview();

    if (dir < 0)
    {
        if (activePane == 1)
        {
            activePane = 0;
        }
        else
        {
            // Already on Left pane: rewind to top and clear selection (Sample Loader pattern)
            rewindPaneToTop(0);
        }
    }
    else
    {
        if (activePane == 0)
        {
            activePane = 1;
        }
        else
        {
            // Mirrored on Right pane: rewind to top and clear selection
            rewindPaneToTop(1);
        }
    }

    refreshBrowseUI();
}

void cFileManagerModule::handleEnter()
{
    if (modalState == ModalState::kProgress)
    {
        return;
    }

    if (isKeyboardActive())
    {
        keyboardManager.confirmKey();
        return;
    }

    if (modalState == ModalState::kConfirmDelete)
    {
        handleConfirmDeleteButton(7);
        return;
    }

    if (modalState == ModalState::kInfoPopup)
    {
        dismissPopupToBrowse(false);
        return;
    }

    if (modalState == ModalState::kPreviewVolume)
    {
        modalState = ModalState::kBrowse;
        refreshBrowseUI();
        return;
    }

    if (modalState != ModalState::kBrowse)
    {
        return;
    }

    PaneState& p = panes[activePane];
    int pos = p.browserData.browserListPosition;
    if (pos < 0 || pos >= p.browserData.browserListLength)
    {
        return;
    }

    if (isParentEntry(activePane, pos) || isFolderAt(activePane, pos))
    {
        stopFilePreview();
        p.browser.browse();
        clearPaneSelection(activePane);
        syncPaneListControl(activePane);
        updatePaneHeaderText(activePane);
        refreshBrowseUI();
    }
    else if (isWavFileAt(activePane, pos))
    {
        startFilePreview();
    }
    else if (isViewableFileAt(activePane, pos))
    {
        openSelectedInViewer();
    }
}

void cFileManagerModule::handleHome()
{
    if (modalState != ModalState::kBrowse)
    {
        return;
    }
    stopFilePreview();
    rewindPaneToTop(activePane);
    refreshBrowseUI();
}

void cFileManagerModule::handleShiftState(uint8_t state)
{
    if (isKeyboardActive())
    {
        if (state == buttonPress)
        {
            keyboardManager.confirmKey();
        }
        return;
    }

    if (state == buttonRelease)
    {
        shiftHeld = false;
        panes[activePane].shiftAnchor = -1;
        if (modalState == ModalState::kBrowse)
        {
            refreshActionButtonLabels();
            display.synchronizeRefresh();
        }
        return;
    }

    if (modalState != ModalState::kBrowse)
    {
        return;
    }

    if (state == buttonPress)
    {
        shiftHeld = true;
        panes[activePane].shiftAnchor = panes[activePane].browserData.browserListPosition;
        refreshActionButtonLabels();
        display.synchronizeRefresh();
    }
}

void cFileManagerModule::handleCopyButton()
{
    if (modalState != ModalState::kBrowse)
    {
        return;
    }

    if (shiftHeld)
    {
        triggerPaste();
        return;
    }

    int count = gatherTargetItems(activePane, clipboardItems, FileOpsWorker::kMaxBatchItems);
    if (count <= 0)
    {
        return;
    }

    FileOpsWorker::normalizePath(
      panes[activePane].browserData.currentPath, scratchState->clipboardSrcDir, sizeof(scratchState->clipboardSrcDir));
    clipboardCount = count;

    snprintf(scratchState->toastText, sizeof(scratchState->toastText), "Copied %d item(s)", count);
    mtPopups.show(0, scratchState->toastText);
    updateTitleBarStatus();
    display.synchronizeRefresh();
}

void cFileManagerModule::handleDeleteButton()
{
    if (isKeyboardActive())
    {
        keyboardManager.makeBackspace();
        return;
    }

    if (modalState == ModalState::kBrowse)
    {
        triggerDelete();
    }
}

void cFileManagerModule::handlePadInput(uint8_t pad, uint8_t state, int16_t velo)
{
    (void)velo;
    if (isKeyboardActive())
    {
        keyboardManager.onPadChange(pad, state);
    }
}

void cFileManagerModule::handleActionButtonPress(uint8_t button)
{
    switch (modalState)
    {
        case ModalState::kConfirmDelete: handleConfirmDeleteButton(button); return;
        case ModalState::kCollisionPrompt: handleCollisionButton(button); return;
        case ModalState::kProgress: handleProgressButton(button); return;
        case ModalState::kKeyboardNewFolder:
        case ModalState::kKeyboardRename: handleKeyboardButton(button); return;
        case ModalState::kInfoPopup: handleInfoPopupButton(button); return;
        case ModalState::kPreviewVolume:
            if (button == 2 || button == 7)
            {
                modalState = ModalState::kBrowse;
                refreshBrowseUI();
            }
            return;
        case ModalState::kBrowse: break;
    }

    PaneState& p = panes[activePane];
    int pos = p.browserData.browserListPosition;

    if (!shiftHeld)
    {
        switch (button)
        {
            case 0: // Enter (folder/parent) or Select (file)
                if (pos >= 0 && pos < p.browserData.browserListLength)
                {
                    if (isParentEntry(activePane, pos) || isFolderAt(activePane, pos))
                    {
                        handleEnter();
                    }
                    else
                    {
                        toggleItemSelection(activePane, pos);
                        refreshBrowseUI();
                    }
                }
                break;

            case 1: // New Folder
                openNewFolderKeyboard();
                break;

            case 2: // Preview (WAV) or View (image/text)
                if (isViewableFileAt(activePane, pos))
                {
                    openSelectedInViewer();
                }
                else
                {
                    previewHeld = true;
                    startFilePreview();
                }
                break;

            case 3: // Copy -> / <- Copy
                triggerTransfer(FileOpsWorker::OpType::kCopy);
                break;

            case 4: // Move -> / <- Move
                triggerTransfer(FileOpsWorker::OpType::kMove);
                break;

            case 5: // Delete
                triggerDelete();
                break;

            case 6: // Rename
                openRenameKeyboard();
                break;

            case 7: // Back
                exitToProjectEditor();
                break;
        }
    }
    else
    {
        switch (button)
        {
            case 0: // Select any item (including folders)
                toggleItemSelection(activePane, pos);
                refreshBrowseUI();
                break;

            case 1: // Select All / Clear Sel
                if (p.selectionCount > 0)
                {
                    clearPaneSelection(activePane);
                }
                else
                {
                    selectAllInPane(activePane);
                }
                refreshBrowseUI();
                break;

            case 2: // Preview Vol
                modalState = ModalState::kPreviewVolume;
                refreshActionButtonLabels();
                display.synchronizeRefresh();
                break;

            case 3: // Sync Dir (opposite pane -> active pane's directory)
            {
                int other = 1 - activePane;
                polystrncpy(panes[other].browserData.currentPath,
                            panes[activePane].browserData.currentPath,
                            kMaxFilePathLen + 1);
                panes[other].browserData.browserListPosition = 0;
                refreshPane(other);
                refreshBrowseUI();
                break;
            }

            case 4: // Info
                showInfoPopup();
                break;

            case 5: // Clear Sel
                clearPaneSelection(activePane);
                refreshBrowseUI();
                break;

            case 6: // Swap Panes
            {
                char tmpPath[kMaxFilePathLen + 1];
                polystrncpy(tmpPath, panes[0].browserData.currentPath, sizeof(tmpPath));
                polystrncpy(panes[0].browserData.currentPath,
                            panes[1].browserData.currentPath,
                            kMaxFilePathLen + 1);
                polystrncpy(panes[1].browserData.currentPath, tmpPath, kMaxFilePathLen + 1);
                std::swap(panes[0].browserData.browserListPosition,
                          panes[1].browserData.browserListPosition);
                refreshPane(0);
                refreshPane(1);
                refreshBrowseUI();
                break;
            }

            case 7: // Back
                exitToProjectEditor();
                break;
        }
    }
}

void cFileManagerModule::handleActionButtonRelease(uint8_t button)
{
    if (button == 2 && previewHeld)
    {
        previewHeld = false;
        stopFilePreview();
    }
}

// =========================================================================
// Operations: New Folder, Rename, Copy, Move, Paste, Delete
// =========================================================================

void cFileManagerModule::showKeyboardUI(const char* titleText, const char* confirmLabel)
{
    stopFilePreview();
    titleBar->setTitle(titleText);
    titleBar->showRightLabel(false);

    for (int p = 0; p < 2; ++p)
    {
        display.setControlHide(panes[p].headerControl);
        display.setControlHide(panes[p].listControl);
    }
    display.setControlHide(frameControl);

    for (uint8_t i = 0; i < 8; ++i)
    {
        display.setControlValue(label[i], 0);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlData(label[i], nullptr);
        display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
        display.setControlStyle(
          label[i], controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        display.setControlShow(label[i]);
    }

    // Matches Sample Loader keyboard bar (gSampleImport.cpp:694-697)
    display.setControlText(label[0], "Enter");
    display.setControlText(label[1], "Auto name");
    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], confirmLabel);

    for (uint8_t i = 0; i < 8; ++i)
    {
        display.refreshControl(label[i]);
    }
    display.refreshControl(bgLabel);
    display.synchronizeRefresh();
}

void cFileManagerModule::openNewFolderKeyboard()
{
    modalState = ModalState::kKeyboardNewFolder;
    keyboardManager.fillName("NewFolder");
    keyboardManager.activateKeyboard(30);
    showKeyboardUI("File Manager - New Folder", "Create");
}

void cFileManagerModule::openRenameKeyboard()
{
    PaneState& p = panes[activePane];
    int pos = p.browserData.browserListPosition;
    if (pos < 0 || pos >= p.browserData.browserListLength || isParentEntry(activePane, pos) ||
        p.selectionCount > 1)
    {
        return;
    }

    char fullPath[kMaxFilePathLen + 1];
    if (!getItemFullPath(activePane, pos, fullPath, sizeof(fullPath)))
    {
        return;
    }

    char activeProj[kMaxFilePathLen + 1];
    getActiveProjectPath(activeProj, sizeof(activeProj));
    if (FileOpsWorker::isProtectedPath(fullPath, activeProj))
    {
        mtPopups.show(0, "Cannot rename protected item");
        return;
    }

    polystrncpy(scratchState->renameOrigPath, fullPath, sizeof(scratchState->renameOrigPath));
    const char* rawName = p.browserData.browserList[pos];
    const char* cleanName = (rawName[0] == '/') ? (rawName + 1) : rawName;
    polystrncpy(scratchState->renameOrigName, cleanName, sizeof(scratchState->renameOrigName));

    modalState = ModalState::kKeyboardRename;
    keyboardManager.fillName(scratchState->renameOrigName);
    keyboardManager.activateKeyboard(60);
    showKeyboardUI("File Manager - Rename", "Rename");
}

void cFileManagerModule::handleKeyboardButton(uint8_t button)
{
    switch (button)
    {
        case 0: keyboardManager.confirmKey(); break;
        case 1: keyboardManager.setRandomName(); break;
        case 6: cancelKeyboard(); break;
        case 7: confirmKeyboard(); break;
        default: break;
    }
}

void cFileManagerModule::confirmKeyboard()
{
    const char* entered = keyboardManager.getName();
    if (hasInvalidFilenameChars(entered))
    {
        mtPopups.show(0, "Invalid name");
        return;
    }

    FileSystem* fs = newFileManager.getFileSystem();
    if (fs == nullptr)
    {
        cancelKeyboard();
        return;
    }

    char targetPath[kMaxFilePathLen + 1];
    if (!FileOpsWorker::joinPath(
          panes[activePane].browserData.currentPath, entered, targetPath, sizeof(targetPath)))
    {
        mtPopups.show(0, "Path too long");
        return;
    }

    if (modalState == ModalState::kKeyboardNewFolder)
    {
        if (fs->exists(targetPath))
        {
            mtPopups.show(0, "Folder already exists");
            return;
        }
        if (!fs->mkdir(0, targetPath, true))
        {
            mtPopups.show(0, "Create folder failed");
            return;
        }
        char createdName[kMaxFileNameLen + 1];
        polystrncpy(createdName, entered, sizeof(createdName));
        keyboardManager.deactivateKeyboard();
        refreshPane(activePane, createdName);
        refreshPane(1 - activePane);
        showBrowseScreen();
        mtPopups.show(0, "Folder created");
    }
    else if (modalState == ModalState::kKeyboardRename)
    {
        if (strcasecmp(scratchState->renameOrigPath, targetPath) == 0)
        {
            cancelKeyboard();
            return;
        }
        if (fs->exists(targetPath))
        {
            mtPopups.show(0, "Name already exists");
            return;
        }
        if (!fs->rename(scratchState->renameOrigPath, targetPath))
        {
            mtPopups.show(0, "Rename failed");
            return;
        }
        char renamedName[kMaxFileNameLen + 1];
        polystrncpy(renamedName, entered, sizeof(renamedName));
        keyboardManager.deactivateKeyboard();
        refreshPane(activePane, renamedName);
        refreshPane(1 - activePane);
        showBrowseScreen();
        mtPopups.show(0, "Renamed");
    }
}

void cFileManagerModule::cancelKeyboard()
{
    keyboardManager.deactivateKeyboard();
    showBrowseScreen();
}

void cFileManagerModule::triggerTransfer(FileOpsWorker::OpType op)
{
    int count = gatherTargetItems(activePane, pendingBatchScratch, FileOpsWorker::kMaxBatchItems);
    if (count <= 0)
    {
        return;
    }

    int dstPane = 1 - activePane;
    if (mtConfig.general.confirmFileDelete == 0) // Confirm all
    {
        pendingOpType = op;
        pendingOpItems = pendingBatchScratch;
        pendingOpCount = count;
        polystrncpy(scratchState->pendingOpSrcDir, panes[activePane].browserData.currentPath, sizeof(scratchState->pendingOpSrcDir));
        polystrncpy(scratchState->pendingOpDstDir, panes[dstPane].browserData.currentPath, sizeof(scratchState->pendingOpDstDir));
        pendingOpHasDst = true;

        const char* verb = (op == FileOpsWorker::OpType::kMove) ? "Move" : "Copy";
        if (count == 1)
        {
            snprintf(scratchState->popupLine1,
                     sizeof(scratchState->popupLine1),
                     "%s %s?",
                     verb,
                     pendingBatchScratch[0].isFolder ? "folder" : "file");
            snprintf(scratchState->popupLine2, sizeof(scratchState->popupLine2), "%.42s", pendingBatchScratch[0].name);
        }
        else
        {
            snprintf(scratchState->popupLine1, sizeof(scratchState->popupLine1), "%s %d items?", verb, count);
            snprintf(scratchState->popupLine2, sizeof(scratchState->popupLine2), "To %.38s", panes[dstPane].headerText);
        }
        showConfirmDeletePopup(scratchState->popupLine1, scratchState->popupLine2);
        return;
    }

    startWorkerBatch(op,
                     panes[activePane].browserData.currentPath,
                     panes[dstPane].browserData.currentPath,
                     pendingBatchScratch,
                     count);
}

void cFileManagerModule::triggerPaste()
{
    if (clipboardCount <= 0)
    {
        mtPopups.show(0, "Clipboard is empty");
        return;
    }

    if (mtConfig.general.confirmFileDelete == 0) // Confirm all
    {
        pendingOpType = FileOpsWorker::OpType::kCopy;
        pendingOpItems = clipboardItems;
        pendingOpCount = clipboardCount;
        polystrncpy(scratchState->pendingOpSrcDir, scratchState->clipboardSrcDir, sizeof(scratchState->pendingOpSrcDir));
        polystrncpy(scratchState->pendingOpDstDir, panes[activePane].browserData.currentPath, sizeof(scratchState->pendingOpDstDir));
        pendingOpHasDst = true;

        if (clipboardCount == 1)
        {
            snprintf(scratchState->popupLine1,
                     sizeof(scratchState->popupLine1),
                     "Paste %s?",
                     clipboardItems[0].isFolder ? "folder" : "file");
            snprintf(scratchState->popupLine2, sizeof(scratchState->popupLine2), "%.42s", clipboardItems[0].name);
        }
        else
        {
            snprintf(scratchState->popupLine1, sizeof(scratchState->popupLine1), "Paste %d items?", clipboardCount);
            snprintf(scratchState->popupLine2, sizeof(scratchState->popupLine2), "To %.38s", panes[activePane].headerText);
        }
        showConfirmDeletePopup(scratchState->popupLine1, scratchState->popupLine2);
        return;
    }

    startWorkerBatch(FileOpsWorker::OpType::kCopy,
                     scratchState->clipboardSrcDir,
                     panes[activePane].browserData.currentPath,
                     clipboardItems,
                     clipboardCount);
}

void cFileManagerModule::triggerDelete()
{
    int count = gatherTargetItems(activePane, pendingBatchScratch, FileOpsWorker::kMaxBatchItems);
    if (count <= 0)
    {
        return;
    }

    // Pre-check if all items are protected
    char activeProj[kMaxFilePathLen + 1];
    getActiveProjectPath(activeProj, sizeof(activeProj));

    bool hasFolder = false;
    int protectedCount = 0;
    for (int i = 0; i < count; ++i)
    {
        if (pendingBatchScratch[i].isFolder)
        {
            hasFolder = true;
        }
        char fullPath[kMaxFilePathLen + 1];
        if (FileOpsWorker::joinPath(panes[activePane].browserData.currentPath,
                                    pendingBatchScratch[i].name,
                                    fullPath,
                                    sizeof(fullPath)))
        {
            if (FileOpsWorker::isProtectedPath(fullPath, activeProj))
            {
                ++protectedCount;
            }
        }
    }

    if (protectedCount == count)
    {
        mtPopups.show(0, "Cannot delete protected item");
        return;
    }

    // Config -> General -> File Manager:
    // 0 = Confirm all, 1 = Confirm delete, 2 = Never ask
    if (mtConfig.general.confirmFileDelete == 2)
    {
        startWorkerBatch(FileOpsWorker::OpType::kDelete,
                         panes[activePane].browserData.currentPath,
                         nullptr,
                         pendingBatchScratch,
                         count);
        return;
    }

    pendingOpType = FileOpsWorker::OpType::kDelete;
    pendingOpItems = pendingBatchScratch;
    pendingOpCount = count;
    polystrncpy(scratchState->pendingOpSrcDir, panes[activePane].browserData.currentPath, sizeof(scratchState->pendingOpSrcDir));
    scratchState->pendingOpDstDir[0] = '\0';
    pendingOpHasDst = false;

    if (count == 1)
    {
        snprintf(scratchState->popupLine1,
                 sizeof(scratchState->popupLine1),
                 "Delete %s?",
                 pendingBatchScratch[0].isFolder ? "folder" : "file");
        snprintf(scratchState->popupLine2, sizeof(scratchState->popupLine2), "%.42s", pendingBatchScratch[0].name);
    }
    else
    {
        snprintf(scratchState->popupLine1, sizeof(scratchState->popupLine1), "Delete %d items?", count);
        polystrncpy(
          scratchState->popupLine2, hasFolder ? "Includes folders (permanent)" : "Permanent action", sizeof(scratchState->popupLine2));
    }

    showConfirmDeletePopup(scratchState->popupLine1, scratchState->popupLine2);
}

void cFileManagerModule::startWorkerBatch(FileOpsWorker::OpType op,
                                          const char* srcDir,
                                          const char* dstDir,
                                          const FileOpsWorker::BatchItem* items,
                                          int count)
{
    stopFilePreview();

    char activeProj[kMaxFilePathLen + 1];
    getActiveProjectPath(activeProj, sizeof(activeProj));

    if (!scratchState->worker.startBatch(op, srcDir, dstDir, items, count, activeProj))
    {
        mtPopups.show(0, scratchState->worker.getErrorMessage());
        return;
    }

    applyCollisionToAll = false;
    lastReportedProgress = -1;
    showProgressUI();
}

// =========================================================================
// Popups: Confirm Delete, Collision, Info, Progress
// =========================================================================

void cFileManagerModule::showConfirmDeletePopup(const char* line1, const char* line2)
{
    stopFilePreview();
    modalState = ModalState::kConfirmDelete;
    display.setControlHide(frameControl);

    for (uint8_t i = 0; i < 8; ++i)
    {
        display.setControlValue(label[i], 0);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlData(label[i], nullptr);
        display.setControlStyle(label[i],
                                controlStyleShow | controlStyleCenterX | controlStyleCenterY |
                                  controlStyleFont3);
        display.setControlColors(label[i], display.getTheme().inactiveButtonLabelsColors);
        display.setControlShow(label[i]);
    }

    // Matches Sample Loader confirmation bar: Btn 7 = Cancel, Btn 8 = Yes
    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], "Yes");
    display.setControlColors(label[6], display.getTheme().highlightedButtonLabelsColors);
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);

    for (uint8_t i = 0; i < 8; ++i)
    {
        display.refreshControl(label[i]);
    }

    mtPopups.showInfoPopup(line1, line2);
    display.synchronizeRefresh();
}

void cFileManagerModule::handleConfirmDeleteButton(uint8_t button)
{
    if (button == 6) // Cancel
    {
        dismissPopupToBrowse(false);
    }
    else if (button == 7) // Yes
    {
        mtPopups.hideInfoPopup();
        startWorkerBatch(pendingOpType,
                         scratchState->pendingOpSrcDir,
                         pendingOpHasDst ? scratchState->pendingOpDstDir : nullptr,
                         pendingOpItems,
                         pendingOpCount);
    }
}

void cFileManagerModule::showCollisionPopup()
{
    modalState = ModalState::kCollisionPrompt;
    display.setControlHide(frameControl);

    snprintf(scratchState->popupLine1, sizeof(scratchState->popupLine1), "Already exists:");
    snprintf(scratchState->popupLine2, sizeof(scratchState->popupLine2), "%.42s", scratchState->worker.getCollisionTargetName());

    for (uint8_t i = 0; i < 8; ++i)
    {
        display.setControlValue(label[i], 0);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlData(label[i], nullptr);
        display.setControlStyle(label[i],
                                controlStyleShow | controlStyleCenterX | controlStyleCenterY |
                                  controlStyleFont3);
        display.setControlColors(label[i], display.getTheme().inactiveButtonLabelsColors);
        display.setControlShow(label[i]);
    }

    display.setControlText(label[0], applyCollisionToAll ? "All: ON" : "All: OFF");
    display.setControlColors(label[0],
                             applyCollisionToAll ? display.getTheme().highlightedButtonLabelsColors
                                                 : display.getTheme().activeButtonLabelsColors);

    display.setControlText(label[4], "Rename");
    display.setControlColors(label[4], display.getTheme().activeButtonLabelsColors);

    display.setControlText(label[5], "Skip");
    display.setControlColors(label[5], display.getTheme().activeButtonLabelsColors);

    display.setControlText(label[6], "Cancel");
    display.setControlColors(label[6], display.getTheme().highlightedButtonLabelsColors);

    display.setControlText(label[7], "Overwrite");
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);

    for (uint8_t i = 0; i < 8; ++i)
    {
        display.refreshControl(label[i]);
    }

    mtPopups.showInfoPopup(scratchState->popupLine1, scratchState->popupLine2);
    display.synchronizeRefresh();
}

void cFileManagerModule::handleCollisionButton(uint8_t button)
{
    if (button == 0)
    {
        applyCollisionToAll = !applyCollisionToAll;
        display.setControlText(label[0], applyCollisionToAll ? "All: ON" : "All: OFF");
        display.setControlColors(label[0],
                                 applyCollisionToAll
                                   ? display.getTheme().highlightedButtonLabelsColors
                                   : display.getTheme().activeButtonLabelsColors);
        display.refreshControl(label[0]);
        display.synchronizeRefresh();
        return;
    }

    FileOpsWorker::CollisionAction action = FileOpsWorker::CollisionAction::kNone;
    switch (button)
    {
        case 4: action = FileOpsWorker::CollisionAction::kAutoRename; break;
        case 5: action = FileOpsWorker::CollisionAction::kSkip; break;
        case 6: action = FileOpsWorker::CollisionAction::kCancel; break;
        case 7: action = FileOpsWorker::CollisionAction::kOverwrite; break;
        default: return;
    }

    mtPopups.hideInfoPopup();
    scratchState->worker.resolveCollision(action, applyCollisionToAll);
    if (scratchState->worker.getState() == FileOpsWorker::State::kRunning)
    {
        showProgressUI();
    }
    else
    {
        scratchState->worker.reset();
        dismissPopupToBrowse(true);
    }
}

void cFileManagerModule::formatProgressTitle()
{
    if (scratchState->worker.isScanning())
    {
        polystrncpy(scratchState->progressTitleText, "Calculating size...", sizeof(scratchState->progressTitleText));
        return;
    }

    const char* verb = "Working";
    switch (scratchState->worker.getOpType())
    {
        case FileOpsWorker::OpType::kCopy: verb = "Copying"; break;
        case FileOpsWorker::OpType::kMove: verb = "Moving"; break;
        case FileOpsWorker::OpType::kDelete: verb = "Deleting"; break;
        default: break;
    }

    int cur = scratchState->worker.getDisplayCurrentIndex();
    int tot = scratchState->worker.getDisplayTotalCount();
    if (tot > 0)
    {
        snprintf(scratchState->progressTitleText, sizeof(scratchState->progressTitleText), "%s (%d/%d)...", verb, cur, tot);
    }
    else
    {
        snprintf(scratchState->progressTitleText, sizeof(scratchState->progressTitleText), "%s...", verb);
    }
}

void cFileManagerModule::showProgressUI()
{
    modalState = ModalState::kProgress;
    display.setControlHide(frameControl);

    for (uint8_t i = 0; i < 8; ++i)
    {
        display.setControlValue(label[i], 0);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlData(label[i], nullptr);
        display.setControlStyle(label[i],
                                controlStyleShow | controlStyleCenterX | controlStyleCenterY |
                                  controlStyleFont3);
        display.setControlColors(label[i], display.getTheme().inactiveButtonLabelsColors);
    }

    display.setControlText(label[6], "Cancel");
    display.setControlColors(label[6], display.getTheme().highlightedButtonLabelsColors);
    for (uint8_t i = 0; i < 8; ++i)
    {
        display.refreshControl(label[i]);
    }

    lastReportedCurIdx = scratchState->worker.getDisplayCurrentIndex();
    lastReportedTotal = scratchState->worker.getDisplayTotalCount();
    lastReportedScanning = scratchState->worker.isScanning();
    lastReportedProgress = scratchState->worker.getProgressPercent();
    formatProgressTitle();

    mtPopups.showProgressPopup(scratchState->progressTitleText);
    mtPopups.changePopupProgress(static_cast<int8_t>(lastReportedProgress));
    display.synchronizeRefresh();
}

void cFileManagerModule::handleProgressButton(uint8_t button)
{
    if (button == 6)
    {
        scratchState->worker.cancel();
    }
}

void cFileManagerModule::showInfoPopup()
{
    PaneState& p = panes[activePane];
    int pos = p.browserData.browserListPosition;
    if (pos < 0 || pos >= p.browserData.browserListLength || isParentEntry(activePane, pos))
    {
        return;
    }

    char fullPath[kMaxFilePathLen + 1];
    if (!getItemFullPath(activePane, pos, fullPath, sizeof(fullPath)))
    {
        return;
    }

    FileSystem* fs = newFileManager.getFileSystem();
    if (fs == nullptr)
    {
        return;
    }

    snprintf(scratchState->popupLine1, sizeof(scratchState->popupLine1), "%.42s", p.browserData.browserList[pos]);
    if (isFolderAt(activePane, pos))
    {
        int files = fs->countContainedFiles(fullPath);
        snprintf(scratchState->popupLine2, sizeof(scratchState->popupLine2), "Folder (%d files)", files);
    }
    else
    {
        char sizeBuf[24];
        formatSizeShort(fs->getFileSize(fullPath), sizeBuf, sizeof(sizeBuf));
        snprintf(scratchState->popupLine2, sizeof(scratchState->popupLine2), "File - %s", sizeBuf);
    }

    modalState = ModalState::kInfoPopup;
    display.setControlHide(frameControl);
    for (uint8_t i = 0; i < 8; ++i)
    {
        display.setControlValue(label[i], 0);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlData(label[i], nullptr);
        display.setControlStyle(label[i],
                                controlStyleShow | controlStyleCenterX | controlStyleCenterY |
                                  controlStyleFont3);
        display.setControlColors(label[i], display.getTheme().inactiveButtonLabelsColors);
    }
    display.setControlText(label[7], "OK");
    display.setControlColors(label[7], display.getTheme().highlightedButtonLabelsColors);
    for (uint8_t i = 0; i < 8; ++i)
    {
        display.refreshControl(label[i]);
    }

    mtPopups.showInfoPopup(scratchState->popupLine1, scratchState->popupLine2);
    display.synchronizeRefresh();
}

void cFileManagerModule::handleInfoPopupButton(uint8_t button)
{
    if (button == 6 || button == 7)
    {
        dismissPopupToBrowse(false);
    }
}

void cFileManagerModule::dismissPopupToBrowse(bool refreshBothPanes)
{
    mtPopups.hideInfoPopup();
    mtPopups.hideProgressPopup();
    if (refreshBothPanes)
    {
        refreshPane(0);
        refreshPane(1);
    }
    showBrowseScreen();
}

void cFileManagerModule::exitToProjectEditor()
{
    stopFilePreview();
    mtInterface.deactivateModule(this);
    mtInterface.activateModule(&projectEditor, 0);
}
