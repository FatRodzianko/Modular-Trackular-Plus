#include "PatchImpl.h"
#include "../sample/SamplePool.h"
#include "Patch.pb.h"

#include "pb_decode.h"
#include "pb_encode.h"

#include "PolyUtil/AudioCS.h"
#include "PolyUtil/ScopeExit.h"
#include "PolyUtil/math.h"
#include "PolyUtil/misc.h"
#include "PolyUtil/string.h"
#include "enum_util.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace polysynth
{
using ConfigPb = polysynth_Config;
using PatchPb = polysynth_Patch;
using ArpStylePb = polysynth_ArpStyle;
using SeqModePb = polysynth_SeqMode;
using ParamPb = polysynth_Param;
using SequencerPb = polysynth_Sequencer;
using ArpConfigPb = polysynth_ArpConfig;
using SeqConfigPb = polysynth_SeqConfig;

PatchImpl::PatchImpl(std::shared_ptr<const InstrumentType> instrumentType,
                     std::shared_ptr<SamplePool> samplePool_)
  : instrumentType(std::move(instrumentType))
  , samplePool(std::move(samplePool_))
{
    updateMetadata();

#ifdef DEBUG
    testMacroValueOptional();
#endif

    auto paramCount = std::ssize(metadata.params);
    paramValues.reserve(paramCount);
    paramValues.insert(paramValues.begin(), paramCount, std::nullopt);
    macroParamValues.reserve(paramCount);
    macroParamValues.insert(macroParamValues.begin(), paramCount, 0);
    macroParamValuesPerVoice.reserve(paramCount);
    macroParamValuesPerVoice.insert(macroParamValuesPerVoice.begin(), paramCount, { 0 });
    paramMacroLookup.reserve(paramCount);
    paramMacroLookup.insert(paramMacroLookup.begin(), paramCount, 0);

    metadata.initDefaultMacros(metadata.macros);
    assert(std::ssize(metadata.macros) == kMacroCount);
    auto macroCount = std::ssize(metadata.macros);
    macroValues.reserve(macroCount);
    macroValues.insert(macroValues.begin(), macroCount, { std::nullopt });

    for (const auto& macro : metadata.macros)
    {
        for (const auto& modifier : macro.modifiers)
        {
            if (modifier.paramId)
            {
                paramMacroLookup[*modifier.paramId].set(
                  std::distance(&*metadata.macros.cbegin(), &macro));
            }
        }
    }

    bool metadataInvalid = false;
    for (int param = 0; param < paramCount; param++)
    {
        paramValues[param] = metadata.params[param].defaultValue;
        metadataInvalid |= paramValues[param] && metadata.params[param].invalidatesOtherMetadata;
    }
    if (metadataInvalid)
    {
        updateMetadata();
    }

    samplePoolIds.resize(metadata.sampleSlots.size(), std::nullopt);
    sampleFilePaths.resize(metadata.sampleSlots.size(), "");

    int sampleSlotId = 0;
    for (const auto& sampleSlot : metadata.sampleSlots)
    {
        if (!sampleSlot.defaultSamplePath.empty() && samplePool)
        {
            auto status = loadSample(sampleSlotId, sampleSlot.defaultSamplePath);
            if (status != LoadStatus::kSuccess)
            {
                auto slashPos = sampleSlot.defaultSamplePath.find('/');
                std::string folder = slashPos != std::string::npos
                                       ? sampleSlot.defaultSamplePath.substr(0, slashPos)
                                       : instrumentType->name;
                auto files = samplePool->listSamples(folder);
                if (!files.empty())
                {
                    [[maybe_unused]] auto _ =
                      loadSample(sampleSlotId, folder + "/" + std::string(files.front()));
                }
            }
        }
        sampleSlotId++;
    }
}

void PatchImpl::testMacroValueOptional()
{
    // tiny_optional with fp16 is a bit fiddly so make sure it works fine

    optional_macrovalue_short_t temp = std::nullopt;
    assert(!temp);
    temp = 1000;
    assert(temp);
}

static void ensureEditorLayouts(Patch::Metadata& metadata)
{
    using EditorLayout = Patch::Metadata::EditorLayout;
    using EditorSection = EditorLayout::EditorSection;
    using EditorSubsection = EditorLayout::EditorSubsection;
    using EditorElement = EditorLayout::EditorElement;

    if (metadata.layoutFlat.sections.empty() && !metadata.layout33.sections.empty())
    {
        for (const auto& sec33 : metadata.layout33.sections)
        {
            EditorSection flatAccum;
            flatAccum.name = (sec33.name == "Mod") ? "Modulation" : sec33.name;
            flatAccum.shortName = sec33.shortName;
            for (const auto& el : sec33.elements)
            {
                if (el.type != EditorElement::Type::kEmpty)
                {
                    flatAccum.elements.push_back(el);
                }
            }
            for (const auto& sub : sec33.subsections)
            {
                if (!sub.name.empty())
                {
                    if (!flatAccum.elements.empty())
                    {
                        metadata.layoutFlat.sections.push_back(std::move(flatAccum));
                        flatAccum = EditorSection{};
                        flatAccum.name = (sec33.name == "Mod") ? "Modulation" : sec33.name;
                        flatAccum.shortName = sec33.shortName;
                    }
                    EditorSection subSec;
                    if (sec33.name.empty())
                    {
                        subSec.name = sub.name;
                    }
                    else if (sec33.name == "Env")
                    {
                        subSec.name = sub.name + " Envelope";
                        subSec.shortName = sub.name;
                    }
                    else
                    {
                        subSec.name = sec33.name + " " + sub.name;
                        subSec.shortName = sub.name;
                    }
                    for (const auto& el : sub.elements)
                    {
                        if (el.type != EditorElement::Type::kEmpty)
                        {
                            subSec.elements.push_back(el);
                        }
                    }
                    if (!subSec.elements.empty())
                    {
                        metadata.layoutFlat.sections.push_back(std::move(subSec));
                    }
                }
                else
                {
                    for (const auto& el : sub.elements)
                    {
                        if (el.type != EditorElement::Type::kEmpty)
                        {
                            flatAccum.elements.push_back(el);
                        }
                    }
                }
            }
            if (!flatAccum.elements.empty())
            {
                metadata.layoutFlat.sections.push_back(std::move(flatAccum));
            }
        }
    }

    if (metadata.layout33.sections.empty() && !metadata.layoutFlat.sections.empty())
    {
        constexpr size_t kMaxHwSections = 5;
        constexpr size_t kKnobsPerSubsection = 9;
        for (size_t s = 0; s < metadata.layoutFlat.sections.size(); ++s)
        {
            const auto& flatSec = metadata.layoutFlat.sections[s];
            const std::string subLabel = !flatSec.shortName.empty() ? flatSec.shortName : flatSec.name;
            if (s < kMaxHwSections - 1 || metadata.layoutFlat.sections.size() <= kMaxHwSections)
            {
                EditorSection sec33;
                sec33.name = flatSec.name;
                sec33.shortName = flatSec.shortName;
                if (flatSec.elements.empty())
                {
                    sec33.subsections.push_back(EditorSubsection{});
                }
                for (size_t i = 0; i < flatSec.elements.size(); i += kKnobsPerSubsection)
                {
                    EditorSubsection sub;
                    if (flatSec.elements.size() > kKnobsPerSubsection)
                    {
                        sub.name = std::to_string((i / kKnobsPerSubsection) + 1);
                    }
                    size_t end = std::min(i + kKnobsPerSubsection, flatSec.elements.size());
                    sub.elements.assign(flatSec.elements.begin() + i, flatSec.elements.begin() + end);
                    sec33.subsections.push_back(std::move(sub));
                }
                metadata.layout33.sections.push_back(std::move(sec33));
            }
            else
            {
                if (metadata.layout33.sections.size() < kMaxHwSections)
                {
                    EditorSection sec33;
                    sec33.name = flatSec.name;
                    sec33.shortName = flatSec.shortName;
                    metadata.layout33.sections.push_back(std::move(sec33));
                }
                auto& lastSec = metadata.layout33.sections[kMaxHwSections - 1];
                for (size_t i = 0; i < flatSec.elements.size(); i += kKnobsPerSubsection)
                {
                    EditorSubsection sub;
                    sub.name = subLabel;
                    size_t end = std::min(i + kKnobsPerSubsection, flatSec.elements.size());
                    sub.elements.assign(flatSec.elements.begin() + i, flatSec.elements.begin() + end);
                    lastSec.subsections.push_back(std::move(sub));
                }
            }
        }
    }
}

void PatchImpl::updateMetadata()
{
    auto macros = std::move(metadata.macros);

    instrumentType->createMetadata(*this, metadata);
    ensureEditorLayouts(metadata);
    metadata.macros = std::move(macros);

    metadata.macros[*SystemMacroId::kPressure].displayName = "Pressure",
    metadata.macros[*SystemMacroId::kPressure].bipolar = false;
    metadata.macros[*SystemMacroId::kPressure].perVoice = true;
    metadata.macros[*SystemMacroId::kPressure].system = true;
    metadata.macros[*SystemMacroId::kVelocity].displayName = "Velocity",
    metadata.macros[*SystemMacroId::kVelocity].bipolar = false;
    metadata.macros[*SystemMacroId::kVelocity].perVoice = true;
    metadata.macros[*SystemMacroId::kVelocity].system = true;
}

tiny_optional<float> PatchImpl::getParamValueEditMapped(int paramId) const
{
    auto value = getParamValue(paramId);

    if (!value)
    {
        return std::nullopt;
    }

    const auto& paramMeta = metadata.params[paramId];

    bool bipolar = paramMeta.minValue < 0;
    assert(!bipolar ||
           paramMeta.minValue == -paramMeta.maxValue); // bipolar needs to have 0 in center
    auto temp = scaleLin(static_cast<float>(*value),
                         paramMeta.minValue,
                         paramMeta.maxValue,
                         bipolar ? -1.0f : 0.0f,
                         1.0f);
    switch (paramMeta.editCurve)
    {
        case Metadata::Param::Curve::kLinear:
            break;
        case Metadata::Param::Curve::kExponential:
        {
            bool negative = temp < 0;
            if (negative)
            {
                temp = -temp;
            }
            temp = std::log10(temp * (kExpCurveBase - 1.0f) + 1.0f);
            if (negative)
            {
                temp = -temp;
            }
            break;
        }
        case Metadata::Param::Curve::kExponentialSteep:
        {
            bool negative = temp < 0;
            if (negative)
            {
                temp = -temp;
            }
            temp = std::log10(temp * (std::pow(kExpCurveBase, kExpSteepCurveMult) - 1.0f) + 1.0f) /
                   kExpSteepCurveMult;
            if (negative)
            {
                temp = -temp;
            }
            break;
        }
        case Metadata::Param::Curve::kPower:
        {
            bool negative = temp < 0;
            if (negative)
            {
                temp = -temp;
            }
            temp = std::pow(temp, 1.0 / kPowerCurveCoeff);
            if (negative)
            {
                temp = -temp;
            }
            break;
        }
        default:
            assert(false);
            break;
    }

    return bipolar ? ((temp + 1) / 2) : temp;
}

void PatchImpl::setParamValueEditMapped(int paramId,
                                        tiny_optional<float> value,
                                        ParamChangeSource source)
{
    if (!value)
    {
        setParamValue(paramId, std::nullopt, source);
        return;
    }

    assert(*value >= 0.0f && *value <= 1.0f);

    const auto& paramMeta = metadata.params[paramId];

    bool bipolar = paramMeta.minValue < 0;
    float temp = bipolar ? (*value * 2 - 1) : *value; // normalize to [-1, 1] for bipolar
    switch (paramMeta.editCurve)
    {
        case Metadata::Param::Curve::kLinear:
            break;
        case Metadata::Param::Curve::kExponential:
        {
            bool negative = temp < 0;
            if (negative)
            {
                temp = -temp;
            }
            temp = (std::pow(kExpCurveBase, temp) - 1.0f) / (kExpCurveBase - 1.0f);
            if (negative)
            {
                temp = -temp;
            }
            break;
        }
        case Metadata::Param::Curve::kExponentialSteep:
        {
            bool negative = temp < 0;
            if (negative)
            {
                temp = -temp;
            }
            temp = (std::pow(kExpCurveBase, kExpSteepCurveMult * temp) - 1.0f) /
                   (std::pow(kExpCurveBase, kExpSteepCurveMult) - 1.0f);
            if (negative)
            {
                temp = -temp;
            }
            break;
        }

        case Metadata::Param::Curve::kPower:
        {
            bool negative = temp < 0;
            if (negative)
            {
                temp = -temp;
            }
            temp = std::pow(temp, kPowerCurveCoeff);
            if (negative)
            {
                temp = -temp;
            }
            break;
        }
        default:
            assert(false);
            temp = 0;
            break;
    }
    setParamValue(paramId,
                  static_cast<optional_paramvalue_t>(scaleLin(
                    temp, bipolar ? -1.0f : 0.0f, 1.0f, paramMeta.minValue, paramMeta.maxValue)),
                  source);
}

// nanopb-generated stuff
// NOLINTBEGIN(*-pro-bounds-*)

#ifdef FEATURE_SEQUENCER

static ConfigPb serializeSeqConfig(const polysynth::Patch::Sequencer::Config& seqConfig)
{
    static_assert(enumutil::EnumMax<ArpStylePb>::value ==
                  enumutil::EnumMax<Patch::Sequencer::Config::Arp::ArpStyle>::value);
    static_assert(enumutil::EnumMin<ArpStylePb>::value ==
                  enumutil::EnumMin<Patch::Sequencer::Config::Arp::ArpStyle>::value);

    static_assert(enumutil::EnumMax<SeqModePb>::value ==
                  enumutil::EnumMax<Patch::Sequencer::Config::Mode>::value);
    static_assert(enumutil::EnumMin<SeqModePb>::value ==
                  enumutil::EnumMin<Patch::Sequencer::Config::Mode>::value);

    ConfigPb result;

    result.arp.style = static_cast<ArpStylePb>(seqConfig.arp.arpStyle);
    result.mode = static_cast<SeqModePb>(seqConfig.mode);
    result.arp.octaveRange = seqConfig.arp.octaveRange;

    return result;
}

#endif

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
extern chordscheme_t getChordSchemeFromPb(const uint64_t (&pbChordScheme)[2]);
extern void fillPbChordScheme(chordscheme_t chordScheme, uint64_t (&pbChordScheme)[2]);
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)

