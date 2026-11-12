// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The few file operations the mods' settings files need. The firmware implements it
// on CE's FileSystem (mods.cpp); host tests implement it in memory.

#ifndef MODS_MODSFILEIO_H_
#define MODS_MODSFILEIO_H_

#include <cstddef>
#include <cstdint>

namespace mods
{

class FileIo
{
  public:
    static constexpr size_t kReadFailed = static_cast<size_t>(-1);

    virtual ~FileIo() = default;

    // Reads up to capacity bytes; returns the file's full size (which may be larger
    // than capacity), or kReadFailed if it doesn't exist or can't be read.
    virtual size_t read(const char* path, uint8_t* buffer, size_t capacity) = 0;
    // Creates or replaces the file.
    virtual bool write(const char* path, const uint8_t* data, size_t length) = 0;
    virtual void remove(const char* path) = 0;
    virtual void makeDir(const char* path) = 0;

    virtual bool exists(const char* path)
    {
        uint8_t byte = 0;
        return read(path, &byte, 0) != kReadFailed;
    }
    // The files in dir whose names end in suffix (any case), sorted, into names: up to count
    // of them, nameSize bytes each with the 0 (longer names are left out). Returns how many.
    virtual size_t list(const char* dir, const char* suffix, char* names, size_t count, size_t nameSize)
    {
        (void)dir;
        (void)suffix;
        (void)names;
        (void)count;
        (void)nameSize;
        return 0;
    }
};

} // namespace mods

#endif /* MODS_MODSFILEIO_H_ */
