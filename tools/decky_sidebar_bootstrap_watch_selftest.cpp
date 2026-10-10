// Actual WinHTTP + WebSocket event fixture; no Steam/Loader/product/config/hardware access.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include "../native/decky_sidebar_bootstrap.h"
#include "../native/decky_sidebar_broker.h"
#include "../native/decky_sidebar_diagnostics.h"
#include <condition_variable>
#include <fstream>
#include <iostream>
using namespace ymcc::deckymirror;
using Json=nlohmann::json;
class CdpFixture {
    SOCKET listener_=INVALID_SOCKET,peer_=INVALID_SOCKET;
    std::thread worker_;
    std::mutex send_,state_;
    std::condition_variable changed_;
    std::vector<int> evaluated_;
public:
    unsigned short port=0;
    std::string run=std::string(32,'b');
    std::atomic<unsigned> enables{0};
    CdpFixture(){
        listener_=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in address{};
        address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(listener_,reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(listener_,1))throw std::runtime_error("fixture listen");
        int length=sizeof(address);getsockname(listener_,reinterpret_cast<sockaddr*>(&address),&length);port=ntohs(address.sin_port);
        worker_=std::thread([this]{
            const auto peer=accept(listener_,nullptr,nullptr);if(peer==INVALID_SOCKET)return;
            {std::lock_guard lock(send_);peer_=peer;}
            try{
                std::string headers;char bytes[8192];
                while(headers.find("\r\n\r\n")==std::string::npos){const int count=recv(peer,bytes,sizeof(bytes),0);if(count<=0)return;headers.append(bytes,count);}
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
                            ++enables;created(101);for(unsigned i=0;i<192;++i)send({{"method","Runtime.consoleAPICalled"},{"params",{{"type","log"},{"args",Json::array()}}}});send({{"id",id},{"result",Json::object()}});
                        }else if(method=="Runtime.evaluate"){
                            const int context=request.at("params").at("contextId");
                            {std::lock_guard lock(state_);evaluated_.push_back(context);changed_.notify_all();}
                            if(context==400)continue; // Silent reply cancellation fixture.
                            if(context==300){send({{"id",id},{"error",{{"code",-32000}}}});continue;}
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
    void send(const Json& value){const auto bytes=encode(value.dump());std::lock_guard lock(send_);if(peer_!=INVALID_SOCKET)::send(peer_,bytes.data(),static_cast<int>(bytes.size()),0);}
    void created(int id,const char* origin="https://steamloopback.host",bool isDefault=true){send({{"method","Runtime.executionContextCreated"},{"params",{{"context",{{"id",id},{"origin",origin},{"auxData",{{"isDefault",isDefault},{"frameId","fixture"}}}}}}}});}
    bool wait(int id){std::unique_lock lock(state_);return changed_.wait_for(lock,std::chrono::seconds(4),[&]{return std::find(evaluated_.begin(),evaluated_.end(),id)!=evaluated_.end();});}
    size_t count(){std::lock_guard lock(state_);return evaluated_.size();}
};
struct WatchFixture {
    CdpFixture cdp;BootstrapWatch watch;std::atomic<bool> owned{true};std::mutex mutex;std::condition_variable changed;std::vector<Json> records;
    WatchFixture(){
        auto session=std::make_shared<ymcc::steamlive::HttpHandle>(WinHttpOpen(L"MirrorWatchFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        WinHttpSetTimeouts(session->h,1000,1000,1500,1500);
        auto connection=std::make_shared<ymcc::steamlive::HttpHandle>(WinHttpConnect(session->h,L"127.0.0.1",cdp.port,0));
        auto cancel=std::make_shared<CancelToken>();const auto deadline=GetTickCount64()+3000;
        AsyncRequest request(WinHttpOpenRequest(connection->h,L"GET",L"/fixture",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),cancel,deadline);
        request.handshake(true);
        auto socket=std::make_shared<AsyncRequest>(WinHttpWebSocketCompleteUpgrade(request.handle(),0),cancel,deadline,true);
        watch.start(session,connection,socket,cancel,[this]{return owned.load();},"fixture guarded readonly expression",cdp.run,GetCurrentProcessId(),[this](const Json& value){std::lock_guard lock(mutex);records.push_back(value);changed.notify_all();});
    }
    ~WatchFixture(){watch.stop();}
    bool stage(const char* expected){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::seconds(4),[&]{return std::any_of(records.begin(),records.end(),[&](const auto& v){return v.value("bootstrapStage",std::string{})==expected;});});}
};
int main(){
    unsigned checks=0;const auto check=[&](bool condition,const char* name){if(!condition)throw std::runtime_error(name);++checks;};
    WSADATA data{};WSAStartup(MAKEWORD(2,2),&data);
    try{
        {
            WatchFixture f;check(f.cdp.wait(101),"existing context receives attachment after Runtime.enable");check(f.stage("context-attached"),"readback confirmed");check(f.cdp.enables==1,"one runtime subscription survives 192 replayed console packets");
            const auto count=f.cdp.count();Sleep(1800);check(f.cdp.count()==count,"idle has no polling or periodic evaluation");
            {std::lock_guard lock(f.mutex);check(std::none_of(f.records.begin(),f.records.end(),[](const auto& v){return v.value("bootstrapStage",std::string{})=="failed";}),"passive wait survives command timeout");}
            f.cdp.send({{"method","Runtime.executionContextsCleared"},{"params",Json::object()}});f.cdp.created(102);check(f.cdp.wait(102),"same-process reload gets new attachment");
            f.cdp.created(201,"https://example.invalid");f.cdp.created(202,"https://steamloopback.host",false);Sleep(100);check(f.cdp.count()==count+1,"foreign and isolated contexts ignored");
            f.cdp.created(300);check(f.cdp.wait(300),"destroyed context reply fixture reached");check(f.stage("context-replaced"),"transient replacement recorded");
            f.cdp.created(301);check(f.cdp.wait(301),"next real event repairs transient navigation without timer");
            const auto start=GetTickCount64();f.watch.stop();check(GetTickCount64()-start<1000,"cancel wakes passive receive promptly");check(f.stage("stopped"),"explicit stop reported");
            const auto final=f.cdp.count();f.cdp.created(302);Sleep(50);check(f.cdp.count()==final,"stopped watch never reattaches");
        }
        {
            WatchFixture f;check(f.cdp.wait(101),"second isolated watch starts");f.cdp.created(400);check(f.cdp.wait(400),"silent reply reached");
            const auto start=GetTickCount64();f.watch.stop();check(GetTickCount64()-start<1000,"cancel wakes silent command IO promptly");
        }
        {
            WatchFixture f;check(f.cdp.wait(101),"ownership test watch starts");f.owned=false;f.cdp.created(500);check(f.stage("failed"),"changed ownership fails closed");check(f.cdp.count()==1,"changed ownership never gets evaluation");
            f.cdp.created(501);Sleep(100);check(f.cdp.count()==1,"terminal ownership failure never retries");
        }
        check(bootstrapSafeReason(std::runtime_error("secret token untrusted parse text"))=="mirror-bootstrap-invalid-message","untrusted exception contents sanitized");
        check(bootstrapSafeReason(std::runtime_error("mirror-bootstrap-owner-mismatch"))=="mirror-bootstrap-owner-mismatch","known error codes retained");
        const auto log=ymcc::deckydiag::record("bootstrap-watch",{{"bootstrapStage","context-attached"},{"contextPid",1},{"contextGeneration",2},{"token","private"},{"endpoint","private"},{"runId","private"},{"expression","private"}});
        check(log.value("bootstrapStage",std::string{})=="context-attached"&&log.value("contextGeneration",0)==2,"safe lifecycle fields retained");
        check(!log.contains("token")&&!log.contains("endpoint")&&!log.contains("runId")&&!log.contains("expression"),"credentials and injected source never logged");
        std::cout<<"Bootstrap context event-watch tests passed: "<<checks<<"; realSteam=0; loader=0; hardware=0; idlePolling=0\n";
        WSACleanup();return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';WSACleanup();return 1;}
}
