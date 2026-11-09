#include "config.h"
#ifdef FEATURE_SYNTH

#include "PolySynth/sdk/InstrumentTypeRegistry.h"
#include "engines/ChiptuneSynthVoice.h"
#include "modsBuild.h"
#if MT_HAS_SYNTHS // MT: WAVE and PHASE (core/mods/synths)
#include "synths/synthsEngines.h"
#include "synths/synthsMod.h"
#endif

namespace polysynth
{
// Factory synth engines (provided by precompiled libPolySynth.a + libPolyDSP.a)
extern std::unique_ptr<InstrumentType> createMidiOut();
extern std::unique_ptr<InstrumentType> createSimpleAnalogSynth();
extern std::unique_ptr<InstrumentType> createFatAnalogSynth();
extern std::unique_ptr<InstrumentType> createVASynth();
extern std::unique_ptr<InstrumentType> createFMSynth();
extern std::unique_ptr<InstrumentType> createDrumSynth();
extern std::unique_ptr<InstrumentType> createShapingSynth();

void InstrumentTypeRegistry::registerInstrumentTypes(
  InstrumentTypeRegistry& registry,
  [[maybe_unused]] const Synth::Allocator& largeBuffersAlloc)
{
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kMidiOut), createMidiOut());
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kSimpleAnalogSynth),
                               createSimpleAnalogSynth());
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kFatAnalogSynth),
                               createFatAnalogSynth());
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kVASynth), createVASynth());
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kFMSynth), createFMSynth());
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kDrumSynth), createDrumSynth());
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kShapingSynth),
                               createShapingSynth());

    // Open-Source Custom Synth Engines
    registry.addInstrumentType(static_cast<int>(InstrumentTypeId::kChiptuneSynth),
                               createChiptuneSynth());
#if MT_HAS_SYNTHS // MT: WAVE and PHASE, registered with the mod Off too, so their parts and patches open
    registry.addInstrumentType(mods::synths::kWaveTypeId, createWaveSynth());
    registry.addInstrumentType(mods::synths::kPhaseTypeId, createPhaseSynth());
#endif
}

} // namespace polysynth

#endif
