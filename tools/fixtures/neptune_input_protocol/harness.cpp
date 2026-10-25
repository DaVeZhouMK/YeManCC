#include "neptune_input_protocol.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ymcc::neptune_input;
static unsigned checks = 0, failures = 0;
static void check(bool value, const std::string& label) {
    ++checks;
    if (!value) { ++failures; if (failures < 25) std::cerr << "FAIL " << label << '\n'; }
}
static std::vector<std::uint8_t> unhex(const std::string& hex) {
    std::vector<std::uint8_t> b;
    for (std::size_t i = 0; i < hex.size(); i += 2)
        b.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    return b;
}
static bool empty(const Result& r) {
    return r.input.seq == 0 && r.input.allButtons == 0 &&
        std::all_of(r.input.axes.begin(), r.input.axes.end(), [](auto a) {return a == 0;});
}
static void pads(const Input& in, const std::string& id) {
    const auto l = in.leftPad(), r = in.rightPad();
    check(l.touch == in.button(Button::BtnLPadTouch) && l.press == in.button(Button::BtnLPadPress) &&
        l.x == in.axis(Axis::LeftPadX) && l.y == in.axis(Axis::LeftPadY) &&
        l.pressure == in.axis(Axis::LeftPadPressure), id + ":leftPad");
    check(r.touch == in.button(Button::BtnRPadTouch) && r.press == in.button(Button::BtnRPadPress) &&
        r.x == in.axis(Axis::RightPadX) && r.y == in.axis(Axis::RightPadY) &&
        r.pressure == in.axis(Axis::RightPadPressure), id + ":rightPad");
    const auto ls = in.leftStick(), rs = in.rightStick();
    check(ls.touch == in.button(Button::BtnLStickTouch) && ls.press == in.button(Button::BtnLStickPress) &&
        ls.x == in.axis(Axis::LeftStickX) && ls.y == in.axis(Axis::LeftStickY), id + ":leftStick");
    check(rs.touch == in.button(Button::BtnRStickTouch) && rs.press == in.button(Button::BtnRStickPress) &&
        rs.x == in.axis(Axis::RightStickX) && rs.y == in.axis(Axis::RightStickY), id + ":rightStick");
    check(in.leftTrigger() == in.axis(Axis::L2) && in.rightTrigger() == in.axis(Axis::R2), id + ":triggers");
    const auto g = in.rawGyro(); const auto a = in.rawAccel();
    check(g.pitch == in.axis(Axis::GyroPitch) && g.yaw == in.axis(Axis::GyroYaw) &&
        g.roll == in.axis(Axis::GyroRoll), id + ":rawGyro");
    check(a.x == in.axis(Axis::GyroAccelX) && a.y == in.axis(Axis::GyroAccelY) &&
        a.z == in.axis(Axis::GyroAccelZ), id + ":rawAccel");
    check(!in.button(static_cast<Button>(0)) && !in.button(static_cast<Button>(255)) &&
        in.axis(static_cast<Axis>(0)) == 0 && in.axis(static_cast<Axis>(255)) == 0, id + ":enumBounds");
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream file(argv[1]); if (!file) return 3;
    std::string line; unsigned packets = 0;
    while (std::getline(file, line)) {
        std::istringstream row(line); std::string id, hex;
        std::uint32_t seq = 0, buttons = 0;
        row >> id >> hex >> seq >> buttons;
        std::array<int, 22> axes{}; for (auto& a : axes) row >> a;
        if (!row) return 4;
        const auto bytes = unhex(hex);
        const auto result = decode(bytes.data(), bytes.size(), bytes.size(), BufferKind::LogicalNCInput);
        check(static_cast<bool>(result), id + ":accepted");
        check(result.input.seq == seq, id + ":seq");
        check(result.input.allButtons == buttons, id + ":buttons");
        for (std::size_t i = 0; i < axes.size(); ++i)
            check(result.input.axes[i] == axes[i], id + ":axis" + std::to_string(i + 1));
        for (unsigned i = 1; i <= 28; ++i)
            check(result.input.button(static_cast<Button>(i)) == ((buttons & (1u << (i - 1))) != 0),
                id + ":button" + std::to_string(i));
        pads(result.input, id);
        ++packets;
    }
    // Independently allocated short buffers catch overreads in Checked and bounds runs.
    for (std::size_t n = 0; n < 60; ++n) {
        std::vector<std::uint8_t> shortPacket(n, 0xff);
        if (n > 0) shortPacket[0] = 1;
        if (n > 1) shortPacket[1] = 0;
        if (n > 2) shortPacket[2] = 9;
        const std::uint8_t dummy = 0;
        const auto r = decode(n == 0 ? &dummy : shortPacket.data(), n, n, BufferKind::LogicalNCInput);
        check(r.error == Error::ShortPacket && empty(r), "exact-short-" + std::to_string(n));
    }
    std::vector<std::uint8_t> buffer(80, 0xff);
    buffer[0] = 1; buffer[1] = 0; buffer[2] = 9;
    check(!decode(buffer.data(), 80, 65) && empty(decode(buffer.data(), 80, 65)), "unverified-default");
    check(!decode(buffer.data(), 80, 65, static_cast<BufferKind>(255)), "unverified-enum");
    check(decode(nullptr, 80, 65, BufferKind::LogicalNCInput).error == Error::NullBuffer, "null");
    check(decode(buffer.data(), 59, 60, BufferKind::LogicalNCInput).error == Error::InvalidActualBytes,
        "actual-exceeds-capacity");
    check(decode(buffer.data(), 80, static_cast<std::size_t>(-1), BufferKind::LogicalNCInput).error ==
        Error::InvalidActualBytes, "actual-overflow");
    const auto core = decode(buffer.data(), 80, 60, BufferKind::LogicalNCInput);
    for (std::size_t n = 60; n <= 80; ++n) {
        const auto r = decode(buffer.data(), 80, n, BufferKind::LogicalNCInput);
        check(static_cast<bool>(r) && r.input.seq == core.input.seq &&
            r.input.allButtons == core.input.allButtons && r.input.axes == core.input.axes,
            "opaque-logical-length-" + std::to_string(n));
    }
    const auto full = decode(buffer.data(), 80, 65, BufferKind::LogicalNCInput);
    check(static_cast<bool>(full) && full.input.allButtons != 0, "stale-setup");
    for (std::size_t n = 0; n < 60; ++n) {
        const auto r = decode(buffer.data(), 80, n, BufferKind::LogicalNCInput);
        check(r.error == Error::ShortPacket && empty(r), "poisoned-capacity-short-" + std::to_string(n));
    }
    for (unsigned offset = 0; offset < 3; ++offset) {
        const auto original = buffer[offset];
        for (unsigned value = 0; value < 256; ++value) {
            buffer[offset] = static_cast<std::uint8_t>(value);
            const auto r = decode(buffer.data(), 80, 65, BufferKind::LogicalNCInput);
            check(static_cast<bool>(r) == (value == original), "strict-header");
            if (value != original) check(empty(r), "header-empty");
        }
        buffer[offset] = original;
    }
    // Synthetic prepended data is refused. This is not an asserted Windows raw-frame contract.
    std::vector<std::uint8_t> prefixed(65, 0); prefixed[1] = 1; prefixed[3] = 9;
    check(decode(prefixed.data(), 65, 65, BufferKind::LogicalNCInput).error == Error::InvalidHeader,
        "no-prefix-inference");
    // Fourth header byte and bytes beyond NCInput are opaque in the selected sources.
    for (unsigned value = 0; value < 256; ++value) {
        buffer[3] = static_cast<std::uint8_t>(value);
        for (std::size_t i = 60; i < 80; ++i) buffer[i] = static_cast<std::uint8_t>(value);
        const auto r = decode(buffer.data(), 80, 65, BufferKind::LogicalNCInput);
        check(static_cast<bool>(r) && r.input.seq == full.input.seq && r.input.allButtons == full.input.allButtons &&
            r.input.axes == full.input.axes, "opaque-a3-and-tail");
    }
    std::cout << "SUMMARY packets=" << packets << " checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
