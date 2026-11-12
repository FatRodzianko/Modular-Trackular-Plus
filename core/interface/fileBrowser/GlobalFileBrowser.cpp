#include "GlobalFileBrowser.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "ampLogarythmicValues.h"
#include "config.h"
#include "core/interface.h"
#include "core/interfacePopups.h"
#include "core/songTimer.h"
#include "core/titleBar.h"
#include "display.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtConfig.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "util.h"
#include "util_math.h"
#include "util_string.h"

#include <cmath>
#include <cstring>

extern uint8_t externalRamScratchBuff[];

cGlobalFileBrowser globalFileBrowser;

static const uint16_t s_gfbFramesPlaces[2][4] = {
    { 0 + 1, 31, (800 / 8) * 3 - 3, 391 },
    { (800 / 8) * 6 + 1, 31, (800 / 8) * 1 - 3, 391 },
};

// Use top 24 KB of externalRamScratchBuff ([744 KB .. 768 KB]) so sdStream ([0 .. 320 KB])
// and .ptfx plugin code/scratch ([0 .. 744 KB]) are never overwritten.
static constexpr uint32_t kBrowserScratchBytes = 24 * 1024;
static constexpr uint32_t kBrowserScratchOffset =
  SHARED_SCRATCH_MEMORY_SIZE - kBrowserScratchBytes;
static_assert(kMaxFilesListLength * (kMaxFileNameLen + sizeof(char*)) <= kBrowserScratchBytes,
              "GlobalFileBrowser scratch exceeds 24 KB");

void openGlobalFileBrowser(const GlobalFileBrowserConfig& config)
{
    globalFileBrowser.configure(config);
    if (config.returnModule != nullptr)
    {
        mtInterface.deactivateModule(config.returnModule);
    }
    mtInterface.activateModule(&globalFileBrowser, 0);
}

static void gfbEncoderTurn(int16_t dir)
{
    if (globalFileBrowser.keyboardManager.getState())
    {
#ifdef TRACKER_MINI
        globalFileBrowser.keyboardManager.confirmKey();
#endif
        return;
    }
    globalFileBrowser.handleEncoderOrArrows(dir, true);
}

static void gfbArrowUp()
{
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.makeMove('w');
        return;
    }
    globalFileBrowser.handleEncoderOrArrows(1, false);
}

static void gfbArrowDown()
{
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.makeMove('s');
        return;
    }
    globalFileBrowser.handleEncoderOrArrows(-1, false);
}

static void gfbArrowLeft()
{
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.makeMove('a');
        return;
    }
    if (globalFileBrowser.selectedPlace == 1)
    {
        globalFileBrowser.selectedPlace = 0;
        globalFileBrowser.refreshBrowserScreen();
    }
}

static void gfbArrowRight()
{
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.makeMove('d');
        return;
    }
    if (globalFileBrowser.selectedPlace == 0)
    {
        globalFileBrowser.selectedPlace = 1;
        globalFileBrowser.refreshBrowserScreen();
    }
}

static void gfbEnterButton()
{
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.confirmKey();
        return;
    }
    globalFileBrowser.handleEnterOrInsert();
}

static void gfbDeleteButton()
{
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.makeBackspace();
        return;
    }
    if (globalFileBrowser.selectedPlace == 1)
    {
        mtConfig.general.previewVolume = PREVIEW_VOLUME_DEFAULT;
        engine.setPreviewVolume(mtConfig.general.previewVolume);
        saveConfig();
        globalFileBrowser.refreshBrowserScreen();
    }
}

static void gfbPadChange(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    if (globalFileBrowser.keyboardManager.getState())
    {
        globalFileBrowser.keyboardManager.onPadChange(pad, state);
    }
}

static void gfbScreenButton(uint8_t button, uint8_t state)
{
    if (globalFileBrowser.keyboardManager.getState())
    {
        if (state == buttonPress)
        {
            globalFileBrowser.handleKeyboardButtonPress(button);
        }
        return;
    }

    if (state == buttonPress)
    {
        globalFileBrowser.handleActionButtonPress(button);
    }
    else if (state == buttonRelease)
    {
        globalFileBrowser.handleActionButtonRelease(button);
    }
}

