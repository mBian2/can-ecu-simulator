# Testing

## Protocol tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CTest runs a dependency-free C++ test executable with seven groups:

- Exact wire bytes for typical, negative-temperature, and upper-boundary messages.
- Decoding independently specified wire fixtures.
- 4,608 round trips spanning every supported integer temperature.
- Rejection of out-of-range application values before serialization.
- Rejection of all 255 incorrect byte-sized payload lengths.
- Rejection of incorrect IDs and extended, remote, or error frame flags.
- Rejection of out-of-range values received on the wire.

The checks remain enabled in Release builds and run without a CAN interface.
Windows builds test this portable code; they do not build the Linux driver.

## SocketCAN integration

On Linux, first build both applications and create a dedicated `vcan0` interface
using the [README instructions](../README.md#set-up-a-virtual-can-interface).
Stop other senders on that interface before running:

```bash
python3 tests/socketcan_integration.py --build-dir build --interface vcan0
```

The script uses only Python's standard library. It checks both CLI help commands,
missing-interface errors, dashboard behavior on an idle bus, changing engine
telemetry, rejection of malformed frames, valid telemetry after those frames,
and clean Ctrl+C shutdown. It starts and cleans up its own application processes;
it does not create or delete network interfaces. Failures exit unsuccessfully.

## Continuous integration

[Build and test](https://github.com/mBian2/can-ecu-simulator/actions/workflows/build.yml) runs on pull requests to
`main` and pushes to `main`. Linux builds both applications and runs the protocol
and vcan integration tests. Windows builds and tests the portable protocol code.
CI results belong to the commit tested; check the PR's latest run before merging.

These tests do not validate physical CAN timing, arbitration, electrical faults,
MCU behavior, or hard real-time performance. The README's example output is
illustrative, not a benchmark.
