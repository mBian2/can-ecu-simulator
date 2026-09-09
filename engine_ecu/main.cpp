#include "common/can/CanProtocol.hpp"
#include "platform/linux/SocketCanDriver.hpp"

#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t stopRequested = 0;

void requestStop(int) {
    // Signal handlers must not log, allocate, or operate the socket.
    stopRequested = 1;
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc > 2 || (argc == 2 && std::string(argv[1]) == "--help")) {
        std::cout << "Usage: engine_ecu [CAN interface, default: vcan0]\n";
        return argc > 2 ? 1 : 0;
    }
    const std::string interfaceName = argc == 2 ? argv[1] : "vcan0";
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);

    try {
        ecu::SocketCanDriver driver(interfaceName);
        std::cout << "Engine transmitting on " << interfaceName
                  << " every 100 ms. Ctrl+C to stop." << std::endl;
        using Clock = std::chrono::steady_clock;
        constexpr auto period = std::chrono::milliseconds(100);
        const auto started = Clock::now();
        auto nextSend = started;

        while (!stopRequested) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
            // A repeating 20-second throttle sweep gives reproducible traffic.
            // This is a telemetry demo, not an engine physics model.
            const int phase = static_cast<int>((elapsed / 100) % 200);
            const int throttle = phase <= 100 ? phase : 200 - phase;
            const ecu::EngineStatus status{
                static_cast<std::uint16_t>(800 + throttle * 45),
                static_cast<std::int16_t>(80 + throttle / 5),
                static_cast<std::uint8_t>(throttle)};
            const auto frame = ecu::serialize(status);
            if (!frame) {
                throw std::runtime_error("simulation produced invalid telemetry");
            }
            if (const auto error = driver.send(*frame)) {
                if (stopRequested && error == std::errc::interrupted) {
                    break;
                }
                throw std::system_error(error, "send engine status on " + interfaceName);
            }
            nextSend += period;
            const auto now = Clock::now();
            // Skip missed slots instead of sending a burst after a long stall.
            if (nextSend <= now) {
                nextSend += period * ((now - nextSend) / period + 1);
            }
            std::this_thread::sleep_until(nextSend);
        }
        std::cout << "Engine stopped.\n";
    } catch (const std::exception& error) {
        std::cerr << "engine_ecu: " << error.what()
                  << "\nCheck that the CAN interface exists and is up (see README).\n";
        return 1;
    }
}
