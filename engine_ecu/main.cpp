#include "common/can/CanProtocol.hpp"
#include "engine_ecu/DemoScenario.hpp"
#include "engine_ecu/EngineController.hpp"
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
        std::cout << "Runs a repeating 12-second ignition/throttle demonstration.\n";
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
        constexpr auto updatePeriod = std::chrono::milliseconds(20);
        constexpr auto transmitPeriod = std::chrono::milliseconds(100);
        const auto started = Clock::now();
        auto nextSend = started;
        auto simulationTime = std::chrono::milliseconds::zero();
        ecu::EngineController controller;
        std::cout << "State: " << ecu::describe(controller.state()) << std::endl;

        while (!stopRequested) {
            const auto wallElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - started);
            // Bound catch-up work to 50 steps. A long stall is reported rather
            // than silently dropping model time or racing through a backlog.
            if (wallElapsed - simulationTime > ecu::EngineParameters::maximumUpdate) {
                throw std::runtime_error("simulation fell more than one second behind its clock");
            }
            while (simulationTime + updatePeriod <= wallElapsed) {
                const auto previous = controller.state();
                if (controller.update(ecu::demoInputs(simulationTime), updatePeriod) !=
                    ecu::UpdateError::None) {
                    throw std::runtime_error("invalid engine simulation update");
                }
                simulationTime += updatePeriod;
                if (controller.state() != previous) {
                    std::cout << "State: " << ecu::describe(previous) << " -> "
                              << ecu::describe(controller.state()) << std::endl;
                }
            }
            const auto now = Clock::now();
            if (now >= nextSend) {
                const auto frame = ecu::serialize(controller.telemetry());
                if (!frame) {
                    throw std::runtime_error("simulation produced invalid telemetry");
                }
                if (const auto error = driver.send(*frame)) {
                    if (stopRequested && error == std::errc::interrupted) {
                        break;
                    }
                    throw std::system_error(error, "send engine status on " + interfaceName);
                }
                // Send the current sample once, even when several slots were missed.
                nextSend += transmitPeriod * ((now - nextSend) / transmitPeriod + 1);
            }
            std::this_thread::sleep_until(started + simulationTime + updatePeriod);
        }
        std::cout << "Engine stopped.\n";
    } catch (const std::exception& error) {
        std::cerr << "engine_ecu: " << error.what() << '\n';
        return 1;
    }
}
