import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {parseArgs} from 'node:util';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';

// No YMCC process, hardware, IPC transport or product file is opened for writing.
// Every case gets a fresh fixture process so production function-local statics
// are initialized naturally, without inventing a reset API in the consumer.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const {values, positionals} = parseArgs({allowPositionals: true, options: {
  out: {type: 'string'},
  vcvars: {type: 'string'},
  help: {type: 'boolean', short: 'h'},
}});
if (values.help) {
  console.log('Usage: node tools/performance_slider_native_repeat_selftest.mjs [--out <evidence-directory>] [--vcvars <vcvars64.bat>]');
  process.exit(0);
}
if (positionals.length > 1 || (values.out && positionals.length)) throw Error('Supply one evidence output directory');
const out = path.resolve(values.out || positionals[0] || path.join(root, '../../../Build/Validation/Performance-Slider-Cadence-20261021/native'));
const vcvars = values.vcvars || 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
const mainPath = path.join(root, 'native/main.cpp');
const source = fs.readFileSync(mainPath, 'utf8');
const sha256 = text => createHash('sha256').update(text).digest('hex');
fs.mkdirSync(out, {recursive: true});

function extract(text) {
  const sections = [];
  function between(name, start, end) {
    const begin = text.indexOf(start);
    const finish = text.indexOf(end, begin + start.length);
    assert(begin >= 0 && finish > begin, `Missing production boundary: ${name}`);
    assert.equal(text.indexOf(start, begin + start.length), -1, `Ambiguous production boundary: ${name}`);
    const code = text.slice(begin, finish);
    sections.push({name, startLine: text.slice(0, begin).split('\n').length,
      endLine: text.slice(0, finish).split('\n').length - 1, sha256: sha256(code), code});
  }
  for (const name of ['g_prevW', 'g_curPad', 'g_gamepadAxisNeutralAdmissionRequired']) {
    const matches = [...text.matchAll(new RegExp(`^static [^\\r\\n;]*\\b${name}\\b[^\\r\\n]*`, 'gm'))];
    assert.equal(matches.length, 1, `Missing or ambiguous production declaration: ${name}`);
    const match = matches[0];
    sections.push({name, startLine: text.slice(0, match.index).split('\n').length,
      endLine: text.slice(0, match.index).split('\n').length, sha256: sha256(match[0]), code: match[0]});
  }
  between('ui-state-and-session', 'static WORD g_uiShoulderPending = 0;', 'static void writeLog(const std::string& level, const std::string& msg);');
  between('native-ui-consumer', 'static bool gamepadUiInputEligible() {', 'static uint64_t gamepadShortcutRecordingMask(');
  between('native-ui-reset', '\n    g_uiShoulderPending = 0;', '    if (g_gpHoldTimerOn && g_hwnd) {');
  between('ui-edit-ipc-consumer', '        if (req.value("event", std::string{}) == "gamepad.fan-node-edit") {', '        const auto generation = fromChild ? 0 : g_webviewGeneration.load(');
  return sections;
}
const sections = extract(source);
const sectionCode = name => sections.find(section => section.name === name).code;
fs.writeFileSync(path.join(out, 'native-main.snapshot.cpp'), source);

const preamble = String.raw`
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
using WORD = std::uint16_t;
using BYTE = std::uint8_t;
using SHORT = std::int16_t;
using ULONGLONG = std::uint64_t;
using HWND = std::uintptr_t;
struct FILETIME { std::uint32_t dwLowDateTime, dwHighDateTime; };
struct XINPUT_GAMEPAD {
    WORD wButtons{}; BYTE bLeftTrigger{}, bRightTrigger{};
    SHORT sThumbLX{}, sThumbLY{}, sThumbRX{}, sThumbRY{};
};
// Public XInput constants and Win32/ownership boundary stubs only. All state,
// cadence decisions, event normalization and IPC JSON below come from main.cpp.
constexpr WORD XINPUT_GAMEPAD_DPAD_UP = 0x0001, XINPUT_GAMEPAD_DPAD_DOWN = 0x0002;
constexpr WORD XINPUT_GAMEPAD_DPAD_LEFT = 0x0004, XINPUT_GAMEPAD_DPAD_RIGHT = 0x0008;
constexpr WORD XINPUT_GAMEPAD_START = 0x0010, XINPUT_GAMEPAD_BACK = 0x0020;
constexpr WORD XINPUT_GAMEPAD_LEFT_SHOULDER = 0x0100, XINPUT_GAMEPAD_RIGHT_SHOULDER = 0x0200;
constexpr WORD XINPUT_GAMEPAD_A = 0x1000, XINPUT_GAMEPAD_B = 0x2000;
constexpr WORD XINPUT_GAMEPAD_X = 0x4000, XINPUT_GAMEPAD_Y = 0x8000;
constexpr BYTE XINPUT_GAMEPAD_TRIGGER_THRESHOLD = 30;
static HWND g_hwnd = 1;
static bool windowValid = true, windowVisible = true, windowIconic = false, foreground = true;
static bool radialOwner = false, childSuppressed = false, childForeground = false;
static ULONGLONG virtualNow = 0;
static json events = json::array(), trace = json::array(), lifecycle = json::array(), childEvents = json::array();
static bool IsWindow(HWND hwnd) { return hwnd != 0 && windowValid; }
static bool IsWindowVisible(HWND) { return windowVisible; }
static bool IsIconic(HWND) { return windowIconic; }
static bool focusMainWindowIsForeground() { return foreground; }
static bool radialHasInputOwner() { return radialOwner; }
static bool customSteamLibraryInputSuppressed() { return childSuppressed; }
static bool customSteamLibraryChildForeground() { return childForeground; }
static bool customSteamLibrarySendSemanticAction(const char* action) {
    childEvents.push_back({{"action", action}, {"at", virtualNow}}); return true;
}
static void GetSystemTimeAsFileTime(FILETIME* ft) { *ft = {123, 456}; }
static std::uint32_t GetCurrentProcessId() { return 4242; }
static void appendNativeLifecycleLog(const char* event, const json& payload) {
    lifecycle.push_back({{"event", event}, {"data", payload}, {"at", virtualNow}});
}
static void ipc_emit(const char* event, const json& payload) {
    events.push_back({{"event", event}, {"data", payload}, {"at", virtualNow}});
}
`;

