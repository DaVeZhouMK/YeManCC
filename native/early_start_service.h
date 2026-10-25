#pragma once
// Existing EarlyStartService only. Reads never mutate, repair never installs or
// changes an executable, and no desktop/Xbox-only selection calls this consumer.
#include <windows.h>
#include <winsvc.h>
#include <string>
#include <vector>
#include "json.hpp"
namespace ymcc::earlystart {
struct State {
    bool known=false, installed=false; DWORD startType=0, status=0, error=0;
};
struct Result {
    State state; bool ok=false, changed=false; std::string reason;
};
inline bool ready(const State& state) {
    return state.known && state.installed && state.startType==SERVICE_AUTO_START && state.status==SERVICE_RUNNING;
}
template<class Ports> Result ensure(Ports& ports) {
    Result result; result.state=ports.read();
    if(!result.state.known){result.reason="service-query-failed";return result;}
    if(!result.state.installed){result.reason="service-missing";return result;}
    if(result.state.startType!=SERVICE_AUTO_START){
        if(!ports.enableAutomatic()){result.state=ports.read();result.reason="service-enable-failed";return result;}
        result.changed=true;result.state=ports.read();
        if(!result.state.known||result.state.startType!=SERVICE_AUTO_START){result.reason="service-enable-unconfirmed";return result;}
    }
    if(result.state.status==SERVICE_STOPPED){
        if(!ports.start()){result.state=ports.read();result.ok=ready(result.state);result.reason=result.ok?"ready":"service-start-failed";return result;}
        result.changed=true;
    } else if(result.state.status!=SERVICE_RUNNING&&result.state.status!=SERVICE_START_PENDING){
        result.reason="service-transition-pending";return result;
    }
    // One bounded observation of an accepted request, never a retry/restart loop.
    const auto begin=ports.now();
    for(unsigned count=0;count<51;++count){
        result.state=ports.read();
        if(ready(result.state)){result.ok=true;result.reason="ready";return result;}
        if(!result.state.known||!result.state.installed||result.state.status!=SERVICE_START_PENDING||ports.now()-begin>=2500)break;
        ports.pause();
    }
    result.reason=result.state.status==SERVICE_START_PENDING?"service-start-pending":"service-not-ready";
    return result;
}
struct ServiceHandle {
    SC_HANDLE value=nullptr;
    explicit ServiceHandle(SC_HANDLE h):value(h){}
    ~ServiceHandle(){if(value)CloseServiceHandle(value);}
    ServiceHandle(const ServiceHandle&)=delete;
};
class WindowsPorts {
    DWORD lastError_=0;
public:
    State read() {
        State state;ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));
        if(!manager.value){state.error=GetLastError();return state;}
        ServiceHandle service(OpenServiceW(manager.value,L"EarlyStartService",SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG));
        if(!service.value){state.error=GetLastError();state.known=state.error==ERROR_SERVICE_DOES_NOT_EXIST;return state;}
        state.installed=true;SERVICE_STATUS_PROCESS status{};DWORD bytes=0;
        if(!QueryServiceStatusEx(service.value,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&bytes)){state.error=GetLastError();return state;}
        DWORD needed=0;QueryServiceConfigW(service.value,nullptr,0,&needed);
        if(!needed||needed>65536){state.error=GetLastError();return state;}
        std::vector<BYTE> storage(needed);auto config=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(storage.data());
        if(!QueryServiceConfigW(service.value,config,needed,&needed)){state.error=GetLastError();return state;}
        state.known=true;state.startType=config->dwStartType;state.status=status.dwCurrentState;state.error=lastError_;return state;
    }
    bool enableAutomatic() {
        ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));
        if(!manager.value){lastError_=GetLastError();return false;}
        ServiceHandle service(OpenServiceW(manager.value,L"EarlyStartService",SERVICE_CHANGE_CONFIG));
        if(!service.value){lastError_=GetLastError();return false;}
        const bool ok=ChangeServiceConfigW(service.value,SERVICE_NO_CHANGE,SERVICE_AUTO_START,SERVICE_NO_CHANGE,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)!=FALSE;
        lastError_=ok?0:GetLastError();return ok;
    }
    bool start() {
        ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));
        if(!manager.value){lastError_=GetLastError();return false;}
        ServiceHandle service(OpenServiceW(manager.value,L"EarlyStartService",SERVICE_START));
        if(!service.value){lastError_=GetLastError();return false;}
        const bool ok=StartServiceW(service.value,0,nullptr)!=FALSE;lastError_=ok?0:GetLastError();
        return ok||lastError_==ERROR_SERVICE_ALREADY_RUNNING;
    }
    ULONGLONG now()const{return GetTickCount64();}
    void pause()const{Sleep(50);}
};
inline nlohmann::json asJson(const State& state) {
    return {{"known",state.known},{"installed",state.installed},{"automatic",state.known&&state.startType==SERVICE_AUTO_START},{"running",state.known&&state.status==SERVICE_RUNNING},{"startType",state.startType},{"state",state.status},{"win32Error",state.error}};
}
inline nlohmann::json read(){WindowsPorts ports;return asJson(ports.read());}
inline nlohmann::json repair(){WindowsPorts ports;const auto result=ensure(ports);auto json=asJson(result.state);json["ok"]=result.ok;json["changed"]=result.changed;json["reason"]=result.reason;return json;}
}
