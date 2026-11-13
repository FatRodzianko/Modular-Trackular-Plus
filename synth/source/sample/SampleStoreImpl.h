#pragma once

#include "PolySynth/SampleStore.h"
#include "SampleFileIO.h"

#include <memory>

namespace polysynth
{
class FileSystem;

class SampleStoreImpl : public SampleStore
{
  public:
    SampleStoreImpl(const std::shared_ptr<FileSystem>& fileSystem,
                    const Synth::Allocator& allocator);

    [[nodiscard]] std::optional<Sample> loadSample(const std::string& filePath,
                                                   std::span<sample_store_t> destBuf) override;
    [[nodiscard]] tiny_optional<int> getSampleSize(const std::string& filePath) override;
    [[nodiscard]] std::vector<std::string_view> listSamples(
      const std::string& folder = "") override;

    static tiny_optional<int> getWaveSize(const std::string& filePath);
    static note_t getBasePitch(const std::string& filePath);

  private:
    std::shared_ptr<FileSystem> fileSystem;
    SampleFileIO fileIO;
};
}