void PatchImpl::serialize(const Patch& patch,
                          std::vector<ParamPb>& params,
                          polysynth_Patch& patchData)
{
    auto paramsCount = std::ssize(patch.getMetadata().params);

    params.clear();
    params.reserve(paramsCount);

    for (int paramIdx = 0; paramIdx < paramsCount; paramIdx++)
    {
        auto value = patch.getParamValue(paramIdx);
        ParamPb param;

        if (value)
        {
            param.has_value = true;
            param.value = *value;
        }
        else
        {
            param.has_value = false;
        }

        params.push_back(param);
    }
    params.shrink_to_fit();

    patchData.instrumentTypeId = patch.getMetadata().instrumentTypeId;
    patchData.params_count = static_cast<pb_size_t>(params.size());
    patchData.params = params.data();
    std::fill(std::begin(patchData.samples), std::end(patchData.samples), polysynth_Sample{});
    std::fill(std::begin(patchData.macros), std::end(patchData.macros), polysynth_Macro{});
    patchData.has_sequencer = true;
    patchData.sequencer =
      SequencerPb{ .has_config = true,
                   .config =
                     ConfigPb{
                       .mode = polysynth_SeqMode_Off,
                       .has_arp = true,
                       .arp = ArpConfigPb{ .style = polysynth_ArpStyle_Random, .octaveRange = 1 },
                       .has_seq = true,
                       .seq = SeqConfigPb{ .rootNote = 0, .scaleScheme = 0 },
                     },
                   .has_sequence = true,
                   .sequence = {} };

    auto samplesCount = std::ssize(patch.getMetadata().sampleSlots);
    for (int sampleIdx = 0; sampleIdx < samplesCount; sampleIdx++)
    {
        const auto* sample = patch.getSample(sampleIdx);
        copyString(sample != nullptr ? sample->name : "", patchData.samples[sampleIdx].filePath);
    }

    auto macrosCount = std::ssize(patch.getMetadata().macros);
    assert(macrosCount <= std::ssize(patchData.macros));
    for (int macroIdx = 0; macroIdx < macrosCount; macroIdx++)
    {
        const auto& macro = patch.getMetadata().macros[macroIdx];
        auto& macroPb = patchData.macros[macroIdx];
        copyString(macro.displayName, macroPb.name);
        macroPb.bipolar = macro.bipolar;
        macroPb.modifiersCount = static_cast<int32_t>(macro.modifiers.size());
        for (int modifierIdx = 0; modifierIdx < macroPb.modifiersCount; modifierIdx++)
        {
            const auto& modifier = macro.modifiers[modifierIdx];
            macroPb.modifiers[modifierIdx].paramId = modifier.paramId ? *modifier.paramId : -1;
            macroPb.modifiers[modifierIdx].amount = modifier.amount;
        }
    }
#ifdef FEATURE_SEQUENCER
    patchData.sequencer.config = serializeSeqConfig(patch.getSequencer().config);
    patchData.sequencer.sequence.stepCount =
      static_cast<int32_t>(patch.getSequencer().sequences[0].steps.size());
    for (int32_t i = 0; i < patchData.sequencer.sequence.stepCount; i++)
    {
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
        patchData.sequencer.sequence.sequence[i].note =
          patch.getSequencer().sequences[0].steps[i].chord.rootNote;
        fillPbChordScheme(patch.getSequencer().sequences[0].steps[i].chord.scheme,
                          patchData.sequencer.sequence.sequence[i].chordScheme);
        patchData.sequencer.sequence.sequence[i].length =
          patch.getSequencer().sequences[0].steps[i].length;
        // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    }
    patchData.sequencer.sequence.rootNote = patch.getSequencer().sequences[0].rootNote;
    patchData.sequencer.sequence.scaleScheme =
      static_cast<int32_t>(patch.getSequencer().sequences[0].scaleScheme.to_ullong());
#endif
}

bool PatchImpl::serialize(const Patch& patch, ProtobufHelpers::WriterFunc writer)
{
    PatchPb patchData;
    std::vector<ParamPb> params;

    serialize(patch, params, patchData);

    pb_ostream_t stream = {
        &ProtobufHelpers::WriterCallback, static_cast<void*>(&writer), SIZE_MAX, 0, nullptr
    };
    return pb_encode(&stream, &polysynth_Patch_msg, &patchData);
}

#ifdef FEATURE_SEQUENCER

static polysynth::Patch::Sequencer::Config deserializeSeqConfig(const ConfigPb& seqConfig)
{
    polysynth::Patch::Sequencer::Config result;

    switch (seqConfig.arp.style)
    {
        case polysynth_ArpStyle_Up:
            result.arp.arpStyle = Patch::Sequencer::Config::Arp::ArpStyle::kUp;
            break;
        case polysynth_ArpStyle_Down:
            result.arp.arpStyle = Patch::Sequencer::Config::Arp::ArpStyle::kDown;
            break;
        case polysynth_ArpStyle_PlayOrder:
            result.arp.arpStyle = Patch::Sequencer::Config::Arp::ArpStyle::kPlayOrder;
            break;
        case polysynth_ArpStyle_Random:
            result.arp.arpStyle = Patch::Sequencer::Config::Arp::ArpStyle::kRandom;
            break;
        default:
            assert(false);
            break;
    }

    switch (seqConfig.mode)
    {
        case polysynth_SeqMode_Off:
            result.mode = Patch::Sequencer::Config::Mode::kOff;
            break;
        case polysynth_SeqMode_Arp:
            result.mode = Patch::Sequencer::Config::Mode::kArp;
            break;
        case polysynth_SeqMode_Seq:
            result.mode = Patch::Sequencer::Config::Mode::kSeq;
            break;
        default:
            assert(false);
            break;
    }

    result.arp.octaveRange = std::clamp<int>(seqConfig.arp.octaveRange,
                                             Patch::Sequencer::Config::Arp::kMinOctaveRange,
                                             Patch::Sequencer::Config::Arp::kMaxOctaveRange);

    return result;
}
#endif

bool PatchImpl::deserialize(Patch& patch, ProtobufHelpers::ReaderFunc reader, bool synchronizeAudio)
{
    PatchPb patchData;
    patchData.params = nullptr;

    pb_istream_t stream = {
        &ProtobufHelpers::ReaderCallback, static_cast<void*>(&reader), SIZE_MAX, nullptr
    };

    ScopeExit freeConfig(
      [&patchData]()
      {
          // NOLINTBEGIN(cppcoreguidelines-owning-memory, *-no-malloc)
          if (patchData.params != nullptr)
          {
              pb_free(patchData.params);
          }
          // NOLINTEND(cppcoreguidelines-owning-memory, *-no-malloc)
      });

    if (!pb_decode(&stream, &polysynth_Patch_msg, &patchData))
    {
        return false;
    }

    if (synchronizeAudio)
    {
        AudioCS cs;
        return deserialize(patch, patchData);
    }

    return deserialize(patch, patchData);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) seems ok
bool PatchImpl::deserialize(Patch& patch, const polysynth_Patch& patchData)
{
    if (patch.getMetadata().instrumentTypeId != patchData.instrumentTypeId &&
        patchData.params_count != patch.getMetadata().params.size())
    {
        return false;
    }

    const auto paramCount = std::min(static_cast<int>(patch.getMetadata().params.size()),
                                     static_cast<int>(patchData.params_count));

    patch.clearMacroModifiers();

    {
        patch.startBatchUpdate();
        ScopeExit finishBatch([&patch] { patch.finishBatchUpdate(); });

        int paramIdx = 0;
        for (; paramIdx < paramCount; paramIdx++)
        {
            const auto& param = patchData.params[paramIdx];
            const auto& pMeta = patch.getMetadata().params[paramIdx];
            patch.setParamValue(paramIdx,
                                param.has_value ? optional_paramvalue_t(param.value)
                                                : pMeta.defaultValue,
                                ParamChangeSource::kPatchAssign);
        }
        for (paramIdx = 0; paramIdx < paramCount; paramIdx++)
        {
            const auto value = patch.getParamValue(paramIdx);
            const auto& pMeta = patch.getMetadata().params[paramIdx];
            if (value && (*value < pMeta.minValue || *value > pMeta.maxValue))
            {
                patch.setParamValue(
                  paramIdx, pMeta.defaultValue, ParamChangeSource::kPatchAssign);
            }
        }
        for (; paramIdx < static_cast<int>(patch.getMetadata().params.size()); paramIdx++)
        {
            patch.setParamValue(paramIdx,
                                patch.getMetadata().params[paramIdx].defaultValue,
                                ParamChangeSource::kPatchAssign);
        }

        const auto samplesCount = std::ssize(patch.getMetadata().sampleSlots);
        for (int sampleIdx = 0; sampleIdx < samplesCount; sampleIdx++)
        {
            // ignore the error, this is temporary solution anyway,
            // samples will be embedded in patches
            [[maybe_unused]] auto result =
              patch.loadSample(sampleIdx, patchData.samples[sampleIdx].filePath);
        }

        static_assert(sizeof(PatchPb::macros) / sizeof(PatchPb::macros[0]) == kMacroCount);

        for (int macroIdx = 0; macroIdx < std::ssize(patchData.macros); macroIdx++)
        {
            const auto& macroPb = patchData.macros[macroIdx];
            if (!patch.getMetadata().macros[macroIdx].system)
            {
                patch.setMacroDisplayName(macroIdx, macroPb.name);
                patch.setMacroBipolar(macroIdx, macroPb.bipolar);
            }

            for (int modIdx = 0; modIdx < macroPb.modifiersCount; modIdx++)
            {
                const auto& paramMod = macroPb.modifiers[modIdx];

                if (paramMod.paramId >= std::ssize(patch.getMetadata().params))
                {
                    continue;
                }
                patch.setMacroModifier(macroIdx,
                                       modIdx,
                                       paramMod.paramId != -1 ? tiny_optional<int>(paramMod.paramId)
                                                              : std::nullopt);
                patch.setMacroModifierAmount(macroIdx, modIdx, paramMod.amount);
            }

            if (macroPb.modifiersCount > 0)
            {
                if (patch.getMetadata().macros[macroIdx].perVoice)
                {
                    for (int voiceIdx = 0; voiceIdx < kMaxInstrumentVoices; voiceIdx++)
                    {
                        patch.setMacroValue(macroIdx, std::nullopt, voiceIdx);
                    }
                }
                else
                {
                    patch.setMacroValue(macroIdx, std::nullopt, std::nullopt);
                }
            }
        }
    }

#ifdef FEATURE_SEQUENCER
    patch.setSequencerConfig(deserializeSeqConfig(patchData.sequencer.config));

    Patch::Sequencer::Sequence sequence;
    for (int32_t i = 0; i < patchData.sequencer.sequence.stepCount; i++)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        const auto& step = patchData.sequencer.sequence.sequence[i];
        sequence.steps.push_back(Patch::Sequencer::Step{
          .chord = Musicology::Chord{ .scheme = getChordSchemeFromPb(step.chordScheme),
                                      .rootNote = step.note },
          .length = (step.length > 0) ? step.length : 1 });
    }
    sequence.rootNote = patchData.sequencer.sequence.rootNote;
    sequence.scaleScheme = patchData.sequencer.sequence.scaleScheme;
    patch.setSequence(sequence, 0);
#endif

    return true;
}

