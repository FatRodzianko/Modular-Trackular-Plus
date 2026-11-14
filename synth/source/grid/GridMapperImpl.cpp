#include "grid/GridMapperImpl.h"
#include "MidiUtils.h"
#include "instrument/InstrumentImpl.h"
#include <algorithm>
#include <cassert>
#include <ranges>
#include <utility>

namespace polysynth
{
GridMapperImpl::GridMapperImpl(std::shared_ptr<Sequencer> sequencer,
                               const Synth::Allocator& allocator,
                               std::shared_ptr<SequencerDataProvider> sequencerDataProvider)
  : sequencer(std::move(sequencer))
  , sequencerDataProvider{ std::move(sequencerDataProvider) }
{
    padStates.resize(kGridPads);
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    padConfigs =
      new (allocator(sizeof(*padConfigs))) static_vector<std::optional<PadConfig>, kGridPads>();
    padConfigs->resize(kGridPads);
}

// NOLINTNEXTLINE(*use-equals-default)
GridMapperImpl::~GridMapperImpl()
{
// not supported, padConfigs would leak
#ifndef UNIT_TESTS
    assert(false);
#endif
}

void GridMapperImpl::connect(Grid& grid)
{
    padEventsCallback = std::make_shared<Grid::PadEventCallback>([this](const Grid::PadEvent& event)
                                                                 { this->onPadEvent(event); });
    grid.subscribePadEvents(padEventsCallback);
}

void GridMapperImpl::subscribeMidiEvents(int instrumentId,
                                         std::weak_ptr<MidiEventCallback> callback)
{
    midiNotifier.subscribeMidiEvents(instrumentId, callback);
    sequencer->impl->subscribeMidiEvents(instrumentId, callback);
}

void GridMapperImpl::onPadEvent(const Grid::PadEvent& padEvent)
{
    const auto& padConfig = getPadConfig(padEvent.row, padEvent.column);
    if (!padConfig)
    {
        return;
    }

    auto& padState = padStates[padIndex(padEvent.row, padEvent.column)];

    auto sequencerMode = getSequencerMode(padEvent, *padConfig);

    switch (padEvent.type)
    {
        case Grid::PadEvent::Type::kPressed:
            padState = {
                padConfig->chord, padConfig->instrument, getVelocity(padEvent), 0, sequencerMode
            };
            break;
        case Grid::PadEvent::Type::kForceChanged:
            if (padState)
            {
                padState->lastAftertouch = getVelocity(padEvent);
            }
            break;

        default:
            break;
    }
    if (!padState)
    {
        return;
    }

    switch (sequencerMode)
    {
        case Patch::Sequencer::Config::Mode::kOff:
        {
            auto midiEvents = mapPadToMidiEvents(padEvent, *padConfig);
            if (midiEvents.empty())
            {
                return;
            }
            midiNotifier.notifySubscribers(padState->activeInstrument,
                                           midiEvents,
                                           MidiEventNotifier::kGridSourceIdsStart +
                                             padIndex(padEvent.row, padEvent.column));
            break;
        }
        case Patch::Sequencer::Config::Mode::kArp:
        {
            handleArpPadEvent(padEvent, padConfig);
            break;
        }
        case Patch::Sequencer::Config::Mode::kSeq:
            handleSeqPadEvent(padEvent, padConfig);
            break;
    }

    if (padEvent.type == Grid::PadEvent::Type::kReleased)
    {
        padState = std::nullopt;
    }
}

void GridMapperImpl::handleSeqPadEvent(const Grid::PadEvent& padEvent,
                                       const std::optional<PadConfig>& padConfig)
{
    assert(padConfig);
    auto padIdx = padIndex(padEvent.row, padEvent.column);
    if (padEvent.type == Grid::PadEvent::Type::kPressed)
    {

        sequencer->impl->startSequence(
          0, padConfig->instrument, padConfig->instrument, getVelocity(padEvent));
        sequencer->impl->transpose(
          padConfig->instrument,
          padConfig->chord.rootNote -
            (getScale().rootNote + (padConfig->chord.rootNote / kOneOctaveNotes) * kOneOctaveNotes),
          polysynth::Musicology::Scale{ .scheme = getScale().scheme,
                                        .rootNote = getScale().rootNote +
                                                    (padConfig->chord.rootNote / kOneOctaveNotes) *
                                                      kOneOctaveNotes });
        if (std::ssize(pressedSeqPadsIndexes[padConfig->instrument]) ==
            kMaxNumberOfSeqPadsPressedPerInstrument)
        {
            pressedSeqPadsIndexes[padConfig->instrument].erase(
              pressedSeqPadsIndexes[padConfig->instrument].begin());
        }
        pressedSeqPadsIndexes[padConfig->instrument].push_back(padIdx);
    }
    else if (padEvent.type == Grid::PadEvent::Type::kReleased)
    {
        if (!padStates[padIdx])
        {
            assert(false);
            return;
        }
        auto activeInstrument = padStates[padIdx]->activeInstrument;
        auto& pressedIndexes = pressedSeqPadsIndexes[activeInstrument];
        auto pressedIdxIt = std::find(pressedIndexes.begin(), pressedIndexes.end(), padIdx);
        if (pressedIdxIt == pressedIndexes.end())
        {
            return;
        }
        pressedIndexes.erase(pressedIdxIt);
        if (pressedIndexes.empty())
        {
            sequencer->impl->stopSequence(activeInstrument);
        }
        else
        {

            const auto& lastPressedPadState = padStates[pressedIndexes.back()];
            sequencer->impl->startSequence(
              0, activeInstrument, activeInstrument, lastPressedPadState->lastVelocity);
            sequencer->impl->transpose(
              activeInstrument,
              lastPressedPadState->activeNotes.rootNote -
                (getScale().rootNote +
                 (lastPressedPadState->activeNotes.rootNote / kOneOctaveNotes) * kOneOctaveNotes),
              polysynth::Musicology::Scale{
                .scheme = getScale().scheme,
                .rootNote = getScale().rootNote +
                            (lastPressedPadState->activeNotes.rootNote / kOneOctaveNotes) *
                              kOneOctaveNotes });
        }
    }
    else if (padEvent.type == Grid::PadEvent::Type::kForceChanged)
    {
        if (!padStates[padIdx])
        {
            assert(false);
            return;
        }
        sequencer->impl->handleAftertouch(padStates[padIdx]->lastAftertouch,
                                          padStates[padIdx]->lastAftertouch,
                                          padConfig->instrument,
                                          padIdx);
    }
}

void GridMapperImpl::handleArpPadEvent(const Grid::PadEvent& padEvent,
                                       const std::optional<PadConfig>& padConfig)
{
    auto padIdx = padIndex(padEvent.row, padEvent.column);
    auto& padState = padStates[padIndex(padEvent.row, padEvent.column)];
    assert(padState);

    switch (padEvent.type)
    {
        case Grid::PadEvent::Type::kPressed:
        {
            sequencer->impl->addToArp(
              { padConfig->chord, getVelocity(padEvent) }, padConfig->instrument, padIdx);
        }
            [[fallthrough]];
        case Grid::PadEvent::Type::kForceChanged:
        {
            sequencer->impl->handleAftertouch(
              getVelocity(padEvent),
              // TODO(karol): find max aftertouch for arpeggio
              findMaxAftertouch(padState->activeInstrument),
              SequencerEngine::getInstrumentTrackId(padState->activeInstrument),
              padIdx);
        }
        break;
        case Grid::PadEvent::Type::kReleased:
        {
            sequencer->impl->removeFromArp(padState->activeInstrument, padIdx);
        }
        break;

        default:
            break;
    }
}

uint8_t GridMapperImpl::getVelocity(const Grid::PadEvent& padEvent)
{
    return padEvent.force * (MidiEvent::Data::Note::kMaxVelocity + 1) /
           (Grid::PadEvent::kMaxForce + 1);
}

Patch::Sequencer::Config::Mode GridMapperImpl::getSequencerMode(const Grid::PadEvent& padEvent,
                                                                const PadConfig& padConfig)
{
    if (padEvent.type == Grid::PadEvent::Type::kReleased)
    {
        return padStates[padIndex(padEvent.row, padEvent.column)]->mode;
    }
    auto playMode = sequencerDataProvider
                      ? sequencerDataProvider->getSequencerData(padConfig.instrument).config.mode
                      : Patch::Sequencer::Config::Mode::kOff;
    switch (padEvent.playMode)
    {
        case Grid::PadEvent::PlayMode::kUsePadConfig:
            break;
        case Grid::PadEvent::PlayMode::kForceOneShot:
            playMode = Patch::Sequencer::Config::Mode::kOff;
            break;
        case Grid::PadEvent::PlayMode::kForceSequence:
            playMode = Patch::Sequencer::Config::Mode::kSeq;
            break;
        default:
            break;
    }
    return playMode;
}

static_vector<MidiEvent, 2 * GridMapperImpl::kMaxMidiEventsPerPad>
GridMapperImpl::mapPadToMidiEvents(const Grid::PadEvent& padEvent, const PadConfig& config)
{
    assert(getSequencerMode(padEvent, config) == Patch::Sequencer::Config::Mode::kOff);
    auto& padState = padStates[padIndex(padEvent.row, padEvent.column)];

    // ensure no need to map velocities
    uint8_t velocity = getVelocity(padEvent);
    switch (padEvent.type)
    {
        case Grid::PadEvent::Type::kPressed:
        {
            auto events = MidiUtils::createMidiNoteOnEvents<2 * kMaxMidiEventsPerPad>(
              config.chord, getVelocity(padEvent));
            MidiUtils::pushAftertouchEvents(
              events, velocity, findMaxAftertouch(config.instrument), events);
            return events;
        }
        case Grid::PadEvent::Type::kReleased:
        {
            if (!padState)
            {
                return {};
            }
            auto midiEvents =
              MidiUtils::createMidiNoteOffEvents<2 * kMaxMidiEventsPerPad>(padState->activeNotes);
            return midiEvents;
        }
        case Grid::PadEvent::Type::kForceChanged:
        {
            if (!padState)
            {
                return {};
            }
            auto events = MidiUtils::createMidiAftertouchEvents<2 * kMaxMidiEventsPerPad>(
              padState->activeNotes, velocity, findMaxAftertouch(padState->activeInstrument));
            return events;
        }
        default:
            break;
    }
    assert(false);
    return {};
}

uint8_t GridMapperImpl::findMaxAftertouch(int instrument)
{
#ifdef __clang__
    // placeholder for clang-tidy because of a bug in clang
    // https://stackoverflow.com/questions/72716894/how-to-use-stdviewstransform-on-a-vector
    (void)instrument;
    (void)this;
    assert(false);
    return 0;
#else
    auto filterByInstrument = [instrument](const std::optional<PadState>& state)
    { return state.has_value() && state->activeInstrument == instrument; };
    auto getAftertouch = [](const auto& state) { return state->lastAftertouch; };
    return std::ranges::max(padStates | std::views::filter(filterByInstrument) |
                            std::views::transform(getAftertouch));
#endif
}

void GridMapperImpl::updateMain()
{
    midiNotifier.updateMain();
}

const std::optional<GridMapper::PadConfig>& GridMapperImpl::getPadConfig(int row, int column) const
{
    const auto& config = (*padConfigs)[padIndex(row, column)];
    __builtin_prefetch(&config);
    return config;
}

void GridMapperImpl::setPadConfig(int row, int column, const PadConfig& config)
{
    (*padConfigs)[padIndex(row, column)] = config;
}

void GridMapperImpl::clearPadConfig(int row, int column)
{
    (*padConfigs)[padIndex(row, column)] = std::nullopt;
}

void GridMapperImpl::clearAllPadsConfig()
{
    std::ranges::fill(*padConfigs, std::nullopt);
}

void GridMapperImpl::setScale(const Musicology::Scale& scale_)
{
    this->scale = scale_;
}

const Musicology::Scale& GridMapperImpl::getScale() const
{
    return this->scale;
}

}
