#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"

static bool watchAllocations = false;
static std::size_t allocations = 0;
void* operator new(std::size_t size) {
    if (watchAllocations) ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// An actual WindowsOps call would be an unresolved link error. Only FakeOps runs.
namespace forbidden_os {
DWORD error(); void setError(DWORD); ULONGLONG tick(); DWORD thread(); HWND foreground();
DWORD pid(HWND, LPDWORD); SHORT async(int); BOOL keyboard(PBYTE);
}
#define GetLastError forbidden_os::error
#define SetLastError forbidden_os::setError
#define GetTickCount64 forbidden_os::tick
#define GetCurrentThreadId forbidden_os::thread
#define GetForegroundWindow forbidden_os::foreground
#define GetWindowThreadProcessId forbidden_os::pid
#define GetAsyncKeyState forbidden_os::async
#define GetKeyboardState forbidden_os::keyboard
#include "../native/input_signal_key_state_evidence.h"
#include "../native/input_edge_evidence.h"

namespace ke = ymcc::inputKeyStateEvidence;
namespace ia = ymcc::inputActivity;
using json = nlohmann::json;
namespace fixture {
static unsigned checks = 0;
static json cases = json::array();
static void check(bool v, const char* label) { ++checks; if (!v) throw std::runtime_error(label); }
struct FakeOps {
    DWORD lastError = 777, queueFailure = 5;
    ULONGLONG clock = 1000;
    bool queueOk = true, pidOk = true, nullForeground = false, changingForeground = false;
    unsigned calls = 0, asyncCalls = 0, queueCalls = 0, tickCalls = 0, threadCalls = 0, foregroundCalls = 0, pidCalls = 0;
    std::array<std::uint16_t, 256> async{};
    std::array<BYTE, 256> queue{};
    std::array<int, ke::candidateCapacity> queried{};
    bool invalidQuery = false;
    DWORD error() noexcept { ++calls; return lastError; }
    void setError(DWORD value) noexcept { ++calls; lastError = value; }
    ULONGLONG now() noexcept { ++calls; ++tickCalls; lastError = 101; clock += 5; return clock; }
    DWORD threadId() noexcept { ++calls; ++threadCalls; lastError = 102; return 42; }
    HWND foreground() noexcept {
        ++calls; ++foregroundCalls; lastError = 103;
        if (nullForeground) return nullptr;
        return reinterpret_cast<HWND>(std::uintptr_t(changingForeground && foregroundCalls == 2 ? 2 : 1));
    }
    bool foregroundPid(HWND window, DWORD& pid) noexcept {
        ++calls; ++pidCalls;
        if (!pidOk) { lastError = 19; return false; }
        lastError = 104; pid = reinterpret_cast<std::uintptr_t>(window) == 1 ? 111u : 222u; return true;
    }
    SHORT asyncKey(int vk) noexcept {
        ++calls; lastError = 105;
        if (asyncCalls >= queried.size() || vk < 1 || vk > 255) { invalidQuery = true; return 0; }
        queried[asyncCalls++] = vk; return std::bit_cast<SHORT>(async[static_cast<std::size_t>(vk)]);
    }
    bool keyboardState(BYTE* out) noexcept {
        ++calls; ++queueCalls;
        if (!queueOk) { lastError = queueFailure; return false; }
        for (std::size_t i = 0; i < queue.size(); ++i) out[i] = queue[i]; lastError = 106; return true;
    }
};
static ia::Snapshot empty() { ia::Snapshot s{}; s.enabled = true; s.drained = true; return s; }
static ia::Row key(std::uint16_t vk, std::uint64_t tick, bool held = false, ia::Channel channel = ia::Channel::LowLevelKeyboard, bool extended = false) {
    ia::Row row{}; row.kind = ia::Kind::Keyboard; row.channel = channel; row.code = vk; row.extended = extended;
    row.eventCount = 2; row.downCount = 1; row.upCount = 1; row.lastEventTick = tick;
    if (ia::isSelfChannel(channel)) row.selfHeldAssumption = held; else row.observedHeld = held;
    return row;
}
static void append(ia::Snapshot& s, ia::Row row) { if (s.count >= s.rows.size()) throw std::runtime_error("fixture overflow"); s.rows[s.count++] = row; s.totalRows = s.count; }
static const ke::Candidate* find(const ke::Selection& s, unsigned vk) {
    for (std::size_t i = 0; i < s.count; ++i) if (s.candidates[i].virtualKey == vk) return &s.candidates[i];
    return nullptr;
}
static const json& jsonKey(const json& value, unsigned vk) {
    for (const auto& row : value["keys"]) if (row["virtualKey"] == vk) return row;
    throw std::runtime_error("JSON key missing");
}
static void bounded(const FakeOps& ops, std::size_t count) {
    check(!ops.invalidQuery && ops.asyncCalls == count && ops.asyncCalls <= 16, "async reads exceed selected key bound");
    check(ops.queueCalls == (count ? 1u : 0u), "not exactly one queue read for nonempty capture");
    check(ops.tickCalls == (count ? 2u : 0u) && ops.threadCalls == (count ? 1u : 0u), "capture tick/thread bound");
    check(ops.foregroundCalls == (count ? 2u : 0u) && ops.pidCalls <= 2, "foreground reads exceed bound");
}
static void emptyDisabled() {
    auto s = empty(); append(s, key(VK_RETURN, 1, true));
    FakeOps disabled; const auto a = ke::capture(false, s, disabled);
    check(!a.selectionPerformed && !a.sampled && disabled.calls == 0 && disabled.lastError == 777, "detailed gate off called Ops");
    s.enabled = false; FakeOps inactive; const auto b = ke::capture(true, s, inactive);
    check(!b.sampled && inactive.calls == 0 && inactive.lastError == 777, "disabled upstream activity called Ops");
    FakeOps noRows; const auto c = ke::capture(true, empty(), noRows);
    check(c.selectionPerformed && !c.sampled && noRows.calls == 0 && noRows.lastError == 777, "empty candidate set called Ops");
    const auto j = ke::toJson(c);
    check(j["captureBeginTick"].is_null() && j["callingThreadId"].is_null() && j["callingThreadQueueReadOk"].is_null() && j["keys"].empty(), "unsampled values fabricated zeros/state");
}
static void heldPriorityAndRecentReleased() {
    auto s = empty();
    for (unsigned i = 0; i < 16; ++i) append(s, key(static_cast<std::uint16_t>(0x40 + i), 1000 + i));
    auto held = key(VK_RETURN, 1, true); held.eventCount = 0; append(s, held);
    auto assumed = key(0x60, 2, true, ia::Channel::SelfSendInputReturnedPrefix); assumed.eventCount = 0; append(s, assumed);
    FakeOps ops; const auto c = ke::capture(true, s, ops); const auto& selected = c.selection;
    check(selected.count == 16 && selected.uniqueEligibleKeys == 18 && selected.omittedKeys == 2 && selected.omittedHeldKeys == 0, "16-key selection/omission counts");
    check(selected.candidates[0].virtualKey == 0x60 && selected.candidates[1].virtualKey == VK_RETURN, "held callbacks/self assumptions lost to recent released activity");
    check(selected.candidates[2].virtualKey == 0x4f && selected.candidates[15].virtualKey == 0x42 && !find(selected, 0x40) && !find(selected, 0x41), "released candidates not chosen by latest retained tick");
    check(selected.omittedEligibleRows == 2 && selected.eligibleRows == 18, "eligible row accounting"); bounded(ops, 16);
}
static void dedupAndReasonCounts() {
    auto s = empty(); append(s, key(VK_RETURN, 10, true)); append(s, key(VK_RETURN, 20, false, ia::Channel::RawKeyboard, true));
    append(s, key(VK_RETURN, 30, true, ia::Channel::SelfSendInputRequest));
    append(s, key(VK_RETURN, 40, false, ia::Channel::LowLevelKeyboard, true));
    append(s, key(VK_CONTROL, 5)); append(s, key(VK_LCONTROL, 5));
    FakeOps ops; const auto c = ke::capture(true, s, ops); const auto* enter = find(c.selection, VK_RETURN);
    check(c.selection.count == 3 && c.selection.eligibleRows == 6 && enter, "dedup by literal VK failed");
    check(enter->contributingRows == 4 && enter->extendedRows == 2 && enter->genericRows == 2 && enter->callbackHeldRows == 1 && enter->selfHeldAssumptionRows == 1, "dedup lost row/held namespaces");
    check(enter->reasonBits == (ke::CallbackHeld | ke::SelfHeldAssumption | ke::RecentActivity | ke::KeyboardRow) && enter->lastActivityTick == 40, "reason bits/max retained tick lost");
    check(find(c.selection, VK_CONTROL) && find(c.selection, VK_LCONTROL), "generic/side modifier VK was converted"); bounded(ops, 3);
}
static void heldOverflowAndUpstreamOmissions() {
    auto s = empty(); for (unsigned i = 0; i < 20; ++i) append(s, key(static_cast<std::uint16_t>(0x40 + i), i, true));
    append(s, key(0x40, 0, true, ia::Channel::RawKeyboard, true));
    s.totalRows = 30; s.omittedRows = 9; s.omittedActiveRows = 6; s.omittedHeldRows = 4; s.heldStateMayHaveGaps = true;
    s.countersSaturated = true; s.contentionCounterWrapped = true; s.droppedContention = 11; s.droppedOverflow = 12;
    FakeOps ops; const auto c = ke::capture(true, s, ops); const auto j = ke::toJson(c);
    check(c.selection.omittedKeys == 4 && c.selection.omittedHeldKeys == 4 && c.selection.omittedEligibleRows == 5 && c.selection.count == 16, "overflow hid held key/duplicate-row omissions");
    check(j["upstreamTotalRows"] == 30 && j["upstreamOmittedRows"] == 9 && j["upstreamOmittedActiveRows"] == 6 && j["upstreamOmittedHeldRows"] == 4, "upstream row omissions lost");
    check(j["upstreamHeldStateMayHaveGaps"] == true && j["upstreamCountersSaturated"] == true && j["upstreamContentionCounterWrapped"] == true && j["upstreamDroppedContention"] == 11 && j["upstreamDroppedOverflow"] == 12, "upstream gaps/drop limits lost"); bounded(ops, 16);
}
static void mouseMappingAndLiteralDedup() {
    auto s = empty();
    constexpr std::array<unsigned, 5> expected{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    for (unsigned i = 1; i <= 5; ++i) { auto row = key(static_cast<std::uint16_t>(i), 1); row.kind = ia::Kind::MouseButton; row.channel = ia::Channel::MouseHook; append(s, row); }
    append(s, key(VK_LBUTTON, 2)); FakeOps ops; const auto c = ke::capture(true, s, ops);
    check(c.selection.count == 5 && c.selection.eligibleRows == 6, "mouse mapping/dedup count");
    for (const auto vk : expected) { const auto* row = find(c.selection, vk); check(row && (row->reasonBits & ke::MouseButtonRow), "standard mouse button VK mapping"); }
    const auto* left = find(c.selection, VK_LBUTTON);
    check(left && left->contributingRows == 2 && (left->reasonBits & ke::KeyboardRow), "keyboard/mouse row collision sampled twice or lost reasons"); bounded(ops, 5);
    const auto j = ke::toJson(c);
    check(j["mouseAsyncInterpretationVerified"] == false && j["mouseLogicalButtonMappingVerified"] == false && j["mouseButtonSwapObserved"] == false, "mouse async samples claim physical/logical mapping or observed swap configuration");
    check(j["mouseAsyncScope"].get<std::string>().find("may differ from logical button mapping") != std::string::npos && j["mouseAsyncScope"].get<std::string>().find("unobserved") != std::string::npos, "mouse async and swap interpretation limit not explicit");
}
static void exclusionsNoConversion() {
    auto s = empty();
    auto scan = key(VK_RETURN, 1, true, ia::Channel::SelfSendInputRequest); scan.kind = ia::Kind::KeyboardScanCode; append(s, scan);
    scan.extended = true; append(s, scan);
    for (const auto kind : {ia::Kind::MouseMove,ia::Kind::WheelVertical,ia::Kind::WheelHorizontal}) { auto r = key(0,1); r.kind = kind; append(s,r); }
    for (const unsigned code : {0u,256u,65535u}) append(s,key(static_cast<std::uint16_t>(code),1,true));
    for (const unsigned code : {0u,6u}) { auto r = key(static_cast<std::uint16_t>(code),1,true); r.kind = ia::Kind::MouseButton; append(s,r); }
    auto extendedMouse = key(1,1,true,ia::Channel::MouseHook,true); extendedMouse.kind = ia::Kind::MouseButton; append(s,extendedMouse);
    auto inactive = key(65,1); inactive.eventCount = 0; append(s,inactive);
    auto unknown = key(65,1); unknown.kind = static_cast<ia::Kind>(255); append(s,unknown);
    FakeOps ops; const auto c = ke::capture(true,s,ops);
    check(!c.sampled && ops.calls == 0 && c.selection.count == 0, "excluded scan/mouse rows caused API reads");
    check(c.selection.scanCodeRowsExcluded == 2 && c.selection.nonButtonMouseRowsExcluded == 3 && c.selection.invalidVirtualKeyRows == 3 && c.selection.invalidMouseButtonRows == 3 && c.selection.inactiveRows == 1 && c.selection.otherKindRowsExcluded == 1, "exclusion categories not explicit");
    check(ke::toJson(c)["scanCodeRowsExcluded"] == 2, "scan exclusion not serialized");
}
static void asyncHighBitAndQueueSeparation() {
    auto s = empty(); for (const unsigned code : {65u,66u,67u}) append(s,key(static_cast<std::uint16_t>(code),1));
    FakeOps ops; ops.async[65] = 1; ops.async[66] = 0x8000; ops.async[67] = 0x8001; ops.queue[65] = 0x80; ops.queue[66] = 1;
    const auto c = ke::capture(true,s,ops); const auto j = ke::toJson(c);
    check(!find(c.selection,65)->asyncDownBit && find(c.selection,66)->asyncDownBit && find(c.selection,67)->asyncDownBit, "async low bit treated as down/edge");
    check(jsonKey(j,65)["callingThreadQueueDown"] == true && jsonKey(j,66)["callingThreadQueueDown"] == false && jsonKey(j,65)["asyncDownBit"] == false, "async and calling-thread queue states merged");
    check(ops.queried[0] == 65 && ops.queried[1] == 66 && ops.queried[2] == 67 && ops.lastError == 777, "deterministic VK tie order or last error restoration");
    check(c.beginTick == 1005 && c.endTick == 1010 && c.callingThreadId == 42, "capture time/thread attribution"); bounded(ops,3);
}
static void queueFailureLastError() {
    auto s = empty(); append(s,key(VK_RETURN,1,true)); FakeOps ops; ops.queueOk = false; ops.queueFailure = 998; ops.lastError = 123456; ops.async[VK_RETURN] = 0x8000;
    const auto c = ke::capture(true,s,ops); const auto j = ke::toJson(c);
    check(!c.queueReadOk && c.queueError == 998 && ops.lastError == 123456, "queue failure/error restoration");
    check(j["callingThreadQueueReadOk"] == false && j["callingThreadQueueError"] == 998 && jsonKey(j,VK_RETURN)["callingThreadQueueDown"].is_null(), "queue failure was serialized as key-up");
    check(jsonKey(j,VK_RETURN)["asyncDownBit"] == true, "queue failure erased async observation"); bounded(ops,1);
}
static void foregroundContextLimits() {
    auto s = empty(); append(s,key(VK_RETURN,1));
    FakeOps changing; changing.changingForeground = true; const auto a = ke::capture(true,s,changing); const auto ja = ke::toJson(a);
    check(ja["foregroundStableDuringCapture"] == false && ja["foregroundBefore"]["pid"] == 111 && ja["foregroundAfter"]["pid"] == 222 && changing.lastError == 777, "foreground change hidden or error corrupted");
    FakeOps failure; failure.pidOk = false; const auto b = ke::capture(true,s,failure); const auto jb = ke::toJson(b);
    check(jb["foregroundStableDuringCapture"].is_null() && jb["foregroundBefore"]["pid"].is_null() && jb["foregroundAfter"]["pidReadError"] == 19 && failure.lastError == 777, "PID failure fabricated stable foreground");
    FakeOps absent; absent.nullForeground = true; const auto d = ke::capture(true,s,absent); const auto jd = ke::toJson(d);
    check(absent.pidCalls == 0 && jd["foregroundStableDuringCapture"].is_null() && jd["foregroundBefore"]["windowPresent"] == false && absent.lastError == 777, "no-foreground case was fabricated as PID zero proof");
}
static void boundedMalformedCount() {
    auto s = empty(); for (unsigned i = 0; i < ia::capacity; ++i) append(s,key(static_cast<std::uint16_t>(i+32),i));
    s.count = ia::capacity+7; FakeOps ops; const auto c = ke::capture(true,s,ops);
    check(c.selection.inputRowCount == 71 && c.selection.inspectedRows == 64 && c.selection.uninspectedRetainedRows == 7 && c.selection.uniqueEligibleKeys == 64, "malformed row count crossed fixed array bound");
    check(c.selection.count == 16 && c.selection.omittedKeys == 48 && c.selection.omittedEligibleRows == 48, "64-row/16-key omission accounting"); bounded(ops,16);
}
static void allocationFreeBeforeSerialization() {
    auto s = empty(); for (unsigned i=0;i<64;++i) append(s,key(static_cast<std::uint16_t>(32+i),i,i%2==0));
    FakeOps ops; allocations=0; watchAllocations=true;
    const auto selected=ke::select(s); const auto c=ke::capture(true,s,ops);
    watchAllocations=false;
    check(allocations==0 && selected.count==16 && c.sampled, "select/capture allocates outside caller serialization");
    allocations=0; watchAllocations=true; const auto j=ke::toJson(c); watchAllocations=false;
    check(allocations>0 && j["keys"].size()==16, "allocation monitor/explicit toJson drain boundary"); bounded(ops,16);
}
static void noProofClaimsOrIdentityRetention() {
    auto s=empty(); auto row=key(VK_RETURN,1,true); row.lastSourceId=0xfeedbeef; row.lastDeviceId=0xdeadbeef; append(s,row);
    FakeOps ops; const auto j=ke::toJson(ke::capture(true,s,ops));
    for (const char* field : {"asyncZeroIsAbsenceProof","callingThreadQueueIsGameState","crossKeyAtomicSnapshot","eventTimeStateProven","eventEdgesCaptured","sourceIdentityKnown","physicalOrInjectedOriginKnown","gameKeyboardReceiptProven","keyReleaseProven","automaticReleaseProven","completeKeyCoverageProven"}) check(j[field]==false,"diagnostics claim more than bounded observations");
    check(j["capturePhase"]=="existing-activity-drain" && j["candidateCapacity"]==16 && j["retainedRowCapacity"]==64,"drain-time scope and hard bounds");
    const auto text=j.dump(); check(text.find("lastSourceId")==std::string::npos && text.find("lastDeviceId")==std::string::npos && text.find("4276993775")==std::string::npos && text.find("3735928559")==std::string::npos,"row source/device identity retained as sample attribution");
}
static void injectedGamepadHoldEvidence() {
    auto s=empty();auto row=key(198,14079984,true);row.hasEdge=true;row.lastEdgeTick=14079984;row.lastSourceId=19;
    append(s,row);FakeOps initial;initial.clock=14083338;initial.async[198]=0x8000;
    auto j=ke::toJson(ke::capture(true,s,initial));const auto& shortHold=jsonKey(j,198)["injectedGamepadHold"];
    check(shortHold["observedAgeMs"]==3359 && shortHold["longHeldCandidate"]==false,"first actual C6 retained edge falsely classified as long hold");bounded(initial,1);
    FakeOps later;later.clock=15522370;later.async[198]=0x8000;
    j=ke::toJson(ke::capture(true,s,later));const auto& longHold=jsonKey(j,198)["injectedGamepadHold"];
    check(longHold["observedAgeMs"]==1442391 && longHold["longHeldCandidate"]==true && j["longHeldInjectedGamepadKeys"]==1,"actual C6 long-held record not recognized");
    check(longHold["stuckKeyProven"]==false && longHold["senderKnown"]==false && j["automaticReleaseProven"]==false,"candidate becomes sender/stuck verdict or automatic release");bounded(later,1);
    FakeOps zero;zero.clock=15522370;j=ke::toJson(ke::capture(true,s,zero));check(jsonKey(j,198)["injectedGamepadHold"]["longHeldCandidate"]==false,"zero async value falsely treated as continued hold");
    FakeOps backwards;backwards.async[198]=0x8000;j=ke::toJson(ke::capture(true,s,backwards));check(jsonKey(j,198)["injectedGamepadHold"]["observedAgeMs"].is_null(),"future edge or boot mismatch underflows age");
    row.observedHeld=false;s.rows[0]=row;FakeOps released;released.clock=15522370;j=ke::toJson(ke::capture(true,s,released));check(!jsonKey(j,198).contains("injectedGamepadHold")&&j["longHeldInjectedGamepadKeys"]==0,"released row retains a held injection candidate");
    row.observedHeld=true;row.sourceMixed=true;s.rows[0]=row;FakeOps mixed;j=ke::toJson(ke::capture(true,s,mixed));check(!jsonKey(j,198).contains("injectedGamepadHold"),"mixed sources treated as a stable injected route");
    row.sourceMixed=false;row.sourceUnknown=true;s.rows[0]=row;FakeOps unknown;j=ke::toJson(ke::capture(true,s,unknown));check(!jsonKey(j,198).contains("injectedGamepadHold"),"unknown source classified as known injected route");
    row.sourceUnknown=false;row.lastSourceId=1;s.rows[0]=row;FakeOps physical;j=ke::toJson(ke::capture(true,s,physical));check(!jsonKey(j,198).contains("injectedGamepadHold"),"physical route becomes injected candidate");
    row.lastSourceId=19;row.code='A';s.rows[0]=row;FakeOps ordinary;j=ke::toJson(ke::capture(true,s,ordinary));check(!jsonKey(j,'A').contains("injectedGamepadHold"),"ordinary typing gets gamepad classifier");
    row.code=198;row.channel=ia::Channel::RawKeyboard;s.rows[0]=row;FakeOps raw;j=ke::toJson(ke::capture(true,s,raw));check(!jsonKey(j,198).contains("injectedGamepadHold"),"raw flags interpreted as hook injection flags");
}
struct EdgeForegroundOps {
    unsigned calls=0,pidCalls=0;bool nullWindow=false,fail=false,change=false;
    std::uintptr_t foreground() noexcept {++calls;return nullWindow?0:change&&calls==2?12:11;}
    bool foregroundPid(std::uintptr_t,std::uint32_t& pid,std::uint32_t& error) noexcept {++pidCalls;pid=42;error=fail?5:0;return !fail;}
};
static void injectedGamepadEventContext() {
    namespace ie=ymcc::inputEdges;
    ie::Event e{};e.channel=ie::Channel::HookKeyboard;e.code=198;e.flags=16;e.down=true;
    EdgeForegroundOps ops;ie::captureInjectedGamepadForeground(e,ops);
    check(e.foreground.attempted&&e.foreground.pidKnown&&e.foreground.pid==42&&e.foreground.before==11&&e.foreground.after==11,"injected C6 callback foreground missing");
    check(ops.calls==2&&ops.pidCalls==1&&e.flags==16&&e.code==198&&e.down,"context changes supplied key event or adds unbounded reads");
    e.foreground={};e.down=false;e.flags=144;EdgeForegroundOps up;ie::captureInjectedGamepadForeground(e,up);check(e.foreground.attempted,"injected C6 keyup loses event context");
    for(const auto channel:{ie::Channel::RawKeyboard,ie::Channel::HookMouse,ie::Channel::SelfSend}){auto other=e;other.channel=channel;other.foreground={};EdgeForegroundOps none;ie::captureInjectedGamepadForeground(other,none);check(none.calls==0&&!other.foreground.attempted,"non-hook channel queries foreground");}
    for(const auto keyCode:{65u,0xc2u,0xdbu,256u}){auto other=e;other.code=keyCode;other.foreground={};EdgeForegroundOps none;ie::captureInjectedGamepadForeground(other,none);check(none.calls==0&&!other.foreground.attempted,"ordinary/non-gamepad key queries foreground");}
    auto normal=e;normal.flags=0;normal.foreground={};EdgeForegroundOps none;ie::captureInjectedGamepadForeground(normal,none);check(none.calls==0,"physical gamepad VK queries injected context");
    auto bad=e;bad.foreground={};EdgeForegroundOps failure;failure.fail=true;ie::captureInjectedGamepadForeground(bad,failure);check(!bad.foreground.pidKnown&&bad.foreground.error==5,"failed foreground PID becomes known sender");
    bad.foreground={};EdgeForegroundOps missing;missing.nullWindow=true;ie::captureInjectedGamepadForeground(bad,missing);check(!bad.foreground.pidKnown&&missing.pidCalls==0,"null foreground queries/claims PID");
    bad.foreground={};EdgeForegroundOps changed;changed.change=true;ie::captureInjectedGamepadForeground(bad,changed);check(bad.foreground.before!=bad.foreground.after,"window race hidden");
    check(ie::capacity==256&&sizeof(ie::Event)<=160,"existing recorder storage bound unexpectedly expanded");
}
static void run(const char* name,void(*test)()) {
    const auto before=checks;
    try { test(); cases.push_back({{"case",name},{"pass",true},{"checks",checks-before}}); }
    catch(const std::exception& error) { watchAllocations=false; cases.push_back({{"case",name},{"pass",false},{"checks",checks-before},{"error",error.what()}}); }
}
}
int main() {
    using namespace fixture;
    run("empty-and-both-disabled-gates-zero-ops",emptyDisabled);
    run("held-priority-then-recent-released-16-limit",heldPriorityAndRecentReleased);
    run("literal-vk-dedup-and-retained-reason-counts",dedupAndReasonCounts);
    run("held-overflow-and-upstream-omissions",heldOverflowAndUpstreamOmissions);
    run("five-mouse-vk-mappings-and-keyboard-collision",mouseMappingAndLiteralDedup);
    run("scan-mouse-invalid-inactive-exclusions-zero-ops",exclusionsNoConversion);
    run("async-high-bit-only-and-queue-separation",asyncHighBitAndQueueSeparation);
    run("queue-failure-unknown-and-last-error-restored",queueFailureLastError);
    run("foreground-change-failure-and-unknown-context",foregroundContextLimits);
    run("malformed-input-count-bounded-by-64",boundedMalformedCount);
    run("allocation-free-select-capture-to-json-drain-only",allocationFreeBeforeSerialization);
    run("all-proof-limits-and-no-row-identity-retention",noProofClaimsOrIdentityRetention);
    run("actual-C6-held-age-and-nonattribution-boundaries",injectedGamepadHoldEvidence);
    run("injected-gamepad-callback-context-and-zero-ops-exclusions",injectedGamepadEventContext);
    bool pass=true; for(const auto& row:cases) if(!row["pass"].get<bool>())pass=false;
    std::cout<<json({{"pass",pass},{"checks",checks},{"cases",cases},{"actualOSInput",false},{"productExecuted",false},{"allOpsInMemory",true},{"windowsOpsInvoked",false},{"selectionAllocations",0},{"candidateCapacity",16},{"retainedRowCapacity",64}}).dump(2)<<'\n';
    return pass?0:1;
}
