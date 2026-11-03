#include "instrumentFileValidation.h"

namespace
{
constexpr uint32_t kSerializedHeaderSize = 16;
constexpr uint32_t kCrcSize = sizeof(uint32_t);
constexpr uint8_t kInstrumentFileType = 1;
constexpr uint8_t kFirstInstrumentFileVersion = 1;
// Firmware versions before v8 used a four-byte-smaller serialized instrument layout.
constexpr uint8_t kFirstFullSizeInstrumentVersion = 8;

uint16_t readUint16(const uint8_t* data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}
}

namespace InstrumentFileValidation
{
bool validate(const uint8_t* data,
              uint32_t loadedSize,
              uint32_t fileSize,
              uint16_t currentInstrumentSize,
              uint8_t currentFileVersion,
              Result& result)
{
    result = {};

    if (data == nullptr || loadedSize < kSerializedHeaderSize || fileSize < kSerializedHeaderSize)
    {
        return false;
    }

    if (data[0] != 'T' || data[1] != 'I' || readUint16(data + 2) != kInstrumentFileType)
    {
        return false;
    }

    const uint8_t version = data[8];
    if (version < kFirstInstrumentFileVersion || version > currentFileVersion ||
        data[9] != version || data[10] != version || data[11] != version)
    {
        return false;
    }

    const uint16_t instrumentSize = readUint16(data + 12);
    const uint16_t legacyInstrumentSize =
      currentInstrumentSize >= kCrcSize ? currentInstrumentSize - kCrcSize : 0;
    const bool knownLegacySize =
      version < kFirstFullSizeInstrumentVersion && instrumentSize == legacyInstrumentSize;

    if (instrumentSize != currentInstrumentSize && !knownLegacySize)
    {
        return false;
    }

    const uint32_t metadataSize = kSerializedHeaderSize + instrumentSize;
    if (metadataSize > loadedSize || metadataSize > fileSize)
    {
        return false;
    }

    result.instrumentSize = instrumentSize;

    if (fileSize == metadataSize)
    {
        // Some metadata-only legacy files omit the CRC field entirely.
        result.dataOffset = metadataSize;
        return true;
    }

    const uint32_t dataOffset = metadataSize + kCrcSize;
    if (dataOffset > loadedSize || dataOffset > fileSize)
    {
        return false;
    }

    result.dataOffset = dataOffset;
    result.hasCrc = true;
    return true;
}
}
