#ifndef PTFX_LOADER_H_
#define PTFX_LOADER_H_

#include "tracker_plugin.h"
#include <stdint.h>

static constexpr uint32_t kPtfxCodeBufferSize        = 128 * 1024; // 128 KB executable code buffer
static constexpr uint32_t kPtfxScratchOffset         = kPtfxCodeBufferSize; // 128 KB (scratch buffer starts right after code buffer)
static constexpr uint32_t kPtfxScratchTopOffset      = 512 * 1024; // 512 KB (384 KB plugin working scratch)
static constexpr uint32_t kPtfxSecondarySampleOffset = 512 * 1024; // [512 KB .. 744 KB] secondary .wav sample buffer
static constexpr uint32_t kPtfxSecondarySampleBytes  = 232 * 1024; // 232 KB (~118k int16_t samples)

class PtfxLoader
{
public:
    static uint8_t* getCodeBuffer();
    static uint32_t getMaxCodeSize();
    static void* getScratchBuffer();
    static uint32_t getMaxScratchSize();
    static const PtfxHostApi* getHostApi();

    static bool loadSecondaryWavFile(const char* filepath);
    static const int16_t* getSecondarySample(uint32_t* outFrames, uint8_t* outChannels);
    static void setSelectedFilePath(uint8_t paramIndex, const char* filepath);
    static const char* getSelectedFilePath(uint8_t paramIndex);
    static void clearSelectedFiles();

    static void syncInstructionCache(void* addr, uint32_t size);

    // Reads 472-byte PtfxHeader from a file on SD card
    static bool readHeader(const char* filepath, PtfxHeader& outHeader);

    // Loads executable binary and header, zeroes .bss, executes cache coherency, returns Thumb entry points
    static bool loadPlugin(const char* filepath,
                           PtfxHeader& outHeader,
                           PtfxInitFunc& outInit,
                           PtfxSetParamFunc& outSetParam,
                           PtfxProcessBlockFunc& outProcessBlock);
};

#endif /* PTFX_LOADER_H_ */
