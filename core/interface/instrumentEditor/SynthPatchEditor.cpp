#include "instrumentEditor/instrumentEditor.h"

#if GUI_HANDLE_SYNTH

#include "PolySynth/Patch.h"

#include "core/interfacePopups.h"
#include "fileManager.h"

#include "interruptsControl.h"
#include "util_math.h"
#include "util_string.h"

#include "mtAudioEngine.h"
#if MT_HAS_SYNTHS // MT: WAVE's and PHASE's tiles and controls
#include "synths/synthsMod.h"
#endif
#if MT_HAS_SYNTHFX // MT: Synth FX's rows
#include "synthfx/synthfxMod.h"
#include <cr_section_macros.h>
#endif

#include <cmath>

const char* const OffOnNames[] = { "Off", "On" };
static const int synthModelListImages[Synth::kSynthTypes.size()] = {
    enDisplayBitmaps::kBitmapACD,  enDisplayBitmaps::kBitmapFAT,  enDisplayBitmaps::kBitmapVAP,
    enDisplayBitmaps::kBitmapWTFM, enDisplayBitmaps::kBitmapPERC, enDisplayBitmaps::kBitmapDIRT, enDisplayBitmaps::kBitmapCHIP
#if MT_HAS_SYNTHS // MT: WAVE and PHASE
    , enDisplayBitmaps::kBitmapWAVE, enDisplayBitmaps::kBitmapPHASE
#endif
};

static cInstrumentEditor* IE = &instrumentEditor;

static void functSynthActionButtons(uint8_t button);
static void functSynthPatchSaveAsKeyboardActionButtons(uint8_t button);
static void functSynthMacroNameKeyboardActionButtons(uint8_t button);

static void functSynthPatchEditEncoder(int16_t value);
static void functSynthPatchEditUp();
static void functSynthPatchEditDown();
static void functSynthPatchEditLeft();
static void functSynthPatchEditRight();

static void functSynthPatchEditMiniMiddle(uint8_t state);
static void functSynthPatchEditPads(uint8_t pad, uint8_t state, int16_t);

static void functSynthPatchEditInsert();
static void functSynthPatchEditDelete();

static void functSynthPatchKeyboardCancel();
static void functSynthPatchSaveAsKeyboardConfirm();
static void functSynthMacroNameKeyboardConfirm();

static void functSynthSave();
static void functSynthSaveAs();
static void functSynthBack();
static void functSynthInit();
static void functSynthInitConfirm();
static void functSynthInitCancel();

#if MT_HAS_SYNTHFX // MT: Synth FX's rows in the parameter list, on button 3, for every engine (synthfxMod.h)
static void functSynthFx();

static bool mtFxView = false; // the parameter list shows Synth FX's rows

namespace
{
// The rows' texts and the value list, in SDRAM (main loop only; CE's own lists are on the heap)
__NOINIT(EXTERNAL_RAM) char mtFxText[mods::synthfx::kMaxRows * 2][cInstrumentEditor::kMaxSynthListStringLen];
__NOINIT(EXTERNAL_RAM) char* mtFxNames[mods::synthfx::kMaxRows];
__NOINIT(EXTERNAL_RAM) char* mtFxValues[mods::synthfx::kMaxRows];
__NOINIT(EXTERNAL_RAM) char mtFxItemText[mods::synthfx::kMaxChoices][cInstrumentEditor::kMaxSynthListStringLen];
__NOINIT(EXTERNAL_RAM) char* mtFxItems[mods::synthfx::kMaxChoices];
} // namespace

static int mtFxPart()
{
    return mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
}

static bool mtFxAvailable()
{
    const int part = mtFxPart();
    return part >= 0 && part < kSynthInstrumentsCount &&
           mods::synthfx::pageShown(IE->getSynth()->getPatch(part));
}

static int mtFxRow()
{
    const int rows = mods::synthfx::rowCount(mtFxPart());
    return clamp(IE->paramListPos, 0, rows > 0 ? rows - 1 : 0);
}

static void mtFxShowList(bool refreshList)
{
    namespace sfx = mods::synthfx;
    const int part = mtFxPart();
    if (refreshList)
    {
        const int rows = sfx::rowCount(part);
        for (int row = 0; row < rows; row++)
        {
            mtFxNames[row] = mtFxText[2 * row];
            mtFxValues[row] = mtFxText[2 * row + 1];
            sfx::rowName(part, row, mtFxNames[row], cInstrumentEditor::kMaxSynthListStringLen);
            sfx::rowText(part, row, mtFxValues[row], cInstrumentEditor::kMaxSynthListStringLen);
        }
        IE->paramListPos = mtFxRow();
        IE->paramsListData.start = IE->paramListPos;
        IE->paramsListData.length = rows;
        IE->paramsListData.linesCount = std::min(rows, 14); // as CE's sections
        IE->paramsListData.params = mtFxNames;
        IE->paramsListData.values = mtFxValues;
        display.setControlData(IE->paramsListControl, &IE->paramsListData);
    }

    display.setControlValue(IE->paramsListControl, IE->paramListPos);
    display.refreshControl(IE->paramsListControl);

    display.setControlText(IE->label[5], "Synth FX");
    display.refreshControl(IE->label[5]);
}

static void mtFxShowValue(bool refreshList)
{
    namespace sfx = mods::synthfx;
    const int part = mtFxPart();
    const int row = mtFxRow();
    const int step = sfx::rowStep(part, row);
    const int count = sfx::rowSteps(part, row);

    if (refreshList)
    {
        if (sfx::rowIsList(part, row))
        {
            const int length = std::min(count, sfx::kMaxChoices);
            for (int i = 0; i < length; i++)
            {
                mtFxItems[i] = mtFxItemText[i];
                sfx::rowItemText(part, row, i, mtFxItems[i], cInstrumentEditor::kMaxSynthListStringLen);
            }
            IE->modelListData.rowHeight = 0;
            IE->modelListData.scrollText = true;
            IE->modelListData.start = step;
            IE->modelListData.length = length;
            IE->modelListData.linesCount = std::min(length, 14); // as CE's lists
            IE->modelListData.params = mtFxItems;
            IE->modelListData.imageIds = nullptr;
            display.setControlData(IE->modelListControl, &IE->modelListData);
            IE->paramValueMode = cInstrumentEditor::kList;
        }
        else
        {
            IE->paramValueMode = cInstrumentEditor::kValue;
        }
    }

    if (IE->paramValueMode == cInstrumentEditor::kList)
    {
        IE->paramValueListPos = step;
        display.setControlHide(IE->barControl[7]);
        display.setControlShow(IE->modelListControl);
        display.setControlValue(IE->modelListControl, step);
        display.refreshControl(IE->modelListControl);
    }
    else
    {
        int barValue = 0;
        if (sfx::rowBipolar(part, row))
        {
            const int middle = (count - 1) / 2;
            barValue = middle > 0 ? (step - middle) * 100 / middle : 0;
            display.setAddControlStyle(IE->barControl[7], controlStyleValueLeftRight_100_100);
            display.setRemoveControlStyle(IE->barControl[7], controlStyleValue_0_100);
        }
        else
        {
            barValue = count > 1 ? static_cast<int>(static_cast<int64_t>(step) * 100 / (count - 1)) : 0;
            display.setAddControlStyle(IE->barControl[7], controlStyleValue_0_100);
            display.setRemoveControlStyle(IE->barControl[7], controlStyleValueLeftRight_100_100);
        }
        display.setControlValue(IE->barControl[7], barValue);
        display.setControlShow(IE->barControl[7]);
        display.refreshControl(IE->barControl[7]);
        display.setControlHide(IE->modelListControl);
    }
    display.synchronizeRefresh();

    if (row < sfx::rowCount(part))
    {
        sfx::rowText(part, row, mtFxText[2 * row + 1], cInstrumentEditor::kMaxSynthListStringLen);
    }
}

// After an edit: the project's record, then the rows (the value list too if a slot's plugin
// changed, and with it the knob rows)
static void mtFxEdited(bool rowsChanged)
{
    mods::projectSettingEdited();
    mtFxShowList(true);
    mtFxShowValue(rowsChanged);
}

static void mtFxChange(int16_t value)
{
    namespace sfx = mods::synthfx;
    const int part = mtFxPart();
    const int row = mtFxRow();
    mtFxEdited(sfx::setRowStep(part, row, sfx::rowStep(part, row) + sfx::rowEncoderSteps(part, row, value)));
}

static void mtFxDefault()
{
    mtFxEdited(mods::synthfx::defaultRow(mtFxPart(), mtFxRow()));
}

