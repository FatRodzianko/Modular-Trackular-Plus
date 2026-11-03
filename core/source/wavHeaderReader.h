#ifndef SOURCE_WAVHEADERREADER_H_
#define SOURCE_WAVHEADERREADER_H_

#include "Platform.h"
#include <stdint.h>

class SdFile;

enum WavAudioFormat
{
    wavFormatPCM = 1,
    wavFormatFloat = 3
};

#define WAVEID 0x45564157 // "WAVE"

struct strWavFileHeader
{
    uint32_t chunkId; // 0
    uint32_t chunkSize;
    uint32_t waveId;

    uint32_t subchunk1Id; // 12
    uint32_t subchunk1Size;
    uint16_t AudioFormat;   // 20
    uint16_t numChannels;   // 22
    uint32_t sampleRate;    // 24
    uint32_t byteRate;      // 28
    uint16_t blockAlign;    // 32
    uint16_t bitsPerSample; // 34

    uint32_t subchunk2Id;   // 36
    uint32_t subchunk2Size; // 40
};

bool readHeader(strWavFileHeader* header, FileSystem::FileHandle fileHandle);
bool readHeader(strWavFileHeader* header,
                FileSystem* fileSystem,
                FileSystem::FileHandle fileHandle);

#endif /* SOURCE_WAVHEADERREADER_H_ */
