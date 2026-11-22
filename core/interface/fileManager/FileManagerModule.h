#ifndef INTERFACE_FILEMANAGER_FILEMANAGERMODULE_H_
#define INTERFACE_FILEMANAGER_FILEMANAGERMODULE_H_

#include "FileBrowser.h"
#include "FileOpsWorker.h"
#include "core/modulesBase.h"
#include "mtKeyboardManager.h"

#include <cstdint>

class cFileManagerModule : public ModuleBase
{
  public:
    cFileManagerModule() = default;
    virtual ~cFileManagerModule() = default;

    void initDisplayControls() override;
    void destroyDisplayControls() override {}
    void start(uint32_t options) override;
    void stop() override;
    void update() override;

    enum class ModalState : uint8_t
    {
        kBrowse = 0,
        kConfirmDelete,
        kCollisionPrompt,
        kProgress,
        kKeyboardNewFolder,
        kKeyboardRename,
        kInfoPopup,
        kPreviewVolume
    };

    void setMainFunctions();

    void handleEncoderOrArrows(int16_t delta, bool isEncoder);
    void handleLeftRight(int dir);
    void handleEnter();
    void handleHome();
    void handleShiftState(uint8_t state);
    void handleCopyButton();
    void handleDeleteButton();
    void handleActionButtonPress(uint8_t button);
    void handleActionButtonRelease(uint8_t button);
    void handlePadInput(uint8_t pad, uint8_t state, int16_t velo);

    void startFilePreview();
    void stopFilePreview();

    ModalState getModalState() const { return modalState; }
    bool isKeyboardActive() const { return keyboardManager.getState() != 0; }

    // Active-only cold strings & worker live inside externalRamScratchBuff (0 B internal RAM)
    struct ActiveScratchState
    {
        FileOpsWorker worker;
        char clipboardSrcDir[kMaxFilePathLen + 1];
        char renameOrigPath[kMaxFilePathLen + 1];
        char renameOrigName[kMaxFileNameLen + 1];
        char pendingOpSrcDir[kMaxFilePathLen + 1];
        char pendingOpDstDir[kMaxFilePathLen + 1];
        char popupLine1[80];
        char popupLine2[80];
        char progressTitleText[48];
        char toastText[64];
        char titleRightText[32];
        char previewVolText[24];
    };

  private:
    struct PaneState
    {
        FileBrowser browser;
        FileBrowserData browserData{};
        strList listControlData{};
        hControl headerControl = -1;
        hControl listControl = -1;
        char headerText[48]{};
        int selectionCount = 0;
        int shiftAnchor = -1;
    };

    void setupPaneScratch(int paneIdx, uint8_t* browserBase);
    void refreshPane(int paneIdx, const char* selectName = nullptr);
    void syncPaneListControl(int paneIdx);
    void updatePaneHeaderText(int paneIdx);
    void clearPaneSelection(int paneIdx);
    void toggleItemSelection(int paneIdx, int pos);
    void selectAllInPane(int paneIdx);
    void applyShiftRangeSelection(int paneIdx, int prevPos, int newPos);
    void recountPaneSelection(int paneIdx);
    void rewindPaneToTop(int paneIdx);

    bool isParentEntry(int paneIdx, int pos) const;
    bool isFolderAt(int paneIdx, int pos) const;
    bool isWavFileAt(int paneIdx, int pos) const;
    bool isViewableFileAt(int paneIdx, int pos) const;
    bool getItemFullPath(int paneIdx, int pos, char* outPath, int outSize) const;
    void openSelectedInViewer();
    void getActiveProjectPath(char* outPath, int outSize) const;

    int gatherTargetItems(int paneIdx, FileOpsWorker::BatchItem* outItems, int maxItems) const;

    void showBrowseScreen();
    void refreshBrowseUI();
    void refreshActionButtonLabels();
    void updateTitleBarStatus();

    void openNewFolderKeyboard();
    void openRenameKeyboard();
    void showKeyboardUI(const char* titleText, const char* confirmLabel);
    void confirmKeyboard();
    void cancelKeyboard();

    void triggerTransfer(FileOpsWorker::OpType op);
    void triggerPaste();
    void triggerDelete();
    void startWorkerBatch(FileOpsWorker::OpType op,
                          const char* srcDir,
                          const char* dstDir,
                          const FileOpsWorker::BatchItem* items,
                          int count);

    void showConfirmDeletePopup(const char* line1, const char* line2);
    void showCollisionPopup();
    void showInfoPopup();
    void showProgressUI();
    void formatProgressTitle();
    void dismissPopupToBrowse(bool refreshBothPanes);

    void handleConfirmDeleteButton(uint8_t button);
    void handleCollisionButton(uint8_t button);
    void handleProgressButton(uint8_t button);
    void handleKeyboardButton(uint8_t button);
    void handleInfoPopupButton(uint8_t button);

    void exitToProjectEditor();

    PaneState panes[2];
    int activePane = 0;
    ModalState modalState = ModalState::kBrowse;

    bool shiftHeld = false;
    bool previewHeld = false;
    bool applyCollisionToAll = false;

    // Remember pane cursor positions across visits (paths live in EXTERNAL_RAM)
    int savedPanePos[2]{ 0, 0 };
    int savedActivePane = 0;
    bool hasSavedPaths = false;

    ActiveScratchState* scratchState = nullptr;

    // Clipboard & pending op pointers (item arrays live in externalRamScratchBuff)
    FileOpsWorker::BatchItem* clipboardItems = nullptr;
    int clipboardCount = 0;

    FileOpsWorker::BatchItem* pendingBatchScratch = nullptr;
    FileOpsWorker::OpType pendingOpType = FileOpsWorker::OpType::kNone;
    const FileOpsWorker::BatchItem* pendingOpItems = nullptr;
    int pendingOpCount = 0;
    bool pendingOpHasDst = false;

    int lastReportedProgress = -1;
    int lastReportedCurIdx = -1;
    int lastReportedTotal = -1;
    bool lastReportedScanning = false;

    // UI Controls
    hControl label[8]{};
    strLabelData transferArrowData[2]{};
    hControl bgLabel = -1;
    hControl frameControl = -1;
    strFrameData frameData{};

    uint32_t headerColorsActive[4]{};
    uint32_t headerColorsInactive[4]{};
};

extern cFileManagerModule fileManagerModule;

#endif /* INTERFACE_FILEMANAGER_FILEMANAGERMODULE_H_ */
