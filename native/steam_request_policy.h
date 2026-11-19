#pragma once
// Fixed operation vocabulary shared by native ingress and the CDP channel.
// Classification is not authorization: account/process/foreground/boot/recovery
// owners keep their own current checks, including the final pre-dispatch check.
#include <string_view>

namespace ymcc::steamrequest {
struct Policy {
    bool known = false;
    bool menu = false;
    bool qamFollowup = false;
    bool sleep = false;
    bool lightGamepadLease = false;
    bool setter = false; // Existing business-result uncertainty classification.
    bool runningLinkRead = false;
    bool shortRead = false;
    bool tryLock = false;
    bool coordinatedEffect = false; // Additional public command arbitration, never permission.
};
inline Policy classify(std::string_view operation) noexcept {
    Policy p;
    p.menu = operation == "menu.button" || operation == "menu.toggle" || operation == "menu.inspect" ||
        operation == "menu.qam.prepare" || operation == "menu.qam.observe" ||
        operation == "menu.qam.retry" || operation == "menu.qam.dispose";
    p.qamFollowup = p.menu && operation.starts_with("menu.qam.");
    p.sleep = operation == "sleep.session.inspect" || operation == "sleep.inspect" ||
        operation == "sleep.reconcile" || operation == "sleep.input-resume";
    p.lightGamepadLease = operation == "input.gamepadLease";
    p.setter = operation == "desktopMouse.set" || operation == "mouse.set" || operation == "overlay.set" ||
        operation == "settings.set" || operation == "decky.repairConnection" || operation == "decky.recoverConnection" ||
        operation == "decky.cleanupConnection" || operation == "menu.toggle" || operation == "menu.button" ||
        operation == "menu.qam.retry" || operation == "sleep.reconcile" || operation == "sleep.input-resume";
    p.runningLinkRead = operation == "sleep.inspect" || operation == "sleep.session.inspect";
    p.shortRead = p.lightGamepadLease || p.qamFollowup || operation == "ui.mode.get" || operation == "sleep.session.inspect" ||
        operation == "diagnostics.apps.api.read" || operation == "diagnostics.apps.contract.read";
    p.tryLock = operation == "desktopMouse.get" || operation == "desktopMouse.set";
    p.coordinatedEffect=p.setter||operation=="menu.qam.prepare"||operation=="menu.qam.dispose";
    // The original lightweight gamepad-lease lane retains its independent
    // try/no-wait policy; facts/readbacks do not acquire an action slot.
    p.known = p.menu || p.sleep || p.lightGamepadLease || operation == "ui.mode.get" ||
        operation == "desktopMouse.get" || operation == "desktopMouse.set" ||
        operation == "mouse.get" || operation == "mouse.set" || operation == "overlay.get" || operation == "overlay.set" ||
        operation == "settings.get" || operation == "settings.set" ||
        operation == "diagnostics.apps.api.read" || operation == "diagnostics.apps.contract.read" ||
        operation == "decky.repairConnection" || operation == "decky.recoverConnection" || operation == "decky.cleanupConnection";
    return p;
}
}
