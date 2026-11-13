#pragma once
#include "PolySynth/GridMapper.h"
#include "sequencer/Step.h"
#include "config.h"

#include <ranges>

namespace polysynth
{

class MidiUtils
{
  public:
    static constexpr bool isSyncMessage(MidiEvent::Type type)
    {
        return type == MidiEvent::Type::kClock || type == MidiEvent::Type::kStart ||
               type == MidiEvent::Type::kStop || type == MidiEvent::Type::kContinue;
    }
    
    /**
     * Creates after touch events for corresponding note on events. Result is pushed to the out&
     * param
     * @tparam MaxMidiEventsIn size of the noteOns
     * @tparam MaxMidiEventsOut size of the output static_vector
     * @param noteOns vector with note on MIDI events
     * @param polyAftertouch value to be applied to poly aftertouch
     * @param channelAftertouch value to be applied for channel aftertouch
     * @param out output static_vector to be appended with aftertouch events
     */
    template<size_t MaxMidiEventsIn, size_t MaxMidiEventsOut>
    static void pushAftertouchEvents(
      const boost::container::static_vector<MidiEvent, MaxMidiEventsIn>& noteOns,
      uint8_t polyAftertouch,
      uint8_t channelAftertouch,
      boost::container::static_vector<MidiEvent, MaxMidiEventsOut>& out)
    {
#ifdef __clang__
        (void)noteOns;
        (void)polyAftertouch;
        (void)channelAftertouch;
        (void)out;
#else
        boost::container::static_vector<MidiEvent, MaxMidiEventsIn> events{};
        std::ranges::transform(

          std::ranges::take_view(noteOns, std::min(noteOns.size(), MaxMidiEventsOut - 1)),
          std::back_inserter(out),
          [polyAftertouch](const auto& event)
          {
              return MidiEvent{ .type = MidiEvent::Type::kPolyAftertouch,
                                .channel = MidiEvent::kChannelOmni,
                                .data = MidiEvent::Data{
                                  .note = MidiEvent::Data::Note{ .note = event.data.note.note,
                                                                 .velocity = polyAftertouch } } };
          });
        out.emplace_back(MidiEvent{
          .type = MidiEvent::Type::kChannelAftertouch,
          .channel = MidiEvent::kChannelOmni,
          .data = MidiEvent::Data{ .channelAftertouch = MidiEvent::Data::ChannelAftertouch{
                                     .velocity = channelAftertouch } } });
#endif
    }

    /**
     * Create MIDI note on events for Musicology::Chord
     * @tparam MaxMidiEvents max size of the output container
     * @param chord Musicology::Chord from which events will be created
     * @param velocity uint8_t velocity of note ons
     * @return boost::container::static_vector of MidiEvents
     */
    template<size_t MaxMidiEvents>
    static auto createMidiNoteOnEvents(const polysynth::Musicology::Chord& chord,
                                       uint8_t velocity,
                                       uint8_t channel = MidiEvent::kChannelOmni)
    {
        return createMidiEvents<MaxMidiEvents>(
          chord,
          [channel, velocity](uint8_t note)
          {
              return MidiEvent{ .type = MidiEvent::Type::kNoteOn,
                                .channel = channel,
                                .data = MidiEvent::Data{ .note = MidiEvent::Data::Note{
                                                           .note = note, .velocity = velocity } } };
          });
    }