const cases = String.raw`
static unsigned checks = 0;
static void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
static void count(std::size_t expected) {
    check(events.size() == expected, "event count: expected " + std::to_string(expected) +
          ", observed " + std::to_string(events.size()) + " at " + std::to_string(virtualNow));
}
static void event(std::size_t index, const char* action, bool repeat, ULONGLONG heldMs) {
    check(index < events.size(), "missing event " + std::to_string(index));
    const auto& envelope = events.at(index);
    const auto& data = envelope.at("data");
    const std::string context = "event " + std::to_string(index) + " " + action + ": ";
    check(envelope.value("event", "") == "gamepad.ui-input", context + "IPC channel");
    check(data.value("action", "") == action, context + "action, observed " + data.dump());
    check(data.contains("repeat") && data.at("repeat").is_boolean(), context + "missing boolean repeat");
    check(data.at("repeat") == repeat, context + "repeat expected " + (repeat ? "true" : "false") + ", observed " + data.dump());
    check(data.contains("heldMs") && data.at("heldMs").is_number_unsigned(), context + "missing unsigned heldMs");
    check(data.at("heldMs") == heldMs, context + "heldMs expected " + std::to_string(heldMs) + ", observed " + data.dump());
    check(data.contains("uiSeq") && data.at("uiSeq").is_number_unsigned() &&
          data.at("uiSeq") == index + 1, context + "strictly increasing uiSeq");
    check(data.contains("provenance") && data.at("provenance").value("session", "") == gamepadUiSessionKey() &&
          !gamepadUiSessionKey().empty(), context + "stable native session");
}
static void tick(ULONGLONG now, WORD buttons = 0, SHORT lx = 0, SHORT ly = 0, BYTE lt = 0, BYTE rt = 0) {
    check(now >= virtualNow, "fixture clock must be monotonic");
    virtualNow = now;
    g_curPad = {buttons, lt, rt, lx, ly, 0, 0};
    const auto before = events.size();
    // The caller's previous-button snapshot is advanced only after the actual
    // consumer returns, as in gamepadEval. No consumer state is reset here.
    gamepadProcessUiInput(buttons, now);
    g_prevW = buttons;
    unsigned navEvents = 0, triggerEvents = 0;
    for (auto index = before; index < events.size(); ++index) {
        const auto action = events.at(index).at("data").value("action", "");
        if (action.rfind("nav-", 0) == 0) ++navEvents;
        if (action.rfind("slider-", 0) == 0) ++triggerEvents;
    }
    check(navEvents <= 1 && triggerEvents <= 1, "at most one direction event per lane per tick; no catch-up burst");
    trace.push_back({{"at", now}, {"buttons", buttons}, {"lx", lx}, {"ly", ly}, {"lt", lt}, {"rt", rt},
        {"foreground", foreground}, {"windowValid", windowValid}, {"windowVisible", windowVisible},
        {"windowIconic", windowIconic}, {"fanEdit", g_uiFanNodeEditActive}, {"sliderEdit", g_uiSliderEditActive},
        {"eventBegin", before}, {"eventEnd", events.size()}});
}
static void triggers(ULONGLONG now, BYTE lt, BYTE rt) { tick(now, 0, 0, 0, lt, rt); }
static void fanEdit(bool active) {
    trace.push_back({{"event", "gamepad.fan-node-edit"}, {"active", active}});
    dispatchUiEditFixture({{"event", "gamepad.fan-node-edit"}, {"data", {{"active", active}}}});
    check(g_uiFanNodeEditActive == active, "real fan edit IPC branch updates state");
}
static void sliderEdit(bool active, bool fromChild = false) {
    const bool previous = g_uiSliderEditActive;
    trace.push_back({{"event", "gamepad.slider-edit"}, {"active", active}, {"fromChild", fromChild}});
    dispatchUiEditFixture({{"event", "gamepad.slider-edit"}, {"data", {{"active", active}}}}, fromChild);
    check(g_uiSliderEditActive == (fromChild ? previous : active), "only root slider edit IPC changes state");
}
static void repeatStateCleared() {
    check(g_uiNavX == 0 && g_uiNavY == 0 && g_uiNavLastEmitTick == 0, "nav direction and last emit reset");
    check(g_uiSliderTriggerDirection == 0 && g_uiSliderTriggerLastEmitTick == 0, "trigger direction and last emit reset");
}
// Independent cadence oracle used against emitted timestamps at 1ms resolution.
static ULONGLONG expectedEditInterval(ULONGLONG heldMs) {
    const auto ramp = heldMs <= 400 ? 0 : heldMs >= 2000 ? 1600 : heldMs - 400;
    return 55 - ramp * 30 / 1600;
}
struct TestCase { std::string name; std::function<void()> run; };
static std::vector<TestCase> testCases() {
    std::vector<TestCase> tests;
    const std::vector<std::pair<WORD, std::string>> directions = {
        {XINPUT_GAMEPAD_DPAD_RIGHT, "right"}, {XINPUT_GAMEPAD_DPAD_LEFT, "left"},
        {XINPUT_GAMEPAD_DPAD_UP, "up"}, {XINPUT_GAMEPAD_DPAD_DOWN, "down"}};
    for (const auto& [button, direction] : directions) {
        tests.push_back({"nav-" + direction, [button, direction] {
            const auto action = "nav-" + direction;
            tick(10000, button); count(1); event(0, action.c_str(), false, 0);
            tick(10219, button); count(1);
            tick(10220, button); count(2); event(1, action.c_str(), true, 220);
            tick(10221, button); count(2);
            tick(10440, button); count(3); event(2, action.c_str(), true, 440);
            tick(10441); tick(10660); count(3);
            tick(10661, button); count(4); event(3, action.c_str(), false, 0);
            tick(10880, button); count(4);
            tick(10881, button); count(5); event(4, action.c_str(), true, 220);
        }});
        tests.push_back({"fan-nav-" + direction, [button, direction] {
            const auto action = "nav-" + direction;
            fanEdit(true);
            tick(10000, button); count(1); event(0, action.c_str(), false, 0);
            tick(10054, button); count(1);
            tick(10055, button); count(2); event(1, action.c_str(), true, 55);
            tick(10110, button); count(3); event(2, action.c_str(), true, 110);
            tick(10220, button); count(4); event(3, action.c_str(), true, 220);
            tick(10275); count(4);
            tick(10276, button); count(5); event(4, action.c_str(), false, 0);
        }});
    }
    for (bool right : {true, false}) {
        tests.push_back({right ? "trigger-right" : "trigger-left", [right] {
            auto pull = [right](ULONGLONG now, BYTE value) { triggers(now, right ? 0 : value, right ? value : 0); };
            const char* action = right ? "slider-increase" : "slider-decrease";
            pull(10000, 29); count(0);
            pull(10001, 30); count(1); event(0, action, false, 0);
            pull(10220, 255); count(1);
            pull(10221, 255); count(2); event(1, action, true, 220);
            pull(10222, 29); count(2);
            pull(10223, 255); count(3); event(2, action, false, 0);
            pull(10442, 255); count(3);
            pull(10443, 255); count(4); event(3, action, true, 220);
            pull(10444, 0); pull(11000, 0); count(4);
        }});
    }
    tests.push_back({"nav-reverse-and-axis-change", [] {
        tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT);
        tick(10001, XINPUT_GAMEPAD_DPAD_LEFT); count(2); event(1, "nav-left", false, 0);
        tick(10220, XINPUT_GAMEPAD_DPAD_LEFT); count(2);
        tick(10221, XINPUT_GAMEPAD_DPAD_LEFT); count(3); event(2, "nav-left", true, 220);
        tick(10222, XINPUT_GAMEPAD_DPAD_UP); count(4); event(3, "nav-up", false, 0);
        tick(10223, XINPUT_GAMEPAD_DPAD_DOWN); count(5); event(4, "nav-down", false, 0);
    }});
    tests.push_back({"trigger-reverse-and-both-held", [] {
        triggers(10000, 255, 0); event(0, "slider-decrease", false, 0);
        triggers(10001, 0, 255); count(2); event(1, "slider-increase", false, 0);
        triggers(10220, 255, 255); count(2);
        triggers(10221, 255, 255); count(3); event(2, "slider-increase", true, 220);
        triggers(10222, 255, 0); count(4); event(3, "slider-decrease", false, 0);
        triggers(10442, 255, 0); count(5); event(4, "slider-decrease", true, 220);
    }});
    tests.push_back({"fan-edit-enter-exit-resets-nav-hold", [] {
        tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT); event(0, "nav-right", false, 0);
        fanEdit(true);
        tick(10010, XINPUT_GAMEPAD_DPAD_RIGHT); count(2); event(1, "nav-right", false, 0);
        tick(10064, XINPUT_GAMEPAD_DPAD_RIGHT); count(2);
        tick(10065, XINPUT_GAMEPAD_DPAD_RIGHT); count(3); event(2, "nav-right", true, 55);
        fanEdit(false);
        tick(10066, XINPUT_GAMEPAD_DPAD_RIGHT); count(4); event(3, "nav-right", false, 0);
        tick(10121, XINPUT_GAMEPAD_DPAD_RIGHT); count(4);
        tick(10285, XINPUT_GAMEPAD_DPAD_RIGHT); count(4);
        tick(10286, XINPUT_GAMEPAD_DPAD_RIGHT); count(5); event(4, "nav-right", true, 220);
    }});
    tests.push_back({"fan-edit-keeps-trigger-at-220ms", [] {
        fanEdit(true);
        triggers(10000, 0, 255); event(0, "slider-increase", false, 0);
        triggers(10055, 0, 255); triggers(10219, 0, 255); count(1);
        triggers(10220, 0, 255); count(2); event(1, "slider-increase", true, 220);
        triggers(12420, 0, 255); count(3); event(2, "slider-increase", true, 2420);
        triggers(12445, 0, 255); triggers(12639, 0, 255); count(3);
        triggers(12640, 0, 255); count(4); event(3, "slider-increase", true, 2640);
    }});
    tests.push_back({"nav-and-trigger-have-independent-hold-ages", [] {
        tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT); event(0, "nav-right", false, 0);
        tick(10100, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(2); event(1, "slider-increase", false, 0);
        tick(10220, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(3); event(2, "nav-right", true, 220);
        tick(10320, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4); event(3, "slider-increase", true, 220);
        tick(10440, XINPUT_GAMEPAD_DPAD_LEFT, 0, 0, 0, 255); count(5); event(4, "nav-left", false, 0);
        tick(10540, XINPUT_GAMEPAD_DPAD_LEFT, 0, 0, 0, 255); count(6); event(5, "slider-increase", true, 440);
    }});
    tests.push_back({"sparse-ticks-preserve-age-without-catchup-burst", [] {
        tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT);
        tick(11000, XINPUT_GAMEPAD_DPAD_RIGHT); count(2); event(1, "nav-right", true, 1000);
        tick(11000, XINPUT_GAMEPAD_DPAD_RIGHT); count(2);
        tick(12200, XINPUT_GAMEPAD_DPAD_RIGHT); count(3); event(2, "nav-right", true, 2200);
        tick(20000); count(3);
    }});
    tests.push_back({"zero-tick-is-valid-hold-origin", [] {
        tick(0, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(2);
        event(0, "slider-increase", false, 0); event(1, "nav-right", false, 0);
        tick(220, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4);
        event(2, "slider-increase", true, 220); event(3, "nav-right", true, 220);
    }});
    tests.push_back({"held-age-and-tick-are-64-bit", [] {
        constexpr ULONGLONG base = (1ULL << 32) + 1000, held = (1ULL << 32) + 220;
        tick(base, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(2);
        tick(base + held, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4);
        event(2, "slider-increase", true, held); event(3, "nav-right", true, held);
    }});
    for (const auto& [button, direction] : directions) {
        const bool horizontal = direction == "left" || direction == "right";
        const int sign = direction == "left" || direction == "down" ? -1 : 1;
        tests.push_back({"analog-threshold-" + direction, [horizontal, sign, direction] {
            auto move = [horizontal, sign](ULONGLONG now, int value) {
                tick(now, 0, static_cast<SHORT>(horizontal ? sign * value : 0),
                     static_cast<SHORT>(horizontal ? 0 : sign * value));
            };
            const auto action = "nav-" + direction;
            move(10000, 15999); count(0);
            move(10001, 16000); count(1); event(0, action.c_str(), false, 0);
            move(10220, 16000); count(1);
            move(10221, 16000); count(2); event(1, action.c_str(), true, 220);
            move(10222, 15999); count(2);
            move(10223, 16000); count(3); event(2, action.c_str(), false, 0);
        }});
    }
    tests.push_back({"axis-neutral-admission-keeps-dpad-available", [] {
        g_gamepadAxisNeutralAdmissionRequired = true;
        tick(10000, 0, 32767); count(0);
        tick(10001, XINPUT_GAMEPAD_DPAD_LEFT, 32767); count(1); event(0, "nav-left", false, 0);
        tick(10002); count(1);
        g_gamepadAxisNeutralAdmissionRequired = false;
        tick(10003, 0, 32767); count(2); event(1, "nav-right", false, 0);
    }});
    for (const std::string gate : {"foreground", "hidden", "iconic", "invalid-window", "null-window"}) {
        tests.push_back({"nav-focus-recovery-" + gate, [gate] {
            tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT); count(1);
            if (gate == "foreground") foreground = false;
            if (gate == "hidden") windowVisible = false;
            if (gate == "iconic") windowIconic = true;
            if (gate == "invalid-window") windowValid = false;
            if (gate == "null-window") g_hwnd = 0;
            tick(10050, XINPUT_GAMEPAD_DPAD_RIGHT); tick(10660, XINPUT_GAMEPAD_DPAD_RIGHT); count(1);
            foreground = windowVisible = windowValid = true; windowIconic = false; g_hwnd = 1;
            tick(10661, XINPUT_GAMEPAD_DPAD_RIGHT); count(2); event(1, "nav-right", false, 0);
            tick(10880, XINPUT_GAMEPAD_DPAD_RIGHT); count(2);
            tick(10881, XINPUT_GAMEPAD_DPAD_RIGHT); count(3); event(2, "nav-right", true, 220);
        }});
    }
    for (bool right : {true, false}) {
        tests.push_back({right ? "trigger-focus-recovery-right" : "trigger-focus-recovery-left", [right] {
            auto pull = [right](ULONGLONG now) { triggers(now, right ? 0 : 255, right ? 255 : 0); };
            const char* action = right ? "slider-increase" : "slider-decrease";
            pull(10000); count(1); event(0, action, false, 0);
            foreground = false; pull(10050); pull(10660); count(1);
            foreground = true; pull(10661); count(2); event(1, action, false, 0);
            pull(10880); count(2);
            pull(10881); count(3); event(2, action, true, 220);
        }});
    }
    tests.push_back({"trigger-brief-focus-loss-must-not-delay-new-hold", [] {
        triggers(10000, 0, 255); count(1);
        foreground = false; triggers(10050, 0, 255); count(1);
        foreground = true; triggers(10060, 0, 255); count(2); event(1, "slider-increase", false, 0);
    }});
    tests.push_back({"trigger-release-while-unfocused-must-end-old-hold", [] {
        triggers(10000, 0, 255); count(1);
        foreground = false; triggers(10050, 0, 0); triggers(10660, 0, 0); count(1);
        foreground = true; triggers(10661, 0, 255); count(2); event(1, "slider-increase", false, 0);
    }});
    tests.push_back({"trigger-neutral-after-focus-restoration-ends-old-hold", [] {
        triggers(10000, 0, 255);
        foreground = false; triggers(10050, 0, 255); count(1);
        foreground = true; triggers(10660, 0, 0); count(1);
        triggers(10661, 0, 255); count(2); event(1, "slider-increase", false, 0);
    }});
    tests.push_back({"ordinary-button-edges-have-default-metadata", [] {
        tick(10000, XINPUT_GAMEPAD_A); count(1); event(0, "confirm", false, 0);
        tick(11000, XINPUT_GAMEPAD_A); count(1);
        tick(11001); tick(11002, XINPUT_GAMEPAD_B); count(2); event(1, "back", false, 0);
        tick(11003); tick(11004, XINPUT_GAMEPAD_Y); count(3); event(2, "edit-game", false, 0);
    }});
    tests.push_back({"emitter-guards-and-sequence", [] {
        gamepadEmitUiAction(nullptr); gamepadEmitUiAction(""); count(0);
        radialOwner = true; gamepadEmitUiAction("nav-right", true, 777); count(0);
        radialOwner = false; foreground = false; gamepadEmitUiAction("nav-right", true, 777); count(0);
        check(!lifecycle.empty() && lifecycle.back().at("event") == "gamepad-ui-input-dropped", "drop logged at boundary");
        foreground = true; gamepadEmitUiAction("nav-right", true, 777); count(1); event(0, "nav-right", true, 777);
        gamepadEmitUiAction("confirm"); count(2); event(1, "confirm", false, 0);
    }});
    tests.push_back({"child-owner-never-duplicates-parent-ipc", [] {
        childSuppressed = true; childForeground = false;
        tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT); count(0);
        childForeground = true;
        tick(10001, XINPUT_GAMEPAD_DPAD_RIGHT); count(0);
        check(childEvents.size() == 1 && childEvents.at(0).value("action", "") == "nav-right", "one child semantic delivery");
        childSuppressed = childForeground = false;
        tick(10002); tick(10003, XINPUT_GAMEPAD_DPAD_RIGHT); count(1); event(0, "nav-right", false, 0);
    }});

    tests.push_back({"edit-interval-exact-integer-formula", [] {
        const std::vector<std::pair<ULONGLONG, ULONGLONG>> anchors = {
            {0, 55}, {399, 55}, {400, 55}, {453, 55}, {454, 54},
            {800, 48}, {1200, 40}, {1600, 33}, {1999, 26}, {2000, 25}, {999999, 25}};
        for (const auto& [held, expected] : anchors)
            check(gamepadUiEditRepeatInterval(held) == expected, "exact integer cadence anchor " + std::to_string(held));
        for (ULONGLONG held = 0; held <= 2500; ++held)
            check(gamepadUiEditRepeatInterval(held) == expectedEditInterval(held), "exact cadence formula " + std::to_string(held));
        check(gamepadUiEditRepeatInterval(~ULONGLONG{0}) == 25, "64-bit hold clamps before multiplication");
    }});
    for (const std::string profile : {"fan", "slider"}) {
        for (const auto& [button, direction] : directions) {
            tests.push_back({profile + "-accelerated-nav-" + direction, [profile, button, direction] {
                if (profile == "fan") fanEdit(true); else sliderEdit(true);
                const auto action = "nav-" + direction;
                ULONGLONG last = 0, previousInterval = 55;
                for (ULONGLONG held = 0; held <= 2600; ++held) {
                    const auto before = events.size();
                    tick(10000 + held, button);
                    const bool due = held == 0 || held - last >= expectedEditInterval(held);
                    count(before + (due ? 1 : 0));
                    if (!due) continue;
                    event(before, action.c_str(), held != 0, held);
                    if (held) {
                        const auto interval = held - last;
                        check(interval <= previousInterval, "successive actual intervals never increase");
                        check(interval == expectedEditInterval(held), "timestamp interval follows linear ramp");
                        if (held >= 2055) check(interval == 25, "long hold stays at 25ms");
                        previousInterval = interval;
                    }
                    last = held;
                    tick(10000 + held, button); count(before + 1);
                }
                check(previousInterval == 25 && events.size() > 60, "continuous repeats reach minimum interval");
            }});
        }
        tests.push_back({profile + "-sparse-accelerated-nav-no-burst", [profile] {
            if (profile == "fan") fanEdit(true); else sliderEdit(true);
            tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT); count(1); event(0, "nav-right", false, 0);
            tick(12000, XINPUT_GAMEPAD_DPAD_RIGHT); count(2); event(1, "nav-right", true, 2000);
            tick(12000, XINPUT_GAMEPAD_DPAD_RIGHT); tick(12024, XINPUT_GAMEPAD_DPAD_RIGHT); count(2);
            tick(12025, XINPUT_GAMEPAD_DPAD_RIGHT); count(3); event(2, "nav-right", true, 2025);
            tick(50000, XINPUT_GAMEPAD_DPAD_RIGHT); count(4); event(3, "nav-right", true, 40000);
            tick(50000, XINPUT_GAMEPAD_DPAD_RIGHT); count(4);
        }});
        tests.push_back({profile + "-accelerated-nav-reversal-axis-release", [profile] {
            if (profile == "fan") fanEdit(true); else sliderEdit(true);
            tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT); count(1);
            tick(12000, XINPUT_GAMEPAD_DPAD_RIGHT); count(2); event(1, "nav-right", true, 2000);
            tick(12001, XINPUT_GAMEPAD_DPAD_LEFT); count(3); event(2, "nav-left", false, 0);
            tick(12055, XINPUT_GAMEPAD_DPAD_LEFT); count(3);
            tick(12056, XINPUT_GAMEPAD_DPAD_LEFT); count(4); event(3, "nav-left", true, 55);
            tick(12057, XINPUT_GAMEPAD_DPAD_UP); count(5); event(4, "nav-up", false, 0);
            tick(12058); count(5);
            check(g_uiNavX == 0 && g_uiNavY == 0, "release clears nav hold direction");
            tick(12059, XINPUT_GAMEPAD_DPAD_UP); count(6); event(5, "nav-up", false, 0);
            tick(12113, XINPUT_GAMEPAD_DPAD_UP); count(6);
            tick(12114, XINPUT_GAMEPAD_DPAD_UP); count(7); event(6, "nav-up", true, 55);
        }});
    }
    for (bool right : {true, false}) {
        tests.push_back({right ? "slider-accelerated-trigger-right" : "slider-accelerated-trigger-left", [right] {
            sliderEdit(true);
            const char* action = right ? "slider-increase" : "slider-decrease";
            ULONGLONG last = 0, previousInterval = 55;
            for (ULONGLONG held = 0; held <= 2600; ++held) {
                const auto before = events.size();
                triggers(10000 + held, right ? 0 : 255, right ? 255 : 0);
                const bool due = held == 0 || held - last >= expectedEditInterval(held);
                count(before + (due ? 1 : 0));
                if (!due) continue;
                event(before, action, held != 0, held);
                if (held) {
                    check(held - last <= previousInterval, "trigger intervals never increase");
                    check(held - last == expectedEditInterval(held), "trigger timestamp follows linear ramp");
                    previousInterval = held - last;
                }
                last = held;
                triggers(10000 + held, right ? 0 : 255, right ? 255 : 0); count(before + 1);
            }
            check(previousInterval == 25 && events.size() > 60, "trigger reaches 25ms continuously");
        }});
    }
    tests.push_back({"slider-sparse-trigger-reversal-and-release-no-burst", [] {
        sliderEdit(true);
        triggers(10000, 0, 255); count(1); event(0, "slider-increase", false, 0);
        triggers(12000, 0, 255); count(2); event(1, "slider-increase", true, 2000);
        triggers(12000, 0, 255); triggers(12024, 0, 255); count(2);
        triggers(12025, 0, 255); count(3); event(2, "slider-increase", true, 2025);
        triggers(12026, 255, 0); count(4); event(3, "slider-decrease", false, 0);
        triggers(12080, 255, 0); count(4);
        triggers(12081, 255, 0); count(5); event(4, "slider-decrease", true, 55);
        triggers(12082, 0, 0); count(5);
        check(g_uiSliderTriggerDirection == 0, "release clears trigger hold direction");
        triggers(12083, 255, 0); count(6); event(5, "slider-decrease", false, 0);
        triggers(12137, 255, 0); count(6);
        triggers(12138, 255, 0); count(7); event(6, "slider-decrease", true, 55);
    }});
    tests.push_back({"slider-edit-root-child-and-duplicate-notifications", [] {
        fanEdit(true);
        tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(2);
        sliderEdit(true, true);
        check(g_uiFanNodeEditActive && g_uiNavX == 1 && g_uiNavLastEmitTick == 10000 &&
              g_uiSliderTriggerDirection == 1 && g_uiSliderTriggerLastEmitTick == 10000, "child cannot enable slider or reset existing profile");
        sliderEdit(true); repeatStateCleared();
        check(!g_uiFanNodeEditActive, "root slider activation clears fan profile");
        tick(10001, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4);
        event(2, "slider-increase", false, 0); event(3, "nav-right", false, 0);
        sliderEdit(false, true); sliderEdit(true, true); sliderEdit(true);
        check(g_uiNavX == 1 && g_uiNavLastEmitTick == 10001 && g_uiSliderTriggerDirection == 1 &&
              g_uiSliderTriggerLastEmitTick == 10001, "child and duplicate true leave repeat state intact");
        tick(10055, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4);
        tick(10056, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
        event(4, "slider-increase", true, 55); event(5, "nav-right", true, 55);
        sliderEdit(false); repeatStateCleared();
        tick(10057, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(8);
        event(6, "slider-increase", false, 0); event(7, "nav-right", false, 0);
        sliderEdit(false); sliderEdit(false, true); sliderEdit(true, true);
        check(g_uiNavLastEmitTick == 10057 && g_uiNavX == 1 && g_uiSliderTriggerDirection == 1 &&
              g_uiSliderTriggerLastEmitTick == 10057, "duplicate false and child cannot reset ordinary cadence");
        tick(10276, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(8);
        tick(10277, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(10);
        event(8, "slider-increase", true, 220); event(9, "nav-right", true, 220);
    }});
    tests.push_back({"slider-edit-exit-long-hold-and-reentry-reset-both-lanes", [] {
        sliderEdit(true);
        tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(2);
        tick(12000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4);
        event(2, "slider-increase", true, 2000); event(3, "nav-right", true, 2000);
        sliderEdit(false); repeatStateCleared();
        tick(12001, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
        event(4, "slider-increase", false, 0); event(5, "nav-right", false, 0);
        tick(12026, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
        tick(12220, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
        tick(12221, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(8);
        event(6, "slider-increase", true, 220); event(7, "nav-right", true, 220);
        sliderEdit(true); repeatStateCleared();
        tick(12222, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(10);
        event(8, "slider-increase", false, 0); event(9, "nav-right", false, 0);
        tick(12276, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(10);
        tick(12277, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(12);
        event(10, "slider-increase", true, 55); event(11, "nav-right", true, 55);
    }});
    tests.push_back({"fan-activation-clears-slider-and-resets-both-lanes", [] {
        sliderEdit(true);
        tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(2);
        tick(12000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4);
        fanEdit(true); repeatStateCleared();
        check(!g_uiSliderEditActive, "fan and slider profiles are mutually exclusive");
        tick(12001, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
        event(4, "slider-increase", false, 0); event(5, "nav-right", false, 0);
        tick(12056, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(7); event(6, "nav-right", true, 55);
        tick(12220, 0, 0, 0, 0, 255); count(7);
        triggers(12221, 0, 255); count(8); event(7, "slider-increase", true, 220);
    }});
    tests.push_back({"fan-repeated-active-true-node-switch-still-resets-nav", [] {
        fanEdit(true);
        tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(2);
        tick(12000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4);
        fanEdit(true);
        check(g_uiNavX == 0 && g_uiNavY == 0 && g_uiNavLastEmitTick == 0, "same fan active resets node cadence");
        check(g_uiSliderTriggerDirection == 1 && g_uiSliderTriggerLastEmitTick == 12000, "existing fan notification preserves ordinary trigger state");
        tick(12001, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(5); event(4, "nav-right", false, 0);
        tick(12055, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(5);
        tick(12056, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6); event(5, "nav-right", true, 55);
    }});
    tests.push_back({"slider-nav-and-trigger-accelerate-from-independent-origins", [] {
        sliderEdit(true);
        tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT); count(1);
        tick(12000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(3);
        event(1, "slider-increase", false, 0); event(2, "nav-right", true, 2000);
        tick(12025, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4); event(3, "nav-right", true, 2025);
        tick(12050, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(5); event(4, "nav-right", true, 2050);
        tick(12055, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6); event(5, "slider-increase", true, 55);
        tick(12056, XINPUT_GAMEPAD_DPAD_LEFT, 0, 0, 0, 255); count(7); event(6, "nav-left", false, 0);
        tick(12110, XINPUT_GAMEPAD_DPAD_LEFT, 0, 0, 0, 255); count(8); event(7, "slider-increase", true, 110);
        tick(12111, XINPUT_GAMEPAD_DPAD_LEFT, 0, 0, 0, 255); count(9); event(8, "nav-left", true, 55);
    }});
    for (const std::string gate : {"foreground", "hidden", "iconic", "invalid-window", "null-window"}) {
        tests.push_back({"slider-accelerated-brief-focus-recovery-" + gate, [gate] {
            sliderEdit(true);
            tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(2);
            tick(12000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4);
            if (gate == "foreground") foreground = false;
            if (gate == "hidden") windowVisible = false;
            if (gate == "iconic") windowIconic = true;
            if (gate == "invalid-window") windowValid = false;
            if (gate == "null-window") g_hwnd = 0;
            tick(12001, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4); repeatStateCleared();
            foreground = windowVisible = windowValid = true; windowIconic = false; g_hwnd = 1;
            tick(12002, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
            event(4, "slider-increase", false, 0); event(5, "nav-right", false, 0);
            tick(12027, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
            tick(12056, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
            tick(12057, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(8);
            event(6, "slider-increase", true, 55); event(7, "nav-right", true, 55);
        }});
    }
    tests.push_back({"fan-accelerated-brief-focus-recovery", [] {
        fanEdit(true);
        tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT); count(1);
        tick(12000, XINPUT_GAMEPAD_DPAD_RIGHT); count(2);
        foreground = false; tick(12001, XINPUT_GAMEPAD_DPAD_RIGHT); count(2); repeatStateCleared();
        foreground = true; tick(12002, XINPUT_GAMEPAD_DPAD_RIGHT); count(3); event(2, "nav-right", false, 0);
        tick(12056, XINPUT_GAMEPAD_DPAD_RIGHT); count(3);
        tick(12057, XINPUT_GAMEPAD_DPAD_RIGHT); count(4); event(3, "nav-right", true, 55);
    }});
    for (const std::string profile : {"fan", "slider"}) {
        tests.push_back({profile + "-native-reset-clears-edit-and-repeat-state", [profile] {
            if (profile == "fan") fanEdit(true); else sliderEdit(true);
            tick(10000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(2);
            tick(12000, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4);
            resetUiFixture(); repeatStateCleared();
            check(!g_uiSliderEditActive && !g_uiFanNodeEditActive, "real native reset clears both edit profiles");
            tick(12001, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
            event(4, "slider-increase", false, 0); event(5, "nav-right", false, 0);
            tick(12220, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
            tick(12221, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(8);
            event(6, "slider-increase", true, 220); event(7, "nav-right", true, 220);
        }});
    }
    tests.push_back({"slider-zero-origin-and-64bit-hold-clamp", [] {
        sliderEdit(true);
        tick(0, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(2);
        event(0, "slider-increase", false, 0); event(1, "nav-right", false, 0);
        tick(54, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(2);
        tick(55, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(4);
        event(2, "slider-increase", true, 55); event(3, "nav-right", true, 55);
        constexpr ULONGLONG held = (1ULL << 32) + 2000;
        tick(held, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
        event(4, "slider-increase", true, held); event(5, "nav-right", true, held);
        tick(held + 24, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(6);
        tick(held + 25, XINPUT_GAMEPAD_DPAD_RIGHT, 0, 0, 0, 255); count(8);
        event(6, "slider-increase", true, held + 25); event(7, "nav-right", true, held + 25);
    }});
    return tests;
}
int main(int argc, char** argv) {
    const auto tests = testCases();
    if (argc == 2 && std::string(argv[1]) == "--list") {
        json names = json::array(); for (const auto& test : tests) names.push_back(test.name);
        std::cout << names.dump() << '\n'; return 0;
    }
    if (argc != 2) { std::cerr << "Expected one case name\n"; return 2; }
    for (const auto& test : tests) {
        if (test.name != argv[1]) continue;
        json result = {{"case", test.name}, {"status", "pass"}};
        try { test.run(); }
        catch (const std::exception& error) { result["status"] = "fail"; result["error"] = error.what(); }
        result["checks"] = checks; result["events"] = events; result["trace"] = trace;
        result["lifecycle"] = lifecycle; result["childEvents"] = childEvents;
        std::cout << result.dump() << '\n'; return result["status"] == "pass" ? 0 : 1;
    }
    std::cerr << "Unknown case: " << argv[1] << '\n'; return 2;
}
`;

