#ifdef FEATURE_SYNTH

#include "synth_PolySynth.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include "PolySynth/Instrument.h"
#include "PolySynth/Synth.h"
#include "Synth.h"
#include "config.h"
#include <span>
#include "modsBuild.h" // MT:
#if MT_HAS_SYNTHFX
#include "synthfx/synthfxMod.h" // MT: Synth FX
#endif
//#include "Audio.h"
//#include "dspinst.h'
//#include "util_math.h"
//#include <cr_section_macros.h>

AUDIO_CORE_RAM void PolySynth::update(void)
{
    if (isBlocked)
    {
        return;
    }

    static_assert(std::is_same<polysynth::sample_t, float>::value);

    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufSendsCommonLeft;
    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufSendsCommonRight;
    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufPostfaderLeft;
    static std::array<polysynth::sample_t, polysynth::kAudioBufferSize> tempBufPostfaderRight;

    static std::array<std::array<polysynth::sample_t, polysynth::kAudioBufferSize>, 2> tempBufSend;

    for (int partIdx = 0; partIdx < kSynthParts; partIdx++)
    {
        auto& part = trackerSynth->parts[partIdx];

        if (part.instrument->getVoiceCount() == 0)
        {
            if (!AudioStream::renderMode)
            {
                continue;
            }

            audio_block_t* blockZero = allocate();
            if (blockZero == NULL)
            {
                return;
            }
            std::fill(std::begin(blockZero->data), std::end(blockZero->data), 0);
            transmit(blockZero, partIdx);
            transmit(blockZero, kRightChannelOffset + partIdx);
            transmit(blockZero, kSend1Offset + partIdx);
            transmit(blockZero, kSend2Offset + partIdx);
            release(blockZero);
            continue;
        }

        bool isStereo =
          part.patch != nullptr && part.patch->getMetadata().outputChannels ==
                                     polysynth::Patch::Metadata::OutputChannels::kStereo;

        bool hasSends = part.patch != nullptr && part.patch->getMetadata().rendersAuxSends;

#if MT_HAS_SYNTHFX // MT: Synth FX: a mono part goes out as stereo while a modulation effect plays
        static_assert(mods::synthfx::kParts == kSynthParts);
        const bool mtEngineStereo = isStereo;
        const mods::synthfx::Insert mtFx = mods::synthfx::begin(partIdx, part.patch.get(), isStereo);
        isStereo = mtFx.stereo;
#endif

        audio_block_t* blockPostFaderLeft = allocate();
        audio_block_t* blockPostFaderRight = isStereo ? allocate() : NULL;
        audio_block_t* blockSendsCommonOrSend1 = allocate();
        audio_block_t* blockSend2 = hasSends ? allocate() : NULL;

        if (blockPostFaderLeft == NULL || (isStereo && blockPostFaderRight == NULL) ||
            blockSendsCommonOrSend1 == NULL || (hasSends && blockSend2 == NULL))
        {
            if (blockPostFaderLeft != NULL)
            {
                release(blockPostFaderLeft);
            }

            if (blockPostFaderRight != NULL)
            {
                release(blockPostFaderRight);
            }

            if (blockSendsCommonOrSend1 != NULL)
            {
                release(blockSendsCommonOrSend1);
            }

            if (blockSend2 != NULL)
            {
                release(blockSend2);
            }

            return;
        }

        std::array<polysynth::AudioBufferRef, 2> tempSendsBufArray = { tempBufSend[0],
                                                                       tempBufSend[1] };

        trackerSynth->synth->renderAudioBuffer(
          { tempBufSendsCommonLeft, tempBufSendsCommonRight },
          std::span<polysynth::AudioBufferRef>(tempSendsBufArray),
          partIdx);

#if MT_HAS_SYNTHFX // MT: Synth FX, before the sends and the fader, so both get it
        mods::synthfx::run(
          mtFx, partIdx, tempBufSendsCommonLeft.data(), tempBufSendsCommonRight.data(), mtEngineStereo);
#endif

        const bool reuseUnmodifiedMono = (!hasSends && !isStereo && gainDB[partIdx] == 0.0f);
        if (!reuseUnmodifiedMono)
        {
            tempBufPostfaderLeft = tempBufSendsCommonLeft;
            if (isStereo)
            {
                tempBufPostfaderRight = tempBufSendsCommonRight;
            }
        }

        polysynth::AudioBufferRef tempBufRef = tempBufSendsCommonLeft;

#if MT_HAS_SYNTHFX // MT: Synth FX may send a mono part as stereo; this checks the engine's own
        assert(hasSends == mtEngineStereo);
#else
        assert(hasSends ==
               isStereo); // Assumption to simplify handling. The only one stereo and with internal
                          // synths is PERC, the other are mono and without sends. Refactor the
                          // handling if needed for new synth.
#endif

        if (hasSends)
        {
            auto* srcSend1 = tempBufSend[0].begin();
            auto* srcSend2 = tempBufSend[1].begin();

            auto* destSend1 = blockSendsCommonOrSend1->data;
            auto* destSend2 = blockSend2->data;

            for (int i = 0; i < polysynth::kAudioBufferSize; i++)
            {
                *(destSend1++) = static_cast<int16_t>(*(srcSend1++) * INT16_MAX);
                *(destSend2++) = static_cast<int16_t>(*(srcSend2++) * INT16_MAX);
            }
        }
        else
        {

            if (isStereo)
            {
                for (int idx = 0; idx < polysynth::kAudioBufferSize; idx++)
                {
                    tempBufSendsCommonLeft[idx] =
                      (tempBufSendsCommonLeft[idx] + tempBufSendsCommonRight[idx]) / 2.0f;
                }
            }

            polysynth::sdk::saturateSoftClip(tempBufRef, 0.0f);

            auto* src = tempBufSendsCommonLeft.begin();
            auto* dest = blockSendsCommonOrSend1->data;

            for (int i = 0; i < polysynth::kAudioBufferSize; i++)
            {
                *(dest++) = static_cast<int16_t>(*(src++) * INT16_MAX);
            }
        }

        if (reuseUnmodifiedMono)
        {
            memcpy(blockPostFaderLeft->data,
                   blockSendsCommonOrSend1->data,
                   sizeof(blockPostFaderLeft->data));
        }

        transmit(blockSendsCommonOrSend1, kSend1Offset + partIdx);
        transmit(hasSends ? blockSend2 : blockSendsCommonOrSend1, kSend2Offset + partIdx);
        release(blockSendsCommonOrSend1);
        if (blockSend2 != NULL)
        {
            release(blockSend2);
        }

        if (!reuseUnmodifiedMono)
        {
            tempBufRef = tempBufPostfaderLeft;
            polysynth::sdk::saturateSoftClip(tempBufRef, gainDB[partIdx]);

            if (isStereo)
            {
                tempBufRef = tempBufPostfaderRight;
                polysynth::sdk::saturateSoftClip(tempBufRef, gainDB[partIdx]);
            }

            if (isStereo)
            {
                auto* srcL = tempBufPostfaderLeft.begin();
                auto* srcR = tempBufPostfaderRight.begin();

                auto* destL = blockPostFaderLeft->data;
                auto* destR = blockPostFaderRight->data;

                for (int i = 0; i < polysynth::kAudioBufferSize; i++)
                {
                    *(destL++) = static_cast<int16_t>(*(srcL++) * INT16_MAX);
                    *(destR++) = static_cast<int16_t>(*(srcR++) * INT16_MAX);
                }
            }
            else
            {
                auto* src = tempBufPostfaderLeft.begin();
                auto* dest = blockPostFaderLeft->data;

                for (int i = 0; i < polysynth::kAudioBufferSize; i++)
                {
                    *(dest++) = static_cast<int16_t>(*(src++) * INT16_MAX);
                }
            }
        }

        transmit(blockPostFaderLeft, partIdx);
        transmit(isStereo ? blockPostFaderRight : blockPostFaderLeft,
                 kRightChannelOffset + partIdx);
        release(blockPostFaderLeft);
        if (blockPostFaderRight != NULL)
        {
            release(blockPostFaderRight);
        }
    }
}

void PolySynth::init(Synth* trackerSynth)
{
    PolySynth::trackerSynth = trackerSynth;
    isBlocked = false;
}

AUDIO_CORE_FLASH void PolySynth::setGain(int idx, float gainDB)
{
    assert(idx < kSynthParts);
    PolySynth::gainDB[idx] = gainDB;
}

#endif
