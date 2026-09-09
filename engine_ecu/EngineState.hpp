#pragma once

namespace ecu {

enum class EngineState { Off, Starting, Idle, Running };

inline const char* describe(EngineState state) noexcept {
    switch (state) {
    case EngineState::Off: return "Off";
    case EngineState::Starting: return "Starting";
    case EngineState::Idle: return "Idle";
    case EngineState::Running: return "Running";
    }
    return "Unknown";
}

} // namespace ecu