static void gfbBtn0(uint8_t state) { gfbScreenButton(interfaceButton0, state); }
static void gfbBtn1(uint8_t state) { gfbScreenButton(interfaceButton1, state); }
static void gfbBtn2(uint8_t state) { gfbScreenButton(interfaceButton2, state); }
static void gfbBtn3(uint8_t state) { gfbScreenButton(interfaceButton3, state); }
static void gfbBtn4(uint8_t state) { gfbScreenButton(interfaceButton4, state); }
static void gfbBtn5(uint8_t state) { gfbScreenButton(interfaceButton5, state); }
static void gfbBtn6(uint8_t state) { gfbScreenButton(interfaceButton6, state); }
static void gfbBtn7(uint8_t state) { gfbScreenButton(interfaceButton7, state); }

void cGlobalFileBrowser::configure(const GlobalFileBrowserConfig& cfg)
{
    config = cfg;
}

void cGlobalFileBrowser::initDisplayControls()
{
    strControlProperties prop{};
    frameData.placesCount = 2;
    frameData.startPlace = 0;
    frameData.places[0] = &s_gfbFramesPlaces[0][0];
    frameData.places[1] = &s_gfbFramesPlaces[1][0];
    prop.style = 0;
    prop.value = 0;
    prop.data = &frameData;
    frameControl = display.createControl<cFrame>(&prop);

    strControlProperties prop2{};
    prop2.value = 1;
    prop2.colors = display.getTheme().activeLabelsColors;
    prop2.style = (controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
    prop2.y = 424;
    prop2.h = 55;

    for (uint8_t i = 0; i < 8; i++)
    {
        prop2.x = (800 / 8) * i + (800 / 16);
        prop2.w = 800 / 8 - 6;
        label[i] = display.createControl<cLabel>(&prop2);
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
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

    prop2.value = 0;
    prop2.colors = display.getTheme().topBarLabelColors;
    prop2.style = (controlStyleShow | controlStyleBackground | controlStyleCenterY |
                   controlStyleCenterX | controlStyleFont4);
    prop2.x = (800 / 16) * 3 - 1;
    prop2.w = (800 / 8) * 3 - 3;
    prop2.y = 32;
    prop2.h = 26;
    explorerLabelControl = display.createControl<cLabel>(&prop2);

    explorerList.start = 0;
    explorerList.length = 0;
    explorerList.linesCount = 13;
    explorerList.data = nullptr;
    explorerList.lineWidthMax = (800 / 8) * 3 - 30;
    explorerList.lineTextLenMax = 32;
    prop.x = 0 + 1;
    prop.y = 29 + 32;
    prop.w = (800 / 8) * 3 - 3;
    prop.h = 394 - 32;
    prop.style = controlStyleBackground |
                 (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    prop.data = &explorerList;
    explorerListControl = display.createControl<cList>(&prop);

    strControlProperties coverProp{};
    coverProp.value = 0;
    coverProp.data = nullptr;
    coverProp.text = nullptr;
    coverProp.colors = display.getTheme().blankSpaceCoverLabelColors;
    coverProp.style = controlStyleBackground |
                      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    coverProp.x = (800 / 8) * 3 + 1;
    coverProp.w = (800 / 8) * 3 - 3;
    coverProp.y = 29;
    coverProp.h = 394;
    middleCoverControl = display.createControl<cLabel>(&coverProp);

    strControlProperties previewVolumeProperties{};
    previewVolumeProperties.x = (800 / 8) * 6 + 1;
    previewVolumeProperties.w = 800 / 8 - 3;
    previewVolumeProperties.y = 29;
    previewVolumeProperties.h = 394;
    previewVolumeProperties.style =
      controlStyleValue_0_100 | controlStyleBackground |
      (display.getTheme().barShadow > 0 ? controlStyleGradient : 0) |
      (display.getTheme().bottomScreenShadow > 0 ? controlStyleBottomShadow : 0);
    previewVolumeBarControl = display.createControl<cBar>(&previewVolumeProperties);

    coverProp.x = (800 / 8) * 7 + 1;
    coverProp.w = (800 / 8) * 1 - 2;
    rightCoverControl = display.createControl<cLabel>(&coverProp);
}

void cGlobalFileBrowser::start(uint32_t options)
{
    (void)options;
    moduleRefresh = 1;

    selectedPlace = 0;
    keyboardPurpose = KeyboardPurpose::kNone;

    uint8_t* scratchBase = externalRamScratchBuff + kBrowserScratchOffset;
    memset(&fileBrowserData, 0, sizeof(fileBrowserData));
    fileBrowserData.mode = kSamplesBrowser;
    fileBrowserData.selectionEnabled = false;
    fileBrowserData.maxFileNameLen = kMaxFileNameLen;
    fileBrowserData.maxBrowserListLen = kMaxFilesListLength;
    fileBrowserData.fileNamesBufferSize = kMaxFilesListLength * kMaxFileNameLen;
    fileBrowserData.fileNamesBuffer = reinterpret_cast<char*>(scratchBase);
    fileBrowserData.browserList =
      reinterpret_cast<char**>(scratchBase + kMaxFilesListLength * kMaxFileNameLen);

    polystrncpy(customExtBuffer, config.extensionFilter.c_str(), sizeof(customExtBuffer));
    fileBrowserData.customExtension = customExtBuffer;

    // Normalize initialDirectory (strip leading and trailing slashes for FileBrowser)
    std::string initDir = config.initialDirectory;
    while (!initDir.empty() && initDir.front() == '/')
    {
        initDir.erase(initDir.begin());
    }
    while (!initDir.empty() && initDir.back() == '/')
    {
        initDir.pop_back();
    }

    FileSystem* fs = newFileManager.getFileSystem();
    if (!initDir.empty() && fs != nullptr && !fs->exists(initDir.c_str()))
    {
        if (fs->exists("Samples"))
        {
            initDir = "Samples";
        }
        else
        {
            initDir.clear();
        }
    }

    polystrncpy(fileBrowserData.currentPath, initDir.c_str(), sizeof(fileBrowserData.currentPath));
    fileBrowserData.defaultPath = fileBrowserData.currentPath;
    fileBrowserData.browserListPosition = 0;

    if (fs != nullptr)
    {
        fileBrowser.start(fs, &fileBrowserData);
    }

    setMainFunctions();
    showBrowserScreen();
}

void cGlobalFileBrowser::stop()
{
    moduleRefresh = 0;
    stopFilePreview();
}

void cGlobalFileBrowser::update()
{
}

bool cGlobalFileBrowser::isFileSelected() const
{
    if (fileBrowserData.browserListLength <= 0)
    {
        return false;
    }
    auto& fb = const_cast<FileBrowser&>(fileBrowser);
    if (!fb.isRoot() && fileBrowserData.browserListPosition == 0)
    {
        return false; // "/.." parent entry
    }
    return !fb.isFolderSelected();
}

void cGlobalFileBrowser::showBrowserScreen()
{
    titleBar->setTitle(config.title.c_str());
    titleBar->show();

    display.setControlShow(explorerLabelControl);
    display.setControlShow(explorerListControl);
    display.setControlShow(middleCoverControl);
    display.setControlShow(previewVolumeBarControl);
    display.setControlShow(rightCoverControl);
    display.setControlShow(frameControl);
    display.setControlShow(bgLabel);

    refreshBrowserScreen();
}

void cGlobalFileBrowser::refreshBrowserScreen()
{
    if (fileBrowser.isRoot())
    {
        polystrncpy(explorerPathText, "/", sizeof(explorerPathText));
    }
    else
    {
        snprintf(
          explorerPathText, sizeof(explorerPathText), "/%s", fileBrowser.getCurrentPath());
    }

    uint32_t* explorerListColors = (selectedPlace == 0)
                                     ? display.getTheme().sampleLoaderActiveListColors
                                     : display.getTheme().sampleLoaderInactiveListColors;
    display.setControlColors(explorerListControl, explorerListColors);

    explorerHeaderColors[0] = explorerListColors[4];
    explorerHeaderColors[1] = explorerListColors[3];
    explorerHeaderColors[2] = explorerListColors[0];
    explorerHeaderColors[3] = explorerListColors[5];
    display.setControlColors(explorerLabelControl, explorerHeaderColors);
    display.setControlText(explorerLabelControl, explorerPathText);

    explorerList.data = fileBrowserData.browserList;
    explorerList.length = fileBrowserData.browserListLength;
    explorerList.start = fileBrowserData.browserListPosition;
    display.setControlData(explorerListControl, &explorerList);
    display.setControlValue(explorerListControl, fileBrowserData.browserListPosition);

    display.setControlValue(
      previewVolumeBarControl, mtConfig.general.previewVolume);

    display.setControlValue(frameControl, selectedPlace);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 0);
        display.setControlStyle(label[i],
                                controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont2);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
        display.setControlShow(label[i]);
    }

    bool fileSelected = isFileSelected();
    bool isSaveMode = (config.mode == GlobalFileBrowserConfig::Mode::kSaveWav ||
                       config.mode == GlobalFileBrowserConfig::Mode::kSaveGenericFile);

    if (fileBrowserData.browserListLength > 0)
    {
        if (!fileSelected)
        {
            display.setControlText(label[0], "Enter");
        }
        else if (!isSaveMode)
        {
            display.setControlText(label[0], "Load");
        }
        else
        {
            display.setControlColors(label[0], display.getTheme().inactiveLabelsColors);
        }
    }
    else
    {
        display.setControlColors(label[0], display.getTheme().inactiveLabelsColors);
    }

    display.setControlColors(label[1], display.getTheme().inactiveLabelsColors);

    if (fileSelected && (config.mode == GlobalFileBrowserConfig::Mode::kLoadWav ||
                         config.mode == GlobalFileBrowserConfig::Mode::kSaveWav))
    {
        display.setControlText(label[2], "Preview");
    }
    else
    {
        display.setControlText(label[2], "Preview");
        display.setControlColors(label[2], display.getTheme().inactiveLabelsColors);
    }

    display.setControlText(label[3], "New Folder");
    display.setControlColors(label[4], display.getTheme().inactiveLabelsColors);
    display.setControlText(label[5], "Cancel");
    display.setControlColors(label[6], display.getTheme().activeLabelsColors);
    display.setControlText(label[6], "Preview Vol");
    display.setControlValue(label[6], 1);
    updatePreviewVolumeText(selectedPlace == 1);

    if (isSaveMode)
    {
        display.setControlText(label[7], "Save Here");
    }
    else
    {
        display.setControlText(label[7], "Load");
        if (!fileSelected)
        {
            display.setControlColors(label[7], display.getTheme().inactiveLabelsColors);
        }
    }

    display.refreshControl(explorerLabelControl);
    display.refreshControl(explorerListControl);
    display.refreshControl(middleCoverControl);
    display.refreshControl(previewVolumeBarControl);
    display.refreshControl(rightCoverControl);
    display.refreshControl(frameControl);
    display.refreshControl(bgLabel);
    for (uint8_t i = 0; i < 8; i++)
    {
        display.refreshControl(label[i]);
    }
    display.synchronizeRefresh();
}

void cGlobalFileBrowser::updatePreviewVolumeText([[maybe_unused]] bool showDb)
{
    float db = 20.0f * log10f((float)mixAmpLogValues[mtConfig.general.previewVolume]);
    snprintf(previewVolumeText, sizeof(previewVolumeText), "%0.2f dB", (double)db);
    display.setControlText2(label[6], previewVolumeText);
    display.refreshControl(label[6]);
}

void cGlobalFileBrowser::setMainFunctions()
{
    ip->clearAll();

    ip->setPotObj(interfacePot0, gfbEncoderTurn);

    ip->setButtonObj(interfaceButtonUp, buttonPress, gfbArrowUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, gfbArrowDown);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, gfbArrowLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, gfbArrowRight);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, gfbEnterButton);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, gfbDeleteButton);

    ip->setButtonObj(interfaceButton0, gfbBtn0);
    ip->setButtonObj(interfaceButton1, gfbBtn1);
    ip->setButtonObj(interfaceButton2, gfbBtn2);
    ip->setButtonObj(interfaceButton3, gfbBtn3);
    ip->setButtonObj(interfaceButton4, gfbBtn4);
    ip->setButtonObj(interfaceButton5, gfbBtn5);
    ip->setButtonObj(interfaceButton6, gfbBtn6);
    ip->setButtonObj(interfaceButton7, gfbBtn7);

    ip->setPadsGlobal(gfbPadChange);
}

