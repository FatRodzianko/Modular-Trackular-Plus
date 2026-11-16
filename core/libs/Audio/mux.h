#ifndef audio_mux_h_
#define audio_mux_h_

#include "AudioStream.h"
#include "modsBuild.h" // MT:

#ifdef FEATURE_SYNTH
constexpr uint8_t MUX_SIZE = 10 + kSynthParts;
#else
constexpr uint8_t MUX_SIZE = 10;
#endif

class AudioMux : public AudioStream
{
  public:
    AudioMux(void)
      : AudioStream(MUX_SIZE, inputQueueArray)
    {
    }
    virtual void update(void);
    void setOut(uint8_t channel);
    void setEnabled(bool enabled);
#if MT_HAS_MASTERFX // MT: the chosen input's block once its source has sent it this cycle (until
                    // update() takes it), or null. Read only, in the audio interrupt.
    const audio_block_t* modsPeek() const { return inputQueueArray[outChannel]; }
#endif

  private:
    uint8_t outChannel = 0;
    bool muxEnabled = true;
    audio_block_t* inputQueueArray[MUX_SIZE];
};

#endif
