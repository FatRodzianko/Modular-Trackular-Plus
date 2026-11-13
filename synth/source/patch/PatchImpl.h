#pragma once

#include "PolySynth/Patch.h"
#include "PolySynth/config.h"
#include "PolyUtil/bitset.h"
#include "instrument/InstrumentType.h"

#include "Patch.pb.h"

#include "ProtobufHelpers.h"

#include <memory>
#include <string>
#include <vector>

namespace polysynth
{
class SamplePool;

class PatchImpl final : public Patch
{
  public:
    PatchImpl(std::shared_ptr<const InstrumentType> instrumentType,
              std::shared_ptr<SamplePool> samplePool);
    ~PatchImpl() override;

    [[nodiscard]] optional_paramvalue_t getParamValue(int paramId) const override;
    void setParamValue(int paramId, optional_paramvalue_t value, ParamChangeSource source) override;

    [[nodiscard]] tiny_optional<float> getParamValueEditMapped(int paramId) const override;
    void setParamValueEditMapped(int paramId,
                                 tiny_optional<float> value,
                                 ParamChangeSource source) override;

    void subscribeParamUpdates(std::weak_ptr<ParamChangeListener> listener) override;
    void unsubscribeParamUpdates(const ParamChangeListener& listener) override;

    void startBatchUpdate() override;
    void finishBatchUpdate() override;

    [[nodiscard]] optional_paramvalue_t getMacroValue(int macroId) const override;
    void setMacroValue(int macroId,
                       optional_paramvalue_t value,
                       tiny_optional<int> voiceId) override;
    void setMacroModifier(int macroId, int modifierIdx, tiny_optional<int> paramId) override;
    void setMacroModifierAmount(int macroId, int modifierIdx, float amount) override;
    [[nodiscard]] tiny_optional<float> getMacroModifierAmount(int macroId,
                                                              int modifierIdx) override;
    void changeMacroModifierAmount(int macroId, int modifierIdx, paramvalue_t value) override;
    void clearMacroModifiers() override;
    void setMacroBipolar(int macroId, bool bipolar) override;
    void setMacroDisplayName(int macroId, std::string name) override;
    [[nodiscard]] Patch::optional_paramvalue_t getParamFinalValue(int paramId) const override;

    [[nodiscard]] const Sample* getSample(int sampleSlotId) const override;
    [[nodiscard]] LoadStatus loadSample(int sampleSlotId, const std::string& filePath) override;

    [[nodiscard]] const Metadata& getMetadata() const override;
    [[nodiscard]] const Sequencer& getSequencer() const override;

    void setSequence(const Sequencer::Sequence& steps, int idx) override;
    void setSequencerConfig(const Sequencer::Config& config) override;

    static bool serialize(const Patch& patch, ProtobufHelpers::WriterFunc writer);
    static void serialize(const Patch& patch,
                          std::vector<polysynth_Param>& params,
                          polysynth_Patch& patchData);
    static bool deserialize(Patch& patch,
                            ProtobufHelpers::ReaderFunc reader,
                            bool synchronizeAudio);
    static bool deserialize(Patch& patch,
                            const polysynth_Patch& patchData,
                            bool loadSampleSlots = true);
    static std::optional<InstrumentTypeId> deserializeInstrumentType(
      ProtobufHelpers::ReaderFunc reader);

  private:
    static constexpr auto kExpCurveBase = 10.0f;
    static constexpr auto kExpSteepCurveMult = 3.0f;
    static constexpr auto kPowerCurveCoeff = 2.5f;
    static constexpr auto kMacroDeltaThreshold = 0.1f;

#if defined(UNIT_TESTS) || defined(REGRESSION_TESTS) || defined(__clang__)
    using optional_macrovalue_short_t = std::optional<paramvalue_short_t>;
#else
    using optional_macrovalue_short_t = tiny::optional<paramvalue_short_t, 32768>;
#endif

    std::shared_ptr<const InstrumentType> instrumentType;
    std::vector<optional_paramvalue_t> paramValues;
    std::vector<std::weak_ptr<ParamChangeListener>> paramListeners;

    // hold per-voice values, if the macro is not per-voice then only idx 0 is valid
    std::vector<std::array<optional_macrovalue_short_t, kMaxInstrumentVoices>> macroValues;
    std::vector<float> macroParamValues;
    std::vector<std::array<float, kMaxInstrumentVoices>> macroParamValuesPerVoice;

    /// Maps each param id to a set of macro ids represented as bits in a bitset
    std::vector<bitset::tiny_bitset8> paramMacroLookup;

    Metadata metadata;

    std::shared_ptr<SamplePool> samplePool;
    std::vector<tiny_optional<int>> samplePoolIds;
    std::vector<std::string> sampleFilePaths;

#ifdef FEATURE_SEQUENCER
    Sequencer sequencer;
#endif
    std::function<void()> seqConfigChanged;

    bool inBatchUpdate = false;

    void updateParamSubscribers(int paramId, ParamChangeSource source, tiny_optional<int> voiceId);

    void updateMetadata();
    void updateMacroValue(int macroId, tiny_optional<int> voiceId);
    void updateMacroParamValues(int paramId, tiny_optional<int> voiceId = std::nullopt);
    void updateMacroParamValue(int macroId, int paramId, tiny_optional<int> voiceId);
    [[nodiscard]] Patch::optional_paramvalue_t getParamFinalValue(int paramId, int voiceId) const;
    void setOrAddMacroModifier(int macroId, int modifierIdx, int paramId);
    void deleteMacroModifier(int macroId, int modifierIdx);
    void notifyListenersPatchReset();
    void unloadAllSamples();
    static void loadSamples(Patch& patch,
                            const polysynth_Patch& patchData,
                            ProtobufHelpers::ReaderFunc* embeddedReader);

    static void testMacroValueOptional();
};
}
