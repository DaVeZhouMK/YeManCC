
#define YMCC_SHORTCUT_FIXTURE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <fstream>
#include <functional>
#include <string>
#include <set>
#include <vector>
#include "input_shortcut_toggle.h"
#include "shortcut_action_protocol.h"
#include "keyboard_shortcut_sequence.h"
#include "shortcut_frontend_button.h"
#include "boot_mode_shortcuts.h"
using json=nlohmann::json;
namespace ymcc::radial{static bool userAvailable(){return false;}static bool userActionAvailable(const std::string& s){return s!="radial.summon";}}
static constexpr int kOemProducerCount=8;
struct NativeKeyboardShortcutRule {
    std::string outputId;
    ymcc::keyboardSequence::Plan keyboardPlan;
    uint64_t requiredMask = 0;
    std::string trigger = "press";
    ULONGLONG holdMs = 500;
    ULONGLONG doubleWindowMs = 500;
    ULONGLONG intervalMs = 150;
    bool linearAcceleration = false;
    WORD virtualKey = 0;
    bool requiresShift = false;
    bool forceExtendedKey = false;
    std::string uiAction;
    json uiParams = json::object();
    std::string inputAction;
    std::string ownAction;
    int actionDelta = 0;
    bool needsNeutral = true;
    std::string preset;
    std::vector<WORD> desktopKeys;
    bool matchedLastTick = false;
    bool holdFired = false;
    ULONGLONG holdStartedAt = 0;
    ULONGLONG lastEmitAt = 0;
    ULONGLONG lastPressAt = 0;
    uint64_t rogClickGeneration = 0;
    uint32_t rogClickRuleEpoch = 0;
    ULONGLONG rogLastClickAt = 0;
    bool rogClickPending = false;
    uint64_t onexClickGeneration = 0;
    uint32_t onexClickRuleEpoch = 0;
    ULONGLONG onexLastClickAt = 0;
    bool onexClickPending = false;
    uint64_t wmiClickGeneration = 0;
    uint32_t wmiClickRuleEpoch = 0;
    ULONGLONG wmiLastClickAt = 0;
    bool wmiClickPending = false; // click-only double history, independent of sampled held state
    std::array<uint64_t, kOemProducerCount> producerGenerationsSeen{}, producerAuthoritiesSeen{};
};
static std::vector<NativeKeyboardShortcutRule> g_nativeKeyboardShortcuts;
static uint32_t g_nativeShortcutLegacyOverrideMask=0;
static bool g_nativeShortcutRulesConfigured=false;
static ULONGLONG g_nativeKeyboardShortcutLastRefresh=0;
static long long g_nativeKeyboardShortcutRevision=-1;
static constexpr ULONGLONG NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS=400;
static std::atomic<uint32_t> g_oemShortcutEdgeEpoch{0};
static json fixtureInput;
static json ymSettingsSection(const char*){return fixtureInput;}
// In-memory settings seam; the production getter is checked by its real Win32 leaf.
static json ymSettingsFields(const char* section, std::initializer_list<const char*> fields) {
    const auto input = ymSettingsSection(section);
    json selected = json::object();
    if (input.is_object()) for (const auto* key : fields) {
        const auto value = input.find(key);
        if (value != input.end()) selected[key] = *value;
    }
    return selected;
}
static bool nativeInputShortcutDesktopKey(const std::string&,WORD&);
static bool nativeKeyboardShortcutMaskBit(const std::string& code, uint64_t& bit) {
    static const std::pair<const char*, uint64_t> entries[] = {
        {"a", XINPUT_GAMEPAD_A}, {"b", XINPUT_GAMEPAD_B},
        {"x", XINPUT_GAMEPAD_X}, {"y", XINPUT_GAMEPAD_Y},
        {"lb", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"rb", XINPUT_GAMEPAD_RIGHT_SHOULDER},
        {"lt", 1ull << 16}, {"rt", 1ull << 17},
        {"back", XINPUT_GAMEPAD_BACK}, {"start", XINPUT_GAMEPAD_START},
        {"leftThumb", XINPUT_GAMEPAD_LEFT_THUMB}, {"rightThumb", XINPUT_GAMEPAD_RIGHT_THUMB},
        {"dpadUp", XINPUT_GAMEPAD_DPAD_UP}, {"dpadDown", XINPUT_GAMEPAD_DPAD_DOWN},
        {"dpadLeft", XINPUT_GAMEPAD_DPAD_LEFT}, {"dpadRight", XINPUT_GAMEPAD_DPAD_RIGHT},
        // ── 掌机专用键（OEM，L2 消费端，2026-09-16）────────────────────────
        // 高位空间（bit20+，与手柄位不重叠）；keyId 对齐 native 目录
        // （oem.keys.get / kRogKeys 等）与前端 {source:'oem', code:keyId}。
        // bit20-31：背键与首轮机型（ROG/MSI/AYANEO）；bit32+：GPDWin5/OneX/
        // Legion/AOKZOE/Ayn 目录键（同一逻辑键跨机型共享位——同一时刻仅单机型
        // 采集器在跑）。HID/WMI 通道键（r4/l4/gamepad/keyboard/home/legionr/
        // legionl/special/turbo/special2）由各自采集器落地后置位。
        {"m1", 1ull << 20}, {"m2", 1ull << 21},
        {"cc", 1ull << 22}, {"ac", 1ull << 23}, {"lib", 1ull << 24},
        {"claw", 1ull << 25}, {"qs", 1ull << 26},
        // AYANEO 专有键（L2 2026-09-16）：CE/KUN Custom Key 4 键 + KUN T + Guide。
        {"ckbig", 1ull << 27}, {"cksmall", 1ull << 28},
        {"cktl", 1ull << 29}, {"cktr", 1ull << 30}, {"ayat", 1ull << 31},
        // GPDWin5 / OneX / Legion 目录键（L2 2026-09-16 补齐位空间）
        {"r4", 1ull << 32}, {"l4", 1ull << 33}, {"gamepad", 1ull << 34},
        {"keyboard", 1ull << 35}, {"home", 1ull << 36},
        {"legionr", 1ull << 37}, {"legionl", 1ull << 38},
        {"special", 1ull << 39}, {"turbo", 1ull << 40}, {"special2", 1ull << 41},
        // AOKZOE / Ayn 专有键（L2 2026-09-16）
        {"homelp", 1ull << 42}, {"homekb", 1ull << 43}, {"hometb", 1ull << 44},
        {"guide", 1ull << 45}, {"lcc", 1ull << 46},
        // Zotac / AYANEO 扩展 / OneXPlayer 变体（L2 2026-09-16 全机型普查）
        {"zotac", 1ull << 47}, {"dots", 1ull << 48}, {"screen", 1ull << 49},
        {"win", 1ull << 50}, {"esc", 1ull << 51},
        {"orange", 1ull << 52}, {"function", 1ull << 53},
        {"orangekb", 1ull << 54}, {"orangetb", 1ull << 55}, {"orangelp", 1ull << 56},
        // Legion Go 背键扩位（W7 第 4 批，2026-09-20）：HC 对外 6 背键（LegionController.cs
        // :44-54 BackEnum），其余 4 键（Y2/Y3/M2/M3）此前无位 —— 由审计 no-bit 抓出并补齐。
        {"legiony2", 1ull << 57}, {"legiony3", 1ull << 58},
        {"legionm2", 1ull << 59}, {"legionm3", 1ull << 60},
        {"legionfrontl", 1ull << 61}, {"legionfrontr", 1ull << 62}, {"gkey", 1ull << 63},
    };
    for (const auto& entry : entries) {
        if (code == entry.first) {
            bit = entry.second;
            return true;
        }
    }
    return false;
}
static ULONGLONG nativeKeyboardShortcutDuration(
    const json& rule,
    const char* key,
    ULONGLONG fallback,
    ULONGLONG minimum,
    ULONGLONG maximum) {
    const auto it = rule.find(key);
    if (it == rule.end() || !it->is_number()) return fallback;
    const double raw = it->get<double>();
    if (!std::isfinite(raw)) return fallback;
    return static_cast<ULONGLONG>((std::max)(
        static_cast<double>(minimum),
        (std::min)(static_cast<double>(maximum), std::round(raw))));
}
static bool nativeKeyboardShortcutRequiredMask(const json& rawInputs, uint64_t& mask) {
    if (!rawInputs.is_array() || rawInputs.empty() || rawInputs.size() > 4) return false;
    mask = 0;
    for (const auto& raw : rawInputs) {
        std::string code;
        if (raw.is_string()) {
            code = raw.get<std::string>();
        } else if (raw.is_object()) {
            const auto source = raw.find("source");
            const auto value = raw.find("code");
            if (source == raw.end() || value == raw.end() || !source->is_string() || !value->is_string())
                return false;
            // 源：controller（手柄按钮）或 oem（掌机专用键 keyId；L2 消费端
            // 2026-09-16——前端 {source:'oem', code:keyId}，位空间见
            // nativeKeyboardShortcutMaskBit 高位段）。
            const std::string sourceStr = source->get<std::string>();
            if (sourceStr != "controller" && sourceStr != "oem") return false;
            code = value->get<std::string>();
        } else {
            return false;
        }
        uint64_t bit = 0;
        if (!nativeKeyboardShortcutMaskBit(code, bit) || (mask & bit) != 0) return false;
        if (raw.is_object() && ((raw["source"] == "oem") != ((bit >> 20) != 0))) return false;
        mask |= bit;
    }
    return mask != 0;
}
static bool nativeInputShortcutDesktopKey(const std::string& code, WORD& key) {
    if (code.size() == 4 && code.compare(0, 3, "Key") == 0 && code[3] >= 'A' && code[3] <= 'Z') { key = code[3]; return true; }
    if (code.size() == 6 && code.compare(0, 5, "Digit") == 0 && code[5] >= '0' && code[5] <= '9') { key = code[5]; return true; }
    if (code.size() >= 2 && code.size() <= 3 && code[0] == 'F') {
        for (int index = 1; index <= 24; ++index) if (code == "F" + std::to_string(index)) { key = static_cast<WORD>(VK_F1 + index - 1); return true; }
    }
    static const std::pair<const char*, WORD> keys[] = {
        {"ControlLeft", VK_LCONTROL}, {"ControlRight", VK_RCONTROL}, {"Control", VK_CONTROL},
        {"ShiftLeft", VK_LSHIFT}, {"ShiftRight", VK_RSHIFT}, {"Shift", VK_SHIFT},
        {"AltLeft", VK_LMENU}, {"AltRight", VK_RMENU}, {"Alt", VK_MENU},
        {"MetaLeft", VK_LWIN}, {"MetaRight", VK_RWIN}, {"Meta", VK_LWIN},
        {"ArrowUp", VK_UP}, {"ArrowDown", VK_DOWN}, {"ArrowLeft", VK_LEFT}, {"ArrowRight", VK_RIGHT},
        {"ContextMenu", VK_APPS}, {"Tab", VK_TAB}, {"Enter", VK_RETURN}, {"Escape", VK_ESCAPE}, {"Space", VK_SPACE},
        {"Backspace", VK_BACK}, {"Delete", VK_DELETE}, {"Insert", VK_INSERT},
        {"Home", VK_HOME}, {"End", VK_END}, {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT},
        {"CapsLock", VK_CAPITAL}, {"NumLock", VK_NUMLOCK}, {"ScrollLock", VK_SCROLL},
        {"PrintScreen", VK_SNAPSHOT}, {"Pause", VK_PAUSE},
        {"BracketLeft", VK_OEM_4}, {"BracketRight", VK_OEM_6}, {"Backslash", VK_OEM_5},
        {"Semicolon", VK_OEM_1}, {"Quote", VK_OEM_7}, {"Comma", VK_OEM_COMMA}, {"Period", VK_OEM_PERIOD}, {"Slash", VK_OEM_2},
        {"Numpad0", VK_NUMPAD0}, {"Numpad1", VK_NUMPAD1}, {"Numpad2", VK_NUMPAD2}, {"Numpad3", VK_NUMPAD3}, {"Numpad4", VK_NUMPAD4},
        {"Numpad5", VK_NUMPAD5}, {"Numpad6", VK_NUMPAD6}, {"Numpad7", VK_NUMPAD7}, {"Numpad8", VK_NUMPAD8}, {"Numpad9", VK_NUMPAD9},
        {"NumpadAdd", VK_ADD}, {"NumpadSubtract", VK_SUBTRACT}, {"NumpadMultiply", VK_MULTIPLY}, {"NumpadDivide", VK_DIVIDE}, {"NumpadDecimal", VK_DECIMAL}, {"NumpadEnter", VK_RETURN},
        {"Backquote", VK_OEM_3}, {"Minus", VK_OEM_MINUS}, {"Equal", VK_OEM_PLUS},
        {"MouseLeft", VK_LBUTTON}, {"MouseRight", VK_RBUTTON}, {"MouseMiddle", VK_MBUTTON},
        {"MouseBack", VK_XBUTTON1}, {"MouseForward", VK_XBUTTON2},
    };
    for (const auto& entry : keys) if (code == entry.first) { key = entry.second; return true; }
    return false;
}
static bool nativeInputShortcutDesktopKeys(const json& inputs, std::vector<WORD>& keys) {
    if (!inputs.is_array() || inputs.empty() || inputs.size() > 4) return false;
    for (const auto& input : inputs) {
        if (!input.is_object()) return false;
        const auto source = ymcc::inputShortcutString(input, "source");
        const auto code = ymcc::inputShortcutString(input, "code");
        if ((source != "keyboard" && source != "mouse") ||
            (source == "mouse") != (code.compare(0, 5, "Mouse") == 0)) return false;
        WORD key = 0;
        if (!nativeInputShortcutDesktopKey(code, key) || std::find(keys.begin(), keys.end(), key) != keys.end()) return false;
        keys.push_back(key);
    }
    return true;
}
static bool nativeKeyboardShortcutVirtualKey(
    const std::string& actionId,
    const std::string& code,
    WORD& virtualKey,
    bool& requiresShift) {
    requiresShift = false;
    if (actionId == "keyboard.numeric") {
        if (code.rfind("Numpad",0)==0 || code=="NumLock") return nativeInputShortcutDesktopKey(code,virtualKey);
        if (code.size() == 6 && code.rfind("Digit", 0) == 0 && code[5] >= '0' && code[5] <= '9') {
            virtualKey = static_cast<WORD>('0' + (code[5] - '0'));
            return true;
        }
        if (code.size() >= 2 && code[0] == 'F') {
            char* end = nullptr;
            const long functionKey = std::strtol(code.c_str() + 1, &end, 10);
            if (end && *end == '\0' && functionKey >= 1 && functionKey <= 24) {
                virtualKey = static_cast<WORD>(VK_F1 + functionKey - 1);
                return true;
            }
        }
        return false;
    }
    if (actionId == "keyboard.letter") {
        if (code.size() == 4 && code.rfind("Key", 0) == 0 && code[3] >= 'A' && code[3] <= 'Z') {
            virtualKey = static_cast<WORD>(code[3]);
            return true;
        }
        return false;
    }
    if (actionId == "keyboard.symbol") {
        requiresShift = true;
        if (code == "Backquote") { virtualKey = VK_OEM_3; return true; }
        if (code.size() == 6 && code.rfind("Digit", 0) == 0 && code[5] >= '0' && code[5] <= '9') {
            virtualKey = static_cast<WORD>('0' + (code[5] - '0'));
            return true;
        }
        if (code == "Minus") { virtualKey = VK_OEM_MINUS; return true; }
        if (code == "Equal") { virtualKey = VK_OEM_PLUS; return true; }
        if (code=="BracketLeft"||code=="BracketRight"||code=="Backslash"||code=="Semicolon"||code=="Quote"||code=="Comma"||code=="Period"||code=="Slash") return nativeInputShortcutDesktopKey(code,virtualKey);
        return false;
    }
    if (actionId == "keyboard.leftFunction") {
        if(code=="Tab"||code=="Space"||code=="Backquote"||code=="Minus"||code=="Equal"||code=="BracketLeft"||code=="BracketRight"||code=="Backslash"||code=="Semicolon"||code=="Quote"||code=="Comma"||code=="Period"||code=="Slash") return nativeInputShortcutDesktopKey(code,virtualKey);
        if (code == "ShiftLeft") { virtualKey = VK_LSHIFT; return true; }
        if (code == "AltLeft") { virtualKey = VK_LMENU; return true; }
        if (code == "Enter") { virtualKey = VK_RETURN; return true; }
        if (code == "MetaLeft") { virtualKey = VK_LWIN; return true; }
        if (code == "CapsLock") { virtualKey = VK_CAPITAL; return true; }
        if (code == "Escape") { virtualKey = VK_ESCAPE; return true; }
        if (code == "ControlLeft") { virtualKey = VK_LCONTROL; return true; }
        if (code == "Backspace") { virtualKey = VK_BACK; return true; }
        return false;
    }
    if (actionId == "keyboard.rightFunction") {
        if(code=="ControlRight"||code=="ShiftRight"||code=="AltRight"||code=="MetaRight"||code=="ContextMenu"||code.rfind("Arrow",0)==0) return nativeInputShortcutDesktopKey(code,virtualKey);
        if (code == "PrintScreen") { virtualKey = VK_SNAPSHOT; return true; }
        if (code == "ScrollLock") { virtualKey = VK_SCROLL; return true; }
        if (code == "Pause") { virtualKey = VK_PAUSE; return true; }
        if (code == "Insert") { virtualKey = VK_INSERT; return true; }
        if (code == "Home") { virtualKey = VK_HOME; return true; }
        if (code == "PageUp") { virtualKey = VK_PRIOR; return true; }
        if (code == "Delete") { virtualKey = VK_DELETE; return true; }
        if (code == "End") { virtualKey = VK_END; return true; }
        if (code == "PageDown") { virtualKey = VK_NEXT; return true; }
        return false;
    }
    return false;
}
static DWORD nativeKeyboardShortcutInputFlags(WORD virtualKey) {
    switch (virtualKey) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT: case VK_SNAPSHOT:
    case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
    case VK_RCONTROL: case VK_RMENU: case VK_NUMLOCK: case VK_DIVIDE:
        return KEYEVENTF_EXTENDEDKEY;
    default:
        return 0;
    }
}
static bool nativeKeyboardShortcutAllInputs(const json& inputs, uint64_t& mask, std::vector<WORD>& keys) {
    mask = 0; keys.clear();
    if (!inputs.is_array() || inputs.empty() || inputs.size() > 4) return false;
    json physical = json::array(), desktop = json::array();
    for (const auto& input : inputs) {
        if (input.is_string()) physical.push_back(input);
        else if (input.is_object()) {
            const auto source = ymcc::inputShortcutString(input, "source");
            if (source == "controller" || source == "oem") physical.push_back(input);
            else if (source == "keyboard" || source == "mouse") desktop.push_back(input);
            else return false;
        } else return false;
    }
    return (physical.empty() || nativeKeyboardShortcutRequiredMask(physical, mask)) &&
        (desktop.empty() || nativeInputShortcutDesktopKeys(desktop, keys));
}
static bool nativeYmccShortcutAction(const std::string& action) {
    return (ymcc::radial::userActionAvailable(action) && action == "radial.summon") || action == "window.summon" || action == "window.hideToTray" ||
        action == "os.returnDesktop" || action == "game.killCurrent" ||
        action == "os.openTouchKeyboard" || action == "mouse.toggle" ||
        action == "power.tdpAdjust" || action == "display.brightnessAdjust" ||
        action == "input.frontendButton";
}
static bool nativeYmccShortcutDelta(const std::string& action, const json& params, int& delta) {
    delta = 0;
    if (action == "input.frontendButton") {
        const auto button = ymcc::frontendButton(ymcc::inputShortcutString(params, "button"));
        delta = static_cast<int>(button);
        return button != ymcc::FrontendButton::Invalid;
    }
    if (action != "power.tdpAdjust" && action != "display.brightnessAdjust") return true;
    const bool power = action == "power.tdpAdjust";
    const std::string direction = params.value("direction", power ? "up" : "right");
    if (direction != (power ? "up" : "right") && direction != (power ? "down" : "left")) return false;
    int amount = power ? 1 : 5;
    const auto value = params.find("amount");
    if (value != params.end()) {
        if (value->is_number_integer()) amount = value->get<int>();
        else if (value->is_string()) {
            const auto text = value->get<std::string>();
            if (text.empty() || text.size() > 2 || text.find_first_not_of("0123456789") != std::string::npos) return false;
            amount = std::stoi(text);
        } else return false;
    }
    if (amount < 1 || amount > 20) return false;
    delta = (direction == "down" || direction == "left") ? -amount : amount;
    return true;
}
static int nativeShortcutLegacyDefaultSlot(const std::string& key) {
    static const char* keys[] = {"enabled", "bDoubleMinimize", "tdpShortcut", "fpsShortcut",
        "killGame", "openKeyboard", "returnDesktop", "mouseToggle"};
    for (int i = 0; i < 8; ++i) if (key == keys[i]) return i;
    return -1; // Start/F7 already has a rule-engine owner, no legacy dispatcher.
}
static bool nativeShortcutLegacyTemplateMatches(const json& raw, int slot) {
    struct Template { const char* action; const char* trigger; uint64_t mask; };
    static const Template templates[] = {
        {"window.summon", "hold", XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER},
        {"window.hideToTray", "double", XINPUT_GAMEPAD_B},
        {"power.tdpAdjust", "repeat", XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_DPAD_UP},
        {"display.brightnessAdjust", "repeat", XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_DPAD_RIGHT},
        {"game.killCurrent", "hold", XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_B},
        {"os.openTouchKeyboard", "hold", XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_X},
        {"os.returnDesktop", "press", XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_A},
        {"mouse.toggle", "hold", XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_Y}
    };
    if (slot < 0 || slot >= 8) return false;
    const auto& expected = templates[slot];
    uint64_t mask = 0;
    if (ymcc::inputShortcutString(raw, "actionId") != expected.action ||
        ymcc::inputShortcutString(raw, "trigger") != expected.trigger ||
        !raw.contains("inputs") || !nativeKeyboardShortcutRequiredMask(raw["inputs"], mask) || mask != expected.mask)
        return false;
    if ((std::string(expected.trigger) == "hold" && nativeKeyboardShortcutDuration(raw,"holdMs",500,100,10000) != 500) ||
        (std::string(expected.trigger) == "double" && nativeKeyboardShortcutDuration(raw,"doubleWindowMs",500,200,2000) != 500) ||
        (std::string(expected.trigger) == "repeat" &&
            (nativeKeyboardShortcutDuration(raw,"holdMs",500,0,10000) != 0 ||
             nativeKeyboardShortcutDuration(raw,"intervalMs",150,40,2000) != 150))) return false;
    if (slot == 2 || slot == 3) {
        const auto params = raw.find("params");
        int delta = 0;
        if (params == raw.end() || !params->is_object() ||
            !nativeYmccShortcutDelta(expected.action, *params, delta) || delta != (slot == 2 ? 1 : 5)) return false;
        const auto acceleration = params->find("linearAcceleration");
        if (acceleration != params->end() && (!acceleration->is_boolean() || !acceleration->get<bool>())) return false;
    }
    return true;
}
static bool nativeShortcutLegacyDefaultAllowed(const char* key, bool legacyEnabled = true) {
    const int slot = nativeShortcutLegacyDefaultSlot(key);
    return slot >= 0 && (g_nativeShortcutLegacyOverrideMask & (1u << slot)) == 0 &&
        (g_nativeShortcutRulesConfigured || legacyEnabled);
}
static void nativeKeyboardShortcutsRefresh(ULONGLONG now) {
    if (g_nativeKeyboardShortcutLastRefresh != 0 &&
        now - g_nativeKeyboardShortcutLastRefresh < NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS) return;
    g_nativeKeyboardShortcutLastRefresh = now;

    const json input = ymSettingsFields("input", {"revision", "msiControllerShieldEnabled", "buttonMapping"});
    const long long revision = input.is_object() && input.contains("revision") && input["revision"].is_number_integer()
        ? input["revision"].get<long long>()
        : 0;
    static bool radialTestingSeen = false;
    const bool radialTesting = ymcc::radial::userAvailable();
#ifndef YMCC_SHORTCUT_FIXTURE
    static YmccFamily vendorFamilySeen = YmccFamily::Unknown;
    // The explicit MSI controller-only shield is persisted in the input
    // section, but remains an MSI-only policy. Refreshing shortcut rules must
    // not clear it (or touch ROG/GPD/common input paths).
    oemVendorStackShieldRequest(msiControllerShieldRequested(input));
    oemVendorStackSchedule(); // Retry a rejected queue publication; no repeated SCM attempts.
    oemVendorStackRecordingRequest(g_shortcutRecordingActive.load(std::memory_order_acquire));
    if (revision == g_nativeKeyboardShortcutRevision && vendorFamilySeen == g_machineIdentity.family && radialTestingSeen == radialTesting) return;
    vendorFamilySeen = g_machineIdentity.family;
#else
    if (revision == g_nativeKeyboardShortcutRevision && radialTestingSeen == radialTesting) return;
#endif
    radialTestingSeen = radialTesting;

    std::vector<NativeKeyboardShortcutRule> parsed;
    uint32_t legacyOverrides = 0, legacyTemplates = 0;
    bool hasConfiguredShortcuts = false;
    try {
        const auto shortcuts = ymcc::bootModeShortcuts::effectiveRules(input);
        if (shortcuts) {
            hasConfiguredShortcuts = true;
            for (const auto& raw : *shortcuts) {
                if (!raw.is_object()) continue;
                int legacySlot = -1;
                try {
                    // Retire only the tagged historical default; custom F7 rules remain supported.
                    if (ymcc::inputShortcutString(raw,"origin") == "native-default" &&
                        ymcc::inputShortcutString(raw,"defaultKey") == "startDoubleF7") continue;
                    const bool enabled = !raw.contains("enabled") || !raw["enabled"].is_boolean() || raw["enabled"].get<bool>();
                    legacySlot = ymcc::inputShortcutString(raw,"origin") == "native-default"
                        ? nativeShortcutLegacyDefaultSlot(ymcc::inputShortcutString(raw,"defaultKey")) : -1;
                    const bool legacyTemplate = nativeShortcutLegacyTemplateMatches(raw, legacySlot);
                    // Only an explicitly saved, enabled original template owns
                    // its legacy branch. A damaged or edited sibling cannot veto it.
                    if (legacySlot >= 0 && enabled && legacyTemplate) legacyTemplates |= 1u << legacySlot;
                    if (!enabled || legacyTemplate) continue;
                    const auto action = raw.find("actionId");
                    const auto inputs = raw.find("inputs");
                    const auto params = raw.find("params");
                    if (action == raw.end() || inputs == raw.end() || params == raw.end() ||
                        !action->is_string() || !params->is_object()) continue;
                    const std::string actionId = action->get<std::string>();
                    if (!ymcc::radial::userActionAvailable(actionId)) continue;
                    NativeKeyboardShortcutRule rule;
                    if (ymcc::inputShortcutAction(actionId)) {
                        const auto preset = params->find("preset");
                        if (preset == params->end() || !preset->is_string() ||
                            !ymcc::inputShortcutPreset(actionId, preset->get<std::string>())) continue;
                        if (!nativeKeyboardShortcutAllInputs(*inputs, rule.requiredMask, rule.desktopKeys)) continue;
                        rule.inputAction = actionId; rule.preset = preset->get<std::string>();
                    } else if (ymcc::shortcutUiAction(actionId)) {
                        if (!nativeKeyboardShortcutAllInputs(*inputs, rule.requiredMask, rule.desktopKeys) ||
                            !ymcc::shortcutUiParams(actionId, *params, rule.uiParams)) continue;
                        rule.uiAction = actionId;
                    } else if (nativeYmccShortcutAction(actionId)) {
                        if (!nativeKeyboardShortcutAllInputs(*inputs, rule.requiredMask, rule.desktopKeys)) continue;
                        if (!nativeYmccShortcutDelta(actionId, *params, rule.actionDelta)) continue;
                        rule.ownAction = actionId;
                        if (actionId == "power.tdpAdjust" || actionId == "display.brightnessAdjust") {
                            const auto acceleration = params->find("linearAcceleration");
                            if (acceleration != params->end() && !acceleration->is_boolean()) continue;
                            rule.linearAcceleration = acceleration == params->end() || acceleration->get<bool>();
                        }
                    } else {
                        const auto key = params->find("key");
                        if (key == params->end() || !key->is_string() ||
                            !nativeKeyboardShortcutAllInputs(*inputs, rule.requiredMask, rule.desktopKeys) ||
                            !nativeKeyboardShortcutVirtualKey(actionId, key->get<std::string>(), rule.virtualKey, rule.requiresShift)) continue;
                        rule.forceExtendedKey = key->get<std::string>() == "NumpadEnter";
                        if (!ymcc::keyboardSequence::parse(*params, actionId, nativeKeyboardShortcutVirtualKey, rule.keyboardPlan)) continue;
                        rule.outputId = raw.value("id", actionId + ":" + key->get<std::string>());
                    }
                    rule.trigger = raw.value("trigger", "press");
                    if (rule.trigger != "press" && rule.trigger != "hold" && rule.trigger != "release" &&
                        rule.trigger != "double" && rule.trigger != "repeat") continue;
                    if (rule.keyboardPlan.toggle && rule.trigger == "repeat") continue;
                    rule.holdMs = nativeKeyboardShortcutDuration(raw, "holdMs", 500, rule.trigger == "repeat" ? 0 : 100, 10000);
                    rule.doubleWindowMs = nativeKeyboardShortcutDuration(raw, "doubleWindowMs", 500, 200, 2000);
                    rule.intervalMs = nativeKeyboardShortcutDuration(raw, "intervalMs", 150, 40, 2000);
                    parsed.push_back(rule);
                } catch (...) {
                    // A damaged row must neither discard its siblings nor restore its legacy binding.
                }
            }
        }
    } catch (...) {
        // Preserve independently parsed rows if an unrelated container is malformed.
    }
    // An explicit rule list replaces the hidden defaults, including an empty
    // list or a custom replacement without origin/defaultKey. Only a present,
    // enabled and pristine default row may delegate to its legacy handler.
    // Missing configuration still uses the original first-run defaults.
    if (hasConfiguredShortcuts) legacyOverrides = 0xffu & ~legacyTemplates;
    g_nativeKeyboardShortcuts = std::move(parsed);
    g_nativeShortcutLegacyOverrideMask = legacyOverrides;
    g_nativeShortcutRulesConfigured = hasConfiguredShortcuts;
#ifndef YMCC_SHORTCUT_FIXTURE
    bool wantedVendorStack = false;
    for (const auto& rule : g_nativeKeyboardShortcuts)
        if (oemVendorStackRuleCanTakeover(g_machineIdentity.family, rule.requiredMask,
                rule.desktopKeys.empty(), rule.trigger)) { wantedVendorStack = true; break; }
    oemVendorStackRequest(wantedVendorStack);
#endif
    g_nativeKeyboardShortcutRevision = revision;
    g_oemShortcutEdgeEpoch.fetch_add(1, std::memory_order_acq_rel);
    // A newly saved mapping must never fire against a chord held before the
    // parser saw that revision. The next neutral sample arms it normally.
    // Each source class waits for its own neutral sample; a held gamepad button
    // cannot prevent a newly saved independent OEM/desktop rule from arming.
}
static std::set<int> desktopDown;
static SHORT fixtureGetAsyncKeyState(int key){return desktopDown.count(key)?SHORT(0x8000):0;}
#define GetAsyncKeyState fixtureGetAsyncKeyState
static void appendNativeLifecycleLog(const char*,const json&){}

