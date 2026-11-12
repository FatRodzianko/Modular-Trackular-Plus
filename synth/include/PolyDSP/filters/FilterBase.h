#pragma once

#include "PolyDSP/Core.h"
#include "PolyDSP/ModMatrix.h"

// NOLINTBEGIN(*-non-private-member-variables-in-classes)

namespace polydsp
{
class FilterBase
{
  public:
    static constexpr param_t kMaxCutoff = 20000;

    enum class Shape
    {
        kLPF,
        kHPF,
        kBPF,
        kBSF, // band-stop
    };

    void noteOn(note_t note) { noteFreq = polydsp::noteToFreq(note); };

    void setFrequencyModDestinationId(int id) { frequencyModDestinationId = id; };
    [[nodiscard]] int getFrequencyModDestinationId() const { return frequencyModDestinationId; };
    void setResonance(param_t resonance) { this->resonance = resonance; };
    [[nodiscard]] param_t getResonance() const { return resonance; };
    void setNoteTracking(param_t tracking) { this->noteTracking = tracking; };
    [[nodiscard]] param_t getNoteTracking() const { return noteTracking; };

  protected:
    // F4 is the ref pitch because of an early bug (invalid C4 constant)
    // changing this would break all the patches
    static inline const time_t kRefNoteFreq = polydsp::noteToFreq(kNoteC4 + 5);

    int frequencyModDestinationId = -1;

    time_t noteFreq = 0;
    param_t resonance = 0;
    param_t noteTracking = 1.0f;

    [[nodiscard]] inline param_t calculateNoteTrackingFreq() const
    {
        return (noteFreq - kRefNoteFreq) * noteTracking;
    };
};
}

// NOLINTEND(*-non-private-member-variables-in-classes)
