#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Actual owner startup/resume integration. No saved-profile or output-persona change.
#include "screen_touchpads.h"
#include <shellapi.h>
#include <cwchar>
#pragma comment(lib,"shell32.lib")
namespace ymcc::screenpads {
inline constexpr UINT kOwnerRebindMessage=WM_APP+176;
inline constexpr const wchar_t* kNativeCandidateSwitch=L"--screen-touchpads-native-input-candidate";
inline constexpr const wchar_t* kNativeLegacySwitch=L"--screen-touchpads-native-input-legacy";
inline bool nativeStartupRequested=false,nativeStartupConflict=false;
inline UINT_PTR ownerLifecycleEpoch=1;inline bool ownerRebindQueued=false;
struct NativeStartupPolicy {bool parsed=false,requested=false,legacy=false,conflict=false;};
inline NativeStartupPolicy parseNativeStartup(const wchar_t* commandLine) noexcept {
    NativeStartupPolicy policy;if(!commandLine||!*commandLine)return policy;int count=0;auto args=CommandLineToArgvW(commandLine,&count);if(!args)return policy;
    policy.parsed=true;for(int index=1;index<count;++index){if(std::wcscmp(args[index],kNativeCandidateSwitch)==0)policy.requested=true;if(std::wcscmp(args[index],kNativeLegacySwitch)==0)policy.legacy=true;}
    LocalFree(args);policy.conflict=policy.requested&&policy.legacy;return policy;
}
inline bool onOwnerThread(HWND window) noexcept {DWORD pid=0;return window&&GetWindowThreadProcessId(window,&pid)==GetCurrentThreadId()&&pid==GetCurrentProcessId();}
} // namespace ymcc::screenpads
#include "screen_touchpad_visibility.h"
namespace ymcc::screenpads {
inline bool initializeForOwner(HINSTANCE module,HWND window,const Config& config,int profile,const wchar_t* commandLine) {
    if(!onOwnerThread(window)){windowError=ERROR_ACCESS_DENIED;return false;}
    const auto policy=parseNativeStartup(commandLine);nativeStartupRequested=policy.parsed&&!policy.legacy&&!policy.conflict;nativeStartupConflict=policy.conflict;
    ++ownerLifecycleEpoch;ownerRebindQueued=false;setNativeInputCandidate(nativeStartupRequested&&!policy.legacy&&!policy.conflict);
    visibilityRefreshObserver=syncOwnerVisibility;
    initialize(module,window,config,profile);syncOwnerVisibility();return initialized;
}
inline void ownerOnSuspend() {++ownerLifecycleEpoch;ownerRebindQueued=false;release();}
inline bool queueOwnerResumeRebind() {
    // Candidate only: retain all existing Sleep Guard/standby/input-host policy.
    if(!nativeInputCandidateRequested||!initialized)return false;const HWND window=owner.load();if(!onOwnerThread(window))return false;
    if(ownerRebindQueued)return true;ownerRebindQueued=true;
    if(PostMessageW(window,kOwnerRebindMessage,ownerLifecycleEpoch,0))return true;
    ownerRebindQueued=false;windowError=GetLastError();return false;
}
inline bool consumeOwnerRebind(UINT_PTR epoch) {
    if(epoch!=ownerLifecycleEpoch||!ownerRebindQueued||!initialized||!onOwnerThread(owner.load()))return false;
    ownerRebindQueued=false;if(!nativeInputCandidateRequested)return false;
    release();for(int side=0;side<2;++side)nativeInputClose(side);buttonsNativeInputDisable();
    refresh(true);return true; // Recreate controlled sources after a compositor/device resume.
}
inline void quiesceForOwner(){
    if(quickAccessEdgeObserver)quickAccessEdgeObserver(-1,false);
    quiesce();if(quickAccessContextObserver)quickAccessContextObserver();stopOwnerVisibility();visibilityRefreshObserver=nullptr;++ownerLifecycleEpoch;ownerRebindQueued=false;
}
inline void shutdownForOwner(){quiesceForOwner();shutdown();}
inline Json ownerStartupState(){
    const auto inputState=state();return {{"requested",nativeStartupRequested},{"conflict",nativeStartupConflict},{"compiled",inputState.value("nativeMousePassCompiled",false)},
        {"active",inputState.value("nativeMousePassActive",false)},{"epoch",ownerLifecycleEpoch},{"rebindQueued",ownerRebindQueued}};
}
inline bool ownerSelftestRequested(const wchar_t* commandLine) noexcept {
    if(!commandLine)return false;int count=0;auto args=CommandLineToArgvW(commandLine,&count);if(!args)return false;
    bool requested=false;for(int i=1;i<count;++i)requested=requested||std::wcscmp(args[i],L"--screen-touchpads-owner-selftest")==0;LocalFree(args);return requested;
}
inline Json ownerApplicationSelfTest(HINSTANCE module,const wchar_t* commandLine) {
    // Early-return app entry: no settings, WebView, device, service or InputHost startup.
    Json result={{"ok",false},{"featureAcceptancePassed",false},{"deviceOperations",0},{"configurationWrites",0},{"systemInputInjected",false},{"cases",Json::array()}};
    bool ok=true;auto check=[&](const char* name,bool passed){ok=ok&&passed;result["cases"].push_back({{"name",name},{"ok",passed}});};
    const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com)){result["failure"]=(long)com;return result;}
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND window=nullptr;
    try {
        WNDCLASSW cls{};cls.hInstance=module;cls.lpszClassName=L"YeManScreenOwnerAppSelftest";
        cls.lpfnWndProc=[](HWND h,UINT m,WPARAM w,LPARAM l)->LRESULT{if(m==kOwnerRebindMessage){consumeOwnerRebind((UINT_PTR)w);return 0;}if(m==kOwnerVisibilityMessage){consumeOwnerVisibility((UINT_PTR)w);return 0;}return DefWindowProcW(h,m,w,l);};
        if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("selftest-owner-class-unavailable");
        window=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,module,nullptr);if(!window)throw std::runtime_error("selftest-owner-unavailable");
        dryRun=true;setContext("disabled",false);Config config=profileDefault(0);config.enabled=true;config.layout=Layout::Single;config.single=Mode::Mouse;config.standaloneSpecialMode=StandaloneSpecialMode::SteamDeck;
        check("application-entry-initializes-actual-owner",initializeForOwner(module,window,config,0,commandLine));
        const auto policy=parseNativeStartup(commandLine);const auto startup=ownerStartupState();const bool compiled=startup.value("compiled",false),expected=compiled&&policy.parsed&&!policy.legacy&&!policy.conflict;
        result["startup"]=startup;check("native-default-with-explicit-legacy-fallback",nativeInputCandidateRequested==expected);
        check("candidate-state-matches-real-pad-and-button-surfaces",state().value("nativeMousePassActive",false)==expected&&windows[0]&&buttonWindows[1]&&buttonWindows[2]);
        const auto saved=configJson(getConfig());const auto epoch=ownerLifecycleEpoch;ownerOnSuspend();check("suspend-advances-owner-epoch",ownerLifecycleEpoch!=epoch);
        const bool first=queueOwnerResumeRebind(),second=queueOwnerResumeRebind();check("resume-only-queues-native-owner",first==expected&&second==expected&&ownerRebindQueued==expected);
        const auto ends=GetTickCount64()+60;MSG message{};do{while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);Sleep(1);}while(GetTickCount64()<ends);
        check("queued-resume-rebuild-does-not-change-profile",!ownerRebindQueued&&saved==configJson(getConfig())&&state().value("nativeMousePassActive",false)==expected);
        check("duplicate-resume-is-ignored",!consumeOwnerRebind(ownerLifecycleEpoch));
        const auto stale=ownerLifecycleEpoch;shutdownForOwner();check("shutdown-cleans-native-pads-and-buttons",!windows[0]&&!buttonsVisible()&&!consumeOwnerRebind(stale));
        check("same-config-app-owner-restart-recreates-windows",initializeForOwner(module,window,config,0,commandLine)&&windows[0]&&buttonWindows[1]&&buttonWindows[2]);
        check("test-has-no-output-or-virtual-target",injectedCount==0&&screenButtonMask.load()==0&&!controlTargetEnabled.load()&&!nativeMode.load());
        result["restartStartup"]=ownerStartupState();shutdownForOwner();
    }catch(const std::exception& error){ok=false;result["failure"]=error.what();shutdownForOwner();}catch(...){ok=false;result["failure"]="native-owner-exception";shutdownForOwner();}
    if(window)DestroyWindow(window);result["ok"]=ok;CoUninitialize();return result;
}
} // namespace ymcc::screenpads