#include "common/can/CanProtocol.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    // Unlike assert(), these checks still execute in Release builds.
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ecu::CanFrame exampleFrame() {
    // Literal wire fixture: 2500 rpm, 91 C, 35% throttle.
    return {0x100, 5, {0x09, 0xc4, 0x00, 0x5b, 0x23, 0, 0, 0}};
}

void exactWireBytes() {
    const auto frame = ecu::serialize({2500, 91, 35});
    require(frame.has_value(), "valid status rejected");
    require(frame->id == 0x100 && frame->length == 5, "wrong ID or payload length");
    require(frame->data == exampleFrame().data, "positive wire encoding differs from specification");
    require(!frame->extended && !frame->remote && !frame->error, "wrong frame type");

    const auto cold = ecu::serialize({0, -40, 0});
    const std::array<std::uint8_t, 8> coldBytes{0, 0, 0xff, 0xd8, 0, 0, 0, 0};
    require(cold && cold->data == coldBytes, "negative temperature wire encoding is incorrect");
    const auto hot = ecu::serialize({12000, 215, 100});
    const std::array<std::uint8_t, 8> hotBytes{0x2e, 0xe0, 0, 0xd7, 0x64, 0, 0, 0};
    require(hot && hot->data == hotBytes, "upper boundary wire encoding is incorrect");
}

void decodeWireFixtures() {
    auto frame = exampleFrame();
    const auto decoded = ecu::deserializeEngineStatus(frame);
    require(decoded.error == ecu::DecodeError::None, "valid wire fixture rejected");
    require(decoded.status.rpm == 2500 && decoded.status.coolantTemperature == 91 &&
            decoded.status.throttlePosition == 35, "wire fixture decoded incorrectly");
    frame.data[2] = 0xff;
    frame.data[3] = 0xd8;
    require(ecu::deserializeEngineStatus(frame).status.coolantTemperature == -40,
            "negative temperature sign extension failed");
    // Bytes outside the declared payload are not transmitted and carry no meaning.
    frame.data[5] = 0xff;
    frame.data[6] = 0x80;
    frame.data[7] = 0xaa;
    require(ecu::deserializeEngineStatus(frame).error == ecu::DecodeError::None,
            "unused storage bytes affected decoding");
}

void roundTrips() {
    for (const auto rpm : {0, 1, 255, 256, 2500, 12000}) {
        for (int temperature = -40; temperature <= 215; ++temperature) {
            for (const auto throttle : {0, 35, 100}) {
                const ecu::EngineStatus status{static_cast<std::uint16_t>(rpm),
                    static_cast<std::int16_t>(temperature), static_cast<std::uint8_t>(throttle)};
                const auto frame = ecu::serialize(status);
                require(frame.has_value(), "in-range status rejected");
                const auto decoded = ecu::deserializeEngineStatus(*frame);
                require(decoded.error == ecu::DecodeError::None, "round-trip decode failed");
                require(decoded.status.rpm == status.rpm &&
                        decoded.status.coolantTemperature == status.coolantTemperature &&
                        decoded.status.throttlePosition == status.throttlePosition,
                        "round trip changed telemetry");
            }
        }
    }
}

void invalidApplicationValues() {
    const ecu::EngineStatus invalid[] = {
        {12001, 91, 35}, {65535, 91, 35}, {2500, -41, 35}, {2500, 216, 35},
        {2500, -32768, 35}, {2500, 32767, 35}, {2500, 91, 101}, {2500, 91, 255}};
    for (const auto& status : invalid) {
        require(!ecu::serialize(status), "out-of-range application value accepted");
    }
}

void invalidPayloadLengths() {
    auto frame = exampleFrame();
    for (unsigned length = 0; length <= 255; ++length) {
        if (length == 5) {
            continue;
        }
        frame.length = static_cast<std::uint8_t>(length);
        require(ecu::deserializeEngineStatus(frame).error == ecu::DecodeError::WrongLength,
                "invalid payload length accepted");
    }
}

void invalidIdentifiersAndTypes() {
    for (const auto id : {0u, 0x101u, 0x7ffu, 0x800u, 0x80000100u}) {
        auto frame = exampleFrame();
        frame.id = id;
        require(ecu::deserializeEngineStatus(frame).error == ecu::DecodeError::WrongId,
                "incorrect identifier accepted");
    }
    for (unsigned flags = 1; flags <= 7; ++flags) {
        auto frame = exampleFrame();
        frame.extended = (flags & 1) != 0;
        frame.remote = (flags & 2) != 0;
        frame.error = (flags & 4) != 0;
        require(ecu::deserializeEngineStatus(frame).error == ecu::DecodeError::UnsupportedFrame,
                "unsupported frame type accepted");
    }
}

void invalidWireValues() {
    const std::array<std::uint8_t, 8> invalid[] = {
        {0x2e, 0xe1, 0, 91, 35},       // 12,001 rpm
        {0xff, 0xff, 0, 91, 35},       // 65,535 rpm
        {0x09, 0xc4, 0xff, 0xd7, 35},  // -41 C
        {0x09, 0xc4, 0, 0xd8, 35},     // 216 C
        {0x09, 0xc4, 0x80, 0, 35},     // -32,768 C
        {0x09, 0xc4, 0x7f, 0xff, 35},  // 32,767 C
        {0x09, 0xc4, 0, 91, 101},
        {0x09, 0xc4, 0, 91, 255}};
    for (const auto& payload : invalid) {
        auto frame = exampleFrame();
        frame.data = payload;
        require(ecu::deserializeEngineStatus(frame).error == ecu::DecodeError::OutOfRange,
                "invalid wire value accepted");
    }
}

} // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"exact wire bytes", exactWireBytes},
        {"decode independent wire fixtures", decodeWireFixtures},
        {"round trips and temperature boundaries", roundTrips},
        {"invalid application values", invalidApplicationValues},
        {"invalid payload lengths", invalidPayloadLengths},
        {"invalid identifiers and frame types", invalidIdentifiersAndTypes},
        {"invalid wire values", invalidWireValues}};
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
