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

    fileCount = kMaxFilesListLength;
    platformFileSystem->listDirContents((pathPrefix + path).c_str(),
                                        ::FileSystem::DirContentType::kFiles,
                                        matcher,
                                        fileNamesBuffer,
                                        kMaxFilesListLength * (kMaxFileNameLen + 1),
                                        kMaxFileNameLen,
                                        fileCount,
                                        listDataName);

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
    auto [result, file] = platformFileSystem->openFile(
      (pathPrefix + path).c_str(),
      (static_cast<int>(options) & static_cast<int>(FileSystem::OpenOptions::kWrite)) != 0
        ? ::FileSystem::FileOpenMode::kWriteCreateNew
        : ::FileSystem::FileOpenMode::kRead);

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
    platformFileSystem->mkdir(0, (pathPrefix + path).c_str());
}
#endif
// NOLINTEND
