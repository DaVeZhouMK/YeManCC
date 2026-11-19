#pragma once
// Additive bootstrap injection. Never edits Steam preferences or launches/restarts Steam.
#include "steam_live_cdp.h"
#include "steam_failure_diagnostics.h"
#include "decky_sidebar_async_http.h"
#include "decky_sidebar_bootstrap_watch.h"
#include "decky_sidebar_frontend_update_script.h"
#include "decky_sidebar_health_script.h"
#include "decky_loader_identity.h"
#include <fstream>
#include <memory>
namespace ymcc::deckymirror {
inline std::string frontendBuildId(const std::wstring& path) {
    std::ifstream stream(path,std::ios::binary);char prefix[128]{};stream.read(prefix,sizeof(prefix)-1);
    const std::string line(prefix,static_cast<size_t>(stream.gcount())),tag="// YMCC_DECKY_FRONTEND_BUILD_ID=";
    if(line.rfind(tag,0)!=0||line.size()<tag.size()+64)return {};
    const auto id=line.substr(tag.size(),64);
    if(id.find_first_not_of("0123456789abcdef")!=std::string::npos)return {};
    const auto end=tag.size()+64;if(line.size()>end&&line[end]!='\r'&&line[end]!='\n')return {};
    return id;
}
// The held-process consumer checks both token fields on the object whose
// image and creation time it already verified, without reopening by PID.
inline bool sameWindowsTokenUser(HANDLE process, DWORD* verifiedSession) {
    struct Handle { HANDLE value=nullptr; ~Handle(){if(value)CloseHandle(value);} } ownToken,peerToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &ownToken.value) ||
        !OpenProcessToken(process, TOKEN_QUERY, &peerToken.value)) return false;
    DWORD ours=0,theirs=0,bytes=0;
    if (verifiedSession &&
        (!GetTokenInformation(ownToken.value,TokenSessionId,&ours,sizeof(ours),&bytes) ||
         !GetTokenInformation(peerToken.value,TokenSessionId,&theirs,sizeof(theirs),&bytes) || ours!=theirs)) return false;
    DWORD ownSize=0,peerSize=0;
    GetTokenInformation(ownToken.value,TokenUser,nullptr,0,&ownSize);
    GetTokenInformation(peerToken.value,TokenUser,nullptr,0,&peerSize);
    if (!ownSize || !peerSize || ownSize>65536 || peerSize>65536) return false;
    std::vector<unsigned char> own(ownSize),peer(peerSize);
    if (!GetTokenInformation(ownToken.value,TokenUser,own.data(),ownSize,&ownSize) ||
        !GetTokenInformation(peerToken.value,TokenUser,peer.data(),peerSize,&peerSize) ||
        !EqualSid(reinterpret_cast<const TOKEN_USER*>(own.data())->User.Sid,reinterpret_cast<const TOKEN_USER*>(peer.data())->User.Sid)) return false;
    if (verifiedSession) *verifiedSession=theirs;
    return true;
}
inline bool sameWindowsUserAndSession(DWORD pid) {
    DWORD ours=0,theirs=0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(),&ours) || !ProcessIdToSessionId(pid,&theirs) || ours!=theirs) return false;
    const auto close=[](void* handle){CloseHandle(handle);};
    const std::unique_ptr<void,decltype(close)> process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid),close);
    return process && sameWindowsTokenUser(process.get(),nullptr);
}
// Owns only a read/observe lease. It is never assigned to our loader Job or killed.
class ObservedProcess {
    HANDLE handle_=nullptr;
public:
    DWORD pid=0;ULONGLONG created=0;
    explicit ObservedProcess(DWORD value):pid(value){
        if(!sameWindowsUserAndSession(pid))throw std::runtime_error("mirror-bootstrap-owner-mismatch");
        handle_=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!handle_&&GetLastError()==ERROR_ACCESS_DENIED)throw std::runtime_error("mirror-bootstrap-context-watch-unavailable");
        try{
            FILETIME birth{},exit{},kernel{},user{};
            if(!handle_||!GetProcessTimes(handle_,&birth,&exit,&kernel,&user)||WaitForSingleObject(handle_,0)!=WAIT_TIMEOUT)
                throw std::runtime_error("mirror-bootstrap-context-exited");
            created=(static_cast<ULONGLONG>(birth.dwHighDateTime)<<32)|birth.dwLowDateTime;
        }catch(...){if(handle_)CloseHandle(handle_);handle_=nullptr;throw;}
    }
    ~ObservedProcess(){if(handle_)CloseHandle(handle_);}
    ObservedProcess(const ObservedProcess&)=delete;
    ObservedProcess& operator=(const ObservedProcess&)=delete;
    bool alive()const{return handle_&&WaitForSingleObject(handle_,0)==WAIT_TIMEOUT;}
    bool sameCurrentUserAndSession()const{
        DWORD session=0;
        return alive()&&sameWindowsTokenUser(handle_,&session)&&alive();
    }
    HANDLE duplicate()const noexcept{
        HANDLE value=nullptr;
        return DuplicateHandle(GetCurrentProcess(),handle_,GetCurrentProcess(),&value,0,FALSE,DUPLICATE_SAME_ACCESS)?value:nullptr;
    }
    HANDLE release(){const auto value=handle_;handle_=nullptr;return value;}
};
// Exact error consumer shared by discovery and its isolated native fixture.
// A failed constructor may still own callback/buffer debt. End this child so
// the existing process owner can prove return before any new attempt.
// Work and cleanup share the original absolute operation deadline. Successful
// CloseHandle alone is not return proof. Preserve the work exception if cleanup
// completes; otherwise stop this child and retain both error facts.
template<class Work>
auto bootstrapRequestRetired(AsyncRequest& request,ULONGLONG deadline,Work&& work){
    using Value=decltype(work(request));std::optional<Value> value;std::exception_ptr failure;
    try{value.emplace(work(request));}catch(...){failure=std::current_exception();}
    const auto returned=request.retire(deadline);
    if(!returned.returned())throw AsyncRetirementFailure(returned.closeAccepted,returned.finalNotification,returned.error,failure);
    if(failure)std::rethrow_exception(failure);
    return std::move(*value);
}
inline void bootstrapDiscoveryRejected(const std::exception& error,bool hasSelection,nlohmann::json& discoveryFailure){
    if(const auto* construction=dynamic_cast<const AsyncConstructionFailure*>(&error))throw *construction;
    if(const auto* cleanup=dynamic_cast<const AsyncRetirementFailure*>(&error))throw *cleanup;
    if(!hasSelection)try{auto fact=bootstrapTransportFailure(error);if(fact.is_object())discoveryFailure=std::move(fact);}catch(...){}
}
class ContextHandleSlot {
    HANDLE handle_=nullptr; // Only the dedicated sidebar worker writes/takes it.
public:
    ContextHandleSlot()=default;
    ContextHandleSlot(const ContextHandleSlot&)=delete;
    ContextHandleSlot& operator=(const ContextHandleSlot&)=delete;
    ~ContextHandleSlot(){if(handle_)CloseHandle(handle_);}
    void replace(HANDLE value){if(handle_)CloseHandle(handle_);handle_=value;}
    HANDLE take(){const auto value=handle_;handle_=nullptr;return value;}
};
inline nlohmann::json injectBootstrap(const std::wstring& steam, const nlohmann::json& bootstrap,
    std::shared_ptr<CancelToken> cancellation = {}, HANDLE* contextProcess = nullptr,
    BootstrapWatch* watch = nullptr, BootstrapWatch::Diagnostic diagnostic = {}, bool requireFreshContext = false, bool retainedFrontendOnly = false,
    DWORD expectedSteamPid = 0, std::function<bool()> bindingCurrent = {}) {
    using namespace ymcc::steamlive;
    if(contextProcess)*contextProcess=nullptr;
    if(watch)watch->stop();
    bool sent=false;
    nlohmann::json steamFailure, discoveryFailure; // Bounded facts from this operation only.
    const auto operationStarted=GetTickCount64();
    try {
        if(!bootstrap.is_object() || !bootstrap.contains("endpoint") || !bootstrap.contains("token") || !bootstrap.contains("runId"))
            throw std::runtime_error("mirror-bootstrap-invalid");
        if (!bootstrap["endpoint"].is_string() || !bootstrap["token"].is_string() || !bootstrap["runId"].is_string())
            throw std::runtime_error("mirror-bootstrap-invalid");
        const auto token = bootstrap["token"].get<std::string>(), run = bootstrap["runId"].get<std::string>();
        if (token.size()!=64 || run.size()!=32 || token.find_first_not_of("0123456789abcdef")!=std::string::npos || run.find_first_not_of("0123456789abcdef")!=std::string::npos)
            throw std::runtime_error("mirror-bootstrap-invalid");
        if(bootstrap.contains("retainedFrontendBuildId")){
            const auto prior=bootstrap.at("retainedFrontendBuildId").get<std::string>();
            if(!retainedFrontendOnly||requireFreshContext||prior.size()!=64||prior.find_first_not_of("0123456789abcdef")!=std::string::npos||
               !bootstrap.contains("frontendBuildId")||bootstrap.at("frontendBuildId")==prior)throw std::runtime_error("mirror-bootstrap-invalid");
        }
        if(!cancellation)cancellation=std::make_shared<CancelToken>();
        const auto deadline=GetTickCount64()+7000;
        listenersource::Deadline listenerDeadline(deadline);
        const auto checkpoint=[&]{
            if(cancellation->canceled())throw std::runtime_error("mirror-bootstrap-canceled");
            if(bindingCurrent&&!bindingCurrent())throw std::runtime_error("mirror-bootstrap-session-changed");
            if(GetTickCount64()>=deadline)throw std::runtime_error("mirror-bootstrap-deadline");
        };
        checkpoint();
        const auto candidates=listeners(steam,expectedSteamPid);
        auto session=std::make_shared<HttpHandle>(WinHttpOpen(L"YMCC-DeckyMirror/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        WinHttpSetTimeouts(session->h,1000,1000,1500,1500);
        const auto started=GetTickCount64();
        const auto selected=ymcc::steamdiscovery::select<std::string>(ymcc::steamdiscovery::Kind::Bootstrap,candidates,
            [&]{checkpoint();if(GetTickCount64()-started>5000)throw std::runtime_error("mirror-bootstrap-discovery-timeout");},
            [](const auto& candidate){return sameWindowsUserAndSession(candidate.pid);},
            [&](const auto& candidate)->std::optional<std::string>{
                auto probe=std::make_shared<HttpHandle>(WinHttpConnect(session->h,candidate.host.c_str(),candidate.port,0));
                auto parents=std::make_shared<BootstrapHttpParents>(session,probe);
                AsyncRequest lookup(WinHttpOpenRequest(probe->h,L"GET",L"/json/list",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),cancellation,deadline,false,parents);
                return bootstrapRequestRetired(lookup,deadline,[&](AsyncRequest& request)->std::optional<std::string>{
                    request.handshake();const auto status=request.status();if(status!=200)throw AsyncFailure("mirror-bootstrap-targets-status",status,ymcc::steamfailure::SourceStage::Discovery,ymcc::steamfailure::Domain::Http);
                    return targetPath(nlohmann::json::parse(request.httpBody(1024*1024)),candidate);
                });
            },[&](const std::exception& error,bool hasSelection){
                bootstrapDiscoveryRejected(error,hasSelection,discoveryFailure);
            });
        if(!selected.endpoint)throw std::runtime_error("mirror-bootstrap-context-unavailable");
        discoveryFailure=nullptr; // A recovered alias cannot label a later phase with its old failure.
        const auto endpoint=*selected.endpoint;
        const auto& target=*selected.payload;
        auto observed=std::make_shared<ObservedProcess>(endpoint.pid);
        if(!stillOwned(steam,endpoint,expectedSteamPid) || !observed->sameCurrentUserAndSession())throw std::runtime_error("mirror-bootstrap-owner-mismatch");
        auto connection=std::make_shared<HttpHandle>(WinHttpConnect(session->h,endpoint.host.c_str(),endpoint.port,0));
        auto parents=std::make_shared<BootstrapHttpParents>(session,connection);
        AsyncRequest request(WinHttpOpenRequest(connection->h,L"GET",widenAscii(target).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),cancellation,deadline,false,parents);
        auto socket=bootstrapRequestRetired(request,deadline,[&](AsyncRequest& upgrade){
            upgrade.handshake(true);const auto upgradeStatus=upgrade.status();if(upgradeStatus!=101)throw AsyncFailure("mirror-bootstrap-upgrade-status",upgradeStatus,ymcc::steamfailure::SourceStage::Upgrade,ymcc::steamfailure::Domain::Http);
            checkpoint();
            return std::make_shared<AsyncRequest>(WinHttpWebSocketCompleteUpgrade(upgrade.handle(),0),cancellation,deadline,true,parents);
        });
        if(!stillOwned(steam,endpoint,expectedSteamPid) || !observed->sameCurrentUserAndSession())throw std::runtime_error("mirror-bootstrap-owner-mismatch");
        const std::string expression="(() => { if (window !== window.top || location.origin !== 'https://steamloopback.host') return; const value = "+bootstrap.dump()+"; if(value.accountSource==='verified-steam-ui' && !(window.__YMCC_DECKY_ACCOUNT_WATCH__?.version===1 && window.__YMCC_DECKY_ACCOUNT_WATCH__.read(value.account))) return 'ymcc-decky-account-not-current'; Object.defineProperty(window, '__YMCC_DECKY_MIRROR__', { value: Object.freeze(value), configurable: true, writable: false }); window.dispatchEvent(new Event('ymcc-decky-bootstrap')); ("+std::string(frontendUpdateScript())+")(value); ("+std::string(healthScript())+")({...value,loaderVersion:\""+std::string(ymcc::deckyidentity::version)+"\"}); if(value.accountSource==='verified-steam-ui' && window.__YMCC_DECKY_HEALTH__?.accountCurrent()!==true) return 'ymcc-decky-account-watch-unavailable'; return value.runId; })()";
        const std::string launchExpression = requireFreshContext
            ? "(() => { if (window !== window.top || location.origin !== 'https://steamloopback.host' || window.deckyHasLoaded || window.DeckyPluginLoader) return 'ymcc-decky-context-already-loaded'; return (" + expression + "); })()"
            : expression;
        // Ordinary rebinding accepts only the exact active module. A native
        // same-Loader update also pins its observed prior build; the existing
        // frontend updater imports only YMCC through the public plugin API.
        // The watch keeps both pins so a concurrent successful import stays valid.
        // findIndex visits sparse slots; some alone can skip holes or short-circuit before a bad slot.
        const std::string retainedGate = "(() => { const p=window.__YMCC_DECKY_FRONTEND__, l=window.DeckyPluginLoader; if(window!==window.top || location.origin!=='https://steamloopback.host' || p?.active!==true || (p.buildId!==" + nlohmann::json(bootstrap.value("frontendBuildId",std::string{})).dump() + " && p.buildId!==" + nlohmann::json(bootstrap.value("retainedFrontendBuildId",bootstrap.value("frontendBuildId",std::string{}))).dump() + ") || (p.buildId!==" + nlohmann::json(bootstrap.value("frontendBuildId",std::string{})).dump() + " && window.DeckyBackend?.ws?.readyState!==1)) return 'ymcc-decky-retained-frontend-mismatch'; const rows=l?.plugins; if(!Array.isArray(rows) || rows.findIndex(x=>x==null)!==-1 || !rows.some(x=>x.name==='ymcc-sidebar') || l.deckyState?.publicState?.()?.versionInfo?.current!==" + nlohmann::json(std::string(ymcc::deckyidentity::version)).dump() + ") return 'ymcc-decky-retained-frontend-mismatch'; return (" + expression + "); })()";
        const std::string guardedExpression = retainedFrontendOnly ? retainedGate : launchExpression;
        const auto message=nlohmann::json{{"id",8101},{"method","Runtime.evaluate"},{"params",{{"expression",guardedExpression},{"returnByValue",true}}}}.dump();
        checkpoint(); // Recheck the pinned session immediately before any injection.
        sent=true;socket->sendText(message);
        unsigned packets=0;
        for(;;){
            const auto reply=nlohmann::json::parse(socket->receiveText(65536));
            if(++packets>128)throw std::runtime_error("mirror-bootstrap-reply-limit");
            if(reply.value("id",0)!=8101)continue;
            const auto readback=reply.value("result",nlohmann::json::object()).value("result",nlohmann::json::object()).value("value",std::string{});
            if(readback=="ymcc-decky-account-not-current"){sent=false;throw std::runtime_error("mirror-bootstrap-session-changed");} // Exact guard precedes all credential writes.
            if(readback=="ymcc-decky-account-watch-unavailable")throw std::runtime_error("mirror-bootstrap-account-changed");
            if (reply.contains("result") && reply["result"].value("result",nlohmann::json::object()).value("value",std::string{}) == "ymcc-decky-retained-frontend-mismatch")
                throw std::runtime_error("mirror-retained-frontend-changed");
            if (reply.contains("result") && reply["result"].value("result",nlohmann::json::object()).value("value",std::string{}) == "ymcc-decky-context-already-loaded")
                throw std::runtime_error("decky-context-already-loaded");
            if(reply.contains("error")||(reply.contains("result")&&reply["result"].contains("exceptionDetails"))){
                try{steamFailure=ymcc::steamfailure::cdp(reply);}catch(...){} // Diagnostics never alter the original outcome.
            }
            if(reply.contains("error")||!reply.contains("result")||reply["result"].contains("exceptionDetails")||
                reply["result"].value("result",nlohmann::json::object()).value("value",std::string{})!=bootstrap["runId"].get<std::string>())
                throw std::runtime_error("mirror-bootstrap-readback-failed");
            break;
        }
        if(!observed->alive()||!stillOwned(steam,endpoint,expectedSteamPid)||!observed->alive())throw std::runtime_error("mirror-bootstrap-context-exited");
        auto success=nlohmann::json{{"ok",true},{"mutated",true},{"contextPid",observed->pid},{"contextCreated",observed->created}};
        success["bootstrapStage"]="injected";
        if(watch){
            // The caller and watch retire independently, but must observe the
            // same process object. Never rebind a long-lived watch by PID.
            const auto close=[](void* handle){CloseHandle(handle);};
            std::unique_ptr<void,decltype(close)> context(contextProcess?observed->duplicate():nullptr,close);
            if(contextProcess&&!context)throw std::runtime_error("mirror-bootstrap-context-watch-unavailable");
            const auto failure=watch->start(session,connection,socket,cancellation,
                [steam,endpoint,observed,expectedSteamPid,bindingCurrent]{return (!bindingCurrent||bindingCurrent())&&observed->sameCurrentUserAndSession()&&stillOwned(steam,endpoint,expectedSteamPid)&&observed->alive();},
                retainedFrontendOnly?retainedGate:expression,run,endpoint.pid,std::move(diagnostic),deadline,parents);
            if(!failure.empty())throw std::runtime_error(failure);
            success["watching"]=true;
            if(contextProcess)*contextProcess=context.release();
        }else if(contextProcess)*contextProcess=observed->release();
        success["elapsedMs"]=GetTickCount64()-operationStarted;success["deadlineMs"]=7000;
        return success;
    }catch(const std::exception& error){
        auto result=nlohmann::json{{"ok",false},{"reason",bootstrapSafeReason(error)},{"mutated",sent},{"elapsedMs",GetTickCount64()-operationStarted},{"deadlineMs",7000}};
        if(const auto* cleanup=dynamic_cast<const AsyncRetirementFailure*>(&error)){
            result["error"]=cleanup->error;result["closeAccepted"]=cleanup->closeAccepted;result["finalNotification"]=cleanup->finalNotification;
        }
        try{auto transport=bootstrapTransportFailure(error);if(transport.is_object())result["steamFailure"]=std::move(transport);else if(steamFailure.is_object())result["steamFailure"]=std::move(steamFailure);
            else if(std::string_view(error.what())=="mirror-bootstrap-context-unavailable"&&discoveryFailure.is_object())result["steamFailure"]=std::move(discoveryFailure);}catch(...){}
        return result;
    }
}
} // namespace ymcc::deckymirror



