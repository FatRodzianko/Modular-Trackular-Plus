#include "fileManagerDefs.h"
#include "mtStructs.h"

#include "mtSequencer.h"

#include "fileTransfer.h"
#include "instrumentFileValidation.h"
#include <string.h>

extern uint8_t externalRamFileManagerSrcBuff[READ_WRITE_BUFOR_SIZE];
extern uint8_t externalRamFileManagerBuffL[READ_WRITE_BUFOR_SIZE];
extern uint8_t externalRamFileManagerBuffR[READ_WRITE_BUFOR_SIZE];

cFileTransfer fileTransfer;

FileSystem::FileHandle transferFile = nullptr;
FileSystem::FileHandle transferFile2 = nullptr;

strWavFileHeader sampleHead;

//#define READ_WRITE_BUFOR_SIZE 9600
// extern uint8_t g_bufferReadWrite[READ_WRITE_BUFOR_SIZE];

void cFileTransfer::init(FileSystem* fileSystem)
{
    this->fileSystem = fileSystem;
}

void cFileTransfer::clear()
{
    sourceMemComplited = 0;
    transferStep = 0;
    sourceMemTotal = 0;
}

uint8_t cFileTransfer::loadFileToMemory(const char* file,
                                        uint8_t* memory,
                                        uint32_t memSize,
                                        uint8_t mode)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(file))
            return fileTransferFileNoExist;

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kRead, &transferFile);
        if (isOpen)
        {
            memset(memory, 0, memSize);

            uint32_t fileSize = fileSystem->getFileSize(transferFile);
            if (fileSize < memSize)
            {
                memSize = fileSize;
            }
            sourceMemTotal = memSize;
            memStep = (mode == fileWholeOnce)      ? sourceMemTotal
                      : (mode == fileDivIntoParts) ? memoryReadPerPart
                                                   : READ_WRITE_BUFOR_SIZE;
            sourceMemComplited = 0;
            transferStep = 1;
        }
    }

    if (transferStep == 1)
    {
        int32_t result = fileSystem->readFile(memory + sourceMemComplited, memStep, transferFile);

        if (result >= 0)
        {
            sourceMemComplited += result;

            if (result == 0) // koniec pliku
            {
                transferStep = 2;
            }
            else if (sourceMemComplited >=
                     memSize) // wczytany wiekszy plik << niebezpieczne przekroczenie pamieci
            {
                transferStep = 2;
            }
            else
            {
                if (mode != fileWholeOnce)
                {
                    return fileTransferInProgress;
                }
            }
        }
        // else wysypuje error na koncu funkcji \/
    }

    if (transferStep == 2)
    {
        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

uint8_t cFileTransfer::loadFileToMemory(const char* file,
                                        uint8_t* memory,
                                        uint32_t memSize,
                                        uint32_t memOffset,
                                        uint8_t mode)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(file))
            return fileTransferFileNoExist;

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kRead, &transferFile);
        if (isOpen)
        {
            sourceMemTotal = memSize;
            memStep = (mode == fileWholeOnce)      ? sourceMemTotal
                      : (mode == fileDivIntoParts) ? memoryReadPerPart
                                                   : READ_WRITE_BUFOR_SIZE;
            sourceMemComplited = 0;
            transferStep = 1;

            memset(memory, 0, memSize);
        }
    }

    if (transferStep == 1)
    {
        bool result = fileSystem->seekFile(memOffset, transferFile);
        if (result)
        {
            transferStep = 2;
        }
        else
        {
            fileSystem->closeFile(transferFile, false);
            transferStep = 0;
            return fileTransferError;
        }
    }

    if (transferStep == 2)
    {
        int32_t result = fileSystem->readFile(memory + sourceMemComplited, memStep, transferFile);

        if (result >= 0)
        {
            sourceMemComplited += result;

            if (result == 0) // koniec pliku
            {
                transferStep = 3;
            }
            else if (sourceMemComplited >=
                     memSize) // wczytany wiekszy plik << niebezpieczne przekroczenie pamieci
            {
                transferStep = 3;
            }
            else
            {
                return fileTransferInProgress;
            }
        }
        // else wysypuje error na koncu funkcji \/
    }

    if (transferStep == 3)
    {
        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

uint8_t cFileTransfer::loadSampleToMemory(const char* file,
                                          int16_t* memory,
                                          uint32_t& outSampleCount)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(file))
        {
            return fileTransferFileNoExist;
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kRead, &transferFile);

        if (isOpen)
        {
            memStep =
              READ_WRITE_BUFOR_SIZE; // wielokrotnosc 4*6*8 (rozne probkowania stereo w bajtach)
            sourceMemComplited = 0;
            convertedDataSize = 0;

            if (!readHeader(&sampleHead, transferFile) || !checkSampleValid(sampleHead))
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferFileNoValid;
            }

            // wylacznie konwersji kiedy format juz sie zgadza
            if (sampleHead.bitsPerSample == 16 && sampleHead.numChannels == 1 &&
                sampleHead.AudioFormat == 1)
            {
                conversionEnabled = 0;
            }
            else
            {
                conversionEnabled = 1;
                conversionConfig.sourceChannelCount = sampleHead.numChannels;
                conversionConfig.sourceBitsPerSample = sampleHead.bitsPerSample;
                conversionConfig.sourceSampleRate = sampleHead.sampleRate;
                conversionConfig.sourceFormat = sampleHead.AudioFormat;
                conversionConfig.destSampleRate = 44100;
            }

            transferStep = 1;

            sourceMemTotal = fileSystem->getFileAvailable(
              fileHandle); // pobiera wielkosc dopiero po przesciu przez header
        }
    }

    if (transferStep == 1)
    {
        int32_t result = fileSystem->readFile(externalRamFileManagerBuffL, memStep, transferFile);

        if (result >= 0)
        {
            if (conversionEnabled)
            {
                conversionConfig.inputData = externalRamFileManagerBuffL;
                conversionConfig.inputSize = result;
                conversionConfig.outPtrL = memory + convertedDataSize;
                conversionConfig.outPtrR = nullptr;

                convertedDataSize += convertAudioData(conversionConfig);
            }
            else
            {
                memcpy(memory + convertedDataSize, externalRamFileManagerBuffL, result);
                convertedDataSize += result / 2;
            }

            sourceMemComplited += result;
            transferBytesComplited += result;

            // przekroczenie mniejszego zatrzyma ladowanie
            if (sourceMemComplited >= sourceMemTotal ||
                sourceMemComplited >= sampleHead.subchunk2Size)
            {
                transferStep = 2;
            }
            else
            {
                return fileTransferInProgress;
            }
        }
        // else wysypuje error na koncu funkcji \/
    }

    if (transferStep == 2)
    {
        if (conversionEnabled)
        {
        }

        // wybiera wielkosc mniejsza z naglowka lub suma read
        uint32_t sizeFromHead = sampleHead.subchunk2Size / sampleHead.blockAlign;
        outSampleCount = (convertedDataSize < sizeFromHead) ? convertedDataSize : sizeFromHead;

        transferStep = 0;
        fileSystem->closeFile(transferFile, false);

        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

bool cFileTransfer::copyFile(FileSystem* fileSystem,
                             const char* srcFilepath,
                             const char* dstFilepath)
{
    if (!fileSystem->exists(srcFilepath))
    {
        return false;
    }

    if (fileSystem->exists(dstFilepath))
    {
        fileSystem->remove(dstFilepath);
    }

    bool result = false;
    bool destinationOpened = false;
    FileSystem::FileHandle sourceHandle = nullptr;
    FileSystem::FileHandle destinationHandle = nullptr;

    auto [isSourceOpen, openedSourceHandle] =
      fileSystem->openFile(srcFilepath, FileSystem::FileOpenMode::kRead, &transferFile);
    sourceHandle = openedSourceHandle;

    if (isSourceOpen)
    {
        auto [isDestinationOpen, openedDestinationHandle] =
          fileSystem->openFile(dstFilepath, FileSystem::FileOpenMode::kWriteCreateNew, &transferFile2);
        destinationHandle = openedDestinationHandle;
        destinationOpened = isDestinationOpen;

        if (isDestinationOpen)
        {
            const size_t sourceSize = fileSystem->getFileSize(sourceHandle);
            size_t bytesCopied = 0;
            result = true;

            while (bytesCopied < sourceSize)
            {
                const size_t bytesRemaining = sourceSize - bytesCopied;
                const size_t bytesToRead =
                  bytesRemaining < READ_WRITE_BUFOR_SIZE ? bytesRemaining : READ_WRITE_BUFOR_SIZE;
                const int32_t readResult =
                  fileSystem->readFile(externalRamFileManagerBuffL, bytesToRead, sourceHandle);

                if (readResult <= 0 || static_cast<size_t>(readResult) > bytesToRead)
                {
                    result = false;
                    break;
                }

                const size_t bytesRead = static_cast<size_t>(readResult);
                size_t bytesWritten = 0;
                while (bytesWritten < bytesRead)
                {
                    const size_t bytesToWrite = bytesRead - bytesWritten;
                    const int32_t writeResult = fileSystem->writeFile(
                      externalRamFileManagerBuffL + bytesWritten, bytesToWrite, destinationHandle);

                    if (writeResult <= 0 || static_cast<size_t>(writeResult) > bytesToWrite)
                    {
                        result = false;
                        break;
                    }

                    bytesWritten += static_cast<size_t>(writeResult);
                }

                if (!result)
                {
                    break;
                }

                bytesCopied += bytesRead;
            }
        }
    }

    if (sourceHandle != nullptr)
    {
        fileSystem->closeFile(sourceHandle, false);
    }
    if (destinationHandle != nullptr)
    {
        fileSystem->closeFile(destinationHandle, false);
    }
    if (destinationOpened && !result)
    {
        fileSystem->remove(dstFilepath);
    }

    return result;
}

uint8_t cFileTransfer::copyFile(const char* src, const char* dest)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(src))
            return fileTransferFileNoExist;
        if (fileSystem->exists(dest))
        {
            fileSystem->remove(dest);
        }

        auto [isSrcOpen, srcFileHandle] =
          fileSystem->openFile(src, FileSystem::FileOpenMode::kRead, &transferFile);

        if (isSrcOpen)
        {
            auto [isDestOpen, destFileHandle] =
              fileSystem->openFile(dest, FileSystem::FileOpenMode::kWriteCreateNew, &transferFile2);

            if (isDestOpen)
            {
                sourceMemTotal = fileSystem->getFileSize(transferFile);

                memStep = READ_WRITE_BUFOR_SIZE; // caly mozliwy bufor
                sourceMemComplited = 0;
                transferStep = 1;
            }
        }
    }

    if (transferStep == 1)
    {
        int32_t read_result =
          fileSystem->readFile(externalRamFileManagerBuffL, memStep, transferFile);

        if (read_result == 0) // koniec pliku - koncz kopiowanie
        {
            transferStep = 2;
        }
        else if (read_result > 0)
        {
            int32_t write_result =
              fileSystem->writeFile(externalRamFileManagerBuffL, read_result, transferFile2);

            if (write_result >= 0)
            {
                sourceMemComplited += write_result;
                transferBytesComplited += write_result;

                if (sourceMemComplited >= sourceMemTotal) // koniec pliku
                {
                    transferStep = 2;
                }
                else if (read_result == write_result)
                {
                    return fileTransferInProgress;
                }
            }
        }
    }

    if (transferStep == 2)
    {
        uint32_t fileNameLen = strlen(src);
        bool invalidInstrument = false;

        if (fileNameLen >= 4 &&
            ((src[fileNameLen - 1] == 'i') || (src[fileNameLen - 1] == 'I')) &&
            ((src[fileNameLen - 2] == 't') || (src[fileNameLen - 2] == 'T')) &&
            ((src[fileNameLen - 3] == 'p') || (src[fileNameLen - 3] == 'P')) &&
            (src[fileNameLen - 4] == '.'))
        {
            fileSystem->seekFile(0, transferFile);
            strInstrumentFile instrumentFile;
            memset(&instrumentFile, 0, sizeof(instrumentFile));

            int32_t bytesRead =
              fileSystem->readFile(&instrumentFile, sizeof(instrumentFile), transferFile);
            InstrumentFileValidation::Result validation;

            if (bytesRead >= 0 &&
                InstrumentFileValidation::validate((const uint8_t*)&instrumentFile,
                                                   bytesRead,
                                                   sourceMemComplited,
                                                   sizeof(strInstrument),
                                                   INSTRUMENT_FILE_VERSION,
                                                   validation))
            {
                convertedDataSize = sourceMemComplited - validation.dataOffset;
            }
            else
            {
                invalidInstrument = true;
            }
        }

        transferStep = 0;

        fileSystem->closeFile(transferFile, false);
        fileSystem->closeFile(transferFile2, false);
        if (invalidInstrument)
        {
            fileSystem->remove(dest);
            return fileTransferFileNoValid;
        }
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    fileSystem->closeFile(transferFile2, false);
    return fileTransferError;
}