// NOLINTEND(*-pro-bounds-*)

std::optional<InstrumentTypeId> PatchImpl::deserializeInstrumentType(
  ProtobufHelpers::ReaderFunc reader)
{
    pb_istream_t stream = {
        &ProtobufHelpers::ReaderCallback, static_cast<void*>(&reader), SIZE_MAX, nullptr
    };

    uint32_t tag = {};
    pb_wire_type_t wire_type = {};
    bool eof = {};
    if (!pb_decode_tag(&stream, &wire_type, &tag, &eof))
    {
        return {};
    }

    if (tag != polysynth_Patch_instrumentTypeId_tag)
    {
        // when the field has default value (0) it's not stored in the stream
        return static_cast<InstrumentTypeId>(0);
    }

    uint64_t instrumentTypeId = 0;
    if (!pb_decode_varint(&stream, &instrumentTypeId))
    {
        return {};
    }
    return static_cast<InstrumentTypeId>(instrumentTypeId);
}

LoadStatus PatchImpl::loadSample(int sampleSlotId, const std::string& filePath)
{
    if (!samplePool)
    {
        if (sampleSlotId >= 0 && sampleSlotId < std::ssize(sampleFilePaths))
        {
            sampleFilePaths[sampleSlotId] = filePath;
        }
        return LoadStatus::kSuccess;
    }
    auto newSampleId = samplePool->loadSample(filePath);

    if (!newSampleId)
    {
        return newSampleId.status();
    }

    auto oldSampleId =
      sampleSlotId < std::ssize(samplePoolIds) ? samplePoolIds.at(sampleSlotId) : std::nullopt;

    {
        AudioCS cs;
        samplePoolIds[sampleSlotId] = *newSampleId;
    }

    if (oldSampleId)
    {
        samplePool->deleteSample(*oldSampleId);
    }

    sampleFilePaths[sampleSlotId] = filePath;

    return LoadStatus::kSuccess;
}