// Main loop (cInstrumentEditor::update): a slot's plugin made, let go or stopped since: redraw
void mtSynthFxPoll()
{
    if (mods::synthfx::takeSlotsChanged() && mtFxView && IE->mode == mtInstEditModeSynthPatchEdit &&
        !IE->keyboardManager.getState())
    {
        mtFxShowList(true);
        mtFxShowValue(true);
    }
}
#endif

void cInstrumentEditor::cleanPatchEditorResources()
{
    if (sectionNamesBufor != nullptr)
    {
        delete[] sectionNamesBufor;
        sectionNamesBufor = nullptr;
    }

    if (sectionNamesGridBuff != nullptr)
    {
        delete[] sectionNamesGridBuff;
        sectionNamesGridBuff = nullptr;
    }

    if (sectionNamesList != nullptr)
    {
        delete[] sectionNamesList;
        sectionNamesList = nullptr;
    }

    if (sectionIds != nullptr)
    {
        delete[] sectionIds;
        sectionIds = nullptr;
    }

    if (paramListTextBuff != nullptr)
    {
        delete[] paramListTextBuff;
        paramListTextBuff = nullptr;
    }

    if (paramListNames != nullptr)
    {
        delete[] paramListNames;
        paramListNames = nullptr;
    }

    if (paramListValues != nullptr)
    {
        delete[] paramListValues;
        paramListValues = nullptr;
    }

    if (sectionParamsIds != nullptr)
    {
        delete[] sectionParamsIds;
        sectionParamsIds = nullptr;
    }

    if (paramValueListTextBuff != nullptr)
    {
        delete[] paramValueListTextBuff;
        paramValueListTextBuff = nullptr;
    }

    if (paramValueListNames != nullptr)
    {
        delete[] paramValueListNames;
        paramValueListNames = nullptr;
    }
}

void cInstrumentEditor::setSynthPatchEditorScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    for (uint8_t i = interfaceButton4; i <= interfaceButton6; i++)
    {
        ip->setButtonObj(i, buttonPress, functSynthActionButtons);
    }

    ip->setButtonObj(interfaceButton0, buttonPress, functSynthSave);
    ip->setButtonObj(interfaceButton1, buttonPress, functSynthSaveAs);
    ip->setButtonObj(interfaceButton2, buttonPress, functSynthInit);
#if MT_HAS_SYNTHFX // MT: Synth FX's rows
    if (mtFxAvailable())
    {
        ip->setButtonObj(interfaceButton3, buttonPress, functSynthFx);
    }
#endif
    ip->setButtonObj(interfaceButton7, buttonPress, functSynthBack);

    ip->setPotObj(interfacePot0, functSynthPatchEditEncoder);

    ip->setButtonObj(interfaceButtonLeft, buttonPress, functSynthPatchEditLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functSynthPatchEditRight);
    ip->setButtonObj(interfaceButtonUp, buttonPress, functSynthPatchEditUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functSynthPatchEditDown);

    ip->setButtonObj(interfaceButtonDelete, buttonPress, functSynthPatchEditDelete);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, functSynthPatchEditInsert);

    ip->setButtonObj(interfaceButtonMiniMiddle, functSynthPatchEditMiniMiddle);

    ip->setPadsGlobal(functSynthPatchEditPads);
}

void cInstrumentEditor::setSynthPatchSaveAsKeyboardFunctions()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);

    for (uint8_t i = interfaceButton0; i <= interfaceButton7; i++)
    {
        ip->setButtonObj(i, buttonPress, functSynthPatchSaveAsKeyboardActionButtons);
    }
}

void cInstrumentEditor::setSynthMacroNameKeyboardFunctions()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);

    for (uint8_t i = interfaceButton0; i <= interfaceButton7; i++)
    {
        ip->setButtonObj(i, buttonPress, functSynthMacroNameKeyboardActionButtons);
    }
}

void cInstrumentEditor::setSynthPatchInitPopupFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->blockAllInputsExcept(interfaceButton6, interfaceButton7);

    ip->setButtonObj(interfaceButton6, buttonPress, functSynthInitCancel);
    ip->setButtonObj(interfaceButton7, buttonPress, functSynthInitConfirm);
}

static void functSynthPatchEditEncoder(int16_t value)
{
    if (IE->keyboardManager.getState())
    {
#ifdef TRACKER_MINI
        IE->keyboardManager.confirmKey();
#endif
        return;
    }

    IE->changeSynthPatchEditorValue(value, IE->kEncoder);
}

static void functSynthPatchEditUp()
{
    if (IE->keyboardManager.getState())
    {
        IE->keyboardManager.makeMove('w');
        return;
    }

    if (IE->selectedPlace[mtInstEditModeSynthPatchEdit] == 0)
    {
        IE->gridChangeSelection(0);
        return;
    }

    IE->changeSynthPatchEditorValue(1);
}

static void functSynthPatchEditDown()
{
    if (IE->keyboardManager.getState())
    {
        IE->keyboardManager.makeMove('s');
        return;
    }

    if (IE->selectedPlace[mtInstEditModeSynthPatchEdit] == 0)
    {
        IE->gridChangeSelection(2);
        return;
    }

    IE->changeSynthPatchEditorValue(-1);
}

static void functSynthPatchEditLeft()
{
    if (IE->keyboardManager.getState())
    {
        IE->keyboardManager.makeMove('a');
        return;
    }

    if (IE->selectedPlace[mtInstEditModeSynthPatchEdit] == 0)
    {
        IE->gridChangeSelection(3);
    }
    else if (IE->selectedPlace[mtInstEditModeSynthPatchEdit] == 1)
    {
#ifdef TRACKER_MINI
        IE->gridChangeSelection(-1);
        IE->selectedPlace[mtInstEditModeSynthPatchEdit] = 0;
        display.setControlHide(IE->frameControl);
        display.refreshControl(IE->frameControl);
        display.synchronizeRefresh();
#endif
    }
}

static void functSynthPatchEditRight()
{
    if (IE->keyboardManager.getState())
    {
        IE->keyboardManager.makeMove('d');
        return;
    }

    if (IE->selectedPlace[mtInstEditModeSynthPatchEdit] == 0 &&
        IE->imgGridData.selection % IE->kGridColumnsCount != IE->kGridColumnsCount - 1)
    {
        IE->gridChangeSelection(1);
    }
    else
    {
        IE->selectedPlace[mtInstEditModeSynthPatchEdit] = 1;
        IE->activateLabelsBorder();
        IE->gridChangeSelection(4);
        display.synchronizeRefresh();
    }
}

static void functSynthPatchEditMiniMiddle(uint8_t state)
{
    if (mtPopups.getStepPopupState() != enumStepPopups::stepPopupNone)
    {
        return;
    }

    if (IE->keyboardManager.getState())
    {
        if (state == buttonPress)
        {
            IE->keyboardManager.confirmKey();
        }
        return;
    }

    if (state == buttonPress && IE->selectedPlace[mtInstEditModeSynthPatchEdit] == 0)
    {
        IE->setSection(IE->imgGridData.selection);
    }
    else if (IE->selectedPlace[mtInstEditModeSynthPatchEdit] == 1)
    {
        IE->synthCommonPreviewButtonFunct(state);
    }
}

static void functSynthPatchEditPads(uint8_t pad, uint8_t state, int16_t)
{
    if (IE->keyboardManager.getState())
    {
        IE->keyboardManager.onPadChange(pad, state);
        return;
    }

    if (state != buttonPress && state != buttonRelease)
    {
        return;
    }

    uint8_t padForSeq = pad;

#ifndef TRACKER_MINI
    if (IE->mode == mtInstEditModeSynthPatchEdit)
    {
        if (pad % 12 < 4)
        {
            int padToSection = (pad % 12) + 4 * (pad / 12);
            IE->setSection(padToSection);
            IE->lightUpPadBoard();
            return;
        }
        padForSeq = clamp<int>(IE->convertPadForEditPatchScreen(pad), 0, 47);
    }
#endif
    IE->playSynthNoteByPad(padForSeq, state, pad);
}

static void functSynthPatchEditInsert()
{
    if (IE->keyboardManager.getState())
    {
        IE->keyboardManager.confirmKey();
        return;
    }
}

static void functSynthPatchEditDelete()
{
    if (IE->keyboardManager.getState())
    {
        IE->keyboardManager.makeBackspace();
        return;
    }

    IE->setSynthPatchParamDefault();
}

static void functSynthPatchSaveAsKeyboardActionButtons(uint8_t button)
{
    if (!IE->keyboardManager.getState())
    {
        return;
    }

    switch (button)
    {
        case interfaceButton0: IE->keyboardManager.confirmKey(); break;
        case interfaceButton1: IE->keyboardManager.setRandomName(); break;
        case interfaceButton6: functSynthPatchKeyboardCancel(); break;
        case interfaceButton7: functSynthPatchSaveAsKeyboardConfirm(); break;
        default: break;
    }
}

