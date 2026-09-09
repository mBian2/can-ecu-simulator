#include "engine_ecu/EngineController.hpp"

#include <algorithm>
#include <cmath>

namespace ecu {
namespace {

double approach(double current, double target, double maximumChange) noexcept {
    return current + std::clamp(target - current, -maximumChange, maximumChange);
}

double seconds(std::chrono::milliseconds elapsed) noexcept {
    return std::chrono::duration<double>(elapsed).count();
}

} // namespace

UpdateError EngineController::update(EngineInputs inputs,
                                     std::chrono::milliseconds elapsed) noexcept {
    if (inputs.throttlePercent < 0 || inputs.throttlePercent > 100) {
        return UpdateError::InvalidThrottle;
    }
    if (elapsed.count() < 0 || elapsed > EngineParameters::maximumUpdate) {
        return UpdateError::InvalidElapsedTime;
    }
    if (elapsed.count() == 0) {
        return UpdateError::None;
    }

    if (!inputs.ignition) {
        // Stopping is deliberately immediate; there is no coast-down state.
        state_ = EngineState::Off;
        startingElapsed_ = std::chrono::milliseconds::zero();
        rpm_ = 0.0;
        throttle_ = 0;
        updateTemperature(EngineParameters::ambientCelsius, seconds(elapsed));
        return UpdateError::None;
    }

    if (state_ == EngineState::Off) {
        state_ = EngineState::Starting;
    }
    if (state_ == EngineState::Starting) {
        const auto startingTime = std::min(elapsed,
            EngineParameters::startupDuration - startingElapsed_);
        rpm_ = approach(rpm_, EngineParameters::idleRpm,
                       EngineParameters::startingRpmPerSecond * seconds(startingTime));
        updateTemperature(EngineParameters::idleCelsius, seconds(startingTime));
        throttle_ = 0; // Throttle has no effect while the starter is active.
        startingElapsed_ += startingTime;
        if (startingElapsed_ < EngineParameters::startupDuration) {
            return UpdateError::None;
        }
        // Only time beyond the startup boundary belongs to normal operation.
        elapsed -= startingTime;
    }

    throttle_ = inputs.throttlePercent;
    state_ = throttle_ == 0 ? EngineState::Idle : EngineState::Running;
    const double demand = static_cast<double>(throttle_) / 100.0;
    const double targetRpm = EngineParameters::idleRpm +
        demand * (EngineParameters::maximumRpm - EngineParameters::idleRpm);
    const double rpmRate = targetRpm >= rpm_ ? EngineParameters::accelerationRpmPerSecond
                                           : EngineParameters::decelerationRpmPerSecond;
    rpm_ = approach(rpm_, targetRpm, rpmRate * seconds(elapsed));
    const double targetTemperature = EngineParameters::idleCelsius +
        demand * (EngineParameters::fullThrottleCelsius - EngineParameters::idleCelsius);
    updateTemperature(targetTemperature, seconds(elapsed));
    return UpdateError::None;
}

void EngineController::updateTemperature(double targetCelsius, double elapsedSeconds) noexcept {
    const double rate = targetCelsius >= temperature_ ? EngineParameters::warmingCelsiusPerSecond
                                                    : EngineParameters::coolingCelsiusPerSecond;
    temperature_ = approach(temperature_, targetCelsius, rate * elapsedSeconds);
}

EngineStatus EngineController::telemetry() const noexcept {
    // Retain fractional progress internally; round only at the wire boundary.
    return {static_cast<std::uint16_t>(std::lround(rpm_)),
            static_cast<std::int16_t>(std::lround(temperature_)),
            static_cast<std::uint8_t>(throttle_)};
}

} // namespace ecu
