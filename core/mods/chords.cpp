// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "chords.h"

#if MT_HAS_CHORDS

namespace mods
{
namespace chords
{

// the note (bit 0) and one to three intervals above it, in semitones
constexpr uint16_t M1(unsigned a)
{
    return static_cast<uint16_t>(1u | 1u << a);
}

constexpr uint16_t M2(unsigned a, unsigned b)
{
    return static_cast<uint16_t>(M1(a) | 1u << b);
}

constexpr uint16_t M3(unsigned a, unsigned b, unsigned c)
{
    return static_cast<uint16_t>(M2(a, b) | 1u << c);
}

// Values are saved in patterns: a value never changes meaning once released.
const uint16_t kLibrary[kCount] = {
    M2(3, 8),          /*  30 " 38" major, 1st inversion */
    M2(5, 9),          /*  31 " 59" major, 2nd inversion */
    M2(4, 9),          /*  32 " 49" minor, 1st inversion */
    M2(5, 8),          /*  33 " 58" minor, 2nd inversion */
    M3(3, 7, 8),       /*  34 "378" maj7, 1st inversion (= CE's 378) */
    M3(4, 5, 9),       /*  35 "459" maj7, 2nd inversion */
    M3(1, 5, 8),       /*  36 "158" maj7, 3rd inversion */
    M3(4, 7, 9),       /*  37 "479" min7, 1st inversion (= CE's 479) */
    M3(3, 5, 8),       /*  38 "358" min7, 2nd inversion */
    M3(2, 5, 9),       /*  39 "259" min7, 3rd inversion */
    M3(3, 6, 8),       /*  40 "368" dom7, 1st inversion */
    M3(3, 5, 9),       /*  41 "359" dom7, 2nd inversion */
    M3(2, 6, 9),       /*  42 "269" dom7, 3rd inversion */
    /* the other inversions of CE's chords */
    M2(3, 9),          /*  43 " 39" diminished, 1st inversion */
    M2(6, 9),          /*  44 " 69" diminished, 2nd inversion */
    M2(6, 0xA),        /*  45 " 6A" sus2#5, 1st inversion */
    M2(4, 6),          /*  46 " 46" sus2#5, 2nd inversion */
    M2(1, 6),          /*  47 " 16" sus#4, 1st inversion */
    M2(5, 0xB),        /*  48 " 5B" sus#4, 2nd inversion */
    M3(4, 6, 9),       /*  49 "469" m7b5, 1st inversion */
    M3(2, 5, 8),       /*  50 "258" m7b5, 2nd inversion */
    M3(4, 5, 8),       /*  51 "458" mMaj7, 1st inversion */
    M3(1, 4, 8),       /*  52 "148" mMaj7, 2nd inversion */
    M3(4, 6, 8),       /*  53 "468" aug7, 1st inversion */
    M3(2, 4, 8),       /*  54 "248" aug7, 2nd inversion */
    M3(2, 6, 0xA),     /*  55 "26A" aug7, 3rd inversion */
    M3(4, 7, 8),       /*  56 "478" augMaj7, 1st inversion */
    M3(3, 4, 8),       /*  57 "348" augMaj7, 2nd inversion */
    M3(1, 5, 9),       /*  58 "159" augMaj7, 3rd inversion */
    M3(2, 5, 7),       /*  59 "257" sus2add6 / 7sus4, 1st inversion */
    M3(3, 5, 0xA),     /*  60 "35A" sus2add6 / 7sus4, 2nd inversion */
    M3(5, 8, 0xA),     /*  61 "58A" sus2b7, 1st inversion */
    M3(3, 5, 7),       /*  62 "357" sus2b7, 2nd inversion */
    M3(2, 4, 9),       /*  63 "249" sus2b7, 3rd inversion */
    M3(5, 9, 0xA),     /*  64 "59A" sus2maj7, 1st inversion */
    M3(4, 5, 7),       /*  65 "457" sus2maj7, 2nd inversion */
    M3(1, 3, 8),       /*  66 "138" sus2maj7, 3rd inversion */
    M3(2, 6, 7),       /*  67 "267" sus4maj7, 1st inversion */
    M3(4, 5, 0xA),     /*  68 "45A" sus4maj7, 2nd inversion */
    M3(1, 6, 8),       /*  69 "168" sus4maj7, 3rd inversion */
};

bool label(char* text, int length, uint8_t value)
{
    const uint16_t mask = bitmask(value);
    if (mask == 0 || length < 4)
    {
        return false;
    }

    text[0] = text[1] = text[2] = ' ';
    text[3] = 0;
    int n = 3;
    for (int i = 11; i >= 1 && n > 0; i--)
    {
        if (mask >> i & 1)
        {
            text[--n] = static_cast<char>(i < 10 ? '0' + i : 'A' + i - 10);
        }
    }
    return true;
}

} // namespace chords
} // namespace mods

#endif // MT_HAS_CHORDS