static void functSynthMacroNameKeyboardActionButtons(uint8_t button)
{
    if (!IE->keyboardManager.getState())
    {
        return;
    }

    switch (button)
    {
        case interfaceButton0: IE->keyboardManager.confirmKey(); break;
        case interfaceButton1: IE->keyboardManager.setRandomName(); break;
        case interfaceButton6: functSynthPatchKeyboardCancel(); break;
        case interfaceButton7: functSynthMacroNameKeyboardConfirm(); break;
        default: break;
    }
}

static void functSynthPatchKeyboardCancel()
{
    IE->keyboardManager.deactivateKeyboard();
    IE->showSynthPatchEditorScreen();
    IE->setSynthPatchEditorScreenFunct();
    IE->lightUpPadBoard();
}

static void functSynthPatchSaveAsKeyboardConfirm()
{
    int syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;

    auto result = IE->getSynth()->savePatch(syntPartIdx, IE->keyboardManager.getName());
    mtPopups.show(0, result ? "Patch saved" : "Error saving patch");

    polystrncpy(mtProject.values.synthPart[syntPartIdx].patchName,
                IE->getSynth()->getPatchName(syntPartIdx).c_str(),
                sizeof(SynthPartSettings::patchName));

    newFileManager.setProjectStructChanged();

    functSynthPatchKeyboardCancel();
}

static void functSynthMacroNameKeyboardConfirm()
{
    int syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    auto& patch = IE->getSynth()->getPatch(syntPartIdx);
    const auto& metadata = patch.getMetadata();

    int macroIdx = IE->sectionIds[IE->sectionListPos];
    if (macroIdx >= 0 && macroIdx < std::ssize(metadata.macros))
    {
        DISABLE_AUDIO_INTERRUPTS();

        patch.setMacroDisplayName(macroIdx, IE->keyboardManager.getName());
        IE->getSynth()->setPatchDirty(syntPartIdx);

        ENABLE_AUDIO_INTERRUPTS();
    }

    functSynthPatchKeyboardCancel();
}

static void functSynthActionButtons(uint8_t button)
{
    if (IE->mode != mtInstEditModeSynthPatchEdit)
    {
        return;
    }

    if (button == 5 && IE->paramValueMode == cInstrumentEditor::kNone)
    {
        IE->changeSynthMacroName();
        return;
    }

    switch (button)
    {
        case 4:
        case 6:
        {
            if (IE->selectedPlace[mtInstEditModeSynthPatchEdit] != 1)
            {
                IE->selectedPlace[mtInstEditModeSynthPatchEdit] = 1;
                return;
            }

            IE->changeSynthPatchEditorValue(button == 4 ? 1 : -1);
            break;
        }
        case 5:
        {
            IE->selectedPlace[mtInstEditModeSynthPatchEdit] = 1;
            IE->activateLabelsBorder();
            IE->gridChangeSelection(4);
            display.synchronizeRefresh();
            return;
        }
        default: break;
    }
}

static void functSynthSave()
{
    uint8_t syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;

    auto result = IE->getSynth()->savePatch(syntPartIdx);

    mtPopups.show(0, result ? "Patch saved" : "Error saving patch");
}

static void functSynthSaveAs()
{
    uint8_t syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;

    char textBuffer[sizeof(SynthPartSettings::patchName) + 1];
    polystrncpy(textBuffer, IE->getSynth()->getPatchName(syntPartIdx).c_str(), sizeof(textBuffer));

    IE->keyboardManager.fillName(textBuffer);
    IE->keyboardManager.activateKeyboard(PROJECT_NAME_SIZE - 1);

    sequencer.stopManualNotes();
    IE->titleBar->setTitle("Edit Patch Name");
    IE->setSynthPatchSaveAsKeyboardFunctions();
    IE->showSaveAsKeyboard();
}

static void functSynthInit()
{
    IE->showSynthPatchInitPopup();
    IE->setSynthPatchInitPopupFunct();
}

static void functSynthInitConfirm()
{
    IE->initCurrentPatch();
    functSynthInitCancel();
}

static void functSynthInitCancel()
{
    IE->ip->unblockAllInputs();
    mtPopups.hideInfoPopup();
    IE->showSynthPatchEditorScreen();
    IE->setSynthPatchEditorScreenFunct();
}

#if MT_HAS_SYNTHFX // MT: button 3 shows Synth FX's rows in the parameter list, or the section's again
static void functSynthFx()
{
    if (IE->mode != mtInstEditModeSynthPatchEdit || IE->keyboardManager.getState())
    {
        return;
    }
    if (!mtFxView && !mtFxAvailable())
    {
        return;
    }
    mtFxView = !mtFxView;
    if (mtFxView)
    {
        mods::synthfx::pageOpened();
    }
    IE->paramListPos = 0;
    IE->selectedPlace[mtInstEditModeSynthPatchEdit] = 1;
    IE->imgGridData.showSelection = false;
    display.refreshControl(IE->imgGridControl);
    IE->activateLabelsBorder();
    IE->showSynthPatchSectionParam(true);
    IE->showSynthPatchSectionParamValue(true);
    IE->showSynthPatchMacroParam(true);
    IE->showSynthPatchMacroParamValue(true);
    display.synchronizeRefresh();
}
#endif

static void functSynthBack()
{
    sequencer.stopManualNotes();
    IE->mode = mtInstEditModeSynth;
    IE->start(0);
    IE->cleanPatchEditorResources();
}

void cInstrumentEditor::changeSynthPatchEditorValue(int value, ValueChangeSource source)
{
    bool invert = source != kEncoder || interfaceGlobals.invertValForList;

    switch (selectedPlace[mtInstEditModeSynthPatchEdit])
    {
        case 0: changeSynthPatchSection(invert ? -value : value); break;
        case 1:
        {
            if (source == kArrowButton)
            {
                changeSynthPatchParam(invert ? -value : value);
            }
            else
            {
                changeSynthPatchParamValue(paramValueMode == kList ? (invert ? -value : value)
                                                                   : value);
            }
            break;
        }
        case 2:
        {
            changeSynthPatchParamValue(paramValueMode == kList ? (invert ? -value : value) : value);
            break;
        }
        default: break;
    }
}

void cInstrumentEditor::changeSynthPatchSection(int16_t value)
{
    int previousValue = sectionListPos;

    if (IE->rollListOver && sectionListPos + value >= sectionListLen)
    {
        sectionListPos = 0;
    }
    else
    {
        sectionListPos = clamp(sectionListPos + value, 0, sectionListLen ? sectionListLen - 1 : 0);
    }

    if (previousValue != sectionListPos)
    {
#if MT_HAS_SYNTHFX // MT: another section leaves Synth FX's rows
        mtFxView = false;
#endif
        showSynthPatchSection();
        showSynthPatchSectionParam(true);
        showSynthPatchSectionParamValue(true);
        showSynthPatchMacroParam(true);
        showSynthPatchMacroParamValue(true);
    }
}

void cInstrumentEditor::changeSynthPatchParam(int16_t value)
{
    int previousValue = paramListPos;

    paramListPos =
      clamp(paramListPos + value, 0, paramsListData.length ? paramsListData.length - 1 : 0);

    if (previousValue != paramListPos)
    {
        showSynthPatchSectionParam();
        showSynthPatchSectionParamValue(true);
        showSynthPatchMacroParam();
        showSynthPatchMacroParamValue(true);
    }
}

