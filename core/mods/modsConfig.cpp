// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "modsBuild.h"

#if MT_MODS

#include "modsConfig.h"

#include "mods.h"

#include <cstring>

namespace mods
{

std::array<uint8_t, kModCount> switches{};

namespace config
{

void load(FileIo& io, Store& store)
{
    switches.fill(0);

    const size_t size = io.read(kPath, store.image(), store.capacity());
    if (size == FileIo::kReadFailed || !store.validate(size))
    {
        store.clear();
        return;
    }

    uint16_t length = 0;
    const uint8_t* data = store.find(kSwitchesRecord, nullptr, &length);
    if (data == nullptr)
    {
        return;
    }
    for (size_t i = 0; i < kModCount && i < length; i++)
    {
        switches[i] = data[i] != 0 ? 1 : 0;
    }
}

bool save(FileIo& io, Store& store)
{
    // Mod ids are 8-bit, so a switches record never holds more than 256 bytes.
    uint8_t bytes[256] = {};
    uint16_t length = 0;
    const uint8_t* old = store.find(kSwitchesRecord, nullptr, &length);
    if (old != nullptr)
    {
        if (length > sizeof(bytes))
        {
            length = sizeof(bytes);
        }
        memcpy(bytes, old, length);
    }
    for (size_t i = 0; i < kModCount; i++)
    {
        bytes[i] = switches[i];
    }
    if (length < kModCount)
    {
        length = kModCount;
    }

    if (!store.put(kSwitchesRecord, kSwitchesVersion, bytes, length))
    {
        return false;
    }

    const size_t size = store.finalize();
    io.makeDir(kDir);
    return io.write(kPath, store.image(), size);
}

} // namespace config
} // namespace mods

#endif // MT_MODS
