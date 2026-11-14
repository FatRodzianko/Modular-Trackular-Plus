#pragma once

#include "PolyDSP/Core.h"
#include "PolyDSP/filters/FilterBase.h"
#include "PolyDSP/modulators/Ramp.h"

#include "PolyUtil/misc.h"

#include <algorithm>
#include <boost/container/static_vector.hpp>

// protected members for readibility in derived classes
// NOLINTBEGIN(*-non-private-member-variables-in-classes)

namespace polydsp
{
class SynthBase
{
  public:
    static constexpr param_t kMaxFilterCutoff = FilterBase::kMaxCutoff;

    // clang-format off
    static inline constexpr std::array kFilterTypeNames = {
        "LP MG 24dB",   "LP OB 24dB",   "LP OB 12dB",  "LP SVF 24dB", "LP SVF 12dB",
        "HP OB 24dB",   "HP OB 12dB",   "HP SVF 24dB", "HP SVF 12dB",
        "BP OB 24dB",   "BP OB 12dB",   "BP SVF 24dB", "BP SVF 12dB",
        "Notch SVF 24dB", "Notch SVF 12dB"
    };
    // clang-format on

    enum class PlayMode
    {
        kNormal,
        kNormalPortamentoOverlapping,
        kLegato,
        kLegatoPortamentoOverlapping,
    };

    enum class EnvelopeAction
    {
        kContinue,
        kStop,
        kRetrigger,
    };

    void setPlayMode(PlayMode mode) { this->playMode = mode; };
    void setPortamento(time_t portamentoTime) { this->portamentoTime = portamentoTime; };

    // Following functions are implementations of polysynth::InstrumentVoice
    // corresponding APIs, forwarded from polysynth::BaseSynthVoice

    [[nodiscard]] bool handlesNote(note_t note) const
    {
        return std::ranges::find_if(notesStack,
                                    [note](const auto& data)
                                    { return data.note == note; }) != notesStack.end();
    };
    [[nodiscard]] bool handlesAnyNote() const { return !notesStack.empty(); };
    [[nodiscard]] tiny_optional<note_t> getLastNote() const { return lastNote; };
    [[nodiscard]] tiny_optional<float> getCurrentNoteVelocity() const
    {
        return notesStack.empty() ? std::nullopt : tiny_optional<float>(notesStack.back().velocity);
    };

    void clearPreviousNotes();

    void clockPulse() {}
    void start() {}
    void pitchBendChange(param_t detuneSemitones);

  protected:
    [[nodiscard]] EnvelopeAction noteOn(const RenderContext& ctx, note_t note, param_t velocity);
    [[nodiscard]] EnvelopeAction noteOff(const RenderContext& ctx, note_t note);

    /** Renders pitch modulation for portamento and pitch bend events */
    void renderPitchMod(ModulationBuffer pitchModBuffer);
    [[nodiscard]] note_t getCurrentNote() const { return notesStack.back().note; };

  private:
    static constexpr int kNotesStackSize = 8;

    struct NoteData
    {
        note_t note;
        param_t velocity;
    };

    time_t portamentoTime = 0;
    tiny_optional<time_t> lastNoteFreq = std::nullopt;
    tiny_optional<note_t> lastNote = std::nullopt;
    boost::container::static_vector<NoteData, kNotesStackSize> notesStack;
    PlayMode playMode = PlayMode::kNormal;
    bool portamentoOverlapOnly = true;
    bool portamentoActive = false;
    Ramp portamentoRamp;
    float pitchMult = 1.0f;

    void triggerPortamento(const RenderContext& ctx, time_t noteFreq);
    [[nodiscard]] inline bool isLegatoMode() const
    {
        return playMode == PlayMode::kLegato || playMode == PlayMode::kLegatoPortamentoOverlapping;
    };
};

inline void SynthBase::pitchBendChange(param_t detuneSemitones)
{
    pitchMult = polydsp::detuneMult(detuneSemitones);
}

// NOLINTEND(*-non-private-member-variables-in-classes)

}