#pragma once

#include "engine_ecu/EngineController.hpp"

namespace ecu {

// Call with nonnegative simulation time at the beginning of an update interval.
inline EngineInputs demoInputs(std::chrono::milliseconds simulationTime) noexcept {
    const auto phase = simulationTime.count() % 12000;
    if (phase < 1000 || phase >= 8000) {
        return {false, 0};
    }
    if (phase >= 3000 && phase < 6000) {
        return {true, 60};
    }
    return {true, 0};
}

} // namespace ecu
