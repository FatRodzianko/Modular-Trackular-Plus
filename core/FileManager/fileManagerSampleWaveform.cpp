#include "fileManagerDefs.h"
#include "mtStructs.h"

#include "fileManager.h"
#include "fileTransfer.h"
#include <vector>

// waveform draw import
uint32_t samplesCount = 0;
uint32_t waveformfileSizeLeft = 0;
int waveformChannels = 0;
float waveformStepSamples;
uint16_t waveBytesPerSample;
uint16_t waveformIndex;
std::vector<int8_t*>* waveformStepLower;
std::vector<int8_t*>* waveformStepUpper;

uint16_t waveformAudioFormat;
uint16_t waveformBitsPerSample;

static inline int16_t getConvertedSample(uint32_t& sample);

void cFileManager::updateDrawSampleWaveform()
{
    switch (currentOperationStep)
    {
        case 0: drawSampleWaveformInit(); break;
        case 1: drawSampleWaveformUpdate(); break;
        case 2: drawSampleWaveformFinish(); break;
        default: stopOperationWithError(fmDrawingSampleWaveformError); break;
    }
}

uint32_t cFileManager::getImportingSamplesCount()
{
    return samplesCount;
}

void cFileManager::drawSampleWaveformInit()
{
    char filePath[PATCH_SIZE];
    volatile int pathSize = sizeof(filePath);
    snprintf(filePath, pathSize, "%s/%s", explorerCurrentPath, explorerList[importCurrentFile]);

    if (fileSystem->isFileOpen(wavfile))
    {
        fileSystem->closeFile(wavfile, false);
    }

    [[maybe_unused]] auto [isOpen, fileHandle] =
      fileSystem->openFile(filePath, FileSystem::FileOpenMode::kRead, &wavfile);

    if (!isOpen)
    {
        stopOperationWithError(fmDrawingSampleWaveformError);
        return;
    }

    strWavFileHeader wavHeader;
    if (!readHeader(&wavHeader, wavfile) || !checkSampleValid(wavHeader))
    {
        fileSystem->closeFile(wavfile, false);
        stopOperationWithError(fmDrawingSampleWaveformError);
        return;
    }

    size_t fileAvailable = fileSystem->getFileAvailable(wavfile);

    waveBytesPerSample = (wavHeader.bitsPerSample / 8) * wavHeader.numChannels;
    waveformBitsPerSample = wavHeader.bitsPerSample;
    waveformAudioFormat = wavHeader.AudioFormat;
    waveformfileSizeLeft =
      (fileAvailable <= wavHeader.subchunk2Size) ? fileAvailable : wavHeader.subchunk2Size;

    samplesCount = waveformfileSizeLeft / waveBytesPerSample;

    waveformStepLower->erase(waveformStepLower->begin() +
                               std::min<int>(wavHeader.numChannels, waveformStepLower->size()),
                             waveformStepLower->end());
    waveformStepUpper->erase(waveformStepUpper->begin() +
                               std::min<int>(wavHeader.numChannels, waveformStepUpper->size()),
                             waveformStepUpper->end());

    if (samplesCount == 0 || waveformfileSizeLeft == 0)
    {
        status = fmDrawingSampleWaveformEnd;
        currentOperationStep = 0;
        currentOperation = fmNoOperation;
        waveformStepLower->clear();
        waveformStepUpper->clear();

        if (fileSystem->isFileOpen(wavfile))
        {
            fileSystem->closeFile(wavfile, false);
        }

        return;
    }

    waveformStepSamples = samplesCount / 600.0f;
    if (waveformStepSamples < 1)
    {
        waveformStepSamples = 1;
    }

    waveformIndex = 0;

    moveToNextOperationStep();
}

void cFileManager::drawSampleWaveformUpdate()
{
    if (fileSystem->isFileOpen(wavfile))
    {
        uint32_t samplein;
        int16_t sampleout;

        while (waveformIndex < 600 &&
               (waveformStepSamples * waveformIndex) < (waveformfileSizeLeft / waveBytesPerSample))
        {
            assert(waveformStepLower->size() == waveformStepUpper->size());
            for (int channel = 0; channel < (int)waveformStepLower->size(); channel++)
            {
                fileSystem->readFile(&samplein, 4, wavfile);
                sampleout = getConvertedSample(samplein);

                if (sampleout > 0)
                {
                    (*waveformStepUpper)[channel][waveformIndex] = sampleout / 300;
                    (*waveformStepLower)[channel][waveformIndex] = -(sampleout / 300);
                }
                else if (sampleout < 0)
                {
                    (*waveformStepUpper)[channel][waveformIndex] = -(sampleout / 300);
                    (*waveformStepLower)[channel][waveformIndex] = sampleout / 300;
                }
                else
                {
                    (*waveformStepUpper)[channel][waveformIndex] = 0;
                    (*waveformStepLower)[channel][waveformIndex] = 0;
                }

                fileSystem->seekFileCur((waveBytesPerSample / (int)waveformStepLower->size() - 4),
                                        wavfile);
            }

            waveformIndex++;
            fileSystem->seekFileCur((uint32_t)waveformStepSamples * waveBytesPerSample -
                                      waveformStepLower->size() * waveBytesPerSample,
                                    wavfile);
        }
    }

    fileSystem->closeFile(wavfile, false);

    moveToNextOperationStep();
}

static inline int16_t getConvertedSample(uint32_t& sample)
{
    uint8_t* inputData = (uint8_t*)&sample;

    switch (waveformAudioFormat)
    {
        case wavFormatPCM:
            switch (waveformBitsPerSample)
            {
                case 8: return (((int16_t)*inputData) - 128) << 8;
                case 16: return *(int16_t*)(inputData);
                case 24:
                {
                    // to avoid unaligned access
                    uint8_t lowerByte = inputData[1];
                    uint8_t upperByte = inputData[2];

                    return (int16_t)((uint16_t)lowerByte | (uint16_t)upperByte << 8);
                }
                case 32: return *(int16_t*)(&inputData[2]);
                default: return 0; // unsupported format
            }
        case wavFormatFloat: return (int16_t)(*((float*)inputData) * INT16_MAX);
        default: return 0; // unsupported format
    }
}

void cFileManager::drawSampleWaveformFinish()
{
    if (fileSystem->isFileOpen(wavfile))
    {
        fileSystem->closeFile(wavfile, false);
    }

    status = fmDrawingSampleWaveformEnd;
    currentOperationStep = 0;
    currentOperation = fmNoOperation;
}

bool cFileManager::drawSampleWaveform(int16_t file_index,
                                      std::vector<int8_t*>& lower,
                                      std::vector<int8_t*>& upper)
{
    stopAutosave();

    if (status != fmIdle)
        return false;
    if (currentOperation != fmNoOperation)
        return false;

    importCurrentFile = file_index;
    waveformStepLower = &lower;
    waveformStepUpper = &upper;

    status = fmDrawingSampleWaveform;
    currentOperationStep = 0;
    currentOperation = fmDrawSampleWaveform;

    return true;
}
