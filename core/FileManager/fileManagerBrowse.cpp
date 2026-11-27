#include "fileManagerDefs.h"
#include "mtConfig.h"
#include "mtStructs.h"

#include "util_string.h"

#include "SdPaths.h"
#include "fileManager.h"

#include <algorithm>

void cFileManager::updateBrowseFirmwares() // fmBrowseFirmwares - 6
{
    switch (currentOperationStep)
    {
        case 0: browseFirmwaresLocation(); break;
        default: stopOperationWithError(fmBrowseFirmwaresError); break;
    }
}

uint32_t cFileManager::getSampleFileMemoryUsage(const char* filePath)
{
    if (filePath == nullptr || filePath[0] == '\0')
    {
        return 0;
    }

    uint32_t size = getWaveSizeAfterConversion(filePath, stereoSampleMode);
    if (size == 0)
    {
        size = getInstrumentSampleSizeIfValid(filePath);
    }
    return size;
}

///====================================================================================================
///====================================================================================================
///====================================================================================================
void cFileManager::browseFirmwaresLocation()
{

    int locationFileCount = fileSystem->createFilesList(
      "Firmware", 0, firmwaresList, list_length_max, 6000, fileSystem->enListFileFilter_firmware);

    if (locationFileCount == fileSystem->kListError)
    {
        stopOperationWithError(fmBrowseFirmwaresError);
        return;
    }

    std::sort(firmwaresList,
              firmwaresList + locationFileCount,
              [](char* a, char* b) { return compareStrings(b, a); });

    firmwaresListLength = locationFileCount;

    status = fmBrowseFirmwaresEnd;
    currentOperationStep = 0;
    currentOperation = fmNoOperation;
}

bool cFileManager::browseFirmwares()
{
    stopAutosave();

    status = fmBrowsingFirmwares;
    currentOperationStep = 0;
    currentOperation = fmBrowseFirmwares;

    return true;
}

uint8_t cFileManager::getFirmwaresList(char*** list)
{
    *list = firmwaresList;
    return firmwaresListLength;
}

const char* cFileManager::getProjectsDefaultPath()
{
    return cDefaultProjectsPath;
}

const char* cFileManager::getModsDefaultPath()
{
    return cModsPath;
}

void cFileManager::createProjectsAndModFolders()
{
    if (!fileSystem->exists(cDefaultProjectsPath))
    {
        fileSystem->mkdir(0, cDefaultProjectsPath);
    }

    if (!fileSystem->exists(cModsPath))
    {
        fileSystem->mkdir(0, cModsPath);
    }
}

void cFileManager::createPatchesFolder()
{
    SdPaths::ensureDir(fileSystem, SdPaths::synthPatchesDir(fileSystem));
}
