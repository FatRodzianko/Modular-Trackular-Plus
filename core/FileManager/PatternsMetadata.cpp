#include "PatternsMetadata.h"

#include "fileManager.h"
#include "fileManagerDefs.h"
#include "log.h"
#include "mtStructs.h"

#include <algorithm>
#include <cr_section_macros.h>
#include <string.h>

struct PattensMetadataFile
{
    struct Header
    {
        char id[4];
        uint16_t version;
        uint32_t dataSize;
        uint32_t control;
    } header;
    struct PatternRecord
    {
        char name[PATTERN_NAME_LENGTH + 1];
        static_assert(sizeof(name) < kMaxFileMemoryPerPatternMetadata);
        char emptySpace[kMaxFileMemoryPerPatternMetadata - sizeof(name)];
    } pattern[PATTERN_INDEX_MAX + 1];
};

__NOINIT(EXTERNAL_RAM)
static PattensMetadataFile externalRamPattensMetadata;

void PatternsMetadata::init(FileSystem* fileSystem)
{
    this->fileSystem = fileSystem;
    successfulRead = false;
}

void PatternsMetadata::loadPatternsMetadata()
{
    if (readWorkspacePatternsMetadata() == false)
    {
        memset(externalRamPattensMetadata.pattern, 0, sizeof(externalRamPattensMetadata.pattern));

        writeWorkspacePatternsMetadata();
    }
}

bool PatternsMetadata::readWorkspacePatternsMetadata()
{
    return readPatternsMetadata(cWorkspacePatternsMetadataFilePath);
}

void PatternsMetadata::writeWorkspacePatternsMetadata()
{
    writePatternsMetadata(cWorkspacePatternsMetadataFilePath);

    mtProject.values.projectNotSavedFlag |= 1;
    newFileManager.setProjectStructChanged();
}

bool PatternsMetadata::readPatternsMetadata(const char* path)
{
    successfulRead = false;

    if (!fileSystem->exists(path))
    {
        return false;
    }

    auto openFileInfo = fileSystem->openFile(path);
    auto isOpen = std::get<0>(openFileInfo);
    auto file = std::get<1>(openFileInfo);

    if (!isOpen)
    {
        return false;
    }

    fileSystem->readFile(&externalRamPattensMetadata, sizeof(externalRamPattensMetadata), file);
    fileSystem->closeFile(file);

    successfulRead = true;
    return true;
}

void PatternsMetadata::writePatternsMetadata(const char* path)
{
    externalRamPattensMetadata.header.id[0] = 'P';
    externalRamPattensMetadata.header.id[1] = 'A';
    externalRamPattensMetadata.header.id[2] = 'M';
    externalRamPattensMetadata.header.id[3] = 'D';
    externalRamPattensMetadata.header.version = 1;
    externalRamPattensMetadata.header.control = 0;
    externalRamPattensMetadata.header.dataSize = sizeof(externalRamPattensMetadata);

    newFileManager.createWorkspaceDirs();

    auto openFileInfo = fileSystem->openFile(path, FileSystem::FileOpenMode::kWriteOpen);
    auto isOpen = std::get<0>(openFileInfo);
    auto file = std::get<1>(openFileInfo);

    if (isOpen)
    {
        fileSystem->writeFile(
          &externalRamPattensMetadata, sizeof(externalRamPattensMetadata), file);
        fileSystem->closeFile(file);
    }
}

bool PatternsMetadata::copyPatternsMetadata(const char* src, const char* dest)
{
    bool correct = true;

    if (readPatternsMetadata(src) == false)
    {
        correct = false;
        memset(externalRamPattensMetadata.pattern, 0, sizeof(externalRamPattensMetadata.pattern));
    }
    writePatternsMetadata(dest);

    return correct;
}

bool PatternsMetadata::validatePatternsMetadata()
{
    if (successfulRead == false)
    {
        if (readWorkspacePatternsMetadata() == false)
        {
            return false;
        }
    }

    if (strncmp("PAMD", externalRamPattensMetadata.header.id, 4) != 0)
    {
        return false;
    }

    return true;
}

char* PatternsMetadata::getPatternName(int idx)
{
    assert(idx <= PATTERN_INDEX_MAX);

    if (successfulRead == false)
    {
        return nullptr;
    }

    if (!validatePatternsMetadata())
    {
        memset(externalRamPattensMetadata.pattern, 0, sizeof(externalRamPattensMetadata.pattern));
    }

    bool correctString = false;

    for (int i = 0; i <= PATTERN_NAME_LENGTH; i++)
    {
        if (externalRamPattensMetadata.pattern[idx].name[i] == 0)
        {
            correctString = true;
        }
    }

    if (correctString == false)
    {
        return nullptr;
    }

    return externalRamPattensMetadata.pattern[idx].name;
}

void PatternsMetadata::setPatternName(int idx, char* name)
{
    assert(idx <= PATTERN_INDEX_MAX);

    validatePatternsMetadata();

    strncpy(externalRamPattensMetadata.pattern[idx].name, name, PATTERN_NAME_LENGTH);
    externalRamPattensMetadata.pattern[idx].name[PATTERN_NAME_LENGTH] = 0;

    writeWorkspacePatternsMetadata();
}
