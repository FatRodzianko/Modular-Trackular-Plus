#include "infoViewer.h"

#include <stdio.h>
#include <string.h>

#include "mtPadBoard.h"

#include "mtPadsBacklight.h"

#include "core/interfaceMenuPopup.h"
#include "infoViewerDefs.h"

#include "core/songTimer.h"

#include "SdPaths.h"
#include "SdTextFile.h"
#include "TrackerImpl.h"
#include "cr_section_macros.h"

cInfoViewer infoViewer;
static cInfoViewer* IV = &infoViewer;

static void functPlayAction();
static void functRecAction();

static void functLeft();
static void functRight();

static void functEncoder(int16_t value);

static void functSwitchModule(uint8_t button);

static void functActionButton(uint8_t button, uint8_t state);

static void functPads(uint8_t pad, uint8_t state, int16_t velo);

static void functMenu(uint8_t state);

__NOINIT(EXTERNAL_RAM) static char s_infoText[2048];
static void functMiniMiddle();

void cInfoViewer::update()
{
    if (refreshImageState == 1)
    {
        if (display.getImgLoadState() == 0 || display.getImgLoadState() == 3)
        {
            FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
            char path[80];
            snprintf(path,
                     sizeof(path),
                     "%s/Info/%s/%d",
                     SdPaths::systemRootRel(fileSystem),
                     actualFolderName,
                     imageNumber);

            if (display.readImgFromSd(path, InfoViewerJpegAddress, 800, 390))
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

void cInfoViewer::start(uint32_t options)
{
    moduleRefresh = 1;

    if (options >= MANUAL_CHAPTERS_COUNT)
    {
        options = 0;
    }

    strcpy(actualFolderName, &manualFolderNames[options][0]);

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

    showDefaultScreen();
    setDefaultScreenFunct();

    countImages();
    refreshImage();
}

void cInfoViewer::stop()
{
    moduleRefresh = 0;
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

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);
    ip->setButtonObj(interfaceButtonRec, buttonPress, functRecAction);

    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonMiniMiddle, buttonPress, functMiniMiddle);

    ip->setButtonObj(interfaceButton0, functActionButton);
    ip->setButtonObj(interfaceButton1, functActionButton);
    ip->setButtonObj(interfaceButton2, functActionButton);
    ip->setButtonObj(interfaceButton3, functActionButton);
    ip->setButtonObj(interfaceButton4, functActionButton);
    ip->setButtonObj(interfaceButton5, functActionButton);
    ip->setButtonObj(interfaceButton6, functActionButton);
    ip->setButtonObj(interfaceButton7, functActionButton);

    ip->setPotObj(interfacePot0, functEncoder);

    ip->setPadsGlobal(functPads);
}

cInfoViewer::PageType cInfoViewer::getPageType(uint8_t number)
{
    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    if (fileSystem == nullptr || !fileSystem->isSdCardInserted())
    {
        return PageType::kNone;
    }

    const char* sysRoot = SdPaths::systemRootRel(fileSystem);
    char path[80];

    snprintf(path, sizeof(path), "%s/Info/%s/%d.jpg", sysRoot, actualFolderName, number);
    if (fileSystem->exists(path))
    {
        return PageType::kImage;
    }

    snprintf(path, sizeof(path), "%s/Info/%s/%d.jpeg", sysRoot, actualFolderName, number);
    if (fileSystem->exists(path))
    {
        return PageType::kImage;
    }

    snprintf(path, sizeof(path), "%s/Info/%s/%d.txt", sysRoot, actualFolderName, number);
    if (fileSystem->exists(path))
    {
        return PageType::kText;
    }

    return PageType::kNone;
}

bool cInfoViewer::imageExists(uint8_t number)
{
    return getPageType(number) != PageType::kNone;
}

void cInfoViewer::refreshImage()
{
    if (imagesCount == 0)
    {
        snprintf(titleText, sizeof(titleText), "%s", actualFolderName);

        display.setControlHide(image);
        refreshImageState = 0;

        display.setRemoveControlStyle(label[0], controlStyleShowBitmap);
        display.setRemoveControlStyle(label[7], controlStyleShowBitmap);
        display.setControlText(label[6], "");
        display.setControlText(label[7], "Close");
        display.refreshControl(label[0]);
        display.refreshControl(label[6]);
        display.refreshControl(label[7]);

        FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
        const char* sysRoot = SdPaths::systemRoot(fileSystem);
        snprintf(s_infoText,
                 sizeof(s_infoText),
                 "No pages found in %s/Info/%s/"
                 "\n\nPlace up to 48 baseline JPEG images (max 800 x 390 px) or .txt files in:"
                 "\n%s/Info/%s/1.jpg (or 1.txt) ... 48.jpg (or 48.txt)"
                 "\n\nUse Left/Right, the Jog-wheel, or Pads 1-48 to switch pages.",
                 sysRoot,
                 actualFolderName,
                 sysRoot,
                 actualFolderName);

        display.setControlShow(infoTextBox);
        display.setControlText(infoTextBox, s_infoText);
        display.refreshControl(infoTextBox);
    }
    else
    {
        snprintf(titleText, sizeof(titleText), "%s %d/%d", actualFolderName, imageNumber, imagesCount);

        display.setControlText(label[6], "Close");
        display.setControlText(label[7], "");
        display.setAddControlStyle(label[0], controlStyleShowBitmap);
        display.setAddControlStyle(label[7], controlStyleShowBitmap);
        display.refreshControl(label[0]);
        display.refreshControl(label[6]);
        display.refreshControl(label[7]);

        if (getPageType(imageNumber) == PageType::kText)
        {
            refreshImageState = 0;
            display.setControlHide(image);
            display.refreshControl(image);

            FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
            char path[80];
            snprintf(path,
                     sizeof(path),
                     "%s/Info/%s/%d.txt",
                     SdPaths::systemRootRel(fileSystem),
                     actualFolderName,
                     imageNumber);

            s_infoText[0] = '\0';
            if (SdTextFile::readAll(fileSystem, path, s_infoText, sizeof(s_infoText)))
            {
                SdTextFile::stripCr(s_infoText);
            }

            display.setControlShow(infoTextBox);
            display.setControlText(infoTextBox, s_infoText);
            display.refreshControl(infoTextBox);
        }
        else
        {
            display.setControlHide(infoTextBox);
            display.refreshControl(infoTextBox);
            display.setControlHide(image);

            refreshImageState = 1;
            display.processDisplayUpdateImmediate();
        }
    }

    titleBar->show();
    titleBar->setTitle(titleText);
    display.synchronizeRefresh();
}

void cInfoViewer::nextImage()
{
    if (imagesCount <= 1)
    {
        return;
    }

    uint8_t loop_start = imageNumber;

    do
    {
        imageNumber++;
        if (imageNumber > 48)
        {
            imageNumber = 1;
        }
        if (imageExists(imageNumber))
        {
            break;
        }
    } while (imageNumber != loop_start);
}

void cInfoViewer::previouseImage()
{
    if (imagesCount <= 1)
    {
        return;
    }

    uint8_t loop_start = imageNumber;

    do
    {
        imageNumber--;
        if (imageNumber < 1)
        {
            imageNumber = 48;
        }
        if (imageExists(imageNumber))
        {
            break;
        }
    } while (imageNumber != loop_start);
}

void cInfoViewer::countImages()
{
    imagesCount = 0;
    int firstFound = 1;

    for (uint8_t i = 1; i <= 48; i++)
    {
        if (imageExists(i))
        {
            if (imagesCount == 0)
            {
                firstFound = i;
            }
            imagesCount++;
        }
    }

    imageNumber = firstFound;
}

void cInfoViewer::setImage(uint8_t number)
{
    if (imagesCount == 0)
    {
        return;
    }

    if (imageExists(number))
    {
        imageNumber = number;
    }
}

//==============================================================================================================
static void functEncoder(int16_t value)
{
    value = interfaceGlobals.invertValForList ? -value : value;

    if (value > 0)
    {
        IV->nextImage();
        IV->refreshImage();
    }
    else
    {
        IV->previouseImage();
        IV->refreshImage();
    }
}

//=========================================================================================================
static void functLeft()
{
    IV->previouseImage();
    IV->refreshImage();
}

static void functRight()
{
    IV->nextImage();
    IV->refreshImage();
}

//=========================================================================================================
static void functPlayAction()
{
}

static void functRecAction()
{
}

static void functSwitchModule(uint8_t button)
{
    IV->eventFunct(eventSwitchModule, IV, &button, 0);
}

//##############################################################################################
//###############################        ACTION BUTTONS        #################################
//##############################################################################################

static void functActionButton(uint8_t button, uint8_t state)
{
    if (state == buttonPress)
    {
        if (button == 0)
        {
            IV->previouseImage();
            IV->refreshImage();
        }
        else if (button == 6)
        {
            IV->eventFunct(eventSwitchToPreviousModule, IV, 0, 0);
        }
        else if (button == 7)
        {
            if (IV->imagesCount == 0)
            {
                IV->eventFunct(eventSwitchToPreviousModule, IV, 0, 0);
            }
            else
            {
                IV->nextImage();
                IV->refreshImage();
            }
        }
    }
}

//##############################################################################################
//###############################          PAD BOARD           #################################
//##############################################################################################

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    if (state == 1)
    {
        IV->setImage(pad + 1);
        IV->refreshImage();
    }
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

static void functMiniMiddle()
{
    if (IV->imagesCount == 0)
    {
        IV->eventFunct(eventSwitchToPreviousModule, IV, 0, 0);
    }
}