void cInstrumentEditor::setSynthPatchParamDefault()
{
#if MT_HAS_SYNTHFX // MT: Synth FX's rows
    if (mtFxView)
    {
        mtFxDefault();
        return;
    }
#endif
    int syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;

    auto& patch = getSynth()->getPatch(syntPartIdx);
    const auto& metadata = patch.getMetadata();

    if (isMacroSelected())
    {
        int macroIdx = sectionIds[sectionListPos];
        if (macroIdx >= 0 && macroIdx < std::ssize(metadata.macros))
        {
            if (paramValueMode == kList)
            {
                paramValueListPos = 0;

                if (paramListPos == 1)
                {
                    DISABLE_AUDIO_INTERRUPTS();
                    patch.setMacroBipolar(macroIdx, false);

                    getSynth()->initSynthPartMacroValue(
                      syntPartIdx, macroIdx, mtProject.values.synthPart[syntPartIdx]);
                    engine.refreshSynthInstrumentParams(mtProject.values.synthPart[syntPartIdx],
                                                        syntPartIdx);

                    ENABLE_AUDIO_INTERRUPTS();
                }
                else if (paramListPos % 2 == 0)
                {
                    int modifierId = paramListPos / 2 - 1;
                    if (modifierId >= 0 && modifierId < kMaxMacroParamMods)
                    {
                        DISABLE_AUDIO_INTERRUPTS();
                        patch.setMacroModifier(macroIdx, modifierId, std::nullopt);
                        ENABLE_AUDIO_INTERRUPTS();
                    }
                }
            }
            else if (paramValueMode == kValue && paramListPos % 2 == 1)
            {
                int modifierId = paramListPos / 2 - 1;
                if (modifierId >= 0 && modifierId < kMaxMacroParamMods)
                {
                    DISABLE_AUDIO_INTERRUPTS();
                    float amount = 0.0f;
                    if (modifierId >= std::ssize(metadata.macros[macroIdx].modifiers))
                    {
                        patch.setMacroModifier(macroIdx, modifierId, std::nullopt);
                    }
                    patch.setMacroModifierAmount(macroIdx, modifierId, amount);
                    ENABLE_AUDIO_INTERRUPTS();
                }
            }
            else
            {
                return;
            }
        }
    }
    else
    {
        int paramIdx = sectionParamsIds[paramListPos];
        if (paramIdx >= 0 && paramIdx < std::ssize(metadata.params))
        {
            if (paramValueMode == kList)
            {
                if (!metadata.params[paramIdx].defaultValue.has_value())
                {
                    return;
                }

                paramValueListPos =
                  clamp<float>(metadata.params[paramIdx].defaultValue.value(),
                               0,
                               modelListData.length ? modelListData.length - 1 : 0);

                DISABLE_AUDIO_INTERRUPTS();
                patch.setParamValue(
                  paramIdx, paramValueListPos, polysynth::Patch::ParamChangeSource::kKnob);
                ENABLE_AUDIO_INTERRUPTS();
            }
            else
            {
                if (!metadata.params[paramIdx].defaultValue.has_value())
                {
                    return;
                }

                DISABLE_AUDIO_INTERRUPTS();
                patch.setParamValue(paramIdx,
                                    metadata.params[paramIdx].defaultValue,
                                    polysynth::Patch::ParamChangeSource::kKnob);
                ENABLE_AUDIO_INTERRUPTS();
            }
        }
    }

    getSynth()->setPatchDirty(syntPartIdx);

    showSynthPatchSectionParam();
    showSynthPatchSectionParamValue();
    showSynthPatchMacroParam();
    showSynthPatchMacroParamValue();
}

void cInstrumentEditor::changeSynthPatchParamValue(int16_t value)
{
#if MT_HAS_SYNTHFX // MT: Synth FX's rows
    if (mtFxView)
    {
        mtFxChange(value);
        return;
    }
#endif
    bool refreshParamList = false;
    int syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;

    auto& patch = getSynth()->getPatch(syntPartIdx);
    const auto& metadata = patch.getMetadata();

    if (isMacroSelected())
    {
        int macroIdx = sectionIds[sectionListPos];
        if (macroIdx >= 0 && macroIdx < std::ssize(metadata.macros))
        {
            if (paramValueMode == kList)
            {
                int previousValue = paramValueListPos;

                paramValueListPos = clamp(paramValueListPos + value,
                                          0,
                                          modelListData.length ? modelListData.length - 1 : 0);

                if (paramListPos == 1)
                {
                    DISABLE_AUDIO_INTERRUPTS();

                    patch.setMacroBipolar(macroIdx, paramValueListPos == 1);

                    getSynth()->initSynthPartMacroValue(
                      syntPartIdx, macroIdx, mtProject.values.synthPart[syntPartIdx]);
                    engine.refreshSynthInstrumentParams(mtProject.values.synthPart[syntPartIdx],
                                                        syntPartIdx);

                    ENABLE_AUDIO_INTERRUPTS();
                }
                else if (paramListPos % 2 == 0)
                {
                    int modifierId = paramListPos / 2 - 1;
                    if (modifierId >= 0 && modifierId < kMaxMacroParamMods)
                    {

                        DISABLE_AUDIO_INTERRUPTS();
                        if (paramValueListPos > 0)
                        {
                            patch.setMacroModifier(macroIdx, modifierId, paramValueListPos - 1);
                        }
                        else
                        {
                            patch.setMacroModifier(macroIdx, modifierId, std::nullopt);
                        }
                        ENABLE_AUDIO_INTERRUPTS();

                        if (previousValue == 0 && paramValueListPos > 0)
                        {
                            refreshParamList = true;
                        }
                    }
                }
            }
            else if (paramValueMode == kValue && paramListPos % 2 == 1)
            {
                int modifierId = paramListPos / 2 - 1;
                if (modifierId >= 0 && modifierId < kMaxMacroParamMods)
                {
                    DISABLE_AUDIO_INTERRUPTS();
                    float amount = 0.0f;

                    if (modifierId >= std::ssize(metadata.macros[macroIdx].modifiers))
                    {
                        patch.setMacroModifier(macroIdx, modifierId, std::nullopt);
                    }
                    else
                    {
                        amount = clamp(
                          (metadata.macros[macroIdx].modifiers[modifierId].amount * 100 + value) /
                            100.0f,
                          -polysynth::Patch::Metadata::Macro::ParamModifier::kMaxAmount,
                          polysynth::Patch::Metadata::Macro::ParamModifier::kMaxAmount);
                    }
                    patch.setMacroModifierAmount(macroIdx, modifierId, amount);
                    ENABLE_AUDIO_INTERRUPTS();
                }
            }
        }
    }
    else
    {
        int paramIdx = sectionParamsIds[paramListPos];
        if (paramIdx >= 0 && paramIdx < std::ssize(metadata.params))
        {
            if (paramValueMode == kList)
            {
                paramValueListPos = clamp(paramValueListPos + value,
                                          0,
                                          modelListData.length ? modelListData.length - 1 : 0);

                DISABLE_AUDIO_INTERRUPTS();
                patch.setParamValue(
                  paramIdx, paramValueListPos, polysynth::Patch::ParamChangeSource::kKnob);
                ENABLE_AUDIO_INTERRUPTS();
            }
            else
            {
                const auto& param = metadata.params[paramIdx];

                auto range = std::abs(param.maxValue - param.minValue);
#ifdef TRACKER_MINI
                auto precision = (param.isEnum && param.minValue == 0) ? 1000000.0f
                                 : range >= 10000                      ? range * 0.03f
                                 : range >= 5000                       ? range * 0.1f
                                 : range >= 1000                       ? range * 0.2f
                                 : range >= 500                        ? range * 0.5f
                                 : range >= 100                        ? range * 10.0f
                                 : range >= 10                         ? range * 20.0f
                                                                       : range * 200.0f;
#else
                auto precision = (param.isEnum && param.minValue == 0) ? 1000000.0f
                                 : range >= 10000                      ? range * 0.1f
                                 : range >= 5000                       ? range * 0.5f
                                 : range >= 1000                       ? range * 2.0f
                                 : range >= 500                        ? range * 3.2f
                                 : range >= 100                        ? range * 10.0f
                                 : range >= 10                         ? range * 20.0f
                                                                       : range * 200.0f;
#endif

                float paramValue = patch.getParamValueEditMapped(paramIdx).value_or(0) * precision;

                paramValue = clamp<float>(paramValue + (value * 2), 0, precision);

                DISABLE_AUDIO_INTERRUPTS();
                patch.setParamValueEditMapped(
                  paramIdx, paramValue / precision, polysynth::Patch::ParamChangeSource::kKnob);
                ENABLE_AUDIO_INTERRUPTS();
            }
        }
    }

    getSynth()->setPatchDirty(syntPartIdx);

#if MT_HAS_SYNTHS // MT: WAVE's and PHASE's LFO Sync redisplays other controls: the list again
    bool mtRebuild = false;
    if (!isMacroSelected())
    {
        const auto& mtMetadata = patch.getMetadata();
        const int mtParam = sectionParamsIds[paramListPos];
        mtRebuild = mods::synths::ownsLayout(mtMetadata.instrumentTypeId) && mtParam >= 0 &&
                    mtParam < std::ssize(mtMetadata.params) &&
                    mtMetadata.params[mtParam].invalidatesOtherMetadata;
    }
    showSynthPatchSectionParam(mtRebuild);
#else
    showSynthPatchSectionParam();
#endif
    showSynthPatchSectionParamValue();
    showSynthPatchMacroParam(refreshParamList);
    showSynthPatchMacroParamValue();
}

