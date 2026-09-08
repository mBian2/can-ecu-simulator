#include "common/can/CanProtocol.hpp"

namespace ecu {
namespace {

constexpr std::uint8_t statusLength = 5;

bool valid(const EngineStatus& status) noexcept {
    return status.rpm <= 12000 && status.coolantTemperature >= -40 &&
           status.coolantTemperature <= 215 && status.throttlePosition <= 100;
}

void write16(CanFrame& frame, std::size_t offset, std::uint16_t value) noexcept {
    frame.data[offset] = static_cast<std::uint8_t>(value >> 8);
    frame.data[offset + 1] = static_cast<std::uint8_t>(value & 0xffu);
}

std::uint16_t read16(const CanFrame& frame, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(frame.data[offset]) << 8) | frame.data[offset + 1]);
}

} // namespace

std::optional<CanFrame> serialize(const EngineStatus& status) noexcept {
    if (!valid(status)) {
        return std::nullopt;
    }
    CanFrame frame{};
    frame.id = static_cast<std::uint32_t>(CanMessageId::EngineStatus);
    frame.length = statusLength;
    write16(frame, 0, status.rpm);
    // Conversion to unsigned gives the required modulo-65536 wire encoding.
    write16(frame, 2, static_cast<std::uint16_t>(status.coolantTemperature));
    frame.data[4] = status.throttlePosition;
    return frame;
}

DecodeResult deserializeEngineStatus(const CanFrame& frame) noexcept {
    if (frame.extended || frame.remote || frame.error) {
        return {{}, DecodeError::UnsupportedFrame};
    }
    if (frame.id != static_cast<std::uint32_t>(CanMessageId::EngineStatus)) {
        return {{}, DecodeError::WrongId};
    }
    if (frame.length != statusLength) {
        return {{}, DecodeError::WrongLength};
    }

    const auto rawTemperature = read16(frame, 2);
    // Avoid implementation-defined conversion of a large unsigned value to
    // int16_t in C++17. Sign-extend arithmetically before narrowing.
    const std::int32_t temperature = rawTemperature <= 0x7fff
        ? static_cast<std::int32_t>(rawTemperature)
        : static_cast<std::int32_t>(rawTemperature) - 65536;
    const EngineStatus status{read16(frame, 0), static_cast<std::int16_t>(temperature), frame.data[4]};
    if (!valid(status)) {
        return {{}, DecodeError::OutOfRange};
    }
    return {status, DecodeError::None};
}

const char* describe(DecodeError error) noexcept {
    switch (error) {
    case DecodeError::None: return "valid message";
    case DecodeError::UnsupportedFrame: return "expected a standard CAN data frame";
    case DecodeError::WrongId: return "unexpected CAN identifier";
    case DecodeError::WrongLength: return "engine status requires exactly 5 bytes";
    case DecodeError::OutOfRange: return "telemetry value outside protocol range";
    }
    return "unknown decode error";
}

} // namespace ecu
