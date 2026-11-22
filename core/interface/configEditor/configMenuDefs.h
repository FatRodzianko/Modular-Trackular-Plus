#ifndef INTERFACE_CONFIGEDITOR_CONFIGMENUDEFS_H_
#define INTERFACE_CONFIGEDITOR_CONFIGMENUDEFS_H_

#include "mtStructs.h"
#include "scales.h"
#include <stdint.h>

typedef enum enMenuType
{
    menuTypeEmpty,
    menuTypeGroup,
    menuTypeItem,

} menu_t;

typedef enum enMenuItemType
{
    menuItemTypeEmpty,
    menuItemTypeValueU8,
    menuTypeItemListValues,
    menuTypeItemListText,
    menuTypeItemListTextWithAction,
    menuTypeItemLabel,
    menuTypeItemLabelPtr,
    menuTypeItemActionButton,

} menu_item_t;

struct strItemTypeValue8
{
    uint8_t* value;
    uint8_t min;
    uint8_t max;
    uint8_t interval; // narazie nie uzywane
    uint8_t dev;
};

struct strItemTypeListValues
{
    uint8_t* value;
    uint8_t min;
    uint8_t max;
    uint8_t interval; // narazie nie uzywane
    uint8_t dev;
};

struct strItemTypeListText
{
    uint8_t* value;
    uint8_t count;
    const char* const* ptrText;
};

struct strItemTypeListTextWithAction
{
    uint8_t* value;
    uint8_t count;
    const char* const* ptrText;
    void (*funct1)(void) = nullptr;
};

struct strItemTypeLabel
{
    const char* text;
};

struct strItemTypeLabelPtr
{
    const char** text;
};

struct strItemTypeActionButton
{
    void (*funct1)(void) = nullptr;
    void (*funct2)(void) = nullptr;
};

const char emptyName[5] = "";
const char* const ptrEmptyName = emptyName;

//===========================================================
//===========================================================
//===========================================================
// templates

const char* const onOffStateTexts[2] = {
    "Off",
    "On",
};

// general

const char* const brightness[3] = {
    "Low",
    "Medium",
    "High",
};

const char* const themeChoices[3] = {
    "Original",
    "Monochrome",
    "Anniversary",
};

const char* const ptrFont[1] = {
    "Original",
};

const char* const radioRegion[4] = {
    "Europe",
    "US",
    "Australia",
    "Japan",
};

const char* const recOptionsTexts[4] = {
    "Only notes",
    "Microtiming",
    "Velocity",
    "Microtiming + Velocity",
};

const char* const recThresholdTexts[16] = {
    "0",
    "-4",
    "-8",
    "-12",
    "-16",
    "-20",
    "-24",
    "-28",
    "-32",
    "-36",
    "-40",
    "-44",
    "-48",
    "-52",
    "-56",
    "-60",
};

const char* const performanceSourceTexts[2] = {
    "Global",
    "Per Project",
};

const char* const padsLayoutTexts[12] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
};

const char* const limiterModeTexts[2] = {
    "Subtle",
    "Extreme",
};

const char* const sendsModeTexts[2] = { "Pre-fader", "Post-fader" };

const char* const maxTracksTexts[kMaxTracksModeCount] = { "4", "8", "12", "16" };

const char* const numberingModeTexts[3] = {
    "Decimal, Steps from 0",
    "Decimal, Steps from 1",
    "Hexadecimal",
};

const char* const padPreviewInPatternEditTexts[3] = {
    "Off",
    "Track",
    "Pattern",
};

const char* const patternArrangementTexts[3] = {
    "Vertical Revolver",
    "Vertical Original",
    "Horizontal",
};

const char* const patternInfoTexts[3] = {
    "Additional info",
    "Track names",
    "Off",
};

const char* const noteDisplayModeTexts[2] = {
    "Original",
    "Traditional",
};

// metronome

const char* const metroTimeSig[12] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12",
};

//===========================================================
//===========================================================
//===========================================================
// midi

const char* const clockIn[3] = {
    "Internal",
    "USB",
    "MIDI In jack",
};

const char* const midiOut[4] = {
    "Off",
    "USB",
    "MIDI Out jack",
    "USB + MIDI Out jack",
};

const char* const midiIn[4] = {
    "Off",
    "USB",
    "MIDI In jack",
    "USB + MIDI In jack",
};

const char* const middleC[4] = {
    "C-3",
    "C-4",
    "C-5",
    "C-6",
};

const char* const notesChannel[17] = {
    "ALL", "CH1",  "CH2",  "CH3",  "CH4",  "CH5",  "CH6",  "CH7",  "CH8",
    "CH9", "CH10", "CH11", "CH12", "CH13", "CH14", "CH15", "CH16",
};

const char* const slaveClockComp[kExternalClockCompMaxVal + 1] = {
    "-6", "-5", "-4", "-3", "-2", "-1", "0", "1", "2", "3", "4", "5", "6",
};

const char* const kUsbAudioList[4] = {
    "Off",
    "Input (2 in)",
#ifdef PLATFORM_IMX
    "Output (28 out)",
    "In/Out (2 in, 28 out)",
#else
    "Output (2 out)",
    "In/Out (2 in, 2 out)",
#endif
};
//############################################################################################
//						OPISY PARAMETROW MENU CONFIG (SD keys -> /System/Text/config_hints.txt)
//############################################################################################

constexpr const char pattDivDesc[] = "pattDiv";
constexpr const char radioRegDesc[] = "radioReg";
constexpr const char displBrightDesc[] = "displBright";
constexpr const char displThemeDesc[] = "displTheme";
constexpr const char displFontDesc[] = "displFont";
constexpr const char padsBrightDesc[] = "padsBright";
constexpr const char recNotesDesc[] = "recNotes";
constexpr const char recThresholdDesc[] = "recThreshold";
constexpr const char perfPresDesc[] = "perfPres";

#ifdef TRACKER_MINI
constexpr const char scaleDesc[] = "scale.mini";
#else
constexpr const char scaleDesc[] = "scale";
#endif

constexpr const char rootNoteDesc[] = "rootNote";
constexpr const char padLayDesc[] = "padLay";
constexpr const char antialDesc[] = "antial";
constexpr const char limiterDesc[] = "limiter";
constexpr const char sendsDesc[] = "sends";
constexpr const char headroomDesc[] = "headroom";
constexpr const char snapToZeroDesc[] = "snapToZero";
constexpr const char maxTracksDesc[] = "maxTracks";
constexpr const char hexModeDesc[] = "hexMode";
constexpr const char patternArraDesc[] = "patternArra";
constexpr const char patternInfo[] = "patternInfo";
constexpr const char noteDisplayModeDesc[] = "noteDisplayMode";
constexpr const char usbAudioDesc[] = "usbAudio";
constexpr const char padsPrevDesc[] = "padsPrev";
constexpr const char firmUpdDesc[] = "firmUpd";
constexpr const char slaveClockCompDesc[] = "slaveClockComp";
constexpr const char supportedCcInDesc[] = "supportedCcIn";
constexpr const char programChangeInDesc[] = "programChangeIn";

#ifdef TRACKER_MINI
constexpr const char setupFunctionKeysDesc[] = "";
#endif

#endif /* INTERFACE_CONFIGEDITOR_CONFIGMENUDEFS_H_ */
