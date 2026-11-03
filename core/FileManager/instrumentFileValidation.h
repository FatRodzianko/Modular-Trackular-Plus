#ifndef FILEMANAGER_INSTRUMENTFILEVALIDATION_H_
#define FILEMANAGER_INSTRUMENTFILEVALIDATION_H_

#include <stddef.h>
#include <stdint.h>

namespace InstrumentFileValidation
{
struct Result
{
    uint16_t instrumentSize = 0;
    uint32_t dataOffset = 0;
    bool hasCrc = false;
};

bool validate(const uint8_t* data,
              uint32_t loadedSize,
              uint32_t fileSize,
              uint16_t currentInstrumentSize,
              uint8_t currentFileVersion,
              Result& result);
}

#endif /* FILEMANAGER_INSTRUMENTFILEVALIDATION_H_ */
