#pragma once

#include "common/can/CanProtocol.hpp"
#include "engine_ecu/EngineState.hpp"

#include <chrono>

namespace ecu {

struct EngineInputs {
    bool ignition{};
    int throttlePercent{}; // Signed so negative requests can be rejected.
};

enum class UpdateError { None, InvalidThrottle, InvalidElapsedTime };

struct EngineParameters {
    static constexpr std::chrono::milliseconds startupDuration{1000};
    static constexpr std::chrono::milliseconds maximumUpdate{1000};
    static constexpr double idleRpm = 800.0;
    static constexpr double maximumRpm = 5300.0;
    static constexpr double startingRpmPerSecond = 800.0;
    static constexpr double accelerationRpmPerSecond = 1500.0;
    static constexpr double decelerationRpmPerSecond = 2000.0;
    static constexpr double ambientCelsius = 20.0;
    static constexpr double idleCelsius = 80.0;
    static constexpr double fullThrottleCelsius = 100.0;
    static constexpr double warmingCelsiusPerSecond = 2.0;
    static constexpr double coolingCelsiusPerSecond = 1.0;
};

class EngineController {
public:
    // Inputs apply throughout this interval. Zero time is a no-op. Invalid
    // inputs or intervals are rejected without changing any controller state.
    UpdateError update(EngineInputs inputs, std::chrono::milliseconds elapsed) noexcept;

    EngineState state() const noexcept { return state_; }
    EngineStatus telemetry() const noexcept;

private:
    void updateTemperature(double targetCelsius, double seconds) noexcept;

    EngineState state_{EngineState::Off};
    std::chrono::milliseconds startingElapsed_{};
    double rpm_{};
    double temperature_{EngineParameters::ambientCelsius};
    int throttle_{};
};

} // namespace ecu