function fixture(mutation) {
  const code = sections.filter(section => !['ui-edit-ipc-consumer', 'native-ui-reset'].includes(section.name)).map(section => {
    let text = section.code;
    if (mutation) text = mutation(section.name, text);
    return `\n#line ${section.startLine} "native/main.cpp"\n${text}\n`;
  }).join('');
  return preamble + code + '\n#line 1 "native-ui-reset-fixture"\nstatic void resetUiFixture() {\n' +
    sectionCode('native-ui-reset') + '\n}\n#line 1 "ui-edit-ipc-fixture"\n' +
    'static void dispatchUiEditFixture(const json& req, bool fromChild = false) {\n' +
    sectionCode('ui-edit-ipc-consumer') + '\n}\n#line 1 "native-repeat-tests"\n' + cases;
}

function build(name, text) {
  const cpp = path.join(out, `${name}.cpp`), exe = path.join(out, `${name}.exe`);
  const obj = path.join(out, `${name}.obj`), cmd = path.join(out, `${name}.compile.cmd`);
  for (const value of [cpp, exe, obj, cmd, vcvars, path.join(root, 'deps/json')]) {
    assert(!/[\r\n"%]/.test(value), 'Unsupported character in Windows build path');
  }
  fs.writeFileSync(cpp, text);
  fs.writeFileSync(cmd, calibrationToolchainPreamble(vcvars) + 'chcp 65001 >nul\r\n' +
    `cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /I"${path.join(root, 'deps/json')}" "${cpp}" /Fo"${obj}" /Fe"${exe}"\r\nexit /b %errorlevel%\r\n`);
  const run = spawnSync('cmd.exe', ['/d', '/c', cmd], {cwd: out, encoding: 'utf8', windowsHide: true, timeout: 120000});
  fs.writeFileSync(path.join(out, `${name}.build.log`), (run.stdout || '') + (run.stderr || '') + (run.error ? String(run.error) : ''));
  assert.equal(run.status, 0, `C++ build failed; see ${name}.build.log: ${(run.stdout || '') + (run.stderr || '')}`);
  return {exe, cpp, fixtureSha256: sha256(text), executableSha256: sha256(fs.readFileSync(exe))};
}
function execute(exe, name) {
  const run = spawnSync(exe, [name], {cwd: out, encoding: 'utf8', windowsHide: true, timeout: 15000});
  const output = (run.stdout || '').trim();
  let result;
  try { result = JSON.parse(output); }
  catch { result = {case: name, status: 'error', error: String(run.error || run.stderr || output || `Exit ${run.status}`)}; }
  if (run.status !== (result.status === 'pass' ? 0 : 1)) {
    result.status = 'error'; result.error = `Unexpected fixture exit ${run.status}: ${run.stderr || run.error || ''}`;
  }
  return {...result, exitCode: run.status};
}

const binding = {
  main: mainPath, mainSha256: sha256(source), test: fileURLToPath(import.meta.url),
  testSha256: sha256(fs.readFileSync(fileURLToPath(import.meta.url))),
  startedAt: new Date().toISOString(),
  buildInputs: ['deps/json/json.hpp', 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'].map(relative => ({
    path: path.join(root, relative), sha256: sha256(fs.readFileSync(path.join(root, relative))),
  })),
  sections: sections.map(({code, ...section}) => section),
  productionConsumerExtractedVerbatim: true, productionFilesWritten: [], liveMutations: 0,
  scope: 'In-memory native consumer regression; Win32/ownership/IPC transport stubbed; no YMCC or device input',
  caseIsolation: 'Fresh fixture process for each case, including function-local hold statics',
};
fs.writeFileSync(path.join(out, 'source-binding.json'), JSON.stringify(binding, null, 2) + '\n');
console.log(`Evidence: ${out}`);
console.log('Compiling verbatim native consumer with in-memory Win32/IPC boundaries...');
const baseline = build('performance-slider-native-repeat.fixture', fixture());
const list = spawnSync(baseline.exe, ['--list'], {encoding: 'utf8', windowsHide: true, timeout: 15000});
assert.equal(list.status, 0, 'Fixture case discovery failed');
const names = JSON.parse(list.stdout);
assert(names.length > 0 && new Set(names).size === names.length, 'Case names must be nonempty and unique');
const results = names.map(name => {
  const result = execute(baseline.exe, name);
  console.log(`${result.status.toUpperCase()} ${name}${result.error ? ': ' + result.error : ''}`);
  return result;
});
fs.writeFileSync(path.join(out, 'cases.json'), JSON.stringify(results, null, 2) + '\n');