void cInstrumentEditor::changeSynthMacroName()
{
    if (!isMacroSelected())
    {
        return;
    }

    uint8_t syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    auto& patch = getSynth()->getPatch(syntPartIdx);
    const auto& metadata = patch.getMetadata();

    int macroIdx = sectionIds[sectionListPos];

    if (macroIdx < 0 && macroIdx >= std::ssize(metadata.macros))
    {
        return;
    }

    keyboardManager.fillName(metadata.macros[macroIdx].displayName.c_str());
    keyboardManager.activateKeyboard(polysynth::Patch::Metadata::Macro::kMaxDisplayNameLen);

    sequencer.stopManualNotes();
    titleBar->setTitle("Edit Macro Name");
    setSynthMacroNameKeyboardFunctions();
    showSaveAsKeyboard();
}

void cInstrumentEditor::gridChangeSelection(int direction)
{
    switch (direction)
    {
        case 0:
        {
            imgGridData.selection += imgGridData.selection >= kGridColumnsCount
                                       ? -kGridColumnsCount
                                       : (kGridRowsCount - 1) * kGridColumnsCount;
            break;
        }
        case 1:
        {
            imgGridData.selection += imgGridData.selection % kGridRowsCount < kGridColumnsCount - 1
                                       ? 1
                                       : -(kGridRowsCount - 1);
            break;
        }
        case 2:
        {
            imgGridData.selection +=
              imgGridData.selection < (kGridRowsCount - 1) * kGridColumnsCount
                ? kGridColumnsCount
                : -((kGridRowsCount - 1) * kGridColumnsCount);
            break;
        }
        case 3:
        {
            imgGridData.selection +=
              imgGridData.selection % kGridRowsCount > 0 ? -1 : kGridRowsCount - 1;
            break;
        }
        case 4:
        {
            imgGridData.showSelection = false;
            break;
        }
        default:
        {
            imgGridData.showSelection = true;
            break;
        }
    }

    display.refreshControl(imgGridControl);
}

void cInstrumentEditor::setSection(int sectionIdx)
{
    if (sectionIdx < 0 || sectionIdx >= sectionListLen)
    {
        return;
    }

    int previousValue = sectionListPos;

    sectionListPos = sectionIdx;
    sectionListPos = clamp<int8_t>(sectionListPos, 0, sectionListLen ? sectionListLen - 1 : 0);

#if MT_HAS_SYNTHFX // MT: picking a section, even the same one, leaves Synth FX's rows
    const bool mtFxLeft = mtFxView;
    mtFxView = false;
    if (previousValue != sectionListPos || mtFxLeft)
#else
    if (previousValue != sectionListPos)
#endif
    {
        showSynthPatchSection();
        showSynthPatchSectionParam(true);
        showSynthPatchSectionParamValue(true);
        showSynthPatchMacroParam(true);
        showSynthPatchMacroParamValue(true);
    }

    imgGridData.showSelection = false;
    selectedPlace[mtInstEditModeSynthPatchEdit] = 1;

    display.refreshControl(imgGridControl);

    activateLabelsBorder();

    display.synchronizeRefresh();
}

// ------------------------------------------------------------------------------------------
// ------------------------------       GUI     ---------------------------------------------
// ------------------------------------------------------------------------------------------

void cInstrumentEditor::showSynthPatchEditorScreen()
{
    assert(mtProject.values.lastUsedInstrument >= SYNTH_INSTRUMENTS_START_IDX);

#if MT_HAS_SYNTHFX // MT: the screen opens on the section, not Synth FX's rows
    mtFxView = false;
#endif

    showTitleBar();

#ifdef TRACKER_MINI
    refreshFramePosition();
    if (selectedPlace[mtInstEditModeSynthPatchEdit] == 0)
    {
        imgGridData.showSelection = true;
        display.setControlHide(IE->frameControl);
        display.refreshControl(IE->frameControl);
    }
    else
    {
        imgGridData.showSelection = false;
    }
#else
    selectedPlace[mtInstEditModeSynthPatchEdit] = 1;
    imgGridData.showSelection = false;
    refreshFramePosition();
#endif

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 0);
        display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
        display.setControlHide(barControl[i]);
        display.setRemoveControlStyle(label[i], controlStyleLimitTextWidth);
    }
    display.setControlColors(barControl[5], localActiveBarColors);
    display.setControlColors(modelListControl, display.getTheme().activeListColors);

    display.setControlData(label[6], &instArrows);

    display.setAddControlStyle(label[6], controlStyleShowBitmap);
    display.setRemoveControlStyle(label[6], controlStyleSpecialFx2);
    display.setRemoveControlStyle(label[7], controlStyleSpecialFx2);
    display.setRemoveControlStyle(label[6], controlStyleBackground);
    display.setRemoveControlStyle(label[7], controlStyleBackground);
    display.setControlValue(label[2], 1);

    display.setControlColors(modelListControl, display.getTheme().activeListColors);

    display.setControlText(label[0], "Save");
    display.setControlText(label[1], "Save As");
    display.setControlText(label[2], "Init");
#if MT_HAS_SYNTHFX // MT: button 3 opens Synth FX's rows
    display.setControlText(label[3], mtFxAvailable() ? "Synth FX" : "");
#else
    display.setControlText(label[3], "");
#endif
    display.setControlText(label[4], "");
    display.setControlText(label[5], "Parameter");
    display.setControlText(label[6], "");
    display.setControlText(label[7], "Back");

    display.setControlText2(label[0], "");
    display.setControlText2(label[1], "");
    display.setControlText2(label[2], "Patch");
    display.setControlText2(label[3], "");
    display.setControlText2(label[4], "");
    display.setControlText2(label[5], "");
    display.setControlText2(label[6], "");
    display.setControlText2(label[7], "");
    display.setControlPosition(label[4], (800 / 8) * 4 + (800 / 16), -1);

    showSynthPatchSection(true);
    showSynthPatchSectionParam(true);
    showSynthPatchSectionParamValue(true);
    showSynthPatchMacroParam(true);
    showSynthPatchMacroParamValue(true);

    imgGridData.columnsCount = kGridColumnsCount;
    imgGridData.rowsCount = kGridRowsCount;
    imgGridData.imgW = 40;
    imgGridData.imgH = 40;
    imgGridData.textList = sectionNamesList;
    imgGridData.imgList = sectionImgList;
    display.setControlShow(imgGridControl);

    display.setControlData(paramsListControl, &paramsListData);
    display.setControlPosition(paramsListControl, (800 / 8) * 4 + 1, -1);
    display.setControlSize(paramsListControl, (800 / 8) * 3 - 3, -1);
    display.setControlShow(paramsListControl);

    display.setControlData(modelListControl, &modelListData);
    display.setControlPosition(modelListControl, (800 / 8) * 7 + 1, -1);
    display.setControlSize(modelListControl, 800 / 8 - 3, -1);

    uint8_t syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    assert(mtProject.values.synthPart[syntPartIdx].synthType < sizeof(synthModelListImages));
    labelImage.bitmaps[0].bitmapIndex =
      synthModelListImages[mtProject.values.synthPart[syntPartIdx].synthType];
    labelImage.bitmaps[0].xValue = 116;
    labelImage.bitmaps[0].yValue = 49;
    labelImage.bitmaps[1].bitmapIndex = 0;
    display.setControlData(noteLabel, &labelImage);
    display.setControlStyle(noteLabel, controlStyleShowBitmap);
    display.setControlColors(noteLabel, display.getTheme().patchEditSynthModelIconColors);
    display.setControlText(noteLabel, "");
    display.setControlValue(noteLabel, 0);
    display.setControlPosition(noteLabel, 50, 45);
    display.setControlSize(noteLabel, 108, 40);
    display.setControlShow(noteLabel);
    display.refreshControl(noteLabel);

    display.setControlHide(doubleLabelAutomation);
    display.setControlHide(instrumentSynthCCListControl);
    display.setControlHide(instrumentListControl);
    display.setControlHide(filterModeListControl);
    display.setControlHide(list1Control);
    display.setControlHide(list2Control);
    display.setControlHide(list3Control);
    display.setControlHide(list4Control);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlStyle2(label[i],
                                 controlStyleCenterX | controlStyleCenterY | controlStyleFont3);
        display.setControlShow(label[i]);
        display.refreshControl(label[i]);
    }

    display.setControlValue(bgLabel, 158);
    display.refreshControl(bgLabel);
    display.setControlShow(bgLabel);

    display.setControlPosition(blankSpaceCover[0], (800 / 8) * 0 + 1, -1);
    display.setControlSize(blankSpaceCover[0], (800 / 8) * 4 - 3, 394);
    display.setControlShow(blankSpaceCover[0]);
    display.refreshControl(blankSpaceCover[0]);

    display.setControlPosition(blankSpaceCover[1], (800 / 8) * 7 + 1, -1);
    display.setControlSize(blankSpaceCover[1], (800 / 8) * 1 - 3, 394);

    display.synchronizeRefresh();
}