namespace ymcc::keyboardEvents {enum class SourceId{NativeKeyboardShortcut};}
struct OemProducerTokens{};
static constexpr uint64_t kOemShortcutUnboundEpoch=0;
static std::atomic<bool> fixtureWindowCurrent{true};
static std::atomic<bool> g_exitRequested{false},g_shortcutRecordingActive{false},g_inputReady{true},fixtureProducerCurrent{true};
static std::atomic<uint64_t> fixturePowerGeneration{1};
static uint64_t currentPowerGeneration(){return fixturePowerGeneration.load();}
static bool sgVirtualWriterAllowedNow(){return true;}
static bool radialHasInputOwner(){return false;}
static bool oemProducerTokensCurrent(OemProducerTokens){return fixtureProducerCurrent.load();}
static bool msiWmiGenerationCurrent(uint64_t){return true;}
static bool msiWmiShortcutAuthoritative(){return false;}
static HWND fixtureForegroundWindow(){return reinterpret_cast<HWND>(uintptr_t(fixtureWindowCurrent.load()?42:43));}
#define GetForegroundWindow fixtureForegroundWindow
static std::mutex sequenceTraceMx;static std::condition_variable sequenceTraceCv;
struct SequenceTrace {WORD vk;bool up;uint64_t at;bool extended=false;};static std::vector<SequenceTrace> sequenceTrace;
static std::atomic<bool> fixtureRejectKeyUps{false};static std::atomic<unsigned> fixtureRejectedKeyUps{0};
static UINT keyboardSelfSendWithEvidence(ymcc::keyboardEvents::SourceId,UINT requested,INPUT* inputs){
 if(fixtureRejectKeyUps.load()&&requested&&(inputs[0].ki.dwFlags&KEYEVENTF_KEYUP)){fixtureRejectedKeyUps.fetch_add(1);sequenceTraceCv.notify_all();return 0;}
 std::lock_guard<std::mutex> lock(sequenceTraceMx);for(UINT i=0;i<requested;++i)sequenceTrace.push_back({inputs[i].ki.wVk,(inputs[i].ki.dwFlags&KEYEVENTF_KEYUP)!=0,GetTickCount64(),(inputs[i].ki.dwFlags&KEYEVENTF_EXTENDEDKEY)!=0});sequenceTraceCv.notify_all();return requested;
}
enum class SerialJobKind{ExternalInjection};
static bool nativeYmccShortcutSubmit(std::function<void()> job,SerialJobKind,uint64_t,uint64_t,uint64_t,bool,OemProducerTokens){job();return true;}
#include "keyboard_shortcut_sequence_runtime.h"