// Mutate only the extracted evidence fixture, never native/main.cpp. These
// controls demonstrate failures for absent metadata, broken age and the old fixed fan rate.
const controls = [
  {name: 'missing-repeat-metadata', testCase: 'nav-right', section: 'native-ui-consumer',
    from: ', {"repeat", repeat}, {"heldMs", heldMs}', to: ''},
  {name: 'lost-trigger-held-age', testCase: 'trigger-right', section: 'native-ui-consumer',
    from: 'now - uiTriggerHoldStarted);', to: '0);'},
  {name: 'fan-rate-fixed-55ms', testCase: 'fan-accelerated-nav-right', section: 'ui-state-and-session',
    from: '(GP_UI_EDIT_REPEAT_START_MS - GP_UI_EDIT_REPEAT_MIN_MS) / GP_UI_EDIT_REPEAT_RAMP_MS;', to: '0;'},
];
const negativeControls = [];
console.log('Checking regression sensitivity against three mutated consumer fixtures...');
for (const control of controls) {
  assert.equal(results.find(result => result.case === control.testCase)?.status, 'pass', `Control needs passing baseline: ${control.testCase}`);
  let replacements = 0;
  const text = fixture((name, code) => {
    if (name !== control.section) return code;
    assert(code.includes(control.from), `Missing mutation target: ${control.name}`);
    assert.equal(code.indexOf(control.from), code.lastIndexOf(control.from), `Ambiguous mutation target: ${control.name}`);
    ++replacements;
    return code.replace(control.from, control.to);
  });
  assert.equal(replacements, 1);
  const mutated = build(`negative-${control.name}`, text);
  const result = execute(mutated.exe, control.testCase);
  const rejected = result.exitCode === 1 && result.status === 'fail';
  negativeControls.push({name: control.name, testCase: control.testCase, rejected,
    mutation: {section: control.section, from: control.from, to: control.to}, ...mutated, result});
  console.log(`${rejected ? 'PASS' : 'FAIL'} negative control ${control.name}${result.error ? ': ' + result.error : ''}`);
}
fs.writeFileSync(path.join(out, 'negative-controls.json'), JSON.stringify(negativeControls, null, 2) + '\n');

