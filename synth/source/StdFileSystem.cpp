#if defined(UNIT_TESTS)

#include "StdFileSystem.h"

#include <filesystem>
#include <iostream>

namespace polysynth
{

std::vector<std::string_view> StdFileSystem::listFiles(
  const std::string& path,
  std::function<bool(const ::std::string& name)> matcher,
  tiny_optional<int> maxFiles)
{
    namesList.clear();
    std::vector<std::string_view> viewsList = {};

    auto fullPath = pathPrefix + "/" + path;
    if (!std::filesystem::exists(fullPath))
    {
        return viewsList;
    }

    for (const auto& entry : std::filesystem::directory_iterator(pathPrefix + "/" + path))
    {
        auto fileName = entry.path().filename();
        if (!matcher(fileName))
        {
            continue;
        }
        namesList.emplace_back(fileName);
        if (maxFiles && --(*maxFiles) < 0)
        {
            break;
        }
    }

    viewsList.reserve(namesList.size());
    for (const auto& name : namesList)
    {
        viewsList.emplace_back(name.c_str(), name.size());
    }

    return viewsList;
}

std::optional<FileSystem::FileHandle> StdFileSystem::open(const std::string& path,
                                                             FileSystem::OpenOptions options)
{
    auto fullPath = pathPrefix + "/" + path;
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto* file = std::fopen(fullPath.c_str(), options == OpenOptions::kWrite ? "wb" : "rb");

    return file != nullptr ? std::optional<FileSystem::FileHandle>(file) : std::nullopt;
}

void StdFileSystem::close(FileHandle file)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::fclose(static_cast<FILE*>(file));
}

bool StdFileSystem::write(FileHandle file, std::span<const uint8_t> data)
{
    return std::fwrite(data.data(), 1, data.size(), static_cast<FILE*>(file)) == data.size();
}

int32_t StdFileSystem::read(FileHandle file, std::span<uint8_t> buf)
{
    return std::fread(buf.data(), 1, buf.size(), static_cast<FILE*>(file));
}

bool StdFileSystem::seek(FileHandle file, int deltaPos)
{
    return std::fseek(static_cast<FILE*>(file), deltaPos, SEEK_CUR) == 0;
}

void StdFileSystem::createFolder(const std::string& /* path */)
{
    assert(false);
}
} // namespace polysynth
#endif