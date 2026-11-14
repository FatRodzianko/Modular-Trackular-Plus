#include "PolyDSP/misc/Mixer.h"

#include "config.h"

#include <algorithm>

namespace polydsp
{

Mixer::Channel::Channel()
{
    std::ranges::fill(sendGainsDb, kSilenceGain);
};

void Mixer::render(std::span<std::array<const AudioBuffer*, 2>> inputBufs,
                   std::span<AudioBuffer*, 2> masterOutputBuf,
                   std::span<AudioBuffer*> sendOutputBufs) const
{
    auto inputs = std::ssize(inputBufs);
    assert(inputs <= std::ssize(channels));
    assert(std::ssize(sendOutputBufs) <= sendCount);

    std::ranges::fill(*masterOutputBuf[0], 0.0f);
    std::ranges::fill(*masterOutputBuf[1], 0.0f);

    for (auto& sendOutputBuf : sendOutputBufs)
    {
        std::ranges::fill(*sendOutputBuf, 0.0f);
    }

    for (int channelIdx = 0; channelIdx < inputs; channelIdx++)
    {
        if (inputBufs[channelIdx][0] == nullptr)
        {
            continue;
        }

        const auto& channel = channels.at(channelIdx);
        auto gainRaw = dbToAmplitude(channel.gainDb);
        auto rightMult = gainRaw * (channel.panning >= 0.0f ? 1 : (1.0f + channel.panning));
        auto leftMult = gainRaw * (channel.panning <= 0.0f ? 1 : (1.0f - channel.panning));

        if (inputBufs[channelIdx][1] == nullptr)
        {
            auto inEnd = inputBufs[channelIdx][0]->end();
            for (auto leftOutIt = masterOutputBuf[0]->begin(),
                      rightOutIt = masterOutputBuf[1]->begin(),
                      inIt = inputBufs[channelIdx][0]->begin();
                 inIt != inEnd;)
            {
                auto sample = *inIt++;
                *leftOutIt++ += sample * leftMult;
                *rightOutIt++ += sample * rightMult;
            }
        }
        else
        {
            auto leftInEnd = inputBufs[channelIdx][0]->end();
            for (auto leftOutIt = masterOutputBuf[0]->begin(),
                      rightOutIt = masterOutputBuf[1]->begin(),
                      leftInIt = inputBufs[channelIdx][0]->begin(),
                      rightInIt = inputBufs[channelIdx][1]->begin();
                 leftInIt != leftInEnd;)
            {
                *leftOutIt++ += (*leftInIt++) * leftMult;
                *rightOutIt++ += (*rightInIt++) * rightMult;
            }
        }

        int sendIdx = 0;
        for (auto& sendOutputBuf : sendOutputBufs)
        {
            float sendMult = gainRaw * dbToAmplitude(channel.sendGainsDb[sendIdx]);

            auto inEnd = inputBufs[channelIdx][0]->end();
            for (auto outIt = sendOutputBuf->begin(), inIt = inputBufs[channelIdx][0]->begin();
                 inIt != inEnd;)
            {
                auto sample = *inIt++;
                *outIt++ += sample * sendMult;
            }

            sendIdx++;
        }
    }
}

void Mixer::renderAdd(std::span<std::array<const AudioBuffer*, 2>> inputBufs,
                      std::span<AudioBuffer*, 2> outputBuf)
{
    auto outEnd = outputBuf[0]->end();
    for (auto& inputBuf : inputBufs)
    {
        assert(inputBuf[0]->size() == outputBuf[0]->size());
        assert(inputBuf[1]->size() == outputBuf[1]->size());

        for (auto leftOutIt = outputBuf[0]->begin(),
                  rightOutIt = outputBuf[1]->begin(),
                  leftInIt = inputBuf[0]->begin(),
                  rightInIt = inputBuf[1]->begin();
             leftOutIt != outEnd;)
        {
            *leftOutIt++ += *leftInIt++;
            *rightOutIt++ += *rightInIt++;
        }
    }
}

void Mixer::renderMixdownAdd(std::span<AudioBuffer, 2> inputBuf,
                             AudioBuffer outputBuf,
                             float gainDb)
{
    auto gainRaw = dbToAmplitude(gainDb);
    auto outEnd = outputBuf.end();

    for (auto outIt = outputBuf.begin(),
              leftInIt = inputBuf[0].begin(),
              rightInIt = inputBuf[1].begin();
         outIt != outEnd;)
    {
        *outIt++ += gainRaw * (*leftInIt++ + *rightInIt++) / 2;
    }
}

void Mixer::renderMixFx(AudioBuffer inputBuf, AudioBuffer fxBuf, float wet, AudioBuffer outputBuf)
{
    assert(wet >= 0.0f && wet <= 1.0f);

    auto outEnd = outputBuf.end();
    auto dry = 1.0f - wet;

    for (auto outIt = outputBuf.begin(), inIt = inputBuf.begin(), fxIt = fxBuf.begin();
         outIt != outEnd;)
    {
        *outIt++ = *inIt++ * dry + *fxIt++ * wet;
    }
}

}
