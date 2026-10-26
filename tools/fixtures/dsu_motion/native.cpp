#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <xinput.h>
#include "dsu_protocol.h"
#include "legion_motion_message_clock.h"
#include "json.hpp"
#include <atomic>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include <thread>
#include <algorithm>
#include "hc_model_motion_catalog.h"
#include "hc_legacy_motion_catalog.h"
#include <vector>
using json=nlohmann::json;
using std::uint64_t;namespace ds=ymcc::dsu;namespace wire=ymcc::dsu::protocol;
static std::atomic<uint64_t> power{1},g_msiMainSourceRevision{3},g_msiFeedbackModeGeneration{4};
static std::atomic<unsigned> g_xinputTopologyEpoch{2},g_xinputSlotIdentityEpoch{9};
static std::atomic<bool> g_msiDinputRouteDinputArmed{false},g_legionRawInputSubscribed{true},g_inputCaptureStop{false};
static std::atomic<uint64_t> g_hidResolveCacheGeneration{5},g_legionMotionCaptureEpoch{7},g_legionMotionEpochStartedAt{1};
enum class PowerLifecycle {Ready,Suspended};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static uint64_t currentPowerGeneration(){return power.load();}
static unsigned legionMotionSelectedSensorIndex() noexcept{return 0;}
static bool msiClawKnownBoardAuthority(){return false;}
static bool msiControllerNeutralCleanupAllowed(bool){return false;}
static bool msiDinputMainSourceEligible(){return true;}
// @@SNAPSHOT_TYPE@@
static const InputCaptureGamepadSnapshot* t_commonFrameSource=nullptr;
// @@FUNCTIONS@@
extern "C" {void dsu_publish(const ds::Frame&);void dsu_feed(bool,float,float,float,float,float,float,uint16_t,float,float,float,float,uint8_t,uint8_t);
size_t dsu_encode_pad_data_for_test(uint8_t*,size_t,int,uint64_t);void dsu_fixture_init();
#ifndef YMCC_DSU_BEFORE
bool dsu_start_test();uint16_t dsu_test_port();void dsu_stop();bool dsu_stopped() noexcept;
#endif
}
struct Sensor {std::atomic<uint64_t> activeBindingGeneration{11};};
static auto owner=std::make_shared<Sensor>();
static struct {bool invertHorizontal=false,invertVertical=false;}g_realStickTest;
static constexpr int kTickCostPreHostDsu=0;
static struct {uint64_t dsuUs=0;} g_tickPreHostSub;
struct TickPreHostSubScope {TickPreHostSubScope(int,uint64_t*){}};
static void dsuUiOutputNote(bool,bool){}
struct FocusNeutralDsu {bool hasMotion=false;float gx=0,gy=0,gz=0,ax=0,ay=0,az=0;uint16_t buttons=0;float lx=32768,ly=32768,rx=32768,ry=32768;uint8_t lt=0,rt=0;};
static FocusNeutralDsu focusIsolationNeutralDsu(){return {};}
static unsigned checks=0,failures=0;
static json failed=json::array();
static std::string activeCase;
static void check(bool good,const char* group){++checks;if(good)return;++failures;if(failed.size()<30)failed.push_back({{"case",activeCase},{"group",group}});}
static bool mapped(const json& m,const json& raw,unsigned offset,double& x,double& y,double& z){
    return ymccApplyNamedImuMatrix(m.at("id"),raw[offset],raw[offset+1],raw[offset+2],x,y,z);
}
static void produce(const json& row){
    power=1;g_hidResolveCacheGeneration=5;g_legionMotionCaptureEpoch=7;g_legionRawInputSubscribed=true;g_inputCaptureStop=false;g_powerLifecycle=PowerLifecycle::Ready;owner->activeBindingGeneration=11;
    InputCaptureGamepadSnapshot captured;captured.live=true;captured.connectedMask=1;captured.inputSource="xinput";
    captured.readAttemptSequence=1;captured.readStartedTick=captured.readCompletedTick=GetTickCount64();
    captured.readPowerGeneration=1;captured.readTopologyGeneration=2;captured.readIdentityGeneration=9;captured.readContextStable=true;
    t_commonFrameSource=&captured;
    const auto& r=row.at("raw");double gx=r[0],gy=r[1],gz=r[2],ax=r[3],ay=r[4],az=r[5];
    double mgx=0,mgy=0,mgz=0,maxa=0,may=0,maz=0;
    check(mapped(row.at("gyro"),r,0,mgx,mgy,mgz)&&mapped(row.at("accel"),r,3,maxa,may,maz),"model-matrix");
    const bool live=true,gamepadMotionPlaneProven=row.at("motion"),dsuSubmitNeutral=row.at("neutral"),dsuUiNeutral=dsuSubmitNeutral;
    const bool frameSteeringYaw=row.at("yaw"),controllerMotionRoute=row.value("controller",false);
    const auto windowsMotionSensorOwner=owner;const uint64_t gyroBindingGeneration=11;
    std::optional<ymcc::legion_motion::Sample> controllerMotion;
    if(controllerMotionRoute){controllerMotion.emplace();controllerMotion->context={1,5};controllerMotion->source={"fixture-controller",7,0};controllerMotion->capturedAt=GetTickCount64();}
    g_realStickTest.invertHorizontal=row.at("invertH");g_realStickTest.invertVertical=row.at("invertV");
    XINPUT_GAMEPAD canonicalPad{};canonicalPad.wButtons=row.at("buttons");const auto& sticks=row.at("sticks");
    canonicalPad.sThumbLX=sticks[0];canonicalPad.sThumbLY=sticks[1];canonicalPad.sThumbRX=sticks[2];canonicalPad.sThumbRY=sticks[3];canonicalPad.bLeftTrigger=row.at("lt");canonicalPad.bRightTrigger=row.at("rt");
    const auto focusMaskedButtons=canonicalPad.wButtons;
// @@PRODUCER@@
    if(row.at("retirePhysical").get<bool>())power=2;
    if(row.at("retireMotion").get<bool>()){owner->activeBindingGeneration=12;g_legionMotionCaptureEpoch=8;}
    t_commonFrameSource=nullptr;
}
static std::vector<uint8_t> unhex(const std::string& s){std::vector<uint8_t>b;for(size_t i=0;i<s.size();i+=2)b.push_back(uint8_t(std::stoul(s.substr(i,2),nullptr,16)));return b;}
#ifndef YMCC_DSU_BEFORE
static uint32_t independentCrc(const uint8_t* p,size_t n){uint32_t c=0xffffffff;for(size_t i=0;i<n;++i){c^=(i>=8&&i<12)?0:p[i];for(int j=0;j<8;++j)c=(c&1)?(c>>1)^0xedb88320:c>>1;}return c^0xffffffff;}
static std::vector<uint8_t> request(uint32_t type,const std::vector<uint8_t>& payload={}){
    std::vector<uint8_t>b(20+payload.size());b[0]='D';b[1]='S';b[2]='U';b[3]='C';
    wire::put16(b.data()+4,1001);wire::put16(b.data()+6,uint16_t(b.size()-16));wire::put32(b.data()+12,0x10203040);wire::put32(b.data()+16,type);
    std::copy(payload.begin(),payload.end(),b.begin()+20);wire::put32(b.data()+8,independentCrc(b.data(),b.size()));return b;
}
static void requestNegatives(){
    activeCase="request-validation";
    auto expect=[&](std::vector<uint8_t> b,bool valid,const char* group){check(wire::parse(b).valid==valid,group);};
    const auto good=request(0x100000);expect(good,true,"parser-version-valid");
    for(size_t n=0;n<good.size();++n)expect({good.begin(),good.begin()+n},false,"parser-truncation");
    auto b=good;b[0]='X';expect(b,false,"parser-magic");
    b=good;b[8]^=1;expect(b,false,"parser-crc");
    b=good;wire::put16(b.data()+4,1002);wire::put32(b.data()+8,independentCrc(b.data(),b.size()));expect(b,false,"parser-future-version");
    b=good;wire::put16(b.data()+6,3);wire::put32(b.data()+8,independentCrc(b.data(),b.size()));expect(b,false,"parser-short-declaration");
    b=good;wire::put16(b.data()+6,100);expect(b,false,"parser-oversized-declaration");
    expect(request(0x100001,{5,0,0,0,0,1,2,3,0}),false,"parser-port-count-bound");
    expect(request(0x100001,{1,0,0,0,4}),false,"parser-invalid-port");
    expect(request(0x100001,{2,0,0,0,1}),false,"parser-truncated-port-list");
    expect(request(0x100002,{1,0}),false,"parser-truncated-registration");
    expect(request(0x100002,{4,0,0,0,0,0,0,0}),false,"parser-unknown-registration-flag");
    expect(request(0x100002,{1,4,0,0,0,0,0,0}),false,"parser-invalid-registration-id");
    const auto decoded=wire::parse(request(0x100002,{2,0,17,17,17,17,17,17}));
    check(decoded.valid&&decoded.client==0x10203040&&decoded.message==0x100002&&decoded.flags==2&&decoded.mac[5]==17,"parser-independent-header-and-mac");
}
static void sourceChanges(const json& cases){
    const auto input=cases.at(0).at("input");std::array<uint8_t,100> packet{};
    auto output=[&](){return dsu_encode_pad_data_for_test(packet.data(),packet.size(),0,1234)==100;};
    auto zeroMotion=[&](){if(!output())return false;for(size_t off=76;off<100;off+=4){float v=1;std::memcpy(&v,packet.data()+off,4);if(v!=0)return false;}return true;};
    auto zeroPad=[&](){return std::all_of(packet.begin()+36,packet.begin()+40,[](uint8_t b){return b==0;})&&packet[40]==128&&packet[41]==127&&packet[54]==0&&packet[55]==0;};
    activeCase="cached-final-source-guards";
    for(unsigned what=0;what<3;++what){g_xinputTopologyEpoch=2;g_xinputSlotIdentityEpoch=9;produce(input);
        if(what==0)power=2;if(what==1)g_xinputTopologyEpoch=3;if(what==2)g_xinputSlotIdentityEpoch=10;
        check(zeroMotion()&&zeroPad(),"source-physical-retirement-neutralizes-wire");}
    g_xinputTopologyEpoch=2;g_xinputSlotIdentityEpoch=9;produce(input);owner->activeBindingGeneration=12;
    check(zeroMotion()&&!zeroPad(),"source-windows-retirement-keeps-base-pad");
    auto controller=input;controller["controller"]=true;
    for(unsigned what=0;what<5;++what){produce(controller);if(what==0)g_hidResolveCacheGeneration=6;if(what==1)g_legionMotionCaptureEpoch=8;if(what==2)g_legionRawInputSubscribed=false;if(what==3)g_inputCaptureStop=true;if(what==4)g_powerLifecycle=PowerLifecycle::Suspended;
        check(zeroMotion()&&!zeroPad(),"source-controller-retirement-keeps-base-pad");}
    produce(input);
}
static void udp(const json& cases){
    activeCase="actual-loopback-client";WSADATA wsa{};check(WSAStartup(MAKEWORD(2,2),&wsa)==0,"udp-setup");
    for(unsigned round=0;round<3;++round){
        check(dsu_start_test(),"udp-start");const auto port=dsu_test_port();check(port!=0&&port!=26760,"udp-isolated-port");
        SOCKET client=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);DWORD timeout=700;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
        sockaddr_in server{};server.sin_family=AF_INET;server.sin_addr.s_addr=htonl(INADDR_LOOPBACK);server.sin_port=htons(port);
        auto send=[&](const std::vector<uint8_t>& b){return sendto(client,reinterpret_cast<const char*>(b.data()),int(b.size()),0,reinterpret_cast<const sockaddr*>(&server),sizeof(server));};
        auto receive=[&](){std::array<uint8_t,512>b{};int n=recv(client,reinterpret_cast<char*>(b.data()),int(b.size()),0);return n>0?std::vector<uint8_t>(b.begin(),b.begin()+n):std::vector<uint8_t>{};};
        send(request(0x100000));auto version=receive();check(version.size()==24&&wire::u32(version.data()+16)==0x100000&&wire::u16(version.data()+20)==1001,"udp-version");
        send(request(0x100001,{2,0,0,0,2,0}));auto first=receive(),second=receive();
        check(first.size()==32&&second.size()==32&&first[20]==2&&second[20]==0,"udp-explicit-port-ids");
        const auto input=cases.at(0).at("input");produce(input);
        send(request(0x100002,{1,0,0,0,0,0,0,0}));auto a=receive();
        check(a.size()==100&&wire::u32(a.data()+16)==0x100002,"udp-register-data");
        check(a.size()==100&&independentCrc(a.data(),a.size())==wire::u32(a.data()+8),"udp-independent-crc");
        produce(input);auto b=receive();check(a.size()==100&&b.size()==100&&wire::u32(b.data()+32)>wire::u32(a.data()+32),"udp-subscription-pushes-new-frame");
        owner->activeBindingGeneration=12;
        // Registration renews the existing client and asks for the latest cache;
        // no BUS update occurs here, proving the send-side source recheck.
        send(request(0x100002,{1,0,0,0,0,0,0,0}));auto stale=receive();bool zero=stale.size()==100;
        for(unsigned off=76;zero&&off<100;off+=4){float v=1;std::memcpy(&v,stale.data()+off,4);zero=v==0;}
        check(zero,"udp-retired-source-zero-motion");
        check(stale.size()==100&&b.size()==100&&stale[36]==b[36]&&stale[54]==b[54],"udp-retirement-keeps-base-input");
        produce(input);auto fresh=receive();check(fresh.size()==100,"udp-current-source-resumes");
        auto corrupt=request(0x100000);corrupt[8]^=1;send(corrupt);check(receive().empty(),"udp-bad-crc-ignored");
        closesocket(client);
        auto peer=[&](){client=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);DWORD shortTimeout=120;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&shortTimeout),sizeof(shortTimeout));};
        peer();produce(input);send(request(0x100002,{2,0,17,17,17,17,17,17}));auto mac=receive();
        check(mac.size()==100&&mac[20]==1,"udp-mac-selects-raw-slot");check(receive().empty(),"udp-mac-only-one-slot");closesocket(client);
        peer();produce(input);send(request(0x100002,{1,2,0,0,0,0,0,0}));auto slot2=receive();bool emptyMotion=slot2.size()==100&&slot2[20]==2;
        for(unsigned off=76;emptyMotion&&off<100;off+=4){float v=1;std::memcpy(&v,slot2.data()+off,4);emptyMotion=v==0;}
        check(emptyMotion,"udp-slot2-no-invented-second-sensor");closesocket(client);
        peer();send(request(0x100002,{1,3,0,0,0,0,0,0}));check(receive().empty(),"udp-slot3-no-data");closesocket(client);
        peer();produce(input);send(request(0x100002,{0,0,0,0,0,0,0,0}));auto all0=receive(),all1=receive(),all2=receive();
        check(all0.size()==100&&all1.size()==100&&all2.size()==100&&all0[20]==0&&all1[20]==1&&all2[20]==2,"udp-all-logical-slots");check(receive().empty(),"udp-all-no-fourth-slot");
        if(round==0){std::this_thread::sleep_for(std::chrono::milliseconds(5050));produce(input);check(receive().empty(),"udp-five-second-expiry");
            send(request(0x100002,{1,0,0,0,0,0,0,0}));check(receive().size()==100,"udp-expired-client-renews");}
        closesocket(client);const auto start=GetTickCount64();
        do{dsu_stop();if(dsu_stopped())break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(GetTickCount64()-start<1000);
        check(dsu_stopped()&&GetTickCount64()-start<1000,"udp-stop-joins-idle-worker");
    }
    WSACleanup();
}
#endif
int main(int argc,char** argv){
    try{
        if(argc<2||argc>3)return 2;const json vectors=json::parse(std::ifstream(argv[1]));dsu_fixture_init();
        for(const auto& row:vectors){activeCase=row.at("input").at("name");produce(row.at("input"));
            for(int slot=0;slot<4;++slot){std::array<uint8_t,116>packet{};const auto n=dsu_encode_pad_data_for_test(packet.data(),packet.size(),slot,1234);
                const auto expected=unhex(row.at("packets").at(slot));check(n==expected.size(),"fixed-hc-report-length");
                if(n&&expected.size()>=20)check(std::equal(expected.begin(),expected.end(),packet.begin()),"fixed-hc-report-bytes");}
        }
#ifndef YMCC_DSU_BEFORE
        requestNegatives();sourceChanges(vectors);if(argc==2)udp(vectors);
#endif
        std::cout<<json({{"status",failures?"fail":"pass"},{"checks",checks},{"failureCount",failures},{"failures",failed},
          {"vectors",vectors.size()},{"network","ephemeral-loopback-only"},{"hardwareIo",false},{"hostExecuted",false}}).dump()<<'\n';return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
