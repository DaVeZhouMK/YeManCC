// Actual WinHTTP + WebSocket event fixture; no Steam/Loader/product/config/hardware access.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <atomic>
// Actual API in normal cases. One explicit diagnostic edge injects only a
// failed status query, never callbacks, socket IO, or cancellation completion.
static std::atomic<bool> closeQueryRefuse{false};
static std::atomic<unsigned> closeQueryCalls{0};
static DWORD WINAPI fixtureCloseQuery(HINTERNET handle,USHORT* status,void* reason,DWORD capacity,DWORD* received){
    ++closeQueryCalls;
    if(closeQueryRefuse.load()){if(status)*status=4999;if(received)*received=0;return ERROR_INVALID_OPERATION;}
    return ::WinHttpWebSocketQueryCloseStatus(handle,status,reason,capacity,received);
}
static std::atomic<HINTERNET> refuseHandle{nullptr};
static std::atomic<unsigned> refusedCloseCalls{0};
static std::atomic<bool> holdFinalNotification{false},finalNotificationHeld{false};
static std::atomic<WINHTTP_STATUS_CALLBACK> downstreamCallback{nullptr};
static std::atomic<HINTERNET> constructorTarget{nullptr};
static std::atomic<bool> constructorCloseAccepted{false};
static std::atomic<bool> constructorContextFailure{false},constructorCallbackFailure{false};
static std::atomic<bool> releaseFinalNotification{false},constructorFinalForwarded{false};
static void captureConstructorContext(HINTERNET handle);
static void captureWorkerScope(HINTERNET handle);
static std::atomic<unsigned> workerScopeMode{0}; // 0 ordinary,1 normal,2 deny,3 callback,4 context
static std::atomic<bool> workerScopeTargetSelected{false};
static std::atomic<HINTERNET> workerScopeSocket{nullptr};
static std::atomic<unsigned> workerScopeFragmentReads{0},workerScopeFinalReads{0};
static std::atomic<bool> workerScopePartialOwned{false};
static HANDLE workerScopeFragmentSignal=nullptr;
static void observeWorkerFragment(HINTERNET handle,DWORD status,void* information,DWORD length);
static void CALLBACK fixtureStatusCallback(HINTERNET handle,DWORD_PTR context,DWORD status,void* information,DWORD length){
    if(handle==refuseHandle.load()&&holdFinalNotification.load()&&status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){
        finalNotificationHeld=true;
        while(!releaseFinalNotification.load())Sleep(1); // Bounded constructor probe may release; old stalled-child case never does.
        if(const auto callback=downstreamCallback.load())callback(handle,context,status,information,length);
        constructorFinalForwarded=true;return;
    }
    if(const auto callback=downstreamCallback.load())callback(handle,context,status,information,length);
    observeWorkerFragment(handle,status,information,length);
}
static WINHTTP_STATUS_CALLBACK WINAPI fixtureSetCallback(HINTERNET handle,WINHTTP_STATUS_CALLBACK callback,DWORD flags,DWORD_PTR reserved){
    downstreamCallback=callback;
    if(handle==constructorTarget.load()&&constructorCallbackFailure.load()){
        SetLastError(ERROR_ACCESS_DENIED);return WINHTTP_INVALID_STATUS_CALLBACK;
    }
    return ::WinHttpSetStatusCallback(handle,fixtureStatusCallback,flags,reserved);
}
static BOOL WINAPI fixtureCloseHandle(HINTERNET handle){
    if(handle&&handle==refuseHandle.load()){
        ++refusedCloseCalls;
        if(!holdFinalNotification.load()){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    }
    const auto accepted=::WinHttpCloseHandle(handle);
    if(handle==constructorTarget.load())constructorCloseAccepted=accepted!=FALSE;
    return accepted;
}
static BOOL WINAPI fixtureSetOption(HINTERNET handle,DWORD option,void* value,DWORD bytes){
    if(workerScopeMode.load()&&option==WINHTTP_OPTION_CONTEXT_VALUE&&!workerScopeTargetSelected.exchange(true)){
        constructorTarget=handle;
        if(workerScopeMode.load()==2)refuseHandle=handle;
        if(workerScopeMode.load()==3)constructorCallbackFailure=true;
        if(workerScopeMode.load()==4)constructorContextFailure=true;
    }
    const auto result=::WinHttpSetOption(handle,option,value,bytes);
    if(result&&workerScopeMode.load()&&option==WINHTTP_OPTION_CONTEXT_VALUE)captureWorkerScope(handle);
    if(result&&handle==constructorTarget.load()&&option==WINHTTP_OPTION_CONTEXT_VALUE){
        captureConstructorContext(handle);
        // Explicit ambiguous-publication edge: actual context is already set,
        // but the injected API result is failure. No product OS policy changes.
        if(constructorContextFailure.load()){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    }
    return result;
}
#include "../native/steam_live_cdp.h"
// Only these two discovery ports are adapted. HTTP framing, request retirement,
// parser, SteamLease, worker and cancellation remain the actual private source.
static std::wstring workerScopeRoot;
static DWORD workerScopeSteamPid=0;
static unsigned short workerScopePort=0;
static std::atomic<unsigned> workerScopeDiscoveryReads{0},workerScopeOwnerReads{0};
static std::vector<ymcc::steamlive::Endpoint> fixtureWorkerListeners(const std::wstring& root,DWORD expected){
    ++workerScopeDiscoveryReads;
    if(ymcc::steamlive::normalizedPath(root)!=workerScopeRoot||expected!=workerScopeSteamPid||!workerScopePort)throw std::runtime_error("live-debug-unavailable");
    return {{GetCurrentProcessId(),workerScopePort,L"127.0.0.1"}};
}
static bool fixtureWorkerStillOwned(const std::wstring& root,const ymcc::steamlive::Endpoint& endpoint,DWORD expected){
    ++workerScopeOwnerReads;
    return ymcc::steamlive::normalizedPath(root)==workerScopeRoot&&expected==workerScopeSteamPid&&endpoint.pid==GetCurrentProcessId()&&endpoint.port==workerScopePort&&endpoint.host==L"127.0.0.1";
}
#define listeners fixtureWorkerListeners
#define stillOwned fixtureWorkerStillOwned
#define WinHttpSetOption fixtureSetOption
#define WinHttpSetStatusCallback fixtureSetCallback
#define WinHttpCloseHandle fixtureCloseHandle
#define WinHttpWebSocketQueryCloseStatus fixtureCloseQuery
#include "../native/decky_sidebar_bootstrap.h"
#undef WinHttpWebSocketQueryCloseStatus
#undef WinHttpCloseHandle
#undef WinHttpSetStatusCallback
#undef WinHttpSetOption
#undef stillOwned
#undef listeners
#include "../native/decky_binding_process_owner.h"
#include "../native/decky_binding_worker.h"
#include "../native/decky_sidebar_broker.h"
#include "../native/decky_sidebar_diagnostics.h"
#include "../native/decky_sidebar_recovery.h"
#include <condition_variable>
#include <fstream>
#include <iostream>
using namespace ymcc::deckymirror;
using Json=nlohmann::json;
static std::shared_ptr<AsyncState> constructorSafety;
static std::weak_ptr<AsyncState> constructorObserved;
static std::vector<std::weak_ptr<AsyncState>> workerScopeStates;
static std::vector<std::weak_ptr<ymcc::steamlive::HttpHandle>> workerScopeParents;
static void captureWorkerScope(HINTERNET handle){
    DWORD_PTR context=0;DWORD size=sizeof(context);
    if(!::WinHttpQueryOption(handle,WINHTTP_OPTION_CONTEXT_VALUE,&context,&size)||!context)throw std::runtime_error("worker scope context query");
    auto* state=reinterpret_cast<AsyncState*>(context);std::lock_guard lock(state->mutex);
    workerScopeStates.emplace_back(state->callbackLifetime);
    const auto parents=std::static_pointer_cast<BootstrapHttpParents>(state->parentLifetime);
    if(!parents)throw std::runtime_error("worker scope parent holder");
    workerScopeParents.emplace_back(parents->session);workerScopeParents.emplace_back(parents->connection);
    if(state->websocket)workerScopeSocket=handle;
}
static void observeWorkerFragment(HINTERNET handle,DWORD status,void* information,DWORD length){
    const auto mode=workerScopeMode.load();
    if(mode<5||handle!=workerScopeSocket.load()||status!=WINHTTP_CALLBACK_STATUS_READ_COMPLETE||!information||length<sizeof(WINHTTP_WEB_SOCKET_STATUS))return;
    const auto* completion=reinterpret_cast<const WINHTTP_WEB_SOCKET_STATUS*>(information);
    if(completion->eBufferType==WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE){
        if(const auto state=workerScopeStates.back().lock()){
            std::lock_guard lock(state->mutex);workerScopePartialOwned=bool(state->callbackLifetime)&&bool(state->parentLifetime);
        }
        const auto count=++workerScopeFragmentReads;
        if(count>=2&&mode!=7&&workerScopeFragmentSignal)SetEvent(workerScopeFragmentSignal);
    }else if(completion->eBufferType==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE&&workerScopeFragmentReads>=2){
        ++workerScopeFinalReads;if(mode==7&&workerScopeFragmentSignal)SetEvent(workerScopeFragmentSignal);
    }
}
static void captureConstructorContext(HINTERNET handle){
    DWORD_PTR context=0;DWORD bytes=sizeof(context);
    if(!::WinHttpQueryOption(handle,WINHTTP_OPTION_CONTEXT_VALUE,&context,&bytes)||!context)throw std::runtime_error("constructor context probe");
    auto* state=reinterpret_cast<AsyncState*>(context);
    std::lock_guard lock(state->mutex);
    constructorSafety=state->callbackLifetime;constructorObserved=constructorSafety;
    if(!constructorSafety)throw std::runtime_error("constructor missing published self lease");
}
static std::mutex capturedExpressionMutex;
static std::vector<std::string> capturedExpressions;
static std::atomic<unsigned> contextReplayMode{0},callbackReplayMode{0};
class CdpFixture {
    SOCKET listener_=INVALID_SOCKET,peer_=INVALID_SOCKET;
    std::thread worker_;
    std::mutex send_,state_;
    std::condition_variable changed_;
    std::vector<int> evaluated_;
    std::atomic<bool> workerFragmentsSent_{false};
public:
    unsigned short port=0;
    std::string run=std::string(32,'b');
    std::atomic<unsigned> enables{0},teardowns{0},healthReads{0},accountChecks{0};
    std::atomic<bool> httpHeadersRead{false};
    std::atomic<bool> boundary{false},bootMissing{true},mirrorCurrent{true};std::atomic<bool> accountRevoked{false},revokeOnTeardown{false};
    explicit CdpFixture(bool initialBoundary=false,bool currentMirror=true,bool initialRevoked=false,bool revokeAtMutation=false,unsigned httpStatus=0,bool silentHttp=false,bool fullBootstrap=false):boundary(initialBoundary),mirrorCurrent(currentMirror),accountRevoked(initialRevoked),revokeOnTeardown(revokeAtMutation){
        listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in address{};
        address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(listener_,1))throw std::runtime_error("fixture listen");
        int length=sizeof(address);getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&length);port=ntohs(address.sin_port);
        worker_=std::thread([this,httpStatus,silentHttp,fullBootstrap]{
            auto peer=accept(listener_,nullptr,nullptr);if(peer==INVALID_SOCKET)return;
            {std::lock_guard lock(send_);peer_=peer;}
            try{
                std::string headers;char bytes[8192];
                while(headers.find("\r\n\r\n")==std::string::npos){const int count=recv(peer,bytes,sizeof(bytes),0);if(count<=0)return;headers.append(bytes,count);}
                httpHeadersRead=true;
                if(fullBootstrap){
                    const auto body=Json::array({{{"type","page"},{"title","SharedJSContext"},{"url","https://steamloopback.host/index.html"},
                        {"webSocketDebuggerUrl","ws://127.0.0.1:"+std::to_string(port)+"/devtools/page/fixture"}}}).dump();
                    const auto response="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
                    ::send(peer,response.data(),static_cast<int>(response.size()),0);
                    {std::lock_guard lock(send_);shutdown(peer,SD_BOTH);closesocket(peer);peer_=INVALID_SOCKET;}
                    peer=accept(listener_,nullptr,nullptr);if(peer==INVALID_SOCKET)return;
                    {std::lock_guard lock(send_);peer_=peer;}
                    headers.clear();
                    while(headers.find("\r\n\r\n")==std::string::npos){const int count=recv(peer,bytes,sizeof(bytes),0);if(count<=0)return;headers.append(bytes,count);}
                }
                if(silentHttp){char ignored;while(recv(peer,&ignored,1,0)>0){}return;}
                if(httpStatus){
                    const std::string body="fixture-http-response";
                    const auto reply="HTTP/1.1 "+std::to_string(httpStatus)+" Fixture\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
                    ::send(peer,reply.data(),static_cast<int>(reply.size()),0);return;
                }
                const std::string marker="sec-websocket-key:";const auto begin=lower(headers).find(marker),end=headers.find("\r\n",begin);
                const auto key=trim(headers.substr(begin+marker.size(),end-begin-marker.size()));
                const auto response="HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Accept: "+websocketAccept(key)+"\r\n\r\n";
                ::send(peer,response.data(),static_cast<int>(response.size()),0);
                std::vector<unsigned char> buffer;
                for(;;){
                    const int count=recv(peer,bytes,sizeof(bytes),0);if(count<=0)break;buffer.insert(buffer.end(),bytes,bytes+count);
                    for(;;){
                        const auto frame=decode(buffer);if(!frame.complete)break;
                        buffer.erase(buffer.begin(),buffer.begin()+frame.bytes);if(frame.opcode!=1)continue;
                        const auto request=Json::parse(frame.payload);const auto method=request.value("method",std::string{});const auto id=request.at("id");
                        if(method=="Runtime.enable"){
                            ++enables;
                            const auto replay=contextReplayMode.exchange(0);
                            if(replay==1)for(unsigned i=0;i<128;++i)created(1000+static_cast<int>(i),i%2?"https://steamloopback.host":"https://example.invalid",i%2==0);
                            if(replay==2)for(unsigned i=0;i<40;++i)created(2000+static_cast<int>(i));
                            created(101);for(unsigned i=0;i<192;++i)send({{"method","Runtime.consoleAPICalled"},{"params",{{"type","log"},{"args",Json::array()}}}});send({{"id",id},{"result",Json::object()}});
                        }else if(method=="Runtime.addBinding"){send({{"id",id},{"result",Json::object()}});
                        }else if(method=="Runtime.evaluate"){
                            const auto expressionForBootstrap=request.at("params").value("expression",std::string{});
                            if(fullBootstrap&&expressionForBootstrap.find("ymcc-decky-bootstrap")!=std::string::npos){
                                send({{"id",id},{"result",{{"result",{{"type","string"},{"value",run}}}}}});
                                if(request.at("params").contains("contextId")&&workerScopeMode.load()>=5&&!workerFragmentsSent_.exchange(true))sendWorkerFragments();
                                continue;
                            }
                            const int context=request.at("params").at("contextId");
                            const auto expression=request.at("params").value("expression",std::string{});
                            if(expression.find("dismountAll")!=std::string::npos){std::lock_guard lock(capturedExpressionMutex);if(std::find(capturedExpressions.begin(),capturedExpressions.end(),expression)==capturedExpressions.end())capturedExpressions.push_back(expression);}
                            if(expression=="window.__YMCC_DECKY_HEALTH__?.read() || ''"){
                                ++healthReads;const auto replay=callbackReplayMode.exchange(0);
                                if(replay==1)for(unsigned i=0;i<128;++i){
                                    if(i%3==0)send({{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",context},{"payload",Json{{"runId",std::string(32,'f')},{"code","decky-render-failed"}}.dump()}}}});
                                    else if(i%3==1)send({{"method","Runtime.exceptionThrown"},{"params",{{"exceptionDetails",{{"executionContextId",context},{"exception",{{"subtype","error"},{"className","TypeError"},{"description","Cannot read properties of undefined"}}},{"stackTrace",{{"callFrames",Json::array({{{"url","http://127.0.0.1:1337/plugins/other/dist/index.js"}}})}}}}}}}});
                                    else send({{"method","Runtime.executionContextDestroyed"},{"params",{{"executionContextId",9000+static_cast<int>(i)}}}});
                                }
                                if(replay==6){
                                    const auto facts=Json{{"runId",run},{"code","decky-socket-close"},{"ordinal",1},{"codeKnown",true},{"closeCode",1006},{"cleanKnown",true},{"wasClean",false},{"opened",true}};
                                    for(unsigned n=0;n<1000;++n)send({{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",context},{"payload",facts.dump()}}}});
                                }
                                if(replay==2){created(3020);send({{"method","Runtime.executionContextDestroyed"},{"params",{{"executionContextId",3020}}}});}
                                if(replay==4)for(unsigned i=0;i<128;++i){
                                    const int foreign=6000+static_cast<int>(i);created(foreign,"https://example.invalid");
                                    if(i%2==0)send({{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",foreign},{"payload",Json{{"runId",run},{"code","decky-account-changed"}}.dump()}}}});
                                    else send({{"method","Runtime.exceptionThrown"},{"params",{{"exceptionDetails",{{"executionContextId",foreign},{"exception",{{"subtype","error"},{"className","TypeError"},{"description","Cannot read properties of undefined"}}},{"stackTrace",{{"callFrames",Json::array({{{"url","http://127.0.0.1:1337/frontend/index.js"}}})}}}}}}}});
                                }
                                if(replay==5){
                                    const auto hint=[&](int target,const char* code,bool oversize=false){
                                        auto payload=Json{{"runId",run},{"code",code}}.dump();
                                        if(oversize)payload.insert(0,200-payload.size(),' ');
                                        send({{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",target},{"payload",payload}}}});
                                    };
                                    created(106);
                                    for(unsigned n=0;n<128;++n)hint(106,n%2?"decky-connection-result":"decky-connection-check");
                                    for(unsigned n=0;n<64;++n)hint(106,"decky-connection-result",true);
                                    created(108);
                                    hint(108,"decky-connection-check");hint(108,"decky-connection-result");
                                }
                                if(replay==3){auto payload=Json{{"runId",run},{"code","decky-account-changed"}}.dump();payload=std::string(200-payload.size(),' ')+payload;
                                    send({{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",context},{"payload",payload}}}});
                                }
                                send({{"id",id},{"result",{{"result",{{"type","string"},{"value",boundary.load()?"decky-render-failed":""}}}}}});continue;}
                            if(expression=="window.__YMCC_DECKY_HEALTH__?.accountCurrent() === false"){++accountChecks;send({{"id",id},{"result",{{"result",{{"type","boolean"},{"value",accountRevoked.load()}}}}}});continue;}
                            if(expression=="!window.DeckyPluginLoader"){send({{"id",id},{"result",{{"result",{{"type","boolean"},{"value",bootMissing.load()}}}}}});continue;}
                            if(expression.find("===")!=std::string::npos||expression.find("dismountAll")!=std::string::npos){const bool teardown=expression.find("dismountAll")!=std::string::npos;if(teardown&&revokeOnTeardown.exchange(false))accountRevoked=true;const bool current=mirrorCurrent.load()&&(expression.find("revoked")==std::string::npos||!accountRevoked.load());const bool accepted=teardown?(expression.find("revoked")==std::string::npos||current):current;if(teardown&&accepted)++teardowns;send({{"id",id},{"result",{{"result",{{"type","boolean"},{"value",accepted}}}}}});continue;}
                            {std::lock_guard lock(state_);evaluated_.push_back(context);changed_.notify_all();}
                            if(context==400)continue; // Silent reply cancellation fixture.
                            if(context==300||context==3020){send({{"id",id},{"error",{{"code",-32000},{"message","Cannot find context private-url-token"}}}});continue;}
                            if(context==305){send({{"id",id},{"result",{{"exceptionDetails",{{"text","TypeError: cannot read properties of undefined private-url-token"},{"lineNumber",17},{"columnNumber",9},{"exception",{{"className","TypeError"},{"description","secret stack account token"}}}}}}}});continue;}
                            send({{"id",id},{"result",{{"result",{{"type","string"},{"value",run}}}}}});
                        }else throw std::runtime_error("unexpected command");
                    }
                }
            }catch(...){}
        });
    }
    ~CdpFixture(){
        {std::lock_guard lock(send_);if(peer_!=INVALID_SOCKET)shutdown(peer_,SD_BOTH);}
        closesocket(listener_);if(worker_.joinable())worker_.join();if(peer_!=INVALID_SOCKET)closesocket(peer_);
    }
    void sendWorkerFragment(const std::string& payload,unsigned opcode,bool final){
        auto frame=encode(payload);frame[0]=static_cast<char>((final?0x80:0)|opcode);
        std::lock_guard lock(send_);size_t offset=0;
        while(offset<frame.size()){
            const int sent=::send(peer_,frame.data()+offset,static_cast<int>(frame.size()-offset),0);
            if(sent<=0)throw std::runtime_error("worker fragment send");offset+=sent;
        }
    }
    void sendWorkerFragments(){
        sendWorkerFragment("{\"method\":\"Runtime.consoleAPICalled\",\"params\":{\"fixture\":\""+std::string(2000,'a'),1,false);
        const auto first=GetTickCount64()+2000;
        while(workerScopeFragmentReads<1&&GetTickCount64()<first)Sleep(1);
        if(workerScopeFragmentReads<1)throw std::runtime_error("first real fragment completion absent");
        sendWorkerFragment(std::string(2000,'b'),0,false);
        const auto second=GetTickCount64()+2000;
        while(workerScopeFragmentReads<2&&GetTickCount64()<second)Sleep(1);
        if(workerScopeFragmentReads<2)throw std::runtime_error("second real fragment completion absent");
        if(workerScopeMode.load()==6)resetTransport();
        else if(workerScopeMode.load()==7)sendWorkerFragment("\"}}",0,true);
    }
    void resetTransport(){std::lock_guard lock(send_);if(peer_!=INVALID_SOCKET){linger reset{1,0};setsockopt(peer_,SOL_SOCKET,SO_LINGER,reinterpret_cast<const char*>(&reset),sizeof(reset));closesocket(peer_);peer_=INVALID_SOCKET;}}
    void closeWithStatus(unsigned code){const std::string reason="close-private-token";std::string frame;frame.push_back('\x88');frame.push_back(char(2+reason.size()));frame.push_back(char(code>>8));frame.push_back(char(code&255));frame+=reason;std::lock_guard lock(send_);if(peer_!=INVALID_SOCKET)::send(peer_,frame.data(),static_cast<int>(frame.size()),0);}
    void closeWebSocket(){const unsigned char frame[]{0x88,0};std::lock_guard lock(send_);if(peer_!=INVALID_SOCKET)::send(peer_,reinterpret_cast<const char*>(frame),sizeof(frame),0);}
    void send(const Json& value){const auto bytes=encode(value.dump());std::lock_guard lock(send_);if(peer_!=INVALID_SOCKET)::send(peer_,bytes.data(),static_cast<int>(bytes.size()),0);}
    void created(int id,const char* origin="https://steamloopback.host",bool isDefault=true){send({{"method","Runtime.executionContextCreated"},{"params",{{"context",{{"id",id},{"origin",origin},{"auxData",{{"isDefault",isDefault},{"frameId","fixture"}}}}}}}});}
    bool wait(int id){std::unique_lock lock(state_);return changed_.wait_for(lock,std::chrono::seconds(4),[&]{return std::find(evaluated_.begin(),evaluated_.end(),id)!=evaluated_.end();});}
    size_t count(){std::lock_guard lock(state_);return evaluated_.size();}
};
struct WatchFixture {
    std::weak_ptr<AsyncState> observedSocket;
    std::weak_ptr<ymcc::steamlive::HttpHandle> observedSession,observedConnection;
    bool parentAdopted=false,upgradeFinalBeforeWatch=false;
    HINTERNET observedHandle=nullptr;std::atomic<bool> failInvalidationLog{false},failOptionalLog{false};
    CdpFixture cdp;BootstrapWatch watch;std::atomic<bool> owned{true};std::mutex mutex;std::condition_variable changed;std::vector<Json> records;std::string startFailure;
    explicit WatchFixture(bool initialBoundary=false,bool currentMirror=true,bool initialRevoked=false,bool revokeAtMutation=false):cdp(initialBoundary,currentMirror,initialRevoked,revokeAtMutation){
        auto session=std::make_shared<ymcc::steamlive::HttpHandle>(WinHttpOpen(L"MirrorWatchFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        WinHttpSetTimeouts(session->h,1000,1000,1500,1500);
        auto connection=std::make_shared<ymcc::steamlive::HttpHandle>(WinHttpConnect(session->h,L"127.0.0.1",cdp.port,0));
        auto cancel=std::make_shared<CancelToken>();const auto deadline=GetTickCount64()+3000;
        auto parents=std::make_shared<BootstrapHttpParents>(session,connection);
        AsyncRequest request(WinHttpOpenRequest(connection->h,L"GET",L"/fixture",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),cancel,deadline,false,parents);
        auto socket=bootstrapRequestRetired(request,deadline,[&](AsyncRequest& upgrade){
            upgrade.handshake(true);return std::make_shared<AsyncRequest>(WinHttpWebSocketCompleteUpgrade(upgrade.handle(),0),cancel,deadline,true,parents);
        });
        if(const auto state=request.callbackState().lock()){std::lock_guard lock(state->mutex);upgradeFinalBeforeWatch=state->finalNotification;}
        observedHandle=socket->handle();
        observedSocket=socket->callbackState();observedSession=session;observedConnection=connection;
        startFailure=watch.start(session,connection,socket,cancel,[this]{return owned.load();},"fixture guarded readonly expression",cdp.run,GetCurrentProcessId(),[this](const Json& value){if(value.value("bootstrapStage",std::string{})=="context-invalidated"&&failInvalidationLog.exchange(false))throw std::bad_alloc();if(value.value("bootstrapStage",std::string{})=="official-socket-close"&&failOptionalLog.exchange(false))throw std::bad_alloc();std::lock_guard lock(mutex);records.push_back(value);changed.notify_all();},0,parents);
        const auto state=observedSocket.lock();
        if(state){std::lock_guard lock(state->mutex);parentAdopted=bool(state->parentLifetime);}
    }
    ~WatchFixture(){watch.stop();}
    bool stage(const char* expected,unsigned ms=4000){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::milliseconds(ms),[&]{return std::any_of(records.begin(),records.end(),[&](const auto& v){return v.value("bootstrapStage",std::string{})==expected;});});}
};
static unsigned watchTransportDiagnostics(){
    unsigned checks=0;const auto check=[&](bool condition,const char* name){if(!condition)throw std::runtime_error(name);++checks;};
    WatchFixture f;check(f.startFailure.empty()&&f.stage("context-attached"),"real watch is attached before reset");
    f.cdp.resetTransport();check(f.stage("failed"),"real reset reaches existing watch failure callback");f.watch.stop();
    std::lock_guard lock(f.mutex);const auto row=f.records.back(),failure=row.value("steamFailure",Json{});
    check(row.value("reason",std::string{})=="mirror-bootstrap-async-error"||row.value("reason",std::string{})=="mirror-bootstrap-ws-receive-failed","numeric trace preserves original socket recovery reason");
    check(failure.value("domain",std::string{})=="winhttp"&&failure.value("codeKnown",false)&&failure.value("code",DWORD{0})!=0,"real watch propagates the current transport numeric code");
    check(failure.value("sourceStage",std::string{})=="receive","passive socket failure records receive stage");
    check(f.cdp.teardowns==0&&f.cdp.enables==1,"numeric diagnostics neither tear down nor resubscribe");
    ymcc::steamfailure::FailureContext context;context.actionId=7;context.operation=ymcc::steamfailure::Operation::Binding;context.phase=ymcc::steamfailure::Phase::Observation;const auto logged=ymcc::deckydiag::failureRecord(row,context);check(logged.contains("steamFailure")&&logged.dump().size()<2304&&logged.dump().find("runId")==std::string::npos,"existing log writer keeps bounded numeric failure without credentials");
    return checks;
}
static unsigned watchCloseDiagnostics(){
    unsigned checks=0;const auto check=[&](bool condition,const char* name){if(!condition)throw std::runtime_error(name);++checks;};
    for(bool refuse:{false,true}){
        closeQueryCalls=0;closeQueryRefuse=refuse;WatchFixture f;check(f.startFailure.empty()&&f.stage("context-attached"),"close watcher starts from a real attached connection");
        f.cdp.closeWithStatus(1011);check(f.stage("failed"),"real peer close reaches existing watch failure path");f.watch.stop();
        std::lock_guard lock(f.mutex);const auto row=f.records.back(),failure=row.value("steamFailure",Json{});
        std::cout<<"WATCH_CLOSE_DIAGNOSTIC_OBSERVATION "<<Json{{"reason",row.value("reason",std::string{})},{"failure",ymcc::steamfailure::normalize(failure)}}.dump()<<"\n";
        check(row.value("reason",std::string{})=="mirror-bootstrap-ws-closed","accepted close keeps original recovery reason");
        check(failure.is_object(),"graceful close metadata missing");
        check(failure.value("domain",std::string{})=="websocket-close"&&failure.value("sourceStage",std::string{})=="receive","watch distinguishes peer close from WinHTTP error");
        check(refuse?(!failure.value("codeKnown",true)&&!failure.contains("code")):(failure.value("codeKnown",false)&&failure.value("code",0)==1011),"watch retains actual status or explicit query unknown");
        ymcc::steamfailure::FailureContext context;context.actionId=9;context.operation=ymcc::steamfailure::Operation::Binding;context.phase=ymcc::steamfailure::Phase::Observation;
        const auto logged=ymcc::deckydiag::failureRecord(row,context);check(logged.at("steamFailure")==failure&&logged.at("actionId")=="9","existing log writer links close to its original action");
        check(logged.dump().size()<2304&&logged.dump().find("close-private-token")==std::string::npos&&logged.dump().find("runId")==std::string::npos,"reason text and credentials stay outside logs");
        check(closeQueryCalls==1&&f.cdp.teardowns==0&&f.cdp.enables==1,"local status query neither probes nor restarts or resubscribes");closeQueryRefuse=false;
    }
    {
        closeQueryCalls=0;WatchFixture f;check(f.startFailure.empty()&&f.stage("context-attached"),"provider boundary starts from a real attached watch");
        f.cdp.closeWithStatus(1012);check(f.stage("failed"),"provider boundary reaches its existing failure callback");f.watch.stop();
        std::lock_guard lock(f.mutex);const auto row=f.records.back(),failure=row.value("steamFailure",Json{});const auto reason=row.value("reason",std::string{});
        std::cout<<"WATCH_CLOSE_DIAGNOSTIC_OBSERVATION "<<Json{{"reason",reason},{"failure",ymcc::steamfailure::normalize(failure)}}.dump()<<"\n";
        check(reason=="mirror-bootstrap-async-error"||reason=="mirror-bootstrap-ws-receive-failed","this provider retains original invalid-close receive reason");
        check(failure.value("domain",std::string{})=="winhttp"&&failure.value("codeKnown",false)&&failure.value("code",0)==ERROR_WINHTTP_INVALID_SERVER_RESPONSE&&failure.value("sourceStage",std::string{})=="receive","provider rejection keeps actual WinHTTP code rather than fake close status");
        check(closeQueryCalls==0&&f.cdp.teardowns==0&&f.cdp.enables==1,"rejected frame neither queries close nor restarts or resubscribes");
        ymcc::steamfailure::FailureContext context;context.actionId=10;const auto logged=ymcc::deckydiag::failureRecord(row,context);
        check(logged.at("steamFailure")==failure&&logged.at("actionId")=="10","provider error stays linked to current action");
        check(logged.dump().size()<2304&&logged.dump().find("close-private-token")==std::string::npos,"provider rejection never adds close text to logs");
    }
    return checks;
}

static int runOwnedCloseDebtChild(ymcc::deckybinding::ChildChannel& channel,bool holdFinal){
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 2;
    WatchFixture f;
    Json row{{"attached",f.startFailure.empty()&&f.stage("context-attached")}};
    holdFinalNotification=holdFinal;refuseHandle=f.observedHandle;
    Json cleanupDiagnostic;
    row["stopReturned"]=ymcc::deckybinding::worker::finishWatch(f.watch,[&](const Json& value){cleanupDiagnostic=value;});
    row["consumerDiagnostic"]=ymcc::deckydiag::record("bootstrap-watch",cleanupDiagnostic);
    const auto callbackUntil=GetTickCount64()+1500;
    while(holdFinal&&!finalNotificationHeld.load()&&GetTickCount64()<callbackUntil)Sleep(1);
    row["actualFinalCallbackHeld"]=finalNotificationHeld.load();
    const auto receipt=f.watch.retirement();
    row.update({{"closeAccepted",receipt.closeAccepted},{"finalNotification",receipt.finalNotification},
        {"cleanupError",receipt.error},{"closeCalls",refusedCloseCalls.load()},
        {"callbackAlive",!f.observedSocket.expired()},{"sessionAlive",!f.observedSession.expired()},
        {"connectionAlive",!f.observedConnection.expired()}});
    // No extra fixture owner or cleanup call: the real watch must own this debt.
    if(!row["stopReturned"].get<bool>())row["reentryFailure"]=f.watch.start({}, {}, {}, std::make_shared<CancelToken>(),[]{return true;},"",std::string(32,'a'),GetCurrentProcessId());
    else row["reentryFailure"]="not-tested-stop-returned";
    row["closeCallsAfterReentry"]=refusedCloseCalls.load();
    const auto text=row.dump();
    if(!channel.writeFrame(std::vector<std::uint8_t>(text.begin(),text.end()),2500))return 2;
    // Deliberately stalled original child boundary. This is a diagnostic fixture
    // condition, not successful production Steam/session/bootstrap admission.
    for(;;)Sleep(INFINITE);
}
static int constructorScopeChild(ymcc::deckybinding::ChildChannel& channel,bool contextFailure){
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 2;
    Json row{{"contextFailure",contextFailure},{"fixtureSafetyExcludedFromOwnership",true},{"independentLeafCleanup",false}};
    try{
        std::weak_ptr<ymcc::steamlive::HttpHandle> observedSession,observedConnection;
        {
            CdpFixture cdp;
            auto session=std::make_shared<ymcc::steamlive::HttpHandle>(WinHttpOpen(L"CtorScopeFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
            WinHttpSetTimeouts(session->h,1000,1000,1500,1500);
            auto connection=std::make_shared<ymcc::steamlive::HttpHandle>(WinHttpConnect(session->h,L"127.0.0.1",cdp.port,0));
            observedSession=session;observedConnection=connection;
            auto token=std::make_shared<CancelToken>();const auto deadline=GetTickCount64()+3000;
            {
                AsyncRequest request(WinHttpOpenRequest(connection->h,L"GET",L"/fixture",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),token,deadline);
                request.handshake(true);const auto socket=WinHttpWebSocketCompleteUpgrade(request.handle(),0);
                if(!socket)throw std::runtime_error("constructor upgrade");
                constructorTarget=socket;refuseHandle=socket;holdFinalNotification=true;
                constructorContextFailure=contextFailure;constructorCallbackFailure=!contextFailure;
                auto parents=std::make_shared<BootstrapHttpParents>(session,connection);
                try{AsyncRequest failing(socket,token,deadline,true,parents);row["constructorThrew"]=false;}
                catch(const std::exception& error){row["constructorThrew"]=true;row["constructorReason"]=error.what();}
                parents.reset();
            }
            connection.reset();session.reset();
            const auto heldUntil=GetTickCount64()+1500;
            while(!finalNotificationHeld.load()&&GetTickCount64()<heldUntil)Sleep(1);
            row["actualFinalCallbackHeld"]=finalNotificationHeld.load();
            bool productLease=false;
            if(const auto state=constructorObserved.lock()){
                std::lock_guard lock(state->mutex);productLease=bool(state->callbackLifetime);
            }
            row["productCallbackLeaseRetained"]=productLease;
            if(productLease){
                constructorSafety.reset();
                row["aliveWithoutFixtureSafety"]=!constructorObserved.expired()&&!observedSession.expired()&&!observedConnection.expired();
            }else row["aliveWithoutFixtureSafety"]=false; // Do not dereference freed state in old source.
            releaseFinalNotification=true;
            const auto releaseUntil=GetTickCount64()+1500;
            while(!constructorFinalForwarded.load()&&GetTickCount64()<releaseUntil)Sleep(1);
            row["actualFinalForwarded"]=constructorFinalForwarded.load();
            if(!productLease)constructorSafety.reset(); // Independent negative-source safety is never consumer ownership.
            const auto returnedUntil=GetTickCount64()+1500;
            while((!constructorObserved.expired()||!observedSession.expired()||!observedConnection.expired())&&GetTickCount64()<returnedUntil)Sleep(1);
            row["callbackAndParentsReturnedBeforeChildExit"]=constructorObserved.expired()&&observedSession.expired()&&observedConnection.expired();
            row["closeCalls"]=refusedCloseCalls.load();
        }
        row["passed"]=row.value("constructorThrew",false)&&row.value("actualFinalCallbackHeld",false)&&row.value("productCallbackLeaseRetained",false)&&row.value("aliveWithoutFixtureSafety",false)&&row.value("actualFinalForwarded",false)&&row.value("callbackAndParentsReturnedBeforeChildExit",false)&&row.value("closeCalls",0u)==1&&row.value("constructorReason",std::string{})==(contextFailure?"mirror-bootstrap-context-failed":"mirror-bootstrap-callback-failed");
    }catch(const std::exception& error){releaseFinalNotification=true;row["passed"]=false;row["error"]=error.what();}
    const auto text=row.dump();channel.writeFrame(std::vector<std::uint8_t>(text.begin(),text.end()),2500);
    WSACleanup();return row.value("passed",false)?0:2;
}
static int discoveryConstructorChild(ymcc::deckybinding::ChildChannel& channel){
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 2;
    Json row{{"scope","original selector+Decky rejection consumer+actual HTTP constructor; endpoint descriptors/fallback payload are inert ports"},
        {"fixtureGuardExcludedFromOwnership",true},{"independentLeafCleanup",false},{"actualHealthySteamBootstrapTested",false}};
    SOCKET listener=INVALID_SOCKET;
    try{
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener==INVALID_SOCKET)throw std::runtime_error("discovery listener");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(listener,1))throw std::runtime_error("discovery bind");
        int bytes=sizeof(address);if(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&bytes))throw std::runtime_error("discovery port");
        struct Candidate {DWORD pid;unsigned short port;unsigned route;};
        const std::vector<Candidate> candidates{{GetCurrentProcessId(),ntohs(address.sin_port),0},{GetCurrentProcessId(),ntohs(address.sin_port),1}};
        auto session=std::make_shared<ymcc::steamlive::HttpHandle>(WinHttpOpen(L"CtorDiscoveryFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        auto connection=std::make_shared<ymcc::steamlive::HttpHandle>(WinHttpConnect(session->h,L"127.0.0.1",ntohs(address.sin_port),0));
        std::weak_ptr<ymcc::steamlive::HttpHandle> weakSession=session,weakConnection=connection;
        auto token=std::make_shared<CancelToken>();auto parents=std::make_shared<BootstrapHttpParents>(session,connection);
        unsigned probes=0,rejected=0,fallback=0;bool terminated=false,selected=false;std::string reason;Json failure;
        try{
            const auto choice=ymcc::steamdiscovery::select<std::string>(ymcc::steamdiscovery::Kind::Bootstrap,candidates,[]{},[](const Candidate&){return true;},
                [&](const Candidate& candidate)->std::optional<std::string>{
                    ++probes;if(candidate.route){++fallback;return "fixture-fallback-payload";}
                    const auto handle=WinHttpOpenRequest(connection->h,L"GET",L"/fixture",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
                    if(!handle)throw std::runtime_error("discovery request handle");
                    constructorTarget=handle;refuseHandle=handle;constructorCallbackFailure=true;constructorContextFailure=false;holdFinalNotification=true;
                    try{AsyncRequest request(handle,token,GetTickCount64()+3000,false,parents);}
                    catch(...){constructorTarget=nullptr;refuseHandle=nullptr;constructorCallbackFailure=false;throw;}
                    throw std::runtime_error("construction unexpectedly succeeded");
                },[&](const std::exception& error,bool hasSelection){++rejected;bootstrapDiscoveryRejected(error,hasSelection,failure);});
            selected=choice.endpoint.has_value();
        }catch(const AsyncConstructionFailure& error){terminated=true;reason=error.what();}
        parents.reset();connection.reset();session.reset();constructorSafety.reset();
        row["fixtureGuardDropped"]=true;
        // Observation window only. No installed callback exists on this fresh
        // HTTP request; do not claim a failed API result implies a final event.
        Sleep(50);bool pending=false,finalSeen=false;
        if(const auto state=constructorObserved.lock()){
            std::lock_guard lock(state->mutex);pending=bool(state->callbackLifetime);finalSeen=state->finalNotification;
        }
        row.update({{"discoveryTerminated",terminated},{"reason",reason},{"selectedEndpoint",selected},{"probes",probes},
            {"rejectedCalls",rejected},{"fallbackProbes",fallback},{"callbackLeaseStillHeld",pending},
            {"actualFinalNotificationObserved",finalSeen},{"parentsStillHeld",!weakSession.expired()&&!weakConnection.expired()},
            {"closeCalls",refusedCloseCalls.load()},{"callbackHoldInvoked",finalNotificationHeld.load()},
            {"constructorRequestHadBusinessIo",false},{"constructorCloseAccepted",constructorCloseAccepted.load()},{"observationWindowMs",50}});
        row["passed"]=terminated&&reason=="mirror-bootstrap-callback-failed"&&!selected&&probes==1&&rejected==1&&fallback==0&&pending&&!finalSeen&&row.value("parentsStillHeld",false)&&refusedCloseCalls==1&&constructorCloseAccepted.load()&&!finalNotificationHeld.load();
        closesocket(listener);listener=INVALID_SOCKET;
    }catch(const std::exception& error){row["passed"]=false;row["error"]=error.what();}
    if(listener!=INVALID_SOCKET)closesocket(listener);
    const auto text=row.dump();channel.writeFrame(std::vector<std::uint8_t>(text.begin(),text.end()),2500);WSACleanup();
    // This is a real terminal failure. The negative leaves selection accepted
    // and returns 0, demonstrating why continuing would prolong request state.
    return row.value("discoveryTerminated",false)?2:0;
}
static int requestRetirementChild(ymcc::deckybinding::ChildChannel& channel,const std::string& kind){
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 2;
    const bool deny=kind=="deny"||kind=="deny-error",cancel=kind=="cancel",httpError=kind=="error"||kind=="deny-error";
    Json row{{"kind",kind},{"actualRequestRetirementConsumer",true},{"independentLeafCleanup",false}};
    bool typedFailure=false;
    try{
        CdpFixture server(false,true,false,false,httpError?405u:200u,cancel);
        auto token=std::make_shared<CancelToken>();
        std::weak_ptr<AsyncState> observed;std::weak_ptr<ymcc::steamlive::HttpHandle> weakSession,weakConnection;
        std::atomic<bool> cancelSawHeaders{false};std::thread canceler;
        std::string reason,bodyValue;Json transport;
        if(cancel)canceler=std::thread([&]{const auto until=GetTickCount64()+2000;while(!server.httpHeadersRead.load()&&GetTickCount64()<until)Sleep(1);cancelSawHeaders=server.httpHeadersRead.load();token->cancel();});
        try{
            auto session=std::make_shared<ymcc::steamlive::HttpHandle>(WinHttpOpen(L"RequestRetirementFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
            WinHttpSetTimeouts(session->h,1000,1000,1500,1500);
            auto connection=std::make_shared<ymcc::steamlive::HttpHandle>(WinHttpConnect(session->h,L"127.0.0.1",server.port,0));
            weakSession=session;weakConnection=connection;auto parents=std::make_shared<BootstrapHttpParents>(session,connection);
            const auto deadline=GetTickCount64()+3000;
            AsyncRequest request(WinHttpOpenRequest(connection->h,L"GET",L"/fixture",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),token,deadline,false,parents);
            observed=request.callbackState();
            if(deny){refuseHandle=request.handle();holdFinalNotification=false;}
            // The exact callback/request owns the only parent holder now.
            parents.reset();connection.reset();session.reset();
            const auto work=[&](AsyncRequest& operation){
                operation.handshake();const auto status=operation.status();
                if(status!=200)throw AsyncFailure("mirror-bootstrap-targets-status",status,ymcc::steamfailure::SourceStage::Discovery,ymcc::steamfailure::Domain::Http);
                return operation.httpBody(65536);
            };
            if(deny){
                struct Candidate{DWORD pid;unsigned short port;};const std::vector<Candidate> candidates{{GetCurrentProcessId(),server.port}};Json failure;
                const auto selected=ymcc::steamdiscovery::select<std::string>(ymcc::steamdiscovery::Kind::Bootstrap,candidates,[]{},[](const Candidate&){return true;},
                    [&](const Candidate&)->std::optional<std::string>{return bootstrapRequestRetired(request,deadline,work);},
                    [&](const std::exception& error,bool hasSelection){bootstrapDiscoveryRejected(error,hasSelection,failure);});
                row["selectedEndpoint"]=selected.endpoint.has_value();
            }else bodyValue=bootstrapRequestRetired(request,deadline,work);
        }catch(const AsyncRetirementFailure& error){
            typedFailure=true;reason=error.what();row["closeAccepted"]=error.closeAccepted;row["finalNotification"]=error.finalNotification;row["cleanupError"]=error.error;
            transport=bootstrapTransportFailure(error);
        }catch(const std::exception& error){reason=error.what();transport=bootstrapTransportFailure(error);}
        if(canceler.joinable())canceler.join();
        row.update({{"reason",reason},{"body",bodyValue},{"transportFailure",transport},{"typedRetirementFailure",typedFailure},{"cancelSawHeaders",cancelSawHeaders.load()}});
        if(deny){
            bool pending=false;if(const auto state=observed.lock()){std::lock_guard lock(state->mutex);pending=bool(state->callbackLifetime)&&!state->finalNotification;}
            row.update({{"callbackStillOwned",pending},{"parentsStillOwned",!weakSession.expired()&&!weakConnection.expired()},{"closeCalls",refusedCloseCalls.load()}});
            row["passed"]=typedFailure&&reason=="mirror-bootstrap-cleanup-incomplete"&&!row.value("closeAccepted",true)&&!row.value("finalNotification",true)&&row.value("cleanupError",DWORD{0})==ERROR_ACCESS_DENIED&&pending&&row.value("parentsStillOwned",false)&&refusedCloseCalls==1&&(!httpError||(transport.value("domain",std::string{})=="http"&&transport.value("code",0)==405&&transport.value("sourceStage",std::string{})=="discovery"));
        }else{
            const auto until=GetTickCount64()+1500;while((!observed.expired()||!weakSession.expired()||!weakConnection.expired())&&GetTickCount64()<until)Sleep(1);
            const bool returned=observed.expired()&&weakSession.expired()&&weakConnection.expired();row["callbackAndParentsReturnedBeforeChildExit"]=returned;
            row["passed"]=returned&&!typedFailure&&(cancel?(cancelSawHeaders.load()&&reason=="mirror-bootstrap-canceled"):httpError?(reason=="mirror-bootstrap-targets-status"&&transport.value("domain",std::string{})=="http"&&transport.value("code",0)==405&&transport.value("sourceStage",std::string{})=="discovery"):(reason.empty()&&bodyValue=="fixture-http-response"));
        }
    }catch(const std::exception& error){row["passed"]=false;row["error"]=error.what();}
    const auto text=row.dump();channel.writeFrame(std::vector<std::uint8_t>(text.begin(),text.end()),2500);WSACleanup();
    return typedFailure?2:0;
}
#include "decky_worker_bootstrap_scope_fixture.inc"
static bool tryOwnedCloseDebtChild(int argc,wchar_t** argv,int& code){
    using namespace ymcc::deckybinding;
    const auto mode=ChildChannel::classify(argc,argv);
    if(mode==ChildMode::ordinary)return false;
    ChildChannel channel;
    if(mode!=ChildMode::worker||channel.openArguments(argc,argv)!=ChildMode::worker){code=2;return true;}
    const auto frame=channel.readFrame(2500);
    if(!frame){code=2;return true;}
    const auto request=std::string(frame.payload.begin(),frame.payload.end());
    if(request.rfind("worker-bootstrap-",0)==0){code=workerBootstrapChild(channel,request.substr(17));return true;}
    if(request.rfind("request-retire-",0)==0){const auto kind=request.substr(15);if(kind!="success"&&kind!="error"&&kind!="cancel"&&kind!="deny"&&kind!="deny-error"){code=2;return true;}code=requestRetirementChild(channel,kind);return true;}
    if(request=="ctor-discovery"){code=discoveryConstructorChild(channel);return true;}
    if(request=="ctor-callback"||request=="ctor-context"){
        code=constructorScopeChild(channel,request=="ctor-context");return true;
    }
    if(request!="watch-close-debt-fixture"&&request!="watch-final-held-fixture"){code=2;return true;}
    code=runOwnedCloseDebtChild(channel,request=="watch-final-held-fixture");return true;
}
static int ownedCloseDebtOnly(const char* output,bool holdFinal=false){
    using namespace ymcc::deckybinding;
    Json raw{{"selection","--owned-close-debt-only"},{"selectedCase","cancel wakes passive receive promptly"},
        {"scope","supplemental original watch+Owner composition; no successful production Steam binding"},
        {"realSteam",false},{"fixtureIndependentLeafCleanup",false}};
    raw["selection"]=holdFinal?"--owned-final-held-only":"--owned-close-debt-only";
    try{
        Owner owner;const auto started=owner.start();if(!started)throw std::runtime_error("owner start");
        const auto before=owner.snapshot();
        const auto held=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.processId);
        if(!held)throw std::runtime_error("owned child handle");
        struct Held {HANDLE value;~Held(){CloseHandle(value);}} process{held};
        const std::string request=holdFinal?"watch-final-held-fixture":"watch-close-debt-fixture";
        if(!owner.writeFrame(std::vector<std::uint8_t>(request.begin(),request.end()),2500,before.generation))throw std::runtime_error("request");
        const auto reply=owner.readFrame(5000,before.generation);
        if(!reply)throw std::runtime_error("debt reply");
        const auto observed=Json::parse(reply.payload);raw["observed"]=observed;
        const auto began=GetTickCount64();const auto stopped=owner.stop(150,1500);const auto after=owner.snapshot();
        raw["ownerStopMs"]=GetTickCount64()-began;
        const bool exited=WaitForSingleObject(held,0)==WAIT_OBJECT_0;
        const bool returned=stopped.fullyDestroyed&&after.fullyDestroyed&&after.childExited&&exited&&after.handles==0&&after.activeCalls==0&&after.pendingIo==0&&after.jobActiveProcesses==0;
        const auto consumer=observed.value("consumerDiagnostic",Json::object());
        const bool consumerKnown=consumer.value("event",std::string{})=="bootstrap-watch"&&consumer.value("bootstrapStage",std::string{})=="failed"&&consumer.value("reason",std::string{})=="mirror-bootstrap-cleanup-incomplete"&&consumer.value("error",DWORD{0})==static_cast<DWORD>(holdFinal?WAIT_TIMEOUT:ERROR_ACCESS_DENIED)&&consumer.contains("closeAccepted")&&consumer["closeAccepted"]==holdFinal&&consumer.contains("finalNotification")&&consumer["finalNotification"]==false;
        raw["actualWorkerFinishConsumerExecuted"]=true;raw["consumerDiagnosticKnown"]=consumerKnown;
        const bool guarded=consumerKnown&&observed.value("actualFinalCallbackHeld",!holdFinal)==holdFinal&&observed.value("attached",false)&&!observed.value("stopReturned",true)&&observed.value("closeAccepted",!holdFinal)==holdFinal&&!observed.value("finalNotification",true)&&observed.value("cleanupError",DWORD{0})==static_cast<DWORD>(holdFinal?WAIT_TIMEOUT:ERROR_ACCESS_DENIED)&&observed.value("closeCalls",0u)==1&&observed.value("closeCallsAfterReentry",0u)==1&&observed.value("callbackAlive",false)&&observed.value("sessionAlive",false)&&observed.value("connectionAlive",false)&&observed.value("reentryFailure",std::string{})=="mirror-bootstrap-cleanup-incomplete";
        raw.update({{"passed",guarded&&returned&&raw["ownerStopMs"]<3200},{"ownerDestroyed",returned},
            {"childExitObserved",exited},{"jobActiveProcesses",after.jobActiveProcesses},{"handles",after.handles},
            {"activeCalls",after.activeCalls},{"pendingIo",after.pendingIo},{"jobTerminationAttempted",after.jobTerminationAttempted},
            {"originalOwnerBoundMs",3200},{"successfulSteamBootstrapTested",false}});
    }catch(const std::exception& error){raw["passed"]=false;raw["error"]=error.what();}
    std::ofstream file(output,std::ios::binary);file<<raw.dump(2);file.close();return raw.value("passed",false)?0:2;
}
static int constructorScopeOnly(const char* output,const std::string& kind){
    using namespace ymcc::deckybinding;
    Json raw{{"selection","--constructor-scope-only"},{"kind",kind},{"realSteam",false},{"successfulSteamBootstrapTested",false}};
    try{
        if(kind!="callback"&&kind!="context")throw std::runtime_error("unknown constructor selection");
        Owner owner;if(!owner.start())throw std::runtime_error("constructor owner start");
        const auto before=owner.snapshot();
        const auto held=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.processId);
        if(!held)throw std::runtime_error("constructor child handle");
        struct Held{HANDLE value;~Held(){CloseHandle(value);}} process{held};
        const auto request="ctor-"+kind;
        if(!owner.writeFrame(std::vector<std::uint8_t>(request.begin(),request.end()),2500,before.generation))throw std::runtime_error("constructor request");
        const auto reply=owner.readFrame(7000,before.generation);if(!reply)throw std::runtime_error("constructor reply");
        const auto observed=Json::parse(reply.payload);raw["observed"]=observed;
        const auto stopped=owner.stop(150,1500);const auto after=owner.snapshot();
        const bool returned=stopped.fullyDestroyed&&after.fullyDestroyed&&after.childExited&&WaitForSingleObject(held,0)==WAIT_OBJECT_0&&after.handles==0&&after.activeCalls==0&&after.pendingIo==0&&after.jobActiveProcesses==0;
        raw.update({{"passed",observed.value("passed",false)&&returned},{"ownerDestroyed",returned},{"childExitCode",after.exitCode},
            {"jobActiveProcesses",after.jobActiveProcesses},{"handles",after.handles},{"activeCalls",after.activeCalls},{"pendingIo",after.pendingIo},
            {"jobTerminationAttempted",after.jobTerminationAttempted}});
    }catch(const std::exception& error){raw["passed"]=false;raw["error"]=error.what();}
    std::ofstream file(output,std::ios::binary);file<<raw.dump(2);file.close();return raw.value("passed",false)?0:2;
}
static int discoveryConstructorOnly(const char* output){
    using namespace ymcc::deckybinding;
    Json raw{{"selection","--discovery-constructor-only"},{"realSteam",false},{"successfulSteamBootstrapTested",false}};
    try{
        Owner owner;if(!owner.start())throw std::runtime_error("discovery owner start");const auto before=owner.snapshot();
        const auto held=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.processId);
        if(!held)throw std::runtime_error("discovery held child");struct Held{HANDLE value;~Held(){CloseHandle(value);}} process{held};
        const std::string request="ctor-discovery";
        if(!owner.writeFrame(std::vector<std::uint8_t>(request.begin(),request.end()),2500,before.generation))throw std::runtime_error("discovery request");
        const auto reply=owner.readFrame(7000,before.generation);if(!reply)throw std::runtime_error("discovery reply");
        const auto observed=Json::parse(reply.payload);raw["observed"]=observed;
        const auto stopped=owner.stop(150,1500);const auto after=owner.snapshot();
        const bool returned=stopped.fullyDestroyed&&after.fullyDestroyed&&after.childExited&&WaitForSingleObject(held,0)==WAIT_OBJECT_0&&after.handles==0&&after.activeCalls==0&&after.pendingIo==0&&after.jobActiveProcesses==0;
        raw.update({{"passed",observed.value("passed",false)&&returned&&after.exitCode==2},{"ownerDestroyed",returned},{"childExitCode",after.exitCode},
            {"handles",after.handles},{"activeCalls",after.activeCalls},{"pendingIo",after.pendingIo},{"jobActiveProcesses",after.jobActiveProcesses},{"jobTerminationAttempted",after.jobTerminationAttempted}});
    }catch(const std::exception& error){raw["passed"]=false;raw["error"]=error.what();}
    std::ofstream file(output,std::ios::binary);file<<raw.dump(2);file.close();return raw.value("passed",false)?0:2;
}
static int requestRetirementOnly(const char* output,const std::string& kind){
    using namespace ymcc::deckybinding;
    Json raw{{"selection","--request-retirement-only"},{"kind",kind},{"realSteam",false},{"successfulSteamBootstrapTested",false}};
    try{
        Owner owner;if(!owner.start())throw std::runtime_error("retirement owner start");const auto before=owner.snapshot();
        const auto held=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.processId);if(!held)throw std::runtime_error("retirement held child");
        struct Held{HANDLE value;~Held(){CloseHandle(value);}} process{held};const auto request="request-retire-"+kind;
        if(!owner.writeFrame(std::vector<std::uint8_t>(request.begin(),request.end()),2500,before.generation))throw std::runtime_error("retirement request");
        const auto reply=owner.readFrame(7000,before.generation);if(!reply)throw std::runtime_error("retirement reply");
        const auto observed=Json::parse(reply.payload);raw["observed"]=observed;
        const auto stopped=owner.stop(150,1500);const auto after=owner.snapshot();
        const bool returned=stopped.fullyDestroyed&&after.fullyDestroyed&&after.childExited&&WaitForSingleObject(held,0)==WAIT_OBJECT_0&&after.handles==0&&after.activeCalls==0&&after.pendingIo==0&&after.jobActiveProcesses==0;
        raw.update({{"passed",observed.value("passed",false)&&returned&&after.exitCode==(kind.rfind("deny",0)==0?2u:0u)},{"ownerDestroyed",returned},{"childExitCode",after.exitCode},
            {"handles",after.handles},{"activeCalls",after.activeCalls},{"pendingIo",after.pendingIo},{"jobActiveProcesses",after.jobActiveProcesses},{"jobTerminationAttempted",after.jobTerminationAttempted}});
    }catch(const std::exception& error){raw["passed"]=false;raw["error"]=error.what();}
    std::ofstream file(output,std::ios::binary);file<<raw.dump(2);file.close();return raw.value("passed",false)?0:2;
}
static Json contextLifecycleMeasurements=Json::object();
static void writeContextLifecycleResult(const char* path,bool passed,unsigned checks,const std::string& error={}){
    const auto text=Json{{"selection","--context-lifecycle-only"},{"passed",passed},{"checks",checks},
        {"realSteam",false},{"newCanonicalIds",0},{"error",error},{"measurements",contextLifecycleMeasurements}}.dump(2);
    const HANDLE file=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("context-result-create");
    DWORD written=0;const bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)!=FALSE&&written==text.size();
    const bool closed=CloseHandle(file)!=FALSE;if(!ok||!closed)throw std::runtime_error("context-result-write");
}
#include "fixtures/decky_official_socket_diagnostic_cases.inc"
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1]).rfind("--official-socket-observation",0)==0){
        if(argc!=3)return 2;
        const std::string selection=argv[1];
        if(selection=="--official-socket-observation-only")return officialSocketObservationOnly(argv[2]);
        if(selection=="--official-socket-observation-continuation")return officialSocketObservationContinuation(argv[2]);
        if(selection=="--official-socket-observation-replay")return officialSocketObservationReplay(argv[2]);
        return 2;
    }
    if(argc>1&&std::string(argv[1])=="--context-lifecycle-only"&&argc!=3)return 2;
    const bool contextLifecycleOnly=argc==3&&std::string(argv[1])=="--context-lifecycle-only";
    if(argc==4&&std::string(argv[1])=="--worker-bootstrap-only")return workerBootstrapOnly(argv[2],argv[3]);
    int wideCount=0;auto wideArgs=CommandLineToArgvW(GetCommandLineW(),&wideCount);int childCode=0;
    const bool child=wideArgs&&tryOwnedCloseDebtChild(wideCount,wideArgs,childCode);if(wideArgs)LocalFree(wideArgs);if(child)return childCode;
    if(argc==4&&std::string(argv[1])=="--request-retirement-only")return requestRetirementOnly(argv[2],argv[3]);
    if(argc==3&&std::string(argv[1])=="--discovery-constructor-only")return discoveryConstructorOnly(argv[2]);
    if(argc==4&&std::string(argv[1])=="--constructor-scope-only")return constructorScopeOnly(argv[2],argv[3]);
    if(argc==3&&std::string(argv[1])=="--owned-close-debt-only")return ownedCloseDebtOnly(argv[2]);
    if(argc==3&&std::string(argv[1])=="--owned-final-held-only")return ownedCloseDebtOnly(argv[2],true);
    if(argc==3&&std::string(argv[1])=="--parent-lifetime-only"){
        WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 2;
        Json raw={{"selection","--parent-lifetime-only"},{"selectedCase","cancel wakes passive receive promptly"},{"realSteam",false}};
        try{
            bool attached=false,adopted=false,stopped=false,lateNoAttach=false,parentsReturned=false,upgradeRetired=false;
            std::weak_ptr<AsyncState> callback;std::weak_ptr<ymcc::steamlive::HttpHandle> session,connection;
            {
                WatchFixture f;attached=f.startFailure.empty()&&f.stage("context-attached");
                adopted=f.parentAdopted;upgradeRetired=f.upgradeFinalBeforeWatch;callback=f.observedSocket;session=f.observedSession;connection=f.observedConnection;
                const auto start=GetTickCount64();f.watch.stop();raw["stopMs"]=GetTickCount64()-start;stopped=f.stage("stopped");
                const auto before=f.cdp.count();f.cdp.created(302);Sleep(50);lateNoAttach=f.cdp.count()==before;
                const auto until=GetTickCount64()+1500;
                while((!callback.expired()||!session.expired()||!connection.expired())&&GetTickCount64()<until)Sleep(1);
                parentsReturned=callback.expired()&&session.expired()&&connection.expired();
            }
            const bool pass=attached&&adopted&&stopped&&lateNoAttach&&parentsReturned&&upgradeRetired&&raw["stopMs"]<1000;
            raw.update({{"passed",pass},{"attached",attached},{"actualCallbackBoundParents",adopted},{"upgradeFinalBeforeWatch",upgradeRetired},
                {"stopped",stopped},{"lateContextDoesNotAttach",lateNoAttach},{"callbackAndParentsReturned",parentsReturned},
                {"originalStopDeadlineMs",1000},{"originalCallbackReturnWindowMs",1500},
                {"actualBootstrapWatch",true},{"newPollers",0},{"newProductThreads",0}});
            std::ofstream output(argv[2],std::ios::binary);output<<raw.dump(2);output.close();WSACleanup();return pass?0:2;
        }catch(const std::exception& error){raw["passed"]=false;raw["error"]=error.what();std::ofstream(argv[2],std::ios::binary)<<raw.dump(2);WSACleanup();return 2;}
    }
    if(argc==2&&std::string(argv[1])=="--async-close-diagnostics"){WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 2;try{const auto checks=watchCloseDiagnostics();WSACleanup();std::cout<<"Bootstrap watch close diagnostic tests passed: "<<checks<<"\n";return 0;}catch(const std::exception& error){WSACleanup();std::cerr<<error.what()<<"\n";return 1;}}
    if(argc==2&&std::string(argv[1])=="--async-diagnostics"){WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 2;try{const auto checks=watchTransportDiagnostics();WSACleanup();std::cout<<"Bootstrap watch transport diagnostic tests passed: "<<checks<<"\n";return 0;}catch(const std::exception& error){WSACleanup();std::cerr<<error.what()<<"\n";return 1;}}
    unsigned checks=0;const auto check=[&](bool condition,const char* name){if(!condition)throw std::runtime_error(name);++checks;};
    WSADATA data{};WSAStartup(MAKEWORD(2,2),&data);
    try{
        {
            contextReplayMode=1;callbackReplayMode=1;WatchFixture f;
            check(f.startFailure.empty(),("foreign context replay cannot exhaust watched context budget: "+f.startFailure).c_str());
            check(f.cdp.wait(101),"existing context receives attachment after Runtime.enable");{
                const auto attached=f.stage("context-attached");std::string reason;
                {std::lock_guard lock(f.mutex);if(!f.records.empty())reason=f.records.back().value("reason",std::string{});}
                check(attached,("unrelated callback replay cannot exhaust watched context budget: "+reason).c_str());
                check(attached,"readback confirmed");
                std::lock_guard lock(f.mutex);
                const auto first=std::find_if(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="context-attached";});
                check(first!=f.records.end()&&!first->contains("contextAttachedTick"),"initial attachment is not a navigation receipt");
            }check(f.cdp.enables==1,"one runtime subscription survives 192 replayed console packets");check(f.cdp.count()==1,"128 foreign or isolated contexts do not create any evaluation");check(f.cdp.healthReads==1,"128 unrelated binding exception and destroy events add no health reads");
            const auto readsBeforeForeign=f.cdp.healthReads.load();const auto evaluationsBeforeForeign=f.cdp.count();
            callbackReplayMode=4;f.cdp.created(105);check(f.cdp.wait(105),"foreign callback replay reaches a valid attachment request");
            {
                std::unique_lock lock(f.mutex);const auto attached=[&]{return std::any_of(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="context-attached"&&row.value("contextGeneration",0u)==2;});};
                const bool ready=f.changed.wait_for(lock,std::chrono::seconds(4),attached);const auto reason=f.records.empty()?std::string{}:f.records.back().value("reason",std::string{});
                check(ready,("foreign context callbacks cannot exhaust watched context budget: "+reason).c_str());
                const auto receipt=std::find_if(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="context-attached"&&row.value("contextGeneration",0u)==2;});
                const auto tick=receipt->value("contextAttachedTick",ULONGLONG{});
                check(tick&&tick<=GetTickCount64(),"verified reattachment publishes its own monotonic tick");
                check(!ymcc::deckydiag::record("bootstrap-watch",*receipt).contains("contextAttachedTick"),"private handoff tick is not exported");
            }
            check(f.cdp.healthReads==readsBeforeForeign+1&&f.cdp.count()==evaluationsBeforeForeign+1,"128 foreign callback contexts add no evaluation or health read");
            check(f.cdp.accountChecks==0&&f.cdp.teardowns==0,"foreign same-run account signals and Loader exceptions cannot act on the current page");
            const auto readsBeforeFuture=f.cdp.healthReads.load();callbackReplayMode=5;f.cdp.created(107);
            check(f.cdp.wait(107),"new current context reaches actual replay response");
            {
                std::unique_lock lock(f.mutex);
                const auto replayed=[&]{std::vector<std::pair<unsigned,std::string>> observations;for(const auto& row:f.records)
                    if(row.value("bootstrapStage",std::string{})=="connection-check")observations.emplace_back(row.value("contextGeneration",0u),row.value("reason",std::string{}));return observations;};
                const auto failure=[&]{const auto found=std::find_if(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="failed";});return found==f.records.end()?std::string{}:found->value("reason",std::string{});};
                check(f.changed.wait_for(lock,std::chrono::seconds(4),[&]{return replayed().size()>=4||!failure().empty();}),"in-flight QAM replay did not resolve");
                check(failure().empty(),("in-flight QAM replay failed: "+failure()).c_str());
                check(replayed()==std::vector<std::pair<unsigned,std::string>>{{4,"qam-open"},{4,"probe-result"},{5,"qam-open"},{5,"probe-result"}},"each exact page gets opening and completion in order without duplicate checks");
            }
            check(f.cdp.wait(106)&&f.cdp.wait(108),"queued future callback context receives its own attachment");
            check(f.cdp.healthReads==readsBeforeFuture+3,"future callbacks add no reads beyond the three real attachments");
            const auto count=f.cdp.count();Sleep(1800);check(f.cdp.count()==count,"idle has no polling or periodic evaluation");
            {std::lock_guard lock(f.mutex);check(std::none_of(f.records.begin(),f.records.end(),[](const auto& v){return v.value("bootstrapStage",std::string{})=="failed";}),"passive wait survives command timeout");}
            {std::lock_guard lock(f.mutex);check(std::none_of(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="context-invalidated";}),"healthy and foreign replay emit no invalidation diagnostic");}
            f.cdp.send({{"method","Runtime.executionContextsCleared"},{"params",Json::object()}});f.cdp.created(102);check(f.cdp.wait(102),"same-process reload gets new attachment");
            check(f.stage("context-invalidated"),"current cleared context diagnostic missing");
            {std::lock_guard lock(f.mutex);const auto found=std::find_if(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="context-invalidated";});
                check(found!=f.records.end()&&found->value("reason",std::string{})=="execution-contexts-cleared","clear event keeps its fixed source reason");
                check(!found->contains("steamFailure"),"passive clear cannot inherit the preceding command failure");
                const auto logged=ymcc::deckydiag::record("bootstrap-watch",*found);
                check(logged.value("reason",std::string{})=="execution-contexts-cleared"&&logged.value("contextGeneration",0ull)>0,"actual log filter keeps active-context source facts");
                check(!logged.contains("runId")&&!logged.contains("contextId")&&!logged.contains("endpoint"),"invalidation log has no credential or document identity");}
            f.cdp.created(201,"https://example.invalid");f.cdp.created(202,"https://steamloopback.host",false);Sleep(100);check(f.cdp.count()==count+1,"foreign and isolated contexts ignored");
            // A relevant destruction can arrive before its accepted context is
            // consumed. It must survive filtering and retire that future target.
            callbackReplayMode=2;f.cdp.created(103);
            check(f.cdp.wait(103)&&f.cdp.wait(3020),"queued future context is processed before its destruction");
            const auto readsBeforeStale=f.cdp.healthReads.load();
            f.cdp.send({{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",3020},{"payload",Json{{"runId",f.cdp.run},{"code","decky-render-failed"}}.dump()}}}});Sleep(80);
            check(f.cdp.healthReads==readsBeforeStale,"queued relevant destruction suppresses a late health read for its retired context");
            {std::lock_guard lock(f.mutex);check(std::count_if(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="context-invalidated"&&row.value("reason",std::string{})=="execution-context-destroyed";})==1,"queued current destruction is observed once");}
            f.cdp.created(104);check(f.cdp.wait(104),"next valid context still attaches after queued creation and destruction");
            f.cdp.created(300);check(f.cdp.wait(300),"destroyed context reply fixture reached");check(f.stage("context-replaced"),"transient replacement recorded");
            {
                std::lock_guard lock(f.mutex);
                const auto failure=std::find_if(f.records.begin(),f.records.end(),[](const auto& row){return row.contains("steamFailure");});
                check(failure!=f.records.end(),"real CDP context error retains safe failure facts");
                ymcc::steamfailure::FailureContext context;
                const auto logged=ymcc::deckydiag::failureRecord(*failure,context);
                check(logged["steamFailure"].value("domain",std::string{})=="cdp-protocol"&&logged["steamFailure"].value("code",0)==-32000,"original native projection keeps actual CDP code");
                check(logged.dump().find("private-url-token")==std::string::npos,"CDP message credentials cannot reach log");
            }
            f.cdp.created(305);check(f.cdp.wait(305),"JavaScript exception reaches the same real watch command");
            {
                std::unique_lock lock(f.mutex);
                const auto hasJs=[&]{return std::any_of(f.records.begin(),f.records.end(),[](const auto& row){return row.contains("steamFailure")&&row["steamFailure"].value("domain",std::string{})=="javascript";});};
                check(f.changed.wait_for(lock,std::chrono::seconds(4),hasJs),"real JS exception retains bounded native reason");
                const auto failure=std::find_if(f.records.begin(),f.records.end(),[](const auto& row){return row.contains("steamFailure")&&row["steamFailure"].value("domain",std::string{})=="javascript";});
                ymcc::steamfailure::FailureContext context;const auto logged=ymcc::deckydiag::failureRecord(*failure,context);
                check(logged["steamFailure"].value("exceptionClass",std::string{})=="TypeError"&&logged["steamFailure"].value("line",0)==17,"real JS error type and line survive watch-to-native projection");
                check(!logged["steamFailure"].value("codeKnown",true)&&!logged["steamFailure"].contains("code")&&logged.dump().find("secret")==std::string::npos,"unknown JS code stays unknown and private stack is excluded");
            }
            f.cdp.created(301);check(f.cdp.wait(301),"next real event repairs transient navigation without timer");
            const auto generationBeforeStorm=static_cast<unsigned long long>(f.cdp.count());
            {std::unique_lock lock(f.mutex);check(f.changed.wait_for(lock,std::chrono::seconds(4),[&]{return std::any_of(f.records.begin(),f.records.end(),[&](const auto& row){return row.value("bootstrapStage",std::string{})=="context-attached"&&row.value("contextGeneration",0ull)==generationBeforeStorm;});}),"original attachment is idle before passive replay storm");}
            const auto readsBeforeStorm=f.cdp.healthReads.load();const auto evaluationsBeforeStorm=f.cdp.count();
            for(unsigned n=0;n<20;++n){
                f.cdp.send({{"method","Runtime.executionContextsCleared"},{"params",Json::object()}});
                for(unsigned repeat=0;repeat<50;++repeat){
                    f.cdp.send({{"method","Runtime.executionContextsCleared"},{"params",Json::object()}});
                    f.cdp.send({{"method","Runtime.executionContextDestroyed"},{"params",{{"executionContextId",900000}}}});
                }
                f.cdp.created(700+static_cast<int>(n));check(f.cdp.wait(700+static_cast<int>(n)),"real context reattaches after duplicate invalidation storm");
                std::unique_lock lock(f.mutex);check(f.changed.wait_for(lock,std::chrono::seconds(4),[&]{return std::any_of(f.records.begin(),f.records.end(),[&](const auto& row){return row.value("bootstrapStage",std::string{})=="context-attached"&&row.value("contextGeneration",0ull)==generationBeforeStorm+n+1;});}),"original context command finishes before next passive-only storm");
            }
            {std::unique_lock lock(f.mutex);check(f.changed.wait_for(lock,std::chrono::seconds(4),[&]{return std::any_of(f.records.begin(),f.records.end(),[&](const auto& row){return row.value("bootstrapStage",std::string{})=="context-attached"&&row.value("contextGeneration",0ull)==generationBeforeStorm+20;});}),"last real context completes its original health read");
                check(std::count_if(f.records.begin(),f.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="context-invalidated";})==16,"invalidation diagnostics stay bounded at sixteen per watch");
                for(const auto& row:f.records)if(row.value("bootstrapStage",std::string{})=="context-invalidated")
                    check(!row.contains("steamFailure")&&(row.value("reason",std::string{})=="execution-contexts-cleared"||row.value("reason",std::string{})=="execution-context-destroyed"),"passive source facts are fixed and carry no command failure");
            }
            check(f.cdp.healthReads==readsBeforeStorm+20&&f.cdp.count()==evaluationsBeforeStorm+20,"two thousand redundant events add zero health queries or attachment evaluations");
            check(f.cdp.enables==1&&f.cdp.teardowns==0&&f.cdp.accountChecks==0,"passive invalidations do not resubscribe tear down or verify accounts");
            contextLifecycleMeasurements={{"redundantEvents",2000},{"acceptedReplacementContexts",20},{"invalidationRows",16},
                {"newHealthReads",f.cdp.healthReads.load()-readsBeforeStorm},{"newAttachmentEvaluations",f.cdp.count()-evaluationsBeforeStorm},
                {"runtimeEnables",f.cdp.enables.load()},{"teardowns",f.cdp.teardowns.load()},{"accountChecks",f.cdp.accountChecks.load()}};
            const auto start=GetTickCount64();f.watch.stop();check(GetTickCount64()-start<1000,"cancel wakes passive receive promptly");check(f.stage("stopped"),"explicit stop reported");
            const auto final=f.cdp.count();f.cdp.created(302);Sleep(50);check(f.cdp.count()==final,"stopped watch never reattaches");
        }
        {
            WatchFixture optional;check(optional.stage("context-attached"),"new watch starts after capped watch stopped");
            optional.failInvalidationLog=true;optional.cdp.send({{"method","Runtime.executionContextsCleared"},{"params",Json::object()}});
            optional.cdp.created(102);check(optional.cdp.wait(102),"optional logging allocation failure cannot stop attachment");
            optional.cdp.send({{"method","Runtime.executionContextDestroyed"},{"params",{{"executionContextId",102}}}});
            optional.cdp.created(103);check(optional.cdp.wait(103)&&optional.stage("context-invalidated"),"new watch has its own bounded diagnostic allowance");
            check(!optional.failInvalidationLog.load(),"real optional diagnostic callback consumed the injected allocation failure");
            {std::lock_guard lock(optional.mutex);check(std::none_of(optional.records.begin(),optional.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="failed";}),"diagnostic allocation failure is never a watch failure");
                const auto count=std::count_if(optional.records.begin(),optional.records.end(),[](const auto& row){return row.value("bootstrapStage",std::string{})=="context-invalidated";});
                check(count==1,"failed diagnostic is not replayed and new destruction is observed once");}
            check(optional.cdp.enables==1&&optional.cdp.teardowns==0,"failed diagnostic adds no subscription or teardown");
            const bool stopped=optional.watch.stop();check(stopped,"optional diagnostic watch releases original request");
            const auto until=GetTickCount64()+1000;while((!optional.observedSocket.expired()||!optional.observedSession.expired()||!optional.observedConnection.expired())&&GetTickCount64()<until)Sleep(1);
            check(optional.observedSocket.expired()&&optional.observedSession.expired()&&optional.observedConnection.expired(),"actual callback state and HTTP parents are returned");
            contextLifecycleMeasurements["optionalFailureContinued"]=true;contextLifecycleMeasurements["newWatchRearmed"]=true;
            contextLifecycleMeasurements["callbackAndParentsReturned"]=true;
        }
        if(contextLifecycleOnly){writeContextLifecycleResult(argv[2],true,checks);WSACleanup();return 0;}
        {
            WatchFixture f;check(f.cdp.wait(101),"second isolated watch starts");f.cdp.created(400);check(f.cdp.wait(400),"silent reply reached");
            const auto start=GetTickCount64();f.watch.stop();check(GetTickCount64()-start<1000,"cancel wakes silent command IO promptly");
        }
        {
            WatchFixture f;check(f.cdp.wait(101),"ownership test watch starts");f.owned=false;f.cdp.created(500);check(f.stage("failed"),"changed ownership fails closed");check(f.cdp.count()==1,"changed ownership never gets evaluation");
            f.cdp.created(501);Sleep(100);check(f.cdp.count()==1,"terminal ownership failure never retries");
        }
        {
            WatchFixture f;check(f.cdp.wait(101),"close fixture attaches to the owned context");f.cdp.created(300);check(f.cdp.wait(300)&&f.stage("context-replaced"),"real context error precedes independent socket close");
            {std::lock_guard lock(f.mutex);const auto previous=f.records.back().at("steamFailure");
                check(previous.value("domain",std::string{})=="cdp-protocol"&&previous.value("code",0)==-32000,"preceding context diagnostic carries the real CDP error");}
            f.cdp.closeWebSocket();check(f.stage("failed"),"real WebSocket CLOSE wakes the watch");
            std::lock_guard lock(f.mutex);const auto last=f.records.back();const auto close=last.at("steamFailure");
            check(last.value("reason",std::string{})=="mirror-bootstrap-ws-closed","CLOSE is distinguished from malformed data");
            check(close.value("domain",std::string{})=="websocket-close"&&close.value("sourceStage",std::string{})=="receive","old CDP error cannot be assigned to the later socket close");
            check(ymcc::deckysidebar::recoverableWatchFailure(last.value("reason",std::string{})),"CLOSE permits verified transport recovery");
        }
        {
            WatchFixture f;check(f.cdp.wait(101),"hang fixture initially attaches");f.cdp.created(400);check(f.cdp.wait(400),"renderer evaluate is deliberately unresponsive");
            const auto start=GetTickCount64();check(f.stage("failed"),"hung evaluate reaches its own finite IO deadline without user cancellation");
            check(GetTickCount64()-start<4000,"hung renderer cannot hold watch command IO indefinitely");
            {std::lock_guard lock(f.mutex);check(ymcc::deckysidebar::recoverableWatchFailure(f.records.back().value("reason",std::string{})),"hung evaluate is a recoverable transport failure, not identity failure");}
            f.watch.stop();
        }
        {
            WatchFixture f;check(f.cdp.wait(101),"a new verified context can attach after the hung watch is released");check(f.stage("context-attached"),"recovery context readback completes");
        }
        {
            WatchFixture f;check(f.cdp.wait(101),"health fixture attaches verified context");check(f.stage("context-attached"),"health context admitted");
            const auto signal=[&](int id,const std::string& run){f.cdp.send({{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",id},{"payload",Json{{"runId",run},{"code","decky-render-failed"}}.dump()}}}});};
            const auto opened=[&](int id,const std::string& run,const char* code="decky-connection-check"){f.cdp.send({{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",id},{"payload",Json{{"runId",run},{"code",code}}.dump()}}}});};
            opened(201,f.cdp.run);opened(101,"stale-run");
            opened(201,f.cdp.run,"decky-connection-result");opened(101,"stale-run","decky-connection-result");
            opened(101,f.cdp.run,"decky-connection-result-unknown");Sleep(50);
            check(!f.stage("connection-check",50),"old context or run cannot request QAM connection recovery");
            opened(101,f.cdp.run);check(f.stage("connection-check"),"actual CDP QAM opening reaches existing native watch diagnostic");
            opened(101,f.cdp.run,"decky-connection-result");
            {std::unique_lock lock(f.mutex);const auto observations=[&]{std::vector<std::string> result;for(const auto& v:f.records)
                if(v.value("bootstrapStage",std::string{})=="connection-check")result.push_back(v.value("reason",std::string{}));return result;};
                check(f.changed.wait_for(lock,std::chrono::seconds(2),[&]{return observations().size()>=2;}),"actual CDP completion reaches existing connection callback");
                check(observations()==std::vector<std::string>{"qam-open","probe-result"},"opening and probe completion keep distinct, scoped observations");}
            for(const auto* source:{"open-or-legacy","probe-result"}){
                const auto row=ymcc::deckydiag::record("connection-check",{{"scope","qam-open"},{"source",source},{"runId","secret"}});
                check(row.value("source",std::string{})==source&&!row.contains("runId"),"connection log preserves only fixed provenance");
            }
            check(!ymcc::deckydiag::record("connection-check",{{"scope","qam-open"},{"source","injected"}}).contains("source")&&
                !ymcc::deckydiag::record("other",{{"scope","qam-open"},{"source","probe-result"}}).contains("source"),"other diagnostic sources are not forwarded");
            check(f.cdp.teardowns==0,"QAM opening cannot disable or restart the Loader itself");
            f.cdp.boundary=true;signal(201,f.cdp.run);signal(101,"stale-run");f.cdp.boundary=false;signal(101,f.cdp.run);Sleep(80);
            check(!f.stage("terminal",50),"foreign/stale/fabricated marker cannot close healthy frontend");
            f.cdp.boundary=true;signal(101,f.cdp.run);check(f.stage("terminal"),"actual boundary readback triggers terminal event");check(f.cdp.teardowns==1,"fatal event tears down upstream lifecycle exactly once without restart");
            {std::lock_guard lock(f.mutex);check(f.records.back().value("reason",std::string{})=="decky-render-failed","typed terminal reason retained");}
        }
        {
            WatchFixture f;check(f.cdp.wait(101)&&f.stage("context-attached"),"unhandled boot fixture attaches");
            const Json error={{"method","Runtime.exceptionThrown"},{"params",{{"exceptionDetails",{{"executionContextId",101},{"lineNumber",23},{"columnNumber",8},{"text","Uncaught private-event-secret"},{"exception",{{"subtype","error"},{"className","TypeError"},{"description","TypeError: Cannot read properties of undefined (reading 'createElement') private-event-secret"}}},{"stackTrace",{{"callFrames",Json::array({{{"url","http://127.0.0.1:1337/frontend/index.js"}}})}}}}}}}};
            auto plugin=error;plugin["params"]["exceptionDetails"]["stackTrace"]["callFrames"][0]["url"]="http://127.0.0.1:1337/plugins/example/dist/index.js";f.cdp.send(plugin);check(!f.stage("terminal",80),"individual plugin error does not close Decky");
            f.cdp.bootMissing=false;f.cdp.send(error);check(!f.stage("terminal",100),"optional core error with initialized loader cannot disable Decky");
            f.cdp.bootMissing=true;f.cdp.send(error);check(f.stage("terminal"),"official boot fatal exception stops consumer");check(f.cdp.teardowns==1,"boot fault owns only lifecycle teardown");
            {
                std::lock_guard lock(f.mutex);
                ymcc::steamfailure::FailureContext context;
                const auto logged=ymcc::deckydiag::failureRecord(f.records.back(),context);
                check(logged["steamFailure"].value("domain",std::string{})=="javascript","actual uncaught event retains its own JavaScript source facts");
                check(logged["steamFailure"].value("exceptionClass",std::string{})=="TypeError"&&logged["steamFailure"].value("line",0)==23&&logged["steamFailure"].value("column",0)==8,"actual event class and position survive verification commands");
                check(!logged["steamFailure"].value("codeKnown",true)&&!logged["steamFailure"].contains("code"),"uncaught event unknown protocol code stays unknown");
                check(logged.dump().size()<2304&&logged.dump().find("private-event-secret")==std::string::npos&&logged.dump().find("127.0.0.1")==std::string::npos,"uncaught event text URL and stack stay outside logs");
            }
        }
        {
            WatchFixture f;check(f.cdp.wait(101)&&f.stage("context-attached"),"offline account fixture attaches");
            const auto signal=[&](int context,const std::string& run){f.cdp.send({{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",context},{"payload",Json{{"runId",run},{"code","decky-account-changed"}}.dump()}}}});};
            f.cdp.accountRevoked=true;signal(201,f.cdp.run);signal(101,"stale-run");f.cdp.accountRevoked=false;signal(101,f.cdp.run);
            check(!f.stage("failed",150),"stale or fabricated account signal cannot revoke a current connection");
            f.cdp.accountRevoked=true;signal(101,f.cdp.run);check(f.stage("failed"),"actual account guard revokes old transport");
            std::lock_guard lock(f.mutex);const auto last=f.records.back();
            check(last.value("reason",std::string{})=="mirror-bootstrap-account-changed","nonfatal account reason preserved");
            check(ymcc::deckysidebar::recoverableWatchFailure(last.value("reason",std::string{})),"account change revalidates original recovery path");
            check(f.cdp.teardowns==0,"account switch never tears down official Loader or restarts Steam");
        }
        {
            // Retained same-run health already reported on a transport which
            // disappeared. No new bindingCalled event is emitted on reattach.
            WatchFixture f(true);check(f.stage("terminal"),"reattach detects existing fatal boundary without a fresh marker");
            check(f.cdp.teardowns==1,"existing boundary uses original lifecycle stop exactly once");
            std::lock_guard lock(f.mutex);check(f.records.back().value("reason",std::string{})=="decky-render-failed","lost marker preserves verified fatal reason");
        }
        {
            WatchFixture f(true,false);check(f.stage("context-attached"),"stale mirror after readback can settle without closing current session");
            check(!f.stage("terminal",80),"existing boundary with changed run never closes a different binding");check(f.cdp.teardowns==0,"stale existing boundary has zero upstream teardown");
        }
        {
            WatchFixture f;check(f.stage("context-attached"),"healthy reattachment completes its single readback");
            check(f.cdp.healthReads==1,"one boundary read per verified context attachment");Sleep(120);check(f.cdp.healthReads==1,"healthy attachment adds no health timer or polling");
        }
        {
            // Logout lands after the guarded injection readback, before the
            // new one-shot boundary read. The runId remains as revoked provenance.
            WatchFixture f(true,true,true);check(f.stage("context-attached"),"revoked account during attachment is transport-only");
            check(!f.stage("terminal",80),"revoked provenance cannot authorize fatal close");check(f.cdp.teardowns==0,"revoked attachment has zero upstream teardown");
            f.cdp.send({{"method","Runtime.bindingCalled"},{"params",{{"name","__YMCC_DECKY_HEALTH_V1"},{"executionContextId",101},{"payload",Json{{"runId",f.cdp.run},{"code","decky-account-changed"}}.dump()}}}});
            check(f.stage("failed"),"queued account marker still reaches transport recovery");
            std::lock_guard lock(f.mutex);check(f.records.back().value("reason",std::string{})=="mirror-bootstrap-account-changed","logout race preserves nonfatal recovery reason");
        }
        {
            WatchFixture f(true,true,false,true);check(f.stage("context-attached"),"logout between native read and page mutation refuses teardown");
            check(!f.stage("terminal",80),"proven mutation refusal cannot enqueue fatal OFF");check(f.cdp.teardowns==0,"late revocation leaves actual lifecycle untouched");
        }
        {
            contextReplayMode=2;WatchFixture f;
            check(f.startFailure=="mirror-bootstrap-context-event-limit","actual relevant context replay still obeys the original 32 event bound");
            f.watch.stop();check(f.cdp.enables==1,"bounded relevant replay failure cannot resubscribe or retry itself");
        }
        {
            callbackReplayMode=3;WatchFixture f;
            check(f.cdp.wait(101)&&f.stage("context-attached"),"original account signal can arrive while an attachment read is in flight");
            for(unsigned n=0;n<500&&f.cdp.accountChecks==0;++n)Sleep(2);
            check(f.cdp.accountChecks==1&&!f.stage("failed",80)&&f.cdp.teardowns==0,"200-byte current-run account signal keeps original live verification and nonfatal meaning");
        }
        checks+=watchTransportDiagnostics();checks+=watchCloseDiagnostics();
        check(bootstrapSafeReason(std::runtime_error("secret token untrusted parse text"))=="mirror-bootstrap-invalid-message","untrusted exception contents sanitized");
        check(bootstrapSafeReason(std::runtime_error("mirror-bootstrap-owner-mismatch"))=="mirror-bootstrap-owner-mismatch","known error codes retained");
        const auto log=ymcc::deckydiag::record("bootstrap-watch",{{"bootstrapStage","context-attached"},{"contextPid",1},{"contextGeneration",2},{"token","private"},{"endpoint","private"},{"runId","private"},{"expression","private"}});
        check(log.value("bootstrapStage",std::string{})=="context-attached"&&log.value("contextGeneration",0)==2,"safe lifecycle fields retained");
        check(!log.contains("token")&&!log.contains("endpoint")&&!log.contains("runId")&&!log.contains("expression"),"credentials and injected source never logged");
        {std::lock_guard lock(capturedExpressionMutex);std::cout<<"DECKY_HEALTH_TEARDOWN_CAPTURE "<<Json(capturedExpressions).dump()<<"\n";}
        std::cout<<"Bootstrap context event-watch tests passed: "<<checks<<"; realSteam=0; loader=0; hardware=0; idlePolling=0\n";
        WSACleanup();return 0;
    }catch(const std::exception& error){if(contextLifecycleOnly)writeContextLifecycleResult(argv[2],false,checks,error.what());std::cerr<<error.what()<<'\n';WSACleanup();return 1;}
}
