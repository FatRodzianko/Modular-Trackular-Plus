// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// A plugin mod's default pack onto the card (Synth FX's and Master FX's), at the mod's start, in
// the main loop only. For each of the pack's files:
//   - missing: written;
//   - already this pack's: left;
//   - exactly a file an earlier release shipped (its name, size and FNV-1a in the mod's shipped
//     table): replaced with this pack's;
//   - anything else (changed by the user, or the user's own): left alone.
// A file is never written over where it is. The new one is written beside it as name.new and
// read back, then the old one is removed and the new one renamed in its place (on a card that
// can't rename, written in place, the .new kept until that is done). A .new found at the start
// is from a start that stopped part way: it is removed first, and if it held this pack's file
// whole, the file it was putting in is put in again (over the old one, a half-written one, or
// none). So a power cut at any step leaves the old file, the new one, or none, which the next
// start writes; never a half-written file that a later start would leave as the user's.
//
// Header only and platform-free, so the host tests and the emulator rigs build it as it is.

#ifndef MODS_PLUGINS_PLUGINPACK_H_
#define MODS_PLUGINS_PLUGINPACK_H_

#include "modsFileIo.h"
#include "pluginKit.h"

#include <cstddef>
#include <cstdint>

namespace mods
{
namespace pluginkit
{

// A pack file a release shipped: one on the card that is exactly this is replaced by the pack's
// file of that name.
struct ShippedFile
{
    const char* name;
    uint32_t size;
    uint32_t hash; // fileHash
};

// FNV-1a, 32 bits (tools/*/build_pack.py's fnv1a)
inline uint32_t fileHash(const uint8_t* data, uint32_t size)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < size; i++)
    {
        h = (h ^ data[i]) * 16777619u;
    }
    return h;
}

constexpr char kNewSuffix[] = ".new";
constexpr size_t kPackPathSize = 96; // a pack file's path with ".new" and its 0

// What writePackFile works with
struct PackCard
{
    FileIo& io;
    uint8_t* buffer;              // capacity bytes nothing else uses now, for reading files back
    uint32_t capacity;            // null buffer: missing files written, nothing else done
    const ShippedFile* shipped;
    size_t shippedCount;
};

namespace packdetail
{

inline bool sameBytes(const uint8_t* a, const uint8_t* b, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

// The file at path holds exactly data
inline bool holds(const PackCard& c, const char* path, const uint8_t* data, uint32_t size)
{
    return size <= c.capacity && c.io.read(path, c.buffer, size) == size && sameBytes(c.buffer, data, size);
}

// The file at path is exactly one a release shipped as name. Nothing read unless the table has
// the name.
inline bool wasShipped(const PackCard& c, const char* path, const char* name)
{
    bool named = false;
    for (size_t i = 0; i < c.shippedCount && !named; i++)
    {
        named = sameText(c.shipped[i].name, name);
    }
    if (!named)
    {
        return false;
    }
    const size_t size = c.io.read(path, c.buffer, 0);
    for (size_t i = 0; i < c.shippedCount; i++)
    {
        const ShippedFile& s = c.shipped[i];
        if (!sameText(s.name, name) || size != s.size || size > c.capacity)
        {
            continue;
        }
        if (c.io.read(path, c.buffer, size) == size && fileHash(c.buffer, static_cast<uint32_t>(size)) == s.hash)
        {
            return true;
        }
    }
    return false;
}

} // namespace packdetail

// One of the pack's files (name, its bytes) onto the card at path, as above
inline void writePackFile(const PackCard& c, const char* path, const char* name, const uint8_t* data, uint32_t size)
{
    char fresh[kPackPathSize];
    copyText(fresh, sizeof(fresh), path);
    appendText(fresh, sizeof(fresh), kNewSuffix);
    if (textLength(fresh, sizeof(fresh)) != textLength(path, sizeof(fresh)) + sizeof(kNewSuffix) - 1)
    {
        return; // too long a path (the packs' are short)
    }
    if (c.buffer == nullptr)
    {
        if (!c.io.exists(path))
        {
            c.io.write(path, data, size);
        }
        return;
    }
    bool resume = false;
    if (c.io.exists(fresh))
    {
        resume = packdetail::holds(c, fresh, data, size); // the last start stopped while putting it in
        c.io.remove(fresh);
    }
    if (c.io.exists(path))
    {
        if (!resume && !packdetail::wasShipped(c, path, name))
        {
            return; // this pack's own, or the user's
        }
        if (packdetail::holds(c, path, data, size))
        {
            return; // this pack's already
        }
    }
    if (!c.io.write(fresh, data, size) || !packdetail::holds(c, fresh, data, size))
    {
        c.io.remove(fresh);
        return; // the old file stays, or none
    }
    c.io.remove(path);
    if (!c.io.rename(fresh, path))
    {
        // No rename here: written again in place, the .new kept until that reads back right
        if (c.io.write(path, data, size) && packdetail::holds(c, path, data, size))
        {
            c.io.remove(fresh);
        }
    }
}

} // namespace pluginkit
} // namespace mods

#endif /* MODS_PLUGINS_PLUGINPACK_H_ */