uint8_t cFileTransfer::copyInstrumentHeader(const char* src, const char* dest)
{
    if (!fileSystem->exists(src))
    {
        return fileTransferFileNoExist;
    }
    if (fileSystem->exists(dest))
    {
        fileSystem->remove(dest);
    }

    auto [isSrcOpen, srcFileHandle] =
      fileSystem->openFile(src, FileSystem::FileOpenMode::kRead, &transferFile);
    if (!isSrcOpen)
    {
        return fileTransferError;
    }

    int32_t readResult =
      fileSystem->readFile(externalRamFileManagerBuffL, sizeof(strInstrumentFile), transferFile);
    uint32_t sourceFileSize = fileSystem->getFileSize(transferFile);
    fileSystem->closeFile(transferFile, false);

    if (readResult < 0)
    {
        return fileTransferError;
    }

    InstrumentFileValidation::Result validation;
    if (!InstrumentFileValidation::validate(externalRamFileManagerBuffL,
                                            readResult,
                                            sourceFileSize,
                                            sizeof(strInstrument),
                                            INSTRUMENT_FILE_VERSION,
                                            validation))
    {
        return fileTransferFileNoValid;
    }
    uint32_t headerSize = validation.dataOffset;

    auto [isDestOpen, destFileHandle] =
      fileSystem->openFile(dest, FileSystem::FileOpenMode::kWriteCreateNew, &transferFile2);
    if (!isDestOpen)
    {
        return fileTransferError;
    }

    int32_t writeResult =
      fileSystem->writeFile(externalRamFileManagerBuffL, headerSize, transferFile2);
    fileSystem->closeFile(transferFile2, false);

    if (writeResult == (int32_t)headerSize)
    {
        transferBytesComplited += writeResult;
        return fileTransferEnd;
    }

    return fileTransferError;
}

