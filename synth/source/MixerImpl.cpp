#include "MixerImpl.h"
#include "instrument/InstrumentImpl.h"

#include "PolyDSP/misc/Amplifier.h"

#include "config.h"

#include <algorithm>

namespace polysynth
{
MixerImpl::MixerImpl(int channelsCount, int sendsCount)
  : mixer(channelsCount, sendsCount)
{
    this->impl = this;
    channels.resize(channelsCount);
    sends.resize(sendsCount);
    inputBuffers.resize(channelsCount);
    sendBuffers.resize(sendsCount);
    sendReturnBuffers.resize(sendsCount);

    for (int send = 0; send < sendsCount; send++)
    {
        sendBuffers[send] = &sends[send].bufferRef;
        sendReturnBuffers[send] = {
            &sends[send].returnBufferRef[0], // NOLINT(readability-container-data-pointer)
            &sends[send].returnBufferRef[1]
        };
        sends[send].returnSendGains.resize(sendsCount, polydsp::kSilenceGain);
    }
};

float MixerImpl::getChannelGain(int channel)
{
    return mixer.getChannelGain(channel);
};

void MixerImpl::setChannelGain(int channel, float gainDb)
{
    return mixer.setChannelGain(channel, gainDb);
}

float MixerImpl::getChannelPanning(int channel)
{
    return mixer.getChannelPanning(channel);
};

void MixerImpl::setChannelPanning(int channel, float panning)
{
    return mixer.setChannelPanning(channel, panning);
}

float MixerImpl::getChannelSend(int channel, int send)
{
    return mixer.getChannelSend(channel, send);
}

void MixerImpl::setChannelSend(int channel, int send, float gainDb)
{
    mixer.setChannelSend(channel, send, gainDb);
}

float MixerImpl::getReturnChannelSend(int returnChannel, int send)
{
    return sends[returnChannel].returnSendGains[send];
}

void MixerImpl::setReturnChannelSend(int returnChannel, int send, float gainDb)
{
    sends[returnChannel].returnSendGains[send] = gainDb;
}

void MixerImpl::connectInput(int channel, std::shared_ptr<Instrument> instrument)
{
    channels[channel].instrument = std::move(instrument);
}

void MixerImpl::disconnectInput(int channel)
{
    channels[channel].instrument = nullptr;
}

void MixerImpl::connectSend(int send, std::shared_ptr<Instrument> instrument)
{
    sends[send].instrument = std::move(instrument);
}

void MixerImpl::renderAudioBuffer([[maybe_unused]] AudioBufferStereoRef buffer)
{
    for (auto [channel, inputBufferIt] =
           std::tuple{ std::begin(channels), std::begin(inputBuffers) };
         channel != std::end(channels);
         channel++, inputBufferIt++)
    {
        inputBufferIt->at(0) = nullptr;
        inputBufferIt->at(1) = nullptr;

        if (channel->instrument == nullptr)
        {
            continue;
        }

        const auto& metadata = channel->instrument->impl->getPatchAssigned()->getMetadata();

        if (metadata.outputChannels == Patch::Metadata::OutputChannels::kNone)
        {
            continue;
        }

        channel->instrument->impl->renderAudioBuffer(
          std::nullopt,
          AudioBufferStereoRef{
            AudioBufferRef{ channel->buffers[0] }, // NOLINT(readability-container-data-pointer)
            AudioBufferRef{ channel->buffers[1] } },
          std::span<AudioBufferRef>());

        inputBufferIt->at(0) =
          &channel->bufferRefs[0]; // NOLINT(readability-container-data-pointer)
        if (metadata.outputChannels == Patch::Metadata::OutputChannels::kStereo)
        {
            inputBufferIt->at(1) = &channel->bufferRefs[1];
        }
    };

    std::array outputBuffers = { &buffer.at(0), &buffer.at(1) };
    mixer.render(inputBuffers, outputBuffers, sendBuffers);

    for (int sendIdx = 0; sendIdx < std::ssize(sends); sendIdx++)
    {
        auto& send = sends[sendIdx];
        send.instrument->impl->renderAudioBuffer(
          AudioBufferStereoRef{ send.buffer, send.buffer },
          AudioBufferStereoRef{ send.returnBuffer[0], send.returnBuffer[1] },
          std::span<AudioBufferRef>());

        for (int targetSendIdx = sendIdx + 1; targetSendIdx < std::ssize(sends); targetSendIdx++)
        {
            auto& targetSend = sends[targetSendIdx];
            auto gain = send.returnSendGains[targetSendIdx];
            if (gain != polydsp::kSilenceGain)
            {
                polydsp::Mixer::renderMixdownAdd(send.returnBufferRef, targetSend.buffer, gain);
            }
        }
    }

    polydsp::Mixer::renderAdd(sendReturnBuffers, outputBuffers);

#ifndef UNIT_TESTS
    polydsp::Amplifier::saturateHardClip(buffer.at(0), -polydsp::kDoubleLoundnessDb);
    polydsp::Amplifier::saturateHardClip(buffer.at(1), -polydsp::kDoubleLoundnessDb);
#endif
}

}
