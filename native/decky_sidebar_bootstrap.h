#pragma once
// Additive bootstrap injection. Never edits Steam preferences or launches/restarts Steam.
#include "steam_live_cdp.h"
#include "decky_sidebar_async_http.h"
#include "decky_sidebar_bootstrap_watch.h"
#include "decky_sidebar_frontend_update_script.h"
#include "decky_sidebar_health_script.h"
#include "decky_loader_identity.h"
#include <fstream>
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
// Path ownership alone is insufficient when several Windows users share one Steam install.
inline bool sameWindowsUserAndSession(DWORD pid) {
    DWORD ours = 0, theirs = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &ours) || !ProcessIdToSessionId(pid, &theirs) || ours != theirs) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    HANDLE ownToken = nullptr, peerToken = nullptr;
    const bool opened = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &ownToken) && OpenProcessToken(process, TOKEN_QUERY, &peerToken);
    CloseHandle(process);
    bool equal = false;
    if (opened) {
        DWORD ownSize = 0, peerSize = 0;
        GetTokenInformation(ownToken, TokenUser, nullptr, 0, &ownSize);
        GetTokenInformation(peerToken, TokenUser, nullptr, 0, &peerSize);
        if (ownSize && peerSize && ownSize <= 65536 && peerSize <= 65536) {
            std::vector<unsigned char> own(ownSize), peer(peerSize);
            if (GetTokenInformation(ownToken, TokenUser, own.data(), ownSize, &ownSize) &&
                GetTokenInformation(peerToken, TokenUser, peer.data(), peerSize, &peerSize))
                equal = EqualSid(reinterpret_cast<const TOKEN_USER*>(own.data())->User.Sid, reinterpret_cast<const TOKEN_USER*>(peer.data())->User.Sid) != FALSE;
        }
    }
    if (ownToken) CloseHandle(ownToken); if (peerToken) CloseHandle(peerToken); return equal;
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
    HANDLE release(){const auto value=handle_;handle_=nullptr;return value;}
};
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
    const auto operationStarted=GetTickCount64();
    try {
        if(!bootstrap.is_object() || !bootstrap.contains("endpoint") || !bootstrap.contains("token") || !bootstrap.contains("runId"))
            throw std::runtime_error("mirror-bootstrap-invalid");
        if (!bootstrap["endpoint"].is_string() || !bootstrap["token"].is_string() || !bootstrap["runId"].is_string())
            throw std::runtime_error("mirror-bootstrap-invalid");
        const auto token = bootstrap["token"].get<std::string>(), run = bootstrap["runId"].get<std::string>();
        if (token.size()!=64 || run.size()!=32 || token.find_first_not_of("0123456789abcdef")!=std::string::npos || run.find_first_not_of("0123456789abcdef")!=std::string::npos)
            throw std::runtime_error("mirror-bootstrap-invalid");
        if(!cancellation)cancellation=std::make_shared<CancelToken>();
        const auto deadline=GetTickCount64()+7000;
        const auto checkpoint=[&]{
            if(cancellation->canceled())throw std::runtime_error("mirror-bootstrap-canceled");
            if(bindingCurrent&&!bindingCurrent())throw std::runtime_error("mirror-bootstrap-session-changed");
            if(GetTickCount64()>=deadline)throw std::runtime_error("mirror-bootstrap-deadline");
        };
        checkpoint();
        const auto candidates=listeners(steam,expectedSteamPid);
        auto session=std::make_shared<HttpHandle>(WinHttpOpen(L"YMCC-DeckyMirror/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        WinHttpSetTimeouts(session->h,1000,1000,1500,1500);
        std::optional<Endpoint> selected;std::string target;
        const auto started=GetTickCount64();
        for(const auto& candidate:candidates) {
            checkpoint();if(GetTickCount64()-started>5000)throw std::runtime_error("mirror-bootstrap-discovery-timeout");
            if (!sameWindowsUserAndSession(candidate.pid)) continue;
            try {
                HttpHandle probe(WinHttpConnect(session->h,candidate.host.c_str(),candidate.port,0));
                AsyncRequest lookup(WinHttpOpenRequest(probe.h,L"GET",L"/json/list",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),cancellation,deadline);
                lookup.handshake();if(lookup.status()!=200)throw std::runtime_error("mirror-bootstrap-targets-status");
                const auto path=targetPath(nlohmann::json::parse(lookup.httpBody(1024*1024)),candidate);
                if(selected && (selected->pid!=candidate.pid || selected->port!=candidate.port))throw std::runtime_error("mirror-bootstrap-ambiguous");
                selected=candidate;target=path;
            }catch(const std::exception& error){
                const std::string reason=error.what();
                if(reason=="mirror-bootstrap-ambiguous" || reason=="live-context-ambiguous" || reason=="live-targets-invalid" ||
                   reason=="mirror-bootstrap-canceled" || reason=="mirror-bootstrap-deadline")throw;
            }
        }
        if(!selected)throw std::runtime_error("mirror-bootstrap-context-unavailable");
        const auto endpoint=*selected;
        ObservedProcess observed(endpoint.pid);
        if(!stillOwned(steam,endpoint,expectedSteamPid) || !sameWindowsUserAndSession(endpoint.pid))throw std::runtime_error("mirror-bootstrap-owner-mismatch");
        auto connection=std::make_shared<HttpHandle>(WinHttpConnect(session->h,endpoint.host.c_str(),endpoint.port,0));
        AsyncRequest request(WinHttpOpenRequest(connection->h,L"GET",widenAscii(target).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),cancellation,deadline);
        request.handshake(true);if(request.status()!=101)throw std::runtime_error("mirror-bootstrap-upgrade-status");
        checkpoint();
        auto socket=std::make_shared<AsyncRequest>(WinHttpWebSocketCompleteUpgrade(request.handle(),0),cancellation,deadline,true);
        if(!stillOwned(steam,endpoint,expectedSteamPid) || !sameWindowsUserAndSession(endpoint.pid))throw std::runtime_error("mirror-bootstrap-owner-mismatch");
        const std::string expression="(() => { if (window !== window.top || location.origin !== 'https://steamloopback.host') return; const value = "+bootstrap.dump()+"; Object.defineProperty(window, '__YMCC_DECKY_MIRROR__', { value: Object.freeze(value), configurable: true, writable: false }); window.dispatchEvent(new Event('ymcc-decky-bootstrap')); ("+std::string(frontendUpdateScript())+")(value); ("+std::string(healthScript())+")({...value,loaderVersion:\""+std::string(ymcc::deckyidentity::version)+"\"}); return value.runId; })()";
        const std::string launchExpression = requireFreshContext
            ? "(() => { if (window !== window.top || location.origin !== 'https://steamloopback.host' || window.deckyHasLoaded || window.DeckyPluginLoader) return 'ymcc-decky-context-already-loaded'; return (" + expression + "); })()"
            : expression;
        // Rebinding is allowed only to our still-active exact module in the verified
        // current Steam document. Never invokes an official Loader entry or reload.
        const std::string retainedGate = "(() => { const p=window.__YMCC_DECKY_FRONTEND__, l=window.DeckyPluginLoader; if(window!==window.top || location.origin!=='https://steamloopback.host' || p?.active!==true || p.buildId!==" + nlohmann::json(bootstrap.value("frontendBuildId",std::string{})).dump() + " || !Array.isArray(l?.plugins) || !l.plugins.some(x=>x.name==='ymcc-sidebar') || l.deckyState?.publicState?.()?.versionInfo?.current!==" + nlohmann::json(std::string(ymcc::deckyidentity::version)).dump() + ") return 'ymcc-decky-retained-frontend-mismatch'; return (" + expression + "); })()";
        const std::string guardedExpression = retainedFrontendOnly ? retainedGate : launchExpression;
        const auto message=nlohmann::json{{"id",8101},{"method","Runtime.evaluate"},{"params",{{"expression",guardedExpression},{"returnByValue",true}}}}.dump();
        checkpoint(); // Recheck the pinned session immediately before any injection.
        sent=true;socket->sendText(message);
        unsigned packets=0;
        for(;;){
            const auto reply=nlohmann::json::parse(socket->receiveText(65536));
            if(++packets>128)throw std::runtime_error("mirror-bootstrap-reply-limit");
            if(reply.value("id",0)!=8101)continue;
            if (reply.contains("result") && reply["result"].value("result",nlohmann::json::object()).value("value",std::string{}) == "ymcc-decky-retained-frontend-mismatch")
                throw std::runtime_error("mirror-retained-frontend-changed");
            if (reply.contains("result") && reply["result"].value("result",nlohmann::json::object()).value("value",std::string{}) == "ymcc-decky-context-already-loaded")
                throw std::runtime_error("decky-context-already-loaded");
            if(reply.contains("error")||!reply.contains("result")||reply["result"].contains("exceptionDetails")||
                reply["result"].value("result",nlohmann::json::object()).value("value",std::string{})!=bootstrap["runId"].get<std::string>())
                throw std::runtime_error("mirror-bootstrap-readback-failed");
            break;
        }
        if(!observed.alive()||!stillOwned(steam,endpoint,expectedSteamPid))throw std::runtime_error("mirror-bootstrap-context-exited");
        auto success=nlohmann::json{{"ok",true},{"mutated",true},{"contextPid",observed.pid},{"contextCreated",observed.created}};
        success["bootstrapStage"]="injected";
        if(watch){
            auto lease=std::make_shared<ObservedProcess>(endpoint.pid);
            const auto failure=watch->start(session,connection,socket,cancellation,
                [steam,endpoint,lease,expectedSteamPid,bindingCurrent]{return (!bindingCurrent||bindingCurrent())&&lease->alive()&&sameWindowsUserAndSession(endpoint.pid)&&stillOwned(steam,endpoint,expectedSteamPid);},
                retainedFrontendOnly?retainedGate:expression,run,endpoint.pid,std::move(diagnostic),deadline);
            if(!failure.empty())throw std::runtime_error(failure);
            success["watching"]=true;
        }
        if(contextProcess)*contextProcess=observed.release();
        success["elapsedMs"]=GetTickCount64()-operationStarted;success["deadlineMs"]=7000;
        return success;
    }catch(const std::exception& error){return {{"ok",false},{"reason",bootstrapSafeReason(error)},{"mutated",sent},{"elapsedMs",GetTickCount64()-operationStarted},{"deadlineMs",7000}};}
}
} // namespace ymcc::deckymirror



