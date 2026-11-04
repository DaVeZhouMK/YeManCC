#pragma once
#include "process_inventory.h"
#include "monitor_identity.h"
#include <functional>

// The owner supplies topology only. All exported instances below are admitted
// from one live held handle; an action reopens and verifies that exact instance.
namespace ymcc::rtssprocess {
struct Identity { DWORD pid=0; ULONGLONG processCreated=0; std::wstring path,name; };
struct Observation {
    bool known=false; DWORD error=ERROR_SUCCESS;
    ULONGLONG observedAt=0; std::uint64_t generation=0;
    std::vector<Identity> processes;
};
class Handle {
    HANDLE value_;
public:
    explicit Handle(HANDLE value):value_(value){}
    ~Handle(){if(value_&&value_!=INVALID_HANDLE_VALUE)CloseHandle(value_);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    HANDLE get()const{return value_;}
};
inline DWORD failure(DWORD error=GetLastError()){return error?error:ERROR_GEN_FAILURE;}
inline Observation observe(const std::vector<std::wstring>& names) {
    Observation result;
    try {
        std::vector<std::wstring> requested;
        if(names.size()>16){result.error=ERROR_INVALID_PARAMETER;return result;}
        for(const auto& name:names){
            if(name.empty()||name.size()>260||name.find_first_of(L"\\/*?")!=std::wstring::npos||
                name.find(L'\0')!=std::wstring::npos){result.error=ERROR_INVALID_PARAMETER;return result;}
            const auto stem=nativeIdentityNormalizeName(name);
            if(stem.empty()){result.error=ERROR_INVALID_PARAMETER;return result;}
            requested.push_back(stem);
        }
        if(requested.empty()){result.known=true;return result;}
        DWORD session=0;
        if(!ProcessIdToSessionId(GetCurrentProcessId(),&session)){result.error=failure();return result;}
        const auto snapshot=ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),
            ymcc::processInventory::currentTime,0);
        result.observedAt=snapshot->observedAt;result.generation=snapshot->generation;
        if(snapshot->status!=ymcc::processInventory::Status::Complete){result.error=failure(snapshot->error);return result;}
        for(const auto& entry:snapshot->rows){
            if(!entry.pid||entry.pid==4||entry.pid==GetCurrentProcessId())continue;
            const auto hint=nativeIdentityNormalizeName(entry.name);
            if(std::find(requested.begin(),requested.end(),hint)==requested.end())continue;
            DWORD owner=0;
            if(!ProcessIdToSessionId(entry.pid,&owner)){result.error=failure();result.processes.clear();return result;}
            if(owner!=session)continue;
            Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.pid));
            if(!process.get()){result.error=failure();result.processes.clear();return result;}
            DWORD heldSession=0;FILETIME born{},ended{},kernel{},user{};
            wchar_t path[32768]{};DWORD count=static_cast<DWORD>(std::size(path));
            if(GetProcessId(process.get())!=entry.pid||!ProcessIdToSessionId(entry.pid,&heldSession)||heldSession!=session||
                WaitForSingleObject(process.get(),0)!=WAIT_TIMEOUT||!QueryFullProcessImageNameW(process.get(),0,path,&count)||
                !count||!GetProcessTimes(process.get(),&born,&ended,&kernel,&user)){
                result.error=failure();result.processes.clear();return result;
            }
            const ULONGLONG created=(ULONGLONG(born.dwHighDateTime)<<32)|born.dwLowDateTime;
            const std::wstring image(path,count);
            if(!created){result.error=ERROR_INVALID_DATA;result.processes.clear();return result;}
            // Reused PID with another image cannot turn a stale name into authority.
            if(nativeIdentityNormalizeName(image)!=hint)continue;
            if(result.processes.size()>=256){result.error=ERROR_MORE_DATA;result.processes.clear();return result;}
            result.processes.push_back({entry.pid,created,image,entry.name});
        }
        result.known=true;return result;
    }catch(const std::bad_alloc&){result.known=false;result.error=ERROR_NOT_ENOUGH_MEMORY;result.processes.clear();return result;}
    catch(...){result.known=false;result.error=ERROR_GEN_FAILURE;result.processes.clear();return result;}
}
struct AffinityResult { bool ok=false,matched=false,applied=false;DWORD error=ERROR_SUCCESS;const char* reason="unobserved"; };
inline bool affinityImageAllowed(const std::wstring& path) {
    const auto stem=nativeIdentityNormalizeName(path);
    return stem==L"rtss"||stem==L"encoderserver64"||stem==L"rtsshooksloader64";
}
template<class Allowed>
inline AffinityResult setFixedAffinity(const Identity& expected,Allowed&& allowed) {
    AffinityResult result;
    if(expected.pid<=4||expected.pid==GetCurrentProcessId()||!expected.processCreated||expected.path.empty()||
        expected.path.size()>32767||expected.path.find(L'\0')!=std::wstring::npos||!affinityImageAllowed(expected.path)){
        result.reason="invalid-identity";result.error=ERROR_INVALID_PARAMETER;return result;
    }
    if(!allowed()){result.reason="context-changed";result.error=ERROR_CANCELLED;return result;}
    DWORD session=0;
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&session)){result.reason="session-unknown";result.error=failure();return result;}
    Handle process(OpenProcess(PROCESS_SET_INFORMATION|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,expected.pid));
    if(!process.get()){result.reason="process-unavailable";result.error=failure();return result;}
    DWORD heldSession=0;FILETIME born{},ended{},kernel{},user{};
    wchar_t path[32768]{};DWORD count=static_cast<DWORD>(std::size(path));
    if(GetProcessId(process.get())!=expected.pid||!ProcessIdToSessionId(expected.pid,&heldSession)||heldSession!=session||
        WaitForSingleObject(process.get(),0)!=WAIT_TIMEOUT||!GetProcessTimes(process.get(),&born,&ended,&kernel,&user)||
        ((ULONGLONG(born.dwHighDateTime)<<32)|born.dwLowDateTime)!=expected.processCreated||
        !QueryFullProcessImageNameW(process.get(),0,path,&count)||!count||_wcsicmp(path,expected.path.c_str())!=0){
        result.reason="identity-changed";result.error=ERROR_INVALID_DATA;return result;
    }
    result.matched=true;
    constexpr DWORD_PTR mask=0xA0;
    DWORD_PTR current=0,system=0;
    if(!GetProcessAffinityMask(process.get(),&current,&system)){
        result.reason="affinity-query-failed";result.error=failure();return result;
    }
    if((system&mask)!=mask){result.reason="affinity-mask-unavailable";result.error=ERROR_NOT_SUPPORTED;return result;}
    if(!allowed()){result.reason="context-changed";result.error=ERROR_CANCELLED;return result;}
    if(!SetProcessAffinityMask(process.get(),mask)){
        result.reason="affinity-set-failed";result.error=failure();return result;
    }
    // Once this allowed write starts, report its actual result. Revocation must
    // not trigger a stale rollback or dispatch a second write into a new owner.
    result.applied=true;
    if(!GetProcessAffinityMask(process.get(),&current,&system)){
        result.reason="affinity-readback-failed";result.error=failure();return result;
    }
    result.ok=current==mask;result.reason=result.ok?"applied":"affinity-readback-mismatch";
    if(!result.ok)result.error=ERROR_INVALID_DATA;
    return result;
}
} // namespace ymcc::rtssprocess
