#pragma once

#include <cstdint>
#include "json.hpp"

namespace ymcc::inputKeyStateEvidence {
enum Reason : std::uint32_t {
    CallbackHeld = 1, SelfHeldAssumption = 2, RecentActivity = 4,
    KeyboardRow = 8, MouseButtonRow = 16
};
}

namespace ymcc::inputActivityLogScope {
inline constexpr char SchemaField[] = "activityScopeSchema";
inline constexpr unsigned Schema = 1;
inline constexpr unsigned ActivityRowSchema = 1;
inline constexpr unsigned KeyStateSchema = 1;

inline nlohmann::json activityFields() {
    return {
        {"scope", "Bounded per-key/button activity counts from existing callbacks and own SendInput. No text/order, consumer receipt, all-process producer identity or automatic key release. Held is callback history, including swallowed edges; count-prefix is an assumption."},
        {"contextScope", "Host, physical/submit/ACK summaries and modifier snapshot are latest drain-time observations, not an atomic binding to all 5s events. Host present means managed handle, not system process enumeration."},
        {"hookCoverageScope", "OEM or passive diagnostic handle cached;not hook liveness,downstream delivery or sender proof"},
        {"hookDecisionRouteEncoding", "LL route low nibble 1=passed-to-next-hook, 2=suppressed-by-YMCC, 3=unobserved; 0x10=injected,0x20=lower-integrity-injected; not producer identity"},
        {"selfSourceEncoding", "SourceId enum + 1; request and returned-prefix namespaces are independent"},
        {"rawSourceEncoding", "1=non-E1,2=E1; device handle is callback-time token, no device-path lookup here"}
    };
}

inline nlohmann::json keyStateFields() {
    using namespace inputKeyStateEvidence;
    return {
        {"selectionReasonBits", {{"callbackHeld", CallbackHeld}, {"selfHeldAssumption", SelfHeldAssumption}, {"recentActivity", RecentActivity}, {"keyboardRow", KeyboardRow}, {"mouseButtonRow", MouseButtonRow}}},
        {"selectionPolicy", "held callback/self-assumption first; descending last retained tick; ascending literal VK for ties"},
        {"selectionScope", "retained Keyboard/MouseButton rows only; no scan-code conversion; no generic/side-key remapping"},
        {"keyboardVirtualKeyFilter", "literal supplied VK 1..255 only; 0/out-of-range excluded"},
        {"mouseButtonPolicy", "1=VK_LBUTTON;2=VK_RBUTTON;3=VK_MBUTTON;4=VK_XBUTTON1;5=VK_XBUTTON2;no swap inference"},
        {"mouseAsyncScope", "Unverified mouse interpretation: GetAsyncKeyState button state may differ from logical button mapping; button-swap configuration is unobserved. Standard VK selection is not a physical/logical button mapping proof."},
        {"asyncApi", "GetAsyncKeyState-high-bit-only"},
        {"queueApi", "GetKeyboardState-calling-thread-message-queue"},
        {"scope", "Bounded drain-time observations only. Selection reasons describe retained callbacks/self assumptions, not OS held state. Zero is not absence proof; async reads can be desktop-restricted, and queue state belongs to the calling thread. Sequential samples miss edges/omitted rows and prove no source identity, game receipt, cross-key atomicity or release."}
    };
}
}
