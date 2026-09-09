#pragma once

#include <array>
#include <cstdint>

namespace ecu {

struct CanFrame {
    std::uint32_t id{};
    std::uint8_t length{};
    std::array<std::uint8_t, 8> data{};
    // Keep transport flags separate so an RTR or extended frame cannot be
    // mistaken for a standard data frame with the same numeric identifier.
    bool extended{};
    bool remote{};
    bool error{};
};

} // namespace ecu
