// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "modsBuild.h"

#if MT_MODS

#include "modsStore.h"

#include <cstring>

namespace mods
{

namespace
{

uint16_t readU16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t readU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void writeU16(uint8_t* p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
}

void writeU32(uint8_t* p, uint32_t value)
{
    for (int i = 0; i < 4; i++)
    {
        p[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

} // namespace

// Bitwise CRC-32 (IEEE 802.3, as zlib). Files are small and read rarely, so no table.
uint32_t crc32(const uint8_t* data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

Store::Store(uint8_t* buffer, size_t capacity, const char* magic)
  : buffer(buffer)
  , bufferCapacity(capacity)
  , payloadLength(0)
{
    memcpy(this->magic, magic, sizeof(this->magic));
}

void Store::clear()
{
    payloadLength = 0;
}

bool Store::validate(size_t fileSize)
{
    payloadLength = 0;

    if (fileSize < kHeaderSize || fileSize > bufferCapacity)
    {
        return false;
    }
    if (memcmp(buffer, magic, sizeof(magic)) != 0 || readU16(buffer + 4) != kFormat)
    {
        return false;
    }

    const size_t length = readU16(buffer + 6);
    if (kHeaderSize + length != fileSize)
    {
        return false;
    }
    if (crc32(buffer + kHeaderSize, length) != readU32(buffer + 8))
    {
        return false;
    }

    // Every record must fit exactly; ids must be unique.
    size_t offset = 0;
    while (offset < length)
    {
        if (length - offset < kRecordHeaderSize)
        {
            return false;
        }
        const size_t recordLength = readU16(record(offset) + 2);
        if (length - offset - kRecordHeaderSize < recordLength)
        {
            return false;
        }
        size_t earlier;
        payloadLength = offset;
        if (findOffset(record(offset)[0], &earlier))
        {
            payloadLength = 0;
            return false;
        }
        offset += kRecordHeaderSize + recordLength;
    }

    payloadLength = length;
    return true;
}

size_t Store::finalize()
{
    memcpy(buffer, magic, sizeof(magic));
    writeU16(buffer + 4, kFormat);
    writeU16(buffer + 6, static_cast<uint16_t>(payloadLength));
    writeU32(buffer + 8, crc32(buffer + kHeaderSize, payloadLength));
    return kHeaderSize + payloadLength;
}

bool Store::findOffset(uint8_t id, size_t* offset) const
{
    size_t at = 0;
    while (at < payloadLength)
    {
        if (record(at)[0] == id)
        {
            *offset = at;
            return true;
        }
        at += kRecordHeaderSize + readU16(record(at) + 2);
    }
    return false;
}

const uint8_t* Store::find(uint8_t id, uint8_t* version, uint16_t* length) const
{
    size_t offset;
    if (!findOffset(id, &offset))
    {
        return nullptr;
    }
    if (version != nullptr)
    {
        *version = record(offset)[1];
    }
    if (length != nullptr)
    {
        *length = readU16(record(offset) + 2);
    }
    return record(offset) + kRecordHeaderSize;
}

bool Store::put(uint8_t id, uint8_t version, const void* data, uint16_t length)
{
    uint8_t* out = reserve(id, version, length);
    if (out == nullptr)
    {
        return false;
    }
    if (length > 0)
    {
        memmove(out, data, length);
    }
    return true;
}

uint8_t* Store::reserve(uint8_t id, uint8_t version, uint16_t length)
{
    size_t oldSize = 0;
    size_t offset;
    const bool exists = findOffset(id, &offset);
    if (exists)
    {
        oldSize = kRecordHeaderSize + readU16(record(offset) + 2);
    }

    const size_t newPayload = payloadLength - oldSize + kRecordHeaderSize + length;
    if (kHeaderSize + newPayload > bufferCapacity || newPayload > 0xFFFF)
    {
        return nullptr;
    }

    if (exists)
    {
        remove(id);
    }

    uint8_t* out = record(payloadLength);
    out[0] = id;
    out[1] = version;
    writeU16(out + 2, length);
    payloadLength += kRecordHeaderSize + length;
    return out + kRecordHeaderSize;
}

void Store::remove(uint8_t id)
{
    size_t offset;
    if (!findOffset(id, &offset))
    {
        return;
    }
    const size_t size = kRecordHeaderSize + readU16(record(offset) + 2);
    memmove(record(offset), record(offset + size), payloadLength - offset - size);
    payloadLength -= size;
}

} // namespace mods

#endif // MT_MODS
