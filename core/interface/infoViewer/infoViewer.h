#ifndef INTERFACE_INFOVIEWER_H_
#define INTERFACE_INFOVIEWER_H_

#include "core/modulesBase.h"

class cInfoViewer : public ModuleBase
{

  public:
    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cInfoViewer() {}
    ~cInfoViewer() {}

    //----------------------------------
    void setDefaultScreenFunct();
    void showDefaultScreen();

    //----------------------------------

    enum class PageType : uint8_t
    {
        kNone = 0,
        kImage,
        kText,
    };

    void refreshImage();
    void countImages();
    void nextImage();
    void previouseImage();
    void setImage(uint8_t number);
    PageType getPageType(uint8_t number);
    bool imageExists(uint8_t number);

    char titleText[40];

    strLabelData doubleArrow[2];
    strFrameData frameData;

    hControl label[8];
    hControl bgLabel;

    hControl image;
    hControl infoTextBox = -1;

    int imageNumber = 1;
    int imagesCount = 0;

    uint8_t refreshImageState = 0;

    char actualFolderName[12];
};

extern cInfoViewer infoViewer;

#endif /* INTERFACE_INFOVIEWER_H_ */
