#include "SampleFileIO.h"

#include "wav.h"

#include "PolyUtil/ScopeExit.h"

#include <cassert>
#include <limits>

namespace polysynth
{
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)

class WaveHeaderReader
{
  public:
    static bool readHeader(FileSystem& fileSystem,
                           FileSystem::FileHandle file,
                           WaveFileHeader& header);
    static bool isSampleValid(const WaveFileHeader& header);

    // convert data from wav file to mono 16bit
    static void convertWavFileData(std::span<sample_store_t> destBuffer,
                                   std::span<uint8_t> srcBuffer,
                                   WaveFileHeader& srcHeader);

  private:
    static bool searchForChunk(FileSystem& fileSystem,
                               FileSystem::FileHandle file,
                               uint32_t chunkId,
                               WaveSubchunkHeader& subchunkHeader);
};

SampleFileIO::SampleFileIO(std::shared_ptr<FileSystem> fileSystem_,
                           const Synth::Allocator& allocator)
  : fileSystem(std::move(fileSystem_))
  , sdOperationBuff(static_cast<uint8_t*>(allocator(kSdBuffSize)), kSdBuffSize)
{
    assert(sdOperationBuff.data());
}

SampleFileIO::OperationResult SampleFileIO::load(const std::string& filePath,
                                                 std::span<sample_store_t> buf,
                                                 int& outSampleRate,
                                                 int& outSampleCount,
                                                 const std::function<void(float)>& progressCallback)
{
    assert(progressCallback);
    WaveFileHeader header = {};

    auto file = fileSystem->open(filePath, FileSystem::OpenOptions::kNone);
    if (!file)
    {
        return OperationResult::kErrorUnspecifed;
    }

    ScopeExit closeFile([this, &file]() { fileSystem->close(*file); });

    if (!WaveHeaderReader::readHeader(*fileSystem, *file, header))
    {
        return OperationResult::kErrorUnspecifed;
    }

    if (!WaveHeaderReader::isSampleValid(header))
    {
        return OperationResult::kErrorUnspecifed;
    }

    outSampleRate = static_cast<int>(header.sampleRate);
    int bytesPerSample =
      (header.bitsPerSample / std::numeric_limits<uint8_t>::digits) * header.numChannels;
    outSampleCount = static_cast<int>(header.dataSubchunkSize / bytesPerSample);

    if (std::ssize(buf) < outSampleCount)
    {
        return OperationResult::kErrorNotEnoughMemory;
    }

    sample_store_t* destBuf = buf.data();

    int sdBlockSize = kSdBuffSize;
    int kBlockSamples = sdBlockSize / bytesPerSample;
    int samplesToLoad = outSampleCount;
    int sdBlocks = samplesToLoad % kBlockSamples == 0 ? samplesToLoad / kBlockSamples
                                                      : (samplesToLoad / kBlockSamples) + 1;
    constexpr int kBlockCountPerCallback = 100000 / kSdBuffSize + 1;
    int blockCnt = 0;
    bool correctFormat =
      (header.audioFormat == WavAudioFormat::kWavFormatPCM &&
       header.bitsPerSample == std::numeric_limits<sample_store_t>::digits + 1 && // sign bit
       header.numChannels == 1);

    for (int block = 0; block < sdBlocks; block++)
    {
        int blockSamples = std::min(samplesToLoad, kBlockSamples);
        sdBlockSize = blockSamples * bytesPerSample;

        if (correctFormat)
        {
            // skip conversion if format is same as used internal by the audio engine
            fileSystem->read(
              *file,
              // NOLINTNEXTLINE(*)
              std::span(reinterpret_cast<uint8_t*>(&destBuf[block * kBlockSamples]), sdBlockSize));
        }
        else
        {
            fileSystem->read(*file, sdOperationBuff);
            WaveHeaderReader::convertWavFileData(
              // NOLINTNEXTLINE(*)
              std::span(&destBuf[block * kBlockSamples], blockSamples),
              sdOperationBuff,
              header);
        }

        samplesToLoad -= blockSamples;

        blockCnt++;
        if (blockCnt > kBlockCountPerCallback)
        {
            blockCnt = 0;
            progressCallback(static_cast<float>(block) / static_cast<float>(sdBlocks));
        }
    }

    if (sdBlocks < kBlockCountPerCallback)
    {
        progressCallback(1.0);
    }

    assert(samplesToLoad == 0);

    return OperationResult::kNoError;
}

