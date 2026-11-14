#pragma once

#include "PolySynth/FileSystem.h"
#include "PolySynth/Synth.h"
#include "instrument/InstrumentTypeRegistry.h"

namespace polysynth
{

class PsynLoader
{
  public:
    static constexpr size_t kArenaSize = 3 * 640 * 1024; // 1,920 KB in external SDRAM
    static constexpr size_t kMaxPreloadBytes = 1024 * 1024; // Preload up to 1 MB of .psyn images at boot

    /**
     * Scan /Synths/<name>.psyn via synthsFs, register all discovered synth engines in
     * registry and the dynamic engine descriptor table, and preload their code images
     * into the external SDRAM arena.
     */
    static void scanAndRegister(FileSystem& synthsFs,
                                InstrumentTypeRegistry& registry,
                                const Synth::Allocator& largeBuffersAlloc,
                                bool isWaveDevice);

    /**
     * Bind the samples FileSystem (/Samples or /SynthSamples) and ensure sample folders
     * exist on the SD card for all sample-based synth engines.
     */
    static void setSamplesFileSystem(FileSystem& samplesFs);

    /**
     * Create the built-in silent fallback synth ("NONE") used only when /Synths/ has no .psyn files.
     */
    static std::unique_ptr<InstrumentType> createFallbackSynth();
};

} // namespace polysynth
