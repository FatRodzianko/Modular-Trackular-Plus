#pragma once
#include "PolySynth/MidiSource.h"
#include "PolySynth/Musicology.h"
#include "boost/container/static_vector.hpp"

namespace polysynth
{

struct BaseStep
{
  public:
    static constexpr int kMaxMidiEventsPerStep = 8;
    uint16_t length = 1;

    /// used to identify particular step in arpeggio
    uint8_t id = 0;

    explicit BaseStep(uint16_t length, uint8_t id = 0)
      : length(length)
      , id(id)
    {
    }
};

struct Step final : public BaseStep
{
  public:
    /// a struct that consists of data used to generate MIDI events
    struct MidiEventsData
    {
        polysynth::Musicology::Chord chord;
        uint8_t velocity;
        bool operator==(const MidiEventsData& rhs) const
        {
            return chord == rhs.chord && velocity == rhs.velocity;
        }
        bool operator!=(const MidiEventsData& rhs) const { return !(rhs == *this); }
    };

    MidiEventsData events;

    Step(uint16_t length, MidiEventsData events, uint8_t id = 0)
      : BaseStep(length, id)
      , events(events)
    {
    }

    bool operator==(const Step& rhs) const
    {
        return length == rhs.length && id == rhs.id && events == rhs.events;
    }
    bool operator!=(const Step& rhs) const { return !(rhs == *this); }
};

}