SampleFileIO::OperationResult SampleFileIO::save(const std::string& filePath,
                                                 std::span<sample_store_t> samples,
                                                 int sampleRate,
                                                 const std::function<void(float)>& progressCallback)
{
    assert(progressCallback);

    auto file = fileSystem->open(filePath, FileSystem::OpenOptions::kWrite);
    if (!file)
    {
        return OperationResult::kErrorUnspecifed;
    }

    ScopeExit closeFile([this, &file]() { fileSystem->close(*file); });

    WaveFileHeader header = {
        .chunkId = kWavFileSubchunkID_RIFF,
        .chunkSize =
          static_cast<uint32_t>(samples.size() * sizeof(sample_store_t) + sizeof(WaveFileHeader)),
        .waveId = kWaveId,
        .fmtSubchunkId = kWavFileSubchunkID_fmt,
        .fmtSubchunkSize = 16, // NOLINT(*-magic-numbers)
        .audioFormat = kWavFormatPCM,
        .numChannels = 1,
        .sampleRate = static_cast<uint32_t>(sampleRate),
        .byteRate = static_cast<uint32_t>(sizeof(sample_store_t) * sampleRate),
        .blockAlign = sizeof(sample_store_t),
        .bitsPerSample = sizeof(sample_store_t) * std::numeric_limits<uint8_t>::digits,
        .dataSubchunkId = kWavFileSubchunkID_data,
        .dataSubchunkSize = static_cast<uint32_t>(samples.size() * sizeof(sample_store_t))
    };

    auto result =
      fileSystem->write(*file, std::span(reinterpret_cast<uint8_t*>(&header), sizeof(header)));

    if (!result)
    {
        return OperationResult::kErrorUnspecifed;
    }

    constexpr int kBlockSize = 1024 * 100;
    int totalSizeToWrite = static_cast<int>(header.dataSubchunkSize);
    int totalWritten = 0;

    while (totalWritten < totalSizeToWrite)
    {
        int bytesLeft = totalSizeToWrite - totalWritten;
        int writeSize = (kBlockSize < bytesLeft) ? kBlockSize : bytesLeft;
        result = fileSystem->write(
          *file,
          // NOLINTNEXTLINE(*)
          std::span(reinterpret_cast<uint8_t*>(samples.data()) + totalWritten, writeSize));
        if (!result)
        {
            return OperationResult::kErrorUnspecifed;
        }
        totalWritten += writeSize;

        progressCallback(static_cast<float>(totalWritten) / static_cast<float>(totalSizeToWrite));
    }

    return totalWritten == static_cast<int>(header.dataSubchunkSize)
             ? OperationResult::kNoError
             : OperationResult::kErrorUnspecifed;
}

tiny_optional<int> SampleFileIO::getSampleCount(const std::string& filePath)
{
    auto file = fileSystem->open(filePath, FileSystem::OpenOptions::kNone);
    if (!file)
    {
        return std::nullopt;
    }

    ScopeExit closeFile([this, &file]() { fileSystem->close(*file); });

    WaveFileHeader header; // NOLINT(*-member-init)

    if (!WaveHeaderReader::readHeader(*fileSystem, *file, header))
    {
        return std::nullopt;
    }

    if (!WaveHeaderReader::isSampleValid(header))
    {
        return std::nullopt;
    }

    int bytePerSample =
      (header.bitsPerSample / std::numeric_limits<uint8_t>::digits) * header.numChannels;
    auto sampleCount = header.dataSubchunkSize / bytePerSample;

    return sampleCount;
}

bool WaveHeaderReader::searchForChunk(FileSystem& fileSystem,
                                      FileSystem::FileHandle file,
                                      uint32_t chunkId,
                                      WaveSubchunkHeader& subchunkHeader)
{
    static constexpr int kMaxSearchTries = 10;

    int subchunkTry = 0;
    int nextChunkOffset = 0;
    while (subchunkTry < kMaxSearchTries)
    {
        size_t bytesRead = fileSystem.read(
          file, std::span(reinterpret_cast<uint8_t*>(&subchunkHeader), sizeof(subchunkHeader)));
        if (bytesRead != sizeof(subchunkHeader))
        {
            return false;
        }

        subchunkTry++;

        if (subchunkHeader.id == chunkId)
        {
            return true;
        }

        nextChunkOffset = static_cast<int>(subchunkHeader.size);

        if (nextChunkOffset % 2 == 1)
        {
            nextChunkOffset += 1;
        }
        if (!fileSystem.seek(file, nextChunkOffset))
        {
            return false;
        }
    }

    return (subchunkTry < kMaxSearchTries);
}

