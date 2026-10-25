#!/usr/bin/env python3
"""Offline actual-source resume/diagnostic bridge regression (Python stdlib only).

Usage: python tools/msi_visibility_resume_generation_selftest.py --output-root DIR
Writes DIR/RESULT.json and a unique owned run directory. Exit 0 means positive
checks passed AND deleting only the new production hook failed the designated
Ready-without-Suspend assertion. Exit 2 means the production hook is not present.
No installed YMCC, observer, Steam, sleep, controller or fan API is executed.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[1]
DEFAULT_OUT = Path(r"G:\YeManCC-Work\Build\Validation\VC-OBSERVER-WAKE-WIRING-20261022\resume-generation")
REFERENCE_MAIN_SHA = "e39be3eef7e7e2355482f4ed527dd56abf07f5776ac30d2507467ca190fc46ce"
TARGET_CHECK = "ready-without-suspend:ready-arms-resolved-generation"
HOOK = "msiVisibilityObserverNoteResume(generation);"
INPUTS = ("native/main.cpp", "native/msi_visibility_wake_runtime.h",
          "native/msi_visibility_wake_diagnostics.h")


class MissingProductionHook(RuntimeError):
    """The parent has not yet supplied the production generation hook."""


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def save_json(path: Path, value) -> None:
    data = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    temporary = path.with_name(path.name + f".{os.getpid()}.tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


def mask_cpp(text: str) -> str:
    """Preserve offsets/newlines; mask comments, strings, chars and raw strings."""
    token = re.compile(r'//[^\r\n]*|/\*[\s\S]*?\*/|'
                       r'(?:u8|u|U|L)?R"([^\s()\\]{0,16})\([\s\S]*?\)\1"|'
                       r'"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
    return token.sub(lambda m: re.sub(r"[^\r\n]", " ", m.group()), text)


def extract_function(text: str, name: str) -> tuple[str, int]:
    masked = mask_cpp(text)
    pattern = r"\bstatic\s+void\s+" + re.escape(name) + r"\s*\([^{};]*\)\s*(?:noexcept\s*)?\{"
    matches = list(re.finditer(pattern, masked))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one complete actual definition of {name}, got {len(matches)}")
    match = matches[0]
    depth = 1
    end = match.end()
    while end < len(masked) and depth:
        depth += (masked[end] == "{") - (masked[end] == "}")
        end += 1
    if depth:
        raise RuntimeError(f"Unbalanced production function: {name}")
    body = text[match.start():end]
    if len(body.encode("utf-8")) > 65536 or re.search(r"^\s*#", mask_cpp(body), re.M):
        raise RuntimeError("Function exceeded the bounded, directive-free fixture contract")
    return body, match.start()


def unique_statement(text: str, statement: str) -> tuple[str, int]:
    masked = mask_cpp(text)
    matches = list(re.finditer(re.escape(mask_cpp(statement)), masked))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one actual statement: {statement}; got {len(matches)}")
    start = matches[0].start()
    actual = text[start:start + len(statement)]
    if actual != statement:
        raise RuntimeError(f"Actual call-site literal drift: {statement}")
    return actual, start


def extract_inputs(raw: dict[str, bytes]) -> tuple[dict[str, bytes], dict]:
    main = raw[INPUTS[0]].decode("utf-8")
    runtime = raw[INPUTS[1]].decode("utf-8")
    resume, start = extract_function(main, "handlePowerResumeNotification")
    if not re.match(r"static void handlePowerResumeNotification\(SgWork work, const char\* reason\)\s*\{", resume):
        raise RuntimeError("Resume signature changed; re-audit the stub boundary")
    masked = mask_cpp(resume)
    hooks = list(re.finditer(re.escape(HOOK), masked))
    if not hooks:
        raise MissingProductionHook("WAITING_FOR_PRODUCTION_HOOK: confirm the source version before running")
    if len(hooks) != 1 or not re.search(re.escape(HOOK) + r"\s*sgQueueWork\(work, generation\);\s*}\s*$", masked):
        raise RuntimeError("New hook must occur exactly once immediately before final sgQueueWork")
    allowed = set("handlePowerResumeNotification store load fetch_add if GetTickCount64 "
                  "appendNativeLifecycleLog currentPowerGeneration sgSleepRetryActive "
                  "stopPowerResumeWatchdog traceLog appendFanLifecycleLog powerLifecycleName "
                  "sgClearInternalSleepRequest KillTimer sgShouldKeepInputDuringStandby "
                  "sgSleepLeaseOwnedByGeneration sgQueueWork hidHideInvalidateRawInputIdentityCache "
                  "xinputSlotsMarkDiscoveryDirty openHardwareWriteGate sgHandheldResumeInputOnWake "
                  "sgRecordFact inputHostRequestWakeRescan closeHardwareWriteGate "
                  "inputHostRequestPowerRelease gamepadResetNativeState armPowerResumeWatchdog "
                  "ipc_emit msiVisibilityObserverNoteResume".split())
    actual_calls = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", masked))
    if actual_calls - allowed:
        raise RuntimeError("Unreviewed resume dependency: " + repr(sorted(actual_calls - allowed)))
    pieces = {"actual_resume.inc": resume.encode("utf-8")}
    locations = {"actual_resume.inc": {"source": INPUTS[0], "line": main.count("\n", 0, start) + 1}}
    for name in ("msiVisibilityObserverNoteResume", "msiVisibilityObserverWakeReady"):
        body, offset = extract_function(runtime, name)
        pieces[name + ".inc"] = body.encode("utf-8")
        locations[name + ".inc"] = {"source": INPUTS[1], "line": runtime.count("\n", 0, offset) + 1}
    for enum in ("SgWork", "PowerLifecycle", "SgRetryKind"):
        found = list(re.finditer(r"\benum class " + enum + r"\s*:\s*uint8_t\s*\{[^{}]*};", mask_cpp(main)))
        if len(found) != 1:
            raise RuntimeError("Actual enum changed: " + enum)
        m = found[0]
        pieces[enum + ".inc"] = main[m.start():m.end()].encode("utf-8")
    calls = {
        "actual_early_receipt.inc": "msiVisibilityObserverNoteResume(currentPowerGeneration());",
        "actual_ready_call.inc": "msiVisibilityObserverWakeReady(generation);",
        "actual_s4_call.inc": 'handlePowerResumeNotification(SgWork::WakeHibernate, "kernel-s4-wake");',
    }
    anchors = {"actual_early_receipt.inc": "case WM_POWERBROADCAST:",
               "actual_ready_call.inc": "case WM_POWER_INPUT_RECOVER:",
               "actual_s4_call.inc": "kernel-s4-wake"}
    for filename, call in calls.items():
        actual, offset = unique_statement(main, call)
        if anchors[filename] not in main[max(0, offset - 20000):offset]:
            raise RuntimeError("Actual call-site context drift: " + filename)
        pieces[filename] = actual.encode("utf-8")
        locations[filename] = {"source": INPUTS[0], "line": main.count("\n", 0, offset) + 1}
    pieces["msi_visibility_wake_diagnostics.h"] = raw[INPUTS[2]]
    return pieces, {"locations": locations, "resumeCalls": sorted(actual_calls),
                    "mainHasCRLF": b"\r\n" in raw[INPUTS[0]],
                    "functionHasCRLF": b"\r\n" in pieces["actual_resume.inc"],
                    "hookImmediatelyBeforeFinalQueue": True}


HARNESS = r'''
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "msi_visibility_wake_diagnostics.h"
#include "SgWork.inc"
#include "PowerLifecycle.inc"
#include "SgRetryKind.inc"
using ULONGLONG = unsigned long long;
using namespace ymcc::msi::visibility::wake;
static_assert(buildEnabled, "Both MSI diagnostic flags must be enabled");
static Policy g_msiVisibilityWakePolicy;
static std::atomic<unsigned long long> g_msiVisibilityWakePending{0};
static std::atomic<unsigned long long> g_powerGeneration{1}, g_resumeReadyGeneration{0};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_sensorSuspendCallbackActive{false}, g_inputReady{true};
static std::atomic<bool> g_inputHostPowerAdmissionRevoked{false}, g_sgSleepCycleActive{false};
static bool g_sgSleepQueryGateClosed=false, g_sgSleepQueryOwnsGeneration=false;
static ULONGLONG g_sgSleepQueryGeneration=0, g_sgRetryDueTick=0, g_lastResumeNotifyTick=0;
static bool g_sgRetryInProgress=false, g_sgModernStandbyActive=false, g_sgModernWakeClassified=false;
static ULONGLONG g_sgModernWakeClassifiedTick=0;
static std::uintptr_t g_hwnd=0;
static constexpr unsigned SG_RETRY_TIMER_ID=99;
struct FakeTask { ULONGLONG generation=0; SgRetryKind retryKind=SgRetryKind::None; };
static FakeTask g_sgTask;
static ULONGLONG fakeNow=1, ownedLeaseGeneration=0, watchdogGeneration=0;
static bool keepInput=false, hardwareGate=true, fixtureEligible=true;
static std::string scenario, noteOrigin="resolved-hook";
struct Work { SgWork kind; ULONGLONG generation; };
static std::vector<Work> queued;
static std::vector<std::string> effects;
static unsigned checks=0, failures=0;

static std::string quote(const std::string& s) {
    std::string out="\"";
    for (const unsigned char c : s) {
        if(c=='"'||c=='\\') { out+='\\'; out+=static_cast<char>(c); }
        else if(c=='\n') out+="\\n";
        else if(c=='\r') out+="\\r";
        else if(c=='\t') out+="\\t";
        else if(c<32) out+='?';
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
static const char* powerLifecycleName(PowerLifecycle p) {
    switch(p) {case PowerLifecycle::Ready:return "Ready";case PowerLifecycle::Suspending:return "Suspending";
        case PowerLifecycle::Suspended:return "Suspended";case PowerLifecycle::Resuming:return "Resuming";}
    return "invalid";
}
static ULONGLONG currentPowerGeneration() { return g_powerGeneration.load(); }
static ULONGLONG GetTickCount64() { return fakeNow; }
struct Value {
    std::string json;
    Value(const char* s):json(quote(s?s:"<null>")){}
    Value(bool b):json(b?"true":"false"){}
    template<class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T,bool>,int> =0>
    Value(T v):json(std::to_string(v)){}
};
using Fields=std::initializer_list<std::pair<const char*,Value>>;
static std::string fields(Fields values) {
    std::string out="{"; bool first=true;
    for(const auto& item:values) {if(!first)out+=',';first=false;out+=quote(item.first)+":"+item.second.json;}
    return out+"}";
}
static void record(const std::string& effect, ULONGLONG argument=0, const std::string& payload="{}") {
    effects.push_back(effect);
    const auto s=g_msiVisibilityWakePolicy.snapshot();
    std::cout<<"{\"type\":\"event\",\"scenario\":"<<quote(scenario)<<",\"effect\":"<<quote(effect)
        <<",\"argument\":"<<argument<<",\"tick\":"<<fakeNow<<",\"generation\":"<<currentPowerGeneration()
        <<",\"wakePending\":"<<g_msiVisibilityWakePending.load()
        <<",\"phase\":"<<quote(powerLifecycleName(g_powerLifecycle.load()))
        <<",\"power\":{\"hardwareGate\":"<<hardwareGate<<",\"inputReady\":"<<g_inputReady.load()
        <<",\"resumeReadyGeneration\":"<<g_resumeReadyGeneration.load()
        <<",\"inputHostPowerAdmissionRevoked\":"<<g_inputHostPowerAdmissionRevoked.load()
        <<",\"sensorSuspend\":"<<g_sensorSuspendCallbackActive.load()
        <<",\"queryClosed\":"<<g_sgSleepQueryGateClosed<<",\"queryOwnsGeneration\":"<<g_sgSleepQueryOwnsGeneration
        <<",\"queryGeneration\":"<<g_sgSleepQueryGeneration<<",\"watchdogGeneration\":"<<watchdogGeneration
        <<",\"lastResumeNotifyTick\":"<<g_lastResumeNotifyTick<<",\"retryActive\":"<<g_sgRetryInProgress
        <<",\"queuedWork\":"<<queued.size()<<"},\"policy\":{\"observedGeneration\":"<<s.observedGeneration
        <<",\"observedTick\":"<<s.observedTick<<",\"offeredGeneration\":"<<s.offeredGeneration
        <<",\"pendingGeneration\":"<<s.pendingGeneration<<",\"pending\":"<<s.pending
        <<",\"armedTick\":"<<s.armedTick<<",\"wakeAttempts\":"<<s.wakeAttempts
        <<"},\"payload\":"<<payload<<"}\n";
}
static void check(bool condition, const char* name) {
    ++checks;if(!condition)++failures;
    std::cout<<"{\"type\":\"check\",\"name\":"<<quote(scenario+":"+name)
             <<",\"pass\":"<<condition<<"}\n";
}
static unsigned count(const std::string& effect) {
    unsigned n=0;for(const auto& item:effects)if(item==effect)++n;return n;
}
static bool samePolicy(const State& a,const State& b) {
    return a.startupAttempted==b.startupAttempted && a.pending==b.pending && a.waitLogged==b.waitLogged &&
        a.observedGeneration==b.observedGeneration && a.observedTick==b.observedTick &&
        a.offeredGeneration==b.offeredGeneration && a.pendingGeneration==b.pendingGeneration &&
        a.armedTick==b.armedTick && a.wakeAttempts==b.wakeAttempts;
}
// Board/log/exit admission is fixture-owned; phase is checked as in production.
static bool msiVisibilityWakeDiagnosticEligible(bool requireReady) {
    return fixtureEligible && (!requireReady || g_powerLifecycle.load()==PowerLifecycle::Ready);
}
// Rename declarations only to record calls without editing extracted bytes.
#define msiVisibilityObserverNoteResume actualNoteResume
#include "msiVisibilityObserverNoteResume.inc"
#undef msiVisibilityObserverNoteResume
#define msiVisibilityObserverWakeReady actualWakeReady
#include "msiVisibilityObserverWakeReady.inc"
#undef msiVisibilityObserverWakeReady
static void msiVisibilityObserverNoteResume(ULONGLONG generation) noexcept {
    actualNoteResume(generation);record("note-resume-"+noteOrigin,generation);
}
static void msiVisibilityObserverWakeReady(ULONGLONG generation) noexcept {
    actualWakeReady(generation);record("wake-ready",generation);
}
// Non-diagnostic dependencies are in-memory recording stubs. No Windows/device/
// process headers, threads, real clocks or sleeps are used in this fixture.
static void appendNativeLifecycleLog(const char* event,Fields data) { record(std::string("native:")+event,0,fields(data)); }
static void appendFanLifecycleLog(const char* event,Fields data) { record(std::string("fan:")+event,0,fields(data)); }
static void sgRecordFact(const char* event,Fields data) { record(std::string("fact:")+event,0,fields(data)); }
static void ipc_emit(const char* event,Fields data) { record(std::string("ipc:")+event,0,fields(data)); }
template<class... T>static void traceLog(const char* format,T...) { record("trace",0,fields({{"format",format}})); }
static bool sgSleepRetryActive() { return g_sgRetryInProgress; }
static bool sgShouldKeepInputDuringStandby() { return keepInput; }
static bool sgSleepLeaseOwnedByGeneration(ULONGLONG generation) { return ownedLeaseGeneration==generation; }
static void stopPowerResumeWatchdog() { watchdogGeneration=0;record("stop-watchdog"); }
static void armPowerResumeWatchdog(ULONGLONG generation) { watchdogGeneration=generation;record("arm-watchdog",generation); }
static void sgClearInternalSleepRequest(const char* reason) { record("clear-internal-sleep",0,fields({{"reason",reason}})); }
static int KillTimer(std::uintptr_t handle,unsigned timer) { record("kill-owned-fake-timer",handle,fields({{"timer",timer}}));return 1; }
static void sgQueueWork(SgWork work,ULONGLONG generation) { queued.push_back({work,generation});record("queue-work",generation,fields({{"work",static_cast<int>(work)}})); }
static void hidHideInvalidateRawInputIdentityCache() { record("invalidate-identity"); }
static void xinputSlotsMarkDiscoveryDirty() { record("discovery-dirty"); }
static void openHardwareWriteGate() { hardwareGate=true;record("open-gate"); }
static void closeHardwareWriteGate(const char* reason) { hardwareGate=false;record("close-gate",0,fields({{"reason",reason}})); }
static void sgHandheldResumeInputOnWake(const char* reason) { record("handheld-resume-stub",0,fields({{"reason",reason}})); }
static void inputHostRequestWakeRescan(ULONGLONG generation) { record("input-rescan-stub",generation); }
static void inputHostRequestPowerRelease(ULONGLONG generation,const char* reason) { record("input-release-stub",generation,fields({{"reason",reason}})); }
static void gamepadResetNativeState(bool all) { record("gamepad-reset-stub",0,fields({{"all",all}})); }
#include "actual_resume.inc"

static void reset(const char* name,PowerLifecycle phase,ULONGLONG generation,ULONGLONG tick) {
    scenario=name;fakeNow=tick;queued.clear();effects.clear();noteOrigin="resolved-hook";
    g_msiVisibilityWakePolicy.restore(State{});g_msiVisibilityWakePending=0;
    g_powerGeneration=generation;g_resumeReadyGeneration=0;g_powerLifecycle=phase;
    g_sensorSuspendCallbackActive=true;g_inputReady=(phase==PowerLifecycle::Ready);
    g_inputHostPowerAdmissionRevoked=(phase!=PowerLifecycle::Ready);g_sgSleepCycleActive=false;
    g_sgSleepQueryGateClosed=true;g_sgSleepQueryOwnsGeneration=true;g_sgSleepQueryGeneration=generation;
    g_sgRetryInProgress=false;g_sgRetryDueTick=0;g_lastResumeNotifyTick=0;g_sgTask=FakeTask{};
    g_sgModernStandbyActive=false;g_sgModernWakeClassified=false;g_sgModernWakeClassifiedTick=0;
    g_hwnd=0;ownedLeaseGeneration=0;watchdogGeneration=0;keepInput=false;
    hardwareGate=(phase==PowerLifecycle::Ready);fixtureEligible=true;record("reset");
}
static void earlyReceipt() {
    noteOrigin="early-pbt";
#include "actual_early_receipt.inc"
    noteOrigin="resolved-hook";
}
// Ready commit is a fake boundary, not execution of the complete UI handler.
static void readyCommit(ULONGLONG generation) {
    g_powerLifecycle=PowerLifecycle::Ready;g_resumeReadyGeneration=generation;
    g_inputReady=true;hardwareGate=true;g_inputHostPowerAdmissionRevoked=false;record("fake-ready-commit",generation);
#include "actual_ready_call.inc"
}
static void kernelS4Call() {
#include "actual_s4_call.inc"
}
static void queryWasReleased() {
    check(!g_sensorSuspendCallbackActive.load()&&!g_sgSleepQueryGateClosed&&
        !g_sgSleepQueryOwnsGeneration&&g_sgSleepQueryGeneration==0,"query-and-sensor-bookkeeping-released");
}
static void readyWithoutSuspend() {
    reset("ready-without-suspend",PowerLifecycle::Ready,41,1000);
    earlyReceipt();const auto early=g_msiVisibilityWakePolicy.snapshot();
    check(early.observedGeneration==41&&early.observedTick==1000,"early-receipt-is-old-generation");
    fakeNow=1100;handlePowerResumeNotification(SgWork::WakeAutomatic,"resume_auto");
    check(currentPowerGeneration()==42,"actual-function-allocates-exactly-one-generation");
    check(g_powerLifecycle.load()==PowerLifecycle::Resuming,"actual-function-enters-resuming");
    check(queued.size()==1&&queued[0].generation==42&&queued[0].kind==SgWork::WakeAutomatic,"resolved-generation-is-queued");
    check(!g_msiVisibilityWakePolicy.snapshot().pending&&g_msiVisibilityWakePending.load()==0,"receipt-does-not-arm-before-ready");
    check(count("invalidate-identity")==1&&count("discovery-dirty")==1,"generation-change-invalidates-discovery");
    check(!hardwareGate&&!g_inputReady.load()&&count("input-release-stub")==1&&count("gamepad-reset-stub")==1,"normal-input-release-effects-recorded");
    check(watchdogGeneration==42&&g_lastResumeNotifyTick==1100&&count("ipc:power.resuming")==1,"watchdog-and-power-event-use-new-generation");
    queryWasReleased();
    msiVisibilityObserverWakeReady(41);
    check(g_msiVisibilityWakePending.load()==0,"stale-ready-does-not-arm");
    fakeNow=1300;readyCommit(42);const auto s=g_msiVisibilityWakePolicy.snapshot();
    // Deleting the hook must fail exactly here: old receipt 41 cannot arm 42.
    check(s.observedGeneration==42&&s.observedTick==1100&&s.pending&&s.pendingGeneration==42&&
        s.offeredGeneration==42&&s.wakeAttempts==1&&s.armedTick==1300&&g_msiVisibilityWakePending.load()==42,
        "ready-arms-resolved-generation");
}
static void normalSuspended() {
    reset("normal-suspended",PowerLifecycle::Suspended,81,2000);earlyReceipt();
    fakeNow=2100;handlePowerResumeNotification(SgWork::WakeSuspend,"resume_suspend");
    check(currentPowerGeneration()==81,"suspended-generation-preserved");
    check(count("invalidate-identity")==0&&count("discovery-dirty")==1,"no-new-generation-invalidation");
    check(count("note-resume-resolved-hook")==1&&g_msiVisibilityWakePolicy.snapshot().observedTick==2000,"same-generation-hook-does-not-delay-receipt");
    check(queued.size()==1&&queued[0].generation==81&&queued[0].kind==SgWork::WakeSuspend,"same-generation-work-queued");
    check(!g_msiVisibilityWakePolicy.snapshot().pending,"suspended-not-armed-before-ready");
    fakeNow=2300;readyCommit(81);const auto s=g_msiVisibilityWakePolicy.snapshot();
    check(s.pending&&s.pendingGeneration==81&&s.wakeAttempts==1&&s.armedTick==2300,"suspended-ready-arms-once");
    queryWasReleased();
}
static void duplicateResume() {
    reset("duplicate-resume",PowerLifecycle::Ready,100,3000);earlyReceipt();
    fakeNow=3010;handlePowerResumeNotification(SgWork::WakeAutomatic,"resume_auto");
    const auto receipt=g_msiVisibilityWakePolicy.snapshot();
    fakeNow=3100;earlyReceipt();fakeNow=3110;handlePowerResumeNotification(SgWork::WakeSuspend,"resume_suspend");
    check(currentPowerGeneration()==101&&queued.size()==2&&queued[1].generation==101,"duplicate-during-resuming-keeps-generation");
    check(samePolicy(receipt,g_msiVisibilityWakePolicy.snapshot()),"duplicate-resuming-does-not-move-receipt-clock");
    check(count("discovery-dirty")==1&&count("close-gate")==1&&count("input-release-stub")==1,"duplicate-resuming-does-not-repeat-power-release");
    check(count("arm-watchdog")==2&&g_lastResumeNotifyTick==3010,"existing-watchdog-behavior-preserved");
    fakeNow=3200;readyCommit(101);const auto armed=g_msiVisibilityWakePolicy.snapshot();
    check(armed.pending&&armed.wakeAttempts==1&&armed.armedTick==3200,"one-pending-observation-after-two-resumes");
    fakeNow=3250;readyCommit(101);
    check(samePolicy(armed,g_msiVisibilityWakePolicy.snapshot()),"repeated-ready-does-not-rearm-or-delay");
    fakeNow=3300;earlyReceipt();handlePowerResumeNotification(SgWork::WakeSuspend,"resume_suspend");
    check(queued.size()==3&&queued.back().kind==SgWork::WakeSuspend,"committed-explicit-duplicate-still-queues-work");
    check(samePolicy(armed,g_msiVisibilityWakePolicy.snapshot()),"committed-explicit-duplicate-keeps-entire-policy");
    fakeNow=3400;earlyReceipt();handlePowerResumeNotification(SgWork::WakeAutomatic,"resume_auto");
    check(queued.size()==3&&currentPowerGeneration()==101,"committed-automatic-duplicate-not-queued");
    check(samePolicy(armed,g_msiVisibilityWakePolicy.snapshot())&&count("note-resume-resolved-hook")==2,"early-return-does-not-create-another-observation");
    ownedLeaseGeneration=101;fakeNow=10000;earlyReceipt();
    handlePowerResumeNotification(SgWork::WakeSuspend,"late_user_wake");readyCommit(101);
    check(queued.size()==4&&currentPowerGeneration()==101,"late-owned-lease-recovery-keeps-generation");
    check(samePolicy(armed,g_msiVisibilityWakePolicy.snapshot()),"late-duplicate-does-not-extend-pending-deadline");
    check(count("close-gate")==1&&count("discovery-dirty")==1&&g_lastResumeNotifyTick==3010,"committed-duplicates-preserve-power-effects");
    fakeNow=armed.armedTick+pendingMaxAgeMs;
    const auto decision=g_msiVisibilityWakePolicy.poll(true,101,fakeNow,Active::Running,101);
    record("policy-expiry",101,fields({{"reason",decision.reason}}));
    check(decision.action==Action::Refuse&&std::string(decision.reason)=="wake-diagnostic-pending-expired"&&
        !g_msiVisibilityWakePolicy.snapshot().pending,"duplicates-cannot-postpone-original-expiry");
    msiVisibilityObserverWakeReady(101);
    check(!g_msiVisibilityWakePolicy.snapshot().pending&&g_msiVisibilityWakePolicy.snapshot().wakeAttempts==1,"expired-same-generation-cannot-rearm");
}
static void retryRejected(SgWork work,bool early) {
    reset(early?"retry-auto-with-early-receipt":"retry-hibernate-without-pbt",PowerLifecycle::Suspended,400,20000);
    g_msiVisibilityWakePolicy.noteResume(true,399,1000);
    (void)g_msiVisibilityWakePolicy.arm(true,399,1100);g_msiVisibilityWakePolicy.cancel(399);
    g_sgRetryInProgress=true;g_sgRetryDueTick=25000;g_sgTask={400,SgRetryKind::UnexpectedWake};watchdogGeneration=400;
    if(early)earlyReceipt();const auto before=g_msiVisibilityWakePolicy.snapshot();
    fakeNow=20100;handlePowerResumeNotification(work,"retry_signal");
    check(g_sgRetryInProgress&&g_sgRetryDueTick==25000&&g_sgTask.retryKind==SgRetryKind::UnexpectedWake,"retry-ownership-preserved");
    check(currentPowerGeneration()==400&&g_powerLifecycle.load()==PowerLifecycle::Suspended,"rejected-retry-keeps-power-generation-and-phase");
    check(queued.empty()&&count("note-resume-resolved-hook")==0,"rejected-retry-never-reaches-new-hook-or-queue");
    check(samePolicy(before,g_msiVisibilityWakePolicy.snapshot())&&g_msiVisibilityWakePending.load()==0,"rejected-retry-adds-no-observation");
    check(watchdogGeneration==0&&count("stop-watchdog")==1,"retry-stops-watchdog");
    check(count("fan:fan-host-wake-deferred")==1&&count("close-gate")==0&&count("discovery-dirty")==0&&
        count("input-release-stub")==0&&count("ipc:power.resuming")==0,"retry-deferral-recorded-without-resume-effects");
    fakeNow=20200;msiVisibilityObserverWakeReady(400);
    check(samePolicy(before,g_msiVisibilityWakePolicy.snapshot())&&g_msiVisibilityWakePending.load()==0,"non-ready-retry-cannot-arm-through-ready-bridge");
    queryWasReleased();
}
static void explicitRetryCancellation() {
    reset("retry-explicit-user-keep-input",PowerLifecycle::Suspended,500,30000);
    g_sgRetryInProgress=true;g_sgRetryDueTick=35000;g_sgTask={500,SgRetryKind::UnexpectedWake};
    keepInput=true;g_hwnd=1;g_sgModernStandbyActive=true;earlyReceipt();
    fakeNow=30100;handlePowerResumeNotification(SgWork::WakeSuspend,"resume_suspend");
    check(!g_sgRetryInProgress&&g_sgRetryDueTick==0&&g_sgTask.retryKind==SgRetryKind::None,"explicit-wake-cancels-retry");
    check(count("clear-internal-sleep")==1&&count("kill-owned-fake-timer")==1,"owned-retry-cleanup-is-recorded");
    check(!g_sgModernStandbyActive&&g_sgModernWakeClassified&&g_sgModernWakeClassifiedTick==30100,"explicit-keep-input-standby-classification");
    check(hardwareGate&&g_inputReady.load()&&!g_inputHostPowerAdmissionRevoked.load()&&
        count("open-gate")==1&&count("handheld-resume-stub")==1&&count("input-rescan-stub")==1,"keep-input-effects-are-stubbed-and-recorded");
    check(count("input-release-stub")==0&&count("gamepad-reset-stub")==0,"keep-input-does-not-request-release");
    check(queued.size()==1&&queued[0].generation==500,"explicit-user-work-preserves-generation");
    fakeNow=30200;readyCommit(500);
    check(g_msiVisibilityWakePolicy.snapshot().pending&&g_msiVisibilityWakePolicy.snapshot().wakeAttempts==1,"explicit-user-ready-arms-one-observation");
}
static void kernelWithoutPbt(bool startsReady) {
    reset(startsReady?"kernel-s4-ready-without-pbt":"kernel-s4-suspended-without-pbt",
        startsReady?PowerLifecycle::Ready:PowerLifecycle::Suspended,600,40000);
    kernelS4Call();const ULONGLONG expected=startsReady?601:600;
    const auto receipt=g_msiVisibilityWakePolicy.snapshot();
    check(count("note-resume-early-pbt")==0&&count("note-resume-resolved-hook")==1,"no-early-pbt-receipt-needed");
    check(currentPowerGeneration()==expected&&receipt.observedGeneration==expected&&receipt.observedTick==40000,"kernel-call-records-resolved-generation");
    check(queued.size()==1&&queued[0].kind==SgWork::WakeHibernate&&queued[0].generation==expected,"actual-kernel-call-queues-hibernate-work");
    check(!receipt.pending&&receipt.wakeAttempts==0,"kernel-receipt-waits-for-ready");
    fakeNow=40100;readyCommit(expected);const auto armed=g_msiVisibilityWakePolicy.snapshot();
    check(armed.pending&&armed.pendingGeneration==expected&&armed.armedTick==40100&&armed.wakeAttempts==1,"kernel-ready-arms-without-pbt");
    queryWasReleased();
}
static void ineligibleDiagnostic() {
    reset("diagnostic-ineligible",PowerLifecycle::Ready,700,50000);fixtureEligible=false;
    earlyReceipt();handlePowerResumeNotification(SgWork::WakeAutomatic,"resume_auto");
    check(currentPowerGeneration()==701&&queued.size()==1,"diagnostic-ineligibility-keeps-public-resume-flow");
    readyCommit(701);const auto s=g_msiVisibilityWakePolicy.snapshot();
    check(!s.observedGeneration&&!s.pending&&!s.wakeAttempts&&g_msiVisibilityWakePending.load()==0,"ineligible-diagnostic-never-observes-or-arms");
}
int main(int argc,char** argv) {
    std::cout<<std::boolalpha;
    const bool regressionOnly=argc==2&&std::string(argv[1])=="--regression-only";
    if(argc>1&&!regressionOnly)return 64;
    readyWithoutSuspend();
    if(!regressionOnly) {
        normalSuspended();duplicateResume();retryRejected(SgWork::WakeAutomatic,true);
        retryRejected(SgWork::WakeHibernate,false);explicitRetryCancellation();
        kernelWithoutPbt(false);kernelWithoutPbt(true);ineligibleDiagnostic();
    }
    std::cout<<"{\"type\":\"summary\",\"checks\":"<<checks<<",\"failures\":"<<failures
        <<",\"observerProcessesCreated\":0,\"realHardwareOperations\":0,\"realSleepOperations\":0}\n";
    return failures?1:0;
}
'''


def short_environment(run: Path) -> dict[str, str]:
    allowed = {"SYSTEMROOT", "WINDIR", "SYSTEMDRIVE", "COMSPEC", "PROGRAMFILES", "PROGRAMFILES(X86)",
               "PROGRAMW6432", "USERPROFILE", "HOMEDRIVE", "HOMEPATH", "PUBLIC", "APPDATA", "LOCALAPPDATA",
               "PROCESSOR_ARCHITECTURE", "PROCESSOR_IDENTIFIER", "NUMBER_OF_PROCESSORS", "OS"}
    env = {key.upper(): value for key, value in os.environ.items() if key.upper() in allowed}
    system = Path(env.get("SYSTEMROOT", r"C:\Windows"))
    scratch = run / "owned-temp"
    scratch.mkdir()
    env.update(PATH=os.pathsep.join(map(str, (system / "System32", system))),
               TEMP=str(scratch), TMP=str(scratch), VSLANG="1033", VSCMD_SKIP_SENDTELEMETRY="1")
    return env


def toolchain(run: Path, override: Path | None) -> tuple[dict[str, str], dict]:
    env = short_environment(run)
    pf = Path(env.get("PROGRAMFILES(X86)", r"C:\Program Files (x86)"))
    candidates = [override] if override else sorted(pf.glob("Microsoft Visual Studio/*/*/VC/Auxiliary/Build/vcvars64.bat"), reverse=True)
    candidates = [path for path in candidates if path and path.is_file()]
    if not candidates:
        raise RuntimeError("vcvars64.bat not found; pass --vcvars64 PATH")
    vcvars = candidates[0]
    # Start every child from a short allowlisted PATH; no unrelated parent
    # environment variables or credentials are serialized into the evidence.
    batch = run / "owned-vcvars.cmd"
    batch.write_bytes((f'@echo off\r\ncall "{vcvars}" > "{run / "vcvars.log"}" 2>&1\r\n'
                       'if errorlevel 1 exit /b %errorlevel%\r\n'
                       f'set > "{run / "vcvars.env"}"\r\n').encode("utf-8"))
    proc = subprocess.run([str(Path(env["SYSTEMROOT"]) / "System32/cmd.exe"), "/d", "/u", "/c", str(batch)],
                          cwd=run, env=env, capture_output=True, timeout=60)
    if proc.returncode:
        raise RuntimeError(f"vcvars64 failed ({proc.returncode}); see {run / 'vcvars.log'}")
    built_env = {}
    for line in (run / "vcvars.env").read_bytes().decode("utf-16-le").splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            built_env[key.upper()] = value
    kit = pf / "Windows Kits/10"
    versions = sorted((p for p in (kit / "Include").glob("*") if re.fullmatch(r"\d+(?:\.\d+)+", p.name)),
                      key=lambda p: tuple(map(int, p.name.split("."))), reverse=True)
    sdk = next((p for p in versions if (p / "ucrt/corecrt.h").is_file() and (p / "um/Windows.h").is_file()
                and (kit / "Lib" / p.name / "um/x64/kernel32.lib").is_file()
                and (kit / "Lib" / p.name / "ucrt/x64/libucrt.lib").is_file()), None)
    if sdk is None:
        raise RuntimeError("No complete local Windows SDK (ucrt/um include and x64 lib)")
    includes = [str(sdk / part) for part in ("ucrt", "shared", "um", "winrt") if (sdk / part).is_dir()]
    libraries = [str(kit / "Lib" / sdk.name / part / "x64") for part in ("ucrt", "um")]
    built_env["INCLUDE"] = ";".join(includes + [built_env.get("INCLUDE", "")])
    built_env["LIB"] = ";".join(libraries + [built_env.get("LIB", "")])
    compiler = shutil.which("cl.exe", path=built_env.get("PATH", ""))
    dumpbin = shutil.which("dumpbin.exe", path=built_env.get("PATH", ""))
    if not compiler or not dumpbin:
        raise RuntimeError("vcvars did not resolve cl.exe and dumpbin.exe")
    return built_env, {"vcvars64": str(vcvars), "compiler": compiler, "dumpbin": dumpbin,
                       "windowsSdkVersion": sdk.name, "explicitSdkInclude": includes,
                       "explicitSdkLib": libraries, "initialChildPath": env["PATH"],
                       "buildChildPathCharacters": len(built_env.get("PATH", ""))}


def child(args: list[str], cwd: Path, env: dict[str, str], stem: str, timeout: int = 60) -> subprocess.CompletedProcess:
    save_json(cwd / (stem + ".command.json"), args)
    proc = subprocess.run(args, cwd=cwd, env=env, capture_output=True, timeout=timeout)
    (cwd / (stem + ".stdout.txt")).write_bytes(proc.stdout)
    (cwd / (stem + ".stderr.txt")).write_bytes(proc.stderr)
    return proc


def run_fixture(folder: Path, pieces: dict[str, bytes], env: dict[str, str], tc: dict, negative: bool) -> dict:
    folder.mkdir()
    for name, data in pieces.items():
        (folder / name).write_bytes(data)
    (folder / "fixture.cpp").write_bytes(HARNESS.encode("utf-8"))
    exe = folder / "fixture.exe"
    command = [tc["compiler"], "/nologo", "/std:c++17", "/utf-8", "/EHsc", "/W4", "/WX", "/wd4505", "/O2", "/MT",
               "/DYMCC_MSI_VISIBILITY_DIAGNOSTIC=1", "/DYMCC_MSI_VISIBILITY_WAKE_DIAGNOSTIC=1",
               str(folder / "fixture.cpp"), "/Fo" + str(folder / "fixture.obj"), "/Fe" + str(exe)]
    compiled = child(command, folder, env, "compile")
    if compiled.returncode:
        raise RuntimeError(f"{folder.name} compile failed; see {folder / 'compile.stdout.txt'}")
    imports = child([tc["dumpbin"], "/nologo", "/imports", str(exe)], folder, env, "imports")
    if imports.returncode:
        raise RuntimeError("Owned fixture import audit failed")
    import_text = imports.stdout.decode("utf-8", "replace")
    forbidden = re.findall(r"\b(?:CreateProcess[AW]|ShellExecute(?:Ex)?[AW]|DeviceIoControl|HidD_\w+|SetSuspendState|InitiateSystemShutdown\w*)\b", import_text)
    if forbidden:
        raise RuntimeError("Unexpected process/device/power import: " + repr(forbidden))
    executed = child([str(exe)] + (["--regression-only"] if negative else []), folder, env, "execute", 15)
    rows = [json.loads(line) for line in executed.stdout.decode("utf-8").splitlines() if line.strip()]
    checks = [row for row in rows if row.get("type") == "check"]
    summaries = [row for row in rows if row.get("type") == "summary"]
    if len(summaries) != 1 or summaries[0]["checks"] != len(checks) or not checks or len({r["name"] for r in checks}) != len(checks):
        raise RuntimeError("Invalid/incomplete/duplicate fixture check protocol")
    failed = [row["name"] for row in checks if not row["pass"]]
    if summaries[0]["failures"] != len(failed) or executed.stderr:
        raise RuntimeError("Fixture failure count or stderr violated the result contract")
    if negative:
        accepted = executed.returncode == 1 and failed == [TARGET_CHECK]
        final = next((row for row in reversed(rows) if row.get("effect") == "wake-ready"), {})
        policy = final.get("policy", {})
        accepted = accepted and final.get("generation") == 42 and policy.get("observedGeneration") == 41 and not policy.get("pending") and final.get("wakePending") == 0
    else:
        accepted = executed.returncode == 0 and not failed
    save_json(folder / "events-and-checks.json", rows)
    return {"pass": bool(accepted), "kind": "expected-failure" if negative else "positive",
            "checks": len(checks), "failedChecks": failed, "exitCode": executed.returncode,
            "executableSha256": sha(exe.read_bytes()), "fixtureSha256": sha((folder / "fixture.cpp").read_bytes()),
            "importAuditPass": not forbidden, "observerStartupTested": False,
            "summary": summaries[0], "directory": str(folder)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--vcvars64", type=Path, help="Optional local vcvars64.bat override")
    args = parser.parse_args()
    output = args.output_root.resolve()
    if output == REPO or REPO in output.parents or output == Path(output.anchor):
        parser.error("--output-root must be an evidence directory outside the source repository")
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-" + dt.datetime.now().strftime("%Y%m%d-%H%M%S-"), dir=output))
    result = {"schemaVersion": 1, "pass": False, "status": "FAIL", "checks": 0, "outputRoot": str(output),
              "run": str(run), "runner": str(Path(__file__).resolve()),
              "observerStartupTested": False, "realDeviceSteamInstallSleepFanIO": False,
              "limits": ["Only the complete extracted resume function and two extracted diagnostic bridges execute.",
                         "Ready commit, eligibility, clock, generation globals and all other dependencies are fake in-memory boundaries.",
                         "Kernel-S4 call expression executes; its full window handler and real event delivery do not.",
                         "No observer startup/child liveness, hardware, Steam, installed application, physical wake or full native TU acceptance.",
                         "Sequential deterministic event schedules do not prove concurrent or physical behavior.",
                         "Unrelated main.cpp edits are recorded; extracted function/call-site/header/runner drift fails."]}
    raw_initial = {name: (REPO / name).read_bytes() for name in INPUTS}
    runner_initial = Path(__file__).read_bytes()
    (run / "runner.snapshot.py").write_bytes(runner_initial)
    for name, data in raw_initial.items():
        (run / (Path(name).name + ".snapshot")).write_bytes(data)
    stages = []
    try:
        pieces, details = extract_inputs(raw_initial)
        bound_hashes = {name: sha(data) for name, data in pieces.items()}
        result["binding"] = {**details, "sourceSha256Before": {name: sha(data) for name, data in raw_initial.items()},
                             "extractSha256": bound_hashes, "runnerSha256": sha(runner_initial),
                             "userReferenceMainSha256": REFERENCE_MAIN_SHA,
                             "matchesUserReferenceMain": sha(raw_initial[INPUTS[0]]) == REFERENCE_MAIN_SHA,
                             "runtimeAdapterRenameOnly": True}
        save_json(run / "source-binding.initial.json", result["binding"])

        def verify(stage: str) -> None:
            current = {name: (REPO / name).read_bytes() for name in INPUTS}
            current_pieces, _ = extract_inputs(current)
            now_hashes = {name: sha(data) for name, data in current_pieces.items()}
            changes = [name for name in sorted(bound_hashes.keys() | now_hashes.keys()) if bound_hashes.get(name) != now_hashes.get(name)]
            if current[INPUTS[1]] != raw_initial[INPUTS[1]]:
                changes.append(INPUTS[1])
            if Path(__file__).read_bytes() != runner_initial:
                changes.append("runner")
            stages.append({"stage": stage, "pass": not changes, "changedBoundInputs": changes,
                           "mainSha256": sha(current[INPUTS[0]]), "mainFullSourceChanged": current[INPUTS[0]] != raw_initial[INPUTS[0]],
                           "resumeSha256": now_hashes["actual_resume.inc"]})
            save_json(run / "source-drift-checks.json", stages)
            if changes:
                raise RuntimeError("Bound actual source drift at " + stage + ": " + repr(changes))

        env, tc = toolchain(run, args.vcvars64)
        result["toolchain"] = tc
        verify("before-positive-build")
        result["positive"] = run_fixture(run / "positive", pieces, env, tc, False)
        verify("after-positive-execution")
        if not result["positive"]["pass"]:
            raise RuntimeError("Positive actual-source regression failed")
        # Delete only the code statement, preserving all other function bytes
        # including CRLF, comments and the earlier production PBT receipt.
        mutant = dict(pieces)
        mutant["actual_resume.inc"] = pieces["actual_resume.inc"].replace(HOOK.encode("ascii"), b"", 1)
        result["negativeMutation"] = {"operation": "delete-only-resolved-generation-hook",
                                      "removed": HOOK, "expectedFailedCheck": TARGET_CHECK,
                                      "originalFunctionSha256": bound_hashes["actual_resume.inc"],
                                      "mutantFunctionSha256": sha(mutant["actual_resume.inc"]),
                                      "productionSourceModified": False, "earlyPbtReceiptRetained": True}
        verify("before-negative-build")
        result["negative"] = run_fixture(run / "negative-delete-hook", mutant, env, tc, True)
        verify("after-negative-execution")
        if not result["negative"]["pass"]:
            raise RuntimeError("Deletion negative did not fail exactly at the old-generation Ready arm assertion")
        rows = json.loads((run / "positive/events-and-checks.json").read_text("utf-8"))
        events = [r for r in rows if r.get("type") == "event"]
        ready_events = [r for r in events if r["scenario"] == "ready-without-suspend"]
        hook_at = next(i for i, r in enumerate(ready_events) if r["effect"] == "note-resume-resolved-hook")
        queue_at = next(i for i, r in enumerate(ready_events) if r["effect"] == "queue-work")
        evidence_checks = [
            {"name": "trace:resolved-hook-before-queue", "pass": hook_at < queue_at and ready_events[hook_at]["policy"]["observedGeneration"] == 42},
            {"name": "trace:early-pbt-receipt-retained", "pass": any(r["effect"] == "note-resume-early-pbt" and r["policy"]["observedGeneration"] == 41 for r in ready_events)},
            {"name": "trace:retry-deferrals-deny-hardware-writes", "pass": sum(r["effect"] == "fan:fan-host-wake-deferred" and r["payload"].get("hardwareWritesAdmitted") is False for r in events) == 2},
        ]
        result["evidenceChecks"] = evidence_checks
        verify("final-source-binding")
        result["sourceDriftChecks"] = stages
        result["checks"] = result["positive"]["checks"] + len(evidence_checks) + len(stages) + 1
        result["checkCounts"] = {"positiveRuntime": result["positive"]["checks"], "evidenceAssertions": len(evidence_checks),
                                 "sourceDriftStages": len(stages), "requiredDeletionNegative": 1,
                                 "negativeRuntimeAssertions": result["negative"]["checks"]}
        result["negativeExpectedFailureDetected"] = result["negative"]["pass"]
        result["pass"] = all(item["pass"] for item in evidence_checks)
        result["status"] = "PASS" if result["pass"] else "FAIL"
    except MissingProductionHook as error:
        result["status"] = "FAIL" if "binding" in result else "WAITING_FOR_PRODUCTION_HOOK"
        result["error"] = str(error)
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = f"{type(error).__name__}: {error}"
    finally:
        result["sourceDriftChecks"] = stages
        final_raw = {name: (REPO / name).read_bytes() for name in INPUTS}
        result["sourceSha256After"] = {name: sha(data) for name, data in final_raw.items()}
        result["mainFullSourceChanged"] = final_raw[INPUTS[0]] != raw_initial[INPUTS[0]]
        if result["mainFullSourceChanged"]:
            (run / "main.cpp.final.snapshot").write_bytes(final_raw[INPUTS[0]])
        if result["pass"]:
            # Compare the final recorded bytes too, so a change between the
            # last scheduled check and result publication cannot get a PASS.
            try:
                final_pieces, _ = extract_inputs(final_raw)
                final_hashes = {name: sha(data) for name, data in final_pieces.items()}
                stable = (final_hashes == bound_hashes and final_raw[INPUTS[1]] == raw_initial[INPUTS[1]]
                          and Path(__file__).read_bytes() == runner_initial)
                result["finalSnapshotBindingStable"] = stable
                if not stable:
                    raise RuntimeError("Bound source changed in the completion snapshot")
            except Exception as error:
                result["pass"] = False
                result["status"] = "FAIL"
                result["error"] = f"{type(error).__name__}: {error}"
        result["finishedAt"] = dt.datetime.now().astimezone().isoformat()
        save_json(run / "RESULT.json", result)
        save_json(output / "RESULT.json", result)
    print(f"{result['status']} checks={result['checks']} RESULT={output / 'RESULT.json'}")
    if "error" in result:
        print(result["error"], file=sys.stderr)
    return 0 if result["pass"] else 2 if result["status"] == "WAITING_FOR_PRODUCTION_HOOK" else 1


if __name__ == "__main__":
    raise SystemExit(main())
