/* Audio Library for Teensy 3.X
 * Copyright (c) 2014, Paul Stoffregen, paul@pjrc.com
 *
 * Development of this audio library was funded by PJRC.COM, LLC by sales of
 * Teensy and Audio Adaptor boards.  Please support PJRC's efforts to develop
 * open source software by purchasing Teensy or other PJRC products.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice, development funding notice, and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "mixer.h"
#include "dspApplyGain.h"
#if MT_HAS_MASTERFX // MT:
#include "masterfxDsp.h"
#endif

#ifdef AUDIO_NULL_IS_SILENCE
AUDIO_CORE_RAM void AudioMixerBase::mix(int size,
                                        int32_t* multiplier,
                                        int32_t* previousMultiplier,
                                        SaturationType saturationType)
{
    constexpr int kMaxMixerInputs = 21;
    audio_block_t* in[kMaxMixerInputs];
    int first = -1;
    int contributors = 0;

    for (int channel = 0; channel < size; channel++)
    {
        audio_block_t* block = receiveReadOnly(channel);
        if (block == nullptr)
        {
            previousMultiplier[channel] = multiplier[channel];
        }
        else if (multiplier[channel] == 0 && previousMultiplier[channel] == 0)
        {
            release(block);
            block = nullptr;
        }
        else
        {
            if (first < 0)
                first = channel;
            contributors++;
        }
        in[channel] = block;
    }
    if (contributors == 0)
        return;

    audio_block_t* out = in[first];
    if (contributors == 1 && saturationType == SaturationType::kHardClip)
    {
        if (multiplier[first] != MULTI_UNITYGAIN || previousMultiplier[first] != MULTI_UNITYGAIN)
        {
            if (out->ref_count > 1)
            {
                audio_block_t* copy = allocate();
                if (copy)
                    memcpy(copy->data, out->data, sizeof(copy->data));
                release(out);
                out = copy;
                if (out == nullptr)
                    return;
            }
            applyGain(out->data, multiplier[first], previousMultiplier[first]);
        }
        transmit(out);
        release(out);
        return;
    }

    int32_t mixedBuffer[AUDIO_BLOCK_SAMPLES];
    if (out->ref_count > 1)
    {
        out = allocate();
        if (out == nullptr)
        {
            for (int channel = first; channel < size; channel++)
            {
                if (in[channel])
                    release(in[channel]);
            }
            return;
        }
        applyGainAndCopy(mixedBuffer, in[first]->data, multiplier[first], previousMultiplier[first]);
        release(in[first]);
    }
    else
    {
        applyGainAndCopy(mixedBuffer, out->data, multiplier[first], previousMultiplier[first]);
    }
    for (int channel = first + 1; channel < size; channel++)
    {
        if (in[channel] != nullptr)
        {
            applyGainThenAdd(mixedBuffer, in[channel]->data, multiplier[channel], previousMultiplier[channel]);
            release(in[channel]);
        }
    }
    saturate(out->data, mixedBuffer, saturationType);
    transmit(out);
    release(out);
}

template<int mixerSize>
AUDIO_CORE_RAM void AudioMixer<mixerSize>::update(void)
{
#if MT_HAS_MASTERFX // MT: the tracks mixer with a bus plugin on chosen tracks
    if constexpr (mixerSize == mods::masterfx::kBusInputs)
    {
        if (mods::masterfx::busLive != 0 && this == mods::masterfx::busMixers[0])
        {
            modsBusUpdate();
            return; // the right object's blocks are taken and sent here; it finds none
        }
    }
#endif
    static_assert(mixerSize <= 21, "AudioMixerBase::mix has room for 21 inputs");
    this->mix(mixerSize, multiplier, previousMultiplier, saturationType);
}
#else
template<int mixerSize>
AUDIO_CORE_RAM void AudioMixer<mixerSize>::update(void)
{
#if MT_HAS_MASTERFX // MT: the tracks mixer with a bus plugin on chosen tracks
    if constexpr (mixerSize == mods::masterfx::kBusInputs)
    {
        if (mods::masterfx::busLive != 0 && this == mods::masterfx::busMixers[0])
        {
            modsBusUpdate();
            return; // the right object's blocks are taken and sent here; it finds none
        }
    }
#endif

    audio_block_t *in = nullptr, *out = nullptr;

    int32_t mixedBuffer[AUDIO_BLOCK_SAMPLES];

    for (int channel = 0; channel < mixerSize; channel++)
    {
        if (nullptr == out)
        {
            out = receiveWritable(channel);
            if (out)
            {
                applyGainAndCopy(
                  mixedBuffer, out->data, multiplier[channel], previousMultiplier[channel]);
            }
        }
        else
        {
            in = receiveReadOnly(channel);
            if (nullptr != in)
            {
                applyGainThenAdd(
                  mixedBuffer, in->data, multiplier[channel], previousMultiplier[channel]);
                release(in);
            }
        }
    }
    if (nullptr != out)
    {
        saturate(out->data, mixedBuffer, saturationType);

        transmit(out);
        release(out);
    }
}
#endif

#if MT_HAS_MASTERFX // MT:
// As update(), for both sides from the left object (CE builds it first, after the tracks): the
// chosen inputs are summed into the bus (floats, in the plugins' scratch) and the rest into the
// mix, the plugin works on the bus, and the mix + the bus goes through CE's saturation. The
// mixer's gains apply as in update(), through the same functions. The blocks are held until the
// mix is made; the mix is on the stack, a side at a time, as update()'s is. Since CE's 7482cc8 a
// silent input sends no block (none is summed), and with no block on a side nothing is sent, as
// CE's mix() does; the plugin still runs on the silent bus.
template<int mixerSize>
template<int N>
AUDIO_CORE_RAM void AudioMixer<mixerSize>::modsBusUpdate()
{
    AudioMixer* const pair[2] = { this, static_cast<AudioMixer*>(mods::masterfx::busMixers[1]) };
    const uint32_t mask = mods::masterfx::busMask;
    constexpr float kScale = 1.0f / 32768.0f; // a power of two: the sums stay exact
    audio_block_t* blocks[2][mixerSize];
    audio_block_t* out[2] = { nullptr, nullptr };
    int32_t mix[AUDIO_BLOCK_SAMPLES];
    for (uint32_t side = 0; side < 2; side++)
    {
        AudioMixer* const m = pair[side];
        float* const bus = mods::masterfx::busFloats + side * AUDIO_BLOCK_SAMPLES;
        for (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
        {
            bus[i] = 0.0f;
        }
        for (int channel = 0; channel < mixerSize; channel++)
        {
            audio_block_t* in;
            if (out[side] == nullptr)
            {
                in = m->receiveWritable(channel);
                out[side] = in; // the first block carries the result, as in update()
            }
            else
            {
                in = m->receiveReadOnly(channel);
            }
            blocks[side][channel] = in;
#ifdef AUDIO_NULL_IS_SILENCE
            if (in == nullptr)
            {
                m->previousMultiplier[channel] = m->multiplier[channel]; // as CE's mix()
            }
#endif
            if (in != nullptr && ((mask >> channel) & 1u) != 0)
            {
                applyGainAndCopy(mix, in->data, m->multiplier[channel], m->previousMultiplier[channel]);
                for (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
                {
                    bus[i] += static_cast<float>(mix[i]) * kScale;
                }
            }
        }
    }
    mods::masterfx::busRun(); // a silent bus too, so the plugin's envelope moves on
    for (uint32_t side = 0; side < 2; side++)
    {
        AudioMixer* const m = pair[side];
        const float* const bus = mods::masterfx::busFloats + side * AUDIO_BLOCK_SAMPLES;
        for (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
        {
            mix[i] = 0;
        }
        for (int channel = 0; channel < mixerSize; channel++)
        {
            audio_block_t* const in = blocks[side][channel];
            if (in == nullptr)
            {
                continue;
            }
            if (((mask >> channel) & 1u) == 0)
            {
                applyGainThenAdd(mix, in->data, m->multiplier[channel], m->previousMultiplier[channel]);
            }
            if (in != out[side])
            {
                release(in);
            }
        }
        if (out[side] == nullptr)
        {
            continue; // nothing came on this side
        }
        for (uint32_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
        {
            const float v = bus[i] * 32768.0f; // held within +-11.0 by the runtime
            mix[i] += static_cast<int32_t>(v >= 0.0f ? v + 0.5f : v - 0.5f);
        }
        saturate(out[side]->data, mix, m->saturationType);
        m->transmit(out[side]);
        release(out[side]);
    }
}
#endif

// if you use this template with other value you should add it in this place to force compilation
// for every class in one .cpp file (to avoid symbol visibility issues)
template class AudioMixer<2>;
template class AudioMixer<4>;
template class AudioMixer<7>;
template class AudioMixer<8>;
template class AudioMixer<10>;
#ifdef PLATFORM_IMX
template class AudioMixer<18>;
template class AudioMixer<11>;
template class AudioMixer<21>;
#endif