void PatchImpl::changeMacroModifierAmount(int macroId, int modifierIdx, Patch::paramvalue_t value)
{
    assert(macroId >= 0 && macroId < std::ssize(metadata.macros));
    assert(modifierIdx >= 0 && modifierIdx < std::ssize(metadata.macros[macroId].modifiers));
    auto& modParam = metadata.macros[macroId].modifiers[modifierIdx];
    assert(modParam.paramId);
    auto paramRange = static_cast<float>(metadata.params[*modParam.paramId].maxValue -
                                         metadata.params[*modParam.paramId].minValue);
    modParam.amount = std::clamp(modParam.amount + static_cast<float>(value) / paramRange,
                                 -Metadata::Macro::ParamModifier::kMaxAmount,
                                 Metadata::Macro::ParamModifier::kMaxAmount);
    updateMacroParamValues(*metadata.macros[macroId].modifiers[modifierIdx].paramId);
}

void PatchImpl::setMacroBipolar(int macroId, bool bipolar)
{
    assert(macroId >= 0 && macroId < std::ssize(metadata.macros));
    metadata.macros[macroId].bipolar = bipolar;

    for (const auto& modifier : metadata.macros[macroId].modifiers)
    {
        if (modifier.paramId)
        {
            updateMacroParamValues(*modifier.paramId);
        }
    }
}