void cGlobalFileBrowser::handleEncoderOrArrows(int16_t delta, bool isEncoder)
{
    if (selectedPlace == 1)
    {
        mtConfig.general.previewVolume = clamp((int)mtConfig.general.previewVolume + delta,
                                               (int)PREVIEW_VOLUME_MIN,
                                               (int)PREVIEW_VOLUME_MAX);
        engine.setPreviewVolume(mtConfig.general.previewVolume);
        saveConfig();
        display.setControlValue(previewVolumeBarControl, mtConfig.general.previewVolume);
        display.refreshControl(previewVolumeBarControl);
        updatePreviewVolumeText(true);
        display.synchronizeRefresh();
        return;
    }

    if (fileBrowserData.browserListLength <= 0)
    {
        return;
    }

    bool invert = !isEncoder || interfaceGlobals.invertValForList;
    int step = invert ? -static_cast<int>(delta) : static_cast<int>(delta);
    int prevPos = fileBrowserData.browserListPosition;
    int newPos = clamp(prevPos + step, 0, fileBrowserData.browserListLength - 1);

    if (newPos != prevPos)
    {
        stopFilePreview();
        fileBrowserData.browserListPosition = newPos;
        refreshBrowserScreen();
        if (buttons->isButtonPressed(interfaceButton2))
        {
            startFilePreview();
        }
    }
}

