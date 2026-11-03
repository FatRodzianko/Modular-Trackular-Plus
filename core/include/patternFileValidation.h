#ifndef PATTERN_FILE_VALIDATION_H
#define PATTERN_FILE_VALIDATION_H

#include <stddef.h>
#include <stdint.h>

namespace PatternFileValidation
{
constexpr uint8_t kLastLegacyPatternVersion = 5;
constexpr uint16_t k8TracksPatternSizeV5 = 6184;
constexpr uint16_t k12TracksPatternSizeV5 = 9260;
constexpr uint16_t k16TracksPatternSizeV5 = 12336;
constexpr uint16_t k8TracksPatternSize = 8232;
constexpr uint16_t k12TracksPatternSize = 12332;
constexpr uint16_t k16TracksPatternSize = 16432;
constexpr size_t kCrcSize = sizeof(uint32_t);

struct Layout
{
    size_t crcDataSize;
    bool hasCrc;
};

inline bool isKnownPatternSize(uint8_t version, uint16_t size)
{
    if (version <= kLastLegacyPatternVersion)
    {
        return size == k8TracksPatternSizeV5 || size == k12TracksPatternSizeV5 ||
               size == k16TracksPatternSizeV5;
    }

    return size == k8TracksPatternSize || size == k12TracksPatternSize ||
           size == k16TracksPatternSize;
}

inline bool resolveLayout(uint8_t version,
                          uint16_t declaredSize,
                          size_t fileSize,
                          Layout& layout)
{
    if (!isKnownPatternSize(version, declaredSize))
    {
        return false;
    }

    if (fileSize == declaredSize)
    {
        layout = { fileSize - kCrcSize, true };
        return true;
    }

    if (version <= kLastLegacyPatternVersion && fileSize + kCrcSize == declaredSize)
    {
        layout = { fileSize, false };
        return true;
    }

    return false;
}
}

#endif
