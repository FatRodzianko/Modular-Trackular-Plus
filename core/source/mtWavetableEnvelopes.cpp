#include "mtWavetableEnvelopes.h"
#include "TrackerImpl.h"
#include "wavHeaderReader.h"
#include <cr_section_macros.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

__NOINIT(EXTERNAL_RAM) int16_t g_wtEnvelopeBank[kWtEnvCount][kWtEnvLength];

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kTwoPi = 6.28318530717958647692f;

static inline float clampF(float val, float minV, float maxV)
{
    if (val < minV) return minV;
    if (val > maxV) return maxV;
    return val;
}

AUDIO_CORE_FLASH int8_t unpackWarpX(uint8_t wtBend)
{
    uint8_t nx = (wtBend >> 4) & 0x0F;
    if (nx <= 7) return (int8_t)nx;
    if (nx >= 9) return (int8_t)(nx - 16);
    return 0;
}

AUDIO_CORE_FLASH int8_t unpackWarpY(uint8_t wtBend)
{
    uint8_t ny = wtBend & 0x0F;
    if (ny <= 7) return (int8_t)ny;
    if (ny >= 9) return (int8_t)(ny - 16);
    return 0;
}

AUDIO_CORE_FLASH uint8_t packWarpXY(int8_t warpX, int8_t warpY)
{
    if (warpX < -7) warpX = -7; else if (warpX > 7) warpX = 7;
    if (warpY < -7) warpY = -7; else if (warpY > 7) warpY = 7;
    uint8_t nx = (warpX >= 0) ? (uint8_t)warpX : (uint8_t)(16 + warpX);
    uint8_t ny = (warpY >= 0) ? (uint8_t)warpY : (uint8_t)(16 + warpY);
    return ((nx & 0x0F) << 4) | (ny & 0x0F);
}

void initWavetableEnvelopes()
{
    for (int shape = 0; shape < kWtEnvCount; ++shape)
    {
        int bank = shape / 16;
        int sub = shape % 16;

        for (int i = 0; i < kWtEnvLength; ++i)
        {
            float t = (float)i / (float)(kWtEnvLength - 1); // 0.0 to 1.0
            float y = 0.0f;

            switch (bank)
            {
                case 0: // Exponential & Logarithmic Decays / Percussive Plucks
                {
                    float p = 0.5f + sub * 0.45f;
                    y = powf(1.0f - t, p);
                    break;
                }
                case 1: // Swells, S-Curves & Smooth Bells
                {
                    if (sub < 8)
                    {
                        float p = 0.5f + sub * 0.5f;
                        y = powf(sinf(t * kPi), p);
                    }
                    else
                    {
                        float center = 0.1f + (sub - 8) * 0.1f;
                        float sigma = 0.22f;
                        float diff = (t - center) / sigma;
                        y = expf(-0.5f * diff * diff);
                    }
                    break;
                }
                case 2: // Double & Multi-Bounces
                {
                    int bounces = 2 + (sub % 5);
                    float scaledT = t * bounces;
                    int bIdx = (int)scaledT;
                    if (bIdx >= bounces) bIdx = bounces - 1;
                    float localT = scaledT - bIdx;
                    float arc = 4.0f * localT * (1.0f - localT);
                    float damp = powf(0.55f, (float)bIdx);
                    y = arc * damp;
                    break;
                }
                case 3: // Rhythmic Gates & Trance Steppers
                {
                    static const uint8_t pulseCounts[16] = { 2, 2, 3, 3, 4, 4, 6, 6, 8, 8, 12, 12, 16, 16, 8, 16 };
                    int pulses = pulseCounts[sub];
                    float duty = (sub % 2 == 0) ? 0.5f : 0.25f;
                    float cycle = t * pulses;
                    float frac = cycle - floorf(cycle);
                    y = (frac < duty) ? 1.0f : 0.0f;
                    break;
                }
                case 4: // Sharks, Fins & Asymmetric Triangles
                {
                    float peak = 0.05f + sub * 0.06f;
                    if (t < peak)
                        y = t / peak;
                    else
                        y = 1.0f - (t - peak) / (1.0f - peak);
                    break;
                }
                case 5: // Ripples & Damped Oscillations
                {
                    float freq = 2.0f + sub * 1.0f;
                    float decay = expf(-3.5f * t);
                    y = decay * (0.5f + 0.5f * cosf(kTwoPi * freq * t));
                    break;
                }
                case 6: // Accelerating & Decelerating Trills
                {
                    float chirp = (sub < 8) ? (t * t) : sqrtf(t);
                    int n = 3 + (sub % 8);
                    float modT = chirp * n;
                    float frac = modT - floorf(modT);
                    y = (1.0f - t * 0.6f) * ((frac < 0.4f) ? 1.0f : 0.0f);
                    break;
                }
                case 7: // Staircases Up, Down & Pyramids
                {
                    int steps = 2 + sub;
                    int curStep = (int)(t * steps);
                    if (curStep >= steps) curStep = steps - 1;
                    if (sub < 8)
                        y = (float)curStep / (float)(steps - 1);
                    else
                        y = 1.0f - ((float)curStep / (float)(steps - 1));
                    break;
                }
                // --- Banks 8-15: Default Factory Fallbacks for User Slots ---
                case 8: // Wavefolded West-Coast
                {
                    float w = sinf(t * kTwoPi * (1.0f + sub * 0.3f));
                    y = fabsf(w);
                    if (y > 0.6f) y = 1.2f - y;
                    y *= 1.4f;
                    break;
                }
                case 9: // Syncopated Pulses
                {
                    uint16_t pat = 0xAA55 ^ (sub * 0x1111);
                    int bit = (int)(t * 16.0f);
                    if (bit > 15) bit = 15;
                    y = ((pat >> bit) & 1) ? 1.0f : 0.0f;
                    break;
                }
                case 10: // Reverse Swells with Snappy Cutoffs
                {
                    float p = 1.2f + sub * 0.3f;
                    if (t < 0.92f)
                        y = powf(t / 0.92f, p);
                    else
                        y = (1.0f - t) / 0.08f;
                    break;
                }
                case 11: // Dual-Peak Camel / Formant Contours
                {
                    float p1 = 0.25f;
                    float p2 = 0.75f;
                    float d1 = (t - p1) / 0.15f;
                    float d2 = (t - p2) / (0.15f + sub * 0.01f);
                    y = 0.7f * expf(-d1 * d1) + (0.5f + sub * 0.03f) * expf(-d2 * d2);
                    break;
                }
                case 12: // Growing Chaos / Rising Wobble
                {
                    float wobble = 0.5f + 0.5f * sinf(kTwoPi * (1.0f + sub * 1.5f) * t * t);
                    y = t * wobble;
                    break;
                }
                case 13: // Karplus-Style Plucks
                {
                    float decay = expf(-5.0f * t);
                    float ripple = 0.8f + 0.2f * sinf(kTwoPi * (10.0f + sub * 2.0f) * t);
                    y = decay * ripple;
                    break;
                }
                case 14: // Deterministic Stepped S&H
                {
                    uint32_t seed = (uint32_t)(shape * 12345 + (int)(t * 16.0f) * 6789);
                    seed = (seed * 1103515245U + 12345U) & 0x7FFFFFFFU;
                    y = (float)(seed % 1000) / 1000.0f;
                    break;
                }
                case 15: // Cinematic Morphing Risers
                default:
                {
                    float s = sinf(t * kPi * 0.5f);
                    float ramp = powf(t, 2.0f + sub * 0.2f);
                    y = 0.5f * s + 0.5f * ramp;
                    break;
                }
            }

            y = clampF(y, 0.0f, 1.0f);
            g_wtEnvelopeBank[shape][i] = (int16_t)(y * 32767.0f);
        }
    }
}

