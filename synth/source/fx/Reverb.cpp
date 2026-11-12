#include "PolyDSP/fx/Reverb.h"

#include "reverb/ReverbModelDattorro.h"
#include "reverb/ReverbModelMutable.h"
#include "reverb/ReverbModelTank.h"

#include "PolyUtil/variant.h"
#include "enum_util.h"

namespace polydsp
{

class ReverbModelProxy
{
  public:
    std::variant<ReverbModelTank, ReverbModelDattorro, ReverbModelMutable> v;
};

Reverb::Reverb(std::span<sample_t> buffer)
  : reverb(std::make_unique<ReverbModelProxy>())
  , buffer(buffer)
{
    assert(buffer.data() != nullptr);
    assert(buffer.size() == getBufferSize());

    std::visit([buffer](auto& reverb) { reverb.init(buffer); }, reverb->v);
}

Reverb::~Reverb() {} // NOLINT(*-use-equals-default) required because of polyverb include only here

void Reverb::setModel(Model model)
{
    reverb->v = createVariant<decltype(ReverbModelProxy::v)>(static_cast<int>(model));
    std::visit(
      [this](auto& reverb)
      {
          reverb.init(buffer);
          reverb.setDecay(decay);
          reverb.setDiffusion(diffusion);
          reverb.setTone(tone);
          reverb.setPredelay(predelay);
          reverb.setGeometry(geometry);
          reverb.setSpaceSize(spaceSize);
      },
      reverb->v);
}

size_t Reverb::getBufferSize()
{
    return std::max(
      ReverbModelTank::getBufferSize(),
      std::max(ReverbModelMutable::getBufferSize(), ReverbModelDattorro::getBufferSize()));
}

void Reverb::render(const AudioBuffer& in, std::span<AudioBuffer, 2> out)
{
    std::visit([in, out](auto& reverb) { reverb.render(in, out); }, reverb->v);

    if (shelfGain != 0.0f)
    {
        for (auto itL = out[0].begin(), itR = out[1].begin(); itL != out[0].end(); itL++, itR++)
        {
            std::tie(*itL, *itR) = shelfFilter.render({ *itL, *itR });
        }
    }
}

void Reverb::setDecay(float decay)
{
    this->decay = decay;
    std::visit([decay](auto& reverb) { reverb.setDecay(decay); }, reverb->v);
};

void Reverb::setDiffusion(float diffusion)
{
    this->diffusion = diffusion;
    std::visit([diffusion](auto& reverb) { reverb.setDiffusion(diffusion); }, reverb->v);
};

void Reverb::setTone(float tone)
{
    this->tone = tone;
    std::visit([tone](auto& reverb) { reverb.setTone(tone); }, reverb->v);
};

void Reverb::setPredelay(float predelay)
{
    this->predelay = predelay;
    std::visit([predelay](auto& reverb) { reverb.setPredelay(predelay); }, reverb->v);
};

void Reverb::setGeometry(float geometry)
{
    this->geometry = geometry;
    std::visit([geometry](auto& reverb) { reverb.setGeometry(geometry); }, reverb->v);
};

void Reverb::setSpaceSize(float size)
{
    this->spaceSize = size;
    std::visit([size](auto& reverb) { reverb.setSpaceSize(size); }, reverb->v);
};

void Reverb::setShelfFilterFreq(const RenderContext& ctx, time_t frequency)
{
    shelfFreq = frequency;
    updateShelfFilter(ctx);
}

void Reverb::setShelfFilterGain(const RenderContext& ctx, param_t gainDb)
{
    shelfGain = gainDb;
    updateShelfFilter(ctx);
}

void Reverb::updateShelfFilter(const RenderContext& ctx)
{
    shelfFilter.setup(ctx, BiquadFilter::Shape::kHighShelf, shelfGain, shelfFreq, 1.0f);
}
}