// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the chords mod: the chord list against CE's chords and the 1.2.0
// patch's numbering, the masks handed to CE, and the cell text. Build and run with
// `make -C core/mods/tests`.

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

    // Together with CE's chords that fit in an octave, the list holds every chord of
    // one to three notes above the step's note, once; only two inversions repeat a
    // CE chord (34 = CE 18 "378", 37 = CE 22 "479").
    std::set<uint16_t> ce;
    for (unsigned v = 1; v <= MAX_CHORD; v++)
    {
        if (chordDef[v] >> 12 == 0)
        {
            ce.insert(chordDef[v]);
        }
    }
    std::set<uint16_t> all = ce;
    std::set<unsigned> repeats;
    bool unique = true;
    std::set<uint16_t> seen;
    for (unsigned v = chords::kFirst; v <= chords::kLast; v++)
    {
        const uint16_t mask = chords::kLibrary[v - chords::kFirst];
        unique = unique && seen.insert(mask).second;
        if (ce.count(mask))
        {
            repeats.insert(v);
        }
        all.insert(mask);
    }
    check(unique, "no chord twice in the list");
    check(repeats == std::set<unsigned>{ 34, 37 }, "only 34 and 37 repeat CE chords");
    check(all.size() == 11 + 55 + 165, "every chord of 1-3 notes within an octave");

    // 70..236: one, two, then three notes, each group in digit order.
    bool ordered = true;
    for (unsigned v = 71; v <= chords::kLast; v++)
    {
        const uint16_t a = chords::kLibrary[v - 1 - chords::kFirst];
        const uint16_t b = chords::kLibrary[v - chords::kFirst];
        const int na = __builtin_popcount(a), nb = __builtin_popcount(b);
        ordered = ordered && (na < nb || (na == nb && intervals(a) < intervals(b)));
    }
    check(ordered, "70-236 in size, then digit order");

    // The 1.2.0 patch's numbers (saved in patterns, so they must not move).
    const struct
    {
        uint8_t value;
        const char* shows;
    } known[] = {
        { 30, " 38" }, { 31, " 59" }, { 32, " 49" }, { 33, " 58" }, { 34, "378" },
        { 35, "459" }, { 36, "158" }, { 37, "479" }, { 38, "358" }, { 39, "259" },
        { 40, "368" }, { 41, "359" }, { 42, "269" }, { 43, " 39" }, { 69, "168" },
        { 70, "  1" }, { 80, "  B" }, { 81, " 12" }, { 116, " AB" }, { 117, "123" },
        { 236, "9AB" },
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
    check(outside, "On: 30-236 have masks and text, CE keeps the rest");

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
