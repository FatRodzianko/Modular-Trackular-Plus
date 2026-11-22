#ifndef INTERFACE_INFOVIEWER_H_
#define INTERFACE_INFOVIEWER_H_

#include "config.h"
#include "core/modulesBase.h"

#include <cstdint>

class cInfoViewer : public ModuleBase
{
  public:
    virtual void update() override;
    virtual void start(uint32_t options) override;
    virtual void stop() override;

    virtual void initDisplayControls() override;
    virtual void destroyDisplayControls() override {}

    cInfoViewer() {}
    ~cInfoViewer() {}

    static bool isImageFile(const char* name);
    static bool isTextFile(const char* name);
    static bool isViewableFile(const char* name);

    void openFile(const char* dirPath, const char* fileName, ModuleBase* callerModule);

    void setDefaultScreenFunct();
    void showDefaultScreen();

    enum class PageType : uint8_t
    {
        kNone = 0,
        kImage,
        kText,
    };

    void scanSiblingFiles();
    void refreshCurrentFile();
    void nextFile();
    void previousFile();
    void scrollText(int deltaLines);
    void closeViewer();

    PageType getCurrentFileType() const;

    char titleText[64]{};
    char scrollStatusText[24]{};

    strLabelData doubleArrow[2]{};
    strFrameData frameData{};

    hControl label[8]{};
    hControl bgLabel = -1;

    hControl image = -1;
    hControl infoTextBox = -1;

    int currentFileIndex = 0;
    int filesCount = 0;
    char** siblingFiles = nullptr;

    // Scrollable text state (backed by externalRamScratchBuff)
    char* textFileBuf = nullptr;
    uint32_t* lineOffsets = nullptr;
    int totalTextLines = 0;
    int scrollLine = 0;

    uint8_t refreshImageState = 0;

    char currentDir[kMaxFilePathLen + 1]{};
    char initialFileName[kMaxFileNameLen + 1]{};
    ModuleBase* returnModule = nullptr;

  private:
    void buildLineIndex();
    void refreshTextWindow();
    void updateActionButtons();
};

extern cInfoViewer infoViewer;

#endif /* INTERFACE_INFOVIEWER_H_ */
