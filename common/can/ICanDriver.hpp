#pragma once

#include "common/can/CanFrame.hpp"

#include <chrono>
#include <system_error>

namespace ecu {

enum class ReceiveStatus { Frame, Timeout, Interrupted, Error };

struct ReceiveResult {
    ReceiveStatus status;
    std::error_code error{};
};

class ICanDriver {
public:
    virtual ~ICanDriver() = default;
    // Send attempts once; a full transmit queue is reported to the caller.
    virtual std::error_code send(const CanFrame& frame) = 0;
    // Output changes only on Frame. Each driver has a single owning caller.
    virtual ReceiveResult receive(CanFrame& frame, std::chrono::milliseconds timeout) = 0;
};

} // namespace ecu
