#include "engine_ecu/DemoScenario.hpp"
#include "engine_ecu/EngineController.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void near(double actual, double expected, const char* message) {
    // Telemetry is rounded to whole units, so allow one unit at that boundary.
    require(std::abs(actual - expected) <= 1.0, message);
}

void advance(ecu::EngineController& engine, ecu::EngineInputs inputs,
             std::chrono::milliseconds elapsed) {
    require(engine.update(inputs, elapsed) == ecu::UpdateError::None, "valid update rejected");
}

bool same(const ecu::EngineController& first, const ecu::EngineController& second) {
    const auto a = first.telemetry();
    const auto b = second.telemetry();
    return first.state() == second.state() && a.rpm == b.rpm &&
        a.coolantTemperature == b.coolantTemperature && a.throttlePosition == b.throttlePosition;
}

void initialAndZeroTime() {
    ecu::EngineController engine;
    require(engine.state() == ecu::EngineState::Off, "engine did not initialize off");
    const auto status = engine.telemetry();
    require(status.rpm == 0 && status.coolantTemperature == 20 && status.throttlePosition == 0,
            "incorrect initial telemetry");
    advance(engine, {true, 100}, 0ms);
    require(engine.state() == ecu::EngineState::Off, "zero time applied ignition input");
    advance(engine, {true, 0}, 1000ms);
    const auto before = engine;
    advance(engine, {false, 0}, 0ms);
    require(same(engine, before), "zero time changed a running controller");
}

void startupBoundary() {
    ecu::EngineController engine;
    advance(engine, {true, 0}, 999ms);
    require(engine.state() == ecu::EngineState::Starting, "startup completed early");
    near(engine.telemetry().rpm, 799.2, "starter did not ramp RPM");
    advance(engine, {true, 0}, 1ms);
    require(engine.state() == ecu::EngineState::Idle, "startup did not finish at one second");
    require(engine.telemetry().rpm == 800, "startup did not reach idle speed");
}

void throttleDuringStartup() {
    ecu::EngineController engine;
    advance(engine, {true, 100}, 500ms);
    require(engine.state() == ecu::EngineState::Starting, "throttle bypassed startup");
    require(engine.telemetry().rpm == 400 && engine.telemetry().throttlePosition == 0,
            "startup applied throttle before completion");
    advance(engine, {true, 100}, 750ms);
    require(engine.state() == ecu::EngineState::Running, "startup did not apply current demand");
    require(engine.telemetry().throttlePosition == 100, "completed startup lost throttle input");
    near(engine.telemetry().rpm, 1175, "startup crossing used the wrong acceleration duration");
}

void stopAndRestart() {
    for (const auto throttle : {0, 100}) {
        ecu::EngineController engine;
        advance(engine, {true, throttle}, 400ms);
        advance(engine, {false, throttle}, 20ms);
        require(engine.state() == ecu::EngineState::Off && engine.telemetry().rpm == 0 &&
                engine.telemetry().throttlePosition == 0, "ignition-off did not cancel startup");
        advance(engine, {true, throttle}, 600ms);
        require(engine.state() == ecu::EngineState::Starting, "restart reused old startup progress");
        advance(engine, {true, throttle}, 400ms);
        advance(engine, {true, throttle}, 1000ms);
        advance(engine, {false, throttle}, 1ms);
        require(engine.state() == ecu::EngineState::Off && engine.telemetry().rpm == 0 &&
                engine.telemetry().throttlePosition == 0, "ignition-off did not stop active engine");
    }
}

void accelerationAndRelease() {
    ecu::EngineController engine;
    advance(engine, {true, 0}, 1000ms);
    advance(engine, {true, 100}, 100ms);
    require(engine.state() == ecu::EngineState::Running, "throttle did not enter running state");
    near(engine.telemetry().rpm, 950, "RPM jumped instead of accelerating at the bounded rate");
    for (int second = 0; second < 4; ++second) {
        advance(engine, {true, 100}, 1000ms);
    }
    require(engine.telemetry().rpm == 5300, "RPM overshot or failed to reach full throttle target");
    advance(engine, {true, 0}, 100ms);
    require(engine.state() == ecu::EngineState::Idle, "release did not enter idle mode");
    near(engine.telemetry().rpm, 5100, "throttle release did not decelerate gradually");
    for (int second = 0; second < 3; ++second) {
        advance(engine, {true, 0}, 1000ms);
    }
    require(engine.telemetry().rpm == 800, "RPM failed to settle at idle");
}

void thermalProgressAndLimits() {
    ecu::EngineController engine;
    advance(engine, {true, 0}, 1000ms);
    require(engine.telemetry().coolantTemperature == 22, "startup heating is not gradual");
    for (int second = 0; second < 50; ++second) {
        advance(engine, {true, 100}, 1000ms);
    }
    require(engine.telemetry().coolantTemperature == 100, "temperature overshot operating target");
    advance(engine, {true, 0}, 1000ms);
    require(engine.telemetry().coolantTemperature == 99, "idle cooldown was not gradual");
    advance(engine, {false, 0}, 1000ms);
    require(engine.telemetry().coolantTemperature == 98, "stopping reset temperature to ambient");
    for (int second = 0; second < 100; ++second) {
        advance(engine, {false, 0}, 1000ms);
    }
    require(engine.telemetry().coolantTemperature == 20, "cooling crossed the ambient limit");
}

