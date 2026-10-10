#pragma once
// Additive bootstrap injection. Never edits Steam preferences or launches/restarts Steam.
#include "steam_live_cdp.h"
#include "decky_sidebar_async_http.h"
#include "decky_sidebar_bootstrap_watch.h"
namespace ymcc::deckymirror {
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
    BootstrapWatch* watch = nullptr, BootstrapWatch::Diagnostic diagnostic = {}) {
    using namespace ymcc::steamlive;
    if(contextProcess)*contextProcess=nullptr;
    if(watch)watch->stop();
    bool sent=false;
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
        const auto checkpoint=[&]{if(cancellation->canceled())throw std::runtime_error("mirror-bootstrap-canceled");if(GetTickCount64()>=deadline)throw std::runtime_error("mirror-bootstrap-deadline");};
        checkpoint();
        const auto candidates=listeners(steam);
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
        if(!stillOwned(steam,endpoint) || !sameWindowsUserAndSession(endpoint.pid))throw std::runtime_error("mirror-bootstrap-owner-mismatch");
        auto connection=std::make_shared<HttpHandle>(WinHttpConnect(session->h,endpoint.host.c_str(),endpoint.port,0));
        AsyncRequest request(WinHttpOpenRequest(connection->h,L"GET",widenAscii(target).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),cancellation,deadline);
        request.handshake(true);if(request.status()!=101)throw std::runtime_error("mirror-bootstrap-upgrade-status");
        checkpoint();
        auto socket=std::make_shared<AsyncRequest>(WinHttpWebSocketCompleteUpgrade(request.handle(),0),cancellation,deadline,true);
        if(!stillOwned(steam,endpoint) || !sameWindowsUserAndSession(endpoint.pid))throw std::runtime_error("mirror-bootstrap-owner-mismatch");
        const std::string expression="(() => { if (window !== window.top || location.origin !== 'https://steamloopback.host') return; const value = "+bootstrap.dump()+"; Object.defineProperty(window, '__YMCC_DECKY_MIRROR__', { value: Object.freeze(value), configurable: true, writable: false }); window.dispatchEvent(new Event('ymcc-decky-bootstrap')); return value.runId; })()";
        const auto message=nlohmann::json{{"id",8101},{"method","Runtime.evaluate"},{"params",{{"expression",expression},{"returnByValue",true}}}}.dump();
        sent=true;socket->sendText(message);
        unsigned packets=0;
        for(;;){
            const auto reply=nlohmann::json::parse(socket->receiveText(65536));
            if(++packets>128)throw std::runtime_error("mirror-bootstrap-reply-limit");
            if(reply.value("id",0)!=8101)continue;
            if(reply.contains("error")||!reply.contains("result")||reply["result"].contains("exceptionDetails")||
                reply["result"].value("result",nlohmann::json::object()).value("value",std::string{})!=bootstrap["runId"].get<std::string>())
                throw std::runtime_error("mirror-bootstrap-readback-failed");
            break;
        }
        if(!observed.alive()||!stillOwned(steam,endpoint))throw std::runtime_error("mirror-bootstrap-context-exited");
        auto success=nlohmann::json{{"ok",true},{"mutated",true},{"contextPid",observed.pid},{"contextCreated",observed.created}};
        success["bootstrapStage"]="injected";
        if(watch){
            auto lease=std::make_shared<ObservedProcess>(endpoint.pid);
            const auto failure=watch->start(session,connection,socket,cancellation,
                [steam,endpoint,lease]{return lease->alive()&&sameWindowsUserAndSession(endpoint.pid)&&stillOwned(steam,endpoint);},
                expression,run,endpoint.pid,std::move(diagnostic),deadline);
            if(!failure.empty())throw std::runtime_error(failure);
            success["watching"]=true;
        }
        if(contextProcess)*contextProcess=observed.release();
        return success;
    }catch(const std::exception& error){return {{"ok",false},{"reason",bootstrapSafeReason(error)},{"mutated",sent}};}
}
} // namespace ymcc::deckymirror



