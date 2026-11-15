#ifndef LIBS_AUDIO_EFFECT_LIMITER_H_
#define LIBS_AUDIO_EFFECT_LIMITER_H_

#include "AudioStream.h"
#include "modsBuild.h" // MT:

constexpr uint8_t LIMITER_DELAY = 5;

class AudioEffectLimiter : public AudioStream
{
  public:
    AudioEffectLimiter(void)
      : AudioStream(2, inputQueueArray)
    {
    }

    void begin();
    void setThreshold(uint16_t threshold);
    void setAttack(uint16_t attack);
    void setRelease(float release);
    void setSidechainEnable(bool isEnable);
    float getAverageGainChange();
    bool isAvailableGainToGet();
    virtual void update(void);

    void processSidechain();
    void processMainStream();
    void blockUpdate();
    void unblockUpdate();
#if MT_HAS_MASTERFX // MT: the dynamics slot's plugin runs both sides from the left object
    void modsUpdate();
#endif

  private:
    audio_block_t* inputQueueArray[2];
    int16_t inputStorageBuffer[LIMITER_DELAY];
    int16_t outputStorageBuffer[LIMITER_DELAY];
    uint16_t threshold;
    float g = 1.0;
    uint16_t xpeak;
    float attackTime;
    float releaseTime;
    uint16_t a;
    float coeff;
    float f;
    float averageGain;
    float sumGain;
    bool isAverageGain;
    bool isSideChainEnable;
    bool isBlocked;
#if MT_HAS_MASTERFX // MT: the knobs as the setters were given them, for the plugin
    uint16_t modsAttack = 300;
    float modsRelease = 10.0f;
#endif
};

#endif /* LIBS_AUDIO_EFFECT_LIMITER_H_ */
