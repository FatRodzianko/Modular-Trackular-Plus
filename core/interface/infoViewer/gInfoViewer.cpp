#include "infoViewer/infoViewer.h"

void cInfoViewer::initDisplayControls()
{
    doubleArrow[0].bitmaps[0].bitmapIndex = displayDoubleArrowL;
    doubleArrow[0].bitmaps[0].xValue = (800 / 8) * 0 + (800 / 16);
    doubleArrow[0].bitmaps[0].yValue = 455;
    doubleArrow[1].bitmaps[0].bitmapIndex = displayDoubleArrowR;
    doubleArrow[1].bitmaps[0].xValue = (800 / 8) * 7 + (800 / 16);
    doubleArrow[1].bitmaps[0].yValue = 445;

    strControlProperties prop;
    // inicjalizacja kontrolek
    for (uint8_t i = 0; i < 8; i++)
    {
        prop.colors = display.getTheme().activeLabelsColors;
        prop.text = (char*)"";
        prop.style =
          (controlStyleShow | controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        prop.x = (800 / 8) * i + (800 / 16);
        prop.y = 424;
        prop.w = 800 / 8 - 6;
        prop.h = 55;
        prop.value = 0;
        label[i] = display.createControl<cLabel>(&prop);
    }

    strControlProperties prop2;
    prop2.text = nullptr;
    prop2.colors = nullptr;
    prop2.value = 255;
    prop2.style = controlStyleNoTransparency | controlStyleShow;
    prop2.x = 0;
    prop2.w = 800;
    prop2.y = 425;
    prop2.h = 54;
    bgLabel = display.createControl<cBgLabel>(&prop2);

    prop.text = nullptr;
    prop.style = 0;
    prop.value = InfoViewerJpegAddress;
    prop.x = 0;
    prop.y = 29;
    prop.w = 800;
    prop.h = 390;
    image = display.createControl<cImg>(&prop);

    strControlProperties prop3;
    prop3.colors = display.getTheme().configItemEditorTextLabelColors;
    prop3.text = nullptr;
    prop3.data = nullptr;
    prop3.style = controlStyleFont5;
    prop3.value = 3;
    prop3.x = 0;
    prop3.y = 30;
    prop3.w = 780;
    prop3.h = 390;
    infoTextBox = display.createControl<cTextBox>(&prop3);
}

void cInfoViewer::showDefaultScreen()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlText(label[i], "");
    }

    titleBar->show();
    titleBar->setTitle(actualFolderName);

    display.setControlData(label[0], &doubleArrow[0]);
    display.setControlData(label[7], &doubleArrow[1]);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.refreshControl(label[i]);
    }

    display.refreshControl(bgLabel);

    display.synchronizeRefresh();
}
