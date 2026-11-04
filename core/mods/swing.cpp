// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "swing.h"

#if MT_HAS_SWING

#include "modsProject.h"

#include <cstdio>
#include <cstring>

#ifndef MT_HOST_TEST
#include "mtSequencer.h"
#endif

namespace mods
{
namespace swing
{

Settings settings;

namespace
{

constexpr size_t kRecordSize = 1;
static_assert(sizeof(Settings) == kRecordSize);
constexpr uint8_t kMaxStored = kMaxAmount - kMinAmount;

// swing.bin from the 1.2.0 patch: magic, Global swing (0 or 1), amount - 25, two zero
// bytes, a checksum over those four.
constexpr char kSideFileName[] = "swing.bin";
constexpr char kSideMagic[] = "HSW1";
constexpr size_t kSideSize = 12;
constexpr size_t kSideData = 4;

Settings fromRecord(const Store& store)
{
    Settings s;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, nullptr, &length);
    if (data != nullptr && length == kRecordSize && data[0] <= kMaxStored)
    {
        s.amount = data[0];
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
    if (fromRecord(store).amount == settings.amount)
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

    const uint8_t on = file[4];
    const uint8_t amount = file[5];
    if (on > 1 || amount > kMaxStored)
    {
        return false;
    }
    Settings s;
    s.amount = amount;
    return store.put(kRecord, kRecordVersion, &s, kRecordSize) && on == 1;
}

#ifndef MT_HOST_TEST

void switchEdited()
{
    if (sequencer.isPlay())
    {
        followSwitch(sequencer.swingForMods(), on(Mod::swing));
    }
}

void amountEdited()
{
    if (sequencer.isPlay() && on(Mod::swing))
    {
        followAmount(sequencer.swingForMods());
    }
}

#endif // MT_HOST_TEST

} // namespace swing
} // namespace mods

#endif // MT_HAS_SWING
