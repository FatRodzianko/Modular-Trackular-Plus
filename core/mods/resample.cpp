// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "resample.h"

#if MT_HAS_RESAMPLE

#include "modsProject.h"

#include <cstdio>
#include <cstring>

namespace mods
{
namespace resample
{

Settings settings;

namespace
{

constexpr uint16_t kRecordSize = 1;

// resample.bin from the 1.2.0 patch, 12 bytes: "RSM1", the settings byte, three zero bytes,
// then a little-endian FNV-1a over bytes 4 to 7.
constexpr char kSideFileName[] = "resample.bin";
constexpr char kSideMagic[4] = { 'R', 'S', 'M', '1' };
constexpr size_t kSideSize = 12;
constexpr size_t kSideDataStart = 4;
constexpr size_t kSideDataSize = 4;
constexpr size_t kSideSumStart = kSideDataStart + kSideDataSize;

uint32_t fnv1a(const uint8_t* bytes, size_t count)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < count; i++)
    {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

uint32_t readLe32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) | static_cast<uint32_t>(bytes[1]) << 8 |
           static_cast<uint32_t>(bytes[2]) << 16 | static_cast<uint32_t>(bytes[3]) << 24;
}

} // namespace

uint8_t toBits(const Settings& s)
{
    if (!s.trkAudio)
    {
        return 0;
    }
    return static_cast<uint8_t>(kTrkAudioBit | (s.trkRecord ? kTrkRecordBit : 0));
}

bool fromBits(uint8_t bits, Settings* s)
{
    if (bits > kMaxBits)
    {
        return false;
    }
    s->trkRecord = (bits & kTrkRecordBit) != 0;
    s->trkAudio = s->trkRecord || (bits & kTrkAudioBit) != 0;
    return true;
}

void load(const Store& store)
{
    Settings s;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, nullptr, &length);
    if (data == nullptr || length != kRecordSize || !fromBits(data[0], &s))
    {
        s = Settings{};
    }
    settings = s;
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
    if (io.read(path, file, sizeof(file)) != kSideSize ||
        memcmp(file, kSideMagic, sizeof(kSideMagic)) != 0 ||
        readLe32(file + kSideSumStart) != fnv1a(file + kSideDataStart, kSideDataSize))
    {
        return false;
    }

    Settings s;
    if (!fromBits(file[kSideDataStart], &s))
    {
        return false;
    }
    const uint8_t bits = toBits(s);
    return store.put(kRecord, kRecordVersion, &bits, kRecordSize) && bits != 0;
}

} // namespace resample
} // namespace mods

#endif // MT_HAS_RESAMPLE
