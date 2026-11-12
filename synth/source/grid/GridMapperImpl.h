#pragma once

#include "Component.h"
#include "config.h"

#include <memory>
#include <optional>
#include <vector>

#include "GridMapper.pb.h"
#include "MidiEventNotifier.h"
#include "PolySynth/GridMapper.h"
#include "PolySynth/Synth.h"
#include "sequencer/SequencerEngine.h"

#include "PolyUtil/misc.h"
#include "SequencerDataProvider.h"

namespace polysynth
{
using boost::container::static_vector;

class GridMapperImpl
  : public GridMapper
  , public Component

{
  public:
    GridMapperImpl(std::shared_ptr<Sequencer> sequencer,
                   const Synth::Allocator& allocator,
                   std::shared_ptr<SequencerDataProvider> sequencerDataProvider);
    ~GridMapperImpl() override;
    DELETE_COPY_MOVE(GridMapperImpl);

    void subscribeMidiEvents(int instrumentId, std::weak_ptr<MidiEventCallback> callback) override;
    void connect(Grid& grid) override;
    [[nodiscard]] const std::optional<PadConfig>& getPadConfig(int row, int column) const override;
    void setPadConfig(int row, int column, const PadConfig& config) override;
    void clearPadConfig(int row, int column) override;
    void clearAllPadsConfig() override;

    void setScale(const Musicology::Scale& scale_) override;
    [[nodiscard]] const Musicology::Scale& getScale() const override;

    void updateMain() override;

    static constexpr size_t kMaxMidiEventsPerPad = 16;

  private:
    static constexpr size_t kGridPads = static_cast<size_t>(kGridRows) * kGridColumns;

    struct PadState
    {
        polysynth::Musicology::Chord activeNotes;
        int activeInstrument = 0;
        uint8_t lastVelocity = 0;
        uint8_t lastAftertouch = 0;
        Patch::Sequencer::Config::Mode mode = Patch::Sequencer::Config::Mode::kOff;
    };

    std::shared_ptr<Grid::PadEventCallback> padEventsCallback = nullptr;

    static_vector<std::optional<PadConfig>, kGridPads>* padConfigs;
    std::vector<std::optional<PadState>> padStates;
    static constexpr int kMaxNumberOfSeqPadsPressedPerInstrument = 8;
    std::array<static_vector<int, 8>, kMaxNumberOfSeqPadsPressedPerInstrument>
      pressedSeqPadsIndexes{ static_vector<int, kMaxNumberOfSeqPadsPressedPerInstrument>{},
                             {},
                             {} };

    std::shared_ptr<Sequencer> sequencer;
    std::shared_ptr<SequencerDataProvider> sequencerDataProvider;

    MidiEventNotifier midiNotifier;

    polysynth::Musicology::Scale scale =
      Musicology::createScale(Musicology::kChormaticScaleIdx, kNoteC1);

    void onPadEvent(const Grid::PadEvent& event);
    static_vector<MidiEvent, 2 * kMaxMidiEventsPerPad> mapPadToMidiEvents(
      const Grid::PadEvent& event,
      const PadConfig& config);

    uint8_t findMaxAftertouch(int instrument);

    static inline int padIndex(int row, int column) { return row * kGridColumns + column; };

    Patch::Sequencer::Config::Mode getSequencerMode(const Grid::PadEvent& padEvent,
                                                    const PadConfig& padConfig);
    static uint8_t getVelocity(const Grid::PadEvent& padEvent);

    void handleArpPadEvent(const Grid::PadEvent& padEvent,
                           const std::optional<PadConfig>& padConfig);
    void handleSeqPadEvent(const Grid::PadEvent& padEvent,
                           const std::optional<PadConfig>& padConfig);
};
}
