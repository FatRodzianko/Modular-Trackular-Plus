#include "AudioDataUtilities.h"

#include "mtAudioUtil.h"

#include <cmath>

static inline int16_t getInputSample(uint8_t* inputData,
                                     int sampleIdx,
                                     uint16_t sourceFormat,
                                     uint16_t sourceBitsPerSample,
                                     uint32_t bytesPerSample)
{
    switch (sourceFormat)
    {
        case wavFormatPCM:
            switch (sourceBitsPerSample)
            {
                case 8: return (((int16_t)inputData[sampleIdx * bytesPerSample]) - 128) << 8;
                case 16: return *(int16_t*)(&inputData[sampleIdx * bytesPerSample]);
                case 24:
                {
                    // to avoid unaligned access
                    uint8_t lowerByte = inputData[sampleIdx * bytesPerSample + 1];
                    uint8_t upperByte = inputData[sampleIdx * bytesPerSample + 2];

                    return (int16_t)((uint16_t)lowerByte | (uint16_t)upperByte << 8);
                }
                case 32: return *(int16_t*)(&inputData[sampleIdx * bytesPerSample] + 2);
                default: return 0; // unsupported format
            }
        case wavFormatFloat:
            return (int16_t)(*(float*)(&inputData[sampleIdx * bytesPerSample]) * INT16_MAX);
        default: return 0; // unsupported format
    }
}

static inline void convertChannel(uint8_t* inputData,
                                  uint32_t inputSamples,
                                  uint32_t bytesPerSample,
                                  uint16_t sourceFormat,
                                  uint16_t sourceBitsPerSample,
                                  float sampleRateRatio,
                                  uint32_t outputSamples,
                                  int16_t* output)
{

    for (uint32_t outIdx = 0; outIdx < outputSamples; outIdx++)
    {
        float inPos = (float)outIdx * sampleRateRatio;
        uint32_t inIdx = floor(inPos);
        int16_t sample1 =
          getInputSample(inputData, inIdx, sourceFormat, sourceBitsPerSample, bytesPerSample);
        int16_t sample0 =
          inIdx > 0 ? getInputSample(
                        inputData, inIdx - 1, sourceFormat, sourceBitsPerSample, bytesPerSample)
                    : sample1;
        int16_t sample2 =
          inIdx + 1 < inputSamples
            ? getInputSample(
                inputData, inIdx + 1, sourceFormat, sourceBitsPerSample, bytesPerSample)
            : sample1;
        int16_t sample3 =
          inIdx + 2 < inputSamples
            ? getInputSample(
                inputData, inIdx + 2, sourceFormat, sourceBitsPerSample, bytesPerSample)
            : sample1;

        output[outIdx] = interpolateCubic(sample0, sample1, sample2, sample3, inPos - inIdx);
    }
}

// zwraca wielkosc w probkach
uint32_t convertAudioData(ConversionConfig& conversionConfig)
{
    uint32_t bytesPerSample =
      (conversionConfig.sourceBitsPerSample / 8) * conversionConfig.sourceChannelCount;

    if (conversionConfig.inputSize < bytesPerSample)
    {
        return 0;
    }

    if (conversionConfig.outPtrL == nullptr)
    {
        return 0;
    }

    if (conversionConfig.sourceBitsPerSample / conversionConfig.sourceChannelCount)
    {
    }

    float sampleRateRatio =
      (float)conversionConfig.sourceSampleRate / (float)conversionConfig.destSampleRate;

    uint8_t* inputData = conversionConfig.inputData;
    uint32_t inputSamples = conversionConfig.inputSize / bytesPerSample;
    uint32_t outputSamples =
      inputSamples * conversionConfig.destSampleRate / conversionConfig.sourceSampleRate;

    convertChannel(inputData,
                   inputSamples,
                   bytesPerSample,
                   conversionConfig.sourceFormat,
                   conversionConfig.sourceBitsPerSample,
                   sampleRateRatio,
                   outputSamples,
                   conversionConfig.outPtrL);

    if (conversionConfig.outPtrR != nullptr)
    {
        inputData = conversionConfig.inputData + conversionConfig.sourceBitsPerSample / 8;
        convertChannel(inputData,
                       inputSamples,
                       bytesPerSample,
                       conversionConfig.sourceFormat,
                       conversionConfig.sourceBitsPerSample,
                       sampleRateRatio,
                       outputSamples,
                       conversionConfig.outPtrR);
    }

    return outputSamples;
}

bool checkSampleValid(const strWavFileHeader& sampleHead)
{
    if ((sampleHead.waveId != WAVEID) ||
        ((sampleHead.numChannels != 1) && (sampleHead.numChannels != 2)) ||
        sampleHead.sampleRate == 0)
    {
        return false;
    }

    if (sampleHead.AudioFormat == wavFormatPCM)
    {
        switch (sampleHead.bitsPerSample)
        {
            case 8:
            case 16:
            case 24:
            case 32: return true;
            default: return false; // unsupported format
        }
    }
    else if (sampleHead.AudioFormat == wavFormatFloat)
    {
        switch (sampleHead.bitsPerSample)
        {
            case 32: return true;
            default: return false;
        }
    }

    return false;
}
