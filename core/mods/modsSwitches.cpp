// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "modsBuild.h"

#if MT_MODS

#include "modsSwitches.h"

#include "mods.h"

#include <cstring>

namespace mods
{

std::array<uint8_t, kModCount> switches{};
std::array<uint8_t, kModCount> defaults{};

void readSwitches(const Store& store, uint8_t* values, size_t count)
{
    uint16_t length = 0;
    const uint8_t* data = store.find(kSwitchesRecord, nullptr, &length);
    for (size_t i = 0; i < count; i++)
    {
        values[i] = data != nullptr && i < length && data[i] != 0 ? 1 : 0;
    }
}

bool writeSwitches(Store& store, const uint8_t* values, size_t count)
{
    // Mod ids are 8-bit, so a switches record never holds more than 256 bytes.
    uint8_t bytes[256] = {};
    if (count > sizeof(bytes))
    {
        count = sizeof(bytes);
    }

    uint16_t length = 0;
    const uint8_t* old = store.find(kSwitchesRecord, nullptr, &length);
    if (old == nullptr)
    {
        length = 0;
    }
    if (length > sizeof(bytes))
    {
        length = sizeof(bytes);
    }
    if (length > 0)
    {
        memcpy(bytes, old, length);
    }
    for (size_t i = 0; i < count; i++)
    {
        bytes[i] = values[i] != 0 ? 1 : 0;
    }
    if (length < count)
    {
        length = static_cast<uint16_t>(count);
    }

    if (length == 0)
    {
        return true; // a build without mods has nothing to record
    }
    return store.put(kSwitchesRecord, kSwitchesVersion, bytes, length);
}

} // namespace mods

#endif // MT_MODS
