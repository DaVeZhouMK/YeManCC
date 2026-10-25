// Private DSU lifecycle consumer. No installed app, devices or non-loopback I/O.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "../../../native/dsu_server.h"
#include "../../../native/dsu_protocol.h"
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <latch>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

extern "C" {bool dsu_start_test();std::uint16_t dsu_test_port();}
static_assert(noexcept(dsu_stopped()));
static_assert(std::is_same_v<decltype(&dsu_stop),void(*)()>);
static_assert(std::is_same_v<decltype(&dsu_stopped),bool(*)() noexcept>);

namespace test {
unsigned checks=0,cases=0;
std::string active;
void require(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(active+": "+reason);}
void begin(const std::string& name){active=name;}
void pass(){++cases;std::cout<<"{\"case\":\""<<active<<"\",\"status\":\"PASS\"}\n";}
void transport(){
    namespace p=ymcc::dsu::protocol;
    const auto port=dsu_test_port();require(port!=0&&port!=26760,"ephemeral port only");
    SOCKET client=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    require(client!=INVALID_SOCKET,"own UDP client creation");
    const auto exercise=[&]{
        DWORD timeout=1500;
        require(setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout))==0,"receive timeout");
        sockaddr_in peer{};peer.sin_family=AF_INET;peer.sin_addr.s_addr=htonl(INADDR_LOOPBACK);peer.sin_port=htons(port);
        std::array<std::uint8_t,20> request{};p::header(request,17,p::versionMessage);request[3]='C';p::finish(request);
        require(sendto(client,reinterpret_cast<const char*>(request.data()),int(request.size()),0,reinterpret_cast<const sockaddr*>(&peer),sizeof(peer))==int(request.size()),"send own loopback request");
        std::array<std::uint8_t,128> reply{};sockaddr_in from{};int size=sizeof(from);
        const int n=recvfrom(client,reinterpret_cast<char*>(reply.data()),int(reply.size()),0,reinterpret_cast<sockaddr*>(&from),&size);
        require(n==24&&from.sin_addr.s_addr==htonl(INADDR_LOOPBACK)&&from.sin_port==htons(port),"actual server reply on own loopback port");
        require(reply[3]=='S'&&p::u32(reply.data()+16)==p::versionMessage&&p::u16(reply.data()+20)==1001,"existing DSU version reply");
        require(p::u32(reply.data()+8)==p::crc({reply.data(),24}),"existing reply CRC");
    };
    try{exercise();}catch(...){::closesocket(client);throw;}
    require(::closesocket(client)==0,"own client closed before DSU WSA cleanup");
    std::cout<<"{\"port\":"<<port<<",\"loopback\":true}\n";
}
void publicCases(){
    begin("never-started-and-repeat-stop");require(dsu_stopped(),"never started complete");dsu_stop();dsu_stop();require(dsu_stopped(),"empty stop complete");pass();
    for(unsigned round=0;round<6;++round){
        begin("actual-loopback-start-stop-"+std::to_string(round));
        require(dsu_start_test(),"start_test succeeds");require(!dsu_stopped(),"live server incomplete");
        const auto port=dsu_test_port();require(dsu_start_test()&&port==dsu_test_port(),"live start idempotent");
        transport();dsu_stop();require(dsu_stopped(),"actual release complete");
        dsu_stop();require(dsu_stopped(),"repeat stop complete");require(dsu_test_port()==0,"closed bound port reset");pass();
    }
    begin("concurrent-query-and-stop");require(dsu_start_test(),"concurrent start");
    std::latch gate{3};bool observed=false;
    std::thread reader([&]{gate.count_down();gate.wait();observed=dsu_stopped();});
    std::thread stopper([&]{gate.count_down();gate.wait();dsu_stop();});
    gate.count_down();gate.wait();reader.join();stopper.join();
    require(dsu_stopped(),"joined concurrent callers and complete stop");
    (void)observed;pass();
}
}