    /**
     *  Create MIDI aftertouch events based on Musicology::Chord and aftertouch params
     * @tparam MaxMidiEvents max size of the output container
     * @param chord input Musicology::Chord from which events will be created
     * @param polyAftertouch uint8_t polyphonic aftertouch param
     * @param channelAftertouch uint8_t channel aftertouch param
     * @return boost::container::static_vector of MidiEvents
     */
    template<size_t MaxMidiEvents>
    static auto createMidiAftertouchEvents(const polysynth::Musicology::Chord& chord,
                                           uint8_t polyAftertouch,
                                           uint8_t channelAftertouch,
                                           uint8_t channel = MidiEvent::kChannelOmni)
    {
        auto events = createMidiEvents<MaxMidiEvents>(
          chord,
          [channel, polyAftertouch](uint8_t note)
          {
              return MidiEvent{ .type = MidiEvent::Type::kPolyAftertouch,
                                .channel = channel,
                                .data =
                                  MidiEvent::Data{ .note = MidiEvent::Data::Note{
                                                     .note = note, .velocity = polyAftertouch } } };
          });
        events.emplace_back(MidiEvent{
          .type = MidiEvent::Type::kChannelAftertouch,
          .channel = channel,
          .data = MidiEvent::Data{ .channelAftertouch = MidiEvent::Data::ChannelAftertouch{
                                     .velocity = channelAftertouch } } });

        return events;
    }

    /**
     * Create MIDI note offs for Musicology::Chord
     * @tparam MaxMidiEvents max size of the output container
     * @param chord Musicology::Chord from which events will be created
     * @return boost::container::static_vector of MidiEvents
     */
    template<size_t MaxMidiEvents>
    static auto createMidiNoteOffEvents(const polysynth::Musicology::Chord& chord,
                                        uint8_t channel = MidiEvent::kChannelOmni)
    {
        return createMidiEvents<MaxMidiEvents>(
          chord,
          [channel](uint8_t note)
          {
              return MidiEvent{ .type = MidiEvent::Type::kNoteOff,
                                .channel = channel,
                                .data = MidiEvent::Data{
                                  .note = MidiEvent::Data::Note{ .note = note, .velocity = 0 } } };
          });
    }

    /**
     * Create MIDI note offs for a vector of notes
     * @tparam MaxMidiEvents max size of the output container
     * @param notes notes from which events will be created
     * @return boost::container::static_vector of MidiEvents
     */
    template<size_t MaxMidiEvents, size_t NotesMaxSize>
    static auto createMidiNoteOffEvents(
      const boost::container::static_vector<uint8_t, NotesMaxSize>& notes,
      uint8_t channel = MidiEvent::kChannelOmni)
    {
        boost::container::static_vector<MidiEvent, MaxMidiEvents> midiEvents{};
        for (auto noteOff : notes)
        {
            if (midiEvents.size() < MaxMidiEvents)
            {
                midiEvents.emplace_back(
                  MidiEvent{ .type = MidiEvent::Type::kNoteOff,
                             .channel = channel,
                             .data = MidiEvent::Data{
                               .note = MidiEvent::Data::Note{ .note = noteOff, .velocity = 0 } } });
            }
        }
        return midiEvents;
    }

    /**
     * Create midi events from Musicology::Chord based on FuncType function
     * @tparam MaxMidiEvents max size of the output container
     * @tparam FuncType function type deduced from func param. Should be MidiEvent(uint8_t note)
     * @param chord Musicology::Chord from which events will be created
     * @param velocity velocity input parameter
     * @param func function that will create MidiEvent based on note and velocity params:
     * MidiEvent(uint8_t note, uint8_t velocity)
     * @return boost::container::static_vector of MidiEvents
     */
    template<size_t MaxMidiEvents, typename FuncType>
    static auto createMidiEvents(const polysynth::Musicology::Chord& chord, FuncType func)
    {
        boost::container::static_vector<MidiEvent, MaxMidiEvents> midiEvents{};

        for (auto i = 0; i < std::ssize(chord.scheme); i++)
        {
            if (chord.scheme[i])
            {
                auto note = chord.rootNote + i - kChordMaxWidth / 2;
                if (note < kNoteMin || note > kNoteMax)
                {
                    continue;
                }
                if (midiEvents.size() < MaxMidiEvents)
                {
                    midiEvents.emplace_back(func(static_cast<uint8_t>(note)));
                }
                else
                {
                    assert(false);
                    break;
                }
            }
        }
        return midiEvents;
    }
};

} // polysynth
