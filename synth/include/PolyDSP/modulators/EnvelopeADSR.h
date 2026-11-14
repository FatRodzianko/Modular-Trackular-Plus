#pragma once

#include "PolyDSP/Core.h"
#include "PolyDSP/ModMatrix.h"

#include <limits>

namespace polydsp
{
class EnvelopeADSR
{
  public:
    explicit EnvelopeADSR(float attackCurve = -1.5f, float decayCurve = -5.0f);

    [[nodiscard]] param_t getAttack() const { return attackTime; }
    void setAttack(param_t attack);
    [[nodiscard]] param_t getDecay() const { return decayTime; }
    void setDecay(param_t decay);
    void setDecayDestinationId(int id) { decayDestinationId = id; }
    void setSustainLevelDestinationId(int id) { sustainLevelDestinationId = id; }
    [[nodiscard]] param_t getRelease() const { return releaseTime; }
    void setRelease(param_t release);

    void render(const RenderContext& ctx, const ModMatrix& modMatrix, ModulationBuffer out);
    void startGate();
    void stopGate();
    void shutdown(bool restart = false);

    bool isActive() { return state != EGState::kOff; };

    // use minimum transition time to avoid clicks on 0 attack/decay/release
    void enableMinTransitionTime();

  private:
    static constexpr float kShutdownTime = 0.002f;
    static constexpr float kMinTransitionTime = 0.002f; // established experimentally

    enum class EGState : uint8_t
    {
        kOff,
        kAttack,
        kDecay,
        kSustain,
        kRelease,
        kShutdown
    };

    float output = 0.0;

    float attackTCO = 0.0;
    float attackLogTCO = 0.0;
    float decayTCO = 0.0;
    float decayLogTCO = 0.0;
    float attackCoeff = 0.0;
    float attackOffset = 0.0;
    float decayCoeff = 0.0;
    float decayOffset = 0.0;
    float releaseCoeff = 0.0;
    float releaseOffset = 0.0;

    float shutdownInc = 0.0;
    float startLevel = 0.0f;

    param_t attackTime = 0;
    param_t decayTime = 0;
    param_t releaseTime = 0;
    param_t minTransitionTime = 0;

    float lastSustainLevel = std::numeric_limits<float>::max();
    float lastDecay = std::numeric_limits<float>::max();

    tiny_optional<int> sustainLevelDestinationId = std::nullopt;
    tiny_optional<int> decayDestinationId = std::nullopt;

    EGState state = EGState::kOff;
    EGState statePostShutdown = EGState::kOff;

    bool legato = false;
    bool resetOnGate = false;

    bool updateAttack = true;
    bool updateRelease = true;

    void update(const RenderContext& ctx, float sustainLevelMod);
};
}