#include <windows.h>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include "../deps/json/json.hpp"
#include "../native/sleep_power_state.h"

namespace fspath = std::filesystem;
using nlohmann::json;
static unsigned long long fixtureGeneration = 10;
static std::uint64_t fixtureSerial = 0, fixtureSequence = 0;
static std::atomic<bool> g_exitRequested{false};
static bool fixtureAdmitted = true, fixtureTaskEnabled = true, closeBeforeSubmit = false;
static bool fixtureTaskReadable = true, closeDuringTaskRead = false;
static bool blockInsideSubmit = false, runResult = true;
static bool fixtureReadable = true, advanceOnProcessRead = false, advanceOnRun = false;
static bool advanceOnTaskRead = false, advanceBeforeRun = false, notifyOnProcessRead = false;
static unsigned calls = 0;
static std::optional<bool> fixtureRunning = false;
static fspath::path g_updateAccelPath;
[[maybe_unused]] static auto currentPowerGeneration() { return fixtureGeneration; }
[[maybe_unused]] static auto hardwareWriteAllowed() { return fixtureAdmitted; }
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* result, std::uint32_t) noexcept {
    using namespace ymcc::sleeppower;
    *result = {};
    if (g_exitRequested || !fixtureReadable) return 0;
    result->generation = fixtureGeneration;
    result->suspendSerial = fixtureSerial;
    result->observedNotificationSequence = fixtureSequence;
    result->flags = kKnown | (fixtureAdmitted ? kHardwareAllowed : kNotificationPending);
    return 1;
}
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& captured) noexcept {
    return ymcc::sleeppower::powerCurrent(&readSharedSystemPower, captured);
}
static void appendNativeLifecycleLog(const char*, const json&) {}
static auto readUpdateAccelRunning(int) {
    if (closeBeforeSubmit) fixtureAdmitted = false;
    if (advanceOnProcessRead) ++fixtureSerial;
    if (notifyOnProcessRead) ++fixtureSequence;
    return fixtureRunning;
}
static bool networkAccelerationWakeTaskEnabled(bool& result) {
    if (closeDuringTaskRead) fixtureAdmitted = false;
    if (advanceOnTaskRead) ++fixtureSerial;
    result = fixtureTaskEnabled;
    return fixtureTaskReadable;
}
#define YMCC_NETWORK_ACCEL_WAKE_FIXTURE
#if defined(YMCC_NETWORK_ACCEL_WAKE_OLD) || defined(YMCC_NETWORK_ACCEL_WAKE_GENERATION_ONLY)
#include <network_acceleration_wake.h>
#else
#include "../native/network_acceleration_wake.h"
#endif