// Concurrent changes elsewhere in main.cpp do not invalidate these bindings.
// Any changed consumer/declaration/IPС branch does invalidate this run.
const sourceAfter = fs.readFileSync(mainPath, 'utf8');
let changedSections;
try {
  const after = extract(sourceAfter);
  changedSections = sections.filter(section => after.find(item => item.name === section.name)?.sha256 !== section.sha256).map(section => section.name);
} catch (error) { changedSections = [String(error)]; }
const passed = results.filter(result => result.status === 'pass').length;
const failed = results.length - passed;
const controlsPassed = negativeControls.filter(control => control.rejected).length;
const exitCode = failed || controlsPassed !== controls.length || changedSections.length ? 1 : 0;
Object.assign(binding, {pass: exitCode === 0, count: results.length, finishedAt: new Date().toISOString(), mainSha256After: sha256(sourceAfter),
  wholeSourceChangedDuringRun: sha256(sourceAfter) !== binding.mainSha256,
  extractedSourceUnchangedDuringRun: changedSections.length === 0, changedSections,
  baseline, cases: results.length, passed, failed,
  checks: results.reduce((total, result) => total + (result.checks || 0), 0),
  negativeControls: {passed: controlsPassed, total: controls.length}, exitCode});
const summary = [`Native repeat regression: ${passed}/${results.length} passed; ${failed} failed.`,
  `Assertions executed: ${binding.checks}. Negative controls rejected: ${controlsPassed}/${controls.length}.`,
  `Extracted production source unchanged during run: ${binding.extractedSourceUnchangedDuringRun}.`,
  ...results.filter(result => result.status !== 'pass').map(result => `FAIL ${result.case}: ${result.error}`),
  ...changedSections.map(name => `STALE extracted source: ${name}`),
  'Scope: in-memory native consumer only; no real YMCC, frontend or hardware validation.'];
fs.writeFileSync(path.join(out, 'source-binding.json'), JSON.stringify(binding, null, 2) + '\n');
fs.writeFileSync(path.join(out, 'summary.json'), JSON.stringify(binding, null, 2) + '\n');
fs.writeFileSync(path.join(out, 'results.txt'), summary.join('\n') + '\n');
console.log(summary.join('\n'));
process.exitCode = exitCode;
