#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"

// Every snapshot API is replaced before either production header is included.
// Only memory fixtures and the two extracted IPC lambdas run in this executable.
// nativeSendTaskView is a command-contract fake, independent of its real implementation.
// Neither a real input producer nor the system API helper/supervisor is exercised.
namespace fixture {
using Json = nlohmann::json;
struct Windows {
    DWORD error = 777, queueFailure = 5;
    unsigned apiCalls = 0, snapshotCalls = 0, asyncCalls = 0, queueCalls = 0, foregroundCalls = 0;
    ULONGLONG tick = 1000;
    bool queueOk = true, foregroundChanges = false;
    std::array<SHORT, 256> async{};
    std::array<BYTE, 256> queue{};
};
static thread_local Windows win;
static SHORT asyncBits(std::uint16_t bits) { return std::bit_cast<SHORT>(bits); }
static DWORD fakeGetLastError() { ++win.apiCalls; return win.error; }
static void fakeSetLastError(DWORD value) { ++win.apiCalls; win.error = value; }
static ULONGLONG fakeTick() { ++win.apiCalls; ++win.snapshotCalls; win.error = 101; return ++win.tick; }
static DWORD fakeThread() { ++win.apiCalls; ++win.snapshotCalls; win.error = 102; return 42; }
static HWND fakeForeground() {
    ++win.apiCalls; ++win.snapshotCalls; ++win.foregroundCalls; win.error = 103;
    return reinterpret_cast<HWND>(std::uintptr_t(win.foregroundChanges && win.foregroundCalls % 2 == 0 ? 2 : 1));
}
static DWORD fakePid(HWND window, DWORD* pid) {
    ++win.apiCalls; ++win.snapshotCalls; win.error = 104;
    *pid = window == reinterpret_cast<HWND>(std::uintptr_t(1)) ? 123u : 456u;
    return 55;
}
static SHORT fakeAsync(int key) { ++win.apiCalls; ++win.snapshotCalls; ++win.asyncCalls; win.error = 105; return win.async.at(key); }
static BOOL fakeQueue(PBYTE output) {
    ++win.apiCalls; ++win.snapshotCalls; ++win.queueCalls;
    if (!win.queueOk) { win.error = win.queueFailure; return FALSE; }
    std::copy(win.queue.begin(), win.queue.end(), output); win.error = 106; return TRUE;
}
}
#define GetLastError fixture::fakeGetLastError
#define SetLastError fixture::fakeSetLastError
#define GetTickCount64 fixture::fakeTick
#define GetCurrentThreadId fixture::fakeThread
#define GetForegroundWindow fixture::fakeForeground
#define GetWindowThreadProcessId fixture::fakePid
#define GetAsyncKeyState fixture::fakeAsync
#define GetKeyboardState fixture::fakeQueue
#include "../native/task_view_action_evidence.h"

