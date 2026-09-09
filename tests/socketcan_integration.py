#!/usr/bin/env python3
"""Exercise both applications on an existing, dedicated Linux vcan interface."""

import argparse
import contextlib
import pathlib
import signal
import socket
import struct
import subprocess
import tempfile
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def wait_for_output(process, log_path, expected):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        output = log_path.read_text()
        if expected in output:
            return
        require(process.poll() is None, f"Process exited before {expected!r}:\n{output}")
        time.sleep(0.02)
    raise RuntimeError(f"Timed out waiting for {expected!r}:\n{log_path.read_text()}")


@contextlib.contextmanager
def running(executable, interface, log_path):
    with log_path.open("w") as output:
        process = subprocess.Popen(
            [str(executable), interface], stdout=output, stderr=subprocess.STDOUT
        )
        try:
            yield process
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)


def stop(process, log_path):
    process.send_signal(signal.SIGINT)
    require(process.wait(timeout=3) == 0, f"Unclean shutdown:\n{log_path.read_text()}")


def observe_lifecycle(bus):
    # Check successive wire observations, not a change in the first 600 ms:
    # the demonstration intentionally begins with the engine switched off.
    stages = [
        ("off", lambda rpm, throttle: rpm == 0 and throttle == 0),
        ("starting", lambda rpm, throttle: 0 < rpm < 800 and throttle == 0),
        ("idle", lambda rpm, throttle: rpm == 800 and throttle == 0),
        ("running", lambda rpm, throttle: rpm >= 3000 and throttle == 60),
        ("decelerating", lambda rpm, throttle: 800 < rpm < 3500 and throttle == 0),
        ("idle again", lambda rpm, throttle: rpm == 800 and throttle == 0),
        ("off again", lambda rpm, throttle: rpm == 0 and throttle == 0),
    ]
    next_stage = 0
    temperatures = []
    deadline = time.monotonic() + 20
    while next_stage < len(stages) and time.monotonic() < deadline:
        # Linux can_frame uses native byte order for its header. EngineStatus
        # fields have their own explicitly big-endian wire representation.
        bus.settimeout(min(3, max(0.001, deadline - time.monotonic())))
        try:
            raw_frame = bus.recv(16)
        except TimeoutError as error:
            raise RuntimeError(f"No telemetry while waiting for {stages[next_stage][0]}") from error
        identifier, length, payload = struct.unpack("=IB3x8s", raw_frame)
        require(identifier == 0x100 and length == 5, "Unexpected engine frame")
        rpm, temperature, throttle = struct.unpack(">HhB", payload[:5])
        require(0 <= rpm <= 12000 and -40 <= temperature <= 215 and
                0 <= throttle <= 100, "Invalid engine telemetry")
        temperatures.append(temperature)
        if stages[next_stage][1](rpm, throttle):
            next_stage += 1
    require(next_stage == len(stages),
            f"Incomplete engine lifecycle; observed {next_stage} of {len(stages)} stages")
    require(max(temperatures) > temperatures[0], "Engine did not warm during its lifecycle")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, default=pathlib.Path("build"))
    parser.add_argument("--interface", default="vcan0")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    engine = build / "engine_ecu"
    dashboard = build / "dashboard_ecu"

    # Use a name confirmed absent rather than relying on the host's configuration.
    missing_interface = "ecu_missing"
    require(missing_interface not in dict(socket.if_nameindex()).values(),
            "Test interface ecu_missing unexpectedly exists")
    for executable in (engine, dashboard):
        help_result = subprocess.run([str(executable), "--help"], capture_output=True,
                                     text=True, timeout=5)
        require(help_result.returncode == 0 and "Usage:" in help_result.stdout,
                f"Help command failed: {executable.name}")
        missing = subprocess.run([str(executable), missing_interface], capture_output=True,
                                 text=True, timeout=5)
        require(missing.returncode != 0 and "find CAN interface" in missing.stderr,
                f"Missing interface was not reported: {executable.name}")

    with tempfile.TemporaryDirectory() as directory:
        logs = pathlib.Path(directory)
        dashboard_log = logs / "dashboard.log"
        engine_log = logs / "engine.log"

        with running(dashboard, args.interface, dashboard_log) as receiver:
            wait_for_output(receiver, dashboard_log, "Dashboard listening")
            # Leave the bus idle long enough to exercise several receive timeouts.
            time.sleep(0.35)
            require(receiver.poll() is None, "Dashboard exited on an idle bus")
            stop(receiver, dashboard_log)

        with socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW) as bus:
            bus.bind((args.interface,))
            bus.settimeout(3)
            with running(dashboard, args.interface, dashboard_log) as receiver:
                wait_for_output(receiver, dashboard_log, "Dashboard listening")
                with running(engine, args.interface, engine_log) as sender:
                    wait_for_output(sender, engine_log, "Engine transmitting")
                    observe_lifecycle(bus)
                    wait_for_output(receiver, dashboard_log, "RPM: 3500")
                    stop(sender, engine_log)
                    transitions = [line for line in engine_log.read_text().splitlines()
                                   if line.startswith("State:")]
                    require(transitions == [
                        "State: Off", "State: Off -> Starting", "State: Starting -> Idle",
                        "State: Idle -> Running", "State: Running -> Idle", "State: Idle -> Off",
                    ], f"Missing, repeated, or out-of-order state logs: {transitions}")

                fixtures = [
                    (0x100, "09C4005B", "requires exactly 5 bytes"),
                    (0x100, "09C4005B65", "outside protocol range"),
                    (0x101, "09C4005B23", "unexpected CAN identifier"),
                    (0x80000100, "09C4005B23", "expected a standard CAN data frame"),
                    (0x40000100, "", "Ignored frame 0x100"),
                    (0x100, "0000FFD800", "RPM: 0 | Coolant: -40 C | Throttle: 0%"),
                    (0x100, "2EE000D764", "RPM: 12000 | Coolant: 215 C | Throttle: 100%"),
                ]
                for identifier, hex_payload, expected in fixtures:
                    payload = bytes.fromhex(hex_payload)
                    bus.send(struct.pack("=IB3x8s", identifier, len(payload), payload))
                    wait_for_output(receiver, dashboard_log, expected)
                stop(receiver, dashboard_log)
                output = dashboard_log.read_text()
                require(output.count("Ignored frame") == 5,
                        f"Dashboard did not reject all five malformed frames:\n{output}")

    print("PASS: help, missing interfaces, idle receive, engine lifecycle, malformed frames, and Ctrl+C")


if __name__ == "__main__":
    main()