uint8_t cFileTransfer::copySample(const char* src,
                                  const char* dest,
                                  uint16_t startSample,
                                  uint16_t endSample,
                                  uint32_t sampleRate)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(src))
            return fileTransferFileNoExist;
        if (fileSystem->exists(dest))
        {
            fileSystem->remove(dest);
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(src, FileSystem::FileOpenMode::kRead, &transferFile);

        if (isOpen)
        {
            if (!readHeader(&sampleHead, transferFile) || !checkSampleValid(sampleHead))
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferFileNoValid;
            }

            if (sampleRate != defaultSampleRate)
            {
                // trigger resampling in convertAudioData to target sampleRate
                // by faking source rate in inverse proportions
                sampleHead.sampleRate = (uint64_t)sampleHead.sampleRate * 44100 / sampleRate;
                sampleHead.byteRate = (uint64_t)sampleHead.byteRate * 44100 / sampleRate;
            }

            auto [isDestOpen, fileDestHandle] =
              fileSystem->openFile(dest, FileSystem::FileOpenMode::kWriteCreateNew, &transferFile2);
            if (isDestOpen)
            {
                int bytesPerSec =
                  sampleHead.sampleRate * sampleHead.numChannels * sampleHead.bitsPerSample / 8;

                conversionInPlace = (bytesPerSec >= 44100 * 1 * 2);
                sourceMemTotal = fileSystem->getFileAvailable(
                  transferFile); // pobiera wielkosc dopiero po przesciu przez header

                sourceMemTotal = (sourceMemTotal < sampleHead.subchunk2Size)
                                   ? sourceMemTotal
                                   : sampleHead.subchunk2Size;
                uint32_t seekPosition = 0;
                uint32_t endPosition = sourceMemTotal;
                uint32_t bytesPerSample = (sampleHead.bitsPerSample / 8) * sampleHead.numChannels;
                uint32_t totalSamplesCount = sourceMemTotal / bytesPerSample;

                if (startSample != 0)
                {
                    float from_pos = (float)startSample / MAX_16BIT;
                    seekPosition = ((uint32_t)(totalSamplesCount * from_pos)) * bytesPerSample;

                    fileSystem->seekFileCur(seekPosition, transferFile);
                }

                if (endSample != MAX_16BIT)
                {
                    float to_pos = (float)endSample / MAX_16BIT;
                    endPosition = ((uint32_t)(totalSamplesCount * to_pos)) * bytesPerSample;
                }

                sourceMemTotal = endPosition - seekPosition;

                memStep =
                  conversionInPlace ? READ_WRITE_BUFOR_SIZE
                                    : // wielokrotnosc 4*6*8 (rozne probkowania stereo w bajtach)
                    READ_WRITE_BUFOR_SIZE / 16; // wczytujemy 1/16 bufora, reszta na wynik konwersji
                sourceMemComplited = 0;

                fileSystem->seekFile(sizeof(sampleHead), transferFile2);

                // wylaczenie konwersji kiedy format juz sie zgadza
                conversionEnabled = sampleHead.bitsPerSample != 16 || sampleHead.numChannels != 1 ||
                                    sampleHead.AudioFormat != 1 || sampleHead.sampleRate != 44100;
                conversionConfig.sourceChannelCount = sampleHead.numChannels;
                conversionConfig.sourceBitsPerSample = sampleHead.bitsPerSample;
                conversionConfig.sourceSampleRate = sampleHead.sampleRate;
                conversionConfig.sourceFormat = sampleHead.AudioFormat;
                conversionConfig.destSampleRate = 44100;

                convertedDataSize = 0;
                transferStep = 1;
            }
        }
    }
    //--------------------------------------------------------
    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t read_result =
          fileSystem->readFile(externalRamFileManagerBuffL, memStep, transferFile);

        if (read_result == 0) // koniec pliku - koncz kopiowanie
        {
            transferStep = 2;
        }
        else if (read_result > 0)
        {
            int32_t converted_bytes;
            int16_t* out_buffer;
            if (conversionEnabled)
            {
                out_buffer = (int16_t*)externalRamFileManagerBuffL +
                             (conversionInPlace ? 0 : READ_WRITE_BUFOR_SIZE / 16);

                conversionConfig.inputData = externalRamFileManagerBuffL;
                conversionConfig.inputSize = read_result;
                conversionConfig.outPtrL = out_buffer;
                conversionConfig.outPtrR = nullptr;

                uint32_t samplesConverted = convertAudioData(conversionConfig);
                converted_bytes = samplesConverted * 2; // liczba probek razy wielkosc probki !
            }
            else
            {
                out_buffer = (int16_t*)externalRamFileManagerBuffL;
                converted_bytes = read_result;
            }

            if (converted_bytes > 0)
            {
                int32_t write_result =
                  fileSystem->writeFile(out_buffer, converted_bytes, transferFile2);

                convertedDataSize += converted_bytes; // tu convertedDataSize to ilsoc bajtow
                sourceMemComplited += read_result;
                transferBytesComplited += read_result;

                if (write_result > 0)
                {
                    if (sourceMemComplited >= sourceMemTotal ||
                        sourceMemComplited >=
                          sampleHead.subchunk2Size) // przekroczenie mniejszego zatrzyma ladowanie
                    {
                        transferStep = 2;
                    }
                    else if (converted_bytes == write_result)
                    {
                        return fileTransferInProgress;
                    }
                }
            }
            else
            {
                transferStep = 2;
            }
        }
        // else wysypuje error na koncu funkcji \/
    }
    //--------------------------------------------------------
    if (transferStep == 2)
    {
        fillHeader(false, convertedDataSize);
        fileSystem->seekFile(0, transferFile2);
        fileSystem->writeFile(&sampleHead, sizeof(sampleHead), transferFile2);

        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        fileSystem->closeFile(transferFile2, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    convertedDataSize = 0;
    fileSystem->closeFile(transferFile, false);
    fileSystem->closeFile(transferFile2, false);
    return fileTransferError;
}

uint8_t cFileTransfer::saveMemoryToFile(uint8_t* memory, const char* file, uint32_t memSize)
{
    if (transferStep == 0)
    {
        if (fileSystem->exists(file))
        {
            fileSystem->remove(file);
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kWriteOpen, &transferFile);
        if (isOpen)
        {
            sourceMemTotal = memSize;
            memStep = (memSize < READ_WRITE_BUFOR_SIZE)
                        ? memSize
                        : READ_WRITE_BUFOR_SIZE; // maksymalne paczki jak bufor mimo ze nie
                                                 // kozystanie z bufora
            sourceMemComplited = 0;
            transferStep = 1;
        }
    }

    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t write_result =
          fileSystem->writeFile(memory + sourceMemComplited, memStep, transferFile);
        if (write_result >= 0)
        {
            sourceMemComplited += write_result;
            if (sourceMemComplited >= sourceMemTotal) // koniec pliku
            {
                transferStep = 2;
            }
            else if ((uint32_t)write_result == memStep)
            {
                return fileTransferInProgress;
            }
        }
    }

    if (transferStep == 2)
    {
        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

void cFileTransfer::endTransfer()
{
    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
}

void cFileTransfer::fillHeader(bool stereo, uint32_t size)
{
    memset(&sampleHead, 0, sizeof(sampleHead));

    sampleHead.chunkId = 0x46464952; // "RIFF"
    sampleHead.chunkSize = size + 36;
    sampleHead.waveId = WAVEID;          // "WAVE"
    sampleHead.subchunk1Id = 0x20746d66; // "fmt "
    sampleHead.subchunk1Size = 16;
    sampleHead.AudioFormat = 1;
    sampleHead.numChannels = stereo ? 2 : 1;
    sampleHead.sampleRate = 44100;
    sampleHead.bitsPerSample = 16;
    sampleHead.byteRate =
      sampleHead.sampleRate * sampleHead.numChannels * (sampleHead.bitsPerSample / 8);
    sampleHead.blockAlign = sampleHead.numChannels * (sampleHead.bitsPerSample / 8);
    sampleHead.subchunk2Id = 0x61746164; // "data"
    sampleHead.subchunk2Size = size;
}

//--------------------------------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------   obsluga nowych instrumentow
//--------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------------------
uint8_t cFileTransfer::loadInstrumentRawSampleDataToMemory(const char* file,
                                                           uint32_t fileOffset,
                                                           uint8_t* memory,
                                                           uint32_t sizeLimitBytes,
                                                           uint32_t& outSampleCount)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(file))
            return fileTransferFileNoExist;

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kRead, &transferFile);
        if (isOpen)
        {
            sourceMemTotal = 0;
            memStep = READ_WRITE_BUFOR_SIZE;
            sourceMemComplited = 0;

            int32_t result = fileSystem->seekFile(fileOffset, transferFile);

            if (result >= 0)
            {
                sourceMemTotal = fileSystem->getFileAvailable(transferFile);
                sourceMemTotal = std::min(sourceMemTotal, sizeLimitBytes);
                transferStep = 1;
            }
            else
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferError;
            }
        }
    }

    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t result = fileSystem->readFile(memory + sourceMemComplited, memStep, transferFile);

        if (result >= 0)
        {
            sourceMemComplited += result;
            transferBytesComplited += result;

            if (result == 0) // koniec pliku
            {
                transferStep = 2;
            }
            else if (sourceMemComplited >=
                     sourceMemTotal) // wczytany wiekszy plik << niebezpieczne przekroczenie pamieci
            {
                transferStep = 2;
            }
            else
            {
                return fileTransferInProgress;
            }
        }
        // else wysypuje error na koncu funkcji \/
    }

    if (transferStep == 2)
    {
        outSampleCount =
          sourceMemComplited > sourceMemTotal ? sourceMemTotal / 2 : sourceMemComplited / 2;
        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

uint8_t cFileTransfer::saveSampleRawDataToFile(uint8_t* memory,
                                               uint32_t memSize,
                                               const char* file,
                                               uint32_t fileOffset,
                                               uint8_t* extraMemory)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(file))
        {
            return fileTransferFileNoValid;
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kWriteOpen, &transferFile);
        if (isOpen)
        {
            sourceMemTotal = memSize;
            memStep = (memSize < READ_WRITE_BUFOR_SIZE)
                        ? memSize
                        : READ_WRITE_BUFOR_SIZE; // maksymalne paczki jak bufor mimo ze nie
                                                 // kozystanie z bufora
            sourceMemComplited = 0;
            srcDataBuff = memory;

            if (fileOffset > 0)
            {
                if (fileSystem->seekFile(fileOffset, transferFile))
                {
                    transferStep = 1;
                }
            }
            else
            {
                transferStep = 1;
            }
        }
    }

    if (transferStep == 1)
    {
        uint32_t stepSize = memStep;
        if (sourceMemTotal - sourceMemComplited < stepSize)
        {
            stepSize = sourceMemTotal - sourceMemComplited;
        }

        int32_t write_result =
          fileSystem->writeFile(srcDataBuff + sourceMemComplited, stepSize, transferFile);

        if (write_result >= 0)
        {
            sourceMemComplited += write_result;
            transferBytesComplited += write_result;

            if (sourceMemComplited >= sourceMemTotal) // koniec pliku
            {
                if (extraMemory != nullptr && srcDataBuff == memory)
                {
                    srcDataBuff = extraMemory;
                    sourceMemComplited = 0;
                    return fileTransferInProgress;
                }

                transferStep = 2;
            }
            else if ((uint32_t)write_result == memStep)
            {
                return fileTransferInProgress;
            }
        }
    }

    if (transferStep == 2)
    {
        transferStep = 0;

        fileSystem->endFile(transferFile);
        fileSystem->closeFile(transferFile, false);

        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

uint8_t cFileTransfer::saveInstrumentStructToInstrumentFile(uint8_t* memory,
                                                            uint32_t memSize,
                                                            char* file,
                                                            uint32_t* outFileSize)
{
    if (transferStep == 0)
    {
        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kWriteOpen, &transferFile);
        if (isOpen)
        {
            sourceMemTotal = memSize;
            memStep = (memSize < READ_WRITE_BUFOR_SIZE)
                        ? memSize
                        : READ_WRITE_BUFOR_SIZE; // maksymalne paczki jak bufor mimo ze nie
                                                 // kozystanie z bufora
            sourceMemComplited = 0;
            transferStep = 1;
        }
        else
        {
            transferStep = 0;
            if (transferFile != nullptr)
            {
                fileSystem->closeFile(transferFile, false);
            }
            return fileTransferError;
        }
    }

    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t write_result =
          fileSystem->writeFile(memory + sourceMemComplited, memStep, transferFile);

        if (write_result >= 0)
        {
            sourceMemComplited += write_result;
            if (sourceMemComplited >= sourceMemTotal) // koniec pliku
            {
                transferStep = 2;
            }
            else if ((uint32_t)write_result == memStep)
            {
                return fileTransferInProgress;
            }
        }
    }

    if (transferStep == 2)
    {
        *outFileSize = fileSystem->getFileSize(transferFile);
        transferStep = 0;
        if (transferFile != nullptr)
        {
            fileSystem->closeFile(transferFile, false);
        }
        return fileTransferEnd;
    }

    transferStep = 0;
    if (transferFile != nullptr)
    {
        fileSystem->closeFile(transferFile, false);
    }
    return fileTransferError;
}

