#pragma once

#include "common/can/CanFrame.hpp"

#include <optional>

namespace ecu {

enum class CanMessageId : std::uint32_t { EngineStatus = 0x100 };

struct EngineStatus {
    std::uint16_t rpm{};                 // 0..12,000 rpm
    std::int16_t coolantTemperature{};   // -40..215 degrees Celsius
    std::uint8_t throttlePosition{};     // 0..100 percent
};

enum class DecodeError { None, UnsupportedFrame, WrongId, WrongLength, OutOfRange };

struct DecodeResult {
    EngineStatus status{}; // Only valid when error is None.
    DecodeError error{DecodeError::None};
};

// Invalid application values must not silently wrap or become valid telemetry.
std::optional<CanFrame> serialize(const EngineStatus& status) noexcept;
DecodeResult deserializeEngineStatus(const CanFrame& frame) noexcept;
const char* describe(DecodeError error) noexcept;

} // namespace ecu