void PatchImpl::setMacroDisplayName(int macroId, std::string name)
{
    assert(macroId >= 0 && macroId < std::ssize(metadata.macros));
    if (name.length() > Metadata::Macro::kMaxDisplayNameLen)
    {
        name.resize(Metadata::Macro::kMaxDisplayNameLen);
    }
    metadata.macros[macroId].displayName = std::move(name);
}

void PatchImpl::setOrAddMacroModifier(int macroId, int modifierIdx, int paramId)
{
    assert(modifierIdx >= 0);
    if (modifierIdx < std::ssize(metadata.macros[macroId].modifiers))
    {
        auto prevAmount = metadata.macros[macroId].modifiers[modifierIdx].amount;
        metadata.macros[macroId].modifiers[modifierIdx].amount = 0.f;
        auto prevParamId = metadata.macros[macroId].modifiers[modifierIdx].paramId;
        if (prevParamId)
        {
            updateMacroParamValues(*prevParamId);

            assert(*prevParamId >= 0 && *prevParamId < std::ssize(paramMacroLookup));
            paramMacroLookup[*prevParamId].reset(macroId);
        }

        // set
        metadata.macros[macroId].modifiers[modifierIdx].paramId = paramId;
        metadata.macros[macroId].modifiers[modifierIdx].amount = prevAmount;
        assert(paramId >= 0 && paramId < std::ssize(paramMacroLookup));
        paramMacroLookup[paramId].set(macroId);
        updateMacroParamValues(paramId);
    }
    else
    {
        // add
        assert(modifierIdx == std::ssize(metadata.macros[macroId].modifiers));
        metadata.macros[macroId].modifiers.push_back({ paramId, 0.f });

        assert(paramId >= 0 && paramId < std::ssize(paramMacroLookup));
        paramMacroLookup[paramId].set(macroId);
        updateMacroParamValues(paramId);
    }
}

