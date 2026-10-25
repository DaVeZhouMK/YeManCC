#pragma once
// Decky-only admission for a real Windows Gaming Full Screen Experience.
// Missing system API, inactive experience, another Windows session and user OFF
// all remain closed. No Steam start/mode request, registry setting or polling.
#include "steam_session_observer.h"
#include "json.hpp"
namespace ymcc::deckygaming {
struct Observation {bool apiAvailable=false,supported=false,active=false,sameWindowsSession=false;};
inline bool admit(const steamsession::Session& session,const nlohmann::json& mode,
                  const Observation& observation,bool enabled,bool powerReady) {
    try{return enabled&&powerReady&&observation.apiAvailable&&observation.supported&&
        observation.active&&observation.sameWindowsSession&&session.pid&&session.created&&!session.root.empty()&&
        mode.value("known",false)&&mode.value("running",false)&&mode.value("mode",std::string{})=="bigpicture"&&
        mode.at("pid")==session.pid&&mode.at("birth")==std::to_string(session.created);
    }catch(...){return false;}
}
class WindowsProbe {
    using Flag=BOOL(WINAPI*)();
    HMODULE module_=nullptr;Flag supported_=nullptr,active_=nullptr;
public:
    WindowsProbe(){
        module_=LoadLibraryExW(L"api-ms-win-gaming-experience-l1-1-0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(module_){supported_=reinterpret_cast<Flag>(GetProcAddress(module_,"IsGamingFullScreenExperienceSupported"));
            active_=reinterpret_cast<Flag>(GetProcAddress(module_,"IsGamingFullScreenExperienceActive"));}
    }
    WindowsProbe(const WindowsProbe&)=delete;
    WindowsProbe& operator=(const WindowsProbe&)=delete;
    ~WindowsProbe(){if(module_)FreeLibrary(module_);}
    Observation read(const steamsession::Session& session)const {
        Observation result;result.apiAvailable=supported_&&active_;
        if(!result.apiAvailable)return result;
        DWORD current=0,steam=0;
        result.sameWindowsSession=ProcessIdToSessionId(GetCurrentProcessId(),&current)&&
            ProcessIdToSessionId(session.pid,&steam)&&current==steam;
        if(!result.sameWindowsSession)return result;
        // Verify a live Steam process with the exact birth/path before using the
        // system signal. The Runtime rechecks session and OFF at dispatch too.
        ULONGLONG birth=0;HANDLE process=steamsession::verifiedProcess(session.pid,session.root,&birth);
        if(!process||birth!=session.created){if(process)CloseHandle(process);result.sameWindowsSession=false;return result;}
        CloseHandle(process);
        result.supported=supported_()!=FALSE;
        if(result.supported)result.active=active_()!=FALSE;
        return result;
    }
};
}
