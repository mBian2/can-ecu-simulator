#include "common/can/CanProtocol.hpp"
#include "platform/linux/SocketCanDriver.hpp"

#include <chrono>
#include <csignal>
#include <iostream>
#include <string>

namespace {
volatile std::sig_atomic_t stopRequested = 0;

void requestStop(int) {
    stopRequested = 1;
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc > 2 || (argc == 2 && std::string(argv[1]) == "--help")) {
        std::cout << "Usage: dashboard_ecu [CAN interface, default: vcan0]\n";
        return argc > 2 ? 1 : 0;
    }
    const std::string interfaceName = argc == 2 ? argv[1] : "vcan0";
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);

    try {
        ecu::SocketCanDriver driver(interfaceName);
        std::cout << "Dashboard listening on " << interfaceName << ". Ctrl+C to stop." << std::endl;
        while (!stopRequested) {
            ecu::CanFrame frame{};
            const auto result = driver.receive(frame, std::chrono::milliseconds(100));
            if (result.status == ecu::ReceiveStatus::Timeout ||
                result.status == ecu::ReceiveStatus::Interrupted) {
                continue;
            }
            if (result.status == ecu::ReceiveStatus::Error) {
                throw std::system_error(result.error, "receive CAN frame on " + interfaceName);
            }
            const auto decoded = ecu::deserializeEngineStatus(frame);
            if (decoded.error != ecu::DecodeError::None) {
                std::cerr << "Ignored frame 0x" << std::hex << frame.id << std::dec
                          << ": " << ecu::describe(decoded.error) << '\n';
                continue;
            }
            const auto& status = decoded.status;
            std::cout << "RPM: " << status.rpm
                      << " | Coolant: " << status.coolantTemperature << " C"
                      << " | Throttle: " << static_cast<unsigned>(status.throttlePosition)
                      << '%' << std::endl;
        }
        std::cout << "Dashboard stopped.\n";
    } catch (const std::exception& error) {
        std::cerr << "dashboard_ecu: " << error.what()
                  << "\nCheck that the CAN interface exists and is up (see README).\n";
        return 1;
    }
}
