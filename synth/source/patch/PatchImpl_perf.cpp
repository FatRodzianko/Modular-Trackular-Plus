#include "../sample/SamplePool.h"
#include "PatchImpl.h"

#include "PolyUtil/AudioCS.h"
#include "PolyUtil/ScopeExit.h"
#include "PolyUtil/math.h"
#include "enum_util.h"

/**
 * This implementation file includes all performance-critical methods, i.e. these called from the
 * audio thread.
 */

namespace polysynth
{
Patch::optional_paramvalue_t PatchImpl::getParamValue(int paramId) const
{
    return paramId < std::ssize(paramValues) ? paramValues[paramId] : std::nullopt;
}

void PatchImpl::setParamValue(int paramId, optional_paramvalue_t value, ParamChangeSource source)
{
    assert(paramId < std::ssize(paramValues));

    paramValues[paramId] = value;

    if (metadata.params[paramId].invalidatesOtherMetadata)
    {
        updateMetadata();
    }
    updateParamSubscribers(paramId, source, std::nullopt);
}

void PatchImpl::updateParamSubscribers(int paramId,
                                       ParamChangeSource source,
                                       tiny_optional<int> voiceId)
{
    if (inBatchUpdate)
    {
        return;
    }

    for (auto& listenerWeakPtr : paramListeners)
    {
        auto listener = listenerWeakPtr.lock();
        // if this fails then the listener has not unsubscribed
        // on destruction
        assert(listener != nullptr);

        if (voiceId && *voiceId < listener->getVoiceCount())
        {
            listener->onParamChange(
              paramId, getParamFinalValue(paramId, *voiceId), source, voiceId);
        }
        else
        {
            // notify with per-voice values as the parameter
            // may be used in per-voice macro
            int voiceCount = listener->getVoiceCount();
            for (int voiceId = 0; voiceId < voiceCount; voiceId++)
            {
                listener->onParamChange(
                  paramId, getParamFinalValue(paramId, voiceId), source, voiceId);
            }
        }
    };
}

void PatchImpl::startBatchUpdate()
{
    assert(!inBatchUpdate); // nested batches not supported
    inBatchUpdate = true;
}

void PatchImpl::finishBatchUpdate()
{
    assert(inBatchUpdate);
    inBatchUpdate = false;

    for (int paramId = 0; paramId < std::ssize(paramValues); paramId++)
    {
        updateParamSubscribers(paramId, ParamChangeSource::kPatchAssign, std::nullopt);
    }
}

void PatchImpl::subscribeParamUpdates(std::weak_ptr<ParamChangeListener> listener)
{
    paramListeners.emplace_back(std::move(listener));
}

void PatchImpl::unsubscribeParamUpdates(const ParamChangeListener& listener)
{
    std::erase_if(paramListeners,
                  [&listener](auto other) { return other.lock().get() == &listener; });
}

[[nodiscard]] Patch::optional_paramvalue_t PatchImpl::getMacroValue(int macroId) const
{
    assert(macroId < std::ssize(macroValues));
    assert(!metadata.macros[macroId].perVoice);

    return macroValues[macroId][0].has_value()
             ? static_cast<optional_paramvalue_t>(*macroValues[macroId][0])
             : std::nullopt;
}

void PatchImpl::setMacroValue(int macroId, optional_paramvalue_t value, tiny_optional<int> voiceId)
{
    assert(macroId < std::ssize(macroValues));
    assert(metadata.macros[macroId].perVoice == voiceId.has_value());

    bool updated = false;
    if (voiceId)
    {
        auto oldValue = macroValues[macroId][*voiceId].value_or(0);
        updated = std::abs(value.value_or(0) - oldValue) > kMacroDeltaThreshold;
        if (updated)
        {
            macroValues[macroId][*voiceId] =
              value.has_value() ? static_cast<optional_macrovalue_short_t>(*value) : std::nullopt;
        }
    }
    else
    {
        for (int voiceId = 0; voiceId < kMaxInstrumentVoices; voiceId++)
        {
            auto oldValue = macroValues[macroId][voiceId].value_or(0);
            if (std::abs(value.value_or(0) - oldValue) > kMacroDeltaThreshold)
            {
                updated = true;
                macroValues[macroId][voiceId] = value.has_value()
                                                  ? static_cast<optional_macrovalue_short_t>(*value)
                                                  : std::nullopt;
            }
        }
    }

    if (updated)
    {
        updateMacroValue(macroId, voiceId);
    }
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void PatchImpl::updateMacroValue(int macroId, tiny_optional<int> voiceId)
{
    for (auto& modifier : metadata.macros[macroId].modifiers)
    {
        if (!modifier.paramId || modifier.amount == 0.f)
        {
            continue;
        }
        updateMacroParamValues(*modifier.paramId, voiceId);
    }
}

const Patch::Metadata& PatchImpl::getMetadata() const
{
    return metadata;
}

const Sample* PatchImpl::getSample(int sampleSlotId) const
{
    if (!samplePool || sampleSlotId < 0 || sampleSlotId >= std::ssize(samplePoolIds))
    {
        return nullptr;
    }

    auto sampleId = samplePoolIds[sampleSlotId];

    if (!sampleId)
    {
        return nullptr;
    }

    return &samplePool->getSample(*sampleId);
}

tiny_optional<float> PatchImpl::getMacroModifierAmount(int macroId, int modifierIdx)
{
    assert(macroId >= 0 && macroId < std::ssize(metadata.macros));
    if (modifierIdx >= std::ssize(metadata.macros[macroId].modifiers))
    {
        return std::nullopt;
    }

    return metadata.macros[macroId].modifiers[modifierIdx].amount;
}

void PatchImpl::updateMacroParamValues(int paramId, tiny_optional<int> voiceId)
{
    auto initValue =
      metadata.params[paramId].macroOperation == Metadata::Param::MacroOperation::kAdd ? 0.0f
                                                                                       : 1.0f;
    macroParamValues[paramId] = initValue;
    if (voiceId)
    {
        macroParamValuesPerVoice[paramId][*voiceId] = initValue;
    }
    else
    {
        std::ranges::fill(macroParamValuesPerVoice[paramId], initValue);
    }

    for (int macroId = 0; macroId < std::ssize(metadata.macros); macroId++)
    {
        if (paramMacroLookup[paramId].test(macroId))
        {
            updateMacroParamValue(macroId, paramId, voiceId);
        }
    }
    updateParamSubscribers(paramId, ParamChangeSource::kMacro, voiceId);
}

Patch::optional_paramvalue_t PatchImpl::getParamFinalValue(int paramId) const
{
    assert(paramId >= 0 && paramId < std::ssize(macroParamValues));

    const auto& value = paramValues[paramId];

    if (!value)
    {
        return std::nullopt;
    }

    auto finalValue =
      metadata.params[paramId].macroOperation == Metadata::Param::MacroOperation::kAdd
        ? *value + static_cast<paramvalue_t>(macroParamValues[paramId])
        : *value * static_cast<paramvalue_t>(macroParamValues[paramId]);

    return std::clamp(finalValue,
                      static_cast<paramvalue_t>(metadata.params[paramId].minValue),
                      static_cast<paramvalue_t>(metadata.params[paramId].maxValue));
}

Patch::optional_paramvalue_t PatchImpl::getParamFinalValue(int paramId, int voiceIdx) const
{
    assert(paramId >= 0 && paramId < std::ssize(macroParamValues));

    const auto& value = paramValues[paramId];

    if (!value)
    {
        return std::nullopt;
    }

    auto finalValue =
      metadata.params[paramId].macroOperation == Metadata::Param::MacroOperation::kAdd
        ? *value + static_cast<paramvalue_t>(macroParamValues[paramId]) +
            static_cast<paramvalue_t>(macroParamValuesPerVoice[paramId][voiceIdx])
        : *value * (static_cast<paramvalue_t>(macroParamValues[paramId]) *
                    static_cast<paramvalue_t>(macroParamValuesPerVoice[paramId][voiceIdx]));
    return std::clamp(finalValue,
                      static_cast<paramvalue_t>(metadata.params[paramId].minValue),
                      static_cast<paramvalue_t>(metadata.params[paramId].maxValue));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) seems ok
void PatchImpl::updateMacroParamValue(int macroId, int paramId, tiny_optional<int> voiceId)
{
    auto paramRange =
      static_cast<float>(metadata.params[paramId].maxValue - metadata.params[paramId].minValue);
    auto& macroParamValue = macroParamValues[paramId];
    auto& macroParamValuePerVoice = macroParamValuesPerVoice[paramId];
    const auto& macroMeta = metadata.macros[macroId];
    auto macroRange = static_cast<float>(macroMeta.bipolar ? 2 * kMaxMacroValue : kMaxMacroValue);
    for (const auto& modifier : macroMeta.modifiers)
    {
        if (modifier.paramId == paramId && modifier.amount != 0.f)
        {
            if (voiceId)
            {
                if (metadata.params[paramId].macroOperation ==
                    Metadata::Param::MacroOperation::kAdd)
                {
                    macroParamValuePerVoice[*voiceId] +=
                      static_cast<float>(macroValues[macroId][*voiceId].value_or(0)) *
                      modifier.amount * paramRange / macroRange;
                }
                else // kMult
                {
                    assert(metadata.params[paramId].macroOperation ==
                           Metadata::Param::MacroOperation::kMult);
                    assert(!macroMeta.bipolar);
                    macroParamValuePerVoice[*voiceId] *=
                      static_cast<float>(macroValues[macroId][*voiceId].value_or(0)) *
                        modifier.amount / macroRange +
                      (1.0f - modifier.amount);
                }
            }
            else
            {
                if (macroMeta.perVoice)
                {
                    if (metadata.params[paramId].macroOperation ==
                        Metadata::Param::MacroOperation::kAdd)
                    {
                        for (int voiceId = 0; voiceId < kMaxInstrumentVoices; voiceId++)
                        {
                            macroParamValuePerVoice[voiceId] +=
                              static_cast<float>(macroValues[macroId][voiceId].value_or(0)) *
                              modifier.amount * paramRange / macroRange;
                        }
                    }
                    else // kMult
                    {
                        assert(metadata.params[paramId].macroOperation ==
                               Metadata::Param::MacroOperation::kMult);
                        for (int voiceId = 0; voiceId < kMaxInstrumentVoices; voiceId++)
                        {
                            macroParamValuePerVoice[voiceId] *=
                              static_cast<float>(macroValues[macroId][voiceId].value_or(0)) *
                                modifier.amount / macroRange +
                              (1.0f - modifier.amount);
                        }
                    }
                }
                else
                {
                    if (metadata.params[paramId].macroOperation ==
                        Metadata::Param::MacroOperation::kAdd)
                    {
                        macroParamValue += static_cast<float>(macroValues[macroId][0].value_or(0)) *
                                           modifier.amount * paramRange / macroRange;
                    }
                    else // kMult
                    {
                        assert(metadata.params[paramId].macroOperation ==
                               Metadata::Param::MacroOperation::kMult);
                        macroParamValue *= static_cast<float>(macroValues[macroId][0].value_or(0)) *
                                             modifier.amount / macroRange +
                                           (1.0f - modifier.amount);
                    }
                }
            }
        }
    }
}

}