#ifndef YMCC_DSU_PUBLIC_CONSUMER
namespace inject {
enum class Acquire {none,startup,socket,stop,frame,network,bind,select,name};
Acquire acquire=Acquire::none;
bool failSocket=false,failNetwork=false,failWsa=false,failEveryHandle=false;
HANDLE failHandle=nullptr;
std::atomic<bool> failWait{false};
std::mutex mx;
std::unordered_set<SOCKET> sockets;
std::unordered_set<HANDLE> handles,networks;
unsigned refs=0,startups=0,creates=0,attempts=0,successes=0,faults=0,blockedUnsafeCalls=0,emergency=0;
void reset(){acquire=Acquire::none;failSocket=failNetwork=failWsa=failEveryHandle=false;failHandle=nullptr;failWait=false;}
bool fail(Acquire leaf){if(acquire!=leaf)return false;++faults;WSASetLastError(WSAENOBUFS);SetLastError(ERROR_NOT_ENOUGH_MEMORY);return true;}
int WSAAPI startup(WORD version,LPWSADATA data){std::lock_guard lock(mx);++startups;if(fail(Acquire::startup))return WSASYSNOTREADY;const int r=::WSAStartup(version,data);if(r==0)++refs;return r;}
SOCKET WSAAPI createSocket(int af,int type,int protocol){std::lock_guard lock(mx);++creates;if(fail(Acquire::socket))return INVALID_SOCKET;const auto s=::socket(af,type,protocol);if(s!=INVALID_SOCKET)sockets.insert(s);return s;}
HANDLE WINAPI createEvent(LPSECURITY_ATTRIBUTES attributes,BOOL manual,BOOL initial,LPCWSTR name){
    std::lock_guard lock(mx);++creates;if(fail(manual?Acquire::stop:Acquire::frame))return nullptr;
    const auto h=::CreateEventW(attributes,manual,initial,name);if(h)handles.insert(h);return h;
}
WSAEVENT WSAAPI createNetwork(){std::lock_guard lock(mx);++creates;if(fail(Acquire::network))return WSA_INVALID_EVENT;const auto h=::WSACreateEvent();if(h!=WSA_INVALID_EVENT)networks.insert(h);return h;}
int WSAAPI closeSocket(SOCKET s){
    std::lock_guard lock(mx);++attempts;
    if(!sockets.contains(s)){++blockedUnsafeCalls;WSASetLastError(WSAENOTSOCK);return SOCKET_ERROR;}
    if(failSocket){++faults;WSASetLastError(WSAEINPROGRESS);return SOCKET_ERROR;}
    const int r=::closesocket(s);if(r==0){sockets.erase(s);++successes;}return r;
}
BOOL WSAAPI closeNetwork(WSAEVENT h){
    std::lock_guard lock(mx);++attempts;
    if(!networks.contains(h)){++blockedUnsafeCalls;WSASetLastError(WSA_INVALID_HANDLE);return FALSE;}
    if(failNetwork){++faults;WSASetLastError(WSA_INVALID_HANDLE);return FALSE;}
    const auto r=::WSACloseEvent(h);if(r){networks.erase(h);++successes;}return r;
}
BOOL WINAPI closeHandle(HANDLE h){
    std::lock_guard lock(mx);++attempts;
    if(!handles.contains(h)){++blockedUnsafeCalls;SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
    if(failEveryHandle||h==failHandle){++faults;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    const auto r=::CloseHandle(h);if(r){handles.erase(h);++successes;}return r;
}
int WSAAPI cleanup(){
    std::lock_guard lock(mx);++attempts;
    // Block an unsafe mutant before it can implicitly destroy a tracked socket.
    if(refs!=1||!sockets.empty()||!networks.empty()||!handles.empty()){++blockedUnsafeCalls;WSASetLastError(WSAEINPROGRESS);return SOCKET_ERROR;}
    if(failWsa){++faults;WSASetLastError(WSAEINPROGRESS);return SOCKET_ERROR;}
    const int r=::WSACleanup();if(r==0){--refs;++successes;}return r;
}
int WSAAPI bindSocket(SOCKET s,const sockaddr* address,int length){
    std::lock_guard lock(mx);const auto* ipv4=reinterpret_cast<const sockaddr_in*>(address);
    if(!sockets.contains(s)||address->sa_family!=AF_INET||ipv4->sin_addr.s_addr!=htonl(INADDR_LOOPBACK)||ipv4->sin_port!=0){++blockedUnsafeCalls;WSASetLastError(WSAEACCES);return SOCKET_ERROR;}
    if(fail(Acquire::bind))return SOCKET_ERROR;return ::bind(s,address,length);
}
int WSAAPI selectEvent(SOCKET s,WSAEVENT h,long events){std::lock_guard lock(mx);if(fail(Acquire::select))return SOCKET_ERROR;return ::WSAEventSelect(s,h,events);}
int WSAAPI socketName(SOCKET s,sockaddr* address,int* length){std::lock_guard lock(mx);if(fail(Acquire::name))return SOCKET_ERROR;return ::getsockname(s,address,length);}
DWORD WINAPI wait(DWORD count,const HANDLE* events,BOOL all,DWORD timeout){if(failWait.exchange(false)){SetLastError(ERROR_GEN_FAILURE);return WAIT_FAILED;}return ::WaitForMultipleObjects(count,events,all,timeout);}
}

// Only this private translation unit substitutes API boundaries. Headers have
// already been parsed; the included implementation bytes are the current source.
#define WSAStartup inject::startup
#define socket inject::createSocket
#define CreateEventW inject::createEvent
#define WSACreateEvent inject::createNetwork
#define closesocket inject::closeSocket
#define WSACloseEvent inject::closeNetwork
#define CloseHandle inject::closeHandle
#define WSACleanup inject::cleanup
#define bind inject::bindSocket
#define WSAEventSelect inject::selectEvent
#define getsockname inject::socketName
#define WaitForMultipleObjects inject::wait
#ifndef YMCC_DSU_IMPLEMENTATION
#define YMCC_DSU_IMPLEMENTATION "../../../native/dsu_server.cpp"
#endif
#include YMCC_DSU_IMPLEMENTATION
#undef WSAStartup
#undef socket
#undef CreateEventW
#undef WSACreateEvent
#undef closesocket
#undef WSACloseEvent
#undef CloseHandle
#undef WSACleanup
#undef bind
#undef WSAEventSelect
#undef getsockname
#undef WaitForMultipleObjects

namespace test {
struct Owners {SOCKET socket;HANDLE stop,frame;WSAEVENT network;bool wsa,thread,live;std::uint16_t port;bool operator==(const Owners&)const=default;};
Owners owners(){std::lock_guard lifecycle(lifecycleMx);std::lock_guard state(stateMx);return {serverSocket,stopEvent,frameEvent,networkEvent,wsaInitialized,serverThread.joinable(),running.load(),boundPort};}
void clean(){
    const auto o=owners();require(!o.thread&&!o.live&&o.socket==INVALID_SOCKET&&!o.stop&&!o.frame&&o.network==WSA_INVALID_EVENT&&!o.wsa&&o.port==0,"all implementation owners settled");
    std::lock_guard lock(inject::mx);require(inject::sockets.empty()&&inject::handles.empty()&&inject::networks.empty()&&inject::refs==0,"all actual API releases recorded successful");require(inject::blockedUnsafeCalls==0,"no unowned or invalid API forwarded");
}
void readOnly(bool expected){
    const auto before=owners();const auto attempts=inject::attempts,creates=inject::creates,startups=inject::startups;
    require(dsu_stopped()==expected,"completion matches full ownership");require(owners()==before,"query retains exact owner records");
    require(inject::attempts==attempts&&inject::creates==creates&&inject::startups==startups,"query does no resource API work");
}
void retry(){
    require(!owners().thread&&!owners().live,"stop joined before cleanup retry");readOnly(false);
    const auto before=owners();const auto startups=inject::startups,creates=inject::creates;
    require(!dsu_start_test(),"start refuses uncleared residuals");require(owners()==before,"blocked start retains residual identities");
    require(inject::startups==startups&&inject::creates==creates,"blocked start acquires no replacement resource");
    const auto attempts=inject::attempts;dsu_stop();require(inject::attempts>attempts&&inject::attempts-attempts<=5,"one finite cleanup pass per explicit stop");readOnly(false);
    inject::reset();dsu_stop();readOnly(true);clean();const auto after=inject::attempts;dsu_stop();require(inject::attempts==after,"repeat complete stop issues no close");
}
void privateCases(){
    clean();
    begin("running-false-with-exited-unjoined-thread");inject::failWait=true;require(dsu_start_test(),"faulted worker starts");
    HANDLE thread=nullptr;{std::lock_guard lifecycle(lifecycleMx);thread=serverThread.native_handle();}
    require(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"owned worker exited at injected wait leaf");
    require(!owners().live&&owners().thread,"running false still owns joinable thread");readOnly(false);dsu_stop();readOnly(true);clean();pass();
    for(unsigned leaf=0;leaf<5;++leaf){
        const char* names[]={"closesocket","WSACloseEvent","CloseHandle-frame","CloseHandle-stop","WSACleanup"};
        begin(std::string("stop-failure-retains-")+names[leaf]);require(dsu_start_test(),"failure scenario starts");const auto original=owners();
        if(leaf==0)inject::failSocket=true;if(leaf==1)inject::failNetwork=true;if(leaf==2)inject::failHandle=original.frame;if(leaf==3)inject::failHandle=original.stop;if(leaf==4)inject::failWsa=true;
        const auto faults=inject::faults;dsu_stop();const auto retained=owners();require(inject::faults>faults,"explicit close failure exercised");require(retained.wsa,"startup responsibility retained");
        if(leaf==0){require(retained.socket==original.socket&&retained.network==original.network&&retained.port==original.port,"socket and associated event owner retained");sockaddr_in address{};int n=sizeof(address);require(::getsockname(retained.socket,reinterpret_cast<sockaddr*>(&address),&n)==0&&address.sin_addr.s_addr==htonl(INADDR_LOOPBACK),"retained socket is a real owned loopback socket");}
        if(leaf==1)require(retained.network==original.network,"network event identity retained");
        if(leaf==2||leaf==3){const auto h=leaf==2?retained.frame:retained.stop;require(h==(leaf==2?original.frame:original.stop),"failed handle identity retained");DWORD flags=0;require(GetHandleInformation(h,&flags)!=0,"retained event is still a valid owned handle");}
        if(leaf==4)require(retained.socket==INVALID_SOCKET&&!retained.stop&&!retained.frame&&retained.network==WSA_INVALID_EVENT,"WSA-only residue");
        retry();pass();
    }
    begin("all-close-failures-progress-in-explicit-passes");require(dsu_start_test(),"mixed failure start");const auto original=owners();
    inject::failSocket=inject::failNetwork=inject::failEveryHandle=inject::failWsa=true;dsu_stop();
    const auto first=owners();require(first.socket==original.socket&&first.stop==original.stop&&first.frame==original.frame&&first.network==original.network&&first.wsa&&!first.thread,"all failed owners retained after join");readOnly(false);
    inject::failSocket=false;dsu_stop();require(owners().socket==INVALID_SOCKET&&owners().network==original.network,"socket progresses independently");
    inject::failNetwork=inject::failEveryHandle=false;dsu_stop();require(owners().wsa&&!owners().frame&&!owners().stop&&owners().network==WSA_INVALID_EVENT,"only failed cleanup responsibility remains");
    retry();pass();
    const std::array<inject::Acquire,8> leaves={inject::Acquire::startup,inject::Acquire::socket,inject::Acquire::stop,inject::Acquire::frame,inject::Acquire::network,inject::Acquire::bind,inject::Acquire::select,inject::Acquire::name};
    for(unsigned i=0;i<leaves.size();++i){
        begin("startup-failure-unwind-"+std::to_string(i));inject::acquire=leaves[i];const auto faults=inject::faults;
        require(!dsu_start_test(),"acquisition failure reported");require(inject::faults>faults,"explicit acquisition failure exercised");readOnly(true);clean();inject::reset();pass();
    }
    for(unsigned i=0;i<3;++i){
        begin("startup-unwind-retains-close-failure-"+std::to_string(i));
        inject::acquire=i==0?inject::Acquire::network:(i==1?inject::Acquire::bind:inject::Acquire::name);
        if(i==0)inject::failEveryHandle=true;if(i==1)inject::failSocket=true;if(i==2)inject::failWsa=true;
        require(!dsu_start_test(),"partial startup rejected");require(owners().wsa,"partial startup cleanup responsibility retained");
        inject::acquire=inject::Acquire::none;retry();pass();
    }
    begin("start-settles-old-residue-before-new-acquisition");require(dsu_start_test(),"restart setup");inject::failWsa=true;dsu_stop();readOnly(false);
    const auto startups=inject::startups;inject::reset();require(dsu_start_test(),"start cleans old responsibility before new acquisition");require(inject::startups==startups+1&&inject::refs==1,"exactly one new reference after old returned");transport();dsu_stop();clean();readOnly(true);pass();
    begin("late-publish-does-not-reopen-resources");ymcc::dsu::Frame frame;dsu_publish(frame);readOnly(true);clean();pass();
}
bool rescue() noexcept {
    // A failed assertion is evidence, never a PASS. Only private owned resources
    // are salvaged so a negative control also exits without leaking its owners.
    ++inject::emergency;inject::reset();dsu_stop();
    std::lock_guard lifecycle(lifecycleMx);std::lock_guard state(stateMx);std::lock_guard lock(inject::mx);
    std::erase_if(inject::sockets,[](SOCKET s){return ::closesocket(s)==0;});
    std::erase_if(inject::networks,[](HANDLE h){return ::WSACloseEvent(h)!=FALSE;});
    std::erase_if(inject::handles,[](HANDLE h){return ::CloseHandle(h)!=FALSE;});
    if(inject::sockets.empty()&&inject::networks.empty()&&inject::handles.empty()&&inject::refs==1&&::WSACleanup()==0)--inject::refs;
    return inject::sockets.empty()&&inject::networks.empty()&&inject::handles.empty()&&inject::refs==0;
}
}
#endif

int main(){
    try{
        test::publicCases();
#ifndef YMCC_DSU_PUBLIC_CONSUMER
        test::privateCases();
        std::cout<<"{\"apiReleaseAttempts\":"<<inject::attempts<<",\"apiReleaseSuccesses\":"<<inject::successes<<",\"injectedFailures\":"<<inject::faults<<",\"blockedUnsafeApiCalls\":"<<inject::blockedUnsafeCalls<<",\"emergencyCleanup\":"<<inject::emergency<<"}\n";
#endif
        std::cout<<"{\"status\":\"PASS\",\"cases\":"<<test::cases<<",\"checks\":"<<test::checks<<"}\n";return 0;
    }catch(const std::exception& error){
        std::cout<<"{\"status\":\"FAIL\",\"case\":\""<<test::active<<"\",\"reason\":\""<<error.what()<<"\",\"checks\":"<<test::checks<<"}\n";
#ifndef YMCC_DSU_PUBLIC_CONSUMER
        const bool rescued=test::rescue();
        std::cout<<"{\"rescueOwnedResourcesReleased\":"<<(rescued?"true":"false")<<",\"blockedUnsafeApiCalls\":"<<inject::blockedUnsafeCalls<<"}\n";
#else
        dsu_stop();
#endif
        return 1;
    }
}
