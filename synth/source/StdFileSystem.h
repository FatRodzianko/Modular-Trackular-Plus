#pragma once

#include "PolySynth/FileSystem.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace polysynth
{

/**
 * Simple file system implementation that uses std::filesystem
 */
class StdFileSystem : public FileSystem
{

  public:
    explicit StdFileSystem(std::string pathPrefix_)
      : pathPrefix(std::move(pathPrefix_)){};

    std::vector<std::string_view> listFiles(const std::string& path,
                                            std::function<bool(const ::std::string& name)> matcher,
                                            tiny_optional<int> maxFiles) override;

    std::optional<FileHandle> open(const std::string& path, OpenOptions options) override;
    void close(FileHandle file) override;
    bool write(FileHandle file, std::span<const uint8_t> data) override;
    int32_t read(FileHandle file, std::span<uint8_t> buf) override;
    bool seek(FileHandle file, int deltaPos) override;
    void createFolder(const std::string& path) override;

  private:
    std::string pathPrefix;
    std::vector<std::string> namesList;
};
} // namespace polysynth