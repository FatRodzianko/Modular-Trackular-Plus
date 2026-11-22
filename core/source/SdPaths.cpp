#include "SdPaths.h"

namespace SdPaths
{
namespace
{
bool s_resolved = false;
bool s_useHiddenFallback = false;
uint32_t s_mountGeneration = 1;

void resolveIfNeeded(FileSystem* fs)
{
    if (s_resolved || fs == nullptr || !fs->isSdCardInserted())
    {
        return;
    }

    if (fs->exists(kPrimarySystemRoot) || fs->exists(kPrimarySystemRootRel))
    {
        s_useHiddenFallback = false;
        s_resolved = true;
        return;
    }

    if (fs->exists(kFallbackSystemRoot) || fs->exists(kFallbackSystemRootRel))
    {
        s_useHiddenFallback = true;
        s_resolved = true;
        return;
    }

    fs->mkdir(0, kPrimarySystemRootRel);
    s_useHiddenFallback = false;
    s_resolved = true;
}
} // namespace

const char* systemRoot(FileSystem* fs)
{
    resolveIfNeeded(fs);
    return s_useHiddenFallback ? kFallbackSystemRoot : kPrimarySystemRoot;
}

const char* systemRootRel(FileSystem* fs)
{
    resolveIfNeeded(fs);
    return s_useHiddenFallback ? kFallbackSystemRootRel : kPrimarySystemRootRel;
}

void resetCache()
{
    s_resolved = false;
    s_useHiddenFallback = false;
    ++s_mountGeneration;
}

uint32_t mountGeneration()
{
    return s_mountGeneration;
}

bool ensureDir(FileSystem* fs, const char* path)
{
    if (fs == nullptr || path == nullptr || path[0] == '\0')
    {
        return false;
    }

    std::string rel(path);
    while (!rel.empty() && rel.front() == '/')
    {
        rel.erase(rel.begin());
    }
    while (!rel.empty() && rel.back() == '/')
    {
        rel.pop_back();
    }
    if (rel.empty())
    {
        return true;
    }

    std::string acc;
    size_t pos = 0;
    while (pos < rel.size())
    {
        size_t nextSlash = rel.find('/', pos);
        std::string seg =
          (nextSlash == std::string::npos) ? rel.substr(pos) : rel.substr(pos, nextSlash - pos);
        if (!seg.empty())
        {
            if (!acc.empty())
            {
                acc += '/';
            }
            acc += seg;
            if (!fs->exists(acc.c_str()))
            {
                if (!fs->mkdir(0, acc.c_str()) && !fs->exists(acc.c_str()))
                {
                    return false;
                }
            }
        }
        if (nextSlash == std::string::npos)
        {
            break;
        }
        pos = nextSlash + 1;
    }
    return true;
}

bool ensureDir(FileSystem* fs, const std::string& path)
{
    return ensureDir(fs, path.c_str());
}

std::string effectsDir(FileSystem* fs)
{
    return std::string(systemRoot(fs)) + "/Effects";
}

std::string effectPresetsDir(FileSystem* fs, const std::string& sanitizedPluginName)
{
    return std::string(systemRoot(fs)) + "/Effects/Presets/" + sanitizedPluginName;
}

std::string envelopesDir(FileSystem* fs)
{
    return std::string(systemRoot(fs)) + "/Envelopes";
}

std::string fontsDir(FileSystem* fs)
{
    return std::string(systemRoot(fs)) + "/Fonts";
}

std::string infoFile(FileSystem* fs)
{
    return std::string(systemRoot(fs)) + "/info.txt";
}

std::string snapshotsDir(FileSystem* fs)
{
    return std::string(systemRoot(fs)) + "/Snapshots";
}

std::string synthsDir(FileSystem* fs)
{
    return std::string(systemRoot(fs)) + "/Synths";
}

std::string synthPatchesDir(FileSystem* fs)
{
    return std::string(systemRoot(fs)) + "/Synths/Patches";
}

std::string synthSamplesDir(FileSystem* fs)
{
    return std::string(systemRoot(fs)) + "/Synths/Samples";
}

std::string themesDir(FileSystem* fs)
{
    return std::string(systemRoot(fs)) + "/Themes";
}

std::string textDir(FileSystem* fs)
{
    return std::string(systemRoot(fs)) + "/Text";
}

std::string configFilePath(FileSystem* fs)
{
    return std::string(systemRoot(fs)) + "/trackerConfig";
}

} // namespace SdPaths