void cGlobalFileBrowser::handleEnterOrInsert()
{
    if (fileBrowserData.browserListLength <= 0)
    {
        return;
    }

    if (!isFileSelected())
    {
        stopFilePreview();
        fileBrowser.browse();
        refreshBrowserScreen();
    }
    else if (config.mode == GlobalFileBrowserConfig::Mode::kLoadWav ||
             config.mode == GlobalFileBrowserConfig::Mode::kLoadGenericFile)
    {
        confirmLoadSelectedFile();
    }
}

void cGlobalFileBrowser::handleActionButtonPress(uint8_t button)
{
    switch (button)
    {
        case interfaceButton0:
            handleEnterOrInsert();
            break;
        case interfaceButton2:
            startFilePreview();
            break;
        case interfaceButton3:
            openNewFolderKeyboard();
            break;
        case interfaceButton5:
            cancelBrowser();
            break;
        case interfaceButton6:
            selectedPlace = 1;
            refreshBrowserScreen();
            break;
        case interfaceButton7:
            if (config.mode == GlobalFileBrowserConfig::Mode::kSaveWav ||
                config.mode == GlobalFileBrowserConfig::Mode::kSaveGenericFile)
            {
                openSaveFileKeyboard();
            }
            else if (isFileSelected())
            {
                confirmLoadSelectedFile();
            }
            break;
        default:
            break;
    }
}

