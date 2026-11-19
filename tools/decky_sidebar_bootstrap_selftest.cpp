// Local Windows identity queries + invalid-input paths only; never contacts Steam/CDP.
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
#define WinHttpWebSocketQueryCloseStatus fixtureCloseQuery
#include "../native/decky_sidebar_bootstrap.h"
#undef WinHttpWebSocketQueryCloseStatus
#include "../native/decky_sidebar_broker.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
// All endpoint traffic is confined to a source-built fixture on loopback.
// No Steam debug endpoint is discovered or contacted.
class LocalHttpFixture {
    SOCKET listener_=INVALID_SOCKET;
    std::thread worker_;
    HANDLE done_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::string mode_;
public:
    HANDLE accepted=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    unsigned short port=0;
    std::atomic<bool> readClientText{false};
    explicit LocalHttpFixture(std::string mode):mode_(std::move(mode)) {
        listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(listener_==INVALID_SOCKET)throw std::runtime_error("fixture socket");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR||listen(listener_,1)==SOCKET_ERROR)throw std::runtime_error("fixture bind");
        int size=sizeof(address);getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&size);port=ntohs(address.sin_port);
        worker_=std::thread([this]{
            SOCKET peer=accept(listener_,nullptr,nullptr);if(peer==INVALID_SOCKET)return;
            DWORD timeout=3000;setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
            std::string request;char buffer[4096];
            while(request.find("\r\n\r\n")==std::string::npos){const auto count=recv(peer,buffer,sizeof(buffer),0);if(count<=0){closesocket(peer);return;}request.append(buffer,count);if(request.size()>8192){closesocket(peer);return;}}
            SetEvent(accepted);
            if(mode_=="http-reset"){
                linger reset{1,0};setsockopt(peer,SOL_SOCKET,SO_LINGER,reinterpret_cast<const char*>(&reset),sizeof(reset));closesocket(peer);return;
            }
            if(mode_=="http-silent"){WaitForSingleObject(done_,4000);closesocket(peer);return;}
            if(mode_=="http-body"){
                const std::string response="HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n[]";
                send(peer,response.data(),static_cast<int>(response.size()),0);closesocket(peer);return;
            }
            const std::string marker="sec-websocket-key:";const auto lowercase=ymcc::deckymirror::lower(request);const auto begin=lowercase.find(marker);
            if(begin==std::string::npos){closesocket(peer);return;}
            const auto end=request.find("\r\n",begin);const auto key=ymcc::deckymirror::trim(request.substr(begin+marker.size(),end-begin-marker.size()));
            const std::string response="HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Accept: "+ymcc::deckymirror::websocketAccept(key)+"\r\n\r\n";
            send(peer,response.data(),static_cast<int>(response.size()),0);
            if(mode_.rfind("ws-close-",0)==0){
                std::string frame;frame.push_back(char(0x88));
                if(mode_=="ws-close-empty")frame.push_back(0);
                else{const unsigned code=mode_=="ws-close-restart"?1012:mode_=="ws-close-server-error"?1011:1000;const auto reason=mode_=="ws-close-max"?std::string(123,'x'):std::string("close-private-token");frame.push_back(char(2+reason.size()));frame.push_back(char(code>>8));frame.push_back(char(code&255));frame+=reason;}
                send(peer,frame.data(),static_cast<int>(frame.size()),0);WaitForSingleObject(done_,3000);closesocket(peer);return;
            }
            if(mode_=="ws-silent"){WaitForSingleObject(done_,4000);closesocket(peer);return;}
            std::vector<unsigned char> frame;
            try{for(;;){const auto count=recv(peer,buffer,sizeof(buffer),0);if(count<=0)break;frame.insert(frame.end(),buffer,buffer+count);const auto value=ymcc::deckymirror::decode(frame);if(value.complete){readClientText=value.payload=="hello-bootstrap";const auto encoded=ymcc::deckymirror::encode("fixture-bootstrap-reply");send(peer,encoded.data(),static_cast<int>(encoded.size()),0);break;}}}catch(...){}
            WaitForSingleObject(done_,3000);closesocket(peer);
        });
    }
    ~LocalHttpFixture(){SetEvent(done_);closesocket(listener_);if(worker_.joinable())worker_.join();CloseHandle(done_);CloseHandle(accepted);}
};
static unsigned asyncDiagnosticChecks(){
    unsigned checks=0;const auto check=[&](bool condition,const char* name){if(!condition)throw std::runtime_error(name);++checks;};
    using namespace ymcc::deckymirror;
    {
        LocalHttpFixture server("http-reset");
        ymcc::steamlive::HttpHandle session(WinHttpOpen(L"MirrorDiagnosticsFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",server.port,0));
        std::weak_ptr<AsyncState> pending;bool observed=false;
        {
            AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/fixture",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),std::make_shared<CancelToken>(),GetTickCount64()+3000);
            pending=request.callbackState();
            try{request.handshake();}catch(const std::exception& error){
                observed=true;const auto failure=bootstrapTransportFailure(error);
                check(std::string(error.what())=="mirror-bootstrap-async-error","numeric diagnostics preserve original callback recovery reason");
                DWORD actual=0;{auto state=pending.lock();std::lock_guard lock(state->mutex);actual=state->error;}
                check(actual!=0,"a real WinHTTP callback supplies the numeric failure");
                check(failure.value("codeKnown",false)&&failure.value("code",DWORD{0})==actual,"real callback code reaches bounded diagnostics unchanged");
                check(failure.value("domain",std::string{})=="winhttp","numeric callback failure domain is explicit");
                const auto stage=failure.value("sourceStage",std::string{});check(stage=="send"||stage=="discovery","same in-flight handshake stage is retained");
                check(failure.dump().size()<ymcc::steamfailure::recordLimit&&failure.dump().find("127.0.0.1")==std::string::npos,"callback diagnostics have fixed fields without peer data");
            }
        }
        check(observed,"the real reset peer actually fails");const auto until=GetTickCount64()+1500;while(!pending.expired()&&GetTickCount64()<until)Sleep(5);
        check(pending.expired(),"diagnostic exception retains no callback owner");
    }
    for(const char* reason:{"mirror-bootstrap-canceled","mirror-bootstrap-deadline","mirror-bootstrap-ws-closed"})
        check(bootstrapTransportFailure(std::runtime_error(reason)).is_null(),"unknown code stays unknown without attaching a previous failure");
    return checks;
}
static unsigned asyncCloseChecks(){
    using namespace ymcc::deckymirror;unsigned checks=0;
    const auto check=[&](bool condition,const char* message){if(!condition)throw std::runtime_error(message);++checks;};
    for(const auto& mode:{std::string("ws-close-normal"),std::string("ws-close-server-error"),std::string("ws-close-restart"),std::string("ws-close-empty"),std::string("ws-close-max"),std::string("ws-close-query-failed")}){
        closeQueryCalls=0;closeQueryRefuse=mode=="ws-close-query-failed";
        LocalHttpFixture server(mode);auto token=std::make_shared<CancelToken>();std::weak_ptr<AsyncState> pending;
        ymcc::steamlive::HttpHandle session(WinHttpOpen(L"MirrorCloseFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",server.port,0));bool failed=false;
        {
            const auto deadline=GetTickCount64()+3000;
            AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/close",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),token,deadline);request.handshake(true);
            AsyncRequest socket(WinHttpWebSocketCompleteUpgrade(request.handle(),0),token,deadline,true);pending=socket.callbackState();
            try{socket.receiveText(100);}catch(const std::exception& error){
                failed=true;const auto failure=bootstrapTransportFailure(error);const auto reason=std::string(error.what());
                std::cout<<"CLOSE_DIAGNOSTIC_OBSERVATION "<<nlohmann::json{{"mode",mode},{"reason",reason},{"failure",failure}}.dump()<<"\n";
                check(failure.is_object(),"graceful close metadata missing");
                if(mode=="ws-close-restart"&&reason!="mirror-bootstrap-ws-closed"){
                    // This Windows provider rejects 1012 as an invalid server
                    // response. Keep its actual error, never fabricate a close.
                    check((reason=="mirror-bootstrap-async-error"||reason=="mirror-bootstrap-ws-receive-failed")&&failure.value("domain",std::string{})=="winhttp"&&failure.value("codeKnown",false)&&failure.value("code",0)==ERROR_WINHTTP_INVALID_SERVER_RESPONSE,"provider-rejected close preserves the actual receive error");
                    check(closeQueryCalls==0,"a provider error never queries or fabricates peer close status");
                }else{
                    check(reason=="mirror-bootstrap-ws-closed","accepted graceful close preserves the original recovery reason");
                    check(failure.value("domain",std::string{})=="websocket-close","accepted peer close stays distinct from WinHTTP failure");
                    if(closeQueryRefuse)check(!failure.value("codeKnown",true)&&!failure.contains("code"),"failed query discards even a partially written status");
                    else{const unsigned expected=mode=="ws-close-restart"?1012:mode=="ws-close-server-error"?1011:mode=="ws-close-empty"?1005:1000;check(failure.value("codeKnown",false)&&failure.value("code",0u)==expected,"actual peer close reaches bounded numeric diagnostics");}
                    check(closeQueryCalls==1,"one completed close uses exactly one local query");
                }
                check(failure.value("sourceStage",std::string{})=="receive","current receive stage is retained");
                check(ymcc::steamfailure::normalize(failure)==failure,"existing projector accepts bounded close and error facts");
                check(failure.dump().size()<ymcc::steamfailure::recordLimit&&failure.dump().find("close-private-token")==std::string::npos&&failure.dump().find(std::string(20,'x'))==std::string::npos,"close reason stays outside diagnostics");
            }
        }
        check(failed,"real close or provider-error receive reached a terminal exception");const auto until=GetTickCount64()+1500;while(!pending.expired()&&GetTickCount64()<until)Sleep(5);check(pending.expired(),"close metadata retains no callback lifetime");closeQueryRefuse=false;
    }
    {
        closeQueryCalls=0;LocalHttpFixture server("ws-silent");auto token=std::make_shared<CancelToken>();
        ymcc::steamlive::HttpHandle session(WinHttpOpen(L"MirrorCloseCancelFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",server.port,0));const auto deadline=GetTickCount64()+3000;
        AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/close",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),token,deadline);request.handshake(true);
        AsyncRequest socket(WinHttpWebSocketCompleteUpgrade(request.handle(),0),token,deadline,true);token->cancel();bool canceled=false;
        try{socket.receiveText(100);}catch(const std::exception& error){canceled=std::string(error.what())=="mirror-bootstrap-canceled";check(bootstrapTransportFailure(error).is_null(),"cancel cannot inherit a preceding close code");}
        check(canceled&&closeQueryCalls==0,"cancel keeps original priority without status query");
    }
    return checks;
}

