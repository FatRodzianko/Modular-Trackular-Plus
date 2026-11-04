// Copyright 2020 Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)

#include "effect_polyverb.h"
#include "cr_mtb_buffer.h"

extern uint16_t externalRamReverbBuff[];
#ifdef PLATFORM_IMX
#if MT_HAS_MASTERFX // MT: Spring keeps floats in this buffer
static uint16_t internalRamReverbBuff[16384] __attribute__((section(".noinit"), aligned(4)));
#else
static uint16_t internalRamReverbBuff[16384] __attribute__((section(".noinit")));
#endif
#else
static __NOINIT(EXTERNAL_RAM) uint16_t internalRamReverbBuff[16384];
#endif

AudioEffectPolyverb::AudioEffectPolyverb()
  : AudioStream(2, inputQueueArray)
{
    buffer = internalRamReverbBuff;
    buffer2 = externalRamReverbBuff;
    reverb.Init(buffer, buffer2);

    reverb.SetInputGain(0.1f);
    reverb.SetDiffusion(0.625f);
    setTime(0.7f);
    setDamp(0.2f);
    setAmount(0.8f);
    blockUpdate();
}

AudioEffectPolyverb::~AudioEffectPolyverb()
{
}

AUDIO_CORE_RAM void AudioEffectPolyverb::update(void)
{
    if (isBlockedUpdate)
        return;

    audio_block_t* block_left;
    audio_block_t* block_right;

    block_left = receiveWritable(0);
    if (block_left == NULL)
        return;
    block_right = receiveWritable(1);
    if (block_right == NULL)
    {
        release(block_left);
        return;
    }

#if MT_HAS_MASTERFX // MT: the chosen reverb instead of CE's
    if (mods::masterfx::activeReverb != 0)
    {
        const mods::masterfx::ReverbKnobs knobs = reverb.knobsForMods();
        mods::masterfx::reverbProcess(knobs, buffer, buffer2, block_left->data, block_right->data);
    }
    else
#endif
    reverb.Process(block_left->data, block_right->data, AUDIO_BLOCK_SAMPLES);

    transmit(block_left, 0);
    transmit(block_right, 1);

    release(block_left);
    release(block_right);
}

AUDIO_CORE_FLASH void AudioEffectPolyverb::reset(void)
{
    isBlockedUpdate = true;
    reverb.Reset();
#if MT_HAS_MASTERFX // MT: the chosen reverb starts over in the cleared buffers
    mods::masterfx::reverbCleared();
#endif
    isBlockedUpdate = false;
}

AUDIO_CORE_FLASH bool AudioEffectPolyverb::isActive(void)
{
    return reverb.IsActive();
}

AUDIO_CORE_FLASH void AudioEffectPolyverb::setActiveThreshold(float threshold)
{
    reverb.SetActiveThreshold(threshold);
}
