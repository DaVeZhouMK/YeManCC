// Offline fixture: real headers + extracted production wrapper; no OS input/token calls.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;

// Count/fail only this thread's allocations inside the wrapper, including real JSON.
namespace heapProbe {
thread_local bool enabled = false;
thread_local std::size_t calls = 0;
thread_local std::ptrdiff_t failAt = -1;
thread_local bool fired = false;
void* allocate(std::size_t size) {
    if (enabled) {
        const auto ordinal = calls++;
        if (failAt >= 0 && ordinal == static_cast<std::size_t>(failAt)) {
            failAt = -1; fired = true; throw std::bad_alloc();
        }
    }
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
}
void* operator new(std::size_t size) { return heapProbe::allocate(size); }
void* operator new[](std::size_t size) { return heapProbe::allocate(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace fake {
std::atomic<unsigned> checks{0};
void check(bool okay, const char* label) {
    checks.fetch_add(1, std::memory_order_relaxed);
    if (!okay) throw std::runtime_error(label);
}
enum class Integrity {
    Valid, ApiError, ZeroLength, ShortLength, Oversized, NullSid, BeforeBuffer,
    AtEnd, SevenBytesLeft, HeaderOnly, CountZero, CountOverflow, Truncated,
    InvalidSid, BoundaryValid, MultipleRids, WrongAuthority
};
struct Properties {
    Integrity integrity = Integrity::Valid;
    DWORD integrityError = ERROR_INSUFFICIENT_BUFFER;
    DWORD rid = SECURITY_MANDATORY_HIGH_RID;
    bool uiOkay = true; DWORD ui = 1, uiLength = sizeof(DWORD), uiError = ERROR_ACCESS_DENIED;
    bool typeOkay = true; TOKEN_TYPE type = TokenPrimary;
    DWORD typeLength = sizeof(TOKEN_TYPE), typeError = ERROR_INVALID_PARAMETER;
};
struct OpenPlan { bool okay = true; DWORD error = ERROR_ACCESS_DENIED; Properties properties{}; };
struct Invocation {
    const INPUT* pointer = nullptr; UINT count = 0, inserted = 0; int cbSize = 0;
    DWORD incomingError = 0;
    std::array<BYTE, sizeof(INPUT) * 96> bytes{};
    std::size_t length = 0;
};
struct Row { std::string domain; json data; };
struct ConcurrentGate {
    std::mutex mutex; std::condition_variable changed;
    unsigned arrived = 0, active = 0, peak = 0, timeouts = 0;
    void enter() {
        std::unique_lock<std::mutex> lock(mutex);
        ++arrived; ++active; peak = (std::max)(peak, active); changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(2), [this] { return arrived == 2; })) ++timeouts;
        --active;
    }
};
struct State {
    bool gate = true, throwGate = false, writeSendError = true, sinkAllowed = false;
    UINT inserted = 2; DWORD error = 0x1234, sendError = 0x2468;
    DWORD pid = 43210, tid = 7654;
    unsigned gates = 0, sends = 0, observers = 0, tickCalls = 0, pidCalls = 0, tidCalls = 0;
    unsigned errorReads = 0, errorWrites = 0;
    unsigned processCalls = 0, threadCalls = 0, openProcessCalls = 0, openThreadCalls = 0;
    unsigned queries = 0, sidCounts = 0, sidValidations = 0, sidRids = 0, logAttempts = 0;
    unsigned throwTick = 0, throwQuery = 0, throwLog = 0;
    bool throwOpenThread = false;
    OpenPlan process{}, thread{};
    std::array<HANDLE, 4> closed{}; unsigned closeCount = 0;
    Invocation sent{}, observed{};
    unsigned observedSource = 0;
    std::vector<Row> rows;
    std::vector<Row> summaries;
    std::array<const char*, 128> trace{}; unsigned traceCount = 0;
    std::uintptr_t sidBegin = 0, sidEnd = 0; Integrity currentIntegrity = Integrity::Valid;
    ConcurrentGate* concurrent = nullptr;
    State() { thread.okay = false; thread.error = ERROR_NO_TOKEN; rows.reserve(4); summaries.reserve(2); }
    void note(const char* event) { check(traceCount < trace.size(), "fake trace capacity"); trace[traceCount++] = event; }
};
thread_local State* activeState = nullptr;
State& state() noexcept { return *activeState; }
struct Bind {
    State* previous;
    explicit Bind(State& value) : previous(activeState) { activeState = &value; }
    ~Bind() { activeState = previous; }
};
HANDLE handle(std::uintptr_t value) noexcept { return reinterpret_cast<HANDLE>(value); }
DWORD getLastError() noexcept { ++state().errorReads; return state().error; }
void setLastError(DWORD value) noexcept { ++state().errorWrites; state().error = value; }
ULONGLONG tick() {
    auto& s = state(); ++s.tickCalls; s.note("tick"); s.error = 0x7001;
    if (s.throwTick == s.tickCalls) throw std::runtime_error("metadata tick failure");
    return 100000 + s.tickCalls;
}
DWORD processId() { auto& s = state(); ++s.pidCalls; s.note("pid"); s.error = 0x7002; return s.pid; }
DWORD threadId() { auto& s = state(); ++s.tidCalls; s.note("tid"); s.error = 0x7003; return s.tid; }
HANDLE processHandle() { auto& s = state(); ++s.processCalls; s.note("process"); return handle(0x101); }
HANDLE threadHandle() { auto& s = state(); ++s.threadCalls; s.note("thread"); return handle(0x102); }
BOOL openProcess(HANDLE process, DWORD access, PHANDLE out) {
    auto& s = state(); ++s.openProcessCalls; s.note("open-process");
    check(process == handle(0x101) && access == TOKEN_QUERY && out, "process TOKEN_QUERY contract");
    if (!s.process.okay) { s.error = s.process.error; return FALSE; }
    *out = handle(0x201); s.error = 0x7004; return TRUE;
}
BOOL openThread(HANDLE thread, DWORD access, BOOL asSelf, PHANDLE out) {
    auto& s = state(); ++s.openThreadCalls; s.note("open-thread");
    check(thread == handle(0x102) && access == TOKEN_QUERY && asSelf == TRUE && out, "thread TOKEN_QUERY/OpenAsSelf contract");
    if (s.throwOpenThread) throw std::runtime_error("thread metadata failure");
    if (!s.thread.okay) { s.error = s.thread.error; return FALSE; }
    *out = handle(0x202); s.error = 0x7005; return TRUE;
}
BOOL close(HANDLE value) noexcept {
    auto& s = state();
    if (s.closeCount < s.closed.size()) s.closed[s.closeCount++] = value;
    s.error = 0x7006; return TRUE;
}
BOOL tokenInformation(HANDLE token, TOKEN_INFORMATION_CLASS kind, LPVOID buffer, DWORD size, PDWORD needed) {
    auto& s = state(); ++s.queries; s.note("query-token"); s.error = 0x7007;
    if (s.throwQuery == s.queries) throw std::runtime_error("token metadata failure");
    check(token == handle(0x201) || token == handle(0x202), "only opened fake tokens queried");
    const auto& p = token == handle(0x201) ? s.process.properties : s.thread.properties;
    if (kind == TokenIntegrityLevel) {
        check(size == 512 && buffer && needed, "integrity fixed 512-byte buffer (no retry)");
        if (p.integrity == Integrity::ApiError) { *needed = 1024; s.error = p.integrityError; return FALSE; }
        const auto base = reinterpret_cast<std::uintptr_t>(buffer);
        std::size_t offset = sizeof(TOKEN_MANDATORY_LABEL);
        *needed = static_cast<DWORD>(offset + 12);
        switch (p.integrity) {
        case Integrity::ZeroLength: *needed = 0; break;
        case Integrity::ShortLength: *needed = sizeof(TOKEN_MANDATORY_LABEL) - 1; break;
        case Integrity::Oversized: *needed = 513; break;
        case Integrity::BoundaryValid: offset = 500; *needed = 512; break;
        case Integrity::MultipleRids: *needed = static_cast<DWORD>(offset + 20); break;
        default: break;
        }
        std::memset(buffer, 0, size);
        auto sidAddress = base + offset;
        if (p.integrity == Integrity::NullSid) sidAddress = 0;
        if (p.integrity == Integrity::BeforeBuffer) sidAddress = base - 4;
        if (p.integrity == Integrity::AtEnd) sidAddress = base + *needed;
        if (p.integrity == Integrity::SevenBytesLeft) sidAddress = base + *needed - 7;
        if (p.integrity == Integrity::HeaderOnly) sidAddress = base + *needed - 8;
        auto* label = static_cast<TOKEN_MANDATORY_LABEL*>(buffer);
        label->Label.Sid = reinterpret_cast<PSID>(sidAddress);
        label->Label.Attributes = SE_GROUP_INTEGRITY;
        if (sidAddress >= base + sizeof(TOKEN_MANDATORY_LABEL) && sidAddress <= base + size - 8) {
            auto* sid = reinterpret_cast<BYTE*>(sidAddress);
            sid[0] = SID_REVISION;
            sid[1] = p.integrity == Integrity::CountZero ? 0 : p.integrity == Integrity::CountOverflow ?
                SID_MAX_SUB_AUTHORITIES + 1 : p.integrity == Integrity::Truncated ? 2 :
                p.integrity == Integrity::MultipleRids ? 3 : 1;
            const SID_IDENTIFIER_AUTHORITY authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
            std::memcpy(sid + 2, authority.Value, sizeof(authority.Value));
            if (p.integrity == Integrity::WrongAuthority) sid[7] = 5;
            if (sidAddress + 12 <= base + size) std::memcpy(sid + 8, &p.rid, sizeof(p.rid));
            if (p.integrity == Integrity::MultipleRids) {
                const DWORD previous = 0x1111;
                std::memcpy(sid + 8, &previous, sizeof(previous));
                std::memcpy(sid + 16, &p.rid, sizeof(p.rid));
            }
        }
        s.sidBegin = base; s.sidEnd = base + (std::min)(*needed, size); s.currentIntegrity = p.integrity;
        return TRUE;
    }
    if (kind == TokenUIAccess) {
        check(size == sizeof(DWORD) && needed, "UIAccess fixed DWORD query");
        *needed = p.uiLength;
        if (!p.uiOkay) { s.error = p.uiError; return FALSE; }
        std::memcpy(buffer, &p.ui, sizeof(p.ui)); return TRUE;
    }
    check(kind == TokenType && size == sizeof(TOKEN_TYPE) && needed, "only IL/UIAccess/type queried");
    *needed = p.typeLength;
    if (!p.typeOkay) { s.error = p.typeError; return FALSE; }
    std::memcpy(buffer, &p.type, sizeof(p.type)); return TRUE;
}
PUCHAR sidCount(PSID sid) {
    auto& s = state(); ++s.sidCounts; const auto address = reinterpret_cast<std::uintptr_t>(sid);
    check(address >= s.sidBegin && address <= s.sidEnd && s.sidEnd - address >= 8, "SID header bounds before dereference");
    return reinterpret_cast<PUCHAR>(sid) + 1;
}
BOOL validSid(PSID sid) {
    auto& s = state(); ++s.sidValidations; const auto address = reinterpret_cast<std::uintptr_t>(sid);
    check(address >= s.sidBegin && address <= s.sidEnd && s.sidEnd - address >= 8, "IsValidSid bounds");
    const auto* bytes = reinterpret_cast<const BYTE*>(sid);
    check(bytes[1] > 0 && bytes[1] <= SID_MAX_SUB_AUTHORITIES && 8u + 4u * bytes[1] <= s.sidEnd - address, "SID body bounds before validation");
    return s.currentIntegrity == Integrity::InvalidSid ? FALSE : TRUE;
}
PDWORD sidAuthority(PSID sid, DWORD index) {
    auto& s = state(); ++s.sidRids; const auto address = reinterpret_cast<std::uintptr_t>(sid);
    check(index < SID_MAX_SUB_AUTHORITIES && address >= s.sidBegin && address + 12 + 4 * index <= s.sidEnd, "SID RID bounds");
    return reinterpret_cast<PDWORD>(reinterpret_cast<BYTE*>(sid) + 8 + 4 * index);
}
void capture(Invocation& call, UINT count, const INPUT* inputs, int size) {
    call.pointer = inputs; call.count = count; call.cbSize = size; call.incomingError = state().error;
    call.length = inputs ? static_cast<std::size_t>(count) * sizeof(INPUT) : 0;
    check(call.length <= call.bytes.size(), "fixture input capacity");
    if (call.length) std::memcpy(call.bytes.data(), inputs, call.length);
}
UINT sendInput(UINT count, LPINPUT inputs, int size) {
    auto& s = state(); s.note("send"); ++s.sends; capture(s.sent, count, inputs, size);
    if (s.concurrent) s.concurrent->enter();
    if (s.writeSendError) s.error = s.sendError;
    return s.inserted;
}
}

// Include windows.h before these aliases so dllimport declarations are never rewritten.
#define GetLastError fake::getLastError
#define SetLastError fake::setLastError
#define GetTickCount64 fake::tick
#define GetCurrentProcessId fake::processId
#define GetCurrentThreadId fake::threadId
#define GetCurrentProcess fake::processHandle
#define GetCurrentThread fake::threadHandle
#define OpenProcessToken fake::openProcess
#define OpenThreadToken fake::openThread
#define GetTokenInformation fake::tokenInformation
#define CloseHandle fake::close
#define GetSidSubAuthorityCount fake::sidCount
#define IsValidSid fake::validSid
#define GetSidSubAuthority fake::sidAuthority
#define SendInput fake::sendInput
#include "keyboard_self_send_evidence.h"

static bool domainLogEnabled(const char* domain) {
    auto& s = fake::state(); ++s.gates; s.note("gate"); s.error = 0x7008;
    fake::check(std::strcmp(domain, "virtual") == 0, "actual virtual domain gate");
    if (s.throwGate) throw std::runtime_error("log gate failure");
    return s.gate;
}
static void appendDomainLog(const char* domain, const char* event, const json& row) {
    auto& s = fake::state(); ++s.logAttempts; s.note("log"); s.error = 0x7009;
    fake::check(s.sinkAllowed, "sender must never call any file/log sink");
    if (s.throwLog == s.logAttempts) throw std::runtime_error("log sink failure");
    if (std::strcmp(event, "keyboard-self-send-batch") == 0) s.rows.push_back({domain, row});
    else {
        fake::check(std::strcmp(event, "keyboard-self-send-batch-queue") == 0, "real queue summary event");
        s.summaries.push_back({domain, row});
    }
}
static void keyboardModifierObserveSelfSend(ymcc::keyboardEvents::SourceId source, const INPUT* inputs, UINT requested, UINT inserted) noexcept {
    auto& s = fake::state(); ++s.observers; s.note("observer");
    fake::capture(s.observed, requested, inputs, sizeof(INPUT));
    s.observed.inserted = inserted; s.observedSource = static_cast<unsigned>(source); s.error = 0x7010;
}
// Both same-name atomic definitions and the function body come verbatim from main.cpp.
#include "actual_keyboard_self_send.inc"
#undef GetLastError
#undef SetLastError
#undef GetTickCount64
#undef GetCurrentProcessId
#undef GetCurrentThreadId
#undef GetCurrentProcess
#undef GetCurrentThread
#undef OpenProcessToken
#undef OpenThreadToken
#undef GetTokenInformation
#undef CloseHandle
#undef GetSidSubAuthorityCount
#undef IsValidSid
#undef GetSidSubAuthority
#undef SendInput

using Source = ymcc::keyboardEvents::SourceId;
using BatchRecord = ymcc::selfSendEvidence::BatchRecord;
static_assert(std::is_trivially_copyable_v<BatchRecord> && std::is_trivially_destructible_v<BatchRecord> && sizeof(BatchRecord) <= 1024);
using fake::check;
static json report = {{"pass", false}, {"cases", json::array()}, {"tokenCases", json::array()}};
static void resetSequence() {
    const auto drained = g_keyboardSelfSendBatchQueue.take();
    check(drained.taken, "test isolation drains real queue without replacing its mutex");
    g_keyboardSelfSendBatchSequence = 0; g_keyboardSelfSendBatchFailures = 0;
}
static void flush(fake::State& s, bool enabled = true, ULONGLONG now = 900000) {
    const auto saved = s.error; s.sinkAllowed = true;
    keyboardSelfSendFlushEvidence(enabled, now); s.sinkAllowed = false;
    check(s.error == saved, "actual flush preserves caller LastError");
}
static INPUT key(WORD vk, DWORD flags = 0, std::uintptr_t tag = 0) {
    INPUT value; std::memset(&value, 0xA5, sizeof(value));
    value.type = INPUT_KEYBOARD; value.ki.wVk = vk; value.ki.wScan = 0x2A;
    value.ki.dwFlags = flags; value.ki.time = 0x345678; value.ki.dwExtraInfo = tag;
    return value;
}
static std::vector<INPUT> altBatch() { return {key(VK_MENU), key(VK_MENU, KEYEVENTF_KEYUP)}; }
static BatchRecord batchRecord(std::uint64_t callId = 1) {
    const INPUT inputs[]{key(VK_MENU), key(VK_MENU, KEYEVENTF_KEYUP)};
    BatchRecord out{}; out.request = ymcc::selfSendEvidence::captureRequest(inputs, 2);
    out.source = Source::FocusAltFallback; out.callId = callId; return out;
}
static std::string hex(const void* bytes, std::size_t length) {
    const auto* p = static_cast<const BYTE*>(bytes); std::string text; text.reserve(length * 2);
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < length; ++i) { text.push_back(digits[p[i] >> 4]); text.push_back(digits[p[i] & 15]); }
    return text;
}
static json rows(const fake::State& s) {
    auto out = json::array();
    for (const auto& row : s.rows) out.push_back({{"domain", row.domain}, {"row", row.data}});
    return out;
}
static void verifySend(const fake::State& s, Source source, UINT requested, INPUT* pointer,
    const std::vector<INPUT>& before, DWORD incoming, UINT returned) {
    check(s.sends == 1 && s.observers == 1, "exactly one original SendInput and old observer; no recovery sends");
    check(s.sent.pointer == pointer && s.observed.pointer == pointer && s.sent.count == requested && s.observed.count == requested, "pointer/count preserved");
    check(s.sent.cbSize == sizeof(INPUT) && s.sent.incomingError == incoming, "cbSize and SendInput entry LastError preserved");
    check(s.observedSource == static_cast<unsigned>(source) && s.observed.inserted == s.inserted && returned == s.inserted, "observer source/inserted and return preserved");
    const auto length = pointer ? static_cast<std::size_t>(requested) * sizeof(INPUT) : 0;
    check(s.sent.length == length && s.observed.length == length, "original input extent");
    check(!length || (std::memcmp(s.sent.bytes.data(), before.data(), length) == 0 &&
        std::memcmp(s.observed.bytes.data(), before.data(), length) == 0 && std::memcmp(pointer, before.data(), length) == 0), "every INPUT byte, padding, scan, time, tag unchanged");
    const DWORD expectedError = s.writeSendError ? s.sendError : incoming;
    check(s.error == expectedError && s.observed.incomingError == expectedError, "send LastError survives observer and diagnostics");
}
static void verifyLogs(const fake::State& s, Source source, UINT requested) {
    check(s.rows.size() == 4 && s.logAttempts == 6, "queued begin and returned mirrored at flush, plus queue summary");
    check(s.summaries.size() == 2 && s.summaries[0].data == s.summaries[1].data &&
        s.summaries[0].domain == "focus" && s.summaries[1].domain == "virtual", "queue summary mirrored");
    check(s.summaries[0].data["rowsDrained"] == 2 && s.summaries[0].data["capacity"] == 32 &&
        s.summaries[0].data["completeInputCoverage"] == false && s.summaries[0].data["emptyIsAbsenceProof"] == false &&
        s.summaries[0].data["crossCounterAtomicSnapshot"] == false && s.summaries[0].data["gateEpochKnown"] == false, "queue counts and evidence limitations");
    check(s.rows[0].data["drainTick"] == 900000 && s.rows[0].data["queueCapacity"] == 32 &&
        s.rows[0].data["queueLossCountersCumulative"] == true, "drain time and cumulative queue metadata");
    check(s.rows[0].domain == "focus" && s.rows[1].domain == "virtual" && s.rows[2].domain == "focus" && s.rows[3].domain == "virtual", "domain order");
    check(s.rows[0].data == s.rows[1].data && s.rows[2].data == s.rows[3].data, "domain mirrors identical");
    const auto& pre = s.rows[0].data; const auto& post = s.rows[2].data;
    check(pre["phase"] == "begin" && post["phase"] == "returned" && pre["callId"] == post["callId"] && pre["callId"].get<std::uint64_t>() != 0, "same nonzero batch ID across real pre/post");
    check(pre["callerContextBeforeSendKnown"] == true && post["callerContextBeforeSendKnown"] == true &&
        pre["senderCppDynamicAllocations"] == false && post["senderCppDynamicAllocations"] == false, "POD capture context known and sender allocation claim preserved at drain");
    check(pre["source"] == ymcc::keyboardEvents::sourceName(source) && pre["requestedInputs"] == requested, "real sourceName and requested count");
    check(post["insertedInputs"] == s.inserted && post["fullBatchInserted"] == (s.inserted == requested), "full/partial/zero insertion evidence");
    const DWORD expectedError = s.writeSendError ? s.sendError : s.sent.incomingError;
    check(post["win32ErrorHint"] == (s.inserted == requested ? 0 : expectedError), "errorHint is exact send error only for incomplete insertion");
    check(post["errorHintIsDefinitive"] == false && post["returnTokenContextCaptured"] == false, "error and token evidence limits");
    for (const auto* name : {"inputOrTagChanged", "ordinaryKeyContentsRetained", "hookEventUniquePairingProven", "osOrGameDeliveryProven", "beginRecordIsSendEntryProof", "logWriteReceiptAvailable"})
        check(pre[name] == false && post[name] == false, "request evidence does not overclaim");
    auto normalized = post;
    for (const auto* name : {"returnTick", "insertedInputs", "fullBatchInserted", "win32ErrorHint", "errorHintIsDefinitive", "returnTokenContextCaptured"}) normalized.erase(name);
    normalized["phase"] = "begin";
    check(normalized == pre, "pre/post request and caller metadata identical");
    check(s.tickCalls == 3 && s.pidCalls == 1 && s.tidCalls == 1 && s.openProcessCalls == 1 && s.openThreadCalls == 1, "one pre-send caller capture, no post-send token recapture");
}
static void normalCase(const char* name, std::vector<INPUT> inputs, bool gate, bool evidence,
    UINT inserted, DWORD sendError, bool writeError = true, bool nullPointer = false, UINT nullCount = 0,
    Source source = Source::NativeKeyboardShortcut) {
    resetSequence(); fake::State s; fake::Bind bind(s);
    s.gate = gate; s.inserted = inserted; s.sendError = sendError; s.writeSendError = writeError;
    const auto before = inputs; const auto incoming = s.error;
    const auto count = nullPointer ? nullCount : static_cast<UINT>(inputs.size());
    auto* pointer = nullPointer ? nullptr : inputs.data();
    heapProbe::calls = 0; heapProbe::enabled = true;
    const auto returned = keyboardSelfSendWithEvidence(source, count, pointer);
    heapProbe::enabled = false; const auto allocations = heapProbe::calls;
    verifySend(s, source, count, pointer, before, incoming, returned);
    check(allocations == 0, "every normal sender path has zero dynamic allocations");
    check(s.gates == (evidence ? 3u : 1u) && g_keyboardSelfSendBatchFailures == 0, "wrapper/record gates and no ordinary-path construction failures");
    check(s.logAttempts == 0 && s.rows.empty() && s.summaries.empty(), "send path queues only; no sink call before explicit flush");
    if (evidence) { flush(s); verifyLogs(s, source, count); check(g_keyboardSelfSendBatchSequence == 1, "one callId per evidenced batch"); }
    else {
        check(s.rows.empty() && s.logAttempts == 0 && g_keyboardSelfSendBatchSequence == 0, "closed/nonmodifier path has no JSON logs or callId increment");
        check(s.queries == 0 && s.openProcessCalls == 0 && s.openThreadCalls == 0 && s.pidCalls == 0 && s.tidCalls == 0 && s.tickCalls == 0 && s.closeCount == 0, "closed/nonmodifier path skips identity/token/tick");
        if (!gate || nullPointer || !count) check(allocations == 0, "closed/empty path allocates no JSON");
    }
    report["cases"].push_back({{"name", name}, {"requested", count}, {"returned", returned},
        {"incomingError", incoming}, {"lastError", s.error}, {"allocations", allocations},
        {"sendCalls", s.sends}, {"observerCalls", s.observers}, {"inputHex", hex(before.data(), before.size() * sizeof(INPUT))},
        {"sentHex", hex(s.sent.bytes.data(), s.sent.length)}, {"rows", rows(s)}});
}
static void normalCases() {
    for (bool gate : {false, true}) for (UINT inserted : {0u, 1u, 2u}) for (DWORD error : {0ul, 5ul, 0xBEEFul}) {
        const auto label = std::string(gate ? "enabled-" : "disabled-") + std::to_string(inserted) + "-" + std::to_string(error);
        normalCase(label.c_str(), altBatch(), gate, gate, inserted, error, true, false, 0, Source::FocusAltFallback);
    }
    normalCase("full-SendInput-leaves-error", altBatch(), true, true, 2, 0, false);
    normalCase("partial-SendInput-leaves-error", altBatch(), true, true, 1, 0, false);
    normalCase("zero-SendInput-leaves-error", altBatch(), true, true, 0, 0, false);
    normalCase("ordinary-only", {key('J', 0, 0x11223344), key('J', KEYEVENTF_KEYUP, 0x88776655)}, true, false, 2, 91);
    INPUT mouse; std::memset(&mouse, 0xB6, sizeof(mouse)); mouse.type = INPUT_MOUSE;
    normalCase("nonkeyboard-only", {mouse}, true, false, 1, 92);
    normalCase("null-with-count", {}, true, false, 0, ERROR_INVALID_PARAMETER, true, true, 7);
    normalCase("zero-count", {}, true, false, 0, 93);
    auto mixed = std::vector<INPUT>{key(VK_SHIFT, 0, 0x123456789ABCDEF0ull), key('Q'),
        key('Q', KEYEVENTF_KEYUP), key(VK_SHIFT, KEYEVENTF_KEYUP, 0x1111222233334444ull), mouse,
        key(VK_LMENU, KEYEVENTF_SCANCODE, 0x77), key('X', KEYEVENTF_UNICODE, 0x88)};
    normalCase("shift-mixed-tags", mixed, true, true, 3, ERROR_ACCESS_DENIED);
    normalCase("scancode-not-guessed", {key(VK_LSHIFT, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0xAC)}, true, true, 1, 94);
    normalCase("unicode-not-guessed", {key(VK_LMENU, KEYEVENTF_UNICODE, 0xBC)}, true, true, 0, 95);
    for (const auto source : {Source::NativeKeyboardShortcut, Source::FocusAltFallback}) {
        normalCase(ymcc::keyboardEvents::sourceName(source), altBatch(), true, true, 2, 96, true, false, 0, source);
    }
    resetSequence(); fake::State s; fake::Bind bind(s); auto inputs = altBatch();
    (void)keyboardSelfSendWithEvidence(Source::FocusAltFallback, 2, inputs.data());
    const auto seq = g_keyboardSelfSendBatchSequence.load(); s.gate = false; s.rows.clear(); s.sends = s.observers = 0;
    (void)keyboardSelfSendWithEvidence(Source::FocusAltFallback, 2, inputs.data());
    check(g_keyboardSelfSendBatchSequence == seq && s.rows.empty(), "closed gate does not advance an existing nonzero sequence");
}
static void requestCases(bool exerciseWrapper = true) {
    using ymcc::selfSendEvidence::request;
    check(request(nullptr, 55).is_null() && request(nullptr, 0).is_null(), "null request unknown");
    auto inputs = altBatch(); const auto before = inputs; auto r = request(inputs.data(), 2);
    check(r["modifierEntryCount"] == 2 && r["attemptedModifierDownMask"] == ymcc::keyboardEvents::bit(VK_MENU) &&
        r["attemptedModifierUpMask"] == ymcc::keyboardEvents::bit(VK_MENU), "real generic Alt bit used");
    check(r["modifierEntries"][0]["index"] == 0 && r["modifierEntries"][0]["down"] == true &&
        r["modifierEntries"][1]["down"] == false && std::memcmp(inputs.data(), before.data(), inputs.size() * sizeof(INPUT)) == 0, "request order, edge and bytes");
    inputs.clear(); unsigned all = 0;
    for (const auto vk : ymcc::keyboardEvents::modifierKeys) {
        inputs.push_back(key(static_cast<WORD>(vk), 0, 0x5555)); inputs.push_back(key(static_cast<WORD>(vk), KEYEVENTF_KEYUP, 0xAAAA));
        all |= ymcc::keyboardEvents::bit(static_cast<int>(vk));
    }
    r = request(inputs.data(), static_cast<UINT>(inputs.size()));
    check(all == ymcc::keyboardEvents::allModifierMask && r["attemptedModifierDownMask"] == all && r["attemptedModifierUpMask"] == all, "all real modifier bits including generic keys");
    check(r["modifierEntryCount"] == 22 && r["modifierEntries"].size() == 16 && r["modifierEntriesTruncated"] == true && r["modifierClassificationComplete"] == true, "entry cap retains full counts and masks");
    for (std::size_t i = 0; i < 16; ++i) {
        const auto& e = r["modifierEntries"][i];
        check(e["index"] == i && e["virtualKey"] == inputs[i].ki.wVk && e["scanCode"] == inputs[i].ki.wScan &&
            e["flags"] == inputs[i].ki.dwFlags && e["extraInfo"] == inputs[i].ki.dwExtraInfo, "logged modifier fields exact");
    }
    if (exerciseWrapper) normalCase("all-modifiers-entry-cap", inputs, true, true, 11, 222);
    std::vector<INPUT> ordinary(64, key('R', 0, 0x1234));
    check(request(ordinary.data(), 64).is_null(), "64 ordinary keys skip identity evidence");
    // Only 64 entries exist: a huge declared count proves bounded inspection without reading entry 65.
    r = request(ordinary.data(), (std::numeric_limits<UINT>::max)());
    check(r["ordinaryKeysOmitted"] == 64 && r["requestedInputs"] == (std::numeric_limits<UINT>::max)() &&
        r["inspectionComplete"] == false && r["modifierClassificationComplete"] == false && r["modifierEntries"].empty(), "bounded inspection and unknown tail");
    ordinary.push_back(key(VK_LCONTROL)); if (exerciseWrapper) normalCase("modifier-only-after-inspection-limit", ordinary, true, true, 65, 223);
    std::vector<INPUT> many(80, key(VK_RWIN, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0x9988));
    r = request(many.data(), 80);
    check(r["modifierEntryCount"] == 64 && r["modifierEntries"].size() == 16 && r["requestedInputs"] == 80 && r["inspectionComplete"] == false, "64 inspection cap and original count retained");
    if (exerciseWrapper) normalCase("eighty-inputs-preserved", many, true, true, 40, 224);
    std::vector<INPUT> mixed{key('J'), key(VK_LSHIFT), key(VK_LSHIFT, KEYEVENTF_KEYUP),
        key(VK_MENU, KEYEVENTF_SCANCODE), key(VK_MENU, KEYEVENTF_UNICODE)};
    INPUT other{}; other.type = INPUT_HARDWARE; mixed.push_back(other);
    r = request(mixed.data(), static_cast<UINT>(mixed.size()));
    check(r["ordinaryKeysOmitted"] == 1 && r["keyboardInputsInspected"] == 5 && r["otherInputsInspected"] == 1 &&
        r["scanCodeOrUnicodeInputsUnclassified"] == 2 && r["modifierEntryCount"] == 2 && r["modifierClassificationComplete"] == false, "mixed request classification counts");
    check(r["modifierEntries"].size() == 2 && r["modifierEntries"][0]["virtualKey"] == VK_LSHIFT, "ordinary/scancode/unicode contents omitted");
}
static void contextCases() {
    for (unsigned processMode = 0; processMode < 3; ++processMode) for (unsigned threadMode = 0; threadMode < 3; ++threadMode) {
        fake::State s; fake::Bind bind(s);
        s.process.okay = processMode == 0; s.process.error = processMode == 1 ? ERROR_NO_TOKEN : ERROR_ACCESS_DENIED;
        s.thread.okay = threadMode == 0; s.thread.error = threadMode == 1 ? ERROR_NO_TOKEN : ERROR_ACCESS_DENIED;
        s.thread.properties.type = TokenImpersonation;
        const DWORD before = s.error; const auto r = ymcc::selfSendEvidence::context();
        check(s.error == before && r["processId"] == s.pid && r["threadId"] == s.tid, "caller identity and standalone LastError preserved");
        check(r["captureBeginTick"] == 100001 && r["captureEndTick"] == 100002 && r["openAsSelf"] == true &&
            r["maxTokenQueries"] == 8 && r["loadedSourceIdentityProven"] == false, "capture scope/budget/clock limits");
        check(s.openProcessCalls == 1 && s.openThreadCalls == 1 && s.processCalls == 1 && s.threadCalls == 1 &&
            s.queries + s.openProcessCalls + s.openThreadCalls <= 8, "bounded independent process/thread token attempts");
        check(r["processToken"]["opened"] == s.process.okay && r["processToken"]["error"] == (s.process.okay ? 0 : s.process.error), "process open result exact");
        if (s.process.okay) check(r["processToken"]["properties"]["type"]["value"] == "primary", "process primary token");
        else check(!r["processToken"].contains("properties"), "process access/no-token failure has no invented properties");
        const auto& t = r["threadToken"];
        check(t["presenceKnown"] == (s.thread.okay || s.thread.error == ERROR_NO_TOKEN), "thread token absence distinct from access failure");
        if (s.thread.okay) check(t["present"] == true && t["properties"]["type"]["value"] == "impersonation", "thread token properties");
        else check(t["error"] == s.thread.error && (s.thread.error == ERROR_NO_TOKEN ? t["present"] == false : t["present"].is_null()) && !t.contains("properties"), "failed thread context remains unknown except ERROR_NO_TOKEN");
        const auto opened = static_cast<unsigned>(s.process.okay) + static_cast<unsigned>(s.thread.okay);
        check(s.closeCount == opened && s.queries == 3 * opened, "every successfully opened token closed exactly once");
        if (opened == 2) check(s.closed[0] == fake::handle(0x202) && s.closed[1] == fake::handle(0x201), "token RAII reverse close order");
        report["tokenCases"].push_back({{"name", "context-" + std::to_string(processMode) + "-" + std::to_string(threadMode)}, {"context", r}, {"queries", s.queries}, {"closed", s.closeCount}});
    }
}
static void tokenCases() {
    const std::array<fake::Integrity, 17> variants{fake::Integrity::Valid, fake::Integrity::ApiError,
        fake::Integrity::ZeroLength, fake::Integrity::ShortLength, fake::Integrity::Oversized, fake::Integrity::NullSid,
        fake::Integrity::BeforeBuffer, fake::Integrity::AtEnd, fake::Integrity::SevenBytesLeft, fake::Integrity::HeaderOnly,
        fake::Integrity::CountZero, fake::Integrity::CountOverflow, fake::Integrity::Truncated, fake::Integrity::InvalidSid,
        fake::Integrity::BoundaryValid, fake::Integrity::MultipleRids, fake::Integrity::WrongAuthority};
    const std::array<const char*, 17> names{"valid", "api-error", "zero-length", "short-length", "oversized", "null-sid",
        "sid-before-buffer", "sid-at-end", "sid-seven-bytes-left", "sid-header-only", "count-zero", "count-overflow",
        "truncated-sid", "invalid-sid", "boundary-valid", "multiple-rids", "valid-SID-wrong-authority"};
    for (bool thread : {false, true}) for (std::size_t i = 0; i < variants.size(); ++i) {
        fake::State s; fake::Bind bind(s); auto& p = thread ? s.thread.properties : s.process.properties;
        p.integrity = variants[i]; p.rid = 0x4567;
        const auto r = ymcc::selfSendEvidence::token(fake::handle(thread ? 0x202 : 0x201));
        const bool valid = p.integrity == fake::Integrity::Valid || p.integrity == fake::Integrity::BoundaryValid || p.integrity == fake::Integrity::MultipleRids;
        check(r["integrity"]["known"] == valid && s.queries == 3, "integrity validity and no retry; other properties still queried");
        if (valid) check(r["integrity"]["rid"] == p.rid && r["integrity"]["error"] == 0 && s.sidRids == 1, "valid last SID authority retained");
        else check(r["integrity"]["rid"].is_null() && r["integrity"]["error"] == (p.integrity == fake::Integrity::ApiError ? p.integrityError : ERROR_INVALID_DATA) && s.sidRids == 0, "bad IL length/SID/count/error/authority stays unknown");
        check(r["uiAccess"]["known"] == true && r["type"]["known"] == true, "bad integrity does not suppress independent fields");
        report["tokenCases"].push_back({{"name", std::string(thread ? "thread-" : "process-") + names[i]}, {"properties", r}, {"sidCountCalls", s.sidCounts}, {"sidValidationCalls", s.sidValidations}});
    }
    for (bool thread : {false, true}) for (unsigned mode = 0; mode < 10; ++mode) {
        fake::State s; fake::Bind bind(s); auto& p = thread ? s.thread.properties : s.process.properties;
        if (mode == 0) { p.ui = 0; p.type = TokenImpersonation; }
        if (mode == 1) p.uiOkay = false;
        if (mode == 2) p.ui = 2;
        if (mode == 3) p.uiLength = 0;
        if (mode == 4) p.uiLength = 3;
        if (mode == 5) p.uiLength = 8;
        if (mode == 6) p.typeOkay = false;
        if (mode == 7) p.type = static_cast<TOKEN_TYPE>(3);
        if (mode == 8) p.typeLength = 0;
        if (mode == 9) p.typeLength = 8;
        const auto r = ymcc::selfSendEvidence::token(fake::handle(thread ? 0x202 : 0x201));
        const bool uiKnown = mode == 0 || mode >= 6, typeKnown = mode <= 5;
        check(r["uiAccess"]["known"] == uiKnown && r["type"]["known"] == typeKnown && r["integrity"]["known"] == true, "independent UIAccess/type success and failures");
        if (!uiKnown) check(r["uiAccess"]["value"].is_null() && r["uiAccess"]["error"] == (p.uiOkay ? ERROR_INVALID_DATA : p.uiError), "bad UIAccess unknown with exact error");
        else check(r["uiAccess"]["value"] == (p.ui != 0), "UIAccess false/true retained");
        if (!typeKnown) check(r["type"]["value"].is_null() && r["type"]["error"] == (p.typeOkay ? ERROR_INVALID_DATA : p.typeError), "bad token type unknown with exact error");
        else check(r["type"]["value"] == (p.type == TokenPrimary ? "primary" : "impersonation"), "token type retained");
        report["tokenCases"].push_back({{"name", std::string(thread ? "thread-property-" : "process-property-") + std::to_string(mode)}, {"properties", r}});
    }
}
static void wrapperTokenCases() {
    for (unsigned mode = 0; mode < 5; ++mode) {
        resetSequence(); fake::State s; fake::Bind bind(s); auto inputs = altBatch(); const auto before = inputs;
        s.thread.okay = mode == 0; s.thread.error = mode == 1 ? ERROR_ACCESS_DENIED : ERROR_NO_TOKEN;
        s.process.okay = mode < 2; s.process.error = mode == 2 ? ERROR_NO_TOKEN : ERROR_ACCESS_DENIED;
        if (mode == 4) { s.process.okay = s.thread.okay = true; s.process.properties.integrity = fake::Integrity::Truncated; s.thread.properties.uiOkay = false; }
        const auto incoming = s.error; heapProbe::calls = 0; heapProbe::enabled = true;
        const auto returned = keyboardSelfSendWithEvidence(Source::FocusAltFallback, 2, inputs.data());
        heapProbe::enabled = false;
        check(heapProbe::calls == 0, "token denial/absence/malformed properties use fixed sender storage only");
        verifySend(s, Source::FocusAltFallback, 2, inputs.data(), before, incoming, returned);
        check(s.logAttempts == 0, "token errors never cause sender-side sink"); flush(s); verifyLogs(s, Source::FocusAltFallback, 2);
        check(g_keyboardSelfSendBatchFailures == 0, "token denial/malformed metadata is evidence, not exception");
    }
}
static void exceptionCases() {
    auto cases = json::array();
    for (unsigned site = 0; site < 11; ++site) {
        resetSequence(); fake::State s; fake::Bind bind(s); s.thread.okay = true;
        if (site == 0) s.throwGate = true;
        if (site >= 1 && site <= 3) s.throwTick = site;
        if (site >= 4 && site <= 9) s.throwQuery = site - 3;
        if (site == 10) s.throwOpenThread = true;
        auto inputs = altBatch(); const auto before = inputs; const auto incoming = s.error;
        const auto returned = keyboardSelfSendWithEvidence(Source::FocusAltFallback, 2, inputs.data());
        verifySend(s, Source::FocusAltFallback, 2, inputs.data(), before, incoming, returned);
        check(s.logAttempts == 0 && g_keyboardSelfSendBatchFailures == 1, "recoverable metadata exception counted without sender sink");
        const unsigned expectedClosed = site == 0 || site == 1 || site == 4 ? (site == 4 ? 1u : 0u) :
            site == 5 || site == 6 || site == 10 ? 1u : 2u;
        check(s.closeCount == expectedClosed, "metadata exception closes successfully opened handles");
        const auto queued = g_keyboardSelfSendBatchQueue.take();
        check(queued.taken && queued.count == (site == 0 ? 0u : site == 3 ? 1u : 2u), "context failure still retains begin/returned; return tick failure retains begin");
        auto retained = json::array();
        for (std::size_t i = 0; i < queued.count; ++i) {
            const auto& pod = queued.rows[i]; const auto row = ymcc::selfSendEvidence::toJson(pod);
            check(pod.contextKnown == (site == 3) && pod.returned == (i == 1) && pod.callId == 1, "POD context failure state and pre/post ID");
            check(row["callerContextBeforeSendKnown"] == (site == 3) && row["senderCppDynamicAllocations"] == false, "serialized context-known state explicit");
            if (site != 3) check(row["callerContextBeforeSend"]["known"] == false &&
                row["callerContextBeforeSend"]["reason"] == "context-capture-failed" && row["diagnosticFailuresBefore"] == 1,
                "failed context is explicit unknown with failure counter");
            retained.push_back(row);
        }
        cases.push_back({{"site", site}, {"retained", retained}, {"sendCalls", s.sends}, {"observerCalls", s.observers}});
    }
    report["metadataAndLogExceptionScenarios"] = cases.size();
    report["metadataExceptionCases"] = cases;
}
static void zeroAllocationCases() {
    auto cases = json::array();
    for (const auto source : {Source::FocusAltFallback, Source::NativeKeyboardShortcut})
    for (bool threadToken : {false, true})
    for (std::ptrdiff_t ordinal : {0, 1, 22, 64, 1170}) {
        resetSequence(); fake::State s; fake::Bind bind(s); s.thread.okay = threadToken;
        auto inputs = altBatch(); const auto before = inputs; const auto incoming = s.error;
        heapProbe::calls = 0; heapProbe::fired = false; heapProbe::failAt = ordinal; heapProbe::enabled = true;
        const auto returned = keyboardSelfSendWithEvidence(source, 2, inputs.data());
        heapProbe::enabled = false; heapProbe::failAt = -1;
        const auto allocations = heapProbe::calls; const bool injected = heapProbe::fired;
        check(allocations == 0 && !injected, "POD sender has no allocation at which any selected fault ordinal can fire");
        check(g_keyboardSelfSendBatchFailures == 0 && s.logAttempts == 0, "unreached fault is not reported as caught bad_alloc");
        verifySend(s, source, 2, inputs.data(), before, incoming, returned);
        const auto queued = g_keyboardSelfSendBatchQueue.take();
        check(queued.taken && queued.count == 2 && queued.rows[0].contextKnown && queued.rows[1].contextKnown &&
            !queued.rows[0].returned && queued.rows[1].returned && queued.rows[0].callId == queued.rows[1].callId,
            "allocation-free sender retains both typed evidence rows");
        cases.push_back({{"ordinal", ordinal}, {"source", ymcc::keyboardEvents::sourceName(source)},
            {"threadToken", threadToken}, {"allocationCalls", allocations}, {"faultInjected", injected},
            {"noInjectionPoint", true}, {"sendCalls", s.sends}, {"observerCalls", s.observers}});
    }
    report["senderAllocationFaultCases"] = cases;
    report["senderAllocationFaultOrdinals"] = {0, 1, 22, 64, 1170};
    report["realJsonAllocationFaultSites"] = 0;
    report["batchRecordBytes"] = sizeof(BatchRecord);
}
namespace ymcc::selfSendEvidence::detail {
struct QueueTestAccess {
    static void lock(Queue& queue) { queue.mutex_.lock(); }
    static void unlock(Queue& queue) { queue.mutex_.unlock(); }
};
}
// The owner is always a different thread. The two-second timeout is only a
// deadlock escape, and reaching it fails the test; no same-thread relocking.
class QueueOwner {
    std::mutex mutex_; std::condition_variable changed_;
    bool held_ = false, release_ = false, timedOut_ = false;
    std::thread worker_;
public:
    explicit QueueOwner(ymcc::selfSendEvidence::Queue& queue) : worker_([this, &queue] {
        ymcc::selfSendEvidence::detail::QueueTestAccess::lock(queue);
        {
            std::unique_lock<std::mutex> lock(mutex_); held_ = true; changed_.notify_all();
            if (!changed_.wait_for(lock, std::chrono::seconds(2), [this] { return release_; })) timedOut_ = true;
        }
        ymcc::selfSendEvidence::detail::QueueTestAccess::unlock(queue);
    }) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!changed_.wait_for(lock, std::chrono::seconds(2), [this] { return held_; })) {
            release_ = true; changed_.notify_all(); lock.unlock(); worker_.join();
            throw std::runtime_error("test queue owner did not acquire lock");
        }
    }
    bool finish() {
        { std::lock_guard<std::mutex> lock(mutex_); release_ = true; changed_.notify_all(); }
        if (worker_.joinable()) worker_.join();
        return !timedOut_;
    }
    ~QueueOwner() { (void)finish(); }
};
static json queueStats(const ymcc::selfSendEvidence::QueueSnapshot& s) {
    return {{"taken", s.taken}, {"changed", s.changed}, {"count", s.count}, {"revision", s.revision},
        {"droppedContention", s.droppedContention}, {"droppedOverflow", s.droppedOverflow},
        {"recordFailures", s.recordFailures}, {"discardedDisabled", s.discardedDisabled}};
}
static void queueCases() {
    using ymcc::selfSendEvidence::Queue;
    Queue queue; const auto seed = batchRecord(123);
    const auto empty = queue.take();
    check(empty.taken && !empty.changed && empty.count == 0 && !queue.hasRows(), "fresh typed queue empty snapshot");
    for (unsigned i = 0; i < 34; ++i) queue.record(batchRecord(i + 1));
    check(queue.hasRows(), "admitted typed rows visible");
    const auto full = queue.take();
    check(full.taken && full.changed && full.count == 32 && full.droppedOverflow == 2 && full.revision == 34 && !queue.hasRows(), "cap32 drops overflow and retains cumulative loss/revision");
    for (unsigned i = 0; i < 32; ++i) check(full.rows[i].callId == i + 1 &&
        ymcc::selfSendEvidence::toJson(full.rows[i])["callId"] == i + 1, "typed queue FIFO and JSON conversion after take");
    const auto unchanged = queue.take();
    check(unchanged.taken && !unchanged.changed && unchanged.count == 0 && unchanged.droppedOverflow == 2, "take consumes rows/revision notification, never cumulative counters");
    queue.record(seed); queue.record(seed); queue.record(seed); queue.discardDisabled();
    const auto discarded = queue.take();
    check(discarded.changed && discarded.count == 0 && discarded.discardedDisabled == 3 && !queue.hasRows(), "disabled discard count and row clearing");
    queue.discardDisabled(); check(!queue.take().changed, "empty disabled discard leaves counters alone");
    for (unsigned invalid = 0; invalid < 3; ++invalid) {
        auto row = seed; row.request.retain = invalid == 2; row.callId = invalid == 1 ? 1 : 0; queue.record(row);
    }
    const auto invalid = queue.take();
    check(invalid.recordFailures == 3 && invalid.count == 0 && invalid.changed, "missing retain or zero callId rejected and counted");
    heapProbe::calls = 0; heapProbe::fired = false; heapProbe::failAt = 0; heapProbe::enabled = true;
    queue.record(seed); const auto copied = queue.take(); queue.record(seed); queue.discardDisabled();
    heapProbe::enabled = false; heapProbe::failAt = -1;
    const auto allocations = heapProbe::calls;
    check(allocations == 0 && !heapProbe::fired && copied.count == 1 && copied.rows[0].callId == seed.callId &&
        copied.recordFailures == 3 && !queue.hasRows(), "typed record/take/discard allocate nothing; allocation fault has no injection point");
    const auto before = queue.take(); queue.record(seed);
    {
        QueueOwner owner(queue);
        queue.record(seed); const auto contended = queue.take(); queue.discardDisabled();
        check(!contended.taken && contended.count == 0, "contended take does not consume rows");
        check(owner.finish(), "record/take/discard each return while another thread owns queue lock");
    }
    const auto after = queue.take();
    check(after.count == 1 && ymcc::selfSendEvidence::toJson(after.rows[0]) == ymcc::selfSendEvidence::toJson(seed) &&
        after.droppedContention == 3 && after.discardedDisabled == 4 && after.recordFailures == 3 && after.droppedOverflow == 2,
        "contention counted for all three operations; retained typed row/cumulative counters intact");
    report["queue"] = {{"capacity", ymcc::selfSendEvidence::queueCapacity}, {"overflow", queueStats(full)},
        {"discard", queueStats(discarded)}, {"beforeContention", queueStats(before)}, {"afterContention", queueStats(after)},
        {"typedOperationAllocationCalls", allocations}, {"faultInjected", false}};
}
static void flushCases() {
    const auto seed = batchRecord(456);
    resetSequence(); fake::State s; fake::Bind bind(s); s.gate = false;
    const auto error = s.error;
    keyboardSelfSendRecordEvidence(seed);
    check(!g_keyboardSelfSendBatchQueue.hasRows() && s.error == error && s.gates == 1 && s.queries == 0 && s.tickCalls == 0 && s.logAttempts == 0, "closed direct record has no query/admission/sink and preserves error");
    const auto reads = s.errorReads, writes = s.errorWrites;
    heapProbe::calls = 0; heapProbe::enabled = true;
    keyboardSelfSendFlushEvidence(false, 321);
    heapProbe::enabled = false;
    check(s.errorReads == reads && s.errorWrites == writes && heapProbe::calls == 0 && s.logAttempts == 0, "disabled empty flush has no GetLastError/SetLastError/JSON/API work");
    s.gate = true; keyboardSelfSendRecordEvidence(seed); check(g_keyboardSelfSendBatchQueue.hasRows(), "open record admission");
    const auto baseline = g_keyboardSelfSendBatchQueue.take();
    keyboardSelfSendRecordEvidence(seed); keyboardSelfSendRecordEvidence(seed);
    flush(s, false);
    check(!g_keyboardSelfSendBatchQueue.hasRows() && s.logAttempts == 0 && s.queries == 0, "disabled flush discards pending evidence without sink/token calls");
    const auto discarded = g_keyboardSelfSendBatchQueue.take();
    check(discarded.discardedDisabled == baseline.discardedDisabled + 2 && discarded.count == 0, "real flush disabled discard counter");
    flush(s); check(s.logAttempts == 0, "unchanged empty enabled flush emits nothing");
    unsigned failures = 0;
    for (unsigned sink = 1; sink <= 6; ++sink) {
        resetSequence(); fake::State f; fake::Bind fbind(f); auto inputs = altBatch(); const auto before = inputs;
        const auto incoming = f.error; const auto returned = keyboardSelfSendWithEvidence(Source::FocusAltFallback, 2, inputs.data());
        check(f.logAttempts == 0 && f.sends == 1, "sink fault cannot be reached before explicit drain");
        f.throwLog = sink; flush(f);
        check(f.logAttempts == sink && g_keyboardSelfSendBatchFailures == 1 && !g_keyboardSelfSendBatchQueue.hasRows(), "drain sink exception contained/counted after queue released");
        verifySend(f, Source::FocusAltFallback, 2, inputs.data(), before, incoming, returned); ++failures;
    }
    // Contention and a full queue lose diagnostics, never the original action.
    for (bool full : {false, true}) {
        resetSequence(); fake::State f; fake::Bind fbind(f); auto inputs = altBatch(); const auto before = inputs;
        const auto starting = g_keyboardSelfSendBatchQueue.take();
        if (full) for (unsigned i = 0; i < 32; ++i) g_keyboardSelfSendBatchQueue.record(seed);
        const auto incoming = f.error; UINT returned = 0;
        const auto send = [&] {
            heapProbe::calls = 0; heapProbe::enabled = true;
            const auto value = keyboardSelfSendWithEvidence(Source::FocusAltFallback, 2, inputs.data());
            heapProbe::enabled = false; return value;
        };
        if (full) returned = send();
        else {
            QueueOwner owner(g_keyboardSelfSendBatchQueue);
            returned = send();
            check(owner.finish(), "sender completes while another thread holds diagnostic queue lock");
        }
        check(heapProbe::calls == 0, "full/contended typed queue sender still allocates nothing");
        verifySend(f, Source::FocusAltFallback, 2, inputs.data(), before, incoming, returned);
        const auto ending = g_keyboardSelfSendBatchQueue.take();
        check(f.logAttempts == 0 && g_keyboardSelfSendBatchFailures == 0, "queue loss causes no sender sink or construction failure");
        check(full ? ending.count == 32 && ending.droppedOverflow == starting.droppedOverflow + 2 :
            ending.count == 0 && ending.droppedContention == starting.droppedContention + 2, "both pre/post admission losses counted exactly");
    }
    {
        resetSequence(); fake::State f; fake::Bind fbind(f); auto inputs = altBatch(); const auto before = inputs;
        const auto incoming = f.error;
        const auto returned = keyboardSelfSendWithEvidence(Source::FocusAltFallback, 2, inputs.data());
        check(f.logAttempts == 0 && f.sends == 1, "sender finishes before drain JSON allocation-failure injection");
        heapProbe::calls = 0; heapProbe::fired = false; heapProbe::failAt = 0; heapProbe::enabled = true;
        f.sinkAllowed = true; keyboardSelfSendFlushEvidence(true, 900001); f.sinkAllowed = false;
        heapProbe::enabled = false; heapProbe::failAt = -1;
        check(heapProbe::fired && f.logAttempts == 0 && g_keyboardSelfSendBatchFailures == 1 &&
            !g_keyboardSelfSendBatchQueue.hasRows(), "first drain JSON construction bad_alloc contained after take");
        verifySend(f, Source::FocusAltFallback, 2, inputs.data(), before, incoming, returned);
    }
    report["drainSinkExceptionScenarios"] = failures;
    report["drainJsonConstructionFaultScenarios"] = 1;
    report["senderQueueLossScenarios"] = 2;
}
static void concurrencyCases() {
    resetSequence(); fake::ConcurrentGate gate; std::array<fake::State, 2> states;
    const auto baseline = g_keyboardSelfSendBatchQueue.take();
    std::array<std::vector<INPUT>, 2> inputs{altBatch(), {key(VK_LSHIFT, 0, 0x5678), key(VK_LSHIFT, KEYEVENTF_KEYUP, 0x8765)}};
    const auto before = inputs; std::array<std::exception_ptr, 2> errors{}; std::array<UINT, 2> returned{};
    std::array<std::thread, 2> workers; std::array<std::size_t, 2> allocations{};
    for (std::size_t i = 0; i < 2; ++i) {
        states[i].concurrent = &gate; states[i].tid += static_cast<DWORD>(i); states[i].sendError += static_cast<DWORD>(i);
        workers[i] = std::thread([&, i] {
            try {
                fake::Bind bind(states[i]); heapProbe::calls = 0; heapProbe::enabled = true;
                returned[i] = keyboardSelfSendWithEvidence(Source::NativeKeyboardShortcut, 2, inputs[i].data());
                heapProbe::enabled = false; allocations[i] = heapProbe::calls;
            } catch (...) { heapProbe::enabled = false; errors[i] = std::current_exception(); }
        });
    }
    for (auto& worker : workers) worker.join();
    for (const auto& error : errors) if (error) std::rethrow_exception(error);
    check(gate.arrived == 2 && gate.peak == 2 && gate.timeouts == 0, "two normal calls overlap inside SendInput substitute; no Busy loss/global send mutex");
    check(g_keyboardSelfSendBatchSequence == 2 && g_keyboardSelfSendBatchFailures == 0, "two concurrent IDs and no construction failure");
    const auto queued = g_keyboardSelfSendBatchQueue.take();
    const auto dropped = queued.droppedContention - baseline.droppedContention;
    check(queued.taken && queued.count + dropped == 4 && queued.droppedOverflow == baseline.droppedOverflow &&
        queued.recordFailures == baseline.recordFailures, "concurrent pre/post evidence either retained or exactly counted as contention loss");
    json retained = json::array();
    std::array<std::set<std::string>, 2> phases;
    for (std::size_t i = 0; i < queued.count; ++i) {
        const auto row = ymcc::selfSendEvidence::toJson(queued.rows[i]); retained.push_back(row);
        const auto tid = row["callerContextBeforeSend"]["threadId"].get<DWORD>();
        const std::size_t index = tid == states[0].tid ? 0 : 1;
        check(tid == states[index].tid && row["source"] == ymcc::keyboardEvents::sourceName(Source::NativeKeyboardShortcut) &&
            row["requestedInputs"] == 2, "retained concurrent evidence has exact caller/source/count");
        check(row["callId"] == 1 || row["callId"] == 2, "retained concurrent callId in sequence");
        check(phases[index].insert(row["phase"].get<std::string>()).second, "no duplicate phase per concurrent caller");
        for (std::size_t j = 0; j < i; ++j) {
            const auto previous = ymcc::selfSendEvidence::toJson(queued.rows[j]);
            const bool sameCaller = previous["callerContextBeforeSend"]["threadId"] == row["callerContextBeforeSend"]["threadId"];
            check((previous["callId"] == row["callId"]) == sameCaller, "same caller keeps batch ID and distinct callers never share it");
        }
    }
    for (std::size_t i = 0; i < 2; ++i) {
        verifySend(states[i], Source::NativeKeyboardShortcut, 2, inputs[i].data(), before[i], 0x1234, returned[i]);
        check(states[i].logAttempts == 0 && states[i].gates == 3 && allocations[i] == 0, "concurrent sender only records POD to memory and allocates nothing");
    }
    report["concurrency"] = {{"sendCalls", gate.arrived}, {"peakInsideSendInput", gate.peak}, {"rendezvousTimeouts", gate.timeouts},
        {"retainedRows", retained}, {"senderAllocationCalls", allocations}, {"droppedContentionDelta", dropped}, {"snapshot", queueStats(queued)}};
}
static int allocationProbe(const char* ordinalText) {
    const auto ordinal = static_cast<std::size_t>(std::strtoull(ordinalText, nullptr, 10));
    resetSequence(); fake::State s; fake::Bind bind(s); auto inputs = altBatch();
    std::set_terminate([] {
        heapProbe::enabled = false;
        bool badAlloc = false;
        try { if (const auto e = std::current_exception()) std::rethrow_exception(e); }
        catch (const std::bad_alloc&) { badAlloc = true; }
        catch (...) {}
        const auto& current = fake::state();
        std::printf("{\"pass\":false,\"nonRecoverableTermination\":true,\"badAlloc\":%s,\"sendCalls\":%u,\"observerCalls\":%u,\"allocationCalls\":%zu}\n",
            badAlloc ? "true" : "false", current.sends, current.observers, heapProbe::calls);
        std::fflush(stdout); std::_Exit(97);
    });
    const auto before = inputs; const auto incoming = s.error;
    heapProbe::calls = 0; heapProbe::fired = false; heapProbe::failAt = static_cast<std::ptrdiff_t>(ordinal); heapProbe::enabled = true;
    const auto returned = keyboardSelfSendWithEvidence(Source::FocusAltFallback, 2, inputs.data());
    heapProbe::enabled = false; heapProbe::failAt = -1;
    const auto allocations = heapProbe::calls; const bool injected = heapProbe::fired;
    verifySend(s, Source::FocusAltFallback, 2, inputs.data(), before, incoming, returned);
    const bool okay = allocations == 0 && !injected && g_keyboardSelfSendBatchFailures == 0 && s.logAttempts == 0;
    std::cout << json({{"pass", okay}, {"faultInjected", injected}, {"noInjectionPoint", allocations == 0 && !injected},
        {"allocationCalls", allocations}, {"requestedOrdinal", ordinal}, {"sendCalls", s.sends},
        {"observerCalls", s.observers}, {"returned", returned},
        {"reason", "R3 sender must have zero allocations; an unreached ordinal is not a caught exception"}}).dump() << '\n';
    return okay ? 0 : 1;
}
int main(int argc, char** argv) {
    if (argc == 2 && std::strncmp(argv[1], "--allocation-fault=", 19) == 0) return allocationProbe(argv[1] + 19);
    try {
        if (argc == 2 && std::strcmp(argv[1], "--core-only") == 0) {
            requestCases(false); contextCases(); tokenCases();
            report["pass"] = true; report["coreOnly"] = true; report["checks"] = fake::checks.load();
            report["realOsInputOrTokenCalls"] = false; std::cout << report.dump(2) << '\n'; return 0;
        }
        std::cerr << "normal cases\n"; normalCases();
        std::cerr << "request cases\n"; requestCases();
        std::cerr << "context cases\n"; contextCases();
        std::cerr << "token cases\n"; tokenCases();
        std::cerr << "wrapper token cases\n"; wrapperTokenCases();
        std::cerr << "exception cases\n"; exceptionCases();
        std::cerr << "zero allocation cases\n"; zeroAllocationCases();
        std::cerr << "queue cases\n"; queueCases();
        std::cerr << "flush cases\n"; flushCases();
        std::cerr << "concurrency cases\n"; concurrencyCases();
        report["pass"] = true; report["checks"] = fake::checks.load(); report["inputSize"] = sizeof(INPUT);
        report["realOsInputOrTokenCalls"] = false;
        std::cout << report.dump(2) << '\n'; return 0;
    } catch (const std::exception& error) {
        heapProbe::enabled = false; heapProbe::failAt = -1;
        report["error"] = error.what(); report["checks"] = fake::checks.load();
        std::cout << report.dump(2) << '\n'; std::cerr << error.what() << '\n'; return 1;
    }
}
