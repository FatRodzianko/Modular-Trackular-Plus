// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// A small file of tagged records, used for both mods.cfg (defaults for new
// projects) and mods.bin (a project's mod set and settings).
//
// File layout, little-endian:
//   header  magic[4], format (u16), payload length (u16), CRC-32 of the payload (u32)
//   payload records, each: id (u8), version (u8), length (u16), data[length]
//
// The store keeps the whole file image in one buffer, so a load is one read into
// image() plus validate(), and a save is finalize() plus one write. Records it doesn't
// know (from a newer build) are kept as they are and written back.

#ifndef MODS_MODSSTORE_H_
#define MODS_MODSSTORE_H_

#include <cstddef>
#include <cstdint>

namespace mods
{

uint32_t crc32(const uint8_t* data, size_t length);

class Store
{
  public:
    static constexpr size_t kHeaderSize = 12;
    static constexpr size_t kRecordHeaderSize = 4;
    static constexpr uint16_t kFormat = 1;

    Store(uint8_t* buffer, size_t capacity, const char* magic);

    // No records.
    void clear();

    // The file image: read a file into it, then call validate().
    uint8_t* image() { return buffer; }
    size_t capacity() const { return bufferCapacity; }

    // Checks the image read into image() (fileSize bytes; may exceed capacity, which
    // fails). A damaged or foreign image leaves the store cleared and returns false.
    bool validate(size_t fileSize);

    // Writes the header for the current records; returns the bytes to write.
    size_t finalize();

    // A record's data, or nullptr. version and length may be nullptr.
    const uint8_t* find(uint8_t id, uint8_t* version, uint16_t* length) const;

    // Adds or replaces a record. False (store unchanged) if it doesn't fit. data must
    // not point into the store.
    bool put(uint8_t id, uint8_t version, const void* data, uint16_t length);

    // Adds or replaces a record whose data the caller writes through the pointer, valid
    // until the store next changes. nullptr (store unchanged) if it doesn't fit.
    uint8_t* reserve(uint8_t id, uint8_t version, uint16_t length);

    void remove(uint8_t id);

    size_t payloadSize() const { return payloadLength; }
    bool empty() const { return payloadLength == 0; }

  private:
    uint8_t* record(size_t offset) { return buffer + kHeaderSize + offset; }
    const uint8_t* record(size_t offset) const { return buffer + kHeaderSize + offset; }
    bool findOffset(uint8_t id, size_t* offset) const;

    uint8_t* buffer;
    size_t bufferCapacity;
    char magic[4];
    size_t payloadLength;
};

} // namespace mods

#endif /* MODS_MODSSTORE_H_ */
