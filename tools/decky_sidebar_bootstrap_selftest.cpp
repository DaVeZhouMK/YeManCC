// Local Windows identity queries + invalid-input paths only; never contacts Steam/CDP.
#include "../native/decky_sidebar_bootstrap.h"
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
            if(mode_=="ws-silent"){WaitForSingleObject(done_,4000);closesocket(peer);return;}
            std::vector<unsigned char> frame;
            try{for(;;){const auto count=recv(peer,buffer,sizeof(buffer),0);if(count<=0)break;frame.insert(frame.end(),buffer,buffer+count);const auto value=ymcc::deckymirror::decode(frame);if(value.complete){readClientText=value.payload=="hello-bootstrap";const auto encoded=ymcc::deckymirror::encode("fixture-bootstrap-reply");send(peer,encoded.data(),static_cast<int>(encoded.size()),0);break;}}}catch(...){}
            WaitForSingleObject(done_,3000);closesocket(peer);
        });
    }
    ~LocalHttpFixture(){SetEvent(done_);closesocket(listener_);if(worker_.joinable())worker_.join();CloseHandle(done_);CloseHandle(accepted);}
};
int main() {
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
    {
        LocalHttpFixture server("http-silent");
        ymcc::steamlive::HttpHandle session(WinHttpOpen(L"MirrorFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",server.port,0));bool expired=false;
        try{AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/fixture",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),std::make_shared<CancelToken>(),GetTickCount64()+100);request.handshake();}
        catch(const std::exception& error){expired=std::string(error.what())=="mirror-bootstrap-deadline";}
        check(expired);
    }
    WSACleanup();
    std::cout<<"Bootstrap local guard tests passed: "<<checks<<"\n";
}