void cInstrumentEditor::showSaveAsKeyboard()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlHide(barControl[i]);
    }

    display.setControlHide(doubleLabelAutomation);
    display.setControlHide(instrumentSynthCCListControl);
    display.setControlHide(instrumentListControl);
    display.setControlHide(filterModeListControl);
    display.setControlHide(blankSpaceCover[0]);
    display.setControlHide(list1Control);
    display.setControlHide(list2Control);
    display.setControlHide(list3Control);
    display.setControlHide(list4Control);
    display.setControlHide(noteLabel);
    display.setControlHide(frameControl);
    display.setControlHide(paramsListControl);
    display.setControlHide(modelListControl);
    display.setControlHide(blankSpaceCover[1]);
    display.setControlHide(imgGridControl);

    for (uint8_t i = 0; i < 8; i++)
    {
        display.setControlValue(label[i], 0);
        display.setControlText(label[i], "");
        display.setControlText2(label[i], "");
        display.setControlColors(label[i], display.getTheme().activeButtonLabelsColors);
    }
    display.setRemoveControlStyle(label[6], controlStyleShowBitmap);

    display.setControlText(label[0], "Enter");
    display.setControlText(label[1], "Auto Name");
    display.setControlText(label[6], "Cancel");
    display.setControlText(label[7], "Save");

    for (uint8_t i = 0; i < 8; i++)
    {
        display.refreshControl(label[i]);
    }

    display.setControlValue(bgLabel, 255);
    display.refreshControl(bgLabel);

    mtPopups.hideInfoPopup();

    display.synchronizeRefresh();
}

void cInstrumentEditor::showSynthPatchSection(bool refreshList)
{
    if (refreshList)
    {
        int syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;

        auto& patch = getSynth()->getPatch(syntPartIdx);
        const auto& metadata = patch.getMetadata();

        if (sectionNamesBufor != nullptr)
        {
            delete[] sectionNamesBufor;
        }

        if (sectionNamesGridBuff != nullptr)
        {
            delete[] sectionNamesGridBuff;
        }

        if (sectionNamesList != nullptr)
        {
            delete[] sectionNamesList;
        }

        if (sectionIds != nullptr)
        {
            delete[] sectionIds;
        }

        int idx = 0;

        sectionNamesBufor = new char[kMaxSynthSectionStringsCount * kMaxSynthListStringLen];
        sectionNamesGridBuff = new char[kMaxSynthSectionStringsCount * kMaxSynthListStringLen];
        sectionNamesList = new char*[kMaxSynthSectionStringsCount];
        sectionIds = new int[kMaxSynthSectionStringsCount];

        const char* shortName = nullptr;
        int number = 0;
        int envCount = 1;
        int lfoCount = 1;

        for (const auto& section : metadata.layoutFlat.sections)
        {
            if (const auto& sectionName = section.name; sectionName != "")
            {
                polystrncpy(&sectionNamesBufor[kMaxSynthListStringLen * idx],
                            sectionName.c_str(),
                            kMaxSynthListStringLen);

                if (sectionName.find("Amp Envelope") != std::string::npos)
                {
                    shortName = "Amp Env";
                    number = 0;
                    sectionImgList[idx] = kBitmapEnv;
                }
                else if (sectionName.find("Amplifier") != std::string::npos)
                {
                    shortName = "Amp";
                    number = 0;
                    sectionImgList[idx] = kBitmapEnv;
                }
                else if (sectionName.find("Envelope") != std::string::npos)
                {
                    shortName = "Env";
                    number = envCount;
                    envCount++;
                    sectionImgList[idx] = kBitmapEnv;
                }
                else if (sectionName.find("Filter") != std::string::npos)
                {
                    shortName = "Filter";
                    number = 0;
                    sectionImgList[idx] = kBitmapFilter;
                }
                else if (sectionName.find("Voice") != std::string::npos)
                {
                    shortName = "Voice";
                    number = 0;
                    sectionImgList[idx] = kBitmapGear;
                }
                else if (sectionName.find("LFO") != std::string::npos)
                {
                    shortName = "LFO";
                    number = lfoCount;
                    lfoCount++;
                    sectionImgList[idx] = kBitmapLfo;
                }
                else if (sectionName.find("Modulation") != std::string::npos)
                {
                    shortName = "Mod";
                    number = 0;
                    sectionImgList[idx] = kBitmapMod;
                }
                else if (sectionName.find("Oscillator") != std::string::npos)
                {
                    shortName = "Osc";
                    number = 0;
                    sectionImgList[idx] = kBitmapOsc;
                }
                else if (sectionName.find("Shaper") != std::string::npos)
                {
                    shortName = "Shp";
                    number = 0;
                    sectionImgList[idx] = kBitmapShaper;
                }
                else if (sectionName.find("Kick") != std::string::npos)
                {
                    shortName = "Kick";
                    number = 0;
                    sectionImgList[idx] = kBitmapKick;
                }
                else if (sectionName.find("Tom") != std::string::npos)
                {
                    shortName = "Tom";
                    number = 0;
                    sectionImgList[idx] = kBitmapTom;
                }
                else if (sectionName.find("Snare") != std::string::npos)
                {
                    shortName = "Snare";
                    number = 0;
                    sectionImgList[idx] = kBitmapSnare;
                }
                else if (sectionName.find("Hihat") != std::string::npos)
                {
                    shortName = "Hihat";
                    number = 0;
                    sectionImgList[idx] = kBitmapHihat;
                }
                else if (sectionName.find("Cymbal") != std::string::npos)
                {
                    shortName = "Cymbal";
                    number = 0;
                    sectionImgList[idx] = kBitmapCymbal;
                }
                else if (sectionName.find("Percussion") != std::string::npos)
                {
                    shortName = "Perc";
                    number = 0;
                    sectionImgList[idx] = kBitmapPercussion;
                }
                else if (sectionName.find("Mixer") != std::string::npos)
                {
                    shortName = "Mixer";
                    number = 0;
                    sectionImgList[idx] = kBitmapMixer;
                }
                else if (sectionName.find("FX") != std::string::npos)
                {
                    shortName = "FX";
                    number = 0;
                    sectionImgList[idx] = kBitmapFX;
                }
                else
                {
                    shortName = "";
                    number = 0;
                    sectionImgList[idx] = -1;
                }

#if MT_HAS_SYNTHS // MT: WAVE's and PHASE's tiles show their sections' short names
                if (mods::synths::ownsLayout(metadata.instrumentTypeId) && !section.shortName.empty())
                {
                    shortName = section.shortName.c_str();
                    number = 0;
                }
#endif

                sectionNamesList[idx] = &sectionNamesGridBuff[kMaxSynthGridStringLen * idx];

                if (number > 0)
                {
                    snprintf(
                      sectionNamesList[idx], kMaxSynthGridStringLen, "%s %d", shortName, number);
                }
                else
                {
                    polystrncpy(sectionNamesList[idx], shortName, kMaxSynthGridStringLen);
                }
            }

            idx++;
            if (idx >= kMaxSynthSectionStringsCount)
            {
                break;
            }
        }

        firstMacroIdx = idx;

        int macroId = 0;
        for (const auto& macro : metadata.macros)
        {
            if (macroId != *polysynth::Patch::SystemMacroId::kPressure)
            {
                snprintf(&sectionNamesBufor[kMaxSynthListStringLen * idx],
                         kMaxSynthListStringLen,
                         "M%d %s",
                         macroId + 1,
                         macro.displayName.c_str());

                sectionNamesList[idx] = &sectionNamesGridBuff[kMaxSynthGridStringLen * idx];
                snprintf(sectionNamesList[idx], kMaxSynthGridStringLen, "M%d", macroId + 1);

                sectionIds[idx] = macroId;
                sectionImgList[idx] = kBitmapMacro;
                idx++;
                if (idx >= kMaxSynthSectionStringsCount)
                {
                    break;
                }
            }
            macroId++;
        }

        sectionListPos = clamp<int8_t>(sectionListPos, 0, idx ? idx - 1 : 0);
        sectionListLen = idx;

        while (idx < kMaxSynthSectionStringsCount)
        {
            sectionNamesList[idx] = &sectionNamesBufor[kMaxSynthListStringLen * idx];
            sectionNamesList[idx][0] = 0;
            sectionImgList[idx] = -1;
            idx++;
        }
    }

    display.setControlValue(imgGridControl, sectionListPos);
    display.refreshControl(imgGridControl);
}

