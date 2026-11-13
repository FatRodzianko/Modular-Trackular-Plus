#include "PolySynth/Musicology.h"

#include "fmt/core.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <ranges>
#include <set>
#include <string>

#include <unordered_map>

#include "PolyUtil/bitset.h"

namespace polysynth
{

struct NoteAlternatives
{
    int noteIndex;
    int alt1;
    int alt2;
    bool operator==(const NoteAlternatives& other) const = default;
};

const std::array<const char*, Musicology::kScalesCount> Musicology::kScaleNames{
    "Chromatic",
    "Minor",
    "Major",
    "Dorian",
    "Lydian Major",
    "Lydian Minor",
    "Locrian",
    "Phrygian ",
    "Phrygian Dominant",
    "Mixlydian",
    "Melodic Minor",
    "Harmonic Minor",
    "BeBop Major",
    "BeBop Dorian",
    "BeBop Mixolydian",
    "Blues Minor",
    "Blues Major",
    "Pentatonic Minor",
    "Pentatonic Major",
    "Hungarian Minor",
    "Ukrainian",
    "Marva",
    "Whole Tone",
    "Diminish",
    "Super Locrian",
    "Hirajoshi",
    "In Sen",
    "Yo",
    "Iwato",
    "Kumoi",
    "Overtone",
    "Double Harmonic",
    "Indian",
    "Neapolitan Major",
    "Neapolitan Minor",
    "Enigmatic",
};

[[maybe_unused]] static constexpr std::array kScaleTypeNotes{
    Musicology::kChromaticScale,     // 0  Chromatic
    scalescheme_t{ 0b010110101101 }, // 1  Minor
    scalescheme_t{ 0b101010110101 }, // 2  Major
    scalescheme_t{ 0b011010101101 }, // 3  Dorian
    scalescheme_t{ 0b101011010101 }, // 4  Lyd Maj
    scalescheme_t{ 0b010111010101 }, // 5  Lyd Min
    scalescheme_t{ 0b010101101011 }, // 6  Locrian
    scalescheme_t{ 0b010110101011 }, // 7  Phrygian
    scalescheme_t{ 0b010110110011 }, // 8  PhrygDom
    scalescheme_t{ 0b011010110101 }, // 9  Mixlydian
    scalescheme_t{ 0b101010101101 }, // 10 Melo Min
    scalescheme_t{ 0b100110101101 }, // 11 Harm Min
    scalescheme_t{ 0b101110110101 }, // 12 BeBopMaj
    scalescheme_t{ 0b011010111101 }, // 13 BeBopDor
    scalescheme_t{ 0b111010110101 }, // 14 BeBopMix
    scalescheme_t{ 0b010011101001 }, // 15 Blues Min
    scalescheme_t{ 0b001010011101 }, // 16 Blues Maj
    scalescheme_t{ 0b010010101001 }, // 17 Penta Min
    scalescheme_t{ 0b001010010101 }, // 18 Penta Maj
    scalescheme_t{ 0b100111001101 }, // 19 Hung Min
    scalescheme_t{ 0b011011001101 }, // 20 Ukrainian
    scalescheme_t{ 0b101011010011 }, // 21 Marva
    scalescheme_t{ 0b010101010101 }, // 22 Wholetone
    scalescheme_t{ 0b101101101101 }, // 23 Dim
    scalescheme_t{ 0b010101011011 }, // 24 SLocrian
    scalescheme_t{ 0b000110001101 }, // 25 Hirajoshi
    scalescheme_t{ 0b010010100011 }, // 26 In Sen
    scalescheme_t{ 0b001010100101 }, // 27 Yo
    scalescheme_t{ 0b010001100011 }, // 28 Iwato
    scalescheme_t{ 0b001010001101 }, // 29 Kumoi
    scalescheme_t{ 0b011011010101 }, // 30 Overtone
    scalescheme_t{ 0b100110110011 }, // 31 DoubHarm
    scalescheme_t{ 0b010110110001 }, // 32 Indian
    scalescheme_t{ 0b101010101011 }, // 33 NeapoMaj
    scalescheme_t{ 0b100110101011 }, // 34 NeapoMin
    scalescheme_t{ 0b110101010011 }, // 35 Enigmatic
};

[[maybe_unused]] const std::array<size_t, kScaleTypeNotes.size()> kNotesCountInScales{
    bitset::countSetBits(kScaleTypeNotes[0], 0, kScaleTypeNotes[0].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[1], 0, kScaleTypeNotes[1].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[2], 0, kScaleTypeNotes[2].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[3], 0, kScaleTypeNotes[3].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[4], 0, kScaleTypeNotes[4].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[5], 0, kScaleTypeNotes[5].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[6], 0, kScaleTypeNotes[6].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[7], 0, kScaleTypeNotes[7].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[8], 0, kScaleTypeNotes[8].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[9], 0, kScaleTypeNotes[9].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[10], 0, kScaleTypeNotes[10].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[11], 0, kScaleTypeNotes[11].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[12], 0, kScaleTypeNotes[12].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[13], 0, kScaleTypeNotes[13].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[14], 0, kScaleTypeNotes[14].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[15], 0, kScaleTypeNotes[15].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[16], 0, kScaleTypeNotes[16].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[17], 0, kScaleTypeNotes[17].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[18], 0, kScaleTypeNotes[18].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[19], 0, kScaleTypeNotes[19].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[20], 0, kScaleTypeNotes[20].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[21], 0, kScaleTypeNotes[21].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[22], 0, kScaleTypeNotes[22].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[23], 0, kScaleTypeNotes[23].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[24], 0, kScaleTypeNotes[24].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[25], 0, kScaleTypeNotes[25].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[26], 0, kScaleTypeNotes[26].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[27], 0, kScaleTypeNotes[27].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[28], 0, kScaleTypeNotes[28].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[29], 0, kScaleTypeNotes[29].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[30], 0, kScaleTypeNotes[30].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[31], 0, kScaleTypeNotes[31].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[32], 0, kScaleTypeNotes[32].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[33], 0, kScaleTypeNotes[33].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[34], 0, kScaleTypeNotes[34].size() - 1),
    bitset::countSetBits(kScaleTypeNotes[35], 0, kScaleTypeNotes[35].size() - 1),
};

[[maybe_unused]] const std::unordered_map<scalescheme_t, size_t> kScaleSchemeIndices{
    { kScaleTypeNotes[0], 0 },   { kScaleTypeNotes[1], 1 },   { kScaleTypeNotes[2], 2 },
    { kScaleTypeNotes[3], 3 },   { kScaleTypeNotes[4], 4 },   { kScaleTypeNotes[5], 5 },
    { kScaleTypeNotes[6], 6 },   { kScaleTypeNotes[7], 7 },   { kScaleTypeNotes[8], 8 },
    { kScaleTypeNotes[9], 9 },   { kScaleTypeNotes[10], 10 }, { kScaleTypeNotes[11], 11 },
    { kScaleTypeNotes[12], 12 }, { kScaleTypeNotes[13], 13 }, { kScaleTypeNotes[14], 14 },
    { kScaleTypeNotes[15], 15 }, { kScaleTypeNotes[16], 16 }, { kScaleTypeNotes[17], 17 },
    { kScaleTypeNotes[18], 18 }, { kScaleTypeNotes[19], 19 }, { kScaleTypeNotes[20], 20 },
    { kScaleTypeNotes[21], 21 }, { kScaleTypeNotes[22], 22 }, { kScaleTypeNotes[23], 23 },
    { kScaleTypeNotes[24], 24 }, { kScaleTypeNotes[25], 25 }, { kScaleTypeNotes[26], 26 },
    { kScaleTypeNotes[27], 27 }, { kScaleTypeNotes[28], 28 }, { kScaleTypeNotes[29], 29 },
    { kScaleTypeNotes[30], 30 }, { kScaleTypeNotes[31], 31 }, { kScaleTypeNotes[32], 32 },
    { kScaleTypeNotes[33], 33 }, { kScaleTypeNotes[34], 34 }, { kScaleTypeNotes[35], 35 },
};

const std::array kChordSchemes{
    Musicology::kEmptyChordScheme,     // 0, None
    Musicology::rootNoteChordScheme(), // 1, Root
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000000000100001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 2, Pow4th
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000000010000001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 3, Pow5th
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000000010001001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 4, Minor
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000000010010001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 5, Major
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000000001001001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 6, Dim
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000000100010001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 7, Aug
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000000010000101000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 8, Sus2
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000000010100001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 9, Sus4
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000100010010001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 10, Maj7
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000010010001001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 11, Min7
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000010010010001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 12, Dom7
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000010001001001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 13, Min7b5
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000001010010001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 14, Maj6
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000001010001001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 15, Min6
    chordscheme_t{ "0000000000000000000000000000000000000000000000000000100010001001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 16, MinMaj7
    chordscheme_t{ "0000000000000000000000000000000000000000000000000100100010010001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 17, Maj9
    chordscheme_t{ "0000000000000000000000000000000000000000000000000100010010001001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 18, Min9
    chordscheme_t{ "0000000000000000000000000000000000000000000000100100100010010001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 19, Maj11
    chordscheme_t{ "0000000000000000000000000000000000000000000000100100010010001001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 20, Min11
    chordscheme_t{ "0000000000000000000000000000000000000000001000100100100010010001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 21, Maj13
    chordscheme_t{ "0000000000000000000000000000000000000000001000100100010010001001000000000000000"
                   "0000000000000000000000000000000000000000000000000" }, // 22, Min13
};

constexpr std::array kBasicChordNames{
    "Empty",   "None", "Pow4th", "Pow5th", "Minor", "Major",  "Dim",   "Aug",
    "Sus2",    "Sus4", "Maj7",   "Min7",   "Dom7",  "Min7b5", "Maj6",  "Min6",
    "MinMaj7", "Maj9", "Min9",   "Maj11",  "Min11", "Maj13",  "Min13",
};
static_assert(kBasicChordNames.size() == kChordSchemes.size());

// TODO(karol): arrays below can be probably upgraded to
// unordered_map<"normalized chord", "preference of being chosen">
/**
 * 2 note chord indices. Sorted by preference of being chosen by align algorithm
 */
static constexpr std::array kTwoNoteChordIndices{
    1,
    2,
};

/**
 * 3 note chord indices. Sorted by preference of being chosen by align algorithm
 */
static constexpr std::array kThreeNoteChordIndices{
    3, 4, 5, 6, 7, 8,
};

/**
 * 4 note chord indices. Sorted by preference of being chosen by align algorithm
 */
static constexpr std::array kFourNoteChordIndices{
    9, 10, 11, 12, 13, 14, 15,
};

/**
 * 5 note chord indices. Sorted by preference of being chosen by align algorithm
 */
static constexpr std::array kFiveNoteChordIndices{ 16, 17 };

chordscheme_t normalizeScheme(chordscheme_t chord)
{
    chordscheme_t result;
    constexpr auto octavesInHalf = (kChordMaxWidth / 2) / kOneOctaveNotes;
    constexpr auto rest = kChordMaxWidth / 2 - octavesInHalf * kOneOctaveNotes;
    result |= ((chord >> (kChordMaxWidth - rest)) << (kChordMaxWidth / 2));
    for (int i = 0; i < 2 * octavesInHalf; i++)
    {
        result |= ((chord << (kOneOctaveNotes * i + rest)) >> (kChordMaxWidth - kOneOctaveNotes))
                  << (kChordMaxWidth / 2);
    }
    result |= ((chord << (kChordMaxWidth - rest)) >> (kChordMaxWidth / 2 - kOneOctaveNotes));
    return result;
}

std::vector<NoteAlternatives> determineChordAlternatives(const Musicology::Chord& chord,
                                                         std::bitset<kChordMaxWidth> scaleExpanded)
{
    auto diffBits = (chord.scheme ^ (scaleExpanded)) & chord.scheme;
    std::vector<NoteAlternatives> alternatives;
    for (int i = 0; i < kChordMaxWidth; i++)
    {
        if (diffBits.test(i))
        {
            auto down = bitset::findSetBit(scaleExpanded, i, 0, true);
            auto up = bitset::findSetBit(scaleExpanded, i, 0, false);
            if (up && down)
            {
                alternatives.push_back({ i, up.value(), down.value() });
            }
        }
    }
    return alternatives;
}

template<typename ChordIndicesArray>
tiny_optional<int> findChordIdxByNoteCount(chordscheme_t scheme, ChordIndicesArray chordsIndices)
{
    auto elementIt = std::ranges::find_if(
      chordsIndices,
      [scheme](auto basicChordIdx)
      { return normalizeScheme(kChordSchemes.at(basicChordIdx)) == normalizeScheme(scheme); });
    if (elementIt != chordsIndices.end())
    {
        return *elementIt;
    }
    return {};
}

/**
 * Determines chord priority using defined arrays (k[Two/Three/Four/Five]NoteChordsIndices)
 * TODO(karol): In future those arrays can be substited by std::unordered map
 */
tiny_optional<int> chordPriority(const chordscheme_t& scheme)
{
    int notesCount = 0;
    for (size_t i = 0; i < scheme.size(); i++)
    {
        if (scheme.test(i))
        {
            notesCount++;
        }
    }

    if (notesCount == 2)
    {
        auto element = findChordIdxByNoteCount(scheme, kTwoNoteChordIndices);
        if (element)
        {
            return element;
        }
    }
    else if (notesCount == 3)
    {
        auto element = findChordIdxByNoteCount(scheme, kThreeNoteChordIndices);
        if (element)
        {
            return element;
        }
    }
    else if (notesCount == 4)
    {
        auto element = findChordIdxByNoteCount(scheme, kFourNoteChordIndices);
        if (element)
        {
            return element;
        }
    }
    else if (notesCount == 5) // NOLINT(*-magic-numbers)
    {
        auto element = findChordIdxByNoteCount(scheme, kFiveNoteChordIndices);
        if (element)
        {
            return element;
        }
    }
    return {};
}

using ChordPriorityPair = std::pair<chordscheme_t, int>;
using PrioritizedChordSet =
  std::set<ChordPriorityPair,
           decltype([](ChordPriorityPair a, ChordPriorityPair b) { return a.second < b.second; })>;

/**
 * Finds alternative chords based on @alternatives.
 * The @result is a set sorted by priority
 * Priority is determined by chordPriority function
 * this function operates in O(2^(N)). Where N = alternatives.size()
 */
void recursiveFindChordAlternatives(const std::vector<NoteAlternatives>& alternatives,
                                    int altIndex,
                                    chordscheme_t& alt,
                                    PrioritizedChordSet& result)
{
    if (altIndex >= std::ssize(alternatives) - 1)
    {
        alt.set(alternatives[altIndex].noteIndex, false);
        alt.set(alternatives[altIndex].alt1, true);
        result.insert({ alt, chordPriority(alt).value_or(std::numeric_limits<int>::max()) });

        alt.set(alternatives[altIndex].noteIndex, false);
        alt.set(alternatives[altIndex].alt1, false);
        alt.set(alternatives[altIndex].alt2, true);
        result.insert({ alt, chordPriority(alt).value_or(std::numeric_limits<int>::max()) });
    }
    else
    {
        alt.set(alternatives[altIndex].noteIndex, false);
        alt.set(alternatives[altIndex].alt1, true);
        recursiveFindChordAlternatives(alternatives, altIndex + 1, alt, result);

        alt.set(alternatives[altIndex].noteIndex, false);
        alt.set(alternatives[altIndex].alt1, false);
        alt.set(alternatives[altIndex].alt2, true);
        recursiveFindChordAlternatives(alternatives, altIndex + 1, alt, result);
    }
}

/**
 * Finds best alternative for @scheme based on @alternatives definition
 * Calls recursive function.
 * Best is determined by k[Two/Three/Four/Five]NoteChordsIndices which are ordered by priority
 */
chordscheme_t findBestAlternative(const chordscheme_t& scheme,
                                  const std::vector<NoteAlternatives>& alternatives)
{
    if (alternatives.empty())
    {
        return scheme;
    }
    PrioritizedChordSet prioritizedChords;
    auto tmp = scheme;

    recursiveFindChordAlternatives(alternatives, 0, tmp, prioritizedChords);

    assert(!prioritizedChords.empty());

    return (*prioritizedChords.begin()).first;
}

std::string Musicology::noteToString(polysynth::note_t note, bool includeOctave)
{
    int noteInOctave = note % polysynth::kOneOctaveNotes;
    if (includeOctave)
    {
        int octave = (note - polysynth::kNoteC0) / polysynth::kOneOctaveNotes;
        return fmt::format("{}{}", kNoteSymbols.at(noteInOctave), octave);
    }
    return kNoteSymbols.at(noteInOctave);
}

Musicology::Chord Musicology::alignToScale(const Musicology::Chord& chord,
                                           const Musicology::Scale& scale)
{
    auto diff = scale.rootNote - chord.rootNote;
    auto scaleRot = bitset::rotateLeft(
      scale.scheme, kChordMaxWidth / 2 + kOneOctaveNotes + (diff % kOneOctaveNotes));
    auto scaleExpanded = bitset::expand<kOneOctaveNotes, kChordMaxWidth>(scaleRot);
    auto alternatives = determineChordAlternatives(chord, scaleExpanded);
    auto best = findBestAlternative(chord.scheme, alternatives);
    return Musicology::Chord{ .scheme = best, .rootNote = chord.rootNote };
}

void Musicology::transpose(polysynth::Musicology::Chord& chord,
                           const polysynth::Musicology::Scale& scale,
                           int delta)
{
    Musicology::Chord result;
    auto nearestStepNote = polysynth::Musicology::getNoteByInterval(scale, chord.rootNote, delta)
                             .value_or(chord.rootNote);
    result.rootNote = std::clamp(nearestStepNote, polysynth::kNoteMin, polysynth::kNoteMax);
    for (auto note : getNotesView(chord))
    {
        auto newNote = polysynth::Musicology::getNoteByInterval(scale, note, delta);
        if (newNote && isInRange(result, *newNote))
        {
            result.scheme.set(kChordMaxWidth / 2 - result.rootNote + *newNote);
        }
    }
    chord = result;
}

void Musicology::transpose(polysynth::Musicology::Chord& chord, int delta)
{
    chord.rootNote = std::clamp(chord.rootNote + delta, polysynth::kNoteMin, polysynth::kNoteMax);
}

void Musicology::alignToScale(polysynth::Musicology::Chord& chord,
                              const polysynth::Musicology::Scale& fromScale,
                              const polysynth::Musicology::Scale& toScale)
{
    Musicology::Chord result;
    auto nearestStepNote = polysynth::Musicology::getNoteByInterval(fromScale, chord.rootNote, 0)
                             .value_or(chord.rootNote);
    result.rootNote = polysynth::Musicology::sameDegreeNote(fromScale, toScale, nearestStepNote);
    if (notesCount(fromScale.scheme) == notesCount(toScale.scheme))
    {
        for (auto note : getNotesView(chord))
        {
            auto newNote = polysynth::Musicology::transpose(note, fromScale, toScale, 0);
            if (isInRange(result, newNote))
            {
                result.scheme.set(kChordMaxWidth / 2 - result.rootNote + newNote);
            }
        }
    }
    else
    {
        result = polysynth::Musicology::alignToScale(chord, toScale);
    }
    chord = result;
}

int Musicology::getDegreeDiff(const Musicology::Scale& scale, note_t fromNote, note_t toNote)
{
    auto expanded = bitset::expand<kOneOctaveNotes, kNoteMax - kNoteMin + 1>(
      bitset::rotateLeft(scale.scheme, scale.rootNote % kOneOctaveNotes));
    if (toNote >= fromNote)
    {
        return static_cast<int>(bitset::countSetBits(expanded, fromNote, toNote) - 1);
    }
    return static_cast<int>(-(bitset::countSetBits(expanded, toNote, fromNote) - 1));
}

tiny_optional<note_t> Musicology::getNoteByInterval(const Musicology::Scale& scale,
                                                    note_t refNote,
                                                    int interval)
{
    auto expanded = bitset::expand<kOneOctaveNotes, kNoteMax - kNoteMin + 1>(
      bitset::rotateLeft(scale.scheme, scale.rootNote % kOneOctaveNotes));
    if (refNote > kNoteMax)
    {
        return bitset::findSetBit(expanded, kNoteMax, 1, false);
    }
    if (refNote < kNoteMin)
    {
        return bitset::findSetBit(expanded, kNoteMin, 1, true);
    }
    if (interval == 0)
    {
        auto nearestUp = bitset::findSetBit(expanded, refNote, 0, true);
        auto nearestDown = bitset::findSetBit(expanded, refNote, 0, false);
        if (nearestUp && nearestDown)
        {
            if (*nearestUp - refNote < refNote - *nearestDown)
            {
                return *nearestUp;
            }
            return *nearestDown;
        }
        if (nearestUp)
        {
            return *nearestUp;
        }
        return nearestDown;
    }
    auto nearest = bitset::findSetBit(expanded, refNote, std::abs(interval), interval > 0);
    return nearest;
}

note_t Musicology::sameDegreeNote(const Scale& fromScale, const Scale& toScale, note_t note)
{
    assert(isInScale(note, fromScale));
    if (notesCount(fromScale.scheme) == notesCount(toScale.scheme))
    {

        auto octave = (note - fromScale.rootNote) / kOneOctaveNotes;
        auto degree = bitset::countSetBits(fromScale.scheme,
                                           0,
                                           std::abs(fromScale.rootNote - note) % kOneOctaveNotes) -
                      1;
        auto noteInScale =
          bitset::findSetBit(toScale.scheme, 0, static_cast<int>(degree), true).value();
        return toScale.rootNote + octave * kOneOctaveNotes + noteInScale;
    }
    auto transpose = (fromScale.rootNote >= toScale.rootNote)
                       ? fromScale.rootNote - toScale.rootNote
                       : toScale.rootNote - fromScale.rootNote;
    auto transposedNote = note + transpose;
    if (!polysynth::Musicology::isInScale(transposedNote, toScale))
    {
        transposedNote = polysynth::Musicology::getNoteByInterval(toScale, transposedNote, 0)
                           .value_or(transposedNote);
    }
    return transposedNote;
}

/**
 * Creates a name for a chord.
 * Format: notes separated by comma. Ex: C 1 E 1 G#2
 */
std::string createNameForUnknownChord(const Musicology::Chord& chord)
{
    std::string chordName;
    bool comma = false;
    for (int i = 0; i < kChordMaxWidth; i++)
    {
        if (chord.scheme.test(i))
        {
            if (comma)
            {
                chordName.append(",");
            }
            else
            {
                comma = true;
            }
            auto note = chord.rootNote + i - kChordMaxWidth / 2;
            if (note >= kNoteMin && note <= kNoteMax)
            {
                chordName.append(Musicology::noteToString(note));
            }
        }
    }
    return chordName;
}

std::string Musicology::chordName(const Musicology::Chord& chord)
{
    auto normalized = normalizeScheme(chord.scheme);
    const auto* elementIt = std::ranges::find_if(
      kChordSchemes, [&](auto element) { return normalizeScheme(element) == normalized; });
    std::string name;
    if (elementIt != kChordSchemes.end())
    {
        auto chordIdx = elementIt - kChordSchemes.begin();
        name = std::string(kBasicChordNames.at(chordIdx));
    }
    else
    {
        name = createNameForUnknownChord(chord);
    }
    return name;
}

std::string Musicology::scaleName(const Musicology::Scale& scale)
{
    return noteToString(scale.rootNote, false) + scaleSchemeName(scale.scheme);
}

std::string Musicology::scaleSchemeName(const scalescheme_t& scaleScheme)
{
    auto findIt = kScaleSchemeIndices.find(scaleScheme);
    if (findIt != kScaleSchemeIndices.end())
    {
        return kScaleNames.at(findIt->second);
    }

    return "unknown";
}

Musicology::Chord Musicology::normalize(const Musicology::Chord& chord)
{
    return Musicology::Chord{ normalizeScheme(chord.scheme), chord.rootNote };
}
bool Musicology::isInScale(const Musicology::Chord& chord, const Musicology::Scale& scale)
{
    int noteInOctave = chord.rootNote % polysynth::kOneOctaveNotes;
    auto diff = scale.rootNote - noteInOctave;
    auto scaleRot = bitset::rotateLeft(
      scale.scheme, kChordMaxWidth / 2 + kOneOctaveNotes + (diff % kOneOctaveNotes));
    auto scaleExpaned = bitset::expand<kOneOctaveNotes, kChordMaxWidth>(scaleRot);

    return (chord.scheme & scaleExpaned) == chord.scheme;
}

bool Musicology::isInScale(note_t note, const Scale& scale)
{

    int noteInOctave = note % kOneOctaveNotes;
    auto scaleRot = bitset::rotateLeft(scale.scheme, scale.rootNote);

    return scaleRot.test(noteInOctave);
}

chordscheme_t Musicology::getChordSchemeRelativeTo(const chordscheme_t& chordScheme, int delta)
{
    const auto* elementIt = std::ranges::find(kChordSchemes, chordScheme);
    auto chordIdx = 1;
    if (elementIt != kChordSchemes.end())
    {
        chordIdx = std::clamp(static_cast<int>(elementIt - kChordSchemes.begin() + delta),
                              1,
                              static_cast<int>(kChordSchemes.size() - 1));
    }
    return kChordSchemes.at(chordIdx);
}

#ifdef __clang__
// placeholder for clang-tidy because of a bug in clang
// https://stackoverflow.com/questions/72716894/how-to-use-stdviewstransform-on-a-vector
chordscheme_t Musicology::getChordSchemeRelativeTo(const Scale& /*unused*/,
                                                   const Chord& /*unused*/,
                                                   int /*unused*/)
{
    assert(false);
    return 0;
}
#else
chordscheme_t Musicology::getChordSchemeRelativeTo(const Scale& scale,
                                                   const Chord& chord,
                                                   int delta)
{
    const auto* elementIt = std::ranges::find(kChordSchemes, chord.scheme);
    auto chordIdx = 1;
    auto counter = 0;
    if (elementIt != kChordSchemes.end())
    {
        int elementIdx = static_cast<int>(elementIt - kChordSchemes.begin());
        auto isInScale = [rootNote = chord.rootNote, &scale](auto& chordScheme) {
            return Musicology::isInScale(Musicology::Chord{ chordScheme, rootNote }, scale);
        };
        auto isNotEmpty = [](auto& chordScheme)
        { return chordScheme != Musicology::kEmptyChordScheme; };
        chordscheme_t result = *elementIt;
        if (delta >= 0)
        {
            for (const auto& chordScheme :
                 (kChordSchemes |
                  std::views::filter([idx = 0, elementIdx](auto) mutable
                                     { return idx++ > elementIdx; }) |
                  std::views::filter(isInScale) | std::views::filter(isNotEmpty)))
            {
                result = chordScheme;
                if (++counter >= delta)
                {
                    break;
                }
            }
        }
        else
        {
            for (const auto& chordScheme :
                 (kChordSchemes | std::ranges::views::reverse |
                  std::views::filter([idx = 0, elementIdx](auto) mutable
                                     { return idx++ >= std::ssize(kChordSchemes) - elementIdx; }) |
                  std::views::filter(isInScale) | std::views::filter(isNotEmpty)))
            {
                result = chordScheme;
                if (++counter >= std::abs(delta))
                {
                    break;
                }
            }
        }
        return result;
    }
    return kChordSchemes.at(chordIdx);
}
#endif

scalescheme_t Musicology::getScaleSchemeRelativeTo(const scalescheme_t& scaleScheme, int delta)
{
    const auto* elementIt = std::ranges::find(kScaleTypeNotes, scaleScheme);
    auto chordIdx = 0;
    if (elementIt != kScaleTypeNotes.end())
    {
        chordIdx = std::clamp(static_cast<int>(elementIt - kScaleTypeNotes.begin() + delta),
                              0,
                              static_cast<int>(kScaleTypeNotes.size() - 1));
    }
    return kScaleTypeNotes.at(chordIdx);
}

Musicology::Scale Musicology::createScale(uint32_t scaleTypeIdx, note_t rootNote)
{

    return Scale{ kScaleTypeNotes.at(scaleTypeIdx), rootNote % kOneOctaveNotes };
}

size_t Musicology::notesCount(const scalescheme_t& scaleScheme)
{
    auto it = kScaleSchemeIndices.find(scaleScheme);
    if (it != kScaleSchemeIndices.end())
    {
        return kNotesCountInScales.at(it->second);
    }
    return bitset::countSetBits(scaleScheme, 0, scaleScheme.size() - 1);
}

void Musicology::toggleChordNote(Musicology::Chord& chord, int notePosition)
{
    auto noteIndex = kChordMaxWidth / 2 + notePosition;
    if (noteIndex >= 0 && noteIndex < kChordMaxWidth)
    {
        chord.scheme.flip(noteIndex);
    }
}

bool Musicology::containsNote(const Musicology::Chord& chord, note_t note)
{
    if (!isInRange(chord, note))
    {
        return false;
    }
    return chord.scheme.test(kChordMaxWidth / 2 - chord.rootNote + note);
}

bool Musicology::isInRange(const Musicology::Chord& chord, note_t note)
{
    if (note < 0 || note > kNoteMax)
    {
        return false;
    }
    auto noteIndex = kChordMaxWidth / 2 - chord.rootNote + note;
    return (noteIndex >= 0 && noteIndex < kChordMaxWidth);
}
bool Musicology::isWhiteKey(note_t note)
{
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    switch (note % kOneOctaveNotes)
    {
        case 1:
        case 3:
        case 6:
        case 8:
        case 10:
            return false;
        default:
            return true;
    }
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    return false;
}

const chordscheme_t& Musicology::rootNoteChordScheme()
{
    static chordscheme_t rootNoteScheme{
        "000000000000000000000000000000000000000000000000000000000000000100000000000000000"
        "00000000000000000000000000000000000000000000000"
    };
    return rootNoteScheme;
}

std::optional<chordscheme_t> Musicology::getChordSchemeByName(std::string_view name)
{
    const auto* iter = std::ranges::find(kBasicChordNames, name);
    if (iter != kBasicChordNames.end())
    {
        size_t index = std::distance(kBasicChordNames.begin(), iter);
        return kChordSchemes.at(index);
    }
    return {};
}

void Musicology::addToKeyboardScheme(keyboardscheme_t& keyboardScheme,
                                     const Musicology::Chord& chord)
{
    if (kChordMaxWidth / 2 < chord.rootNote)
    {
        keyboardScheme |= chord.scheme << (chord.rootNote - kChordMaxWidth / 2);
    }
    else
    {
        keyboardScheme |= chord.scheme >> (kChordMaxWidth / 2 - chord.rootNote);
    }
}

void Musicology::removeFromKeyboardScheme(keyboardscheme_t& keyboardScheme,
                                          const Musicology::Chord& chord)
{
    if (kChordMaxWidth / 2 < chord.rootNote)
    {
        keyboardScheme &= ~(chord.scheme << (chord.rootNote - kChordMaxWidth / 2));
    }
    else
    {
        keyboardScheme &= ~(chord.scheme >> (kChordMaxWidth / 2 - chord.rootNote));
    }
}

int Musicology::transpose(note_t note,
                          const Musicology::Scale& fromScale,
                          const Musicology::Scale& toScale,
                          int semitones)
{
    auto degreeDiff = Musicology::getDegreeDiff(fromScale, fromScale.rootNote, note);
    return Musicology::getNoteByInterval(toScale, toScale.rootNote + semitones, degreeDiff)
      .value_or(note + semitones);
}
}