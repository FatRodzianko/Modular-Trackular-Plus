#include "interfaceDefs.h"

#include "SdPaths.h"
#include "SdTextFile.h"
#include "TrackerImpl.h"
#include "cr_section_macros.h"
#include "interfaceFxDesc.h"

#include <cstdio>

constexpr uint8_t InterfaceGlobals::fxIDs[FX_COUNT];
constexpr char InterfaceGlobals::rndFxName[FX_NAME_SIZE + 4];
constexpr char InterfaceGlobals::fxNames[FX_COUNT][FX_NAME_SIZE + 4];
constexpr char InterfaceGlobals::fxShortNames[FX_COUNT][FX_NAME_SIZE + 4];
constexpr const char* InterfaceGlobals::ptrAllFxNames[FX_COUNT + 1];
constexpr const char* InterfaceGlobals::ptrAllFxNamesShort[FX_COUNT + 1];
constexpr const char* const* InterfaceGlobals::ptrFxNames;

namespace
{
__NOINIT(EXTERNAL_RAM) char s_fxHintBuf[640];
int16_t s_lastLoadedFxId = -1;
uint32_t s_lastMountGen = 0;
} // namespace

char* InterfaceGlobals::fxDescPtr(int16_t id)
{
    if (id < 0 || id >= FX_COUNT)
    {
        return const_cast<char*>("");
    }

    uint32_t mountGen = SdPaths::mountGeneration();
    if (id == s_lastLoadedFxId && mountGen == s_lastMountGen)
    {
        return s_fxHintBuf;
    }

    s_fxHintBuf[0] = '\0';

    auto* tracker = TrackerImpl::getInstance();
    auto* platform = (tracker != nullptr) ? tracker->getPlatform() : nullptr;
    FileSystem* fs = (platform != nullptr) ? platform->getFileSystem() : nullptr;

    if (fs != nullptr && fs->isSdCardInserted())
    {
        std::string path = SdPaths::textDir(fs) + "/fx_hints.txt";
        const char* baseKey = fxHintKeys[id];
        bool loaded = false;

#ifdef FEATURE_SYNTH
        char synthKey[40];
        snprintf(synthKey, sizeof(synthKey), "%s.SYNTH", baseKey);
        loaded =
          SdTextFile::readSection(fs, path.c_str(), synthKey, s_fxHintBuf, sizeof(s_fxHintBuf));
#endif
        if (!loaded)
        {
            SdTextFile::readSection(fs, path.c_str(), baseKey, s_fxHintBuf, sizeof(s_fxHintBuf));
        }
    }

    s_lastLoadedFxId = id;
    s_lastMountGen = mountGen;
    return s_fxHintBuf;
}
