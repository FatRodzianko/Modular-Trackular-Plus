#pragma once

#include "PolySynth/Mixer.h"
#include "PolySynth/config.h"

#include "PolyDSP/misc/Mixer.h"

#include <span>

namespace polysynth
{

class MixerImpl final : public Mixer
{
  public:
    explicit MixerImpl(int channelsCount, int sendsCount);

    [[nodiscard]] float getChannelGain(int channel) override;
    void setChannelGain(int channel, float gainDb) override;
    [[nodiscard]] float getChannelPanning(int channel) override;
    void setChannelPanning(int channel, float panning) override;
    [[nodiscard]] float getChannelSend(int channel, int send) override;
    void setChannelSend(int channel, int send, float gainDb) override;
    [[nodiscard]] float getReturnChannelSend(int returnChannel, int send) override;
    void setReturnChannelSend(int returnChannel, int send, float gainDb) override;

    void connectInput(int channel, std::shared_ptr<Instrument> instrument) override;
    void disconnectInput(int channel) override;
    void connectSend(int send, std::shared_ptr<Instrument> instrument) override;

    void renderAudioBuffer(AudioBufferStereoRef buffer);

  private:
    polydsp::Mixer mixer;

    struct Channel
    {
        std::shared_ptr<Instrument> instrument = nullptr;
        std::array<std::array<sample_t, kAudioBufferSize>, 2> buffers = {};
        AudioBufferStereoRef bufferRefs = { buffers[0], buffers[1] };
    };

    struct Send
    {
        std::shared_ptr<Instrument> instrument = nullptr;
        std::array<sample_t, kAudioBufferSize> buffer = {};
        std::array<std::array<sample_t, kAudioBufferSize>, 2> returnBuffer = {};
        AudioBufferRef bufferRef = { buffer };
        AudioBufferStereoRef returnBufferRef = { returnBuffer[0], returnBuffer[1] };
        std::vector<float> returnSendGains;
    };

    std::vector<Channel> channels;
    std::vector<Send> sends;
    std::vector<std::array<const AudioBufferRef*, 2>> inputBuffers;
    std::vector<AudioBufferRef*> sendBuffers;
    std::vector<std::array<const AudioBufferRef*, 2>> sendReturnBuffers;
};
}