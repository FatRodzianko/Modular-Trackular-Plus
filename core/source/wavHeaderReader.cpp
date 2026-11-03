#include "wavHeaderReader.h"
#include "TrackerImpl.h"

#include <limits.h>
#include <string.h>

namespace
{
constexpr uint32_t kRiffId = 0x46464952;
constexpr uint32_t kFmtId = 0x20746D66;
constexpr uint32_t kDataId = 0x61746164;
constexpr uint32_t kMinimumFmtSize = 16;

uint16_t readLe16(const uint8_t* bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

uint32_t readLe32(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

bool readExact(FileSystem* fileSystem,
               FileSystem::FileHandle fileHandle,
               void* destination,
               size_t size)
{
    int32_t bytesRead = fileSystem->readFile(destination, size, fileHandle);
    return bytesRead >= 0 && static_cast<size_t>(bytesRead) == size;
}

bool seekTo(FileSystem* fileSystem,
            FileSystem::FileHandle fileHandle,
            uint64_t position,
            uint64_t fileSize)
{
    return position <= fileSize && position <= UINT32_MAX &&
           fileSystem->seekFile((uint32_t)position, fileHandle);
}
} // namespace

bool readHeader(strWavFileHeader* header, FileSystem* fileSystem, FileSystem::FileHandle wavfile)
{
    if (header == nullptr)
    {
        return false;
    }

    memset(header, 0, sizeof(*header));

    if (fileSystem == nullptr || wavfile == nullptr)
    {
        return false;
    }

    const uint64_t fileSize = fileSystem->getFileSize(wavfile);
    if (fileSize < 12 || fileSize > UINT32_MAX)
    {
        return false;
    }

    uint8_t riffHeader[12];
    if (!readExact(fileSystem, wavfile, riffHeader, sizeof(riffHeader)))
    {
        return false;
    }

    strWavFileHeader parsedHeader{};
    parsedHeader.chunkId = readLe32(riffHeader);
    parsedHeader.chunkSize = readLe32(riffHeader + 4);
    parsedHeader.waveId = readLe32(riffHeader + 8);

    if (parsedHeader.chunkId != kRiffId || parsedHeader.waveId != WAVEID)
    {
        return false;
    }

    const uint64_t riffEnd = (uint64_t)parsedHeader.chunkSize + 8;
    if (riffEnd < sizeof(riffHeader) || riffEnd > fileSize || riffEnd > UINT32_MAX)
    {
        return false;
    }

    uint64_t position = sizeof(riffHeader);
    bool foundFmt = false;

    while (position < riffEnd)
    {
        if (riffEnd - position < 8)
        {
            return false;
        }

        uint8_t chunkHeader[8];
        if (!readExact(fileSystem, wavfile, chunkHeader, sizeof(chunkHeader)))
        {
            return false;
        }

        position += sizeof(chunkHeader);
        const uint32_t chunkId = readLe32(chunkHeader);
        const uint32_t chunkSize = readLe32(chunkHeader + 4);
        const uint64_t chunkEnd = position + chunkSize;
        const uint64_t paddedChunkEnd = chunkEnd + (chunkSize & 1U);

        if (chunkEnd < position || paddedChunkEnd < chunkEnd || paddedChunkEnd > riffEnd ||
            paddedChunkEnd > fileSize)
        {
            return false;
        }

        if (chunkId == kFmtId && !foundFmt)
        {
            if (chunkSize < kMinimumFmtSize)
            {
                return false;
            }

            uint8_t fmtData[kMinimumFmtSize];
            if (!readExact(fileSystem, wavfile, fmtData, sizeof(fmtData)))
            {
                return false;
            }

            parsedHeader.subchunk1Id = chunkId;
            parsedHeader.subchunk1Size = chunkSize;
            parsedHeader.AudioFormat = readLe16(fmtData);
            parsedHeader.numChannels = readLe16(fmtData + 2);
            parsedHeader.sampleRate = readLe32(fmtData + 4);
            parsedHeader.byteRate = readLe32(fmtData + 8);
            parsedHeader.blockAlign = readLe16(fmtData + 12);
            parsedHeader.bitsPerSample = readLe16(fmtData + 14);
            foundFmt = true;
            position += sizeof(fmtData);
        }
        else if (chunkId == kDataId && foundFmt)
        {
            parsedHeader.subchunk2Id = chunkId;
            parsedHeader.subchunk2Size = chunkSize;
            *header = parsedHeader;
            return true;
        }

        if (position != paddedChunkEnd)
        {
            if (!seekTo(fileSystem, wavfile, paddedChunkEnd, fileSize))
            {
                return false;
            }
            position = paddedChunkEnd;
        }
    }

    return false;
}

bool readHeader(strWavFileHeader* header, FileSystem::FileHandle wavfile)
{
    FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
    return readHeader(header, fileSystem, wavfile);
}
