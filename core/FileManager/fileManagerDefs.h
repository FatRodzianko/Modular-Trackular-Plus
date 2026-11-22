
#ifndef FILEMANAGER_FILEMANAGERDEFS_H_
#define FILEMANAGER_FILEMANAGERDEFS_H_

#include "mtStructs.h"

#include <cr_section_macros.h>
#include <stdint.h>

struct strProjectFileHeader
{
    char id_file[2];              // typ pliku, np MT = plik projektu, TI = instrument
    uint16_t type;                // typ w liczbie
    char fwVersion[4];            // wersja fw w ktorej zapisano plik
    char fileStructureVersion[4]; // wersja struktury pliku
    uint16_t size;                // rozmiar
};

struct strInstrumentFile
{
    struct strInstrumentDataAndHeader
    {
        strProjectFileHeader instrHeader;
        strInstrument instrument;

    } instrumentDataAndHeader;

    uint32_t crc;
};

struct strProjectFile
{
    struct strProjectDataAndHeader
    {
        strProjectFileHeader projectHeader;
        strMtProjectRemote project;
    } projectDataAndHeader;

    uint32_t crc;
};

//----------------------------------------------------------------------------------

enum enFileManagerErrorSource
{
    errorSourceUnknow = 0,
    errorSourceProject,
    errorSourcePattern,
    errorSourceInstrument,
    errorSourceSample,

};

const char cNewProjectName[] = "New Project";

const char cWorkspacePath[] = "Workspace";
const char cProjectFileNameInWorkspace[] = "Workspace/project.mt";
const char cWorkspacePatternsPath[] = "Workspace/patterns";
const char cWorkspacePatternFileFormat[] = "Workspace/patterns/pattern_%02d.mtp";
const char cWorkspaceInstrumentsPath[] = "Workspace/instruments";
const char cWorkspaceInstrumentFileFormatOld[] = "Workspace/instruments/instrument_%02d.mti";
const char cWorkspaceInstrumentFileFormatNew[] = "Workspace/instruments/instrument_%02d.pti";
const char cWorkspaceSamplesPath[] = "Workspace/samples";
const char cWorkspaceSamplesFilesFormat[] = "Workspace/samples/instr%02d.wav";
const char cWorkspacePatternsMetadataFilePath[] = "Workspace/patterns/patternsMetadata";

const char cDefaultProjectsPath[] = "Projects";
const char cProjectsPathFormat[] = "%s/%s";
const char cProjectFileInProjectsFormat[] = "%s/%s/project.mt";
const char cProjectsPatternsPathFormat[] = "%s/%s/patterns";
const char cProjectsPatternFileFormat[] = "%s/%s/patterns/pattern_%02d.mtp";
const char cProjectsPatternsMetadataFileFormat[] = "%s/%s/patterns/patternsMetadata";
const char cProjectsInstrumentsPathFormat[] = "%s/%s/instruments";
const char cProjectsInstrumentFileFormatOld[] = "%s/%s/instruments/instrument_%02d.mti";
const char cProjectsInstrumentFileFormatNew[] = "%s/%s/instruments/%d %s.pti";
const char cProjectsInstrumentFileFormatNewAlt[] = "%s/%s/instruments/%02d %s.pti";
const char cProjectsSamplesFilesFormat[] = "%s/%s/samples/instr%02d.wav";
const char cProjectsSamplesPathFormat[] = "%s/%s/samples";

const char cProjectSamplesExportFilesFormat[] = "%s/%s/export/samples/%s%s.wav";
const char cProjectSamplesExportPathFormat[] = "%s/%s/export/samples";
const char cProjectExportPathFormat[] = "%s/%s/export";

const char cExportPath[] = "Export";
const char cExportProjectPathFormat[] = "Export/%s";

const char cModsPath[] = "Mod";
const char cModsPathFormat[] = "Mod/%s";

const char cExportModPath[] = "Mod";
const char cExportModFilePathFormat[] = "Mod/%s.it";

const char cProjectFileName[] = "project.mt";

const char cProjectsRecentFilePath[] = "Projects/recents";

const uint8_t WORKSPACE_SAMPLES_FORMAT_MAX_LENGTH = 38;

const uint8_t WORKSPACE_INSTRUMENTS_FORMAT_MAX_LENGTH = 47;

#endif /* FILEMANAGER_FILEMANAGERDEFS_H_ */