static int emissions=0;
static void tick(uint64_t controllerMask,uint64_t oemMask,ULONGLONG now){for(auto& rule:g_nativeKeyboardShortcuts){const bool oemRule=(rule.requiredMask>>20)!=0;
        const uint64_t currentMask = (oemRule ? oemMask : 0) |
            ((rule.requiredMask & ((1ull << 20) - 1)) != 0 ? controllerMask : 0);
        bool matched = currentMask == rule.requiredMask;
        bool neutral = currentMask == 0;
        if (!rule.desktopKeys.empty()) {
            bool any = false;
            for (const auto key : rule.desktopKeys) {
                const bool down = !nativeKeyboardSequenceOwnsKey(key) && (GetAsyncKeyState(key) & 0x8000) != 0;
                any = any || down; matched = matched && down;
            }
            neutral = neutral && !any;
        }
        if (rule.needsNeutral) {
            if (neutral) rule.needsNeutral = false;
            else if (oemRule && !rule.matchedLastTick) {
                appendNativeLifecycleLog("oem-shortcut-neutral-required", {{"mask", currentMask}, {"requiredMask", rule.requiredMask}});
            }
            // Remember blockage only to avoid a log every held sample. No edge
            // or hold/release action is inherited across a rule revision.
            rule.matchedLastTick = !neutral;
            rule.holdFired = false; rule.holdStartedAt = 0;
            if (!rule.needsNeutral) rule.matchedLastTick = false;
            continue;
        }
        const bool pressed = matched && !rule.matchedLastTick;
        bool emit = false;
        if (rule.trigger == "press") {
            emit = pressed;
        } else if (rule.trigger == "release") {
            emit = !matched && rule.matchedLastTick;
        } else if (rule.trigger == "double") {
            if (pressed) {
                emit = rule.lastPressAt != 0 && now - rule.lastPressAt <= rule.doubleWindowMs;
                rule.lastPressAt = emit ? 0 : now;
            }
        } else if (rule.trigger == "hold") {
            if (!matched) {
                rule.holdStartedAt = 0;
                rule.holdFired = false;
            } else {
                if (pressed) rule.holdStartedAt = now;
                if (!rule.holdFired && rule.holdStartedAt != 0 && now - rule.holdStartedAt >= rule.holdMs) {
                    rule.holdFired = true;
                    emit = true;
                }
            }
        } else if (rule.trigger == "repeat") {
            if (!matched) {
                rule.holdStartedAt = 0;
                rule.lastEmitAt = 0;
            } else {
                if (pressed) rule.holdStartedAt = now;
                ULONGLONG interval = rule.intervalMs;
                const ULONGLONG heldMs = now - rule.holdStartedAt;
                if (rule.linearAcceleration && heldMs > 500) {
                    const ULONGLONG decrease = (heldMs - 500) / 400 * 15;
                    interval = decrease >= interval - 40 ? 40 : interval - decrease;
                }
                if (rule.holdStartedAt != 0 && heldMs >= rule.holdMs &&
                    (rule.lastEmitAt == 0 || now - rule.lastEmitAt >= interval)) {
                    rule.lastEmitAt = now;
                    emit = true;
                }
            }
        }

if(emit)++emissions;rule.matchedLastTick=matched;
}}