void PatchImpl::deleteMacroModifier(int macroId, int modifierIdx)
{
    metadata.macros[macroId].modifiers[modifierIdx].amount = 0.f;
    auto prevParamId = metadata.macros[macroId].modifiers[modifierIdx].paramId;
    if (prevParamId)
    {
        updateMacroParamValues(*prevParamId);
        assert(prevParamId >= 0 && prevParamId < std::ssize(paramMacroLookup));
        paramMacroLookup[*prevParamId].reset(macroId);
    }

    assert(modifierIdx >= 0 && modifierIdx < std::ssize(metadata.macros[macroId].modifiers));
    metadata.macros[macroId].modifiers.erase(metadata.macros[macroId].modifiers.begin() +
                                             modifierIdx);
}

void PatchImpl::setMacroModifier(int macroId, int modifierIdx, tiny_optional<int> paramId)
{
    assert(macroId >= 0 && macroId < std::ssize(metadata.macros));
    if (paramId)
    {
        setOrAddMacroModifier(macroId, modifierIdx, *paramId);
    }
    else if (modifierIdx >= std::ssize(metadata.macros[macroId].modifiers))
    {
        while (modifierIdx >= std::ssize(metadata.macros[macroId].modifiers))
        {
            metadata.macros[macroId].modifiers.push_back({ std::nullopt, 0.f });
        }
    }
    else
    {
        auto prevParamId = metadata.macros[macroId].modifiers[modifierIdx].paramId;
        metadata.macros[macroId].modifiers[modifierIdx].paramId = std::nullopt;
        if (prevParamId)
        {
            updateMacroParamValues(*prevParamId);

            paramMacroLookup[*prevParamId].reset(macroId);
        }
    }
}

