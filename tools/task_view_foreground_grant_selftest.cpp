#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include "json.hpp"

// Deliberately undefined: using the real adapter must fail at link time.
namespace forbidden_os {
ULONGLONG tick(); DWORD thread(); DWORD process(); DWORD window(HWND, LPDWORD);
HWND foreground(); DWORD error(); void setError(DWORD); BOOL allow(DWORD);
LRESULT send(HWND, UINT, WPARAM, LPARAM, UINT, UINT, PDWORD_PTR);
}
#define GetTickCount64 forbidden_os::tick
#define GetCurrentThreadId forbidden_os::thread
#define GetCurrentProcessId forbidden_os::process
#define GetWindowThreadProcessId forbidden_os::window
#define GetForegroundWindow forbidden_os::foreground
#define GetLastError forbidden_os::error
#define SetLastError forbidden_os::setError
#define AllowSetForegroundWindow forbidden_os::allow
#define SendMessageTimeoutW forbidden_os::send
#include "../native/task_view_foreground_grant.h"

using json = nlohmann::json;
namespace fg = ymcc::taskView::foreground;
namespace fixture {
unsigned checks = 0;
json cases = json::array(), records = json::array();
void check(bool ok, const std::string& reason) {
    ++checks;
    if (!ok) throw std::runtime_error(reason);
}
HWND hwnd(std::uintptr_t value) { return reinterpret_cast<HWND>(value); }
constexpr DWORD poolThread = 3168, uiThread = 12412, ownPid = 17336, readyChild = 11056;
static_assert(std::is_integral_v<WPARAM> && sizeof(WPARAM) == sizeof(UINT_PTR));
static_assert(!std::is_copy_constructible_v<fg::Broker>);
struct Window { DWORD thread, pid; };
struct Attempt { DWORD pid, thread; HWND foreground; };
struct FakeOps {
    fg::Broker& broker;
    ULONGLONG clock = 1000;
    DWORD current = poolThread, deliveryThread = uiThread, lastError = 0;
    HWND foreground = hwnd(1);
    std::map<std::uintptr_t, Window> windows{{1,{uiThread,ownPid}}, {2,{uiThread,ownPid}},
        {3,{9000,9999}}, {4,{9001,ownPid}}};
    bool apiAccepted = true, denyPool = true, sendAccepted = true, deliverMessage = true, failAfterDelivery = false, apiAssignsError = true;
    DWORD apiError = ERROR_ACCESS_DENIED, sendError = ERROR_TIMEOUT;
    DWORD advance = 0, sendCalls = 0, waitSeen = 0;
    DWORD_PTR emptyReply = static_cast<DWORD_PTR>(fg::encodeReply({fg::DeliveryStage::InvalidToken, ERROR_CANCELLED}));
    WPARAM tokenSeen = 0;
    HWND destination = nullptr;
    std::vector<WPARAM> tokens;
    std::vector<Attempt> allows;
    std::vector<fg::DeliveryReceipt> deliveries;
    std::vector<std::string> trace;
    std::function<void()> beforeDeliver, afterDeliver, onAllow;
    explicit FakeOps(fg::Broker& value) : broker(value) {}
    ULONGLONG now() noexcept { trace.push_back("now"); return clock; }
    DWORD threadId() noexcept { trace.push_back("threadId"); return current; }
    DWORD processId() noexcept { trace.push_back("processId"); return ownPid; }
    DWORD windowThread(HWND window, DWORD& pid) noexcept {
        trace.push_back("windowThread");
        auto found = windows.find(reinterpret_cast<std::uintptr_t>(window));
        pid = found == windows.end() ? 0 : found->second.pid;
        return found == windows.end() ? 0 : found->second.thread;
    }
    HWND foregroundWindow() noexcept { trace.push_back("foregroundWindow"); return foreground; }
    DWORD error() noexcept { trace.push_back("error"); return lastError; }
    void setError(DWORD value) noexcept { trace.push_back("setError"); lastError = value; }
    bool allow(DWORD pid) noexcept {
        trace.push_back("allow"); allows.push_back({pid,current,foreground});
        if (onAllow) onAllow();
        if (!apiAccepted || (denyPool && current != uiThread)) { if (apiAssignsError) lastError = apiError; return false; }
        return true;
    }
    DWORD uiDeliver(HWND window, WPARAM token) noexcept {
        const auto savedThread = current, savedError = lastError;
        current = deliveryThread;
        fg::DeliveryReceipt receipt;
        const DWORD result = broker.deliver(*this,window,token,&receipt);
        deliveries.push_back(receipt);
        current = savedThread; lastError = savedError;
        return result;
    }
    bool send(HWND window, WPARAM token, DWORD wait, DWORD_PTR& result) noexcept {
        trace.push_back("send"); ++sendCalls; destination = window; tokenSeen = token;
        tokens.push_back(token); waitSeen = wait; clock += advance;
        if (beforeDeliver) beforeDeliver();
        if (!sendAccepted) { lastError = sendError; return false; }
        if (deliverMessage) { uiDeliver(window,token); result = static_cast<DWORD_PTR>(fg::encodeReply(deliveries.back())); }
        else result = emptyReply;
        if (afterDeliver) afterDeliver();
        if (failAfterDelivery) { lastError=sendError; return false; }
        return true;
    }
    void inert() const {
        const std::vector<std::string> allowed{"now","threadId","processId","windowThread",
            "foregroundWindow","error","setError","allow","send"};
        for (const auto& operation : trace)
            check(std::find(allowed.begin(),allowed.end(),operation) != allowed.end(),"unexpected side effect");
        for (const auto& attempt : allows) {
            check(attempt.pid != 0 && attempt.pid != ASFW_ANY,"broad/invalid grant");
            check(attempt.foreground == hwnd(1) || attempt.foreground == hwnd(2) || attempt.foreground == hwnd(4),"grant while foreign foreground");
        }
        check(!sendCalls || (destination == hwnd(1) && waitSeen == 250 && tokenSeen != 0),"message target/token/budget");
        // There is deliberately no focus/input mutator in Ops. Source and import audits
        // independently reject real focus, input, process and COM calls before execution.
    }
};
void outcome(bool accepted, const FakeOps& ops, bool expected, DWORD error, std::size_t allows, unsigned sends) {
    check(accepted == expected,"request acceptance");
    check(ops.lastError == error,"error was lost or rewritten");
    check(ops.allows.size() == allows,"allow count/retry");
    check(ops.sendCalls == sends,"message count/retry");
    ops.inert();
}
json record(const char* scenario, bool accepted, const FakeOps& ops, DWORD caller) {
    return {{"scenario",scenario},{"simulated",true},{"event","focus-task-view-api-result"},
        {"status",accepted ? "requested" : "foreground-grant-failed"}, {"threadId",caller},
        {"helperPid",readyChild},{"helperStarted",true},{"helperResumed",true},{"helperReady",true},
        {"foregroundGrantAttempted",true},{"foregroundGrantAccepted",accepted},
        {"foregroundGrantError",ops.lastError},{"errorHint",ops.lastError},
        {"executionReleased",false},{"apiRequestAccepted",false},{"taskViewVisibleProven",false},
        {"foregroundBeforeDispatch",{{"callingThreadId",caller},
            {"callerWindow",{{"processId",ownPid},{"threadId",uiThread}}},
            {"foregroundBefore",{{"processId",ownPid},{"threadId",uiThread}}}}},
        {"allowThreadId",ops.allows.empty() ? 0 : ops.allows.back().thread},
        {"executionReleaseScope","broker fixture stops at grant; helper/COM never executed"}};
}
template<class F> void run(const char* name, F test) {
    const unsigned before = checks;
    try { test(); cases.push_back({{"case",name},{"pass",true},{"checks",checks-before}}); }
    catch (const std::exception& error) { cases.push_back({{"case",name},{"pass",false},{"checks",checks-before},{"error",error.what()}}); }
}
}
int main() {
    using namespace fixture;
    run("constants-opaque-scalar-token-and-250ms-bound",[] {
        check(fg::message == WM_APP + 825,"message identifier");
        check(fg::maximumWaitMs == 250,"250ms maximum");
    });
    run("assumed-permission-model-pool-denied-owner-ui-accepted",[] {
        fg::Broker b; FakeOps ops(b);
        const bool baseline = ops.allow(readyChild);
        check(!baseline && ops.lastError == 5 && ops.allows.size() == 1,"pool baseline must fail error 5");
        records.push_back(record("assumed-direct-pool-denial",baseline,ops,poolThread));
        ops.current = uiThread;
        const bool directUi = ops.allow(readyChild); ops.setError(directUi ? 0 : ops.error());
        check(directUi && ops.allows.size() == 2 && ops.foreground == hwnd(1),"same owner UI baseline must succeed");
        records.push_back(record("assumed-owner-ui-control",directUi,ops,uiThread));
        ops.current = poolThread; ops.allows.clear(); ops.trace.clear();
        const bool bridged = b.request(ops,hwnd(1),readyChild);
        outcome(bridged,ops,true,0,1,1);
        check(ops.allows.front().thread == uiThread && ops.allows.front().pid == readyChild,"UI grant target");
        check(ops.current == poolThread && ops.foreground == hwnd(1),"caller/foreground changed");
        records.push_back(record("new-worker-to-owner-ui-bridge",bridged,ops,poolThread));
    });
    // These are contract conditions, not a model of Windows deciding permission
    // from virtual ON/OFF. Both API outcomes must survive the actual broker.
    for (const bool allowed : {false,true}) run(allowed ? "receipt-real-api-acceptance" : "receipt-real-api-denial",[allowed] {
        fg::Broker b; FakeOps o(b); o.apiAccepted = allowed;
        fg::DeliveryReceipt receipt;
        outcome(b.request(o,hwnd(1),readyChild,&receipt),o,allowed,allowed ? 0 : 5,1,1);
        check(receipt.apiAttempted() && receipt.apiAccepted() == allowed,"actual API receipt lost");
        check(receipt.stage == (allowed ? fg::DeliveryStage::ApiAccepted : fg::DeliveryStage::ApiRejected),"actual API stage lost");
        check(receipt.error == o.lastError,"API error not bound to scalar receipt");
    });
    run("receipt-foreign-foreground-is-not-a-windows-api-denial",[] {
        fg::Broker b; FakeOps o(b); o.foreground=hwnd(3); fg::DeliveryReceipt receipt;
        outcome(b.request(o,hwnd(1),readyChild,&receipt),o,false,5,0,1);
        check(receipt.stage==fg::DeliveryStage::ForeignForeground && !receipt.apiAttempted(),"guard masquerades as API failure");
    });
    run("receipt-wrong-ui-thread-is-not-a-windows-api-denial",[] {
        fg::Broker b; FakeOps o(b); o.deliveryThread=poolThread; fg::DeliveryReceipt receipt;
        outcome(b.request(o,hwnd(1),readyChild,&receipt),o,false,5,0,1);
        check(receipt.stage==fg::DeliveryStage::WrongOwnerThread && !receipt.apiAttempted(),"thread guard masquerades as API failure");
    });
    run("receipt-ui-timeout-is-not-a-windows-api-denial",[] {
        fg::Broker b; FakeOps o(b); o.sendAccepted=false; fg::DeliveryReceipt receipt;
        outcome(b.request(o,hwnd(1),readyChild,&receipt),o,false,ERROR_TIMEOUT,0,1);
        check(receipt.stage==fg::DeliveryStage::TransportFailed && !receipt.deliveryKnown(),"transport masquerades as known non-delivery");
    });
    for (const DWORD_PTR reply : {DWORD_PTR(0),DWORD_PTR(5),
            DWORD_PTR(fg::encodeReply({fg::DeliveryStage::ApiAccepted,5})),
            DWORD_PTR(fg::encodeReply({fg::DeliveryStage::ApiRejected,0})),
            DWORD_PTR(UINT64_MAX)}) run(("malformed-scalar-reply-"+std::to_string(reply)).c_str(),[reply] {
        fg::Broker b; FakeOps o(b); o.deliverMessage=false; o.emptyReply=reply; fg::DeliveryReceipt receipt;
        outcome(b.request(o,hwnd(1),readyChild,&receipt),o,false,ERROR_INVALID_DATA,0,1);
        check(receipt.stage==fg::DeliveryStage::InvalidReply && !receipt.apiAttempted(),"malformed reply authorized worker");
    });
    run("worker-marshals-one-ready-child-to-ui",[] {
        fg::Broker b; FakeOps o(b); outcome(b.request(o,hwnd(1),readyChild),o,true,0,1,1);
        check(o.allows[0].thread == uiThread && o.allows[0].pid == readyChild,"wrong thread/child");
        check(o.current == poolThread && o.foreground == hwnd(1),"focus or caller changed");
    });
    run("direct-ui-never-sends",[] {
        fg::Broker b; FakeOps o(b); o.current = uiThread;
        outcome(b.request(o,hwnd(1),readyChild),o,true,0,1,0);
        check(o.allows[0].thread == uiThread && o.foreground == hwnd(1),"direct UI grant");
    });
    run("delivery-on-wrong-thread-is-denied",[] {
        fg::Broker b; FakeOps o(b); o.deliveryThread = poolThread;
        outcome(b.request(o,hwnd(1),readyChild),o,false,5,0,1);
    });
    for (const bool lost : {false,true}) run(lost ? "lost-foreground-denied" : "foreign-foreground-denied",[lost] {
        fg::Broker b; FakeOps o(b); o.foreground = lost ? nullptr : hwnd(3);
        outcome(b.request(o,hwnd(1),readyChild),o,false,5,0,1);
    });
    for (const bool lost : {false,true}) run(lost ? "foreground-lost-during-send" : "foreground-changed-during-send",[lost] {
        fg::Broker b; FakeOps o(b); o.beforeDeliver = [&] { o.foreground = lost ? nullptr : hwnd(3); };
        outcome(b.request(o,hwnd(1),readyChild),o,false,5,0,1);
    });
    run("same-process-other-foreground-window-accepted",[] {
        fg::Broker b; FakeOps o(b); o.foreground = hwnd(4);
        outcome(b.request(o,hwnd(1),readyChild),o,true,0,1,1);
        check(o.foreground == hwnd(4),"broker changed foreground");
    });
    run("api-denial-error-5-preserved-with-no-retry",[] {
        fg::Broker b; FakeOps o(b); o.apiAccepted = false; o.apiError = 5;
        outcome(b.request(o,hwnd(1),readyChild),o,false,5,1,1);
        check(o.uiDeliver(hwnd(1),o.tokenSeen) == ERROR_CANCELLED,"denied token reused");
    });
    run("direct-ui-denial-is-not-converted-to-success",[] {
        fg::Broker b; FakeOps o(b); o.current = uiThread; o.apiAccepted = false;
        outcome(b.request(o,hwnd(1),readyChild),o,false,5,1,0);
    });
    run("api-zero-error-falls-back-to-denied",[] {
        fg::Broker b; FakeOps o(b); o.apiAccepted = false; o.apiError = 0; fg::DeliveryReceipt receipt;
        outcome(b.request(o,hwnd(1),readyChild,&receipt),o,false,5,1,1);
        check(receipt.stage==fg::DeliveryStage::ApiRejectedNoError && receipt.extendedError()==0,"zero API error was invented");
    });
    run("api-failure-with-no-error-cannot-inherit-stale-ui-error",[] {
        fg::Broker b; FakeOps o(b); o.apiAccepted=false; o.apiAssignsError=false;
        o.beforeDeliver=[&] { o.lastError=999; }; fg::DeliveryReceipt receipt;
        outcome(b.request(o,hwnd(1),readyChild,&receipt),o,false,5,1,1);
        check(receipt.stage==fg::DeliveryStage::ApiRejectedNoError && receipt.extendedError()==0,"stale UI last-error fabricated Windows error");
    });
    run("api-other-error-is-preserved",[] {
        fg::Broker b; FakeOps o(b); o.apiAccepted = false; o.apiError = 1234;
        outcome(b.request(o,hwnd(1),readyChild),o,false,1234,1,1);
    });
    run("hung-ui-250ms-timeout-cancels-late-token",[] {
        fg::Broker b; FakeOps o(b); o.sendAccepted = false; o.advance = 250;
        outcome(b.request(o,hwnd(1),readyChild),o,false,ERROR_TIMEOUT,0,1);
        check(o.clock == 1250,"hung UI bound");
        check(o.uiDeliver(hwnd(1),o.tokenSeen) == ERROR_CANCELLED && o.allows.empty(),"late timeout token authorized");
    });
    run("ui-grant-ran-but-sender-timed-out-delivery-remains-unknown",[] {
        fg::Broker b; FakeOps o(b); o.failAfterDelivery=true; fg::DeliveryReceipt receipt;
        outcome(b.request(o,hwnd(1),readyChild,&receipt),o,false,ERROR_TIMEOUT,1,1);
        check(!receipt.deliveryKnown() && o.deliveries.back().apiAccepted(),"timeout fabricated a known UI outcome");
        check(o.uiDeliver(hwnd(1),o.tokenSeen)==ERROR_CANCELLED,"late completed token retried");
        o.failAfterDelivery=false;outcome(b.request(o,hwnd(1),readyChild+1,&receipt),o,true,0,2,2);
        check(receipt.apiAccepted() && o.allows.back().pid==readyChild+1,"next request receipt corrupted");
    });
    run("send-failure-zero-error-is-generic-failure-delivery-unknown",[] {
        fg::Broker b; FakeOps o(b); o.sendAccepted = false; o.sendError = 0; fg::DeliveryReceipt receipt;
        outcome(b.request(o,hwnd(1),readyChild,&receipt),o,false,ERROR_GEN_FAILURE,0,1);
        check(!receipt.deliveryKnown() && receipt.extendedErrorKnown() && receipt.extendedError()==0,"zero send error was invented");
    });
    run("send-access-denied-preserved-and-token-cancelled",[] {
        fg::Broker b; FakeOps o(b); o.sendAccepted = false; o.sendError = 5;
        outcome(b.request(o,hwnd(1),readyChild),o,false,5,0,1);
        check(o.uiDeliver(hwnd(1),o.tokenSeen) == ERROR_CANCELLED,"failed send left live token");
    });
    run("send-with-no-delivery-is-cancelled",[] {
        fg::Broker b; FakeOps o(b); o.deliverMessage = false;
        outcome(b.request(o,hwnd(1),readyChild),o,false,ERROR_CANCELLED,0,1);
        check(o.uiDeliver(hwnd(1),o.tokenSeen) == ERROR_CANCELLED,"undelivered token survived return");
    });
    for (DWORD delay : {249u,250u,251u}) run(delay == 249 ? "delivery-at-249ms" : delay == 250 ? "delivery-at-250ms-expired" : "delivery-after-deadline",[delay] {
        fg::Broker b; FakeOps o(b); o.advance = delay;
        outcome(b.request(o,hwnd(1),readyChild),o,delay < 250,delay < 250 ? 0 : ERROR_TIMEOUT,delay < 250 ? 1 : 0,1);
        check(o.uiDeliver(hwnd(1),o.tokenSeen) == ERROR_CANCELLED,"expired/completed token survived");
    });
    run("zero-forged-and-wrong-window-tokens-rejected-before-valid-delivery",[] {
        fg::Broker b; FakeOps o(b); std::vector<DWORD> replies; std::size_t before = 999;
        o.beforeDeliver = [&] {
            replies.push_back(o.uiDeliver(hwnd(1),0));
            replies.push_back(o.uiDeliver(hwnd(1),o.tokenSeen + 100));
            replies.push_back(o.uiDeliver(hwnd(2),o.tokenSeen));
            before = o.allows.size();
        };
        outcome(b.request(o,hwnd(1),readyChild),o,true,0,1,1);
        check(before == 0 && replies == std::vector<DWORD>(3,ERROR_CANCELLED),"forged delivery changed pending request");
    });
    run("duplicate-token-during-and-after-send-rejected",[] {
        fg::Broker b; FakeOps o(b); DWORD duplicate = 0;
        o.afterDeliver = [&] { duplicate = o.uiDeliver(hwnd(1),o.tokenSeen); };
        outcome(b.request(o,hwnd(1),readyChild),o,true,0,1,1);
        check(duplicate == ERROR_CANCELLED && o.uiDeliver(hwnd(1),o.tokenSeen) == ERROR_CANCELLED,"duplicate authorized twice");
        check(o.allows.size() == 1,"more than one API attempt");
    });
    run("duplicate-denied-api-token-never-retries",[] {
        fg::Broker b; FakeOps o(b); o.apiAccepted = false; DWORD duplicate = 0;
        o.afterDeliver = [&] { duplicate = o.uiDeliver(hwnd(1),o.tokenSeen); };
        outcome(b.request(o,hwnd(1),readyChild),o,false,5,1,1);
        check(duplicate == ERROR_CANCELLED,"denied API token retried");
    });
    for (const bool timeout : {false,true}) run(timeout ? "timed-out-old-token-cannot-grant-next-child" : "completed-old-token-cannot-grant-next-child",[timeout] {
        fg::Broker b; FakeOps o(b); o.sendAccepted = !timeout;
        outcome(b.request(o,hwnd(1),readyChild),o,!timeout,timeout ? ERROR_TIMEOUT : 0,timeout ? 0 : 1,1);
        const auto old = o.tokenSeen; const auto firstCount = o.allows.size();
        o.sendAccepted = true; DWORD oldReply = 0; std::size_t afterOld = 999;
        o.beforeDeliver = [&] { oldReply = o.uiDeliver(hwnd(1),old); afterOld = o.allows.size(); };
        outcome(b.request(o,hwnd(1),readyChild + 1),o,true,0,firstCount + 1,2);
        check(old != o.tokenSeen && oldReply == ERROR_CANCELLED && afterOld == firstCount,"old token granted next request");
        check(o.allows.back().pid == readyChild + 1,"next child mismatch");
    });
    run("reentrant-request-during-send-is-busy-and-lease-recovers",[] {
        fg::Broker b; FakeOps o(b); bool inner = true; DWORD error = 0;
        o.beforeDeliver = [&] { inner = b.request(o,hwnd(1),readyChild + 1); error = o.lastError; };
        outcome(b.request(o,hwnd(1),readyChild),o,true,0,1,1);
        check(!inner && error == ERROR_BUSY && o.allows[0].pid == readyChild,"reentry replaced active request");
        o.beforeDeliver = {}; outcome(b.request(o,hwnd(1),readyChild + 1),o,true,0,2,2);
    });
    run("reentrant-request-and-delivery-inside-allow",[] {
        fg::Broker b; FakeOps o(b); bool inner = true; DWORD error = 0, duplicate = 0;
        o.onAllow = [&] {
            inner = b.request(o,hwnd(1),readyChild + 1); error = o.lastError;
            duplicate = b.deliver(o,hwnd(1),o.tokenSeen);
        };
        outcome(b.request(o,hwnd(1),readyChild),o,true,0,1,1);
        check(!inner && error == ERROR_BUSY && duplicate == ERROR_CANCELLED,"allow reentry authorized");
    });
    run("direct-ui-reentrant-request-is-busy",[] {
        fg::Broker b; FakeOps o(b); o.current = uiThread; bool inner = true; DWORD error = 0;
        o.onAllow = [&] { inner = b.request(o,hwnd(1),readyChild + 1); error = o.lastError; };
        outcome(b.request(o,hwnd(1),readyChild),o,true,0,1,0);
        check(!inner && error == ERROR_BUSY,"UI reentry not busy");
    });
    for (const DWORD pid : {DWORD(0),DWORD(ASFW_ANY)}) run(pid == 0 ? "zero-child-pid-rejected" : "ASFW_ANY-rejected",[pid] {
        fg::Broker b; FakeOps o(b); outcome(b.request(o,hwnd(1),pid),o,false,ERROR_INVALID_PARAMETER,0,0);
        check(o.trace == std::vector<std::string>{"setError"},"invalid PID touched owner/UI/API");
        outcome(b.request(o,hwnd(1),readyChild),o,true,0,1,1);
    });
    run("null-owner-rejected",[] {
        fg::Broker b; FakeOps o(b); outcome(b.request(o,nullptr,readyChild),o,false,ERROR_INVALID_PARAMETER,0,0);
    });
    run("foreign-owner-window-rejected-before-send",[] {
        fg::Broker b; FakeOps o(b); outcome(b.request(o,hwnd(3),readyChild),o,false,5,0,0);
    });
    run("invalid-owner-window-rejected-before-send",[] {
        fg::Broker b; FakeOps o(b); outcome(b.request(o,hwnd(99),readyChild),o,false,5,0,0);
    });
    run("owner-with-zero-thread-rejected",[] {
        fg::Broker b; FakeOps o(b); o.windows[1].thread = 0;
        outcome(b.request(o,hwnd(1),readyChild),o,false,5,0,0);
    });
    for (const bool destroyed : {false,true}) run(destroyed ? "owner-destroyed-before-ui-delivery" : "owner-pid-changed-before-ui-delivery",[destroyed] {
        fg::Broker b; FakeOps o(b);
        o.beforeDeliver = [&] { if (destroyed) o.windows.erase(1); else o.windows[1].pid = 9999; };
        outcome(b.request(o,hwnd(1),readyChild),o,false,5,0,1);
    });
    run("owner-thread-changed-before-ui-delivery",[] {
        fg::Broker b; FakeOps o(b); o.beforeDeliver = [&] { o.windows[1].thread = 8888; };
        outcome(b.request(o,hwnd(1),readyChild),o,false,5,0,1);
    });
    run("no-pending-request-rejects-all-delivery-tokens",[] {
        fg::Broker b; FakeOps o(b);
        for (WPARAM token : {WPARAM(0),WPARAM(1),WPARAM(123456)})
            check(o.uiDeliver(hwnd(1),token) == ERROR_CANCELLED,"idle forged token accepted");
        check(o.allows.empty() && o.sendCalls == 0,"idle delivery side effect");
    });
    run("timeout-then-success-uses-fresh-token-without-background-retry",[] {
        fg::Broker b; FakeOps o(b); o.sendAccepted = false; o.advance = 250;
        outcome(b.request(o,hwnd(1),readyChild),o,false,ERROR_TIMEOUT,0,1);
        o.sendAccepted = true; o.advance = 0;
        outcome(b.request(o,hwnd(1),readyChild + 1),o,true,0,1,2);
        check(o.tokens.size() == 2 && o.tokens[0] != o.tokens[1] && o.allows[0].pid == readyChild + 1,"retry reused token/child");
    });
    bool pass = true; for (const auto& item : cases) if (!item["pass"].get<bool>()) pass = false;
    std::cout << json({{"pass",pass},{"count",cases.size()},{"checks",checks},{"cases",cases},{"records",records},
        {"allOpsInMemory",true},{"actualOSInput",false},{"focusChangedByBroker",false},
        {"productExecuted",false},{"actualHelperExecuted",false},{"comInvoked",false},{"windowsOpsInvoked",false},
        {"threadModel","logical pool/UI IDs; send invokes actual Broker::deliver synchronously"},
        {"scope","actual foreground header under FakeOps; OS scheduling, COM and visible TaskView are not proven"}}).dump(2) << '\n';
    return pass ? 0 : 1;
}