using json = nlohmann::json;
namespace ev = ymcc::taskViewEvidence;
namespace fixture {
static std::atomic<unsigned> checks{0};
static Json cases = Json::array();
static void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
static Json caller(const std::string& token = "switch:fixture-1") {
    return {{"diagnosticAction", "game-recognition-switch-program"}, {"diagnosticActionId", token}};
}
static void resetWindows() { win = Windows{}; }
struct Rows {
    std::vector<Json> rows;
    void operator()(Json row) { rows.push_back(std::move(row)); win.error = 991; }
};
struct Rendezvous {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, released = false, secondFinished = false;
    bool timedOut = false;
    void enterAndWait() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true; cv.notify_all();
        if (!cv.wait_for(lock, std::chrono::seconds(5), [&] { return released; })) timedOut = true;
    }
    bool waitEntered() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [&] { return entered; });
    }
    void finishSecond() {
        std::lock_guard<std::mutex> lock(mutex); secondFinished = true; cv.notify_all();
    }
    bool waitSecond() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [&] { return secondFinished; });
    }
    void release() { std::lock_guard<std::mutex> lock(mutex); released = true; cv.notify_all(); }
};
struct OriginalError : std::runtime_error {
    const void* identity;
    explicit OriginalError(const void* id) : std::runtime_error("TaskView command fixture original failure: contract-sentinel"), identity(id) {}
};
struct NonStandardError { const void* identity; };
struct Action {
    bool gate = true, gateAfterAction = true;
    bool changeGate = false, sinkThrows = false, postAccepted = true;
    int throwKind = 0;
    unsigned taskCalls = 0, postCalls = 0, sinkCalls = 0, gateReads = 0;
    ULONGLONG taskDeadline = ~ULONGLONG(0);
    std::uint64_t taskSequence=0;
    bool postArgumentsOk = true, sinkDomainOk = true;
    const void* identity = nullptr;
    Rendezvous* blockInProducer = nullptr;
};
static thread_local Action action;
static std::mutex logMutex;
static std::vector<Json> logRows;
static void resetAction() { resetWindows(); action = Action{}; std::lock_guard<std::mutex> lock(logMutex); logRows.clear(); }
static std::vector<Json> rowsCopy() { std::lock_guard<std::mutex> lock(logMutex); return logRows; }
static void modifyAtAction() {
    win.async.fill(0); win.queue.fill(0);
    win.async[VK_RMENU] = asyncBits(0x8001); win.queue[VK_LSHIFT] = 0x80;
    win.error = 4321;
    if (action.changeGate) action.gate = action.gateAfterAction;
}
}
static bool sensorDetailLoggingGate() { ++fixture::action.gateReads; return fixture::action.gate; }
static void appendNativeLifecycleLog(const char* domain, json row) {
    using namespace fixture;
    ++action.sinkCalls;
    action.sinkDomainOk = action.sinkDomainOk && std::string(domain) == "focus-task-view-action";
    win.error = 9090;
    if (action.sinkThrows) throw std::runtime_error("fixture log sink failed");
    std::lock_guard<std::mutex> lock(logMutex); logRows.push_back(std::move(row));
}
static void nativeSendTaskView(ULONGLONG deadline=0,std::uint64_t nativeRequestSequence=0) {
    using namespace fixture;
    ++action.taskCalls; action.taskDeadline = deadline; action.taskSequence=nativeRequestSequence;
    if (action.blockInProducer) action.blockInProducer->enterAndWait();
    modifyAtAction();
    if (action.throwKind == 1) throw OriginalError(action.identity);
    if (action.throwKind == 2) throw NonStandardError{action.identity};
}
static HWND g_hwnd = reinterpret_cast<HWND>(std::uintptr_t(0x1234));
static BOOL fakePostMessageW(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    using namespace fixture;
    ++action.postCalls;
    action.postArgumentsOk = action.postArgumentsOk && window == g_hwnd && message == WM_SYSCOMMAND && wparam == SC_MINIMIZE && lparam == 0;
    modifyAtAction();
    return action.postAccepted ? TRUE : FALSE;
}
#define PostMessageW fakePostMessageW
static std::map<std::string, std::function<json(const json&)>> handlers;
static void ipc_on(const char* command, std::function<json(const json&)> handler) {
    if (!handlers.emplace(command, std::move(handler)).second) throw std::runtime_error("duplicate extracted IPC handler");
}
#include "task_view_action_evidence_main.inc"

