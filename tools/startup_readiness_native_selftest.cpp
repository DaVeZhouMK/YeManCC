#include "../native/startup_readiness.h"
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
using ymcc::StartupInputState;
static int checks = 0;
static void check(bool ok, const char* name) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
int main() {
    using S = StartupInputState;
    for (const auto input : {S::Initializing, S::Waiting, S::Skipped, S::Ready, S::Failed}) {
        for (const bool navigation : {false, true}) for (const bool frontend : {false, true}) {
            const bool terminal = input == S::Skipped || input == S::Ready || input == S::Failed;
            check(ymcc::startupMayReveal(navigation, frontend, input) == (navigation && frontend && terminal),
                  "both signals in either completion order; disabled/failure allowed, pending denied");
        }
    }
    for (const bool enabled : {false, true}) for (const bool busy : {false, true})
    for (const bool attempted : {false, true}) for (const bool success : {false, true})
    for (const bool healthy : {false, true}) for (const bool expired : {false, true}) {
        const auto actual = ymcc::startupInputDecision(enabled, busy, attempted, success, healthy, expired);
        S expected = S::Waiting;
        if (!busy) {
            if (!enabled) expected = S::Skipped;
            else if (!attempted) expected = expired ? S::Failed : S::Waiting;
            else expected = success && healthy ? S::Ready : S::Failed;
        }
        check(actual == expected, "startup evidence exhaustive combinations");
    }
    check(ymcc::startupInputDecision(true, true, true, true, true, true) == S::Waiting,
          "pending handoff cannot be bypassed by success or deadline");
    check(ymcc::startupInputDecision(true, true, true, false, false, true) == S::Waiting,
          "failed in-flight cleanup cannot be bypassed by timeout");
    check(ymcc::startupInputDecision(true, false, false, false, false, true) == S::Failed,
          "no physical source has bounded explicit failure");
    check(!ymcc::startupFrontendUsable(false, false, true), "unacknowledged DOM is not ready");
    check(!ymcc::startupFrontendUsable(false, true, false), "degraded flag without rendered fallback is not ready");
    check(ymcc::startupFrontendUsable(false, true, true), "actual fallback is usable failure UI");
    std::printf("STARTUP_NATIVE_PASS checks=%d hardwareOperations=0\n", checks);
}
