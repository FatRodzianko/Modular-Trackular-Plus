#pragma once

#include <cstdint>

enum WavAudioFormat : uint16_t
{
    kWavFormatPCM = 1,
    kWavFormatFloat = 3
};

static constexpr uint32_t kWaveId = 0x45564157; // "WAVE"
static constexpr uint32_t kWavFileSubchunkID_data = 0x61746164;
static constexpr uint32_t kWavFileSubchunkID_fmt = 0x20746d66; //"fmt "
static constexpr uint32_t kWavFileSubchunkID_RIFF = 0x46464952;

struct WaveFileHeader
{
    uint32_t chunkId;
    uint32_t chunkSize;
    uint32_t waveId;

    uint32_t fmtSubchunkId;
    uint32_t fmtSubchunkSize;
    WavAudioFormat audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;

    uint32_t dataSubchunkId;
    uint32_t dataSubchunkSize;
};

struct WaveSubchunkHeader
{
    uint32_t id;
    uint32_t size;
};
