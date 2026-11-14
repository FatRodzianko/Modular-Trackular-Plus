#pragma once

#include "PolyDSP/Core.h"

#include <vector>

namespace polydsp
{
class Mixer
{
  public:
    explicit Mixer(int channelCount, int sendCount)
      : sendCount(sendCount)
    {
        channels.resize(channelCount);
    };

    float getChannelGain(int channel) { return channels.at(channel).gainDb; };
    void setChannelGain(int channel, float gainDb) { channels.at(channel).gainDb = gainDb; };
    float getChannelPanning(int channel) { return channels.at(channel).panning; };
    void setChannelPanning(int channel, float panning) { channels.at(channel).panning = panning; };
    float getChannelSend(int channel, int send) { return channels.at(channel).sendGainsDb[send]; };
    void setChannelSend(int channel, int send, float gainDb);

    /**
     * @param inputBufs pairs of (left, right) inputs, right is nullptr for mono inputs
     * @param masterOutputBuf (left, right) master output
     * @param sendOutputBufs mono send bus outputs
     */
    void render(std::span<std::array<const AudioBuffer*, 2>> inputBufs,
                std::span<AudioBuffer*, 2> masterOutputBuf,
                std::span<AudioBuffer*> sendOutputBufs) const;

    /**
     * Adds buffers to existing content of output, no gain/pan control.
     *
     * @param inputBufs pairs of (left, right) inputs
     * @param outputBuf (left, right) output buffer to add to
     */
    static void renderAdd(std::span<std::array<const AudioBuffer*, 2>> inputBufs,
                          std::span<AudioBuffer*, 2> outputBuf);

    /**
     * Adds input buffer to existing content of output, mixing down stereo to mono.
     *
     * @param inputBuf
     * @param outputBuf
     * @param gainDb
     */
    static void renderMixdownAdd(std::span<AudioBuffer, 2> inputBuf,
                                 AudioBuffer outputBuf,
                                 float gainDb);

    /**
     * Renders main signal with fx processed signal in the given dry/wet proportion.
     *
     * @param wet 0 fully dry, 1 fully wet
     */
    static void renderMixFx(AudioBuffer inputBuf,
                            AudioBuffer fxBuf,
                            float wet,
                            AudioBuffer outputBuf);

  private:
    struct Channel
    {
        Channel();

        float gainDb = 0;
        float panning = 0;
        std::array<float, kMixerMaxSends> sendGainsDb = {};
    };

    std::vector<Channel> channels;
    int sendCount;
};

inline void Mixer::setChannelSend(int channel, int send, float gainDb)
{
    channels.at(channel).sendGainsDb[send] = gainDb;
};

}