uint8_t cFileTransfer::copySampleToInstrumentFile(const char* src,
                                                  const char* dest,
                                                  uint16_t startSample,
                                                  uint16_t endSample,
                                                  uint32_t sampleRate,
                                                  uint32_t fileOffset)
{
    if (transferStep == 0)
    {
        if (!fileSystem->exists(src))
        {
            return fileTransferFileNoExist;
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(src, FileSystem::FileOpenMode::kRead, &transferFile);
        if (isOpen)
        {
            if (!readHeader(&sampleHead, transferFile) || !checkSampleValid(sampleHead))
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferFileNoValid;
            }

            if (sampleRate != defaultSampleRate)
            {
                // trigger resampling in convertAudioData to target sampleRate
                // by faking source rate in inverse proportions
                sampleHead.sampleRate = (uint64_t)sampleHead.sampleRate * 44100 / sampleRate;
                sampleHead.byteRate = (uint64_t)sampleHead.byteRate * 44100 / sampleRate;
            }

            auto [isDestOpen, fileDestHandle] =
              fileSystem->openFile(dest, FileSystem::FileOpenMode::kWriteOpen, &transferFile2);

            if (isDestOpen)
            {
                uint32_t sourceBytesPerSample =
                  (sampleHead.bitsPerSample / 8) * sampleHead.numChannels;
                uint32_t sourceBytesPerSec = sampleHead.sampleRate * sourceBytesPerSample;

                conversionInPlace = (sourceBytesPerSec >= 44100 * 1 * 2);

                sourceMemTotal = fileSystem->getFileAvailable(
                  transferFile); // pobiera wielkość za nagłówkiem do końca
                sourceMemTotal = (sourceMemTotal <= sampleHead.subchunk2Size)
                                   ? sourceMemTotal
                                   : sampleHead.subchunk2Size;

                uint32_t seekPosition = 0;
                uint32_t endPosition = sourceMemTotal;
                uint32_t sourceTotalSamplesCount = sourceMemTotal / sourceBytesPerSample;

                if (startSample != 0)
                {
                    float from_pos = (float)startSample / MAX_16BIT;
                    seekPosition =
                      ((uint32_t)(sourceTotalSamplesCount * from_pos)) * sourceBytesPerSample;

                    fileSystem->seekFileCur(seekPosition, transferFile);
                }

                if (endSample != MAX_16BIT)
                {
                    float to_pos = (float)endSample / MAX_16BIT;
                    endPosition =
                      ((uint32_t)(sourceTotalSamplesCount * to_pos)) * sourceBytesPerSample;
                }

                sourceMemTotal = endPosition - seekPosition;
                sourceMemComplited = 0;
                memStep =
                  conversionInPlace
                    ? READ_WRITE_BUFOR_SIZE       //  4*6*8 (rozne probkowania stereo)
                    : READ_WRITE_BUFOR_SIZE / 16; // laduj 1/16 bufora, reszta na wynik konwersji

                destDataOffset = fileOffset;

                bool result = fileSystem->seekFile(fileOffset, transferFile2);
                if (result)
                {
                    // wylaczenie konwersji kiedy format juz sie zgadza
                    conversionEnabled =
                      sampleHead.bitsPerSample != 16 || sampleHead.numChannels != 1 ||
                      sampleHead.AudioFormat != 1 || sampleHead.sampleRate != 44100;
                    conversionConfig.sourceChannelCount = sampleHead.numChannels;
                    conversionConfig.sourceBitsPerSample = sampleHead.bitsPerSample;
                    conversionConfig.sourceSampleRate = sampleHead.sampleRate;
                    conversionConfig.sourceFormat = sampleHead.AudioFormat;
                    conversionConfig.destSampleRate = 44100;

                    destSampleCount = sourceTotalSamplesCount * 44100 / sampleHead.sampleRate;

                    convertedDataSize = 0;
                    transferStep = 1;
                }
            }
        }
    }
    //--------------------------------------------------------
    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t bytesReadFromSource =
          fileSystem->readFile(externalRamFileManagerSrcBuff, memStep, transferFile);

        if (bytesReadFromSource == 0) // koniec pliku - koncz kopiowanie
        {
            transferStep = 2;
        }
        else if (bytesReadFromSource > 0)
        {
            int32_t bytesConverted = 0;
            int16_t* outBufferL = nullptr;
            int16_t* outBufferR = nullptr;

            //            switch (channelMode)
            //            {
            //                case ChannelMode::kLeftAndRight:
            //                    outBufferL = (int16_t*)externalRamFileManagerBuffL;
            //                    outBufferR = (int16_t*)externalRamFileManagerBuffR;
            //                    break;
            //                case ChannelMode::kLeft: outBufferL =
            //                (int16_t*)externalRamFileManagerBuffL; break; case
            //                ChannelMode::kRight: outBufferR =
            //                (int16_t*)externalRamFileManagerBuffR; break;
            //            }

            if (conversionEnabled)
            {
                conversionConfig.inputData = externalRamFileManagerSrcBuff;
                conversionConfig.inputSize = bytesReadFromSource;
                conversionConfig.outPtrL = outBufferL;
                conversionConfig.outPtrR = outBufferR;

                uint32_t samplesConverted = convertAudioData(conversionConfig);
                bytesConverted = 2 * samplesConverted; // liczba probek razy wielkosc probki !
            }
            else // no conversion = always mono left channel buffer
            {
                outBufferR = nullptr;
                outBufferL = (int16_t*)externalRamFileManagerSrcBuff;
                bytesConverted = bytesReadFromSource;
            }
            convertedDataSize += bytesConverted;

            if (bytesReadFromSource > 0)
            {
                int32_t bytesWroteL = 0;
                int32_t bytesWroteR = 0;

                if (outBufferL != nullptr)
                {
                    fileSystem->seekFile(destDataOffset + sourceMemComplited, transferFile2);
                    bytesWroteL = fileSystem->writeFile(outBufferL, bytesConverted, transferFile2);
                }

                if (outBufferR != nullptr)
                {
                    fileSystem->seekFile(destDataOffset + destSampleCount + sourceMemComplited,
                                         transferFile2);
                    bytesWroteR = fileSystem->writeFile(outBufferL, bytesConverted, transferFile2);
                }

                sourceMemComplited += bytesReadFromSource;

                if (bytesWroteL > 0 || bytesWroteR > 0)
                {
                    if (sourceMemComplited >= sourceMemTotal ||
                        sourceMemComplited >= sampleHead.subchunk2Size)
                    {
                        // przekroczenie mniejszego zatrzyma ladowanie
                        transferStep = 2;
                    }
                    else if (bytesConverted == bytesWroteL)
                    {
                        return fileTransferInProgress;
                    }
                }
            }
            else
            {
                transferStep = 2;
            }
        }
        // else wysypuje error na koncu funkcji \/
    }
    //--------------------------------------------------------
    if (transferStep == 2)
    {
        transferStep = 0;

        fileSystem->endFile(transferFile2);

        fileSystem->closeFile(transferFile, false);
        fileSystem->closeFile(transferFile2, false);

        return fileTransferEnd;
    }

    transferStep = 0;
    convertedDataSize = 0;
    fileSystem->closeFile(transferFile, false);
    fileSystem->closeFile(transferFile2, false);
    return fileTransferError;
}

