#pragma once

#include "PolySynth/config.h"
#include "Sequence.h"

#include <tuple>

namespace polysynth
{

struct NoteOffEvent
{
    static constexpr int kMaxNumberOfNoteOffs = kMaxInstrumentVoices + 4;
    using StepMidiEvents = boost::container::static_vector<uint8_t, kMaxNumberOfNoteOffs>;
    StepMidiEvents events{};
};

class NoteOffEvents : public Sequence<NoteOffEvent>
{
  public:
    /**
     * Inserts or appends note offs to the container based on the position "itemPos".
     * If at given position for given instrumentId there are already NoteOffs then this functions
     * appends to existing.
     * @param noteOffsPerInstrument NoteOffEvent
     * @param itemPos position to put or append the note offs
     */
    void insert(const NoteOffEvent& noteOffsPerInstrument, seqpos_t itemPos)
    {
        if (empty())
        {
            getSteps().push_back({ itemPos, { noteOffsPerInstrument } });
            return;
        }
        int stepIdx = 0;
        for (; stepIdx < ssize(); stepIdx++)
        {
            if (getSteps()[stepIdx].first == itemPos)
            {
                addToExisting(noteOffsPerInstrument, stepIdx);
                return;
            }
            if (getSteps()[stepIdx].first > itemPos)
            {
                if (stepIdx + 1 < kMaxSequenceLength)
                {
                    getSteps().insert(getSteps().begin() + stepIdx,
                                      { itemPos, { noteOffsPerInstrument } });
                }
                return;
            }
        }

        assert(stepIdx < kMaxSequenceLength);
        if (stepIdx == ssize())
        {
            getSteps().push_back({ itemPos, { noteOffsPerInstrument } });
        }
    }

    void addToExisting(const NoteOffEvent& noteOffs, int stepIdx)
    {
        assert(std::ssize(noteOffs.events) + std::ssize(getSteps()[stepIdx].second.events) <
               NoteOffEvent::kMaxNumberOfNoteOffs);
        std::ranges::copy(noteOffs.events, std::back_inserter(getSteps()[stepIdx].second.events));
    }

    void clear() { getSteps().clear(); }
};

}