#ifdef YMCC_NETWORK_ACCEL_WAKE_OLD
static bool networkAccelerationWakeSubmit(unsigned long long) {
    if (blockInsideSubmit) return false;
#elif defined(YMCC_NETWORK_ACCEL_WAKE_GENERATION_ONLY)
static bool networkAccelerationWakeSubmit(unsigned long long, bool& deferred) {
    if (blockInsideSubmit) { deferred = true; return false; }
#else
static bool networkAccelerationWakeSubmit(const ymcc::sleeppower::SharedPowerV1& captured, bool& deferred) {
    if (advanceBeforeRun) ++fixtureSerial;
    if (blockInsideSubmit || !sharedPowerLifecycleCurrent(captured)) { deferred = true; return false; }
#endif
    ++calls;
    if (advanceOnRun) ++fixtureSerial;
    return runResult;
}
static void check(bool ok,const char* name) { if (!ok) throw std::runtime_error(name); }
static auto wake(unsigned long long generation, std::uint64_t serial) {
#if defined(YMCC_NETWORK_ACCEL_WAKE_OLD) || defined(YMCC_NETWORK_ACCEL_WAKE_GENERATION_ONLY)
    return recheckNetworkAccelerationAfterWake(generation);
#else
    return recheckNetworkAccelerationAfterWake({generation, serial});
#endif
}
static auto checkedGeneration() {
#if defined(YMCC_NETWORK_ACCEL_WAKE_OLD) || defined(YMCC_NETWORK_ACCEL_WAKE_GENERATION_ONLY)
    return g_networkAccelerationWakeChecked;
#else
    return g_networkAccelerationWakeChecked.generation;
#endif
}
static void reset() {
    fixtureGeneration=10; fixtureSerial=0; fixtureSequence=0; g_exitRequested=false;
#if defined(YMCC_NETWORK_ACCEL_WAKE_OLD) || defined(YMCC_NETWORK_ACCEL_WAKE_GENERATION_ONLY)
    g_networkAccelerationWakeChecked=9;
#else
    g_networkAccelerationWakeChecked={9,0};
#endif
    fixtureAdmitted=true; fixtureTaskEnabled=true; closeBeforeSubmit=false; blockInsideSubmit=false;
    fixtureTaskReadable=true; closeDuringTaskRead=false;
    fixtureReadable=true; advanceOnProcessRead=false; advanceOnRun=false;
    advanceOnTaskRead=false; advanceBeforeRun=false; notifyOnProcessRead=false;
    runResult=true; calls=0; fixtureRunning=false;
}
static void expect(const char* reason, unsigned launches, unsigned long long selected=10, std::uint64_t serial=0) {
    const auto result=wake(selected, serial);
    check(result["reason"]==reason,reason);
    check(calls==launches,"launch count");
    if (std::string_view(reason)=="power-deferred-before-launch")
        check(result["generation"]==selected&&result["ok"]==false&&result["submitted"]==false,"typed proof");
#if !defined(YMCC_NETWORK_ACCEL_WAKE_OLD) && !defined(YMCC_NETWORK_ACCEL_WAKE_GENERATION_ONLY)
    check(result["generation"]==selected && result["suspendSerial"]==serial,"reply scope");
#endif
}

int main(int argc, char** argv) {
    try {
        check(argc==2,"expected an inert existing file path");
        g_updateAccelPath=argv[1];
        for (int block=0; block<3; ++block) {
            reset();
            if (block==0) fixtureAdmitted=false;
            if (block==1) closeBeforeSubmit=true;
            if (block==2) blockInsideSubmit=true;
            expect("power-deferred-before-launch",0);
            check(checkedGeneration()==9,"same generation retained");
            fixtureAdmitted=true;closeBeforeSubmit=false;blockInsideSubmit=false;
            expect("launch-request-accepted",1);
            expect("already-checked",1);
        }
        for (bool readable : {false, true}) {
            reset(); closeDuringTaskRead=true; fixtureTaskReadable=readable; fixtureTaskEnabled=false;
            expect("power-deferred-before-launch",0);
            check(checkedGeneration()==9,"revoked task read does not consume generation");
            fixtureAdmitted=true; closeDuringTaskRead=false; fixtureTaskReadable=true; fixtureTaskEnabled=true;
            expect("launch-request-accepted",1); expect("already-checked",1);
        }
        for (std::optional<bool> running : {std::optional<bool>{}, std::optional<bool>{true}}) {
            reset(); closeBeforeSubmit=true; fixtureRunning=running;
            expect("power-deferred-before-launch",0);
            check(checkedGeneration()==9,"revoked process read does not consume generation");
            fixtureAdmitted=true; closeBeforeSubmit=false; fixtureRunning=false;
            expect("launch-request-accepted",1); expect("already-checked",1);
        }
        reset(); runResult=false; expect("launch-request-failed",1); expect("already-checked",1);
        for (const bool taskRead : {false, true}) {
            reset(); fixtureTaskReadable=!taskRead;
            if (!taskRead) fixtureRunning=std::nullopt;
            const auto initial=wake(10,0);
            check(initial["reason"]==(taskRead ? "startup-state-unavailable" : "process-state-unavailable") &&
                initial["generation"]==10 && initial["suspendSerial"]==0 && initial["ok"]==false &&
                initial["submitted"]==false && calls==0,"unavailable read never called Run");
            fixtureTaskReadable=true; fixtureRunning=false;
            expect("launch-request-accepted",1); expect("already-checked",1);
        }
        reset(); fixtureRunning=true; expect("already-running",0); expect("already-checked",0);
        reset(); fixtureTaskEnabled=false; expect("startup-disabled",0); expect("already-checked",0);
        reset(); fixtureGeneration=11; expect("stale-or-suspended",0); expect("launch-request-accepted",1,11);
        reset(); g_exitRequested=true; expect("stale-or-suspended",0);
        reset(); expect("launch-request-accepted",1); ++fixtureSerial;
        expect("launch-request-accepted",2,10,1);
        expect("stale-or-suspended",2); expect("already-checked",2,10,1);
        for (auto advance : {&advanceOnTaskRead, &advanceOnProcessRead, &advanceBeforeRun}) {
            reset(); *advance=true;
            expect("stale-or-suspended",0);
            check(checkedGeneration()==9,"revoked scope was not consumed before Run");
            *advance=false;
            expect("launch-request-accepted",1,10,1); expect("already-checked",1,10,1);
        }
        reset(); notifyOnProcessRead=true; expect("power-deferred-before-launch",0);
        notifyOnProcessRead=false; expect("launch-request-accepted",1);
        reset(); fixtureReadable=false; expect("power-deferred-before-launch",0);
        fixtureReadable=true; expect("launch-request-accepted",1);
        reset(); advanceOnRun=true; runResult=false; expect("launch-request-failed",1);
        advanceOnRun=false; runResult=true; expect("stale-or-suspended",1);
        expect("launch-request-accepted",2,10,1);
        std::cout << "network acceleration native decision: 21 cases passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
