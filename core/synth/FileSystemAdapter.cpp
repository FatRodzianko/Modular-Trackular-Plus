// NOLINTBEGIN
#ifdef FEATURE_SYNTH
#include "FileSystemAdapter.h"
#include "config.h"

#include "cr_section_macros.h"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace FileSystemAdapterBuffers
{
__NOINIT(EXTERNAL_RAM) static uint8_t fileNames[kMaxFilesListLength * (kMaxFileNameLen + 1)];
__NOINIT(EXTERNAL_RAM) static uint8_t listDataName[kMaxFilesListLength * sizeof(char**)];
}

char* FileSystemAdapter::fileNamesBuffer = nullptr;
char** FileSystemAdapter::listDataName = nullptr;
FileSystemAdapter::FileSystemAdapter(const std::string& pathPrefix,
                                     ::FileSystem* platformFileSystem)
  : platformFileSystem(platformFileSystem)
  , pathPrefix(pathPrefix)
{
}

void FileSystemAdapter::staticInit()
{
#ifdef DEBUG
    static int instanceCounter = 0;
    assert(instanceCounter++ == 0);
#endif
    fileNamesBuffer = (char*)FileSystemAdapterBuffers::fileNames;
    listDataName = (char**)FileSystemAdapterBuffers::listDataName;
}

std::vector<std::string_view> FileSystemAdapter::listFiles(
  const std::string& path,
  std::function<bool(const ::std::string& name)> matcher,
  tiny_optional<int> /*maxFiles*/)
{

    memset(listDataName, 0, kMaxFilesListLength);

    auto wrappedMatcher = [&matcher](const ::std::string& name)
    {
        if (name.empty() || name[0] == '.')
        {
            return false;
        }
        return matcher ? matcher(name) : true;
    };

    fileCount = kMaxFilesListLength;
    platformFileSystem->listDirContents((pathPrefix + path).c_str(),
                                        ::FileSystem::DirContentType::kFiles,
                                        wrappedMatcher,
                                        fileNamesBuffer,
                                        kMaxFilesListLength * (kMaxFileNameLen + 1),
                                        kMaxFileNameLen,
                                        fileCount,
                                        listDataName);
    if (fileCount == 0 && pathPrefix == "/SynthSamples/")
    {
        fileCount = kMaxFilesListLength;
        platformFileSystem->listDirContents(("/Samples/" + path).c_str(),
                                            ::FileSystem::DirContentType::kFiles,
                                            wrappedMatcher,
                                            fileNamesBuffer,
                                            kMaxFilesListLength * (kMaxFileNameLen + 1),
                                            kMaxFileNameLen,
                                            fileCount,
                                            listDataName);
    }

    std::vector<std::string_view> fileList;

    std::ranges::transform(std::span(listDataName, fileCount),
                           std::back_inserter(fileList),
                           [](char* file) { return std::string_view(file); });

    return fileList;
}

std::optional<polysynth::FileSystem::FileHandle> FileSystemAdapter::open(
  const std::string& path,
  polysynth::FileSystem::OpenOptions options)
{
    const auto mode =
      (static_cast<int>(options) & static_cast<int>(FileSystem::OpenOptions::kWrite)) != 0
        ? ::FileSystem::FileOpenMode::kWriteCreateNew
        : ::FileSystem::FileOpenMode::kRead;
    if (!path.empty() && path[0] == '/')
    {
        auto [absResult, absFile] = platformFileSystem->openFile(path.c_str(), mode);
        if (absResult)
        {
            return std::optional<polysynth::FileSystem::FileHandle>(absFile);
        }
    }
    auto [result, file] = platformFileSystem->openFile((pathPrefix + path).c_str(), mode);
    if (!result && mode == ::FileSystem::FileOpenMode::kRead &&
        (pathPrefix == "/Samples/" || pathPrefix == "/SynthSamples/"))
    {
        std::tie(result, file) = platformFileSystem->openFile(("/Samples/" + path).c_str(), mode);
        if (!result)
        {
            std::tie(result, file) = platformFileSystem->openFile(("/" + path).c_str(), mode);
        }
        if (!result)
        {
            std::tie(result, file) = platformFileSystem->openFile(("/Patches/" + path).c_str(), mode);
        }
    }

    return result ? std::optional<polysynth::FileSystem::FileHandle>(file) : std::nullopt;
}

void FileSystemAdapter::close(FileHandle file)
{
    platformFileSystem->closeFile(file);
}

bool FileSystemAdapter::write(FileHandle file, std::span<const uint8_t> data)
{
    return platformFileSystem->writeFile(data.data(), data.size(), file) ==
           static_cast<int32_t>(data.size());
}

int32_t FileSystemAdapter::read(FileHandle file, std::span<uint8_t> buf)
{
    return platformFileSystem->readFile(buf.data(), buf.size(), file);
}

bool FileSystemAdapter::seek(FileHandle file, int offset)
{
    return platformFileSystem->seekFileCur(offset, file);
}

void FileSystemAdapter::createFolder(const std::string& path)
{
    std::string fullPath = pathPrefix + path;
    while (fullPath.size() > 1 && fullPath.back() == '/')
    {
        fullPath.pop_back();
    }
    if (!platformFileSystem->exists(fullPath.c_str()))
    {
        platformFileSystem->mkdir(0, fullPath.c_str());
    }
}
#endif
// NOLINTEND
