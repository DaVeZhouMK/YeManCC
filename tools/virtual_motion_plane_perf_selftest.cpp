// Offline actual motion-plane source extraction: scalar input/clock/cost surroundings isolated.
#define NOMINMAX
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
static_assert(std::is_same<json::allocator_type,std::allocator<json>>::value);
struct Allocation {uint64_t calls=0,bytes=0;};
static thread_local bool track=false;
static thread_local Allocation allocations;
void* operator new(size_t n){void*p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();if(track){++allocations.calls;allocations.bytes+=n;}return p;}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
void operator delete[](void*p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t a){void*p=_aligned_malloc(n?n:1,static_cast<size_t>(a));if(!p)throw std::bad_alloc();if(track){++allocations.calls;allocations.bytes+=n;}return p;}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void*p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void*p,size_t,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void*p,size_t,std::align_val_t)noexcept{_aligned_free(p);}
struct Context {
    struct Config {double gyroMultiplier=1.,accelerometerMultiplier=1.;bool yaw=true;} config;
    bool proven=true;
    double cgx=1.125,cgy=-2.25,cgz=3.75,gravityX=.125,gravityY=-.25,gravityZ=.875;
    double linearAccelX=.0625,linearAccelY=-.03125,linearAccelZ=.25,orientationW=1.,orientationX=0.,orientationY=0.,orientationZ=0.;
    float psx=3.25f,psy=-7.75f,wsx=1.5f,wsy=-.5f;
    uint64_t now=80,lastUi=0,steeringReads=0,costScopes=0;
    uintptr_t window=1;
};
static thread_local Context*active=nullptr;
struct Active {Context* previous;explicit Active(Context&c):previous(active){active=&c;}~Active(){active=previous;}};
struct Sub {uint64_t motionPlaneUs=0;};static thread_local Sub g_tickPreHostSub;
static constexpr int kTickCostPreHostMotionPlane=0;
struct TickPreHostSubScope {TickPreHostSubScope(int,uint64_t*){++active->costScopes;}};
bool inputCaptureHcSteeringAxisResolvesYaw(){++active->steeringReads;return active->config.yaw;}
#include "plane-extracted.h"
static uint64_t checks=0;
void check(bool ok,const char*m){++checks;if(!ok)throw std::runtime_error(m);}
bool exact(const json&a,const json&b){
    if(a.type()!=b.type())return false;
    if(a.is_number_float())return std::bit_cast<uint64_t>(a.get<double>())==std::bit_cast<uint64_t>(b.get<double>());
    if(a.is_object()||a.is_array()){if(a.size()!=b.size())return false;auto x=a.begin(),y=b.begin();for(;x!=a.end();++x,++y){if(a.is_object()&&x.key()!=y.key())return false;if(!exact(x.value(),y.value()))return false;}return true;}
    return a==b;
}
json parity(Context input,bool mutate){
    auto a=input,b=input;auto x=beforeFixture::tick(a,mutate),y=currentFixture::tick(b,mutate);
    check(x.dump()==y.dump(),"serialized plane bytes differ");check(exact(x,y),"type/bits differ");
    check(a.lastUi==b.lastUi,"UI gate schedule differs");check(a.steeringReads==b.steeringReads,"snapshot lookup counts differ");check(a.costScopes==b.costScopes,"cost scalar-capture scope differs");
    return y;
}
struct Result {double ns=0;Allocation allocation;uint64_t emitted=0,reads=0;};
using Tick=json(*)(Context&,bool);
static std::atomic<uint64_t>sink{0};
Result measure(Tick fn,int step,bool proven,bool window,bool everyFrame,bool count,int frames){
    Context c;c.proven=proven;c.window=window?1:0;c.now=0;c.lastUi=0;uint64_t sizes=0,emitted=0;
    allocations={};track=count;const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<frames;++i){c.now+=step;c.cgx=(i&1)?1.125:-2.25;c.config.yaw=i&2;if(everyFrame)c.lastUi=c.now-80;const auto out=fn(c,false);sizes+=out.size();if(!out.is_null())++emitted;}
    const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/frames;
    track=false;sink.fetch_add(sizes,std::memory_order_relaxed);return {ns,allocations,emitted,c.steeringReads};
}
json benchmark(const char*name,int step,bool proven,bool window,bool everyFrame){
    measure(beforeFixture::tick,step,proven,window,everyFrame,false,500);measure(currentFixture::tick,step,proven,window,everyFrame,false,500);
    constexpr int frames=10000;std::vector<double>a,b;
    for(int i=0;i<5;++i){if(i&1){b.push_back(measure(currentFixture::tick,step,proven,window,everyFrame,false,frames).ns);a.push_back(measure(beforeFixture::tick,step,proven,window,everyFrame,false,frames).ns);}else{a.push_back(measure(beforeFixture::tick,step,proven,window,everyFrame,false,frames).ns);b.push_back(measure(currentFixture::tick,step,proven,window,everyFrame,false,frames).ns);}}
    const auto ca=measure(beforeFixture::tick,step,proven,window,everyFrame,true,frames),cb=measure(currentFixture::tick,step,proven,window,everyFrame,true,frames);
    check(ca.emitted==cb.emitted,"benchmark emitted frame count mismatch");check(ca.reads==cb.reads,"benchmark per-tick scalar capture mismatch");
    if(!everyFrame)check(cb.allocation.calls<ca.allocation.calls,"no allocation reduction");
    std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
    return {{"name",name},{"ticks",frames},{"beforeMedianNs",a[2]},{"afterMedianNs",b[2]},{"beforeSamplesNs",a},{"afterSamplesNs",b},
      {"beforeAllocCallsPerTick",static_cast<double>(ca.allocation.calls)/frames},{"afterAllocCallsPerTick",static_cast<double>(cb.allocation.calls)/frames},
      {"beforeAllocBytesPerTick",static_cast<double>(ca.allocation.bytes)/frames},{"afterAllocBytesPerTick",static_cast<double>(cb.allocation.bytes)/frames},{"emittedFrames",ca.emitted},{"scalarReads",ca.reads}};
}
int main(){try{
    for(bool p:{false,true})for(uintptr_t w:{uintptr_t(0),uintptr_t(1)})for(uint64_t n:{0ULL,79ULL,80ULL,81ULL,1000ULL,std::numeric_limits<uint64_t>::max()})for(uint64_t last:{0ULL,1ULL,80ULL,900ULL}){
        Context c;c.proven=p;c.window=w;c.now=n;c.lastUi=last;parity(c,true);}
    std::mt19937_64 r(0x20261014);Context state;
    for(int i=0;i<3000;++i){Context c=state;c.now+=1+r()%27;c.proven=r()&1;c.window=(r()%11)?1:0;c.config.yaw=r()&1;
        c.config.gyroMultiplier=std::ldexp(static_cast<double>(static_cast<int32_t>(r())),-30);c.config.accelerometerMultiplier=std::ldexp(static_cast<double>(static_cast<int32_t>(r())),-30);
        c.cgx=std::bit_cast<double>(r());c.cgy=std::bit_cast<double>(r());c.cgz=std::bit_cast<double>(r());c.gravityX=std::bit_cast<double>(r());c.gravityY=std::bit_cast<double>(r());c.gravityZ=std::bit_cast<double>(r());
        c.linearAccelX=std::bit_cast<double>(r());c.orientationW=std::bit_cast<double>(r());c.psx=std::bit_cast<float>(static_cast<uint32_t>(r()));
        parity(c,(i&1)!=0);state=c;if(c.window&&c.now-c.lastUi>=80)state.lastUi=c.now;}
    for(double x:{-0.,0.,std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max(),std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        Context c;c.cgx=x;c.cgy=x;c.gravityY=x;c.linearAccelZ=x;c.config.gyroMultiplier=x;parity(c,true);}
    std::atomic<bool>ok{true};std::vector<std::thread>threads;
    for(int t=0;t<4;++t)threads.emplace_back([&](){for(int i=0;i<500;++i){Context a,b;a.now=b.now=80;auto x=beforeFixture::tick(a,true),y=currentFixture::tick(b,true);if(x.dump()!=y.dump()||!exact(x,y))ok=false;}});
    for(auto&t:threads)t.join();check(ok,"multithread snapshots differ");
    json benches=json::array({benchmark("proven-125Hz",8,true,true,false),benchmark("proven-250Hz",4,true,true,false),benchmark("unproven-125Hz",8,false,true,false),benchmark("proven-no-window",8,true,false,false),benchmark("every-tick-emits-dynamic",80,true,true,true)});
    std::cout<<json({{"pass",true},{"assertions",checks},{"randomStateCases",3000},{"concurrentCases",2000},{"benchmarks",benches},{"hardwareOrProductExecuted",false},
        {"notes","Actual plane builder/snapshot and original gate; surrounding sensor/config/time/cost-scope are fixtures. Snapshot settings changed after original capture to exercise delayed JSON build. Numeric type and IEEE double bits preserved. Entire input and host submission untouched."}}).dump(2)<<'\n';return 0;
}catch(const std::exception&e){track=false;std::cerr<<e.what()<<'\n';return 1;}}
