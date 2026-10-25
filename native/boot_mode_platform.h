#pragma once
// Mode switching is an explicit user action. Startup restores configuration only.
#include <windows.h>
#include "json.hpp"
namespace ymcc::bootmode {
struct PlatformState { bool known=false, supported=false, active=false, activeKnown=false; DWORD error=0; const char* reason="api-unavailable"; };
inline PlatformState platformObservation(bool supportedExport,bool activeExport,bool supported,bool active) {
    PlatformState state;state.known=supportedExport||(activeExport&&active);state.supported=supported||active;
    state.activeKnown=activeExport;state.active=activeExport&&active;
    state.reason=!state.known?"api-unavailable":!state.supported?"not-supported":!state.activeKnown?"active-query-unavailable":state.active?"active":"supported-desktop";
    return state;
}
inline PlatformState readPlatform() {
    using Flag = BOOL(WINAPI*)();
    const auto module=LoadLibraryExW(L"api-ms-win-gaming-experience-l1-1-0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module) {PlatformState state;state.error=GetLastError();return state;}
    const auto supported=reinterpret_cast<Flag>(GetProcAddress(module,"IsGamingFullScreenExperienceSupported"));
    const auto active=reinterpret_cast<Flag>(GetProcAddress(module,"IsGamingFullScreenExperienceActive"));
    // Active is an independent OS observation; a support hint must not mask it.
    const bool activeValue=active && active()!=FALSE;
    auto state=platformObservation(supported!=nullptr,active!=nullptr,supported && supported()!=FALSE,activeValue);
    FreeLibrary(module);
    return state;
}
inline nlohmann::json platformJson(const PlatformState& state) {
    return {{"known",state.known},{"supported",state.supported},{"active",state.active},{"activeKnown",state.activeKnown},{"reason",state.reason},{"win32Error",state.error}};
}
inline bool mayRequest(const PlatformState& state,bool expected,bool target,bool userInitiated) {
    return userInitiated && state.known && state.supported && state.activeKnown && state.active==expected && state.active!=target;
}
inline nlohmann::json setPlatform(const nlohmann::json& args) {
    if(!args.is_object() || args.size()!=3 || !args.contains("active") || !args["active"].is_boolean() ||
       !args.contains("expectedActive") || !args["expectedActive"].is_boolean() ||
       !args.contains("userInitiated") || !args["userInitiated"].is_boolean() || !args["userInitiated"].get<bool>())
        return {{"ok",false},{"reason","explicit-intent-required"}};
    const auto state=readPlatform();const auto target=args["active"].get<bool>();
    auto result=platformJson(state);
    if(!state.known || !state.supported || !state.activeKnown) {result["ok"]=false;result["reason"]="gaming-experience-unavailable";return result;}
    if(state.active!=args["expectedActive"].get<bool>()) {result["ok"]=false;result["reason"]="gaming-experience-changed";return result;}
    if(state.active==target) {result["ok"]=true;result["dispatched"]=false;return result;}
    if(!mayRequest(state,args["expectedActive"].get<bool>(),target,true)) {result["ok"]=false;return result;}
    // Windows owns entry/exit. Do not synthesize a fullscreen window or modify boot intent.
    if((GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN)|GetAsyncKeyState(VK_F11)) & 0x8000) {result["ok"]=false;result["reason"]="shortcut-already-held";return result;}
    INPUT keys[4]{};
    for(auto& key:keys) key.type=INPUT_KEYBOARD;
    keys[0].ki.wVk=VK_LWIN;keys[1].ki.wVk=VK_F11;
    keys[2].ki.wVk=VK_F11;keys[2].ki.dwFlags=KEYEVENTF_KEYUP;
    keys[3].ki.wVk=VK_LWIN;keys[3].ki.dwFlags=KEYEVENTF_KEYUP;
    const auto sent=SendInput(4,keys,sizeof(INPUT));
    if(sent!=4) { SendInput(2,keys+2,sizeof(INPUT));result["ok"]=false;result["reason"]="input-request-unconfirmed";return result; }
    result=platformJson(readPlatform());result["ok"]=true;result["dispatched"]=true;
    return result; // Request acceptance alone does not prove the OS changed mode.
}
}
