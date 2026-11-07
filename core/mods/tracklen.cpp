// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "tracklen.h"

#if MT_HAS_TRACKLEN

#include "modsProject.h"

#include <cstdio>
#include <cstring>

#ifndef MT_HOST_TEST
#include <cr_section_macros.h>
#endif

namespace mods
{
namespace tracklen
{

uint8_t activeLengths[kTracks];
volatile uint16_t activePattern = 0xFFFF;

namespace
{

constexpr size_t kTableSize = kPatterns * kTracks;
constexpr uint16_t kNoPattern = 0xFFFF;

#ifdef MT_HOST_TEST
uint8_t table[kPatterns][kTracks];
#else
__NOINIT(EXTERNAL_RAM) uint8_t table[kPatterns][kTracks];
#endif

// tracklen.bin from the 1.2.0 patch: magic, the number of entries (u32), the entries, a
// checksum (u32) over the number and the entries.
constexpr char kSideFileName[] = "tracklen.bin";
constexpr char kSideMagic[] = "TLN1";
constexpr size_t kSideHeader = 8;
constexpr size_t kSideSize = kSideHeader + kMaxRecordSize + 4;

// An imported file is read into here, so the main loop's stack stays small.
#ifdef MT_HOST_TEST
uint8_t sideFile[kSideSize];
#else
__NOINIT(EXTERNAL_RAM) uint8_t sideFile[kSideSize];
#endif

uint32_t sideSum(const uint8_t* data, size_t length)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < length; i++)
    {
        h = (h ^ data[i]) * 16777619u;
    }
    return h;
}

uint32_t get32(const uint8_t* b)
{
    return b[0] | b[1] << 8 | b[2] << 16 | static_cast<uint32_t>(b[3]) << 24;
}

bool hasLengths(uint32_t pattern)
{
    for (size_t t = 0; t < kTracks; t++)
    {
        if (table[pattern][t] != 0)
        {
            return true;
        }
    }
    return false;
}

void clearTable()
{
    memset(table, 0, kTableSize);
    activePattern = kNoPattern;
}

// Entries are valid if there's a whole number of them and each names a pattern.
bool validEntries(const uint8_t* entries, size_t size)
{
    if (size % kEntrySize != 0 || size > kMaxRecordSize)
    {
        return false;
    }
    for (size_t i = 0; i < size; i += kEntrySize)
    {
        if (entries[i] == 0)
        {
            return false;
        }
    }
    return true;
}

} // namespace

void init()
{
    clearTable();
}

uint8_t stored(uint8_t pattern, uint8_t track)
{
    return track < kTracks ? table[pattern][track] : 0;
}

void fillActive(uint8_t pattern)
{
    for (size_t t = 0; t < kTracks; t++)
    {
        activeLengths[t] = table[pattern][t];
    }
    activePattern = pattern;
}

bool edit(uint8_t pattern, uint8_t track, int delta, int patternLength)
{
    if (track >= kTracks || patternLength < 1)
    {
        return false;
    }

    int length = ownLength(table[pattern][track], patternLength) + delta;
    if (length < 1)
    {
        length = 1;
    }
    if (length > patternLength)
    {
        length = patternLength;
    }

    const uint8_t value = static_cast<uint8_t>(length < patternLength ? length : 0);
    if (value == table[pattern][track])
    {
        return false;
    }

    // In this order: the interrupt either still has the old copy or fills in the new one.
    table[pattern][track] = value;
    activePattern = kNoPattern;
    return true;
}

bool copyTracks(uint8_t sourcePattern, uint8_t destPattern, uint8_t sourceTrack, uint8_t destTrack, uint8_t tracks)
{
    if (sourceTrack + tracks > kTracks || destTrack + tracks > kTracks)
    {
        return false;
    }

    // A block pasted over itself in one pattern reads the lengths from before the paste.
    uint8_t lengths[kTracks];
    memcpy(lengths, table[sourcePattern], kTracks);

    bool changed = false;
    for (size_t t = 0; t < tracks; t++)
    {
        if (table[destPattern][destTrack + t] != lengths[sourceTrack + t])
        {
            table[destPattern][destTrack + t] = lengths[sourceTrack + t];
            changed = true;
        }
    }
    if (changed)
    {
        activePattern = kNoPattern; // after the table, as in edit()
    }
    return changed;
}

void load(const Store& source)
{
    clearTable();

    uint16_t length = 0;
    const uint8_t* data = source.find(kRecord, nullptr, &length);
    if (data == nullptr || !validEntries(data, length))
    {
        return;
    }
    for (size_t i = 0; i < length; i += kEntrySize)
    {
        memcpy(table[data[i]], data + i + 1, kTracks);
    }
    activePattern = kNoPattern;
}

bool store(Store& target)
{
    size_t size = 0;
    for (uint32_t p = 1; p < kPatterns; p++)
    {
        if (hasLengths(p))
        {
            size += kEntrySize;
        }
    }

    if (size == 0)
    {
        target.remove(kRecord);
        return true;
    }

    uint8_t* out = target.reserve(kRecord, kRecordVersion, static_cast<uint16_t>(size));
    if (out == nullptr)
    {
        return false;
    }
    for (uint32_t p = 1; p < kPatterns; p++)
    {
        if (hasLengths(p))
        {
            *out++ = static_cast<uint8_t>(p);
            memcpy(out, table[p], kTracks);
            out += kTracks;
        }
    }
    return true;
}

bool importSideFile(FileIo& io, const char* projectFolder, Store& target)
{
    char path[project::kPathSize];
    const int n = snprintf(path, sizeof(path), "%s/%s", projectFolder, kSideFileName);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(path))
    {
        return false;
    }

    const size_t size = io.read(path, sideFile, kSideSize);
    if (size == FileIo::kReadFailed || size > kSideSize || size < kSideHeader + 4 ||
        memcmp(sideFile, kSideMagic, 4) != 0)
    {
        return false;
    }

    const uint32_t count = get32(sideFile + 4);
    if (count >= kPatterns || size != kSideHeader + count * kEntrySize + 4)
    {
        return false;
    }
    const size_t entriesSize = count * kEntrySize;
    const uint8_t* entries = sideFile + kSideHeader;
    if (get32(entries + entriesSize) != sideSum(sideFile + 4, 4 + entriesSize) ||
        !validEntries(entries, entriesSize))
    {
        return false;
    }

    bool any = false;
    for (size_t i = 0; i < entriesSize; i += kEntrySize)
    {
        for (size_t t = 0; t < kTracks; t++)
        {
            any = any || entries[i + 1 + t] != 0;
        }
    }
    if (!any)
    {
        return false;
    }
    return target.put(kRecord, kRecordVersion, entries, static_cast<uint16_t>(entriesSize));
}

} // namespace tracklen
} // namespace mods

#endif // MT_HAS_TRACKLEN
