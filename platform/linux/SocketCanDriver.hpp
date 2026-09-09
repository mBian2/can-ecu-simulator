#pragma once

#include "common/can/ICanDriver.hpp"

#include <string>

namespace ecu {

class SocketCanDriver final : public ICanDriver {
public:
    explicit SocketCanDriver(const std::string& interfaceName);
    ~SocketCanDriver() override;

    SocketCanDriver(const SocketCanDriver&) = delete;
    SocketCanDriver& operator=(const SocketCanDriver&) = delete;
    SocketCanDriver(SocketCanDriver&&) = delete;
    SocketCanDriver& operator=(SocketCanDriver&&) = delete;

    std::error_code send(const CanFrame& frame) override;
    ReceiveResult receive(CanFrame& frame, std::chrono::milliseconds timeout) override;

private:
    int socket_{-1};
};

} // namespace ecu
