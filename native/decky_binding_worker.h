#pragma once
// One serialized binding request inside the same YMCC executable's owned worker.
// The caller supplies only its inherited cancellation event and private pipe sink.
// This is the child consumer; Runtime's legacy callback path is not replaced here.
#include "decky_sidebar_bootstrap.h"
#include "steam_session_observer.h"
#include <charconv>
#include <limits>
#include <mutex>
#include <string_view>
#include <set>
#include <vector>

namespace ymcc::deckybinding::worker {
using Json=nlohmann::json;
inline constexpr const char* schema="ymcc.decky.binding-worker.v1";
inline constexpr std::size_t maximumRequestBytes=128*1024;
inline void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
inline bool hex(std::string_view text,std::size_t size){return text.size()==size&&text.find_first_not_of("0123456789abcdef")==std::string_view::npos;}
inline std::uint64_t decimal(const Json& value){
    require(value.is_string(),"mirror-bootstrap-worker-identity-invalid");
    const auto& text=value.get_ref<const std::string&>();std::uint64_t result=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    require(!text.empty()&&text.size()<=20&&parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&result!=0,"mirror-bootstrap-worker-identity-invalid");
    return result;
}
inline DWORD number(const Json& value,bool nonzero=true){
    require(value.is_number_unsigned()||(value.is_number_integer()&&value.get<std::int64_t>()>=0),"mirror-bootstrap-worker-identity-invalid");
    const auto result=value.get<std::uint64_t>();
    require(result<=MAXDWORD&&(!nonzero||result!=0),"mirror-bootstrap-worker-identity-invalid");return static_cast<DWORD>(result);
}
inline std::wstring wide(const Json& value){
    require(value.is_string(),"mirror-bootstrap-worker-identity-invalid");
    const auto& text=value.get_ref<const std::string&>();
    require(!text.empty()&&text.size()<=32768&&text.find('\0')==std::string::npos,"mirror-bootstrap-worker-identity-invalid");
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    require(count>0,"mirror-bootstrap-worker-identity-invalid");std::wstring result(static_cast<std::size_t>(count),L'\0');
    require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),count)==count,"mirror-bootstrap-worker-identity-invalid");return result;
}
struct Request {
    std::string id;
    std::uint64_t generation=0;
    ymcc::steamsession::Session steam;
    DWORD windowsSession=0;
    Json bootstrap;
    bool requireFreshContext=false,retainedFrontendOnly=false;
};
inline Request parse(std::string_view bytes){
    require(!bytes.empty()&&bytes.size()<=maximumRequestBytes,"mirror-bootstrap-worker-request-size");
    std::vector<std::set<std::string>> objectKeys;
    const auto value=Json::parse(bytes,[&](int,Json::parse_event_t event,Json& parsed){
        if(event==Json::parse_event_t::object_start)objectKeys.emplace_back();
        else if(event==Json::parse_event_t::object_end)objectKeys.pop_back();
        else if(event==Json::parse_event_t::key)require(!objectKeys.empty()&&objectKeys.back().insert(parsed.get<std::string>()).second,"mirror-bootstrap-worker-request-invalid");
        return true;
    });
    require(value.is_object()&&value.size()==7&&value.value("schema",std::string{})==schema,"mirror-bootstrap-worker-request-invalid");
    Request result;
    result.id=value.at("id").get<std::string>();require(hex(result.id,32),"mirror-bootstrap-worker-request-invalid");
    result.generation=decimal(value.at("generation"));
    const auto& identity=value.at("steam");require(identity.is_object()&&identity.size()==5,"mirror-bootstrap-worker-identity-invalid");
    result.steam.root=ymcc::steamlive::normalizedPath(wide(identity.at("root")));
    require(!result.steam.root.empty()&&std::filesystem::path(result.steam.root).is_absolute(),"mirror-bootstrap-worker-identity-invalid");
    result.steam.pid=number(identity.at("pid"));result.steam.created=decimal(identity.at("created"));result.steam.account=number(identity.at("account"));result.windowsSession=number(identity.at("windowsSession"),false);
    require(value.at("requireFreshContext").is_boolean()&&value.at("retainedFrontendOnly").is_boolean(),"mirror-bootstrap-worker-request-invalid");
    result.requireFreshContext=value.at("requireFreshContext").get<bool>();result.retainedFrontendOnly=value.at("retainedFrontendOnly").get<bool>();
    require(!(result.requireFreshContext&&result.retainedFrontendOnly),"mirror-bootstrap-worker-request-invalid");
    result.bootstrap=value.at("bootstrap");
    require(result.bootstrap.is_object()&&hex(result.bootstrap.value("token",std::string{}),64)&&hex(result.bootstrap.value("runId",std::string{}),32)&&result.bootstrap.contains("endpoint")&&result.bootstrap.at("endpoint").is_string(),"mirror-bootstrap-worker-request-invalid");
    if(result.retainedFrontendOnly)require(hex(result.bootstrap.value("frontendBuildId",std::string{}),64),"mirror-bootstrap-worker-request-invalid");
    return result;
}
// ActiveProcess.pid can outlive Steam. Observer already discovers a unique live
// Steam when this value is stale; accept that SAME verified lease in the child.
// Different live Steam or unreadable identity still vetoes. Do not write registry.
// PID/birth/image/account/Windows user/session and registry root stay authoritative.
inline bool registeredPidCompatible(DWORD registered,DWORD expected,const std::wstring& root){
    if(!registered||registered==expected)return true;
    const auto process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,registered);
    if(!process)return GetLastError()==ERROR_INVALID_PARAMETER; // Confirmed absent only.
    const auto waited=WaitForSingleObject(process,0);
    bool stale=waited==WAIT_OBJECT_0;
    if(waited==WAIT_TIMEOUT){
        wchar_t image[32768]{};DWORD count=static_cast<DWORD>(std::size(image));
        if(QueryFullProcessImageNameW(process,0,image,&count))
            stale=ymcc::steamlive::normalizedPath(std::wstring(image,count))!=root+L"\\steam.exe";
    }
    CloseHandle(process);return stale;
}
class SteamLease {
    HANDLE process_=nullptr;
    const Request& request_;
    const std::wstring registryPath_;
public:
    // Alternate registry scope is for isolated native tests only.
    explicit SteamLease(const Request& request,const std::wstring& registryPath=L"Software\\Valve\\Steam",std::string* rejectedCheck=nullptr):request_(request),registryPath_(registryPath){
        ULONGLONG born=0;
        process_=ymcc::steamsession::verifiedProcess(request.steam.pid,request.steam.root,&born);
        try{
            // Named predicates only: never include user/account/token values in
            // diagnostics. This explains a refusal without weakening the lease.
            const auto reject=[&](const char* check){if(rejectedCheck)*rejectedCheck=check;throw std::runtime_error("mirror-bootstrap-session-changed");};
            if(!process_||born!=request.steam.created)reject("steam-process-or-birth");
            if(!ymcc::deckymirror::sameWindowsUserAndSession(request.steam.pid))reject("steam-user-or-session");
            require(current(rejectedCheck),"mirror-bootstrap-session-changed");
        }
        catch(...){if(process_)CloseHandle(process_);process_=nullptr;throw;}
    }
    ~SteamLease(){if(process_)CloseHandle(process_);}
    SteamLease(const SteamLease&)=delete;
    bool current(std::string* rejectedCheck=nullptr)const{
        const auto reject=[&](const char* check){if(rejectedCheck)*rejectedCheck=check;return false;};
        if(rejectedCheck)rejectedCheck->clear();
        if(!process_||WaitForSingleObject(process_,0)!=WAIT_TIMEOUT)return reject("steam-process-exited");
        DWORD session=0;if(!ProcessIdToSessionId(request_.steam.pid,&session)||session!=request_.windowsSession)return reject("steam-windows-session");
        const auto registered=ymcc::steamsession::regNumber(registryPath_,L"pid");
        if(ymcc::steamsession::regNumber(registryPath_,L"ActiveUser")!=request_.steam.account)return reject("steam-account");
        if(ymcc::steamsession::steamRoot(registryPath_)!=request_.steam.root)return reject("steam-root");
        if(!registeredPidCompatible(registered,request_.steam.pid,request_.steam.root))return reject("steam-registered-pid");
        return true;
    }
};
struct Event {
    HANDLE value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Event(){require(value!=nullptr,"mirror-bootstrap-worker-event-failed");}
    ~Event(){if(value)CloseHandle(value);}
    Event(const Event&)=delete;
};
// The identity comes from the live observation HANDLE, never from request JSON.
inline Json contextIdentity(HANDLE context) {
    require(context && WaitForSingleObject(context,0)==WAIT_TIMEOUT,"mirror-bootstrap-context-exited");
    const DWORD pid=GetProcessId(context); DWORD session=0;
    FILETIME birth{},exit{},kernel{},user{};
    wchar_t image[32768]{}; DWORD length=static_cast<DWORD>(std::size(image));
    require(pid && GetProcessTimes(context,&birth,&exit,&kernel,&user) &&
        QueryFullProcessImageNameW(context,0,image,&length) && ProcessIdToSessionId(pid,&session),
        "mirror-bootstrap-context-watch-unavailable");
    const auto path=ymcc::steamlive::normalizedPath(std::wstring(image,length));
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);
    require(count>0,"mirror-bootstrap-context-watch-unavailable");
    std::string utf8(static_cast<std::size_t>(count),'\0');
    require(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),utf8.data(),count,nullptr,nullptr)==count,
        "mirror-bootstrap-context-watch-unavailable");
    require(WaitForSingleObject(context,0)==WAIT_TIMEOUT,"mirror-bootstrap-context-exited");
    return {{"pid",pid},{"birth",std::to_string((static_cast<ULONGLONG>(birth.dwHighDateTime)<<32)|birth.dwLowDateTime)},
        {"path",utf8},{"session",session}};
}
// Emit is confined to the child. A noncooperative transport cannot retain the
// parent's Runtime stack: the process owner cancels/ends this entire child Job.
// It must serialize one framed packet, and returns false when the parent is gone.
template<class Emit>
int run(std::string_view bytes,HANDLE inheritedCancel,Emit&& emit){
    Request request;bool identified=false,resultEmitted=false,mutationMayHaveOccurred=false;std::mutex output;std::string rejectedSessionCheck;
    const auto send=[&](const char* kind,const Json& detail){
        std::lock_guard lock(output);
        require(emit(Json{{"schema",schema},{"id",request.id},{"generation",std::to_string(request.generation)},{"kind",kind},{"detail",detail}}),"mirror-bootstrap-worker-channel-closed");
    };
    try{
        request=parse(bytes);identified=true;
        auto cancellation=std::make_shared<ymcc::deckymirror::CancelToken>(inheritedCancel);
        require(!cancellation->canceled(),"mirror-bootstrap-canceled");SteamLease steam(request,L"Software\\Valve\\Steam",&rejectedSessionCheck);Event watchDone;
        ymcc::deckymirror::BootstrapWatch watch;
        const auto diagnostic=[&](const Json& detail){
            bool finished=detail.value("bootstrapStage",std::string{})=="failed"||detail.value("bootstrapStage",std::string{})=="terminal";
            try{send("diagnostic",detail);}catch(...){cancellation->cancel();finished=true;}
            if(finished)SetEvent(watchDone.value);
        };
        HANDLE context=nullptr;
        mutationMayHaveOccurred=true; // A failed transport must not be reported as a proven no-op.
        auto result=ymcc::deckymirror::injectBootstrap(request.steam.root,request.bootstrap,cancellation,&context,&watch,diagnostic,request.requireFreshContext,request.retainedFrontendOnly,request.steam.pid,[&]{return steam.current();});
        struct Context {HANDLE value;~Context(){if(value)CloseHandle(value);}} lease{context};
        mutationMayHaveOccurred=result.value("mutated",true);
        if(result.value("ok",false))result["contextLease"]=contextIdentity(context);
        send("result",result);resultEmitted=true;
        if(!result.value("ok",false)){watch.stop();return 2;}
        require(context!=nullptr,"mirror-bootstrap-context-watch-unavailable");
        HANDLE waiting[]{cancellation->event(),context,watchDone.value};
        const auto end=WaitForMultipleObjects(3,waiting,FALSE,INFINITE);
        require(end<WAIT_OBJECT_0+3,"mirror-bootstrap-worker-wait-failed");
        // This indefinite wait exists only inside the owned child and wakes on
        // cancel/context exit/watch termination. Parent hard stop is independent.
        watch.stop();return 0;
    }catch(const std::exception& error){
        if(identified){try{
            if(!resultEmitted){
                Json failure{{"ok",false},{"mutated",mutationMayHaveOccurred},{"reason",ymcc::deckymirror::bootstrapSafeReason(error)}};
                if(!rejectedSessionCheck.empty())failure["sessionCheck"]=rejectedSessionCheck;
                send("result",failure);
            }
            else send("diagnostic",Json{{"bootstrapStage","failed"},{"reason",ymcc::deckymirror::bootstrapSafeReason(error)}});
        }catch(...){}}
        return 2;
    }
}
} // namespace ymcc::deckybinding::worker