bool WaveHeaderReader::readHeader(FileSystem& fileSystem,
                                  FileSystem::FileHandle file,
                                  WaveFileHeader& header)
{
    static constexpr int kWavFileFmtChunkSizeToRead = 16;

    WaveSubchunkHeader subchunkHeader; // NOLINT(*-member-init)

    size_t bytesRead = fileSystem.read(
      file, std::span(reinterpret_cast<uint8_t*>(&subchunkHeader), sizeof(subchunkHeader)));
    if (bytesRead != sizeof(subchunkHeader))
    {
        return false;
    }

    if (subchunkHeader.id != kWavFileSubchunkID_RIFF)
    {
        return false;
    }

    header.chunkId = subchunkHeader.id;
    header.chunkSize = subchunkHeader.size;
    bytesRead = fileSystem.read(
      file, std::span(reinterpret_cast<uint8_t*>(&header.waveId), sizeof(header.waveId)));
    if (bytesRead != sizeof(header.waveId))
    {
        return false;
    }

    if (searchForChunk(fileSystem, file, kWavFileSubchunkID_fmt, subchunkHeader))
    {
        header.fmtSubchunkId = subchunkHeader.id;
        header.fmtSubchunkSize = subchunkHeader.size;
        fileSystem.read(
          file,
          std::span(reinterpret_cast<uint8_t*>(&header.audioFormat), kWavFileFmtChunkSizeToRead));

        fileSystem.seek(file, static_cast<int>(subchunkHeader.size) - kWavFileFmtChunkSizeToRead);
    }
    else
    {
        return false;
    }

    if (searchForChunk(fileSystem, file, kWavFileSubchunkID_data, subchunkHeader))
    {
        header.dataSubchunkId = subchunkHeader.id;
        header.dataSubchunkSize = subchunkHeader.size;
    }
    else
    {
        return false;
    }
    return true;
}

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTBEGIN(*-magic-numbers)
// NOLINTBEGIN(*-pro-type-cstyle-cast, *-readability-casting) easier to read in this case
bool WaveHeaderReader::isSampleValid(const WaveFileHeader& header)
{
    bool waveIdValid = header.waveId == kWaveId;

    bool audioFormatValid = header.audioFormat == WavAudioFormat::kWavFormatFloat ||
                            header.audioFormat == WavAudioFormat::kWavFormatPCM;

    bool bitsPerSampleValid = header.bitsPerSample == 8 || header.bitsPerSample == 16 ||
                              header.bitsPerSample == 24 || header.bitsPerSample == 32;

    bool numChannelsValid = header.numChannels == 1 || header.numChannels == 2;

    return waveIdValid && audioFormatValid && bitsPerSampleValid && numChannelsValid;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void WaveHeaderReader::convertWavFileData(std::span<sample_store_t> destBuffer,
                                          std::span<uint8_t> srcBuffer,
                                          WaveFileHeader& srcHeader)
{
    static_assert(sizeof(sample_store_t) == 2);

    int bytesPerSample = (srcHeader.bitsPerSample / 8) * srcHeader.numChannels;

    // auto destBufferIt = destBuffer.begin();
    auto srcBufferIt = srcBuffer.begin();

    switch (srcHeader.numChannels)
    {
        case 1:
        {
            switch (srcHeader.audioFormat)
            {
                case WavAudioFormat::kWavFormatFloat:
                {
                    for (auto& destSample : destBuffer)
                    {
                        assert(srcBufferIt < srcBuffer.end());
                        const float sampleF = std::clamp(*((float*)&*srcBufferIt), -1.0f, 1.0f);
                        destSample = static_cast<sample_store_t>(
                          sampleF * std::numeric_limits<sample_store_t>::max());
                        srcBufferIt += bytesPerSample;
                    }
                }
                break;
                case WavAudioFormat::kWavFormatPCM:
                {
                    switch (srcHeader.bitsPerSample)
                    {
                        case 8:
                            for (auto& destSample : destBuffer)
                            {
                                assert(srcBufferIt < srcBuffer.end());
                                destSample = static_cast<sample_store_t>(
                                  ((sample_store_t)(*srcBufferIt) - 128) << 8);
                                srcBufferIt += bytesPerSample;
                            }
                            break;
                        case 16:
                            for (auto& destSample : destBuffer)
                            {
                                assert(srcBufferIt < srcBuffer.end());
                                destSample = *((sample_store_t*)&*srcBufferIt);
                                srcBufferIt += bytesPerSample;
                            }
                            break;
                        case 24:
                            for (auto& destSample : destBuffer)
                            {
                                assert(srcBufferIt < srcBuffer.end());
                                destSample = *((sample_store_t*)(&*srcBufferIt + 1));
                                srcBufferIt += bytesPerSample;
                            }
                            break;
                        case 32:
                            for (auto& destSample : destBuffer)
                            {
                                assert(srcBufferIt < srcBuffer.end());
                                destSample = *((sample_store_t*)(&*srcBufferIt + 2));
                                srcBufferIt += bytesPerSample;
                            }
                            break;
                        default:
                            assert(false);
                            break;
                    }
                }
                break;
                default:
                    assert(false);
                    break;
            }
        }
        break;
        case 2:
        {
            int bytesPerChannel = bytesPerSample / 2;

            switch (srcHeader.audioFormat)
            {
                case WavAudioFormat::kWavFormatFloat:
                {
                    for (auto& destSample : destBuffer)
                    {
                        assert(srcBufferIt < srcBuffer.end());
                        const float mixedF = std::clamp(
                          0.5f * (*((float*)&*srcBufferIt) +
                                  *((float*)(&*srcBufferIt + bytesPerChannel))),
                          -1.0f,
                          1.0f);
                        destSample = static_cast<sample_store_t>(
                          mixedF * std::numeric_limits<sample_store_t>::max());

                        srcBufferIt += bytesPerSample;
                    }
                }
                break;
                case WavAudioFormat::kWavFormatPCM:
                {
                    switch (srcHeader.bitsPerSample)
                    {
                        case 8:
                            for (auto& destSample : destBuffer)
                            {
                                assert(srcBufferIt < srcBuffer.end());
                                sample_store_long_t input =
                                  ((sample_store_t)((sample_store_t)(*srcBufferIt) - 128) +
                                   static_cast<sample_store_t>(
                                     (sample_store_t)(*(&*srcBufferIt + bytesPerChannel)) - 128))
                                  << 7;
                                destSample =
                                  static_cast<sample_store_t>(std::clamp<sample_store_long_t>(
                                    input,
                                    std::numeric_limits<sample_store_t>::min(),
                                    std::numeric_limits<sample_store_t>::max()));

                                srcBufferIt += bytesPerSample;
                            }
                            break;
                        case 16:
                            for (auto& destSample : destBuffer)
                            {
                                assert(srcBufferIt < srcBuffer.end());
                                sample_store_long_t input =
                                  (*((sample_store_t*)&*srcBufferIt) +
                                   *((sample_store_t*)(&*srcBufferIt + bytesPerChannel))) >>
                                  1;
                                destSample =
                                  static_cast<sample_store_t>(std::clamp<sample_store_long_t>(
                                    input,
                                    std::numeric_limits<sample_store_t>::min(),
                                    std::numeric_limits<sample_store_t>::max()));

                                srcBufferIt += bytesPerSample;
                            }
                            break;
                        case 24:
                            for (auto& destSample : destBuffer)
                            {
                                assert(srcBufferIt < srcBuffer.end());
                                sample_store_long_t input =
                                  (*((sample_store_t*)(&*srcBufferIt + 1)) +
                                   *((sample_store_t*)(&*srcBufferIt + 1 + bytesPerChannel))) >>
                                  1;
                                destSample =
                                  static_cast<sample_store_t>(std::clamp<sample_store_long_t>(
                                    input,
                                    std::numeric_limits<sample_store_t>::min(),
                                    std::numeric_limits<sample_store_t>::max()));

                                srcBufferIt += bytesPerSample;
                            }
                            break;
                        case 32:
                            for (auto& destSample : destBuffer)
                            {
                                assert(srcBufferIt < srcBuffer.end());
                                sample_store_long_t input =
                                  (*((sample_store_t*)(&*srcBufferIt + 2)) +
                                   *((sample_store_t*)(&*srcBufferIt + 2 + bytesPerChannel))) >>
                                  1;
                                destSample =
                                  static_cast<sample_store_t>(std::clamp<sample_store_long_t>(
                                    input,
                                    std::numeric_limits<sample_store_t>::min(),
                                    std::numeric_limits<sample_store_t>::max()));

                                srcBufferIt += bytesPerSample;
                            }
                            break;
                        default:
                            assert(false);
                            break;
                    }
                }
                break;
                default:
                    assert(false);
                    break;
            }
        }
        break;
        default:
            assert(false);
            break;
    }
}
// NOLINTEND(*-pro-type-cstyle-cast, *-readability-casting)
// NOLINTEND(*-magic-numbers)
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

}