int main(int argc,char** argv) {
    if(argc==2&&std::string(argv[1])=="--async-close-diagnostics"){WSADATA sockets{};if(WSAStartup(MAKEWORD(2,2),&sockets))return 2;try{const auto checks=asyncCloseChecks();WSACleanup();std::cout<<"Bootstrap close diagnostic tests passed: "<<checks<<"\n";return 0;}catch(const std::exception& error){WSACleanup();std::cerr<<error.what()<<"\n";return 1;}}
    if(argc==2&&std::string(argv[1])=="--async-diagnostics"){WSADATA sockets{};if(WSAStartup(MAKEWORD(2,2),&sockets))return 2;try{const auto checks=asyncDiagnosticChecks();WSACleanup();std::cout<<"Bootstrap async diagnostic tests passed: "<<checks<<"\n";return 0;}catch(const std::exception& error){WSACleanup();std::cerr<<error.what()<<"\n";return 1;}}
    int checks=0;
    auto check=[&](bool condition){if(!condition)throw std::runtime_error("Bootstrap guard assertion failed");++checks;};
    check(ymcc::deckymirror::sameWindowsUserAndSession(GetCurrentProcessId()));
    check(!ymcc::deckymirror::sameWindowsUserAndSession(0));
    for (const auto& value : {
        nlohmann::json::object(),
        nlohmann::json{{"endpoint",42},{"token","x"},{"runId","y"}},
        nlohmann::json{{"endpoint","ws://127.0.0.1:1/mirror"},{"token","bad"},{"runId",std::string(32,'a')}}
    }) {
        const auto result=ymcc::deckymirror::injectBootstrap(L"NEVER-QUERY-STEAM",value);
        check(!result.value("ok",true));check(result.value("reason",std::string{})=="mirror-bootstrap-invalid");
        check(!result.value("mutated",true));
    }
    {
        ymcc::deckymirror::ObservedProcess observed(GetCurrentProcessId());
        check(observed.alive());check(observed.pid==GetCurrentProcessId()&&observed.created>0);
        ymcc::deckymirror::ContextHandleSlot slot;slot.replace(observed.release());
        check(!observed.alive());HANDLE lease=slot.take();
        check(lease&&WaitForSingleObject(lease,0)==WAIT_TIMEOUT&&slot.take()==nullptr);CloseHandle(lease);
    }
    {
        bool rejected=false;try{ymcc::deckymirror::ObservedProcess invalid(0);}catch(const std::exception&){rejected=true;}
        check(rejected);
    }
    check(!std::is_copy_assignable_v<ymcc::deckymirror::ObservedProcess> && !std::is_copy_constructible_v<ymcc::deckymirror::ContextHandleSlot> && !std::is_copy_assignable_v<ymcc::deckymirror::ContextHandleSlot>);
    WSADATA winsock{};check(WSAStartup(MAKEWORD(2,2),&winsock)==0);
    using namespace ymcc::deckymirror;
    CancelSignal signal;signal.renew();auto oldToken=signal.current();signal.cancel();check(oldToken->canceled());
    signal.renew();check(!signal.current()->canceled());check(oldToken->canceled());
    {
        LocalHttpFixture server("http-body");
        ymcc::steamlive::HttpHandle session(WinHttpOpen(L"MirrorFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",server.port,0));
        AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/fixture",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),std::make_shared<CancelToken>(),GetTickCount64()+3000);
        request.handshake();check(request.status()==200);check(request.httpBody(100)=="[]");
    }
    {
        LocalHttpFixture server("ws-text");
        ymcc::steamlive::HttpHandle session(WinHttpOpen(L"MirrorFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",server.port,0));auto token=std::make_shared<CancelToken>();const auto deadline=GetTickCount64()+3000;
        AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/fixture",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),token,deadline);
        request.handshake(true);check(request.status()==101);
        AsyncRequest socket(WinHttpWebSocketCompleteUpgrade(request.handle(),0),token,deadline,true);
        socket.sendText("hello-bootstrap");check(socket.receiveText(100)=="fixture-bootstrap-reply");check(server.readClientText);
    }
    for(const auto& mode:{std::string("http-silent"),std::string("ws-silent")}){
        LocalHttpFixture server(mode);auto token=std::make_shared<CancelToken>();
        ymcc::steamlive::HttpHandle session(WinHttpOpen(L"MirrorFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",server.port,0));
        const auto started=GetTickCount64();std::weak_ptr<AsyncState> closingState;bool canceled=false;
        std::thread cancel([&]{if(WaitForSingleObject(server.accepted,2000)==WAIT_OBJECT_0){Sleep(40);token->cancel();}});
        try{
            AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/fixture",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),token,GetTickCount64()+3000);
            closingState=request.callbackState();request.handshake(mode=="ws-silent");
            if(mode=="ws-silent"){
                AsyncRequest socket(WinHttpWebSocketCompleteUpgrade(request.handle(),0),token,GetTickCount64()+3000,true);
                closingState=socket.callbackState();socket.receiveText(100);
            }
        }catch(const std::exception& error){canceled=std::string(error.what())=="mirror-bootstrap-canceled";}
        cancel.join();check(canceled);check(GetTickCount64()-started<1500);
        const auto until=GetTickCount64()+1500;while(!closingState.expired()&&GetTickCount64()<until)Sleep(5);
        check(closingState.expired());
    }
    // A replacement binding must cancel the preceding real WinHTTP wait,
    // including a passive WebSocket receive with no peer response. No Steam IO.
    for(const auto& mode:{std::string("http-silent"),std::string("ws-silent")}){
        LocalHttpFixture server(mode);CancelSignal owner;owner.renew();auto previous=owner.current();
        ymcc::steamlive::HttpHandle session(WinHttpOpen(L"MirrorGenerationFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",server.port,0));
        const auto started=GetTickCount64();std::vector<std::weak_ptr<AsyncState>> pending;bool canceled=false;
        std::thread replace([&]{if(WaitForSingleObject(server.accepted,2000)==WAIT_OBJECT_0){Sleep(40);owner.renew();}});
        try{
            AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/generation",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),previous,GetTickCount64()+1000);
            pending.push_back(request.callbackState());request.handshake(mode=="ws-silent");
            if(mode=="ws-silent"){
                AsyncRequest socket(WinHttpWebSocketCompleteUpgrade(request.handle(),0),previous,GetTickCount64()+1000,true);
                pending.push_back(socket.callbackState());socket.receiveText(100);
            }
        }catch(const std::exception& error){canceled=std::string(error.what())=="mirror-bootstrap-canceled";}
        replace.join();check(canceled);check(GetTickCount64()-started<700);
        check(previous->canceled());auto current=owner.current();check(current!=previous&&!current->canceled());
        previous->cancel();check(!current->canceled());owner.cancel();check(current->canceled());
        const auto until=GetTickCount64()+1500;
        const auto closed=[&]{return std::all_of(pending.begin(),pending.end(),[](const auto& state){return state.expired();});};
        while(!closed()&&GetTickCount64()<until)Sleep(5);
        check(closed());
    }
    {
        LocalHttpFixture server("http-silent");
        ymcc::steamlive::HttpHandle session(WinHttpOpen(L"MirrorFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",server.port,0));bool expired=false;
        try{AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/fixture",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),std::make_shared<CancelToken>(),GetTickCount64()+100);request.handshake();}
        catch(const std::exception& error){expired=std::string(error.what())=="mirror-bootstrap-deadline";}
        check(expired);
    }
    checks+=asyncDiagnosticChecks();checks+=asyncCloseChecks();
    WSACleanup();
    std::cout<<"Bootstrap local guard tests passed: "<<checks<<"\n";
    return 0;
}


