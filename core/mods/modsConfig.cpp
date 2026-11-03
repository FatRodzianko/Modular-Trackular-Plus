// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "modsBuild.h"

#if MT_MODS

#include "modsConfig.h"

#include "modsSwitches.h"

namespace mods
{
namespace config
{

void load(FileIo& io, Store& store, uint8_t* defaults, size_t count)
{
    const size_t size = io.read(kPath, store.image(), store.capacity());
    if (size == FileIo::kReadFailed || !store.validate(size))
    {
        store.clear();
    }
    readSwitches(store, defaults, count);
}

bool save(FileIo& io, Store& store, const uint8_t* defaults, size_t count)
{
    if (!writeSwitches(store, defaults, count))
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
