#include "instrument/InstrumentTypeRegistry.h"
#include "synths/PsynLoader.h"

#include <cstring>
#include <vector>

namespace polysynth
{
extern std::unique_ptr<InstrumentType> createReverb(const Synth::Allocator& largeBuffersAlloc);
extern std::unique_ptr<InstrumentType> createDelay(const Synth::Allocator& largeBuffersAlloc);
extern std::unique_ptr<InstrumentType> createMidiOut();
extern std::unique_ptr<InstrumentType> createModulationFx(
  const Synth::Allocator& largeBuffersAlloc);

namespace
{
std::vector<SynthEngineDescriptor> gDynamicEngines;
} // namespace

bool registerSynthEngine(const SynthEngineDescriptor& desc)
{
    if (desc.typeId <= 0 || desc.codeName == nullptr)
    {
        return false;
    }

    for (auto& existing : gDynamicEngines)
    {
        if (existing.typeId == desc.typeId)
        {
            existing = desc;
            return true;
        }
    }

    gDynamicEngines.push_back(desc);
    return true;
}

std::size_t getRegisteredSynthEngineCount()
{
    return gDynamicEngines.size();
}

const SynthEngineDescriptor* getRegisteredSynthEngine(std::size_t index)
{
    if (index >= gDynamicEngines.size())
    {
        return nullptr;
    }
    return &gDynamicEngines[index];
}

const SynthEngineDescriptor* findRegisteredSynthEngine(int typeId)
{
    for (const auto& existing : gDynamicEngines)
    {
        if (existing.typeId == typeId)
        {
            return &existing;
        }
    }
    return nullptr;
}

__attribute__((weak)) void registerCustomEngines()
{
    // Open-source / custom C++ engines can be registered here via registerSynthEngine(...)
}

void InstrumentTypeRegistry::registerInstrumentTypes(
  InstrumentTypeRegistry& registry,
  [[maybe_unused]] const Synth::Allocator& largeBuffersAlloc)
{
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kMidiOut), createMidiOut());
    registry.addInstrumentType(1, PsynLoader::createFallbackSynth());
#ifndef UNIT_TESTS
#if !defined(PRODUCT_PLAY_PLUS) && !defined(PRODUCT_TRACKER_PLUS)
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kDelay),
                               createDelay(largeBuffersAlloc));
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kReverb),
                               createReverb(largeBuffersAlloc));
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kModulationFx),
                               createModulationFx(largeBuffersAlloc));
#endif

    registerCustomEngines();
    for (const auto& eng : gDynamicEngines)
    {
        if (eng.createInstrumentType != nullptr)
        {
            registry.addInstrumentType(eng.typeId, eng.createInstrumentType());
        }
    }
#endif
}
}