void loadUserWavetablesFromSD(FileSystem* fs)
{
    if (!fs || !fs->isSdCardInserted()) return;
    if (!fs->exists("Envelopes")) return;

    bool zeroBased = fs->exists("Envelopes/000.wav") || fs->exists("Envelopes/0.wav");
    char pathBuf[48];
    for (int i = 0; i < 128; ++i)
    {
        int fileNum = zeroBased ? i : (i + 1);
        snprintf(pathBuf, sizeof(pathBuf), "Envelopes/%03d.wav", fileNum);
        if (!fs->exists(pathBuf))
        {
            snprintf(pathBuf, sizeof(pathBuf), "Envelopes/%d.wav", fileNum);
            if (!fs->exists(pathBuf)) continue;
        }

        auto [opened, fileHandle] = fs->openFile(pathBuf, FileSystem::kRead);
        if (!opened || !fileHandle) continue;

        strWavFileHeader header;
        bool headerRead = readHeader(&header, fileHandle);

        if (headerRead && header.chunkId == 0x46464952 && header.AudioFormat == 1 &&
            header.numChannels > 0 &&
            (header.bitsPerSample == 8 || header.bitsPerSample == 16 || header.bitsPerSample == 24))
        {
            size_t fileSize = fs->getFileSize(fileHandle);
            size_t availableBytes = fs->getFileAvailable(fileHandle);
            if (availableBytes > fileSize)
            {
                fs->closeFile(fileHandle, true);
                continue;
            }

            size_t dataOffset = fileSize - availableBytes;
            size_t dataBytes =
              availableBytes < header.subchunk2Size ? availableBytes : header.subchunk2Size;
            int bytesPerSample = header.bitsPerSample / 8;
            int channels = header.numChannels;
            int frameSize = bytesPerSample * channels;

            if (frameSize > 0 && dataBytes >= (size_t)frameSize)
            {
                size_t numFrames = dataBytes / frameSize;
                int16_t rawPts[kWtEnvLength];
                int16_t minSample = 32767;
                bool samplesRead = true;
                for (int pt = 0; pt < kWtEnvLength; ++pt)
                {
                    size_t frameIdx = (pt * numFrames) / kWtEnvLength;
                    uint64_t fileOffset = (uint64_t)dataOffset + frameIdx * frameSize;
                    if (fileOffset > UINT32_MAX || !fs->seekFile((uint32_t)fileOffset, fileHandle))
                    {
                        samplesRead = false;
                        break;
                    }

                    int16_t sample16 = 0;
                    if (header.bitsPerSample == 16)
                    {
                        samplesRead = fs->readFile(&sample16, 2, fileHandle) == 2;
                    }
                    else if (header.bitsPerSample == 8)
                    {
                        uint8_t u8 = 0;
                        samplesRead = fs->readFile(&u8, 1, fileHandle) == 1;
                        sample16 = ((int16_t)u8 - 128) << 8;
                    }
                    else if (header.bitsPerSample == 24)
                    {
                        uint8_t b[3];
                        samplesRead = fs->readFile(b, 3, fileHandle) == 3;
                        sample16 = (int16_t)(((int32_t)b[1] << 8) | ((int32_t)b[2] << 16));
                    }

                    if (!samplesRead) break;

                    rawPts[pt] = sample16;
                    if (sample16 < minSample) minSample = sample16;
                }

                if (samplesRead)
                {
                    bool isBipolar = (minSample < -256);
                    for (int pt = 0; pt < kWtEnvLength; ++pt)
                    {
                        int32_t unipolar =
                          isBipolar ? (((int32_t)rawPts[pt] + 32768) >> 1) : (int32_t)rawPts[pt];
                        if (unipolar > 32767) unipolar = 32767;
                        else if (unipolar < 0) unipolar = 0;
                        g_wtEnvelopeBank[kWtEnvFactoryCount + i][pt] = (int16_t)unipolar;
                    }
                }
            }
        }
        fs->closeFile(fileHandle, true);
    }
}

