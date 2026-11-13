#pragma once

#include "PolyDSP/Core.h"

namespace polydsp
{
class Amplifier
{
  public:
    void setGain(param_t gaindB) { this->volumeMult = dbToAmplitude(gaindB); };

    [[nodiscard]] param_t getVolumeMult() const { return volumeMult; }
    void setVolumeMult(param_t mult) { this->volumeMult = mult; }

    void render(const RenderContext& ctx,
                AudioBuffer& buf,
                ConstModulationBuffer gainModulationRaw) const;
    void renderAdd(AudioBuffer& inputBuf,
                   AudioBuffer& outputBuf,
                   ConstModulationBuffer gainModulationRaw) const;

    static void saturateHardClip(AudioBuffer& buf, float gainDb);
    static void saturateSoftClip(AudioBuffer& buf, float gainDb);
    static void saturateLight(AudioBuffer& buf, float gainDb);
    static void saturateOverdrive(AudioBuffer& buf, float gainDb);
    static void saturateOverdriveNoMakeup(AudioBuffer& buf, float gainDb);
    static void saturateOverdrive2(AudioBuffer& buf, float gainDb);
    static void saturateDiode(AudioBuffer& buf, float multiplier);
    static void saturateFuzz(AudioBuffer& buf, float multiplier);
    static void saturateAsymmetric(AudioBuffer& buf, float multiplier);
    static void saturateSin(AudioBuffer& buf, float multiplier);
    static void saturateSin2(AudioBuffer& buf, float multiplier);
    static void bitcrush(AudioBuffer& buf, float bits);
    static void bitcrush2(AudioBuffer& buf, float bits);
    static void sampleRateRedux(AudioBuffer& buf, int factor);
    static void sampleRateRedux2(AudioBuffer& buf, int factor);
    static void rectifier(AudioBuffer& buf, float amount);

  private:
    static constexpr param_t kDefaultModulationRange = 96;

    param_t volumeMult = 1.0f;
    param_t modulationRangeDb = kDefaultModulationRange;
};

}
