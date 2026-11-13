#pragma once

#include "PolySynth/Synth.h"
#include "PolySynth/sdk/InstrumentType.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>

namespace polysynth
{

struct SynthEngineDescriptor
{
    int typeId = 0;
    const char* codeName = nullptr;    // e.g. "CHIP"
    const char* displayName = nullptr; // e.g. "Chiptune"
    const char* patchFolder = nullptr; // e.g. "CHIP"
    std::uint8_t defaultVoices = 1;
    std::uint8_t maxVoices = 8;
    std::unique_ptr<InstrumentType> (*createInstrumentType)() = nullptr;
};

bool registerSynthEngine(const SynthEngineDescriptor& desc);
std::size_t getRegisteredSynthEngineCount();
const SynthEngineDescriptor* getRegisteredSynthEngine(std::size_t index);
const SynthEngineDescriptor* findRegisteredSynthEngine(int typeId);
void registerCustomEngines();

/**
 * Registry of InstrumentType instances accessed by id.
 *
 * The registry is configured in synth/source/synths/registry.cpp.
 */
class InstrumentTypeRegistry
{
  public:
    [[nodiscard]] std::shared_ptr<const InstrumentType> getInstrumentType(int typeId) const;
    void addInstrumentType(int typeId, std::unique_ptr<InstrumentType> instrumentType);
    void removeInstrumentType(int typeId);
    [[nodiscard]] int getFallbackSynthTypeId() const;

    static void registerInstrumentTypes(InstrumentTypeRegistry& registry,
                                        const Synth::Allocator& largeBuffersAlloc);

    const std::map<int, std::shared_ptr<InstrumentType>>& getInstrumentTypes() const
    {
        return instrumentTypes;
    }

  private:
    std::map<int, std::shared_ptr<InstrumentType>> instrumentTypes;
};

} // namespace polysynth
