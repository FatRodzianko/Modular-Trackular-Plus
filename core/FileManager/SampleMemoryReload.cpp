#include "SampleMemoryReload.h"

#include "fileManager.h"
#include "fileManagerDefs.h"
#include "fileTransfer.h"

#include <cstring>

void SampleMemoryReload::update()
{
    SampleMemoryReloadStep nextStep = operationStep;

    switch (operationStep)
    {
        case SampleMemoryReloadStep::initStep:
        {
            sampleMemoryReloadInit();
            nextStep = SampleMemoryReloadStep::loadInstrumentStruct;
            break;
        }
        case SampleMemoryReloadStep::loadInstrumentStruct:
        {
            bool result = loadInstrStruct(currentInstrument, currentInstrumentFileDataOffset);
            if (result)
            {
                nextStep = SampleMemoryReloadStep::loadInstrumentSample;
            }
            else
            {
                if (currentInstrument < kSampleInstrumentsCount - 1)
                {
                    currentInstrument++;
                }
                else
                {
                    nextStep = SampleMemoryReloadStep::finishStep;
                }
            }
            break;
        }
        case SampleMemoryReloadStep::loadInstrumentSample:
        {
            bool result = newFileManager.loadSampleData(
              currentInstrument, sampleLoadStep, currentInstrumentFileDataOffset, true);
            if (result)
            {
                if (currentInstrument < kSampleInstrumentsCount - 1)
                {
                    currentInstrument++;
                    sampleLoadStep = 0;
                    nextStep = SampleMemoryReloadStep::loadInstrumentStruct;
                }
                else
                {
                    nextStep = SampleMemoryReloadStep::finishStep;
                }
            }
            break;
        }
        case SampleMemoryReloadStep::finishStep:
        {
            sampleMemoryReloadFinish();
            break;
        }
        default: stopOperationWithError(fmReloadSamplesError); break;
    }

    operationStep = nextStep;
}

//=================================================================================================
//=================================================================================================
void SampleMemoryReload::sampleMemoryReloadInit()
{
    currentInstrument = 0;
    sampleLoadStep = 0;

    mtProject.used_memory = 0;
}

// return value: true = success, false = fail
bool SampleMemoryReload::loadInstrStruct(int idx, uint32_t& instrumentFileDataOffsetOut)
{
    if (idx >= kSampleInstrumentsCount)
    {
        return false;
    }

    char instrumentFilePath[WORKSPACE_INSTRUMENTS_FORMAT_MAX_LENGTH];
    volatile int pathSize = sizeof(instrumentFilePath);

    snprintf(instrumentFilePath, pathSize, cWorkspaceInstrumentFileFormatNew, idx + 1);

    strInstrumentFile fileManagerInstrumentBuffer;
    uint32_t instrumentFileSize = fileSystem->getFileSize(instrumentFilePath);

    uint8_t loadStatus = fileTransfer.loadFileToMemory(instrumentFilePath,
                                                       (uint8_t*)&fileManagerInstrumentBuffer,
                                                       sizeof(strInstrumentFile),
                                                       fileWholeOnce);

    if (loadStatus == fileTransferEnd)
    {
        if (newFileManager.loadInstrumentFormFileStruct(&mtProject.instrument[idx],
                                                        &fileManagerInstrumentBuffer,
                                                        instrumentFileSize,
                                                        instrumentFileDataOffsetOut))
        {
            newFileManager.removeWavExtensionFromName(mtProject.instrument[idx].sample.file_name);
            mtProject.instrument[idx].isActive = 1;
            return true;
        }
    }

    newFileManager.completeLoadedInstrumentStruct(idx, false);
    return false;
}

void SampleMemoryReload::sampleMemoryReloadFinish()
{
    newFileManager.setStatus(fmReloadSamplesEnd);
    newFileManager.setCurrentOperation(fmNoOperation);
}

//=================================================================================================
//=================================================================================================
bool SampleMemoryReload::reload()
{
    if (newFileManager.getStatus() != fmIdle)
        return false;
    if (newFileManager.getCurrentOperation() != fmNoOperation)
        return false;

    newFileManager.setStatus(fmReloadingSamples);
    newFileManager.setCurrentOperation(fmReloadSamples);
    operationStep = SampleMemoryReload::initStep;
    return true;
}

//=================================================================================================
//=================================================================================================

//=================================================================================================
//=================================================================================================
