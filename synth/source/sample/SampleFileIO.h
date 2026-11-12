#pragma once

#include "PolySynth/FileSystem.h"
#include "PolySynth/Synth.h"
#include "config.h"

#include <functional>
#include <span>

struct WaveFileHeader;
struct WaveSubchunkHeader;

namespace polysynth
{

class SampleFileIO
{
  public:
    constexpr static std::string_view kWavExtension = "wav";

    enum class OperationResult
    {
        kNoError,
        kErrorNotEnoughMemory,
        kErrorUnspecifed,
    };

    SampleFileIO(std::shared_ptr<FileSystem> fileSystem, const Synth::Allocator& allocator);

    OperationResult load(const std::string& filePath,
                         std::span<sample_store_t> buf,
                         int& outSampleRate,
                         int& outSampleCount,
                         const std::function<void(float)>& progressCallback);
    tiny_optional<int> getSampleCount(const std::string& filePath);
    OperationResult save(const std::string& filePath,
                         std::span<sample_store_t> samples,
                         int sampleRate,
                         const std::function<void(float)>& progressCallback);

  private:
    static constexpr size_t kSdBuffSize = 36864;

    std::shared_ptr<FileSystem> fileSystem;
    std::span<uint8_t, kSdBuffSize> sdOperationBuff;
};

}
