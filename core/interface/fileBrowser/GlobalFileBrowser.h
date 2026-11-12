#ifndef INTERFACE_FILEBROWSER_GLOBALFILEBROWSER_H_
#define INTERFACE_FILEBROWSER_GLOBALFILEBROWSER_H_

#include "FileBrowser.h"
#include "core/modulesBase.h"
#include "mtKeyboardManager.h"

#include <cstdint>
#include <functional>
#include <string>

struct GlobalFileBrowserConfig
{
    enum class Mode : uint8_t
    {
        kLoadWav,
        kSaveWav,
        kLoadGenericFile,
        kSaveGenericFile
    };

    Mode mode = Mode::kLoadWav;
    std::string title = "Load File";
    std::string initialDirectory = "/Samples";
    std::string defaultFileName = "sample";
    std::string extensionFilter = ".wav";
    ModuleBase* returnModule = nullptr;
    uint32_t returnModuleOptions = 0;
    std::function<void(const std::string& fullPath)> onConfirm;
    std::function<void()> onCancel;
};

class cGlobalFileBrowser : public ModuleBase
{
  public:
    cGlobalFileBrowser() = default;
    virtual ~cGlobalFileBrowser() {}

    void initDisplayControls() override;
    void destroyDisplayControls() override {}
    void start(uint32_t options) override;
    void stop() override;
    void update() override;

    void configure(const GlobalFileBrowserConfig& cfg);

    void showBrowserScreen();
    void refreshBrowserScreen();
    void showKeyboardScreen(const char* titleText, const char* confirmLabel);

    void setMainFunctions();

    void handleEncoderOrArrows(int16_t delta, bool isEncoder);
    void handleEnterOrInsert();
    void handleActionButtonPress(uint8_t button);
    void handleActionButtonRelease(uint8_t button);
    void handleKeyboardButtonPress(uint8_t button);

    void startFilePreview();
    void stopFilePreview();
    void updatePreviewVolumeText(bool showDb);

    void openNewFolderKeyboard();
    void openSaveFileKeyboard();
    void confirmKeyboard();
    void cancelKeyboard();

    void confirmLoadSelectedFile();
    void cancelBrowser();

    bool isFileSelected() const;

    GlobalFileBrowserConfig config;
    FileBrowser fileBrowser;
    FileBrowserData fileBrowserData{};
    strList explorerList{};
    strFrameData frameData{};

    uint8_t selectedPlace = 0; // 0 = file list, 1 = preview volume

    enum class KeyboardPurpose : uint8_t
    {
        kNone = 0,
        kNewFolder,
        kSaveFile
    };
    KeyboardPurpose keyboardPurpose = KeyboardPurpose::kNone;

  private:
    void returnToCallerModule();

    hControl label[8]{};
    hControl bgLabel = -1;
    hControl frameControl = -1;
    hControl explorerLabelControl = -1;
    hControl explorerListControl = -1;
    hControl previewVolumeBarControl = -1;
    hControl middleCoverControl = -1;
    hControl rightCoverControl = -1;

    char explorerPathText[kMaxFilePathLen + 4]{};
    char customExtBuffer[16]{};
    char previewVolumeText[24]{};
    uint32_t explorerHeaderColors[4]{};
};

extern cGlobalFileBrowser globalFileBrowser;

void openGlobalFileBrowser(const GlobalFileBrowserConfig& config);

#endif /* INTERFACE_FILEBROWSER_GLOBALFILEBROWSER_H_ */
