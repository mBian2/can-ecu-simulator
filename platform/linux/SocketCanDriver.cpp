#include "platform/linux/SocketCanDriver.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace ecu {
namespace {

std::error_code systemError() noexcept {
    return {errno, std::generic_category()};
}

} // namespace

SocketCanDriver::SocketCanDriver(const std::string& interfaceName) {
    if (interfaceName.empty() || interfaceName.size() >= IFNAMSIZ ||
        interfaceName.find('\0') != std::string::npos) {
        throw std::invalid_argument("invalid CAN interface name");
    }
    const auto index = if_nametoindex(interfaceName.c_str());
    if (index == 0) {
        throw std::system_error(systemError(), "find CAN interface " + interfaceName);
    }

    socket_ = ::socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, CAN_RAW);
    if (socket_ < 0) {
        throw std::system_error(systemError(), "open SocketCAN socket");
    }
    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = static_cast<int>(index);
    if (::bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        const auto error = systemError();
        // A failed constructor never runs this object's destructor.
        ::close(socket_);
        socket_ = -1;
        throw std::system_error(error, "bind CAN interface " + interfaceName);
    }
    // CAN_RAW_FD_FRAMES stays disabled: this driver handles Classical CAN only.
    // Default local loopback lets separate processes share the same vcan bus.
}

SocketCanDriver::~SocketCanDriver() {
    if (socket_ >= 0) {
        ::close(socket_);
    }
}

std::error_code SocketCanDriver::send(const CanFrame& frame) {
    if (frame.extended || frame.remote || frame.error || frame.id > CAN_SFF_MASK ||
        frame.length > frame.data.size()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    can_frame native{};
    native.can_id = frame.id;
    native.len = frame.length;
    std::copy_n(frame.data.begin(), frame.length, native.data);
    const auto written = ::write(socket_, &native, sizeof(native));
    if (written < 0) {
        return systemError();
    }
    if (written != static_cast<ssize_t>(sizeof(native))) {
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}

ReceiveResult SocketCanDriver::receive(CanFrame& frame, std::chrono::milliseconds timeout) {
    if (timeout.count() < 0 || timeout.count() > INT_MAX) {
        return {ReceiveStatus::Error, std::make_error_code(std::errc::invalid_argument)};
    }
    pollfd descriptor{socket_, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
    if (ready < 0) {
        return errno == EINTR ? ReceiveResult{ReceiveStatus::Interrupted}
                             : ReceiveResult{ReceiveStatus::Error, systemError()};
    }
    if (ready == 0) {
        return {ReceiveStatus::Timeout};
    }
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        return {ReceiveStatus::Error, std::make_error_code(std::errc::io_error)};
    }
    if (!(descriptor.revents & POLLIN)) {
        return {ReceiveStatus::Error, std::make_error_code(std::errc::io_error)};
    }

    can_frame native{};
    const auto received = ::read(socket_, &native, sizeof(native));
    if (received < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return {ReceiveStatus::Interrupted};
        }
        return {ReceiveStatus::Error, systemError()};
    }
    if (received != static_cast<ssize_t>(sizeof(native)) || native.len > CAN_MAX_DLEN) {
        return {ReceiveStatus::Error, std::make_error_code(std::errc::protocol_error)};
    }

    CanFrame result{};
    result.extended = (native.can_id & CAN_EFF_FLAG) != 0;
    result.remote = (native.can_id & CAN_RTR_FLAG) != 0;
    result.error = (native.can_id & CAN_ERR_FLAG) != 0;
    result.id = native.can_id & CAN_EFF_MASK;
    result.length = native.len;
    std::copy_n(native.data, result.length, result.data.begin());
    frame = result;
    return {ReceiveStatus::Frame};
}

} // namespace ecu