void cGlobalFileBrowser::handleActionButtonRelease(uint8_t button)
{
    switch (button)
    {
        case interfaceButton2:
            stopFilePreview();
            break;
        case interfaceButton6:
            selectedPlace = 0;
            refreshBrowserScreen();
            break;
        default:
            break;
    }
}

void cGlobalFileBrowser::startFilePreview()
{
    if (!isFileSelected())
    {
        return;
    }
    if (config.mode != GlobalFileBrowserConfig::Mode::kLoadWav &&
        config.mode != GlobalFileBrowserConfig::Mode::kSaveWav)
    {
        return;
    }

    char relPath[kMaxFilePathLen + 1]{};
    fileBrowser.getSelectedItemWithPath(relPath, sizeof(relPath));
    char fullPath[kMaxFilePathLen + 2]{};
    snprintf(fullPath, sizeof(fullPath), "/%s", relPath);

    auto sdPlayer = engine.getSdPlayer();
    if (sdPlayer != nullptr)
    {
        sdPlayer->stop();
        sdPlayer->play(fullPath, 0, MAX_16BIT);
    }
}

void cGlobalFileBrowser::stopFilePreview()
{
    auto sdPlayer = engine.getSdPlayer();
    if (sdPlayer != nullptr)
    {
        sdPlayer->stop();
    }
}

void cGlobalFileBrowser::openNewFolderKeyboard()
{
    stopFilePreview();
    keyboardPurpose = KeyboardPurpose::kNewFolder;
    keyboardManager.fillName("New Folder");
    keyboardManager.activateKeyboard(30);
    showKeyboardScreen("Create New Folder", "Create");
}

void cGlobalFileBrowser::openSaveFileKeyboard()
{
    stopFilePreview();
    keyboardPurpose = KeyboardPurpose::kSaveFile;

    char initialName[64]{};
    if (isFileSelected())
    {
        polystrncpy(initialName, fileBrowser.getSelectedItemName(), sizeof(initialName));
        char* dot = strrchr(initialName, '.');
        if (dot != nullptr)
        {
            *dot = '\0';
        }
    }
    else
    {
        polystrncpy(initialName, config.defaultFileName.c_str(), sizeof(initialName));
        char* dot = strrchr(initialName, '.');
        if (dot != nullptr)
        {
            *dot = '\0';
        }
    }

    if (initialName[0] == '\0')
    {
        polystrncpy(initialName, "sample", sizeof(initialName));
    }

    keyboardManager.fillName(initialName);
    keyboardManager.activateKeyboard(30);
    showKeyboardScreen(config.title.c_str(), "Save");
}

