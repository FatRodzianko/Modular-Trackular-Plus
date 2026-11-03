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
    /* every other chord within an octave: one, two, then three intervals, in digit order */
    /*  70 */ M1(1), M1(2), M1(3), M1(4), M1(5), M1(6),
    /*  76 */ M1(7), M1(8), M1(9), M1(0xA), M1(0xB), M2(1, 2),
    /*  82 */ M2(1, 3), M2(1, 4), M2(1, 5), M2(1, 7), M2(1, 8), M2(1, 9),
    /*  88 */ M2(1, 0xA), M2(1, 0xB), M2(2, 3), M2(2, 4), M2(2, 5), M2(2, 6),
    /*  94 */ M2(2, 9), M2(2, 0xA), M2(2, 0xB), M2(3, 4), M2(3, 5), M2(3, 0xA),
    /* 100 */ M2(3, 0xB), M2(4, 5), M2(4, 0xA), M2(4, 0xB), M2(5, 6), M2(6, 8),
    /* 106 */ M2(6, 0xB), M2(7, 8), M2(7, 9), M2(7, 0xA), M2(7, 0xB), M2(8, 9),
    /* 112 */ M2(8, 0xA), M2(8, 0xB), M2(9, 0xA), M2(9, 0xB), M2(0xA, 0xB), M3(1, 2, 3),
    /* 118 */ M3(1, 2, 4), M3(1, 2, 5), M3(1, 2, 6), M3(1, 2, 7), M3(1, 2, 8), M3(1, 2, 9),
    /* 124 */ M3(1, 2, 0xA), M3(1, 2, 0xB), M3(1, 3, 4), M3(1, 3, 5), M3(1, 3, 6), M3(1, 3, 7),
    /* 130 */ M3(1, 3, 9), M3(1, 3, 0xA), M3(1, 3, 0xB), M3(1, 4, 5), M3(1, 4, 6), M3(1, 4, 7),
    /* 136 */ M3(1, 4, 9), M3(1, 4, 0xA), M3(1, 4, 0xB), M3(1, 5, 6), M3(1, 5, 7), M3(1, 5, 0xA),
    /* 142 */ M3(1, 5, 0xB), M3(1, 6, 7), M3(1, 6, 9), M3(1, 6, 0xA), M3(1, 6, 0xB), M3(1, 7, 8),
    /* 148 */ M3(1, 7, 9), M3(1, 7, 0xA), M3(1, 7, 0xB), M3(1, 8, 9), M3(1, 8, 0xA), M3(1, 8, 0xB),
    /* 154 */ M3(1, 9, 0xA), M3(1, 9, 0xB), M3(1, 0xA, 0xB), M3(2, 3, 4), M3(2, 3, 5), M3(2, 3, 6),
    /* 160 */ M3(2, 3, 7), M3(2, 3, 8), M3(2, 3, 9), M3(2, 3, 0xA), M3(2, 3, 0xB), M3(2, 4, 5),
    /* 166 */ M3(2, 4, 6), M3(2, 4, 7), M3(2, 4, 0xA), M3(2, 4, 0xB), M3(2, 5, 6), M3(2, 5, 0xA),
    /* 172 */ M3(2, 5, 0xB), M3(2, 6, 8), M3(2, 6, 0xB), M3(2, 7, 8), M3(2, 8, 9), M3(2, 8, 0xA),
    /* 178 */ M3(2, 8, 0xB), M3(2, 9, 0xA), M3(2, 9, 0xB), M3(2, 0xA, 0xB), M3(3, 4, 5), M3(3, 4, 6),
    /* 184 */ M3(3, 4, 7), M3(3, 4, 9), M3(3, 4, 0xA), M3(3, 4, 0xB), M3(3, 5, 6), M3(3, 5, 0xB),
    /* 190 */ M3(3, 6, 7), M3(3, 6, 0xB), M3(3, 8, 9), M3(3, 8, 0xA), M3(3, 8, 0xB), M3(3, 9, 0xA),
    /* 196 */ M3(3, 9, 0xB), M3(3, 0xA, 0xB), M3(4, 5, 6), M3(4, 5, 0xB), M3(4, 6, 7), M3(4, 6, 0xA),
    /* 202 */ M3(4, 6, 0xB), M3(4, 9, 0xA), M3(4, 9, 0xB), M3(4, 0xA, 0xB), M3(5, 6, 7), M3(5, 6, 8),
    /* 208 */ M3(5, 6, 9), M3(5, 6, 0xA), M3(5, 6, 0xB), M3(5, 7, 8), M3(5, 7, 9), M3(5, 8, 9),
    /* 214 */ M3(5, 8, 0xB), M3(5, 9, 0xB), M3(5, 0xA, 0xB), M3(6, 7, 8), M3(6, 7, 9), M3(6, 7, 0xA),
    /* 220 */ M3(6, 7, 0xB), M3(6, 8, 9), M3(6, 8, 0xA), M3(6, 8, 0xB), M3(6, 9, 0xA), M3(6, 9, 0xB),
    /* 226 */ M3(6, 0xA, 0xB), M3(7, 8, 9), M3(7, 8, 0xA), M3(7, 8, 0xB), M3(7, 9, 0xA), M3(7, 9, 0xB),
    /* 232 */ M3(7, 0xA, 0xB), M3(8, 9, 0xA), M3(8, 9, 0xB), M3(8, 0xA, 0xB), M3(9, 0xA, 0xB),
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
