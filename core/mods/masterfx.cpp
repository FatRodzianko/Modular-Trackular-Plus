// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "masterfx.h"

#if MT_HAS_MASTERFX

#include "modsProject.h"

#include <cstdio>
#include <cstring>

namespace mods
{
namespace masterfx
{

const char* const kReverbNames[kReverbCount] = {
    "Stock", "Freeverb", "Plate", "Spring", "Shimmer", "\"Gated\"", "Reverse",
};
const char* const kDelayNames[kDelayCount] = { "Stock", "Tape", "BBD" };

Settings settings;

namespace
{

constexpr size_t kRecordSize = 2;
static_assert(sizeof(Settings) == kRecordSize);

// masterfx.bin from the 1.2.0 patch: magic, reverb, delay, two zero bytes, a checksum
// over those four. Delay 3 was "Gated" before it moved to the reverbs: that plays the
// stock delay and keeps the reverb.
constexpr char kSideFileName[] = "masterfx.bin";
constexpr char kSideMagic[] = "HMF1";
constexpr size_t kSideSize = 12;
constexpr size_t kSideData = 4;
constexpr uint8_t kOldDelayCount = 4;

// A value out of range plays Stock, and the other one stays.
Settings fromRecord(const Store& store)
{
    Settings s;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, nullptr, &length);
    if (data != nullptr && length == kRecordSize)
    {
        s.reverb = data[0] < kReverbCount ? data[0] : kStock;
        s.delay = data[1] < kDelayCount ? data[1] : kStock;
    }
    return s;
}

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

} // namespace

void load(const Store& store)
{
    settings = fromRecord(store);
}

bool storeIfEdited(Store& store)
{
    const Settings stored = fromRecord(store);
    if (stored.reverb == settings.reverb && stored.delay == settings.delay)
    {
        return false;
    }
    Settings copy = settings;
    return store.put(kRecord, kRecordVersion, &copy, kRecordSize);
}

bool importSideFile(FileIo& io, const char* projectFolder, Store& store)
{
    char path[project::kPathSize];
    const int n = snprintf(path, sizeof(path), "%s/%s", projectFolder, kSideFileName);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(path))
    {
        return false;
    }

    uint8_t file[kSideSize];
    if (io.read(path, file, sizeof(file)) != kSideSize || memcmp(file, kSideMagic, 4) != 0 ||
        get32(file + 4 + kSideData) != sideSum(file + 4, kSideData))
    {
        return false;
    }

    if (file[4] >= kReverbCount || file[5] >= kOldDelayCount)
    {
        return false;
    }
    Settings s;
    s.reverb = file[4];
    s.delay = file[5] < kDelayCount ? file[5] : kStock;
    return store.put(kRecord, kRecordVersion, &s, kRecordSize) &&
           (s.reverb != kStock || s.delay != kStock);
}

} // namespace masterfx
} // namespace mods

#endif // MT_HAS_MASTERFX
