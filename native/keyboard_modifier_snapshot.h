#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include "json.hpp"

// Diagnostic only. The existing lifecycle owner samples this at most once per
// second with virtual logging enabled. No hook, key injection, or state repair.
namespace ymcc::keyboardEvidence {
inline constexpr std::array<int,8> modifierKeys{
    VK_LCONTROL,VK_RCONTROL,VK_LSHIFT,VK_RSHIFT,VK_LMENU,VK_RMENU,VK_LWIN,VK_RWIN};
inline constexpr std::array<const char*,8> modifierNames{
    "leftCtrl","rightCtrl","leftShift","rightShift","leftAlt","rightAlt","leftWin","rightWin"};
struct Snapshot {
    ULONGLONG beginTick=0,endTick=0;
    DWORD threadId=0,foregroundPidBefore=0,foregroundPidAfter=0,queueError=0;
    HWND foregroundBefore=nullptr,foregroundAfter=nullptr;
    std::uint16_t asyncDownMask=0,callingThreadQueueDownMask=0;
    bool queueReadOk=false;
};
inline Snapshot read() noexcept {
    const DWORD savedError=GetLastError();
    Snapshot s;s.beginTick=GetTickCount64();s.threadId=GetCurrentThreadId();
    s.foregroundBefore=GetForegroundWindow();
    if(s.foregroundBefore)GetWindowThreadProcessId(s.foregroundBefore,&s.foregroundPidBefore);
    for(std::size_t i=0;i<modifierKeys.size();++i){
        // Discard the unreliable "pressed since last query" low bit.
        const auto value=static_cast<unsigned short>(GetAsyncKeyState(modifierKeys[i]));
        if((value&0x8000u)!=0)s.asyncDownMask|=std::uint16_t(1u<<i);
    }
    BYTE keyboard[256]{};
    SetLastError(ERROR_SUCCESS);
    s.queueReadOk=GetKeyboardState(keyboard)!=FALSE;
    if(s.queueReadOk){
        for(std::size_t i=0;i<modifierKeys.size();++i)
            if((keyboard[modifierKeys[i]]&0x80u)!=0)s.callingThreadQueueDownMask|=std::uint16_t(1u<<i);
    }else s.queueError=GetLastError();
    s.foregroundAfter=GetForegroundWindow();
    if(s.foregroundAfter)GetWindowThreadProcessId(s.foregroundAfter,&s.foregroundPidAfter);
    s.endTick=GetTickCount64();
    SetLastError(savedError);
    return s;
}
inline nlohmann::json toJson(const Snapshot& s) {
    auto modifiers=nlohmann::json::object();
    for(std::size_t i=0;i<modifierKeys.size();++i){
        modifiers[modifierNames[i]]={
            {"virtualKey",modifierKeys[i]},
            {"asyncDownBit",(s.asyncDownMask&(1u<<i))!=0},
            {"callingThreadQueueDown",s.queueReadOk?nlohmann::json((s.callingThreadQueueDownMask&(1u<<i))!=0):nlohmann::json(nullptr)}};
    }
    return {{"schemaVersion",1},{"captureBeginTick",s.beginTick},{"captureEndTick",s.endTick},
        {"callingThreadId",s.threadId},{"foregroundPidBefore",s.foregroundPidBefore},{"foregroundPidAfter",s.foregroundPidAfter},
        {"foregroundStableDuringCapture",s.foregroundBefore==s.foregroundAfter&&s.foregroundPidBefore==s.foregroundPidAfter},
        {"asyncDownMask",s.asyncDownMask},{"callingThreadQueueReadOk",s.queueReadOk},
        {"callingThreadQueueError",s.queueReadOk?nlohmann::json(nullptr):nlohmann::json(s.queueError)},
        {"callingThreadQueueDownMask",s.queueReadOk?nlohmann::json(s.callingThreadQueueDownMask):nlohmann::json(nullptr)},
        {"modifiers",std::move(modifiers)},
        {"asyncApi","GetAsyncKeyState-high-bit-only"},{"queueApi","GetKeyboardState-calling-thread-message-queue"},
        {"asyncZeroIsAbsenceProof",false},{"crossKeyAtomicSnapshot",false},{"eventEdgesCaptured",false},
        {"sourceAttribution","unobserved"},{"gameOrSteamKeyboardReceipt","unobserved"},
        {"scope","Sampled modifier state only; async zero may reflect desktop/access restrictions. Queue state belongs to the calling thread, not Steam/game. No physical/injected/device-origin or key-release proof; short edges may be missed."}};
}
} // namespace ymcc::keyboardEvidence