uint8_t cFileTransfer::saveMemoryToWav(int16_t* memoryChannelL,
                                       int16_t* memoryChannelR,
                                       uint32_t samplesCount,
                                       const char* file)
{
    if (transferStep == 0)
    {
        if (fileSystem->exists(file))
        {
            fileSystem->remove(file);
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(file, FileSystem::FileOpenMode::kWriteCreateNew, &transferFile);
        if (isOpen)
        {
            if (memoryChannelL == memoryChannelR)
            {
                sourceMemTotal = samplesCount * 2;
            }
            else
            {
                conversionInPlace = true; // combine two buffers into stereo file
                sourceMemTotal = samplesCount * 4;
            }

            memStep = READ_WRITE_BUFOR_SIZE;
            sourceMemComplited = 0;
            transferStep = 1;
        }
    }

    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int16_t* data = memoryChannelL + sourceMemComplited / 2;

        if (conversionInPlace)
        {
            uint32_t offset = sourceMemComplited / 4;
            for (uint32_t sample = 0; sample < memStep / 2; sample++)
            {
                ((int16_t*)externalRamFileManagerSrcBuff)[2 * sample] =
                  memoryChannelL[sample + offset];
                ((int16_t*)externalRamFileManagerSrcBuff)[2 * sample + 1] =
                  memoryChannelR[sample + offset];
            }

            data = (int16_t*)externalRamFileManagerSrcBuff;
        }

        int32_t write_result = fileSystem->writeFile((uint8_t*)data, memStep, transferFile);

        if (write_result >= 0)
        {
            sourceMemComplited += write_result;
            transferBytesComplited += write_result;

            if (sourceMemComplited >= sourceMemTotal)
            {
                transferStep = 2;
            }
            else if ((uint32_t)write_result == memStep)
            {
                return fileTransferInProgress;
            }
        }
    }

    if (transferStep == 2)
    {
        fillHeader(conversionInPlace, sourceMemComplited);

        fileSystem->seekFile(0, transferFile);
        fileSystem->writeFile(&sampleHead, sizeof(sampleHead), transferFile);

        transferStep = 0;
        fileSystem->closeFile(transferFile, false);
        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}

uint8_t cFileTransfer::loadSampleDataToMemory(
  FileManagerOperation::SampleLoadData* sampleImportData,
  int16_t* memoryAddress,
  uint32_t targetSampleRate)
{
    if (transferStep == 0)
    {
        char srcFilePath[PATCH_SIZE];
        snprintf(srcFilePath,
                 sizeof(srcFilePath),
                 "%s/%s",
                 sampleImportData->filePath,
                 sampleImportData->fileName);

        if (!fileSystem->exists(srcFilePath))
        {
            return fileTransferFileNoExist;
        }

        auto [isOpen, fileHandle] =
          fileSystem->openFile(srcFilePath, FileSystem::FileOpenMode::kRead, &transferFile);

        if (isOpen)
        {
            memStep = READ_WRITE_BUFOR_SIZE;
            sourceMemComplited = 0;
            convertedDataSize = 0;
            conversionConfig.sourceChannelCount = sampleImportData->sourceChannelCount;
            conversionConfig.sourceBitsPerSample = sampleImportData->sourceBitsPerSample;
            conversionConfig.sourceSampleRate = sampleImportData->sourceSampleRate;
            conversionConfig.sourceFormat = sampleImportData->sourceFormat;
            conversionConfig.destSampleRate = targetSampleRate;

            fileSystem->seekFile(sampleImportData->fileOffset, transferFile);

            sourceMemTotal = fileSystem->getFileAvailable(transferFile);

            if (sourceMemTotal < sampleImportData->sourceAllChannelBytesPerSample *
                                   sampleImportData->sourceTotalSamplesCountPerChannel)
            {
                fileSystem->closeFile(transferFile, false);
                transferStep = 0;
                return fileTransferFileNoValid;
            }
            else
            {
                sourceMemTotal = sampleImportData->sourceAllChannelBytesPerSample *
                                 sampleImportData->sourceTotalSamplesCountPerChannel;
            }

            transferStep = 1;
        }
    }

    if (transferStep == 1)
    {
        if (sourceMemTotal - sourceMemComplited < memStep)
        {
            memStep = sourceMemTotal - sourceMemComplited;
        }

        int32_t bytesReadFromSource =
          fileSystem->readFile(externalRamFileManagerSrcBuff, memStep, transferFile);

        if (bytesReadFromSource == 0)
        {
            transferStep = 2;
        }
        else if (bytesReadFromSource > 0)
        {
            int32_t bytesConverted = 0;
            int16_t* outBufferL = nullptr;
            int16_t* outBufferR = nullptr;

            switch (sampleImportData->destChannelCount)
            {
                case 1: outBufferL = memoryAddress + convertedDataSize / 2; break;
                case 2:
                    outBufferL = memoryAddress + convertedDataSize / 2;
                    outBufferR = memoryAddress + sampleImportData->destSampleCountPerChannel +
                                 convertedDataSize / 2;
                    break;
                default: break;
            }

            // conversion means sample is not mono or has incorrect other parameters
            // stereo need conversion to split into two buffers
            if (sampleImportData->conversion)
            {
                conversionConfig.inputData = externalRamFileManagerSrcBuff;
                conversionConfig.inputSize = bytesReadFromSource;
                conversionConfig.outPtrL = outBufferL;
                conversionConfig.outPtrR = outBufferR;

                uint32_t samplesConverted = convertAudioData(conversionConfig);
                bytesConverted = samplesConverted * 2;
            }
            else // no conversion = always mono left channel buffer
            {
                memcpy((uint8_t*)outBufferL,
                       (uint8_t*)externalRamFileManagerSrcBuff,
                       bytesReadFromSource);
                bytesConverted = bytesReadFromSource;
            }
            convertedDataSize += bytesConverted;
            sourceMemComplited += bytesReadFromSource;
            transferBytesComplited += bytesReadFromSource;

            if (sourceMemComplited >= sourceMemTotal ||
                convertedDataSize >= sampleImportData->destSampleCountPerChannel * 2)
            {
                transferStep = 2;
            }
            else
            {
                return fileTransferInProgress;
            }
        }
    }

    if (transferStep == 2)
    {
        uint32_t samplesWritten = convertedDataSize / 2;
        if (samplesWritten < sampleImportData->destSampleCountPerChannel)
        {
            uint32_t samplesRemaining =
              sampleImportData->destSampleCountPerChannel - samplesWritten;
            memset(memoryAddress + samplesWritten, 0, samplesRemaining * sizeof(int16_t));
            if (sampleImportData->destChannelCount == 2)
            {
                memset(memoryAddress + sampleImportData->destSampleCountPerChannel + samplesWritten,
                       0,
                       samplesRemaining * sizeof(int16_t));
            }
        }

        transferStep = 0;
        fileSystem->closeFile(transferFile, false);

        return fileTransferEnd;
    }

    transferStep = 0;
    fileSystem->closeFile(transferFile, false);
    return fileTransferError;
}