void PatchImpl::setMacroModifierAmount(int macroId, int modifierIdx, float amount)
{
    assert(macroId >= 0 && macroId < std::ssize(metadata.macros));
    assert(modifierIdx >= 0 && modifierIdx < std::ssize(metadata.macros[macroId].modifiers));

    metadata.macros[macroId].modifiers[modifierIdx].amount =
      std::clamp(amount,
                 -Metadata::Macro::ParamModifier::kMaxAmount,
                 Metadata::Macro::ParamModifier::kMaxAmount);

    if (metadata.macros[macroId].modifiers[modifierIdx].paramId)
    {
        updateMacroParamValues(*metadata.macros[macroId].modifiers[modifierIdx].paramId);
    }
}

void PatchImpl::clearMacroModifiers()
{
    for (auto& macro : metadata.macros)
    {
        macro.modifiers.clear();
    }

    for (int paramId = 0; paramId < std::ssize(metadata.params); paramId++)
    {
        auto initValue =
          metadata.params[paramId].macroOperation == Metadata::Param::MacroOperation::kAdd ? 0.0f
                                                                                           : 1.0f;
        macroParamValues[paramId] = initValue;
        std::ranges::fill(macroParamValuesPerVoice[paramId], initValue);
    }
}

void PatchImpl::setSequence([[maybe_unused]] const Patch::Sequencer::Sequence& steps,
                            [[maybe_unused]] int idx)
{
#ifdef FEATURE_SEQUENCER
    this->sequencer.sequences[idx] = steps;
#else
    assert(false);
#endif
}

void PatchImpl::setSequencerConfig([[maybe_unused]] const Patch::Sequencer::Config& config)
{
#ifdef FEATURE_SEQUENCER
    this->sequencer.config = config;
    if (seqConfigChanged)
    {
        seqConfigChanged();
    }
#else
    assert(false);
#endif
}

const Patch::Sequencer& PatchImpl::getSequencer() const
{
#ifdef FEATURE_SEQUENCER
    return this->sequencer;
#else
    assert(false);
    return *static_cast<Sequencer*>(nullptr);
#endif
}

}