AUDIO_CORE_FLASH float evalWavetableEnvelopeFloat(float phase01, uint8_t shapeIdx, uint8_t wtBend)
{
    float p = clampF(phase01, 0.0f, 1.0f);

    if (wtBend != 0)
    {
        int8_t wx = unpackWarpX(wtBend);
        if (wx != 0)
        {
            float k = (float)wx * (1.0f / 9.0f);
            float denom = p * 2.0f * k + 1.0f - k;
            if (denom != 0.0f)
                p = (p * (1.0f + k)) / denom;
            p = clampF(p, 0.0f, 1.0f);
        }
    }

    float idxF = p * (float)(kWtEnvLength - 1);
    int idx = (int)idxF;
    if (idx < 0) idx = 0;
    if (idx >= kWtEnvLength - 1)
    {
        idx = kWtEnvLength - 2;
    }
    float frac = idxF - (float)idx;

    const int16_t* tbl = g_wtEnvelopeBank[shapeIdx];
    float y0 = (float)tbl[idx];
    float y1 = (float)tbl[idx + 1];
    float y = (y0 + frac * (y1 - y0)) * (1.0f / 32767.0f);

    if (wtBend != 0)
    {
        int8_t wy = unpackWarpY(wtBend);
        if (wy == -7)
        {
            y = 1.0f - y;
        }
        else if (wy != 0)
        {
            float ky = (float)wy * (1.0f / 9.0f);
            float denom = y * 2.0f * ky + 1.0f - ky;
            if (denom != 0.0f)
                y = (y * (1.0f + ky)) / denom;
        }
        return clampF(y, 0.0f, 1.0f);
    }

    return y;
}

AUDIO_CORE_FLASH int32_t evalWavetableEnvelopeFixed30(uint32_t phaseQ16, uint8_t shapeIdx, uint8_t wtBend)
{
    if (wtBend == 0)
    {
        const int16_t* tbl = g_wtEnvelopeBank[shapeIdx];
        if (phaseQ16 >= 65536u)
        {
            return ((int32_t)tbl[kWtEnvLength - 1]) << 15;
        }
        uint32_t scaled = phaseQ16 * (uint32_t)(kWtEnvLength - 1);
        uint32_t idx = scaled >> 16;
        uint32_t frac = scaled & 0xFFFFu;
        int32_t y0 = tbl[idx];
        int32_t y1 = tbl[idx + 1];
        return ((y0 << 16) + (int32_t)frac * (y1 - y0)) >> 1;
    }

    float phase01 = (float)phaseQ16 * (1.0f / 65536.0f);
    float y = evalWavetableEnvelopeFloat(phase01, shapeIdx, wtBend);
    return (int32_t)(y * 1073741823.0f); // 0 .. 0x3FFFFFFF (Q30)
}
