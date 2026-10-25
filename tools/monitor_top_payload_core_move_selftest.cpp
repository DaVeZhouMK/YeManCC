// Actual top publisher, before/current, with independent payloads and ownership checks.
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <vector>
#include "__ROOT__/deps/json/json.hpp"
#include "__ROOT__/native/monitor_gpu_selection.h"
#include "__ROOT__/native/monitor_filesystem_rates.h"
#include "__ROOT__/native/monitor_freshness.h"
#include "__ROOT__/native/monitor_low_slots.h"
#include "__ROOT__/native/monitor_frame_time.h"
#include "__ROOT__/native/monitor_battery_energy.h"
#include "__ROOT__/native/monitor_rtss_global.h"
#include "__ROOT__/native/monitor_fan_receipt.h"
using json=nlohmann::json;
static bool track=false;
static std::size_t allocations=0,allocationBytes=0,checks=0,comparisons=0;
void* operator new(std::size_t n){if(track){++allocations;allocationBytes+=n;}if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
static void check(bool ok,const char* name){++checks;if(!ok){std::cerr<<"FAIL "<<name<<'\n';std::exit(1);}}
__STRUCTURE__
namespace before {
__BEFORE__
}
namespace after {
__AFTER__
}
struct Input {
    NativeMonitorHw hw;
    bool ac=true,hasBattery=true,healthy=true,cpuValid=true;
    int percent=73;
    double remain=41.2,cpu=88;
    long long ts=1700000000000LL,coreTs=1700000000000LL,batteryTs=1700000000000LL,statusTs=1700000000000LL,cpuTs=1700000000000LL;
    ymcc::monitorbattery::Power battery;
    ymcc::monitorbattery::RemainingTime remaining;
    ymcc::monitorrtss::Limit limit;
    ymcc::monitorfan::Receipt fan;
    ymcc::monitorfs::Rates file;
};
static Input seed(){
    Input i;auto&h=i.hw;
    h.packagePower=30;h.freqMhz=3200;h.tempC=65;h.cpuPowerValid=h.cpuClockValid=h.cpuTempValid=true;
    h.cpuVoltageV=1.05;h.gpuVoltageV=.9;h.physicalMemory={8192,24576,32768,25};
    h.gpuMemory={4096,12288,16384,25};h.gpuPowerW=180;h.gpuClockMhz=2500;h.gpuTempC=65;h.gpuLoad=60;
    h.gpuSelectedPresent=h.gpuPinned=true;h.gpuMemorySource="dedicated";
    const char*label="dGPU [#0]: NVIDIA GeForce RTX 5080 / \xe4\xb8\xad\xe6\x96\x87";std::strncpy(h.gpuName.data(),label,h.gpuName.size()-1);
    h.networkDownMBps=4.2;h.networkUpMBps=.3;h.virtualMemoryCommittedFound=true;h.virtualMemoryCommittedMb=12000;
    h.virtualMemoryLoadFound=true;h.virtualMemoryLoadPct=37.5;h.thermalThrottleFound=true;h.thermalThrottleMax=0;
    i.battery={true,false,-35.7,30000,"energy-delta"};i.remaining={true,false,110,"energy-power"};
    i.limit={true,120,i.ts};i.fan={true,true,76,i.ts};i.file={5,2,i.ts};
    return i;
}
static json core(std::size_t n){
    json a=json::array();auto&v=a.get_ref<json::array_t&>();v.resize(n);
    for(std::size_t k=0;k<n;++k)if(k%7)v[k]=double(k%101)+.5;
    return a;
}
static std::string oldCall(const Input&i,const json&c){
    return before::nativeMonitorTopPayloadText(i.hw,i.ac,i.hasBattery,i.percent,i.remain,i.cpu,i.ts,i.healthy,c,i.coreTs,i.battery,i.batteryTs,i.statusTs,i.remaining,i.limit,i.fan,i.file,i.cpuValid,i.cpuTs);
}
static std::string legacyCall(const Input&i,const json&c){
    return after::nativeMonitorTopPayloadText(i.hw,i.ac,i.hasBattery,i.percent,i.remain,i.cpu,i.ts,i.healthy,c,i.coreTs,i.battery,i.batteryTs,i.statusTs,i.remaining,i.limit,i.fan,i.file,i.cpuValid,i.cpuTs);
}
static std::string newCall(const Input&i,json&c,json*transfer){
    return after::nativeMonitorTopPayloadText(i.hw,i.ac,i.hasBattery,i.percent,i.remain,i.cpu,i.ts,i.healthy,c,i.coreTs,i.battery,i.batteryTs,i.statusTs,i.remaining,i.limit,i.fan,i.file,i.cpuValid,i.cpuTs,transfer);
}
static bool sameTyped(const json&a,const json&b){
    if(a.type()!=b.type())return false;
    if(a.is_object()){if(a.size()!=b.size())return false;for(auto it=a.begin();it!=a.end();++it){auto found=b.find(it.key());if(found==b.end()||!sameTyped(it.value(),*found))return false;}return true;}
    if(a.is_array()){if(a.size()!=b.size())return false;for(std::size_t k=0;k<a.size();++k)if(!sameTyped(a[k],b[k]))return false;return true;}
    return a==b;
}
static void compare(const Input&i,const json&c){
    json owned=c,unchanged=c,other="another caller's buffer";
    const auto expected=oldCall(i,c),moved=newCall(i,owned,&owned),legacy=legacyCall(i,c),wrong=newCall(i,unchanged,&other);
    check(expected==moved&&expected==legacy&&expected==wrong,"all complete payload bytes match old production function");
    check(sameTyped(json::parse(expected),json::parse(moved)),"complete nested output types remain identical");
    const bool included=c.is_array()&&!c.empty()&&i.coreTs>0;
    check((included?owned.is_null():owned==c)&&unchanged==c&&other=="another caller's buffer","only explicit same-object handoff of an included array is consumed");
    auto value=json::parse(moved);
    check(value.contains("coreUsage")==included&&value.contains("coreUsageTs")==included,"array and source timestamp omission rules unchanged");
    if(included){check(value["coreUsage"]==c&&value["coreUsageTs"]==i.coreTs,"genuine values, unknown slots, zero and observation time retained");}
    ++comparisons;
}
struct Work {std::size_t count=0,bytes=0;std::string output;};
static Work observe(unsigned mode,const Input&i,json&c){
    allocations=allocationBytes=0;track=true;
    std::string text=mode==0?oldCall(i,c):mode==1?newCall(i,c,&c):legacyCall(i,c);
    track=false;return {allocations,allocationBytes,std::move(text)};
}
int main(int argc,char**argv){
    check(argc==2,"output path supplied");
    auto i=seed();
    check(before::nativeMonitorTopPayloadText(i.hw,true,false,-1,-1,0,i.ts,false)==
          after::nativeMonitorTopPayloadText(i.hw,true,false,-1,-1,0,i.ts,false),
          "legacy caller omitting all new optional proofs keeps exact bytes");
    for(std::size_t n:{0U,1U,2U,16U,32U,64U,128U})for(long long stamp:{-1LL,0LL,i.ts}){
        i.coreTs=stamp;compare(i,core(n));
    }
    i=seed();for(const auto&c:json::array({nullptr,false,12,"not-an-array",json::object(),json::array({nullptr,0,100,99.9})}))compare(i,c);
    // Independent receipts must retain their original health/source/time behavior.
    for(unsigned k=0;k<64;++k){
        i=seed();i.ac=k&1;i.hasBattery=k&2;i.healthy=k&4;i.cpuValid=k&8;i.percent=k&16?73:-1;i.battery.valid=k&32;
        compare(i,core(k%5?16:128));
    }
    i=seed();i.hw.gpuName.fill(0);i.hw.gpuName[0]=char(0xff);compare(i,core(32));
    i=seed();i.cpu=105;i.cpuValid=false;compare(i,core(16));
    // Avoid undefined float-to-int cases: scalar temperature/frequency stay finite.
    std::mt19937_64 rng(0x594d4343ULL);
    for(unsigned k=0;k<1000;++k){
        i=seed();i.hw.packagePower=double(rng()%700)/10;i.hw.tempC=rng()%110;i.hw.freqMhz=rng()%6000;
        i.hw.gpuSelectedPresent=k%3;i.hw.gpuPinned=k%5;i.hw.networkDownMBps=double(rng()%10000)/100;
        i.ts+=k*1000;i.coreTs=k%7?i.ts:0;i.cpuTs=k%11?i.ts:0;i.healthy=k%4;i.cpuValid=k%9;
        auto values=core(rng()%129);if(k%13==0)values=nullptr;compare(i,values);
    }
    json work=json::array();
    for(std::size_t n:{0U,1U,16U,32U,64U,128U})for(bool included:{false,true}){
        i=seed();if(!included)i.coreTs=0;
        for(unsigned warm=0;warm<20;++warm){auto a=core(n),b=a,c=a;observe(0,i,a);observe(1,i,b);observe(2,i,c);}
        std::size_t ac=0,ab=0,bc=0,bb=0,lc=0,lb=0;
        for(unsigned k=0;k<300;++k){
            auto a=core(n),b=a,c=a;const auto old=observe(0,i,a),next=observe(1,i,b),legacy=observe(2,i,c);
            check(old.output==next.output&&old.output==legacy.output,"measured full serialized payloads are byte-identical");
            ac+=old.count;ab+=old.bytes;bc+=next.count;bb+=next.bytes;lc+=legacy.count;lb+=legacy.bytes;
        }
        check(lc<=ac&&lb<=ab,"legacy caller allocation costs do not regress against original publisher");
        if(n&&included)check(lc-bc==600&&lb-bb==300*(sizeof(json::array_t)+n*sizeof(json)),"one actual array copy removed per owned core payload");
        else check(lc==bc&&lb==bb,"empty or omitted array pays no extra copy");
        work.push_back({{"coreSlots",n},{"included",included},{"calls",300},{"before",{{"allocations",ac},{"bytes",ab}}},{"after",{{"allocations",bc},{"bytes",bb}}},{"legacy",{{"allocations",lc},{"bytes",lb}}}});
    }
    json result={{"ok",true},{"checks",checks},{"payloadComparisons",comparisons},{"work",work},{"scope","Actual full old/current native top payload function; deterministic receipt/core inputs. Matching production handoff retains original caller-independent bytes and types. Allocation counter covers complete payload construction/serialization, not input generation, JSON parse, CPU APIs, runtime CPU percentage or timing."}};
    std::ofstream(argv[1])<<result.dump(2);std::cout<<"PASS "<<checks<<" checks / "<<comparisons<<" payloads\n";
}