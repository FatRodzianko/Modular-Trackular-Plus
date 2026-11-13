#pragma once

#include "PolyDSP/Core.h"
#include "PolyDSP/filters/BiquadFilter.h"

#include <memory>
#include <span>
#include <variant>

namespace polydsp
{
class ReverbModelProxy;

class Reverb
{
  public:
    static inline constexpr std::array kModelNames = { "Natural", "Plate", "Warp" };

    enum Model
    {
        kModelTank,
        kModelDatorro,
        kModelMutable,
    };

    explicit Reverb(std::span<sample_t> buffer);
    ~Reverb();

    static size_t getBufferSize();

    void setModel(Model model);
    void setDecay(float decay);
    void setDiffusion(float diffusion);
    void setTone(float tone);
    void setPredelay(float predelay);
    void setGeometry(float geometry);
    void setSpaceSize(float size);
    void setShelfFilterFreq(const RenderContext& ctx, time_t frequency);
    void setShelfFilterGain(const RenderContext& ctx, param_t gainDb);

    void render(const AudioBuffer& in, std::span<AudioBuffer, 2> out);

    Reverb(const Reverb& other) = delete;
    Reverb(Reverb&& other) = delete;
    Reverb& operator=(const Reverb& other) = delete;
    Reverb& operator=(Reverb&& other) = delete;

  private:
    std::unique_ptr<ReverbModelProxy> reverb;
    std::span<sample_t> buffer;

    BiquadFilter shelfFilter;
    time_t shelfFreq = 5000;
    time_t shelfGain = 0;

    float decay = 0.5f;
    float diffusion = 1.0f;
    float tone = 0.5f;
    float predelay = 0.0f;
    float geometry = 1.0f;
    float spaceSize = 1.0f;

    void updateShelfFilter(const RenderContext& ctx);
};
}