// Native DSU 1001: fixed HC packet layout, registration and source-bound snapshots.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "dsu_protocol.h"
#include "dsu_server.h"
#include <atomic>
#include <array>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace {
namespace wire=ymcc::dsu::protocol;
using Frame=ymcc::dsu::Frame;
std::mutex stateMx,lifecycleMx;
Frame currentFrame;
std::atomic<bool> running{false},hasClients{false};
SOCKET serverSocket=INVALID_SOCKET;
HANDLE stopEvent=nullptr,frameEvent=nullptr;
WSAEVENT networkEvent=WSA_INVALID_EVENT;
std::thread serverThread;
std::uint32_t serverId=0,sequence=0;
std::uint16_t boundPort=0;
bool wsaInitialized=false;
struct Client {
    sockaddr_in endpoint{};
    std::uint64_t all=0;
    std::array<std::uint64_t,4> ids{},macs{};
    static bool fresh(std::uint64_t at,std::uint64_t now) noexcept{return at&&at<=now&&now-at<5000;}
    bool wants(unsigned slot,std::uint64_t now) const noexcept{return fresh(all,now)||fresh(ids[slot],now)||fresh(macs[slot],now);}
    bool alive(std::uint64_t now) const noexcept{for(unsigned s=0;s<4;++s)if(wants(s,now))return true;return false;}
};
std::vector<Client> clients; // Single DSU owner thread only.
bool sameEndpoint(const sockaddr_in& a,const sockaddr_in& b) noexcept{return a.sin_addr.s_addr==b.sin_addr.s_addr&&a.sin_port==b.sin_port;}
void sendPacket(const sockaddr_in& to,std::span<const std::uint8_t> packet) noexcept {
    if(!running.load(std::memory_order_acquire)||serverSocket==INVALID_SOCKET)return;
    sendto(serverSocket,reinterpret_cast<const char*>(packet.data()),int(packet.size()),0,reinterpret_cast<const sockaddr*>(&to),sizeof(to));
}
void registerClient(const sockaddr_in& from,const wire::Request& request,std::uint64_t now) {
    auto found=std::find_if(clients.begin(),clients.end(),[&](const Client& c){return sameEndpoint(c.endpoint,from);});
    if(found==clients.end()){
        // Expired entries are not accumulated across registrations.
        std::erase_if(clients,[&](const Client& c){return !c.alive(now);});
        if(clients.size()>=256)return;
        clients.push_back(Client{});found=std::prev(clients.end());found->endpoint=from;
    }
    if(request.flags==0)found->all=now;
    else {
        if((request.flags&1)&&request.id<4)found->ids[request.id]=now;
        if(request.flags&2)for(unsigned slot=0;slot<4;++slot){
            if(std::all_of(request.mac.begin(),request.mac.end(),[&](auto b){return b==0x10+slot;}))found->macs[slot]=now;
        }
    }
    hasClients.store(std::any_of(clients.begin(),clients.end(),[&](const Client& c){return c.alive(now);}),std::memory_order_release);
}
bool processRequest(std::span<const std::uint8_t> received,const sockaddr_in& from) {
    const auto request=wire::parse(received);if(!request.valid)return false;
    std::array<std::uint8_t,100> packet{};
    if(request.message==wire::versionMessage){const auto n=wire::encodeVersion(packet,serverId);sendPacket(from,{packet.data(),n});}
    else if(request.message==wire::portsMessage){for(unsigned i=0;i<request.count;++i){const auto n=wire::encodePort(packet,request.ids[i],serverId);sendPacket(from,{packet.data(),n});}}
    else if(request.message==wire::dataMessage){registerClient(from,request,GetTickCount64());return true;}
    return false;
}
void sendFrame() {
    const auto now=GetTickCount64();
    std::erase_if(clients,[&](const Client& c){return !c.alive(now);});
    hasClients.store(!clients.empty(),std::memory_order_release);
    if(clients.empty())return;
    Frame snapshot;{std::lock_guard<std::mutex> lock(stateMx);snapshot=currentFrame;}
    snapshot.timestampMs=now; // HC protocol time is output time, not hardware sample time.
    for(unsigned slot=0;slot<3;++slot){
        const bool wanted=std::any_of(clients.begin(),clients.end(),[&](const Client& c){return c.wants(slot,now);});
        if(!wanted)continue;
        std::array<std::uint8_t,100> packet{};
        // Revalidate immediately before encoding/sending, outside the cache lock.
        // An already-issued UDP datagram cannot be revoked by later retirement.
        const auto state=ymcc::dsu::admit(snapshot,GetTickCount64());
        const auto n=wire::encodeData(packet,slot,serverId,sequence++,snapshot,state);
        for(const auto& client:clients)if(client.wants(slot,now))sendPacket(client.endpoint,{packet.data(),n});
    }
}
void run() noexcept {
    const HANDLE events[]={stopEvent,networkEvent,frameEvent};
    try {
        while(running.load(std::memory_order_acquire)){
            const auto ready=WaitForMultipleObjects(3,events,FALSE,INFINITE);
            if(ready==WAIT_OBJECT_0||!running.load(std::memory_order_acquire))break;
            bool publish=ready==WAIT_OBJECT_0+2;
            if(ready==WAIT_OBJECT_0+1){
                WSANETWORKEVENTS happened{};
                if(WSAEnumNetworkEvents(serverSocket,networkEvent,&happened)==SOCKET_ERROR)break;
                if(happened.lNetworkEvents&FD_CLOSE)break;
                if(happened.lNetworkEvents&FD_READ)for(unsigned i=0;i<64;++i){
                    std::array<std::uint8_t,2048> bytes{};sockaddr_in from{};int length=sizeof(from);
                    const auto n=recvfrom(serverSocket,reinterpret_cast<char*>(bytes.data()),int(bytes.size()),0,reinterpret_cast<sockaddr*>(&from),&length);
                    if(n==SOCKET_ERROR){if(WSAGetLastError()==WSAEWOULDBLOCK)break;continue;}
                    if(n>0)publish=processRequest({bytes.data(),std::size_t(n)},from)||publish;
                }
            }else if(ready!=WAIT_OBJECT_0+2)break;
            if(publish&&running.load(std::memory_order_acquire))sendFrame();
        }
    }catch(...){/* Lifecycle owner still joins and closes. */}
    running.store(false,std::memory_order_release);
}
// Owner records are inspected/mutated with lifecycleMx then stateMx held.
// Callers must join the server before closing resources used by run().
bool resourcesClosed() noexcept {
    return serverSocket==INVALID_SOCKET&&networkEvent==WSA_INVALID_EVENT&&
        !frameEvent&&!stopEvent&&!wsaInitialized;
}
bool closeResources() noexcept {
    // A failed close retains its owner for a later explicit stop/start retry.
    // Keep the associated network event until the socket has actually closed.
    if(serverSocket!=INVALID_SOCKET&&closesocket(serverSocket)==0)serverSocket=INVALID_SOCKET;
    if(serverSocket==INVALID_SOCKET){
        boundPort=0;
        if(networkEvent!=WSA_INVALID_EVENT&&WSACloseEvent(networkEvent))networkEvent=WSA_INVALID_EVENT;
    }
    if(frameEvent&&CloseHandle(frameEvent))frameEvent=nullptr;
    if(stopEvent&&CloseHandle(stopEvent))stopEvent=nullptr;
    // WSACleanup can implicitly close sockets. Do not use it to erase a failed
    // socket close, or surrender the startup reference while any owner remains.
    if(serverSocket==INVALID_SOCKET&&networkEvent==WSA_INVALID_EVENT&&!frameEvent&&!stopEvent&&
       wsaInitialized&&WSACleanup()==0)wsaInitialized=false;
    return resourcesClosed();
}
// lifecycleMx retains the thread handle until a kernel termination receipt.
// A failed/unknown wait never permits closing resources still used by run().
bool joinStoppedServer() noexcept {
    if(!serverThread.joinable())return true;
    if(serverThread.get_id()==std::this_thread::get_id())return false;
    const auto thread=serverThread.native_handle();
    if(!thread||thread==INVALID_HANDLE_VALUE||WaitForSingleObject(thread,0)!=WAIT_OBJECT_0)return false;
    try{serverThread.join();}catch(...){return false;}
    return true;
}
bool start(std::uint16_t port,bool loopback) {
    std::unique_lock<std::mutex> lifecycle(lifecycleMx,std::try_to_lock);
    if(!lifecycle.owns_lock())return false;
    if(running.load(std::memory_order_acquire))return true;
    if(!joinStoppedServer())return false;
    std::unique_lock<std::mutex> lock(stateMx,std::try_to_lock);
    if(!lock.owns_lock())return false;
    if(!closeResources())return false; // Never overwrite an unsettled owner.
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)!=0)return false;
    wsaInitialized=true;
    serverSocket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);frameEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);networkEvent=WSACreateEvent();
    if(serverSocket==INVALID_SOCKET||!stopEvent||!frameEvent||networkEvent==WSA_INVALID_EVENT){closeResources();return false;}
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(loopback?INADDR_LOOPBACK:INADDR_ANY);
    if(bind(serverSocket,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR||
       WSAEventSelect(serverSocket,networkEvent,FD_READ|FD_CLOSE)==SOCKET_ERROR){closeResources();return false;}
    int size=sizeof(address);if(getsockname(serverSocket,reinterpret_cast<sockaddr*>(&address),&size)==SOCKET_ERROR){closeResources();return false;}
    currentFrame={};boundPort=ntohs(address.sin_port);serverId=std::uint32_t(GetTickCount64());sequence=0;
    clients.clear();hasClients.store(false,std::memory_order_release);running.store(true,std::memory_order_release);
    try{serverThread=std::thread(run);}catch(...){running.store(false);closeResources();return false;}
    return true;
}
static ymcc::dsu::Admission legacyNumeric(const Frame& f,std::uint64_t) noexcept{return {f.live,f.motionReady};}
}
extern "C" {
void dsu_publish(const Frame& frame) {
    std::lock_guard<std::mutex> lock(stateMx);
    currentFrame=frame;
    if(running.load(std::memory_order_acquire)&&hasClients.load(std::memory_order_acquire)&&frameEvent)SetEvent(frameEvent);
}
// Numeric compatibility seam used by existing isolated focus tests. The actual
// BUS publishes a typed frame with a retained source and native validator.
void dsu_feed(bool motion,float gx,float gy,float gz,float ax,float ay,float az,
    std::uint16_t buttons,float lx,float ly,float rx,float ry,std::uint8_t lt,std::uint8_t rt) {
    Frame frame;frame.live=true;frame.motionReady=motion;frame.raw={gx,gy,gz,ax,ay,az};frame.transformed=frame.raw;
    frame.buttons=buttons;frame.lt=lt;frame.rt=rt;frame.timestampMs=GetTickCount64();frame.validate=legacyNumeric;
    const auto axis=[](float v){if(!std::isfinite(v))return std::int16_t(0);v=std::clamp(v,0.0f,65535.0f);return std::int16_t(int(v+0.5f)-32768);};
    frame.lx=axis(lx);frame.ly=axis(ly);frame.rx=axis(rx);frame.ry=axis(ry);dsu_publish(frame);
}
std::size_t dsu_encode_pad_data_for_test(std::uint8_t* out,std::size_t capacity,int slot,std::uint64_t timestampMs) {
    if(!out||slot<0||slot>=4)return 0;Frame frame;std::uint32_t id=0;
    {std::lock_guard<std::mutex> lock(stateMx);frame=currentFrame;id=serverId;}
    frame.timestampMs=timestampMs;return wire::encodeData({out,capacity},unsigned(slot),id,0,frame,ymcc::dsu::admit(frame,GetTickCount64()));
}
bool dsu_start(){return start(26760,false);}
void dsu_stop(){
    std::unique_lock<std::mutex> lifecycle(lifecycleMx,std::try_to_lock);
    if(!lifecycle.owns_lock())return;
    running.store(false,std::memory_order_release);
    // Only lifecycle owners write/close stopEvent. Publishers access frameEvent
    // under stateMx, never stopEvent; request stop even while stateMx is busy.
    if(stopEvent)SetEvent(stopEvent);
    if(!joinStoppedServer())return;
    std::unique_lock<std::mutex> lock(stateMx,std::try_to_lock);
    if(!lock.owns_lock())return;
    closeResources();currentFrame={};
    clients.clear();hasClients.store(false);
}
bool dsu_stopped() noexcept {
    try {
        std::unique_lock<std::mutex> lifecycle(lifecycleMx,std::try_to_lock);
        if(!lifecycle.owns_lock())return false;
        std::unique_lock<std::mutex> lock(stateMx,std::try_to_lock);
        if(!lock.owns_lock())return false;
        return !running.load(std::memory_order_acquire)&&!serverThread.joinable()&&resourcesClosed();
    }catch(...){return false;} // Failure to inspect ownership cannot prove completion.
}
#ifdef YMCC_DSU_TEST
bool dsu_start_test(){return start(0,true);}
std::uint16_t dsu_test_port(){std::lock_guard<std::mutex> lock(stateMx);return boundPort;}
#endif
}
#endif
