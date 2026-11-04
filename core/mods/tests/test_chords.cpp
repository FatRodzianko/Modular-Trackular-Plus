// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the chords mod: the chord list against CE's chords' inversions and
// the 1.2.0 patch's numbering, the masks handed to CE, and the cell text. Build and run
// with `make -C core/mods/tests`.

#include "chords.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "../../source/mtChordDefs.h"

using namespace mods;

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& name)
{
    checks++;
    if (!condition)
    {
        failures++;
        printf("FAIL %s\n", name.c_str());
    }
}

void setChords(bool on)
{
    switches[static_cast<size_t>(Mod::chords)] = on ? 1 : 0;
}

std::string text(uint8_t value)
{
    char buffer[8] = "xxxxxxx";
    if (!chords::label(buffer, sizeof(buffer), value))
    {
        return "-";
    }
    return buffer;
}

// A chord's inversions: each of its other notes as the lowest, the rest folded into
// the octave above it.
std::set<uint16_t> inversionsOf(uint16_t mask)
{
    std::set<uint16_t> result;
    for (int bass = 1; bass < 12; bass++)
    {
        if (mask >> bass & 1)
        {
            uint16_t inversion = 0;
            for (int i = 0; i < 12; i++)
            {
                if (mask >> i & 1)
                {
                    inversion |= 1u << ((i - bass + 12) % 12);
                }
            }
            result.insert(inversion);
        }
    }
    return result;
}

// A mask's intervals as CE labels its chords, from the mask alone.
std::string intervals(uint16_t mask)
{
    std::string digits;
    for (int i = 1; i < 16; i++)
    {
        if (mask >> i & 1)
        {
            digits += static_cast<char>(i < 10 ? '0' + i : 'A' + i - 10);
        }
    }
    return std::string(3 - std::min<size_t>(3, digits.size()), ' ') + digits;
}

void testList()
{
    setChords(true);

    for (unsigned v = chords::kFirst; v <= chords::kLast; v++)
    {
        const uint16_t mask = chords::kLibrary[v - chords::kFirst];
        const int notes = __builtin_popcount(mask);
        if (!(mask & 1) || mask >> 12 || notes < 2 || notes > 4)
        {
            check(false, "value " + std::to_string(v) + ": the note plus 1-3 notes in an octave");
        }
    }
    check(true, "every chord is the note plus 1-3 notes in an octave");

    // The list was made from CE's first 30 chords, the ones it had when the numbers were
    // fixed: every inversion of them that fits in an octave, except those
    // that are CE chords already; two of those stay so that every maj7 and min7
    // inversion has a value in a row (34 = CE 18 "378", 37 = CE 22 "479").
    std::set<uint16_t> ce;
    std::set<uint16_t> inversions;
    for (unsigned v = 1; v <= 29; v++)
    {
        ce.insert(chordDef[v]);
        if (chordDef[v] >> 12 == 0)
        {
            for (uint16_t inversion : inversionsOf(chordDef[v]))
            {
                inversions.insert(inversion);
            }
        }
    }
    std::set<uint16_t> expected;
    for (uint16_t inversion : inversions)
    {
        if (!ce.count(inversion))
        {
            expected.insert(inversion);
        }
    }
    expected.insert(chordDef[18]);
    expected.insert(chordDef[22]);

    std::set<uint16_t> list;
    std::set<unsigned> repeats;
    bool unique = true;
    for (unsigned v = chords::kFirst; v <= chords::kLast; v++)
    {
        const uint16_t mask = chords::kLibrary[v - chords::kFirst];
        unique = unique && list.insert(mask).second;
        if (ce.count(mask))
        {
            repeats.insert(v);
        }
    }
    check(unique, "no chord twice in the list");
    check(repeats == std::set<unsigned>{ 34, 37 }, "only 34 and 37 repeat CE chords");
    check(list == expected, "the list is exactly the inversions CE lacks, plus 34 and 37");
    check(chords::kCount == 40, "40 chords, 30-69");

    // The 1.2.0 patch's numbers (saved in patterns, so they must not move).
    const struct
    {
        uint8_t value;
        const char* shows;
    } known[] = {
        { 30, " 38" }, { 31, " 59" }, { 32, " 49" }, { 33, " 58" }, { 34, "378" },
        { 35, "459" }, { 36, "158" }, { 37, "479" }, { 38, "358" }, { 39, "259" },
        { 40, "368" }, { 41, "359" }, { 42, "269" }, { 43, " 39" }, { 48, " 5B" },
        { 55, "26A" }, { 59, "257" }, { 69, "168" },
    };
    for (const auto& k : known)
    {
        check(text(k.value) == k.shows,
              "value " + std::to_string(k.value) + " shows \"" + k.shows + "\", got \"" +
                text(k.value) + "\"");
    }

    // The text is the chord: every value's digits are its mask's intervals.
    bool matches = true;
    for (unsigned v = chords::kFirst; v <= chords::kLast; v++)
    {
        matches = matches && text(v) == intervals(chords::kLibrary[v - chords::kFirst]);
    }
    check(matches, "every chord's text is its intervals");
}

void testOnOff()
{
    setChords(true);
    bool outside = true;
    for (unsigned v = 0; v < 256; v++)
    {
        const bool inList = v >= chords::kFirst && v <= chords::kLast;
        outside = outside && (inList ? chords::bitmask(v) == chords::kLibrary[v - chords::kFirst]
                                     : chords::bitmask(v) == 0 && text(v) == "-");
    }
    check(outside, "On: 30-69 have masks and text, CE keeps the rest");

    char small[3];
    check(!chords::label(small, sizeof(small), 31), "no text into a buffer under 4 bytes");

    setChords(false);
    bool off = true;
    for (unsigned v = 0; v < 256; v++)
    {
        off = off && chords::bitmask(v) == 0 && text(v) == "-";
    }
    check(off, "Off: no masks, no text (CE exactly)");
}

} // namespace

int main()
{
    testList();
    testOnOff();

    if (failures == 0)
    {
        printf("PASS %d checks\n", checks);
    }
    printf("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