void smallStepsAccumulate() {
    ecu::EngineController fine;
    ecu::EngineController coarse;
    for (int step = 0; step < 5000; ++step) {
        advance(fine, {true, 50}, 1ms);
    }
    for (int step = 0; step < 50; ++step) {
        advance(coarse, {true, 50}, 100ms);
    }
    require(fine.state() == ecu::EngineState::Running, "small steps did not complete startup");
    near(fine.telemetry().rpm, 3050, "fractional RPM progress was lost");
    near(fine.telemetry().coolantTemperature, 30, "fractional temperature progress was lost");
    near(fine.telemetry().rpm, coarse.telemetry().rpm, "update size changed steady demand RPM");
    near(fine.telemetry().coolantTemperature, coarse.telemetry().coolantTemperature,
         "update size changed steady demand temperature");
}

void invalidUpdatesAreAtomic() {
    struct InvalidUpdate {
        ecu::EngineInputs inputs;
        std::chrono::milliseconds elapsed;
        ecu::UpdateError error;
    };
    const InvalidUpdate invalid[] = {
        {{false, -1}, 20ms, ecu::UpdateError::InvalidThrottle},
        {{true, 101}, 20ms, ecu::UpdateError::InvalidThrottle},
        {{true, std::numeric_limits<int>::max()}, 0ms, ecu::UpdateError::InvalidThrottle},
        {{false, 0}, -1ms, ecu::UpdateError::InvalidElapsedTime},
        {{true, 100}, 1001ms, ecu::UpdateError::InvalidElapsedTime},
        {{true, 0}, std::chrono::milliseconds::max(), ecu::UpdateError::InvalidElapsedTime}};
    for (const auto& update : invalid) {
        ecu::EngineController engine;
        advance(engine, {true, 0}, 500ms);
        auto unchanged = engine;
        require(engine.update(update.inputs, update.elapsed) == update.error,
                "invalid update did not report the expected error");
        require(same(engine, unchanged), "invalid update changed visible state");
        // Check hidden startup time and fractional progress through later behavior.
        advance(engine, {true, 100}, 600ms);
        advance(unchanged, {true, 100}, 600ms);
        require(same(engine, unchanged), "invalid update changed subsequent behavior");
    }
}

void repeatableDemoAndProtocol() {
    ecu::EngineController first;
    ecu::EngineController second;
    std::vector<ecu::EngineState> transitions{first.state()};
    for (auto time = 0ms; time < 12000ms; time += 20ms) {
        const auto inputs = ecu::demoInputs(time);
        const auto previous = first.state();
        advance(first, inputs, 20ms);
        advance(second, inputs, 20ms);
        require(same(first, second), "identical input sequences gave different results");
        if (first.state() != previous) {
            transitions.push_back(first.state());
        }
        const auto frame = ecu::serialize(first.telemetry());
        require(frame && frame->id == 0x100 && frame->length == 5,
                "controller output broke protocol limits or layout");
        const auto decoded = ecu::deserializeEngineStatus(*frame);
        require(decoded.error == ecu::DecodeError::None &&
                decoded.status.rpm == first.telemetry().rpm &&
                decoded.status.coolantTemperature == first.telemetry().coolantTemperature &&
                decoded.status.throttlePosition == first.telemetry().throttlePosition,
                "controller telemetry did not round-trip through the existing protocol");
        const auto repeat = ecu::demoInputs(time + 12000ms);
        require(inputs.ignition == repeat.ignition && inputs.throttlePercent == repeat.throttlePercent,
                "demo inputs did not repeat every twelve seconds");
    }
    const std::vector<ecu::EngineState> expected{
        ecu::EngineState::Off, ecu::EngineState::Starting, ecu::EngineState::Idle,
        ecu::EngineState::Running, ecu::EngineState::Idle, ecu::EngineState::Off};
    require(transitions == expected, "demo lifecycle transitions were missing or out of order");
}

} // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"initial state and zero elapsed time", initialAndZeroTime},
        {"startup boundary", startupBoundary},
        {"throttle during startup and boundary crossing", throttleDuringStartup},
        {"stop and restart from active states", stopAndRestart},
        {"bounded acceleration and throttle release", accelerationAndRelease},
        {"gradual temperature changes and limits", thermalProgressAndLimits},
        {"small update intervals preserve progress", smallStepsAccumulate},
        {"invalid updates leave the model unchanged", invalidUpdatesAreAtomic},
        {"repeatable demo and protocol compatibility", repeatableDemoAndProtocol}};
    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "PASS: " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