void cInstrumentEditor::showSynthPatchSectionParam(bool refreshList)
{
#if MT_HAS_SYNTHFX // MT: Synth FX's rows
    if (mtFxView)
    {
        mtFxShowList(refreshList);
        return;
    }
#endif
    if (isMacroSelected())
    {
        return;
    }

    if (refreshList)
    {
        if (paramListTextBuff != nullptr)
        {
            delete[] paramListTextBuff;
        }

        if (paramListNames != nullptr)
        {
            delete[] paramListNames;
        }

        if (paramListValues != nullptr)
        {
            delete[] paramListValues;
        }

        if (sectionParamsIds != nullptr)
        {
            delete[] sectionParamsIds;
        }

        paramListTextBuff = new char[kMaxSynthParamStringsCount * kMaxSynthListStringLen * 2];
        paramListNames = new char*[kMaxSynthParamStringsCount];
        paramListValues = new char*[kMaxSynthParamStringsCount];
        sectionParamsIds = new int[kMaxSynthParamStringsCount];

        int syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
        auto& patch = getSynth()->getPatch(syntPartIdx);
        const auto& metadata = patch.getMetadata();
        const auto& section = metadata.layoutFlat.sections[sectionListPos];
        int idx = 0;

        for (const auto& element : section.elements)
        {
            if (element.type ==
                polysynth::Patch::Metadata::EditorLayout::EditorElement::Type::kParam)
            {
                const auto paramIdx = element.id;
                const auto& param = metadata.params[paramIdx];

                sectionParamsIds[idx] = paramIdx;

                paramListNames[idx] = &paramListTextBuff[(2 * kMaxSynthListStringLen) * idx];
                paramListValues[idx] = paramListNames[idx] + kMaxSynthListStringLen;

                // get name
                polystrncpy(paramListNames[idx], param.displayName.c_str(), kMaxSynthListStringLen);

                // get value
                auto formatter = param.valueFormatter;
                auto value = patch.getParamValue(paramIdx).value_or(param.defaultValue.value_or(0));

                if (formatter != nullptr)
                {
                    polystrncpy(paramListValues[idx],
                                formatter(patch, value).c_str(),
                                kMaxSynthListStringLen);
                }
                else
                {
                    snprintf(paramListValues[idx], kMaxSynthListStringLen, "%3.f", value);
                }

                idx++;
            }
            if (idx >= kMaxSynthParamStringsCount)
            {
                break;
            }
        }

        paramListPos = clamp(paramListPos, 0, idx ? idx - 1 : 0);

        paramsListData.start = paramListPos;
        paramsListData.length = idx;
        paramsListData.linesCount = std::min<uint16_t>(idx, 14);
        paramsListData.params = paramListNames;
        paramsListData.values = paramListValues;

        display.setControlData(paramsListControl, &paramsListData);
    }

    display.setControlValue(paramsListControl, paramListPos);
    display.refreshControl(paramsListControl);

    display.setControlText(label[5], &sectionNamesBufor[kMaxSynthListStringLen * sectionListPos]);
    display.refreshControl(label[5]);
}

void cInstrumentEditor::showSynthPatchMacroParam(bool refreshList)
{
#if MT_HAS_SYNTHFX // MT: Synth FX's rows
    if (mtFxView)
    {
        return;
    }
#endif
    if (!isMacroSelected())
    {
        return;
    }

    if (refreshList)
    {
        if (paramListTextBuff != nullptr)
        {
            delete[] paramListTextBuff;
        }

        if (paramListNames != nullptr)
        {
            delete[] paramListNames;
        }

        if (paramListValues != nullptr)
        {
            delete[] paramListValues;
        }

        constexpr int kMacroParamsLinesCount = 2 + kMaxMacroParamMods * 2;

        paramListTextBuff = new char[kMacroParamsLinesCount * kMaxSynthListStringLen * 2];
        paramListNames = new char*[kMacroParamsLinesCount];
        paramListValues = new char*[kMacroParamsLinesCount];

        for (int lineIdx = 0; lineIdx < kMacroParamsLinesCount; lineIdx++)
        {
            paramListNames[lineIdx] = &paramListTextBuff[(2 * kMaxSynthListStringLen) * lineIdx];
            paramListValues[lineIdx] = paramListNames[lineIdx] + kMaxSynthListStringLen;
        }

        int syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
        auto& patch = getSynth()->getPatch(syntPartIdx);
        const auto& metadata = patch.getMetadata();
        int idx = 0;

        int macroIdx = sectionIds[sectionListPos];
        if (macroIdx >= 0 && macroIdx < std::ssize(metadata.macros))
        {
            polystrncpy(paramListNames[idx], "Macro name", kMaxSynthListStringLen);
            polystrncpy(paramListValues[idx],
                        metadata.macros[macroIdx].displayName.c_str(),
                        kMaxSynthListStringLen);

            idx++;

            polystrncpy(paramListNames[idx], "Bipolar", kMaxSynthListStringLen);
            polystrncpy(paramListValues[idx],
                        metadata.macros[macroIdx].bipolar ? "On" : "Off",
                        kMaxSynthListStringLen);

            idx++;

            for (int modifierId = 0; modifierId < kMaxMacroParamMods; modifierId++)
            {
                snprintf(
                  paramListNames[idx], kMaxSynthListStringLen, "Parameter %d", modifierId + 1);

                const auto* modifier = modifierId < std::ssize(metadata.macros[macroIdx].modifiers)
                                         ? &metadata.macros[macroIdx].modifiers[modifierId]
                                         : nullptr;
                int paramId = modifier != nullptr && modifier->paramId ? *modifier->paramId : -1;
                if (paramId >= 0)
                {
                    polystrncpy(paramListValues[idx],
                                getSynth()->getParamFullName(syntPartIdx, paramId).c_str(),
                                kMaxSynthListStringLen);
                }
                else
                {
                    polystrncpy(paramListValues[idx], "Unassigned", kMaxSynthListStringLen);
                }

                idx++;

                snprintf(paramListNames[idx], kMaxSynthListStringLen, "Amount %d", modifierId + 1);

                float amount = modifier != nullptr ? modifier->amount : 0;

                snprintf(paramListValues[idx], kMaxSynthListStringLen, "%0.2f", amount);

                idx++;

                if (modifier == nullptr)
                {
                    break;
                }

                if (idx >= kMacroParamsLinesCount)
                {
                    break;
                }
            }
        }

        paramListPos = clamp(paramListPos, 0, idx ? idx - 1 : 0);

        paramsListData.start = paramListPos;
        paramsListData.length = idx;
        paramsListData.linesCount = std::min<uint16_t>(idx, 14);
        paramsListData.params = paramListNames;
        paramsListData.values = paramListValues;

        display.setControlData(paramsListControl, &paramsListData);
    }

    display.setControlValue(paramsListControl, paramListPos);
    display.refreshControl(paramsListControl);

    display.setControlText(label[5], &sectionNamesBufor[kMaxSynthListStringLen * sectionListPos]);
    display.refreshControl(label[5]);
}

