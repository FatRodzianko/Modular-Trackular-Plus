#pragma once

#include "Core.h"

#include "PolyUtil/misc.h"

#include <array>
#include <cassert>
#include <optional>
#include <tuple>
#include <vector>

namespace polydsp
{

/**
 * Modulation matrix utility - aggregates multiple source modulations to destination.
 * Each source modulation is associated with a multiplier value.
 * For each destination there is a base value that source modulations are added to.
 * The base value is smoothed out - linearly interpolated over the first buffer
 * after each change.
 *
 * Note: to save on memory, all instances use a shared pool of buffers for sources and destinations,
 * based on assumption that the buffers content does not need to be preserved and
 * that only one instance of ModMatrix is used at a time (during processing of a synth voice).
 */
class ModMatrix
{
  public:
    /**
     * Definition of a modulation matrix entry.
     *
     */
    struct Modulation
    {
        enum class Operation
        {
            kAdd,
            kMult,
            kMultBipolar,
        };

        int modulationId = -1;
        int sourceId = -1;
        int destinationId = -1;

        /**
         * Initial value of the multiplier.
         */
        param_t multiplier = 0;

        /**
         * Id of a destination that will be added to the multiplier.
         * Enable modulating of multiplier value with another modulation.
         */
        tiny_optional<int> multiplierAddDestinationId = std::nullopt;

        Operation operation = Operation::kAdd;
    };

    enum class UpdatePhase
    {
        kNone = 0,
        kFillBaseValues = 1,
        kUpdateModulations = 2,
        kAll = kFillBaseValues | kUpdateModulations
    };

    ModMatrix(int modulationCount, int sourceCount, int destinationCount);

    void setModulations(std::span<const Modulation> modulations);

    [[nodiscard]] ModulationBuffer getSourceBuffer(int sourceId);
    [[nodiscard]] ConstModulationBuffer getDestinationBuffer(int destinationId) const;

    [[nodiscard]] param_t getBaseValue(int destinationId);
    void setBaseValue(int destinationId, param_t value);
    [[nodiscard]] param_t getMultiplier(int modulationId) const;
    void setMultiplier(int modulationId, param_t multiplier);

    // calculate matrix and fill destination buffers
    void update(UpdatePhase phases = UpdatePhase::kAll);

    // reset base values history so that smoothing is not applied in next update
    void resetSmoothing();

    void setModulationSource(int modulationId, int sourceId);
    void setModulationDestination(int modulationId, int destinationId);
    [[nodiscard]] int getModulationDestination(int modulationId) const;

    void setOperation(int modulationId, Modulation::Operation operation);

  private:
    static constexpr param_t kSmoothingThreshold = 0.0001f;

    struct Destination
    {
        param_t baseValue = 0;
        param_t previousBaseValue = 0;
        std::span<param_short_t, kBufferSize> buffer =
          std::span<param_short_t, kBufferSize>{ static_cast<param_short_t*>(nullptr),
                                                 kBufferSize };
    };

    static std::vector<std::array<param_short_t, kBufferSize>> buffersPool;

    std::vector<std::span<param_short_t, kBufferSize>> sourceBuffers;
    std::vector<Modulation> modulations;
    std::vector<Destination> destinations;
    bool skipSmoothing = true;
#ifdef DEBUG
    UpdatePhase phasesPassed = {};
#endif

    void fillDestinationBaseValue(Destination& destination) const;
    void updateModulation(const Modulation& modulation);
};

inline ModulationBuffer ModMatrix::getSourceBuffer(int sourceId)
{
    assert(sourceId >= 0 && sourceId < std::ssize(sourceBuffers));
    return sourceBuffers[sourceId];
}

inline ConstModulationBuffer ModMatrix::getDestinationBuffer(int destinationId) const
{
    assert(destinationId >= 0 && destinationId < std::ssize(destinations));
    return destinations[destinationId].buffer;
}

inline void ModMatrix::setBaseValue(int destinationId, param_t value)
{
    assert(destinationId >= 0 && destinationId < std::ssize(destinations));
    destinations[destinationId].baseValue = value;
}

inline param_t ModMatrix::getBaseValue(int destinationId)
{
    assert(destinationId >= 0 && destinationId < std::ssize(destinations));
    return destinations[destinationId].baseValue;
}

inline void ModMatrix::setMultiplier(int modulationId, param_t multiplier)
{
    assert(modulationId >= 0 && modulationId < std::ssize(modulations));
    modulations[modulationId].multiplier = multiplier;
}
}