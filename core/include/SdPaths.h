#ifndef SD_PATHS_H_
#define SD_PATHS_H_

#include "Platform.h"
#include <string>

namespace SdPaths
{
constexpr const char* kPrimarySystemRoot = "/System";
constexpr const char* kFallbackSystemRoot = "/.system";
constexpr const char* kPrimarySystemRootRel = "System";
constexpr const char* kFallbackSystemRootRel = ".system";
constexpr const char* kLegacyConfigFilePath = "/trackerConfig";

// Resolves and caches the active system root ("/System" or "/.system").
// Priority: /System -> /.system -> create /System.
const char* systemRoot(FileSystem* fs);

// Same as systemRoot() without a leading slash ("System" or ".system").
const char* systemRootRel(FileSystem* fs);

// Clears the cached system root (call on SD unmount / remount).
void resetCache();

// Recursively ensures all directory segments in `path` exist (leading slash optional).
bool ensureDir(FileSystem* fs, const char* path);
bool ensureDir(FileSystem* fs, const std::string& path);

std::string effectsDir(FileSystem* fs);
std::string effectPresetsDir(FileSystem* fs, const std::string& sanitizedPluginName);
std::string envelopesDir(FileSystem* fs);
std::string fontsDir(FileSystem* fs);
std::string imagesDir(FileSystem* fs);
std::string snapshotsDir(FileSystem* fs);
std::string synthsDir(FileSystem* fs);
std::string synthPatchesDir(FileSystem* fs);
std::string synthSamplesDir(FileSystem* fs);
std::string themesDir(FileSystem* fs);
std::string textDir(FileSystem* fs);
std::string configFilePath(FileSystem* fs);

} // namespace SdPaths

#endif /* SD_PATHS_H_ */