static std::array<int,8> legacyCounts{};
static ULONGLONG fixtureNow=0;
static ULONGLONG fixtureTickCount(){return fixtureNow;}
#define GetTickCount64 fixtureTickCount
#define IsWindow(handle) true
#define IsWindowVisible(handle) true
static HWND g_hwnd=reinterpret_cast<HWND>(1);
struct FocusTargetSnapshot{};static FocusTargetSnapshot g_pendingSummonTarget;
static bool focusMainWindowIsForeground(){return false;}
static FocusTargetSnapshot focusCaptureForegroundTarget(){return {};}
static void nativeYmccShortcutSummon(const FocusTargetSnapshot&){++legacyCounts[0];}
static void focusBeginReturnToPreviousWindow(ULONGLONG){++legacyCounts[1];}
static void nativeAdjustTdp(int){++legacyCounts[2];}
static void nativeAdjustBrightness(int){++legacyCounts[3];}
static void runKillBat(){++legacyCounts[4];}
static void openTouchKeyboard(){++legacyCounts[5];}
static void returnToDesktop(){++legacyCounts[6];}
static void toggleMouseMode(const std::string&){++legacyCounts[7];}
template<class F>static void gamepadSerialSubmit(F job){job();}
static std::mutex g_gamepadSettingsMx;static std::string g_mouseBackend="fixture";
static bool g_summonEnabled=true,g_bDoubleMinimize=true,g_tdpShortcut=true,g_fpsShortcut=true,g_killGame=true,g_openKeyboard=true,g_returnDesktop=true,g_mouseToggle=true;
static WORD g_prevW=0;
static ULONGLONG gpHoldStart=0,gpLastBPress=0,g_bClosePendingSince=0,g_bCloseReadyAt=0,s_dpHeldStart=0,s_dpLastEmit=0,s_brightnessHeldStart=0,s_brightnessLastEmit=0,gpKbHoldStart=0,gpKxHoldStart=0,gpKyHoldStart=0;
static bool gpArmed=true,gpPrevB=false,g_bClosePending=false,gpKbArmed=true,gpKxArmed=true,gpKyArmed=true;
static void allLegacyTick(WORD w,ULONGLONG now){fixtureNow=now;constexpr ULONGLONG HOLD_MS=500,B_DOUBLE_MS=500;
const bool both=(w&(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER))==(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER),startHeld=w&XINPUT_GAMEPAD_START,selectHeld=w&XINPUT_GAMEPAD_BACK,bPressed=w&XINPUT_GAMEPAD_B,xPressed=w&XINPUT_GAMEPAD_X,yPressed=w&XINPUT_GAMEPAD_Y,aPressed=w&XINPUT_GAMEPAD_A,dpUp=w&XINPUT_GAMEPAD_DPAD_UP,dpDown=w&XINPUT_GAMEPAD_DPAD_DOWN,dpLeft=w&XINPUT_GAMEPAD_DPAD_LEFT,dpRight=w&XINPUT_GAMEPAD_DPAD_RIGHT;
    const bool shoulderDown = (w & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0;
    const bool shoulderWasDown = (g_prevW & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0;
    // 在首个肩键刚按下时就记录原前台，防止 Steam/Game Bar/LS 在0.5秒等待期内抢走前台。
    if (nativeShortcutLegacyDefaultAllowed("enabled", g_summonEnabled) && shoulderDown && !shoulderWasDown && !focusMainWindowIsForeground())
        g_pendingSummonTarget = focusCaptureForegroundTarget();

    // Saving a replacement cannot leave a previous legacy double-B pending.
    if (!nativeShortcutLegacyDefaultAllowed("bDoubleMinimize")) {
        gpLastBPress = 0;
        g_bClosePending = false;
        g_bClosePendingSince = g_bCloseReadyAt = 0;
    }

    // ── 双击 B → 等待第二次B释放后隐藏并回焦，避免B键泄漏到游戏 ──
    if (bPressed && !gpPrevB && !g_bClosePending) {
        const bool canClose = nativeShortcutLegacyDefaultAllowed("bDoubleMinimize", g_bDoubleMinimize) &&
                              g_hwnd && IsWindow(g_hwnd) && IsWindowVisible(g_hwnd);
        if (canClose && gpLastBPress != 0 && (now - gpLastBPress) <= B_DOUBLE_MS) {
            gpLastBPress = 0;
            g_bClosePending = true;
            g_bClosePendingSince = now;
            g_bCloseReadyAt = 0;
        } else if (canClose) {
            gpLastBPress = now;
        } else {
            gpLastBPress = 0;
        }
    }
    gpPrevB = bPressed;

    if (g_bClosePending) {
        // This is a global shortcut.  Do not cancel it merely because another
        // window became foreground while the second B is being released.
        // The old foreground gate made the shortcut depend on page/window
        // focus and caused an occasional missed minimize.
        if (!bPressed) {
            if (g_bCloseReadyAt == 0) g_bCloseReadyAt = now + 60;
            if (now >= g_bCloseReadyAt) {
                g_bClosePending = false;
                g_bClosePendingSince = 0;
                g_bCloseReadyAt = 0;
                focusBeginReturnToPreviousWindow(now);
            }
        } else if (now - g_bClosePendingSince >= 1500) {
            // 坏键/长按保护：不能无限阻塞隐藏动作。
            g_bClosePending = false;
            g_bClosePendingSince = 0;
            g_bCloseReadyAt = 0;
            focusBeginReturnToPreviousWindow(now);
        }
    }

    // ── 按住 LB+RB 0.5s → 强制呼出/抢焦（任意窗口态，全局免点击）──
    if (both && nativeShortcutLegacyDefaultAllowed("enabled")) {
        if (gpHoldStart == 0) gpHoldStart = now;
        if (gpArmed && nativeShortcutLegacyDefaultAllowed("enabled", g_summonEnabled) && g_hwnd && (now - gpHoldStart) >= HOLD_MS) {
            gpArmed = false;
            const FocusTargetSnapshot summonTarget = g_pendingSummonTarget;
            nativeYmccShortcutSummon(summonTarget);
        }
    } else {
        gpHoldStart = 0;
        if (!shoulderDown && gpArmed) g_pendingSummonTarget = {};
        gpArmed = true;
    }

    // ── Start + 方向键 全局快捷调节（TDP / RTSS 锁帧），按住自动连发 ──
    // 速度参数需与 src/gamepad/engine.ts 的 SLIDER_REPEAT_* / SLIDER_ACCEL_* 保持一致。
    if (startHeld) {
        if (nativeShortcutLegacyDefaultAllowed("tdpShortcut", g_tdpShortcut) && (dpUp || dpDown)) {
            int delta = dpUp ? 1 : -1;
            ULONGLONG nowMs = GetTickCount64();
            if (s_dpHeldStart == 0) { s_dpHeldStart = nowMs; s_dpLastEmit = nowMs; nativeAdjustTdp(delta); }
            else {
                ULONGLONG heldMs = nowMs - s_dpHeldStart;
                ULONGLONG interval = 150;
                if (heldMs > 500) { ULONGLONG dec = (heldMs - 500) / 400 * 15; interval = dec >= 110 ? 40 : 150 - dec; }
                if (nowMs - s_dpLastEmit >= interval) { s_dpLastEmit = nowMs; nativeAdjustTdp(delta); }
            }
        } else { s_dpHeldStart = 0; }
        if (nativeShortcutLegacyDefaultAllowed("fpsShortcut", g_fpsShortcut) && (dpRight || dpLeft)) {
            int dir = dpRight ? 1 : -1;
            ULONGLONG nowMs = GetTickCount64();
            if (s_brightnessHeldStart == 0) { s_brightnessHeldStart = nowMs; s_brightnessLastEmit = nowMs; nativeAdjustBrightness(dir); }
            else {
                ULONGLONG heldMs = nowMs - s_brightnessHeldStart;
                ULONGLONG interval = 150;
                if (heldMs > 500) { ULONGLONG dec = (heldMs - 500) / 400 * 15; interval = dec >= 110 ? 40 : 150 - dec; }
                if (nowMs - s_brightnessLastEmit >= interval) { s_brightnessLastEmit = nowMs; nativeAdjustBrightness(dir); }
            }
        } else { s_brightnessHeldStart = 0; }
    } else {
        s_dpHeldStart = 0;
        s_brightnessHeldStart = 0;
    }

    // ── 选择(Back) + B 长按 0.5s → 结束当前游戏 ──
    if (selectHeld && bPressed && nativeShortcutLegacyDefaultAllowed("killGame")) {
        if (gpKbHoldStart == 0) gpKbHoldStart = now;
        if (gpKbArmed && nativeShortcutLegacyDefaultAllowed("killGame", g_killGame) && (now - gpKbHoldStart) >= HOLD_MS) {
            gpKbArmed = false;
            gamepadSerialSubmit([] { runKillBat(); });
        }
    } else { gpKbHoldStart = 0; gpKbArmed = true; }

    // ── 选择(Back) + X 长按 0.5s → 打开 Windows 触摸键盘 ──
    if (selectHeld && xPressed && nativeShortcutLegacyDefaultAllowed("openKeyboard")) {
        if (gpKxHoldStart == 0) gpKxHoldStart = now;
        if (gpKxArmed && nativeShortcutLegacyDefaultAllowed("openKeyboard", g_openKeyboard) && (now - gpKxHoldStart) >= HOLD_MS) {
            gpKxArmed = false;
            gamepadSerialSubmit([] { openTouchKeyboard(); });
        }
    } else { gpKxHoldStart = 0; gpKxArmed = true; }

    // ── 选择(Back) + A 组合按下瞬间 → 返回桌面（只在组合刚建立时触发一次） ──
    const bool comboNow = selectHeld && aPressed;
    const bool comboPrev = (g_prevW & XINPUT_GAMEPAD_BACK) && (g_prevW & XINPUT_GAMEPAD_A);
    if (nativeShortcutLegacyDefaultAllowed("returnDesktop", g_returnDesktop) && comboNow && !comboPrev) {
        returnToDesktop();
    }

    // ── 选择(Back) + Y 长按 0.5s → 模拟鼠标开/关 ──
    if (selectHeld && yPressed && nativeShortcutLegacyDefaultAllowed("mouseToggle")) {
        if (gpKyHoldStart == 0) gpKyHoldStart = now;
        if (gpKyArmed && nativeShortcutLegacyDefaultAllowed("mouseToggle", g_mouseToggle) && (now - gpKyHoldStart) >= HOLD_MS) {
            gpKyArmed = false;
            std::string backend;
            {
                std::lock_guard<std::mutex> settingsLock(g_gamepadSettingsMx);
                backend = g_mouseBackend;
            }
            gamepadSerialSubmit([backend] { toggleMouseMode(backend); });
        }
    } else { gpKyHoldStart = 0; gpKyArmed = true; }

g_prevW=w;
}
static std::array<int,8> replayAllLegacy(){legacyCounts.fill(0);g_prevW=0;gpHoldStart=gpLastBPress=g_bClosePendingSince=g_bCloseReadyAt=s_dpHeldStart=s_dpLastEmit=s_brightnessHeldStart=s_brightnessLastEmit=gpKbHoldStart=gpKxHoldStart=gpKyHoldStart=0;gpArmed=gpKbArmed=gpKxArmed=gpKyArmed=true;gpPrevB=g_bClosePending=false;
const WORD chords[]={XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_DPAD_DOWN,XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_DPAD_RIGHT,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_Y};
ULONGLONG base=20000;for(const auto chord:chords){allLegacyTick(0,base);allLegacyTick(chord,base+100);allLegacyTick(chord,base+700);allLegacyTick(0,base+710);allLegacyTick(chord,base+800);allLegacyTick(0,base+810);allLegacyTick(chord,base+900);allLegacyTick(chord,base+1500);allLegacyTick(0,base+1510);allLegacyTick(0,base+1600);base+=3000;}return legacyCounts;}
static int checks=0;static json failures=json::array();
static void check(bool ok,const std::string& name){++checks;if(!ok){failures.push_back(name);std::cout<<"FAIL "<<name<<'\n';}}
static json key(const char* code,const char* src="controller"){return{{"source",src},{"code",code}};}
static json row(const char* id="good"){return{{"id",id},{"origin","custom"},{"enabled",true},{"actionId","window.summon"},{"inputs",json::array({key("a")})},{"trigger","press"},{"params",json::object()}};}
static void refresh(){g_nativeKeyboardShortcutLastRefresh=0;g_nativeKeyboardShortcutRevision=-1;nativeKeyboardShortcutsRefresh(10000);emissions=0;desktopDown.clear();}
static void load(json rows,bool initialized=true){fixtureInput={{"revision",1},{"buttonMapping",{{"rules",{{"shortcutRules",rows},{"shortcutRulesInitialized",initialized}}}}}};refresh();}
static int repeats(json rule,bool acceleration,const char* action="power.tdpAdjust"){rule["actionId"]=action;rule["params"]={{"direction",std::string(action)=="power.tdpAdjust"?"up":"right"},{"amount",1},{"linearAcceleration",acceleration}};rule["trigger"]="repeat";rule["holdMs"]=0;rule["intervalMs"]=150;load(json::array({rule}));tick(0,0,10000);for(ULONGLONG t=10100;t<=15100;t+=10)tick(XINPUT_GAMEPAD_A,0,t);return emissions;}

static uint64_t observedKeyboardHoldMs=0;
static void keyboardSequenceChecks(){
 using namespace ymcc::keyboardSequence;
 const auto item=[](const char* action,const char* code){return json{{"actionId",action},{"code",code}};};
 const json chord={{"keys",json::array({item("keyboard.letter","KeyS"),item("keyboard.leftFunction","ControlLeft")})},{"holdMs",80}};
 const json seq={{"version",1},{"steps",json::array({chord})},{"mode","once"},{"repeatCount",1},{"intervalMs",150}};
 auto parseSequence=[&](const json& value,Plan& plan){return parse(json{{"keySequence",value.dump()}},"keyboard.letter",nativeKeyboardShortcutVirtualKey,plan);};
 Plan plan;check(parseSequence(seq,plan)&&plan.steps[0].keys[0].vk==VK_LCONTROL&&plan.steps[0].keys[1].vk=='S',"native combo parser always presses modifiers before main keys");
 Plan legacy;check(parse(json{{"key","KeyX"}},"keyboard.letter",nativeKeyboardShortcutVirtualKey,legacy)&&legacy.steps[0].duration==80,"existing single keys migrate to 80ms without changing configuration");
 std::vector<std::pair<unsigned short,bool>> trace;const auto send=[&](Key k,bool up){trace.push_back({k.vk,up});return true;};const auto neutral=[](Key){return false;};
 Engine e;e.start("chord",plan,0);e.tick(0,true,send,neutral);check(trace.size()==2&&!trace[0].second,"chord key-downs are separate from delayed key-ups");e.tick(79,true,send,neutral);check(trace.size()==2,"no 2ms pulse: no release before hold deadline");e.tick(80,true,send,neutral);check(trace.size()==4&&trace[2]==std::make_pair((unsigned short)'S',true)&&trace[3]==std::make_pair((unsigned short)VK_LCONTROL,true)&&!e.active(),"80ms chord releases main key before modifier");
 auto delayed=seq;delayed["steps"]=json::array({chord,json{{"delayMs",2000}},json{{"keys",json::array({item("keyboard.letter","KeyX")})},{"holdMs",80}}});Plan waits;check(parseSequence(delayed,waits),"native parser accepts 2 second wait");trace.clear();Engine delay;delay.start("wait",waits,0);delay.tick(0,true,send,neutral);delay.tick(80,true,send,neutral);delay.tick(2079,true,send,neutral);check(trace.size()==4,"explicit delay sends no early key-down");delay.tick(2080,true,send,neutral);check(trace.size()==5&&trace.back().first=='X'&&!trace.back().second,"next step starts after complete wait");delay.tick(2160,true,send,neutral);check(!delay.active()&&!delay.pending(),"one-shot sequence completes with all keys released");
 Plan repeated=legacy;repeated.steps.push_back(legacy.steps[0]);trace.clear();Engine duplicates;duplicates.start("twice",repeated,0);duplicates.tick(0,true,send,neutral);duplicates.tick(80,true,send,neutral);duplicates.tick(119,true,send,neutral);check(trace.size()==2,"repeated identical key has a real 40ms released gap");duplicates.tick(120,true,send,neutral);duplicates.tick(200,true,send,neutral);check(trace.size()==4&&!duplicates.active(),"duplicate key steps both execute");
 Plan loop=legacy;loop.toggle=true;trace.clear();Engine looping;looping.start("loop",loop,0);looping.tick(0,true,send,neutral);looping.tick(80,true,send,neutral);looping.tick(230,true,send,neutral);check(trace.size()==3,"loop executes next cycle after saved interval");looping.start("loop",loop,231);looping.tick(231,true,send,neutral);check(trace.size()==4&&!looping.active()&&!looping.pending(),"second trigger stops loop and releases held key immediately");looping.tick(10000,true,send,neutral);check(trace.size()==4,"stopped loop cannot resume by itself");
 trace.clear();Engine cancel;cancel.start("cancel",plan,0);cancel.tick(0,true,send,neutral);cancel.tick(1,false,send,neutral);check(trace.size()==4&&!cancel.active()&&!cancel.pending(),"revoked owner cancels before hold deadline and releases all owned keys");
 trace.clear();Engine partial;partial.start("partial",plan,0);partial.tick(0,true,[&](Key k,bool up){if(!up&&k.vk=='S')return false;return send(k,up);},neutral);check(trace.size()==2&&trace.back().second&&!partial.active()&&!partial.pending(),"failed main key-down repairs already pressed modifier without continuing");
 trace.clear();Engine release;release.start("debt",plan,0);release.tick(0,true,send,neutral);release.tick(80,true,[](Key,bool){return false;},neutral);check(release.pending()&&!release.active(),"failed key-up is retained as release-only debt");release.tick(329,true,send,neutral);check(trace.size()==2,"release retry respects backoff");release.tick(330,true,send,neutral);check(!release.pending()&&trace.size()==4&&trace[2].second&&trace[3].second,"failed release retries only owned ups in reverse order");
 trace.clear();Engine physical;physical.start("physical",plan,0);physical.tick(0,true,send,[](Key k){return k.vk==VK_LCONTROL;});physical.tick(80,true,send,neutral);check(trace.size()==2&&trace[0].first=='S'&&trace[1].first=='S',"physical held modifier is never released by the macro");
 for(int n=0;n<9;++n){auto bad=seq;if(n==0)bad["steps"]=json::array();if(n==1)bad["steps"][0]["holdMs"]=2;if(n==2)bad["steps"][0]["holdMs"]=2001;if(n==3)bad["repeatCount"]=101;if(n==4)bad["mode"]="bad";if(n==5)bad["steps"]=json::array({json{{"delayMs",2000}}});if(n==6)bad["steps"][0]["keys"][0]["code"]="Unknown";if(n==7)bad["steps"][0]["keys"][1]=bad["steps"][0]["keys"][0];if(n==8)bad["intervalMs"]=0;Plan invalid;check(!parseSequence(bad,invalid),"native keyboard sequence rejects malformed boundary "+std::to_string(n));}
 // Exercise the actual worker/adapter with only the lowest OS send port mocked.
 NativeKeyboardSequenceOwner owner;const auto waitSize=[](size_t count){std::unique_lock<std::mutex> lock(sequenceTraceMx);return sequenceTraceCv.wait_for(lock,std::chrono::seconds(2),[&]{return sequenceTrace.size()>=count;});};
 check(owner.submit("actual-chord",plan,[]{return true;}),"actual worker accepts combo without blocking serial input");check(waitSize(4),"actual worker sends both down and delayed reverse ups");{
  std::lock_guard<std::mutex> lock(sequenceTraceMx);observedKeyboardHoldMs=sequenceTrace.size()>=4?sequenceTrace[2].at-sequenceTrace[1].at:0;check(sequenceTrace.size()==4&&observedKeyboardHoldMs>=80,"actual native worker separates down/up by approximately 80ms rather than one batch");}
 check(owner.submit("actual-loop",loop,[]{return true;}),"actual worker starts toggle loop");check(waitSize(5),"actual loop produces first down");check(owner.submit("actual-loop",loop,[]{return true;}),"actual same trigger is accepted as stop");check(waitSize(6),"actual stop sends outstanding up");
 check(owner.submit("revoked",plan,[]{return fixtureProducerCurrent.load();}),"actual worker accepts revocable producer");check(waitSize(8),"actual producer presses chord before revoke");fixtureProducerCurrent=false;check(waitSize(10),"actual source revoke releases chord on worker without another physical trigger");owner.stop();fixtureProducerCurrent=true;
 g_keyboardSequenceInputFence['S'].store(1);desktopDown.insert('S');check(nativeKeyboardSequenceOwnsKey('S'),"injected held output is excluded from physical shortcut input");g_keyboardSequenceInputFence['S'].store(2);check(nativeKeyboardSequenceOwnsKey('S'),"release boundary remains fenced while OS still reports the injected down");desktopDown.clear();check(nativeKeyboardSequenceOwnsKey('S')&&!nativeKeyboardSequenceOwnsKey('S'),"neutral sample retires injected-key fence before later real input");nativeKeyboardSequenceOwnsKey(VK_LCONTROL);nativeKeyboardSequenceOwnsKey('X');

 NativeKeyboardShortcutRule dispatchedRule;dispatchedRule.outputId="real-dispatch";dispatchedRule.keyboardPlan=plan;
 const auto baseTrace=sequenceTrace.size();check(nativeKeyboardSequenceDispatch(dispatchedRule,kOemShortcutUnboundEpoch,0,0,false,{}),"production dispatcher submits recorded keyboard plan to worker");check(waitSize(baseTrace+2),"production dispatcher reaches separate real worker key-down calls");fixtureWindowCurrent=false;check(waitSize(baseTrace+4),"foreground window change cancels real dispatch and releases chord");fixtureWindowCurrent=true;
 for(int guard=0;guard<4;++guard){dispatchedRule.outputId="guard-"+std::to_string(guard);const auto before=sequenceTrace.size();check(nativeKeyboardSequenceDispatch(dispatchedRule,kOemShortcutUnboundEpoch,0,0,false,{}),"actual dispatch guard admitted before transition "+std::to_string(guard));check(waitSize(before+2),"actual dispatch guard key-down "+std::to_string(guard));if(guard==0)g_shortcutRecordingActive=true;if(guard==1)g_inputReady=false;if(guard==2)fixturePowerGeneration.fetch_add(1);if(guard==3)g_exitRequested=true;check(waitSize(before+4),"actual recording/input/power/exit revoke releases owned chord "+std::to_string(guard));g_shortcutRecordingActive=false;g_inputReady=true;g_exitRequested=false;}
 g_nativeKeyboardSequenceOwner.stop();
 uint64_t slowNow=0;Engine slow;slow.start("slow-port",plan,0);slow.tick(0,true,[&](Key,bool){slowNow+=20;return true;},neutral,[&]{return slowNow;});int earlyUps=0;slow.tick(119,true,[&](Key,bool up){if(up)++earlyUps;return true;},neutral,[&]{return uint64_t(119);});check(earlyUps==0,"hold duration starts after the final down send returns, even if sender is slow");
 const auto count=sequenceTrace.size();std::this_thread::sleep_for(std::chrono::milliseconds(30));check(sequenceTrace.size()==count,"native worker stop joins and emits no later downs");
 // Reverse-check two independent OS key identities and cross-rule release debt.
 auto enterSequence=seq;enterSequence["steps"]=json::array({json{{"keys",json::array({item("keyboard.leftFunction","Enter"),item("keyboard.numeric","NumpadEnter")})},{"holdMs",80}}});Plan enters;
 check(parseSequence(enterSequence,enters)&&enters.steps[0].keys.size()==2&&!enters.steps[0].keys[0].extended&&enters.steps[0].keys[1].extended,"main Enter and numeric Enter stay separate keys in a chord");
 Plan manualEnters;manualEnters.steps.push_back({{{VK_RETURN,false},{VK_RETURN,true}},80});
 NativeKeyboardSequenceOwner enterPlayback;const auto playbackStart=sequenceTrace.size();check(enterPlayback.submit("enters",manualEnters,[]{return true;}),"distinct Enter playback starts");check(waitSize(playbackStart+4),"both main and numeric Enter receive their own down and up");enterPlayback.stop();{std::lock_guard<std::mutex> lock(sequenceTraceMx);check(sequenceTrace.size()>=playbackStart+4&&!sequenceTrace[playbackStart].extended&&sequenceTrace[playbackStart+1].extended&&sequenceTrace[playbackStart+2].extended&&!sequenceTrace[playbackStart+3].extended,"extended numeric Enter packets keep their identity through reverse release");}
 NativeKeyboardSequenceOwner debtOwner;const auto debtStart=sequenceTrace.size();fixtureRejectKeyUps=true;fixtureRejectedKeyUps=0;
 check(debtOwner.submit("failed-release",plan,[]{return true;}),"release-failure scenario starts original chord");check(waitSize(debtStart+2),"release-failure scenario owns modifier and main key");{
  std::unique_lock<std::mutex> lock(sequenceTraceMx);check(sequenceTraceCv.wait_for(lock,std::chrono::seconds(2),[]{return fixtureRejectedKeyUps.load()>0;}),"actual worker observes rejected key-up");}
 const bool admittedDuringDebt=debtOwner.submit("must-not-run-under-stuck-Ctrl",legacy,[]{return true;});check(!admittedDuringDebt,"pending failed release blocks new rules instead of typing under a stuck modifier");
 fixtureRejectKeyUps=false;check(waitSize(debtStart+4),"release-only cleanup completes once sender recovers");check(debtOwner.submit("explicit-retry-after-release",legacy,[]{return true;}),"repaired release debt allows an explicit later user trigger again");check(waitSize(debtStart+6),"explicit retry executes without replaying rejected tasks");debtOwner.stop();

}

int main(){try{
keyboardSequenceChecks();
json variants;std::ifstream("VARIANTS.json")>>variants;
std::set<std::string> ui={"os.switchProgram","monitor.toggle","display.frameRateControl","fan.switchPreset","app.launchQuickApp"};

const auto factory=json::parse(R"FACTORY([{"id":"native-enabled","origin":"native-default","enabled":true,"actionId":"window.summon","inputs":[{"source":"controller","code":"lb"},{"source":"controller","code":"rb"}],"trigger":"hold","holdMs":500,"doubleWindowMs":500,"intervalMs":150,"params":{},"defaultKey":"enabled"},{"id":"native-bDoubleMinimize","origin":"native-default","enabled":true,"actionId":"window.hideToTray","inputs":[{"source":"controller","code":"b"}],"trigger":"double","holdMs":2000,"doubleWindowMs":500,"intervalMs":150,"params":{},"defaultKey":"bDoubleMinimize"},{"id":"native-tdpShortcut","origin":"native-default","enabled":true,"actionId":"power.tdpAdjust","inputs":[{"source":"controller","code":"start"},{"source":"controller","code":"dpadUp"}],"trigger":"repeat","holdMs":0,"doubleWindowMs":500,"intervalMs":150,"params":{"direction":"up","amount":"1","linearAcceleration":true},"defaultKey":"tdpShortcut"},{"id":"native-fpsShortcut","origin":"native-default","enabled":true,"actionId":"display.brightnessAdjust","inputs":[{"source":"controller","code":"start"},{"source":"controller","code":"dpadRight"}],"trigger":"repeat","holdMs":0,"doubleWindowMs":500,"intervalMs":150,"params":{"direction":"right","amount":"5","linearAcceleration":true},"defaultKey":"fpsShortcut"},{"id":"native-killGame","origin":"native-default","enabled":true,"actionId":"game.killCurrent","inputs":[{"source":"controller","code":"back"},{"source":"controller","code":"b"}],"trigger":"hold","holdMs":500,"doubleWindowMs":500,"intervalMs":150,"params":{},"defaultKey":"killGame"},{"id":"native-openKeyboard","origin":"native-default","enabled":true,"actionId":"os.openTouchKeyboard","inputs":[{"source":"controller","code":"back"},{"source":"controller","code":"x"}],"trigger":"hold","holdMs":500,"doubleWindowMs":500,"intervalMs":150,"params":{},"defaultKey":"openKeyboard"},{"id":"native-returnDesktop","origin":"native-default","enabled":true,"actionId":"os.returnDesktop","inputs":[{"source":"controller","code":"back"},{"source":"controller","code":"a"}],"trigger":"press","holdMs":500,"doubleWindowMs":500,"intervalMs":150,"params":{},"defaultKey":"returnDesktop"},{"id":"native-mouseToggle","origin":"native-default","enabled":true,"actionId":"mouse.toggle","inputs":[{"source":"controller","code":"back"},{"source":"controller","code":"y"}],"trigger":"hold","holdMs":500,"doubleWindowMs":500,"intervalMs":150,"params":{},"defaultKey":"mouseToggle"}])FACTORY");
load(factory);const auto originals=replayAllLegacy();for(int slot=0;slot<8;slot++)check(originals[slot]>0,"actual legacy branch positive control "+factory[slot]["defaultKey"].get<std::string>());
for(int slot=0;slot<8;slot++)for(int scenario=0;scenario<4;scenario++){auto rows=factory;auto& r=rows[slot];if(scenario==0){r["inputs"]=json::array({key("l4","oem")});r["trigger"]="press";}else if(scenario==1)r["enabled"]=false;else if(scenario==2)rows.erase(rows.begin()+slot);else{r["origin"]="custom";r.erase("defaultKey");r["inputs"]=json::array({key("l4","oem")});r["trigger"]="press";}load(rows);const auto counts=replayAllLegacy();const auto name=factory[slot]["defaultKey"].get<std::string>();check(counts[slot]==0,"actual old branch disabled after edit/disable/delete/replacement "+name+" scenario "+std::to_string(scenario));for(int other=0;other<8;other++)if(other!=slot)check(counts[other]>0,"other original branches preserved while editing "+name+" scenario "+std::to_string(scenario));}
// The saved rule table is the only shortcut authority even if the old
// compatibility flags are stale after a failed secondary IPC sync.
load(factory);g_summonEnabled=g_bDoubleMinimize=g_tdpShortcut=g_fpsShortcut=g_killGame=g_openKeyboard=g_returnDesktop=g_mouseToggle=false;
const auto staleFlags=replayAllLegacy();for(int slot=0;slot<8;slot++)check(staleFlags[slot]>0,"restored enabled template survives stale legacy flag in slot "+std::to_string(slot));
load(json::array(),false);const auto firstRunOff=replayAllLegacy();for(int slot=0;slot<8;slot++)check(firstRunOff[slot]==0,"unconfigured first-run still respects old disabled flags");
g_summonEnabled=false;g_bDoubleMinimize=g_tdpShortcut=g_fpsShortcut=g_killGame=g_openKeyboard=g_returnDesktop=g_mouseToggle=true;load(json::array({factory[1]}));check(replayAllLegacy()[1]>0,"keeping hide enabled does not depend on retaining the summon action");
g_summonEnabled=g_bDoubleMinimize=g_tdpShortcut=g_fpsShortcut=g_killGame=g_openKeyboard=g_returnDesktop=g_mouseToggle=true;
// Valid explicit templates must survive an opaque/damaged sibling carrying
// the same historical slot; restoring defaults is a real recovery operation.
for(int slot=0;slot<8;slot++){
 auto damaged=factory[slot];damaged.erase("actionId");damaged["id"]="opaque-default";
 load(json::array({factory[slot],damaged}));check(nativeShortcutLegacyDefaultAllowed(factory[slot]["defaultKey"].get<std::string>().c_str()),"restored template survives damaged sibling in slot "+std::to_string(slot));check(replayAllLegacy()[slot]>0,"restored original branch really dispatches in slot "+std::to_string(slot));
 auto changed=factory[slot];changed["id"]="explicit-second-binding";changed["inputs"]=json::array({key("l4","oem")});changed["trigger"]="press";
 load(json::array({factory[slot],changed}));check(replayAllLegacy()[slot]>0&&g_nativeKeyboardShortcuts.size()==1,"two explicitly saved old/new bindings both remain active in slot "+std::to_string(slot));
}
// Every available output/parameter variant is also legal on every default identity.
const json invalidInputLists=json::array({json::array(),json::array({key("a"),key("b"),key("x"),key("y"),key("lb")}),json::array({key("a"),key("a")}),json::array({key("a","future")}),json::array({key("a","oem")}),json::array({key("F25","keyboard")}),json::array({key("MouseUnknown","mouse")}),json::array({7}),json::array({json::object()})});
for(int slot=0;slot<8;slot++)for(const auto& inputs:invalidInputLists){auto broken=factory[slot];broken["inputs"]=inputs;load(json::array({broken,row()}));check(g_nativeKeyboardShortcuts.size()==1&&g_nativeShortcutLegacyOverrideMask==255,"invalid default chord cannot execute, truncate or revive its old binding; valid sibling remains");}
fixtureInput={{"revision",1},{"buttonMapping",{{"rules",{{"shortcutRulesInitialized",true}}}}}};refresh();check(g_nativeKeyboardShortcuts.empty()&&g_nativeShortcutLegacyOverrideMask==255,"missing initialized array stays user-owned and never enables defaults");
for(int slot=0;slot<8;slot++)for(const auto& variant:variants){auto r=variant["rule"];r["origin"]="native-default";r["defaultKey"]=factory[slot]["defaultKey"];r["inputs"]=json::array({key("l4","oem")});load(json::array({r}));check(g_nativeKeyboardShortcuts.size()==1,"every default slot accepts visible output: "+variant["name"].get<std::string>());check(g_nativeShortcutLegacyOverrideMask==255,"replacing any default output leaves no hidden old shortcut");}
// A damaged initialized table must not turn user-owned defaults back on.
for(const auto& bad:json::array({nullptr,7,"bad",json::object()})){
 fixtureInput={{"revision",1},{"buttonMapping",{{"rules",{{"shortcutRulesInitialized",true},{"shortcutRules",bad}}}}}};refresh();check(g_nativeShortcutLegacyOverrideMask==255&&g_nativeKeyboardShortcuts.empty(),"initialized damaged table fails closed instead of reviving factory shortcuts");
}
for(const auto& variant:variants){const auto r=variant["rule"];const auto action=r["actionId"].get<std::string>(),name=variant["name"].get<std::string>();load(json::array({r}));check(g_nativeKeyboardShortcuts.size()==1,name+" parses exactly once");if(g_nativeKeyboardShortcuts.empty())continue;const auto p=g_nativeKeyboardShortcuts[0];
 if(action.rfind("keyboard.",0)==0){check(p.virtualKey!=0&&p.ownAction.empty()&&p.uiAction.empty()&&p.inputAction.empty(),name+" keyboard dispatch category");check(p.requiresShift==(action=="keyboard.symbol"),name+" symbol shift");const auto code=r["params"]["key"].get<std::string>();check(p.forceExtendedKey==(code=="NumpadEnter"),name+" keypad Enter flag");if(action=="keyboard.letter")check(p.virtualKey==code[3],name+" letter virtual key");if(code.rfind("Digit",0)==0)check(p.virtualKey==code[5],name+" digit virtual key");}
 else if(ui.count(action)){check(p.uiAction==action&&p.uiParams==r["params"],name+" UI output params preserved");}
 else if(action=="input.gyroToggle"||action=="input.virtualGamepadToggle"){check(p.inputAction==action&&p.preset==r["params"]["preset"].get<std::string>(),name+" input toggle preset");}
 else {check(p.ownAction==action,name+" native dispatch category");if(action=="power.tdpAdjust"||action=="display.brightnessAdjust"){const std::string dir=r["params"]["direction"];const auto amount=r["params"]["amount"];const int n=amount.is_string()?std::stoi(amount.get<std::string>()):amount.get<int>();check(p.actionDelta==((dir=="up"||dir=="right")?n:-n),name+" signed delta");}}
 for(const char* trigger:{"press","hold","release","double","repeat"}){auto edit=r;edit["trigger"]=trigger;edit["holdMs"]=500;edit["doubleWindowMs"]=500;edit["intervalMs"]=150;load(json::array({edit}));if(edit["params"].contains("keySequence")&&json::parse(edit["params"]["keySequence"].get<std::string>())["mode"]=="toggle"&&std::string(trigger)=="repeat"){check(g_nativeKeyboardShortcuts.empty(),"loop cannot toggle on every repeat frame");continue;}tick(XINPUT_GAMEPAD_A,0,10000);check(emissions==0,name+" "+trigger+" revision held blocked");tick(0,0,10010);tick(XINPUT_GAMEPAD_A,0,10100);tick(XINPUT_GAMEPAD_A,0,10600);tick(0,0,10610);tick(XINPUT_GAMEPAD_A,0,10650);tick(XINPUT_GAMEPAD_A,0,11150);tick(0,0,11160);const int expected=std::string(trigger)=="double"?0:2;check(emissions==expected,name+" "+trigger+" edge counts");}
}
for(const char* trigger:{"press","hold","release","double","repeat"})for(int count=1;count<=4;count++){auto r=row();r["trigger"]=trigger;r["holdMs"]=100;r["inputs"]=json::array({key("a")});if(count>=2)r["inputs"].push_back(key("l4","oem"));if(count>=3)r["inputs"].push_back(key("F14","keyboard"));if(count>=4)r["inputs"].push_back(key("KeyK","keyboard"));load(json::array({r}));check(g_nativeKeyboardShortcuts.size()==1,std::string(trigger)+" mixed input count "+std::to_string(count));if(g_nativeKeyboardShortcuts.empty())continue;tick(0,0,10000);uint64_t oem=0;if(count>=2)nativeKeyboardShortcutMaskBit("l4",oem);if(count>=3)desktopDown.insert(VK_F14);if(count>=4)desktopDown.insert('K');tick(XINPUT_GAMEPAD_A,oem,10100);tick(XINPUT_GAMEPAD_A,oem,10200);desktopDown.clear();tick(0,0,10210);if(count>=3)desktopDown.insert(VK_F14);if(count>=4)desktopDown.insert('K');tick(XINPUT_GAMEPAD_A,oem,10300);tick(XINPUT_GAMEPAD_A,oem,10400);desktopDown.clear();tick(0,0,10410);check(emissions==(std::string(trigger)=="double"?1:2),std::string(trigger)+" mixed input trigger semantics");}
json savedDocuments;std::ifstream("SAVED-INPUTS.json")>>savedDocuments;
for(const auto& saved:savedDocuments){const auto input=saved["input"];const auto rows=input["buttonMapping"]["rules"]["shortcutRules"];
if(saved.value("name","")=="actual editor combo sequence") {
 fixtureInput=input;refresh();check(rows.size()==9&&g_nativeKeyboardShortcuts.size()==1,"actual sequence save retains eight original defaults and one custom owner");
 if(!g_nativeKeyboardShortcuts.empty()){const auto& rule=g_nativeKeyboardShortcuts[0];check(rule.requiredMask==XINPUT_GAMEPAD_START,"actual sequence save preserves its real Start input");check(rule.keyboardPlan.toggle&&rule.keyboardPlan.steps.size()==4&&rule.keyboardPlan.steps[1].keys.empty()&&rule.keyboardPlan.steps[1].duration==2000,"actual UI JSON becomes native loop with complete 2-second wait");check(rule.keyboardPlan.steps[0].keys.size()==2&&rule.keyboardPlan.steps[0].keys[0].vk==VK_LCONTROL&&rule.keyboardPlan.steps[0].keys[1].vk=='S',"actual UI Ctrl+S becomes ordered native chord");check(rule.keyboardPlan.steps[2].keys.size()==2&&rule.keyboardPlan.steps[2].keys[0].vk==VK_LSHIFT&&rule.keyboardPlan.steps[2].keys[1].vk=='X'&&rule.keyboardPlan.steps[3].keys[1].vk=='X',"actual repeated Shift+X steps survive native parse");}
 continue;
}
size_t expected=0;for(const auto& r:rows){load(json::array({r}));const bool legacy=r.value("origin","")=="native-default"&&nativeShortcutLegacyDefaultAllowed(r.value("defaultKey","").c_str());check(g_nativeKeyboardShortcuts.size()+(legacy?1:0)==1,"actual editor saved row has exactly one runtime owner: "+r.value("id",""));expected+=g_nativeKeyboardShortcuts.size();if(!g_nativeKeyboardShortcuts.empty()){const auto& p=g_nativeKeyboardShortcuts[0];check(p.requiredMask==(uint64_t(XINPUT_GAMEPAD_X)|(1ull<<20)),"actual editor saved source/code becomes the exact physical mask");}}
fixtureInput=input;refresh();check(g_nativeKeyboardShortcuts.size()==expected,"actual editor saved table preserves every individually valid runtime rule");check(expected==(saved["name"].get<std::string>().rfind("defaults-",0)==0?8:21),"actual editor edited defaults/new outputs have expected native ownership count");}

json repairDocuments;std::ifstream("REPAIR-SAVED-INPUTS.json")>>repairDocuments;
for(const auto& saved:repairDocuments){const auto rows=saved["input"]["buttonMapping"]["rules"]["shortcutRules"];size_t expected=0;
 for(const auto& r:rows){load(json::array({r}));const auto action=ymcc::inputShortcutString(r,"actionId");const bool enabled=r.value("enabled",true);const bool legacy=r.value("origin","")=="native-default"&&nativeShortcutLegacyDefaultAllowed(r.value("defaultKey","").c_str());const bool supported=!action.empty()&&(nativeYmccShortcutAction(action)||ymcc::shortcutUiAction(action)||ymcc::inputShortcutAction(action)||action.rfind("keyboard.",0)==0);
 check(g_nativeKeyboardShortcuts.size()+(legacy?1:0)==(enabled&&supported?1:0),"repaired UI receipt has the correct enabled runtime owner: "+saved["name"].get<std::string>());expected+=g_nativeKeyboardShortcuts.size();if(g_nativeKeyboardShortcuts.empty())continue;const auto& p=g_nativeKeyboardShortcuts[0];
 check(p.trigger==r.value("trigger","press"),"repaired UI receipt trigger survives native parsing");check(p.holdMs==r.value("holdMs",500ull)&&p.intervalMs==r.value("intervalMs",150ull)&&p.doubleWindowMs==r.value("doubleWindowMs",500ull),"repaired UI receipt timing survives native parsing");
 uint64_t expectedMask=0;bool known=true;for(const auto& input:r["inputs"]){const auto code=input["code"].get<std::string>();if(input["source"]!="controller"){known=false;break;}if(code=="a")expectedMask|=XINPUT_GAMEPAD_A;else if(code=="b")expectedMask|=XINPUT_GAMEPAD_B;else if(code=="x")expectedMask|=XINPUT_GAMEPAD_X;else if(code=="y")expectedMask|=XINPUT_GAMEPAD_Y;else{known=false;break;}}
 if(known)check(p.requiredMask==expectedMask,"repaired UI receipt has the exact newly recorded physical mask");
 if(!p.uiAction.empty()){check(p.uiAction==action,"repaired UI output action is unchanged");for(auto it=p.uiParams.begin();it!=p.uiParams.end();++it)check(r["params"].contains(it.key())&&r["params"][it.key()]==it.value(),"repaired UI known output params survive while native ignores extensions");}
 if(action=="keyboard.letter")check(p.virtualKey==r["params"]["key"].get<std::string>()[3],"repaired keyboard key becomes the exact native virtual key");
 }
 fixtureInput=saved["input"];refresh();check(g_nativeKeyboardShortcuts.size()==expected,"repaired UI table parses without losing valid sibling rules");
}
json controlDocuments;std::ifstream("CONTROL-SAVED-INPUTS.json")>>controlDocuments;
for(const auto& saved:controlDocuments){
 const auto rows=saved["input"]["buttonMapping"]["rules"]["shortcutRules"];size_t expected=0;
 for(const auto& r:rows){if(!r.is_object()||!r.contains("actionId")||!r["actionId"].is_string())continue;const auto action=r["actionId"].get<std::string>();if(!nativeYmccShortcutAction(action)&&!ymcc::shortcutUiAction(action)&&!ymcc::inputShortcutAction(action)&&action.rfind("keyboard.",0)!=0)continue;
  load(json::array({r}));const bool active=r.value("enabled",true);const bool legacy=r.value("origin","")=="native-default"&&nativeShortcutLegacyDefaultAllowed(r.value("defaultKey","").c_str());check(g_nativeKeyboardShortcuts.size()+(legacy?1:0)==(active?1:0),"UI control receipt has exactly the enabled runtime owner: "+saved["name"].get<std::string>());expected+=g_nativeKeyboardShortcuts.size();
  if(!g_nativeKeyboardShortcuts.empty()){const auto& parsed=g_nativeKeyboardShortcuts[0];check(parsed.trigger==r["trigger"].get<std::string>(),"UI control receipt trigger is unchanged");check(parsed.holdMs==r["holdMs"].get<ULONGLONG>()&&parsed.doubleWindowMs==r["doubleWindowMs"].get<ULONGLONG>()&&parsed.intervalMs==r["intervalMs"].get<ULONGLONG>(),"UI control receipt timing survives exactly");uint64_t expectedMask=0;std::vector<WORD> desktop;check(nativeKeyboardShortcutAllInputs(r["inputs"],expectedMask,desktop)&&parsed.requiredMask==expectedMask&&parsed.desktopKeys==desktop,"UI control receipt input sources reverse to the exact physical chord");if(action=="monitor.toggle")check(parsed.uiAction==action,"default slot can dispatch its chosen monitor output");if(action=="keyboard.letter")check(parsed.virtualKey=='K',"default slot can dispatch its chosen real keyboard output");}
 }
 fixtureInput=saved["input"];refresh();check(g_nativeKeyboardShortcuts.size()==expected,"whole control receipt retains all individually valid rules: "+saved["name"].get<std::string>());
 const auto old=replayAllLegacy();for(int slot=0;slot<8;slot++){bool explicitOriginal=false;for(const auto& r:rows)if(r.is_object()&&r.value("enabled",true)&&nativeShortcutLegacyTemplateMatches(r,slot)&&r.value("origin","")=="native-default"&&r.value("defaultKey","")==factory[slot]["defaultKey"].get<std::string>())explicitOriginal=true;check((old[slot]>0)==explicitOriginal,"no hidden original branch beyond explicitly saved templates: "+saved["name"].get<std::string>()+" slot "+std::to_string(slot));}
}
for(int field=0;field<3;field++)for(int position=0;position<3;position++){auto bad=row("bad");if(field==0)bad["trigger"]=123;else if(field==1){bad["actionId"]="power.tdpAdjust";bad["params"]={{"direction",123}};}else bad["params"]=nullptr;json rows=json::array({row("a"),row("b")});rows.insert(rows.begin()+position,bad);load(rows);check(g_nativeKeyboardShortcuts.size()==2,"malformed row "+std::to_string(field)+" position "+std::to_string(position)+" does not remove good siblings");}
load(json::array(),false);check(nativeShortcutLegacyDefaultAllowed("enabled"),"uninitialized empty global uses factory defaults");load(json::array(),true);check(!nativeShortcutLegacyDefaultAllowed("enabled"),"explicit initialized empty list disables all defaults");fixtureInput["startupObservedBoot"]="boot-a";fixtureInput["bootModeShortcuts"]={{"schema",1},{"mode","steam"},{"boot","boot-a"},{"rules",json::array()}};refresh();check(!nativeShortcutLegacyDefaultAllowed("enabled"),"retired empty mode cannot override the explicit global empty table");
load(json::array({row()}));fixtureInput["bootModeShortcuts"]={{"schema","bad"}};refresh();check(g_nativeKeyboardShortcuts.size()==1,"malformed mode metadata cannot swallow valid global rules");
fixtureInput={{"revision",1},{"startupObservedBoot","boot-a"},{"bootModeShortcuts",{{"schema",1},{"mode","steam"},{"boot","boot-a"},{"rules",json::array({row()})}}}};refresh();check(g_nativeKeyboardShortcuts.empty()&&nativeShortcutLegacyDefaultAllowed("enabled"),"retired mode rules cannot replace the unconfigured global defaults");
auto damagedDefault=row();damagedDefault["origin"]="native-default";damagedDefault["defaultKey"]="enabled";damagedDefault["trigger"]=12;load(json::array({damagedDefault,row()}));check(g_nativeKeyboardShortcuts.size()==1&&!nativeShortcutLegacyDefaultAllowed("enabled"),"damaged edited default cannot revive its old binding or remove siblings");
const auto normal=repeats(row(),false),accelerated=repeats(row(),true);check(accelerated>normal,"saved linear acceleration increases repeated TDP rate");check(normal==34,"disabled acceleration honors fixed 150ms interval");check(repeats(row(),true,"display.brightnessAdjust")==accelerated&&repeats(row(),false,"display.brightnessAdjust")==normal,"brightness and TDP share the saved acceleration cadence");
for(const char* src:{"oem","controller","keyboard","mouse","future"}){auto r=row();r["inputs"]=json::array({key("no-such-key",src)});load(json::array({r}));check(g_nativeKeyboardShortcuts.empty(),std::string(src)+" unknown input fails closed");}
json report={{"nativeWorkerObservedHoldMs",observedKeyboardHoldMs},{"pass",failures.empty()},{"checks",checks},{"variants",variants.size()},{"failures",failures},{"acceleratedCount",accelerated},{"constantCount",normal},{"physicalInput",false},{"settingsIo","memory"}};std::ofstream("RESULT.json")<<report.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:2;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