void cGlobalFileBrowser::showKeyboardScreen(const char* titleText, const char* confirmLabel)
{
    titleBar->setTitle(titleText);

    display.setControlHide(explorerLabelControl);
    display.setControlHide(explorerListControl);
    display.setControlHide(middleCoverControl);
    display.setControlHide(previewVolumeBarControl);
    display.setControlHide(rightCoverControl);
    display.setControlHide(frameControl);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 0);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
    }

    display.setControlText(label[0], "Enter");
    display.setControlText(label[1], "Auto Name");
    display.setControlColors(label[2], display.getTheme().inactiveLabelsColors);
    display.setControlColors(label[3], display.getTheme().inactiveLabelsColors);
    display.setControlColors(label[4], display.getTheme().inactiveLabelsColors);
    display.setControlColors(label[5], display.getTheme().inactiveLabelsColors);
    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], confirmLabel);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.refreshControl(label[i]);
    }
    display.synchronizeRefresh();
}

void cGlobalFileBrowser::handleKeyboardButtonPress(uint8_t button)
{
    switch (button)
    {
        case interfaceButton0:
            keyboardManager.confirmKey();
            break;
        case interfaceButton1:
            keyboardManager.setRandomName();
            break;
        case interfaceButton6:
            cancelKeyboard();
            break;
        case interfaceButton7:
            confirmKeyboard();
            break;
        default:
            break;
    }
}

void cGlobalFileBrowser::cancelKeyboard()
{
    keyboardPurpose = KeyboardPurpose::kNone;
    keyboardManager.deactivateKeyboard();
    showBrowserScreen();
}

void cGlobalFileBrowser::confirmKeyboard()
{
    char enteredName[64]{};
    polystrncpy(enteredName, keyboardManager.getName(), sizeof(enteredName));

    if (enteredName[0] == '\0')
    {
        return;
    }

    if (keyboardPurpose == KeyboardPurpose::kNewFolder)
    {
        keyboardPurpose = KeyboardPurpose::kNone;
        keyboardManager.deactivateKeyboard();

        if (fileBrowser.createFolderInCurrentPath(enteredName))
        {
            fileBrowser.refresh();
            fileBrowser.selectSpecificFolderName(enteredName);
        }
        showBrowserScreen();
        return;
    }

    if (keyboardPurpose == KeyboardPurpose::kSaveFile)
    {
        keyboardPurpose = KeyboardPurpose::kNone;
        keyboardManager.deactivateKeyboard();

        std::string ext = config.extensionFilter;
        if (ext.empty() && config.mode == GlobalFileBrowserConfig::Mode::kSaveWav)
        {
            ext = ".wav";
        }
        if (!ext.empty() && ext.front() != '.')
        {
            ext.insert(ext.begin(), '.');
        }

        std::string fileNameStr(enteredName);
        if (!ext.empty() && !hasFileExtension(fileNameStr.c_str(), ext.c_str() + 1))
        {
            fileNameStr += ext;
        }

        std::string fullPath;
        if (fileBrowser.isRoot())
        {
            fullPath = "/" + fileNameStr;
        }
        else
        {
            fullPath = std::string("/") + fileBrowser.getCurrentPath() + "/" + fileNameStr;
        }

        auto onConfirmCb = config.onConfirm;
        returnToCallerModule();
        if (onConfirmCb)
        {
            onConfirmCb(fullPath);
        }
    }
}

void cGlobalFileBrowser::confirmLoadSelectedFile()
{
    if (!isFileSelected())
    {
        return;
    }

    stopFilePreview();

    char relPath[kMaxFilePathLen + 1]{};
    fileBrowser.getSelectedItemWithPath(relPath, sizeof(relPath));
    std::string fullPath = std::string("/") + relPath;

    auto onConfirmCb = config.onConfirm;
    returnToCallerModule();
    if (onConfirmCb)
    {
        onConfirmCb(fullPath);
    }
}

void cGlobalFileBrowser::cancelBrowser()
{
    stopFilePreview();
    auto onCancelCb = config.onCancel;
    returnToCallerModule();
    if (onCancelCb)
    {
        onCancelCb();
    }
}

void cGlobalFileBrowser::returnToCallerModule()
{
    ModuleBase* retMod = config.returnModule;
    uint32_t retOpts = config.returnModuleOptions;
    mtInterface.deactivateModule(this);
    if (retMod != nullptr)
    {
        mtInterface.activateModule(retMod, retOpts);
    }
}