void cInstrumentEditor::showSynthPatchSectionParamValue(bool refreshList)
{
#if MT_HAS_SYNTHFX // MT: Synth FX's rows
    if (mtFxView)
    {
        mtFxShowValue(refreshList);
        return;
    }
#endif
    if (isMacroSelected())
    {
        return;
    }

    int syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    auto& patch = getSynth()->getPatch(syntPartIdx);
    const auto& metadata = patch.getMetadata();

    int paramIdx = sectionParamsIds[paramListPos];
    if (paramIdx < 0 && paramIdx >= std::ssize(metadata.params))
    {
        display.setControlHide(barControl[7]);
        display.setControlHide(modelListControl);
        display.synchronizeRefresh();
        return;
    }

    const auto& param = metadata.params[paramIdx];

    if (refreshList)
    {
        if (param.isEnum && param.minValue == 0)
        {
            if (paramValueListTextBuff != nullptr)
            {
                delete[] paramValueListTextBuff;
            }

            if (paramValueListNames != nullptr)
            {
                delete[] paramValueListNames;
            }

            int length = param.maxValue + 1;

            paramValueListTextBuff = new char[length * kMaxSynthListStringLen];
            paramValueListNames = new char*[length];

            auto formatter = param.valueFormatter;

            for (int idx = 0; idx < length; idx++)
            {
                paramValueListNames[idx] = &paramValueListTextBuff[kMaxSynthListStringLen * idx];

                if (formatter != nullptr)
                {
                    polystrncpy(paramValueListNames[idx],
                                formatter(patch, idx).c_str(),
                                kMaxSynthListStringLen);
                }
                else
                {
                    snprintf(paramValueListNames[idx], kMaxSynthListStringLen, "%3.f", (float)idx);
                }
            }

            paramValueListPos =
              std::lround(patch.getParamValue(paramIdx).value_or(param.defaultValue.value_or(0)));

            paramValueListPos = clamp<int>(paramValueListPos, 0, length ? length - 1 : 0);
            modelListData.rowHeight = 0;
            modelListData.scrollText = true;
            modelListData.start = paramValueListPos;
            modelListData.length = length;
            modelListData.linesCount = std::min<uint16_t>(length, 14);
            modelListData.params = paramValueListNames;
            modelListData.imageIds = nullptr;

            display.setControlData(modelListControl, &modelListData);

            paramValueMode = kList;
        }
        else
        {
            paramValueMode = kValue;
        }
    }

    auto range = std::abs(param.maxValue - param.minValue);
    auto value = patch.getParamValue(paramIdx).value_or(param.defaultValue.value_or(0));

    if (paramValueMode == kList)
    {
        paramValueListPos = std::lround(value);

        display.setControlHide(barControl[7]);
        display.setControlShow(modelListControl);
        display.setControlValue(modelListControl, paramValueListPos);
        display.refreshControl(modelListControl);
        display.synchronizeRefresh();
    }
    else if (paramValueMode == kValue)
    {
        int barValue = 0;

        if (range > param.maxValue && param.maxValue == -param.minValue)
        {
            barValue = value * 100.0f / param.maxValue;

            display.setAddControlStyle(barControl[7], controlStyleValueLeftRight_100_100);
            display.setRemoveControlStyle(barControl[7], controlStyleValue_0_100);
        }
        else
        {
            barValue = (value - param.minValue) * 100.0f / range;

            display.setAddControlStyle(barControl[7], controlStyleValue_0_100);
            display.setRemoveControlStyle(barControl[7], controlStyleValueLeftRight_100_100);
        }

        display.setControlValue(barControl[7], barValue);
        display.setControlShow(barControl[7]);
        display.refreshControl(barControl[7]);
        display.setControlHide(modelListControl);
        display.synchronizeRefresh();
    }
    else
    {
        display.setControlHide(barControl[7]);
        display.setControlHide(modelListControl);
        display.setControlShow(blankSpaceCover[1]);
        display.refreshControl(blankSpaceCover[1]);
        display.synchronizeRefresh();
        return;
    }

    auto formatter = param.valueFormatter;
    if (formatter != nullptr)
    {
        polystrncpy(
          paramListValues[paramListPos], formatter(patch, value).c_str(), kMaxSynthListStringLen);
    }
    else
    {
        snprintf(paramListValues[paramListPos], kMaxSynthListStringLen, "%3.f", value);
    }
}

void cInstrumentEditor::showSynthPatchMacroParamValue(bool refreshList)
{
#if MT_HAS_SYNTHFX // MT: Synth FX's rows
    if (mtFxView)
    {
        return;
    }
#endif
    if (!isMacroSelected())
    {
        return;
    }

    int syntPartIdx = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;

    auto& patch = getSynth()->getPatch(syntPartIdx);
    const auto& metadata = patch.getMetadata();

    int macroIdx = sectionIds[sectionListPos];
    if (macroIdx < 0 && macroIdx >= std::ssize(metadata.macros))
    {
        display.setControlHide(barControl[7]);
        display.setControlHide(modelListControl);
        display.synchronizeRefresh();
        return;
    }

    if (refreshList)
    {
        int macroIdx = sectionIds[sectionListPos];
        paramValueMode = kNone;

        if (macroIdx >= 0 && macroIdx < std::ssize(metadata.macros))
        {
            const polysynth::Patch::Metadata::Macro& currentMacro = metadata.macros[macroIdx];

            if (paramListPos == 0)
            {
                paramValueMode = kNone;
                display.setControlText(label[5], "Edit");
                display.refreshControl(label[5]);
            }
            else if (paramListPos == 1)
            {
                paramValueListPos = currentMacro.bipolar ? 1 : 0;
                modelListData.rowHeight = 0;
                modelListData.scrollText = false;
                modelListData.start = paramValueListPos;
                modelListData.length = 2;
                modelListData.linesCount = 2;
                modelListData.params = (char**)OffOnNames;
                modelListData.imageIds = nullptr;

                display.setControlData(modelListControl, &modelListData);

                paramValueMode = kList;
            }
            else if (paramListPos % 2 == 0) // param
            {
                if (paramValueListTextBuff != nullptr)
                {
                    delete[] paramValueListTextBuff;
                }

                if (paramValueListNames != nullptr)
                {
                    delete[] paramValueListNames;
                }

                int length = patch.getMetadata().params.size() + 1;

                paramValueListTextBuff = new char[length * kMaxSynthListStringLen];
                paramValueListNames = new char*[length];

                for (int idx = 0; idx < length; idx++)
                {
                    paramValueListNames[idx] =
                      &paramValueListTextBuff[kMaxSynthListStringLen * idx];

                    if (idx - 1 >= 0)
                    {
                        polystrncpy(paramValueListNames[idx],
                                    getSynth()->getParamFullName(syntPartIdx, idx - 1).c_str(),
                                    kMaxSynthListStringLen);
                    }
                    else
                    {
                        polystrncpy(paramValueListNames[idx], "Unassigned", kMaxSynthListStringLen);
                    }
                }

                int modifierId = paramListPos / 2 - 1;
                const auto* modifier =
                  modifierId >= 0 && modifierId < std::ssize(currentMacro.modifiers)
                    ? &currentMacro.modifiers[modifierId]
                    : nullptr;

                paramValueListPos =
                  modifier != nullptr && modifier->paramId ? *modifier->paramId + 1 : 0;

                paramValueListPos = clamp<int>(paramValueListPos, 0, length ? length - 1 : 0);
                modelListData.rowHeight = 0;
                modelListData.scrollText = true;
                modelListData.start = paramValueListPos;
                modelListData.length = length;
                modelListData.linesCount = std::min<uint16_t>(length, 14);
                modelListData.params = paramValueListNames;
                modelListData.imageIds = nullptr;

                display.setControlData(modelListControl, &modelListData);

                paramValueMode = kList;
            }
            else if (paramListPos % 2 == 1) // amount
            {
                paramValueMode = kValue;
            }
        }
    }

    if (paramValueMode == kList)
    {
        if (paramListPos == 1)
        {
            polystrncpy(paramListValues[paramListPos],
                        metadata.macros[macroIdx].bipolar ? "On" : "Off",
                        kMaxSynthListStringLen);
        }
        else
        {
            if (paramValueListPos > 0)
            {
                polystrncpy(
                  paramListValues[paramListPos],
                  getSynth()->getParamFullName(syntPartIdx, paramValueListPos - 1).c_str(),
                  kMaxSynthListStringLen);
            }
            else
            {
                polystrncpy(paramListValues[paramListPos], "Unassigned", kMaxSynthListStringLen);
            }
        }

        display.setControlHide(barControl[7]);
        display.setControlHide(blankSpaceCover[1]);
        display.setControlShow(modelListControl);
        display.setControlValue(modelListControl, paramValueListPos);
        display.refreshControl(modelListControl);
        display.synchronizeRefresh();
    }
    else if (paramValueMode == kValue)
    {
        int modifierId = paramListPos / 2 - 1;
        int barValue = 50;
        float amount = 0;

        if (modifierId >= 0 && modifierId < kMaxMacroParamMods)
        {
            if (modifierId < std::ssize(metadata.macros[macroIdx].modifiers))
            {
                barValue = (metadata.macros[macroIdx].modifiers[modifierId].amount +
                            metadata.macros[macroIdx].modifiers[modifierId].kMaxAmount) *
                           100.0f /
                           (metadata.macros[macroIdx].modifiers[modifierId].kMaxAmount * 2);
                amount = metadata.macros[macroIdx].modifiers[modifierId].amount;
            }
            snprintf(paramListValues[paramListPos], kMaxSynthListStringLen, "%0.2f", amount);
        }

        display.setAddControlStyle(barControl[7], controlStyleValue_0_100);
        display.setRemoveControlStyle(barControl[7], controlStyleValueLeftRight_100_100);
        display.setControlValue(barControl[7], barValue);
        display.setControlShow(barControl[7]);
        display.refreshControl(barControl[7]);

        display.setControlHide(modelListControl);
        display.setControlHide(blankSpaceCover[1]);
        display.synchronizeRefresh();
    }
    else
    {
        display.setControlHide(barControl[7]);
        display.setControlHide(modelListControl);
        display.setControlShow(blankSpaceCover[1]);
        display.refreshControl(blankSpaceCover[1]);
        display.synchronizeRefresh();
        return;
    }
}
#endif