namespace fixture {
static void paired(const std::vector<Json>& rows, const std::string& command, const std::string& token) {
    check(rows.size() == 2, "expected exactly before/after rows");
    check(rows[0]["phase"] == "before-call" && rows[1]["phase"] == "after-call", "before/after order");
    check(rows[0]["nativeRequestSequence"] == rows[1]["nativeRequestSequence"] && rows[0]["nativeRequestSequence"].get<std::uint64_t>() > 0, "native sequence shared and nonzero");
    for (const auto& row : rows) {
        check(row["command"] == command && row["caller"]["token"] == token, "command/token correlation");
        check(row["diagnosticFailureScope"] == "caught-by-this-recorder-only" && row["logSinkReturnIsDurableReceipt"] == false, "failure counter scope and durable receipt limit");
        check(row["caller"]["humanGestureProven"] == false && row["errorHintIsDefinitive"] == false, "caller and error uncertainty");
        check(row["taskViewVisibleProven"] == false && row["minimizeCompletedProven"] == false && row["downstreamKeyboardReceiptProven"] == false, "no UI or keyboard delivery claim");
    }
}
static void gateOff() {
    ev::Recorder recorder; Rows sink; const auto args = caller(); resetWindows();
    const auto context = recorder.begin(false, args, "shell.taskView", sink);
    unsigned issued = 0; ++issued;
    recorder.end(false, args, context, "call-returned", 44, false, false, sink);
    recorder.end(true, args, context, "call-returned", 44, false, false, sink);
    check(!context.active && sink.rows.empty() && win.apiCalls == 0, "disabled gate/inactive context performed diagnostics");
    check(issued == 1 && win.error == 777 && recorder.failures() == 0, "disabled diagnostics changed original operation/error");
    const auto next = recorder.begin(true, args, "shell.taskView", sink);
    check(next.sequence == 1, "disabled command consumed native sequence");
}
static void snapshotsAndSequence() {
    ev::Recorder recorder; Rows sink; resetWindows(); const auto args = caller();
    win.async[VK_LCONTROL] = asyncBits(0x8001); win.async[VK_RCONTROL] = 1;
    win.queue[VK_RCONTROL] = 0x80;
    const auto context = recorder.begin(true, args, "shell.taskView", sink);
    check(win.error == 777 && win.asyncCalls == 8 && win.queueCalls == 1 && win.foregroundCalls == 2, "bounded before snapshot and saved last error");
    win.async.fill(0); win.queue.fill(0); win.async[VK_RMENU] = asyncBits(0x8000); win.queue[VK_LSHIFT] = 0x80; win.error = 444;
    recorder.end(true, args, context, "call-returned", win.error, false, false, sink);
    paired(sink.rows, "shell.taskView", "switch:fixture-1");
    const auto& before = sink.rows[0]["keyboardModifiers"]; const auto& after = sink.rows[1]["keyboardModifiers"];
    check(before["asyncDownMask"] == 1 && before["callingThreadQueueDownMask"] == 2, "before side/queue distinction and async low-bit ignored");
    check(after["asyncDownMask"] == 32 && after["callingThreadQueueDownMask"] == 4, "after modifiers were not sampled again");
    check(after["modifiers"]["rightAlt"]["asyncDownBit"] == true && after["modifiers"]["leftCtrl"]["asyncDownBit"] == false, "pre/post side changes");
    check(after["crossKeyAtomicSnapshot"] == false && after["eventEdgesCaptured"] == false && after["asyncZeroIsAbsenceProof"] == false, "snapshot limits retained");
    check(win.error == 444 && sink.rows[1]["win32ErrorHint"] == 444 && sink.rows[1]["postAccepted"].is_null(), "after error preserved and unknown post not false");
    check(win.asyncCalls == 16 && win.queueCalls == 2 && win.foregroundCalls == 4, "exactly two bounded snapshots");
    const auto second = recorder.begin(true, caller("next"), "window.minimize", sink);
    check(second.sequence == context.sequence + 1, "next command sequence does not advance monotonically");
}
static void snapshotFailure() {
    ev::Recorder recorder; Rows sink; resetWindows(); win.queueOk = false; win.queueFailure = 998; win.foregroundChanges = true;
    const auto context = recorder.begin(true, caller(), "shell.taskView", sink);
    check(context.active && sink.rows.size() == 1 && win.error == 777, "failed queue snapshot disturbed command/error");
    const auto& snapshot = sink.rows[0]["keyboardModifiers"];
    check(snapshot["callingThreadQueueReadOk"] == false && snapshot["callingThreadQueueError"] == 998, "queue error evidence");
    check(snapshot["callingThreadQueueDownMask"].is_null() && snapshot["modifiers"]["leftCtrl"]["callingThreadQueueDown"].is_null(), "unknown queue converted to zero");
    check(snapshot["foregroundStableDuringCapture"] == false && snapshot["foregroundPidBefore"] == 123 && snapshot["foregroundPidAfter"] == 456, "foreground race was hidden");
    check(recorder.failures() == 0, "ordinary snapshot API failure incorrectly counted as sink failure");
}
static Json observedCaller(const Json& args) {
    ev::Recorder recorder; Rows sink; resetWindows();
    recorder.begin(true, args, "shell.taskView", sink);
    check(sink.rows.size() == 1, "arguments lost the row or threw");
    return sink.rows.front()["caller"];
}
static void tokenValidation() {
    for (unsigned value = 0; value <= 255; ++value) {
        const std::string token(1, static_cast<char>(value));
        const bool allowed = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == ':' || value == '-' || value == '_' || value == '.';
        const auto result = observedCaller(caller(token));
        check(result["tokenStatus"] == (allowed ? "retained" : "token-characters-invalid"), "ASCII whitelist mismatch at byte " + std::to_string(value));
        check(allowed ? result["token"] == token : result["token"].is_null(), "invalid byte retained in caller token");
    }
    for (const std::size_t length : {0u, 1u, 79u, 80u, 81u, 4096u}) {
        const auto result = observedCaller(caller(std::string(length, 'A')));
        const bool valid = length > 0 && length <= 80;
        check(result["tokenStatus"] == (valid ? "retained" : "token-length-invalid"), "token length boundary");
        check(valid ? result["token"].get<std::string>().size() == length : result["token"].is_null(), "token length leaked or truncated");
    }
    const std::vector<Json> nonobjects{nullptr, false, true, 12, 1.5, "PRIVATE nonobject text", Json::array({1, "PRIVATE array text"})};
    for (const auto& args : nonobjects) {
        const auto result = observedCaller(args);
        check(result["tokenStatus"] == "arguments-not-object" && result["token"].is_null(), "nonobject argument safety");
        check(result.dump().find("PRIVATE") == std::string::npos, "nonobject text retained");
    }
    for (const Json entry : {Json(nullptr), Json(3), Json(true), Json::array(), Json::object(), Json("PRIVATE unknown entry")}) {
        auto args = caller("PRIVATE-token"); args["diagnosticAction"] = entry;
        const auto result = observedCaller(args);
        check(result["entry"] == "unobserved" && result["tokenStatus"] == "entry-unrecognized" && result["token"].is_null(), "unknown entry allowed token");
        check(result.dump().find("PRIVATE") == std::string::npos, "unknown caller text retained");
    }
    auto absent = caller(); absent.erase("diagnosticActionId");
    check(observedCaller(absent)["tokenStatus"] == "absent", "missing token status");
    for (const Json token : {Json(nullptr), Json(3), Json(false), Json::array({"PRIVATE"}), Json{{"PRIVATE", "payload"}}}) {
        auto args = caller(); args["diagnosticActionId"] = token;
        const auto result = observedCaller(args);
        check(result["tokenStatus"] == "token-not-string" && result["token"].is_null(), "nonstring token safety");
        check(result.dump().find("PRIVATE") == std::string::npos, "nonstring token payload retained");
    }
    ev::Recorder recorder; Rows sink;
    auto privateArgs = caller("PRIVATE payload with spaces"); privateArgs["arbitrary"] = "PRIVATE extra field";
    recorder.begin(true, privateArgs, "shell.taskView", sink);
    check(sink.rows.front().dump().find("PRIVATE") == std::string::npos, "arbitrary args or invalid caller token reached diagnostic row");
    check(observedCaller(caller("AZaz09:._-"))["token"] == "AZaz09:._-", "valid punctuation rejected");
}
static void activationValidation() {
    for(const char* value:{"gamepad-engine","trusted-touch","trusted-pen","trusted-mouse","trusted-other","scripted","unknown"}) {
        auto args=caller();args["diagnosticActivation"]=value;args["diagnosticEventTrusted"]=true;args["diagnosticClickDetail"]=1;
        const auto result=observedCaller(args);
        check(result["activation"]==value&&result["eventTrusted"]==true&&result["clickDetail"]==1,"activation observation lost");
        check(result["humanGestureProven"]==false,"activation observation promoted to authority");
    }
    for(const Json value:{Json(nullptr),Json(false),Json(12),Json::array({"PRIVATE"}),Json{{"PRIVATE","value"}},Json("PRIVATE unbounded origin")}) {
        auto args=caller();args["diagnosticActivation"]=value;args["diagnosticEventTrusted"]=value;args["diagnosticClickDetail"]=value;
        const auto result=observedCaller(args);
        check(result["activation"]=="unknown"&&result["clickDetail"].is_null(),"unrecognized activation/detail retained");
        check(result.dump().find("PRIVATE")==std::string::npos,"private activation payload retained");
    }
    for(const Json value:{Json(-1),Json(4),Json(1.5),Json(std::numeric_limits<Json::number_unsigned_t>::max()),Json(std::numeric_limits<Json::number_integer_t>::min())}) {
        auto args=caller();args["diagnosticClickDetail"]=value;
        check(observedCaller(args)["clickDetail"].is_null(),"out-of-range click detail accepted");
    }
    for(const Json value:{Json(0),Json(3),Json(Json::number_unsigned_t(3))}) {
        auto args=caller();args["diagnosticClickDetail"]=value;
        check(observedCaller(args)["clickDetail"]==value,"boundary click detail lost");
    }
    auto args=caller();args["diagnosticEventTrusted"]="true";
    check(observedCaller(args)["eventTrusted"].is_null(),"string trust coerced to human authority");
}
static void requiredCaller() {
    ev::Recorder recorder; Rows sink;
    resetWindows(); const auto required = recorder.begin(false, caller(), "window.minimize", sink, true);
    check(!required.active && win.apiCalls == 0 && sink.rows.empty(), "disabled known minimize performed diagnostics");
    std::vector<Json> unrelated{nullptr, false, 1, "private", Json::array(), Json::object(), {{"diagnosticAction", "other"}}, {{"diagnosticAction", 7}}};
    for (const auto& entry : {std::string(" game-recognition-switch-program"), std::string("game-recognition-switch-program "), std::string("Game-recognition-switch-program"), std::string("game-recognition-switch-program-extra"), std::string("game-recognition-switch-program\0", 32)}) {
        unrelated.push_back({{"diagnosticAction", entry}, {"diagnosticActionId", "PRIVATE-unrelated-token"}});
    }
    for (const auto& args : unrelated) {
        resetWindows(); const auto context = recorder.begin(true, args, "window.minimize", sink, true);
        recorder.end(true, args, context, "post-returned", 0, true, true, sink);
        check(!context.active && sink.rows.empty() && win.snapshotCalls == 0 && win.error == 777, "unrelated minimize sampled despite callerRequired or altered last error");
    }
    auto known = caller("invalid token with PRIVATE spaces"); resetWindows();
    const auto context = recorder.begin(true, known, "window.minimize", sink, true);
    check(context.active && context.sequence == 1 && sink.rows.size() == 1, "recognized entry admission incorrectly depends on valid token");
    check(sink.rows[0]["caller"]["token"].is_null(), "known entry retained an invalid token");
    recorder.end(true, known, context, "post-returned", 0, true, false, sink);
    check(sink.rows.back()["postAccepted"] == false, "failed post evidence changed to accepted");
}
static void sinkFailures() {
    ev::Recorder recorder; resetWindows(); unsigned calls = 0;
    auto bad = [&](Json) { ++calls; win.error = 999; throw std::runtime_error("PRIVATE sink error"); };
    const auto args = caller(); const auto context = recorder.begin(true, args, "shell.taskView", bad);
    check(context.active && calls == 1 && recorder.failures() == 1 && win.error == 777, "begin sink exception propagated or last error/count lost");
    win.error = 321; recorder.end(true, args, context, "call-threw", 321, false, false, bad);
    check(calls == 2 && recorder.failures() == 2 && win.error == 321, "end sink exception propagated or last error/count lost");
    Rows good; const auto next = recorder.begin(true, args, "shell.taskView", good);
    check(next.sequence == context.sequence + 1 && good.rows[0]["diagnosticFailures"] == 2, "sink failure broke sequence or subsequent error counter");
    check(good.rows[0].dump().find("PRIVATE") == std::string::npos, "sink error text retained");
}
static void sinkAccountingScope() {
    ev::Recorder recorder; resetWindows(); unsigned swallowed = 0; Json attempted;
    auto swallowingSink = [&](Json row) {
        attempted = std::move(row);
        try { throw std::runtime_error("PRIVATE lower sink write error"); }
        catch (...) { ++swallowed; }
        win.error = 9191;
    };
    const auto context = recorder.begin(true, caller(), "shell.taskView", swallowingSink);
    check(context.active && swallowed == 1 && recorder.failures() == 0 && win.error == 777, "recorder counted a swallowed lower-sink failure as caught/durable acknowledgement");
    check(attempted["diagnosticFailureScope"] == "caught-by-this-recorder-only" && attempted["logSinkReturnIsDurableReceipt"] == false && attempted["diagnosticFailures"] == 0, "row overstates sink persistence or failure visibility");
    auto nonstandardSink = [](Json) { win.error = 2020; throw 17; };
    win.error = 3131;
    recorder.end(true, caller(), context, "call-returned", 3131, false, false, nonstandardSink);
    check(recorder.failures() == 1 && win.error == 3131, "non-standard sink exception escaped or overwrote last error");
}
static void gateTransitions() {
    ev::Recorder recorder; Rows sink; const auto args = caller(); resetWindows(); unsigned operations = 0;
    const auto off = recorder.begin(false, args, "shell.taskView", sink); ++operations;
    recorder.end(true, args, off, "call-returned", 0, false, false, sink);
    check(sink.rows.empty() && win.apiCalls == 0, "off-to-on emitted orphan after row");
    const auto on = recorder.begin(true, args, "shell.taskView", sink); ++operations;
    const auto callsBeforeEnd = win.apiCalls;
    recorder.end(false, args, on, "call-returned", 0, false, false, sink);
    check(sink.rows.size() == 1 && win.apiCalls == callsBeforeEnd && operations == 2, "on-to-off sampled/logged after gate closed or changed action");
    const auto next = recorder.begin(true, caller("fresh"), "shell.taskView", sink);
    recorder.end(true, caller("fresh"), next, "call-returned", 0, false, false, sink);
    check(sink.rows.size() == 3 && sink.rows[1]["nativeRequestSequence"] == sink.rows[2]["nativeRequestSequence"] && next.sequence != on.sequence, "reopened gate reused an unfinished native sequence");
}
static void concurrentRecorder() {
    ev::Recorder recorder; Rendezvous rendezvous; std::mutex rowsMutex; std::vector<Json> rows;
    std::atomic<unsigned> issued{0}; std::atomic<bool> threadFailed{false};
    auto sink = [&](Json row) {
        const bool block = row["caller"]["token"] == "concurrent:A" && row["phase"] == "before-call";
        { std::lock_guard<std::mutex> lock(rowsMutex); rows.push_back(std::move(row)); }
        if (block) rendezvous.enterAndWait();
    };
    auto command = [&](const char* token, const char* name, bool second) {
        try {
            resetWindows(); const auto args = caller(token);
            const auto context = recorder.begin(true, args, name, sink);
            ++issued; recorder.end(true, args, context, "call-returned", 0, false, false, sink);
        } catch (...) { threadFailed = true; }
        if (second) rendezvous.finishSecond();
    };
    std::thread first(command, "concurrent:A", "shell.taskView", false);
    const bool entered = rendezvous.waitEntered();
    std::thread second(command, "concurrent:B", "window.minimize", true);
    const bool independent = rendezvous.waitSecond(); rendezvous.release(); first.join(); second.join();
    check(entered && independent && !rendezvous.timedOut, "blocked diagnostic sink serialized the other command");
    check(!threadFailed && issued == 2 && rows.size() == 4, "concurrent command/action lost");
    std::map<std::string, std::vector<Json>> byToken;
    for (const auto& row : rows) byToken[row["caller"]["token"].get<std::string>()].push_back(row);
    paired(byToken["concurrent:A"], "shell.taskView", "concurrent:A");
    paired(byToken["concurrent:B"], "window.minimize", "concurrent:B");
    check(byToken["concurrent:A"][0]["nativeRequestSequence"] != byToken["concurrent:B"][0]["nativeRequestSequence"], "concurrent tokens shared native sequence");
}
static void concurrentStress() {
    ev::Recorder recorder; std::mutex mutex; std::vector<Json> rows; std::atomic<unsigned> issued{0}; std::atomic<bool> failed{false};
    auto sink = [&](Json row) { std::lock_guard<std::mutex> lock(mutex); rows.push_back(std::move(row)); };
    std::vector<std::thread> threads;
    constexpr unsigned threadCount = 6, perThread = 24;
    for (unsigned t = 0; t < threadCount; ++t) threads.emplace_back([&, t] {
        try {
            for (unsigned i = 0; i < perThread; ++i) {
                resetWindows(); const auto args = caller("stress:" + std::to_string(t) + ":" + std::to_string(i));
                const auto context = recorder.begin(true, args, "shell.taskView", sink);
                ++issued; recorder.end(true, args, context, "call-returned", 0, false, false, sink);
            }
        } catch (...) { failed = true; }
    });
    for (auto& thread : threads) thread.join();
    check(!failed && issued == threadCount * perThread && rows.size() == threadCount * perThread * 2, "stress lost action/rows");
    std::map<std::uint64_t, std::vector<Json>> pairs;
    for (const auto& row : rows) pairs[row["nativeRequestSequence"].get<std::uint64_t>()].push_back(row);
    check(pairs.size() == threadCount * perThread, "stress reused a sequence");
    for (const auto& item : pairs) {
        const auto& pair = item.second;
        check(pair.size() == 2 && pair[0]["phase"] == "before-call" && pair[1]["phase"] == "after-call" && pair[0]["caller"] == pair[1]["caller"], "stress pair/token ambiguity");
    }
}
static void extractedHandlersBasic() {
    check(handlers.size() == 2, "expected two real IPC registrations");
    for (const char* command : {"shell.taskView", "window.minimize"}) {
        for (const bool gate : {false, true}) {
            resetAction(); action.gate = gate; win.async[VK_LCONTROL] = asyncBits(0x8000);
            const auto result = handlers.at(command)(caller()); const auto rows = rowsCopy();
            check(result == true && action.taskCalls + action.postCalls == 1, "handler result/call count changed");
            check(action.taskCalls == (std::string(command) == "shell.taskView" ? 1u : 0u), "wrong original producer called");
            check(action.postArgumentsOk && action.sinkDomainOk && action.gateReads == 2, "original post arguments/log domain/helper gate reads changed");
            check(win.error == 4321, "handler diagnostics overwrote operation last error");
            if (gate) {
                paired(rows, command, "switch:fixture-1");
                if(std::string(command)=="shell.taskView")check(action.taskSequence==rows.front()["nativeRequestSequence"],"API result sequence detached from action");
                check(rows[0]["keyboardModifiers"]["asyncDownMask"] == 1 && rows[1]["keyboardModifiers"]["asyncDownMask"] == 32, "real handler boundaries do not surround original producer");
                check(rows[1]["win32ErrorHint"] == 4321 && rows[1]["operationOutcome"] == (std::string(command) == "shell.taskView" ? "call-returned" : "post-returned"), "real handler outcome/error evidence");
            } else check(rows.empty() && action.sinkCalls == 0 && win.snapshotCalls == 0, "real disabled handler sampled or logged");
        }
    }
    for (const Json args : {Json(nullptr), Json::array({1}), Json::object(), Json{{"diagnosticAction", "unknown"}}}) {
        resetAction(); check(handlers.at("window.minimize")(args) == true, "unrelated minimize no longer returns true");
        check(action.postCalls == 1 && rowsCopy().empty() && win.snapshotCalls == 0 && action.sinkCalls == 0, "unrelated real minimize sampled/logged or lost original post");
        resetAction(); check(handlers.at("shell.taskView")(args) == true && action.taskCalls == 1 && rowsCopy().size() == 2, "taskView rejected unknown/nonobject caller");
    }
}
static void extractedDeadlineForwarding() {
    struct DeadlineCase { Json args; ULONGLONG expected; };
    auto valid = caller("deadline:valid"); valid["_nativeTaskViewDeadlineTick"] = Json::number_unsigned_t(0x100000001ULL);
    auto zero = caller("deadline:zero"); zero["_nativeTaskViewDeadlineTick"] = Json::number_unsigned_t(0);
    auto invalid = caller("deadline:invalid"); invalid["_nativeTaskViewDeadlineTick"] = "PRIVATE deadline text";
    auto negative = caller("deadline:negative"); negative["_nativeTaskViewDeadlineTick"] = -1;
    const std::vector<DeadlineCase> inputs{{valid,0x100000001ULL},{zero,0},{caller("deadline:absent"),0},{invalid,0},{negative,0},{Json(nullptr),0}};
    for (const auto& input : inputs) for (const bool enabled : {false,true}) {
        resetAction(); action.gate = enabled;
        check(handlers.at("shell.taskView")(input.args) == true && action.taskCalls == 1 && action.taskDeadline == input.expected, "handler deadline parse/forward changed command contract");
        check(win.error == 4321 && action.postCalls == 0, "deadline forwarding changed operation/error");
        const auto rows = rowsCopy();
        check(rows.size() == (enabled ? 2u : 0u), "deadline forwarding changed diagnostic gate");
        for (const auto& row : rows) check(row.dump().find("PRIVATE") == std::string::npos && !row.contains("_nativeTaskViewDeadlineTick"), "deadline args leaked arbitrary diagnostic text");
    }
}
static void extractedFailures() {
    for (const bool gate : {false, true}) for (const bool badSink : {false, true}) for (const int kind : {1, 2}) {
        resetAction(); action.gate = gate; action.sinkThrows = badSink; action.throwKind = kind;
        int identity = 17; action.identity = &identity; bool caught = false;
        const auto oldFailures = g_taskViewActionEvidence.failures();
        try { handlers.at("shell.taskView")(caller("throw:original")); }
        catch (const OriginalError& error) { caught = kind == 1 && error.identity == &identity && std::string(error.what()) == "TaskView command fixture original failure: contract-sentinel"; }
        catch (const NonStandardError& error) { caught = kind == 2 && error.identity == &identity; }
        check(caught && action.taskCalls == 1 && action.postCalls == 0 && win.error == 4321, "original exception type/message/identity/count/error altered");
        check(g_taskViewActionEvidence.failures() == oldFailures + (gate && badSink ? 2u : 0u), "real handler sink failure count");
        const auto rows = rowsCopy();
        if (gate && !badSink) { paired(rows, "shell.taskView", "throw:original"); check(rows[1]["operationOutcome"] == "call-threw", "catch evidence omitted"); }
        else check(rows.empty(), "disabled/failed sink produced rows");
    }
    for (const bool gate : {false, true}) for (const bool badSink : {false, true}) for (const bool accepted : {false, true}) {
        resetAction(); action.gate = gate; action.sinkThrows = badSink; action.postAccepted = accepted;
        const auto oldFailures = g_taskViewActionEvidence.failures();
        check(handlers.at("window.minimize")(caller("post:result")) == accepted, "top-menu minimize hid failed posting receipt");
        check(action.postCalls == 1 && action.postArgumentsOk && win.error == 4321, "failed post was retried, altered, or last error lost");
        check(g_taskViewActionEvidence.failures() == oldFailures + (gate && badSink ? 2u : 0u), "post diagnostic failure accounting");
        const auto rows = rowsCopy();
        if (gate && !badSink) check(rows.size() == 2 && rows[1]["postAccepted"] == accepted, "real post acceptance observation");
        else check(rows.empty(), "post disabled/throwing sink unexpectedly logged");
    }
    resetAction(); action.sinkThrows = true;
    const auto oldFailures = g_taskViewActionEvidence.failures();
    check(handlers.at("shell.taskView")(caller()) == true && action.taskCalls == 1, "throwing sink suppressed a successful taskView");
    check(g_taskViewActionEvidence.failures() == oldFailures + 2 && win.error == 4321, "successful action diagnostic error isolation");
}
static void extractedGateTransitions() {
    for (const char* command : {"shell.taskView", "window.minimize"}) for (const bool initial : {false, true}) {
        resetAction(); action.gate = initial; action.changeGate = true; action.gateAfterAction = !initial;
        check(handlers.at(command)(caller("gate:change")) == true && action.taskCalls + action.postCalls == 1, "mid-operation gate change altered original action");
        const auto rows = rowsCopy();
        check(rows.size() == (initial ? 1u : 0u) && action.sinkCalls == (initial ? 1u : 0u), "gate transition emitted orphan or forbidden after row");
        check(win.asyncCalls == (initial ? 8u : 0u) && win.error == 4321, "gate transition snapshot/error invariant");
    }
}
static void extractedConcurrency() {
    resetAction(); Rendezvous rendezvous; std::atomic<unsigned> issued{0}; std::atomic<bool> failed{false};
    std::thread first([&] {
        try {
            resetWindows(); action = Action{}; action.blockInProducer = &rendezvous;
            const auto result = handlers.at("shell.taskView")(caller("handler:A"));
            if (result != true || action.taskCalls != 1 || win.error != 4321) failed = true;
            issued += action.taskCalls;
        } catch (...) { failed = true; }
    });
    const bool entered = rendezvous.waitEntered();
    std::thread second([&] {
        try {
            resetWindows(); action = Action{};
            const auto result = handlers.at("window.minimize")(caller("handler:B"));
            if (result != true || action.postCalls != 1 || !action.postArgumentsOk || win.error != 4321) failed = true;
            issued += action.postCalls;
        } catch (...) { failed = true; }
        rendezvous.finishSecond();
    });
    const bool independent = rendezvous.waitSecond(); rendezvous.release(); first.join(); second.join();
    check(entered && independent && !rendezvous.timedOut && !failed && issued == 2, "real handlers serialized/lost concurrent original operations");
    std::map<std::string, std::vector<Json>> byToken;
    for (const auto& row : rowsCopy()) byToken[row["caller"]["token"].get<std::string>()].push_back(row);
    paired(byToken["handler:A"], "shell.taskView", "handler:A");
    paired(byToken["handler:B"], "window.minimize", "handler:B");
    check(byToken["handler:A"][0]["nativeRequestSequence"] != byToken["handler:B"][0]["nativeRequestSequence"], "real concurrent handler correlation collision");
}
static void runCase(const char* name, void (*test)()) {
    const auto before = checks.load();
    try { test(); cases.push_back({{"case", name}, {"pass", true}, {"checks", checks.load() - before}}); }
    catch (const std::exception& error) { cases.push_back({{"case", name}, {"pass", false}, {"checks", checks.load() - before}, {"error", error.what()}}); }
    catch (...) { cases.push_back({{"case", name}, {"pass", false}, {"checks", checks.load() - before}, {"error", "non-standard fixture exception"}}); }
}
}
int main() {
    using namespace fixture;
    registerTaskViewExtractedHandlers();
    runCase("gate-off-zero-snapshot-log-and-sequence", gateOff);
    runCase("paired-sequence-pre-post-modifier-change", snapshotsAndSequence);
    runCase("queue-failure-and-foreground-race", snapshotFailure);
    runCase("all-256-token-bytes-lengths-and-hostile-arguments", tokenValidation);
    runCase("activation-origin-validation-and-privacy", activationValidation);
    runCase("minimize-known-caller-admission", requiredCaller);
    runCase("sink-exceptions-last-error-and-error-count", sinkFailures);
    runCase("caught-failure-scope-and-no-durable-sink-receipt", sinkAccountingScope);
    runCase("gate-mid-command-transitions", gateTransitions);
    runCase("concurrent-recorder-with-blocked-sink", concurrentRecorder);
    runCase("concurrent-sequence-token-stress", concurrentStress);
    runCase("extracted-real-helpers-and-handlers", extractedHandlersBasic);
    runCase("extracted-deadline-parse-and-forwarding-contract", extractedDeadlineForwarding);
    runCase("extracted-original-exceptions-and-post-failure", extractedFailures);
    runCase("extracted-gate-mid-command-transitions", extractedGateTransitions);
    runCase("extracted-concurrent-original-commands", extractedConcurrency);
    bool pass = true; for (const auto& item : cases) if (!item["pass"].get<bool>()) pass = false;
    std::cout << Json({{"pass", pass}, {"checks", checks.load()}, {"cases", cases}, {"actualOSInput", false}, {"productExecuted", false}, {"allSnapshotApisReplaced", true}, {"realMainHandlersExtracted", true}, {"commandWrapperContractOnly", true}, {"deadlineForwardingTested", true}, {"realInputProducerTested", false}, {"systemApiHelperOrTimeoutTested", false}, {"concurrencyStressCommands", 144}}).dump(2) << '\n';
    return pass ? 0 : 1;
}
