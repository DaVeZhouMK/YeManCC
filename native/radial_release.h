#pragma once
#include <array>
#include <atomic>
#include <string_view>

namespace ymcc::radial {
// Product policy, mirrored by src/radial/availability.ts and checked together.
inline constexpr bool kUserAvailable = true;
// Each process starts closed; only the explicit diagnostic opt-in admits it.
inline std::atomic_bool testingEnabled{false};
inline bool userAvailable() { return kUserAvailable && testingEnabled.load(std::memory_order_acquire); }
inline void setTestingEnabled(bool enabled) { testingEnabled.store(kUserAvailable && enabled, std::memory_order_release); }
template<class Json> Json unavailableResponse() {
    return {{"ok", false}, {"available", false}, {"enabled", false}, {"monitorHold", false},
        {"fullScreenHold", false}, {"feedback", false}, {"reason", "轮盘测试功能未开启"},
        {"fullScreenRequested", false}, {"fullScreenInstalled", false}, {"rawRegistered", false}};
}
inline bool userActionAvailable(std::string_view action) {
    return action != "radial.summon" || userAvailable();
}
// Register explicit refusals instead of leaving old clients able to configure
// touch observers or create an output window. This never reads saved settings.
template<class Json, class Register>
bool registerUnavailableUserEndpoints(Register&& registerHandler) {
    if constexpr (kUserAvailable) return false;
    constexpr std::array<const char*, 8> commands = {
        "radial.configure", "radial.status", "radial.request", "radial.create",
        "radial.frame", "radial.keepAlive", "radial.result", "radial.close"
    };
    for (const auto* command : commands) registerHandler(command, [](const Json&) -> Json {
        return {{"ok", false}, {"available", false}, {"enabled", false}, {"monitorHold", false},
            {"fullScreenHold", false}, {"feedback", false}, {"reason", "轮盘功能尚未开放"},
            {"fullScreenRequested", false}, {"fullScreenInstalled", false}, {"rawRegistered", false}};
    });
    return true;
}
}
