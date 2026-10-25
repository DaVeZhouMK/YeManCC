// Isolated actual-source fixture. Only context values are stubs; the cache,
// append functions and full SUBMIT_FRAME emitter are extracted unchanged.
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <new>
#include <numeric>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include <crtdbg.h>
#include "json.hpp"
using json = nlohmann::json;
static_assert(std::is_same_v<json::number_float_t,double>);
static_assert(std::is_same_v<json::allocator_type,std::allocator<json>>);
static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559);
struct Count {std::uint64_t calls=0,bytes=0;};
static thread_local bool counting=false,inHook=false;
static thread_local Count allocations;
static thread_local long long failAt=-1;
static thread_local void (*allocationHook)(void*)=nullptr;
static thread_local void* hookContext=nullptr;
static void observeAllocation(size_t n){
    if(allocationHook&&!inHook){
        inHook=true;bool oldCounting=counting;auto oldFail=failAt;counting=false;failAt=-1;
        allocationHook(hookContext);counting=oldCounting;failAt=oldFail;inHook=false;
    }
    if(failAt==0){failAt=-1;throw std::bad_alloc();}
    if(failAt>0)--failAt;
    if(counting){++allocations.calls;allocations.bytes+=n;}
}
void* operator new(size_t n){observeAllocation(n);if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
void operator delete[](void*p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t a){observeAllocation(n);if(void*p=_aligned_malloc(n?n:1,size_t(a)))return p;throw std::bad_alloc();}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void*p,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete(void*p,size_t,std::align_val_t)noexcept{_aligned_free(p);}
void operator delete[](void*p,size_t,std::align_val_t)noexcept{_aligned_free(p);}
#include "context.hpp"
#include "before.hpp"
#include "candidate.hpp"
static std::atomic<std::uint64_t> assertions{0};
static std::uint64_t sink=0;
static void check(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
static std::uint64_t random64(std::uint64_t& x){x+=0x9e3779b97f4a7c15ULL;auto z=x;z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);}
static double fromBits(std::uint64_t bits){return std::bit_cast<double>(bits);}
static std::uint64_t bitsOf(double v){return std::bit_cast<std::uint64_t>(v);}
static size_t usedCount(const auto& c){return std::count(c.used_.begin(),c.used_.end(),std::uint8_t(1));}
static void equalState(const before::C6aNumberTextCache&a,const candidate::C6aNumberTextCache&b){
    check(a.used_==b.used_,"ordinary used publication trace differs");
    check(a.keys_==b.keys_,"ordinary bitkeys/probe ordering differs");
    check(a.vals_==b.vals_,"ordinary stored values differ");
    check(a.scratch_==b.scratch_,"ordinary scratch value differs");
}
static std::vector<double> boundaries(){
    std::vector<double> values;
    for(auto b: {0ULL,0x8000000000000000ULL,1ULL,0x8000000000000001ULL,
                0xfffffffffffffULL,0x10000000000000ULL,0x7fefffffffffffffULL,
                0xffefffffffffffffULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,
                0x7ff0000000000001ULL,0x7ff8000000000000ULL,0xffffffffffffffffULL})values.push_back(fromBits(b));
    for(int i=1;i<=64;++i){values.push_back(fromBits(0x7ff8000000000000ULL|std::uint64_t(i)));values.push_back(fromBits(0xfff0000000000000ULL|std::uint64_t(i)));}
    for(int exponent:{-1074,-1022,-1021,-53,-1,0,1,52,53,1023}){
        const double x=std::ldexp(1.0,exponent);
        for(double v:{x,std::nextafter(x,0.0),std::nextafter(x,std::numeric_limits<double>::infinity())}){values.push_back(v);values.push_back(-v);}
    }
    for(int exponent:{-308,-100,-6,-5,-4,-3,0,14,15,16,100,308}){
        const double x=std::pow(10.,exponent);
        for(double v:{x,std::nextafter(x,0.0),std::nextafter(x,std::numeric_limits<double>::infinity())}){values.push_back(v);values.push_back(-v);}
    }
    for(double v:{0.1,-0.1,1.2345678901234567,9007199254740991.,9007199254740992.,9007199254740994.})values.push_back(v);
    return values;
}
static json scalarSuite(){
    before::C6aNumberTextCache a;candidate::C6aNumberTextCache b;
    auto values=boundaries();const size_t boundaryCount=values.size();
    std::uint64_t seed=0x20261015c6aULL;for(int i=0;i<150000;++i)values.push_back(fromBits(random64(seed)));
    for(size_t i=0;i<values.size();++i){
        const double v=values[i];const auto expected=json(v).dump();
        check(a.text(v)==expected,"before scalar differs from dump oracle");
        check(b.text(v)==expected,"candidate scalar byte mismatch");
        check(candidate::C6aNumberTextCache::format(v)==expected,"pure formatter byte mismatch");
        check(bitsOf(v)==bitsOf(values[i]),"input IEEE bits changed");
        if(i%10000==0)equalState(a,b);
    }
    equalState(a,b);check(a.text(0.)=="0.0"&&b.text(-0.)=="-0.0","signed zero changed");
    return {{"randomIEEE64",150000},{"boundaryCases",boundaryCount},{"usedSlots",usedCount(b)},
        {"normalStateTraceEqual",true},{"directFormatterOracleEqual",true}};
}
static std::vector<double> sameBucket(size_t wanted,size_t count){
    std::vector<double> found;std::uint64_t seed=0xabcddcba;
    while(found.size()<count){auto bits=random64(seed);double v=fromBits(bits);
        if(std::isfinite(v)&&(before::C6aNumberTextCache::mix(bits)&8191)==wanted)found.push_back(v);}
    return found;
}
static std::vector<double> fillCache(auto& c){
    std::vector<double> keys;keys.reserve(8192);std::uint64_t seed=0x323232;
    while(keys.size()<8192){const auto bits=random64(seed);const double v=fromBits(bits);const auto slot=before::C6aNumberTextCache::mix(bits)&8191;
        if(std::isfinite(v)&&!c.used_[slot]){check(c.text(v)==json(v).dump(),"full-table population mismatch");keys.push_back(v);}}
    return keys;
}
static json collisionSuite(){
    before::C6aNumberTextCache a;candidate::C6aNumberTextCache b;
    const auto values=sameBucket(8190,7);
    for(size_t i=0;i<values.size();++i){
        check(a.text(values[i])==b.text(values[i]),"wrapped collision/fallback mismatch");equalState(a,b);
        check(usedCount(a)==std::min<size_t>(i+1,4),"four-probe capacity changed");
    }
    check(a.used_[8190]&&a.used_[8191]&&a.used_[0]&&a.used_[1],"wraparound slots changed");
    const auto* scratch=&b.text(values[4]);check(scratch==&b.scratch_,"exhausted probe did not use scratch");
    check(&b.text(values[5])==scratch,"scratch object identity changed");
    a=before::C6aNumberTextCache{};b=candidate::C6aNumberTextCache{};
    const auto keys=fillCache(a);for(double value:keys)b.text(value);equalState(a,b);
    check(usedCount(b)==8192,"capacity not retained");const auto state=b.used_;
    for(double v:boundaries()){check(a.text(v)==b.text(v),"full fallback mismatch");check(b.used_==state,"full fallback published slot");}
    equalState(a,b);return {{"capacity",8192},{"probe",4},{"wraparound",true},{"fullFallback",true},{"scratchIdentity",true}};
}
template<class C>struct Reentry{C*cache=nullptr;double value=0;bool published=false;std::string observed;bool done=false;};
template<class C>static void observeReentry(void*ptr){
    auto& r=*static_cast<Reentry<C>*>(ptr);if(r.done)return;r.done=true;
    const auto slot=C::mix(bitsOf(r.value))&8191;r.published=r.cache->used_[slot]!=0;
    r.observed=r.cache->text(r.value);
}
static json failureSuite(){
    before::C6aNumberTextCache a;candidate::C6aNumberTextCache b;const double v=1.2345678901234567;
    Reentry<before::C6aNumberTextCache>ra{&a,v};Reentry<candidate::C6aNumberTextCache>rb{&b,v};
    allocationHook=&observeReentry<before::C6aNumberTextCache>;hookContext=&ra;a.text(v);allocationHook=nullptr;
    allocationHook=&observeReentry<candidate::C6aNumberTextCache>;hookContext=&rb;b.text(v);allocationHook=nullptr;
    check(ra.done&&ra.published&&ra.observed.empty(),"before premature publication observation missing");
    check(rb.done&&!rb.published&&rb.observed==json(v).dump(),"candidate reentry observation changed text");
    check(a.text(v)==b.text(v),"successful reentry final value differs");
    return {{"allocationFaults","Separate per-site child processes"},
        {"reentryScope","allocation-hook observation and same-value nested lookup; no general mutable reentrancy claim"},
        {"beforeObservedPrematureEmpty",true},{"candidateObservedCorrectRetry",true}};
}
static json frame(double seed=0.0,bool touch=false){
    json f={{"backButtons",0ULL},{"buttons",0ULL},
        {"imu",{{"admitted",false},{"gyroDps",{{"x",seed},{"y",0.},{"z",0.}}},
          {"accelG",{{"x",0.},{"y",0.},{"z",0.}}},{"quaternion",{{"w",1.},{"x",0.},{"y",0.},{"z",0.}}}}},
        {"lt",0.},{"ltRaw",0},{"lx",0.},{"ly",-0.},
        {"raw",{{"bLeftTrigger",0},{"bRightTrigger",0},{"buttons",0ULL},{"sThumbLX",0},{"sThumbLY",0},{"sThumbRX",0},{"sThumbRY",0}}},
        {"rt",0.},{"rtRaw",0},{"rx",0.},{"ry",0.}};
    if(touch){f["touchpads"]={{"left",{{"active",true},{"click",true},{"x",-12345},{"y",23456},{"pressure",32767}}},
        {"right",{{"active",false},{"click",false},{"x",0},{"y",0},{"pressure",0}}}};f["screenButtons"]=0x123456789abcdefULL;}
    return f;
}
static std::string wire(bool revised,const json& f,unsigned long long seq=1){
    static const std::string request="fixture-guid-\"-\n-\\",hash(64,'a');
    return revised?candidate::inputHostBuildSubmitFrameText(f,request,seq,seq+17,seq+1234,hash,"hc-same-provider-selector-v2"):
        before::inputHostBuildSubmitFrameText(f,request,seq,seq+17,seq+1234,hash,"hc-same-provider-selector-v2");
}
static std::string oracle(const json&f,unsigned long long seq=1){
    static const std::string request="fixture-guid-\"-\n-\\",hash(64,'a');
    return wireOracle(f,request,seq,seq+17,seq+1234,hash,"hc-same-provider-selector-v2");
}
static void setFloatFields(json&f,std::uint64_t&seed,bool arbitrary){
    auto number=[&](){const auto bits=random64(seed);return arbitrary?fromBits(bits):double(std::int32_t(bits))/2147483648.0;};
    for(const char*key:{"lt","lx","ly","rt","rx","ry"})f[key]=number();
    for(const char*key:{"x","y","z"}){f["imu"]["gyroDps"][key]=number();f["imu"]["accelG"][key]=number();f["imu"]["quaternion"][key]=number();}
    f["imu"]["quaternion"]["w"]=number();f["imu"]["admitted"]=true;
}
struct Outcome{std::string text,error;int id=0;};
static Outcome wireOutcome(bool revised,const json&f){try{return{wire(revised,f),"",0};}catch(const json::exception&e){return{"",e.what(),e.id};}}
static json wireSuite(){
    before::c6aNumberTextCache()=before::C6aNumberTextCache{};candidate::c6aNumberTextCache()=candidate::C6aNumberTextCache{};
    std::uint64_t seed=0x202610150012ULL;size_t bytes=0;
    for(size_t i=0;i<20000;++i){
        json f=frame(0.0,(i&1)!=0);setFloatFields(f,seed,true);
        f["buttons"]=random64(seed);f["backButtons"]=random64(seed)&3;
        f["raw"]["buttons"]=f["buttons"];f["raw"]["sThumbLX"]=std::int16_t(random64(seed));
        f["ltRaw"]=int(random64(seed)&255);f["rtRaw"]=int(random64(seed)&255);
        if(i%7==0){f["raw"]["escape-fixture"]="\"\\\b\f\n\r\t\x01\x1f UTF8 \xe4\xb8\xad";}
        const auto a=wire(false,f,i+1),b=wire(true,f,i+1),expected=oracle(f,i+1);
        check(a==expected,"before complete wire differs from production parity oracle");
        check(b==expected,"candidate complete wire byte mismatch");bytes+=b.size();
        if(i%4000==0)equalState(before::c6aNumberTextCache(),candidate::c6aNumberTextCache());
    }
    equalState(before::c6aNumberTextCache(),candidate::c6aNumberTextCache());
    for(const char* key:{"backButtons","buttons","imu","lt","ltRaw","lx","ly","raw","rt","rtRaw","rx","ry"}){
        auto f=frame();f.erase(key);const auto a=wireOutcome(false,f),b=wireOutcome(true,f);
        check(a.id==403&&a.id==b.id&&a.error==b.error,"missing field exception identity changed");
    }
    for(auto value:{json(nullptr),json(true),json("string"),json::array(),json::object()}){
        const auto a=wireOutcome(false,value),b=wireOutcome(true,value);
        check(!a.error.empty()&&a.id==b.id&&a.error==b.error,"root type exception changed");
    }
    for(json v:{json::binary({0,1,127,255}),json(json::value_t::discarded),json::array({nullptr,true,-1,18446744073709551615ULL,-0.0}),json("\x80")}){
        std::string a,b;before::c6aAppendJsonValue(a,v);candidate::c6aAppendJsonValue(b,v);check(a==b,"append fallback or existing UTF8 behavior changed");
    }
    return {{"completeWireCases",20000},{"totalComparedBytes",bytes},{"oracle","actual inputHostVerifySubmitFrameParity reference initializer/merge/dump"},
        {"schema","actual emitter key order; source-schema imu/raw/touch/buttons fixtures, no captured product frame claim"},
        {"missingAndWrongRootExceptions",17},{"appendFallbackAndUtf8Cases",4},{"byteParity",true}};
}
static json threadSuite(){
    constexpr size_t n=8;std::barrier<> guard{std::ptrdiff_t(n)};std::array<std::thread,n>threads;std::array<std::uintptr_t,n>a{},b{};std::array<std::exception_ptr,n>errors;
    for(size_t t=0;t<n;++t)threads[t]=std::thread([&,t]{
        auto&left=before::c6aNumberTextCache();auto&right=candidate::c6aNumberTextCache();a[t]=reinterpret_cast<std::uintptr_t>(&left);b[t]=reinterpret_cast<std::uintptr_t>(&right);guard.arrive_and_wait();
        try{std::uint64_t seed=t+0x5511;for(size_t i=0;i<3000;++i){double v=fromBits(random64(seed));const auto expected=json(v).dump();check(left.text(v)==expected&&right.text(v)==expected,"thread independent text mismatch");}
            equalState(left,right);
        }catch(...){errors[t]=std::current_exception();}guard.arrive_and_wait();
    });
    for(auto&thread:threads)thread.join();for(auto e:errors)if(e)std::rethrow_exception(e);
    for(size_t i=0;i<n;++i)for(size_t j=0;j<i;++j)check(a[i]!=a[j]&&b[i]!=b[j],"thread_local cache shared across threads");
    return {{"threads",n},{"valuesPerThread",3000},{"cacheAddressesIndependent",true},{"ordinaryStateTraceEqual",true}};
}
struct Measurement{double ns=0,calls=0,bytes=0;size_t events=0;};
static void resetCaches(){before::c6aNumberTextCache()=before::C6aNumberTextCache{};candidate::c6aNumberTextCache()=candidate::C6aNumberTextCache{};}
static Measurement measureWirePass(bool revised,const std::vector<json>&frames,int mode,bool count){
    resetCaches();
    if(mode==0||mode==1){wire(revised,frames.front());}
    if(mode==4){if(revised)fillCache(candidate::c6aNumberTextCache());else fillCache(before::c6aNumberTextCache());}
    double elapsed=0;Count total;
    if(mode==3){
        for(const auto&f:frames){resetCaches();allocations={};counting=count;auto start=std::chrono::steady_clock::now();
            {const auto out=wire(revised,f);sink+=out.size();}
            const auto stop=std::chrono::steady_clock::now();counting=false;elapsed+=std::chrono::duration<double,std::nano>(stop-start).count();total.calls+=allocations.calls;total.bytes+=allocations.bytes;}
    }else{
        allocations={};counting=count;auto start=std::chrono::steady_clock::now();
        for(const auto&f:frames){const auto out=wire(revised,f);sink+=out.size();}
        const auto stop=std::chrono::steady_clock::now();counting=false;elapsed=std::chrono::duration<double,std::nano>(stop-start).count();total=allocations;
    }
    return {elapsed/frames.size(),double(total.calls)/frames.size(),double(total.bytes)/frames.size(),frames.size()};
}
static Measurement measureWire(bool revised,const std::vector<json>&frames,int mode,bool count){
    // Repeat independently reset trajectories to extend timing without changing
    // cache occupancy/miss mix. Cache/vector initialization remains excluded.
    constexpr int repeats=16;Measurement total;
    for(int i=0;i<repeats;++i){const auto part=measureWirePass(revised,frames,mode,count);total.ns+=part.ns;total.calls+=part.calls;total.bytes+=part.bytes;total.events+=part.events;}
    total.ns/=repeats;total.calls/=repeats;total.bytes/=repeats;return total;
}
static double median(std::vector<double>v){std::sort(v.begin(),v.end());return v[v.size()/2];}

struct Trace{size_t calls=0,hits=0,insertions=0,fallback=0;};
static json traceJson(const Trace&t){return{{"calls",t.calls},{"hits",t.hits},{"insertions",t.insertions},{"fallback",t.fallback},{"formatterCalls",t.insertions+t.fallback}};}
template<class C>static Trace trajectory(const std::vector<json>&frames,int mode){
    C cache;std::array<bool,8192>seen{};Trace trace;
    if(mode==4){fillCache(cache);seen.fill(true);}
    auto walk=[&](auto&&self,const json&v)->void{
        if(v.is_number_float()){
            const auto&text=cache.text(v.get<double>());++trace.calls;
            if(&text==&cache.scratch_)++trace.fallback;
            else{const auto slot=size_t(&text-cache.vals_.data());check(slot<8192,"stored result outside vals");if(seen[slot])++trace.hits;else{seen[slot]=true;++trace.insertions;}}
        }else if(v.is_object()||v.is_array()){for(const auto&child:v)self(self,child);}
    };
    if(mode==0||mode==1)walk(walk,frames.front());
    for(const auto&f:frames){if(mode==3){cache=C{};seen.fill(false);}walk(walk,f);}
    return trace;
}
static json wireBench(int rounds){
    std::vector<json> stationary(1024,frame()),ordinary,dynamic,cold,saturated(512,frame(1.2345678901234567,true));
    std::uint64_t seed=0xfefe;
    for(int i=0;i<512;++i){auto f=frame(0.0,(i%2)!=0);if(i%16==0)setFloatFields(f,seed,false);ordinary.push_back(f);}
    for(int i=0;i<512;++i){auto f=frame(0.0,true);setFloatFields(f,seed,false);f["touchpads"]["left"]["x"]=i-256;f["touchpads"]["right"]["active"]=true;f["touchpads"]["right"]["pressure"]=i;dynamic.push_back(f);}
    for(int i=0;i<128;++i){auto f=frame(1.2345678901234567,true);setFloatFields(f,seed,false);cold.push_back(f);}
    const std::array<const std::vector<json>*,5>sets{&stationary,&ordinary,&dynamic,&cold,&saturated};
    const std::array<const char*,5>names{"stationary-cache-hit","ordinary-mixed-trajectory","dynamic-IMU-touch","cold-cache","saturated-fallback"};
    json rows=json::array();
    for(int mode=0;mode<5;++mode){
        std::vector<double>bt,ct;
        for(int round=0;round<rounds;++round){Measurement a,b;
            if(round&1){b=measureWire(true,*sets[mode],mode,false);a=measureWire(false,*sets[mode],mode,false);}
            else{a=measureWire(false,*sets[mode],mode,false);b=measureWire(true,*sets[mode],mode,false);}
            bt.push_back(a.ns);ct.push_back(b.ns);
        }
        const auto a=measureWire(false,*sets[mode],mode,true),b=measureWire(true,*sets[mode],mode,true);
        if(mode==0)check(a.calls==b.calls&&a.bytes==b.bytes,"hot cache allocation trace changed");
        else check(a.calls>b.calls&&a.bytes>b.bytes,"miss-heavy wire allocation gain absent");
        const auto beforeTrace=traceJson(trajectory<before::C6aNumberTextCache>(*sets[mode],mode));
        const auto candidateTrace=traceJson(trajectory<candidate::C6aNumberTextCache>(*sets[mode],mode));
        check(beforeTrace==candidateTrace,"ordinary local formatter/cache trajectory counts differ");
        rows.push_back({{"traceIncludingWarmup",beforeTrace},{"candidateTraceEqual",true},{"scenario",names[mode]},{"eventsPerRound",sets[mode]->size()*16},{"independentTrajectoryRepeats",16},{"rounds",rounds},
            {"beforeNs",median(bt)},{"candidateNs",median(ct)},{"beforeRoundNs",bt},{"candidateRoundNs",ct},
            {"beforeAllocationCalls",a.calls},{"candidateAllocationCalls",b.calls},{"beforeRequestedBytes",a.bytes},{"candidateRequestedBytes",b.bytes}});
    }
    return rows;
}
static Measurement scalarMeasure(bool revised,const std::vector<double>&values,bool count){
    allocations={};counting=count;const auto start=std::chrono::steady_clock::now();
    for(double v:values){const std::string s=revised?candidate::C6aNumberTextCache::format(v):json(v).dump();sink+=s.size();}
    const auto stop=std::chrono::steady_clock::now();counting=false;
    return{std::chrono::duration<double,std::nano>(stop-start).count()/values.size(),double(allocations.calls)/values.size(),double(allocations.bytes)/values.size(),values.size()};
}
static json scalarBench(int rounds){
    json result=json::array();std::vector<std::pair<std::string,std::vector<double>>>cases;
    cases.push_back({"short-finite",std::vector<double>(4096,0.0)});cases.push_back({"long-finite",std::vector<double>(4096,1.2345678901234567)});
    cases.push_back({"nonfinite",std::vector<double>(4096,std::numeric_limits<double>::infinity())});
    std::vector<double>random;std::uint64_t seed=0x202020;for(int i=0;i<4096;++i)random.push_back(fromBits(random64(seed)));cases.push_back({"random-IEEE64",random});
    for(const auto&[name,values]:cases){std::vector<double>a,b;for(int round=0;round<rounds;++round){
        if(round&1){b.push_back(scalarMeasure(true,values,false).ns);a.push_back(scalarMeasure(false,values,false).ns);}
        else{a.push_back(scalarMeasure(false,values,false).ns);b.push_back(scalarMeasure(true,values,false).ns);}}
        const auto old=scalarMeasure(false,values,true),now=scalarMeasure(true,values,true);
        check(old.calls>now.calls&&old.bytes>now.bytes,"formatter allocation saving absent");
        result.push_back({{"scenario",name},{"beforeNs",median(a)},{"candidateNs",median(b)},
            {"beforeAllocationCalls",old.calls},{"candidateAllocationCalls",now.calls},
            {"beforeRequestedBytes",old.bytes},{"candidateRequestedBytes",now.bytes}});
    }
    return result;
}

template<class C>static json cacheFaultChild(bool old,const std::string&scenario,long long point){
    C c;if(scenario=="fallback"){fillCache(c);c.text(0.0);}
    const double v=1.2345678901234567;const std::string expected=json(v).dump();const auto previous=c.scratch_;
    const auto slot=C::mix(bitsOf(v))&8191;
    allocations={};counting=true;failAt=point;bool caught=false;
    try{const auto&result=c.text(v);sink+=result.size();}catch(const std::bad_alloc&){caught=true;}
    failAt=-1;counting=false;const auto measured=allocations;
    const bool published=c.used_[slot]!=0;const bool unchangedScratch=c.scratch_==previous;
    const auto retry=c.text(v);const bool correct=retry==expected,empty=retry.empty();
    if(point>=0){check(caught,"selected cache fault did not throw");
        if(scenario=="cache"&&old)check(empty,"old cache pollution missing");
        else check(correct,"candidate or fallback cache retry incorrect");
        if(scenario=="fallback")check(unchangedScratch,"scratch changed on fault");
    }else check(correct,"cache control differs from oracle");
    return {{"scenario",scenario},{"faultPoint",point},{"controlAllocationCalls",measured.calls},
        {"caughtBadAlloc",caught},{"usedAtPrimarySlotAfterFailure",published},
        {"retryCorrect",correct},{"retryEmpty",empty},{"scratchUnchangedAfterFailure",unchangedScratch}};
}
static json wireFaultChild(bool revised,long long point){
    json f=frame(1.2345678901234567,true);std::uint64_t seed=0x8450;setFloatFields(f,seed,false);const auto expected=oracle(f);resetCaches();
    allocations={};counting=true;failAt=point;bool caught=false;
    try{const auto result=wire(revised,f);sink+=result.size();}catch(const std::bad_alloc&){caught=true;}
    failAt=-1;counting=false;const auto measured=allocations;
    const size_t published=revised?usedCount(candidate::c6aNumberTextCache()):usedCount(before::c6aNumberTextCache());
    const bool correct=wire(revised,f)==expected;
    if(point>=0){check(caught,"selected wire fault did not throw");if(revised)check(correct,"candidate wire fault retry not exact");}
    else check(correct,"wire control differs from oracle");
    return {{"scenario","wire"},{"faultPoint",point},{"controlAllocationCalls",measured.calls},
        {"caughtBadAlloc",caught},{"publishedSlotsAfterFailure",published},{"retryCorrect",correct}};
}
int main(int argc,char**argv){
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_WARN,0);_CrtSetReportMode(_CRT_ERROR,0);_CrtSetReportMode(_CRT_ASSERT,0);
#endif
    std::set_terminate([](){std::fputs("TERMINATE during injected allocation\n",stderr);std::_Exit(86);});
    try{
        if(argc==5&&std::string(argv[1])=="--fault"){
            const bool revised=std::string(argv[2])=="candidate";const std::string scenario=argv[3];const auto point=std::stoll(argv[4]);
            const auto result=scenario=="wire"?wireFaultChild(revised,point):
                (revised?cacheFaultChild<candidate::C6aNumberTextCache>(false,scenario,point):cacheFaultChild<before::C6aNumberTextCache>(true,scenario,point));
            std::cout<<result.dump()<<'\n';return 0;
        }
        check(sizeof(before::C6aNumberTextCache)==sizeof(candidate::C6aNumberTextCache),"cache object ABI changed");
        check(before::C6aNumberTextCache::kCap==candidate::C6aNumberTextCache::kCap&&before::C6aNumberTextCache::kProbe==candidate::C6aNumberTextCache::kProbe,"cache constants changed");
        const int rounds=argc>1&&std::string(argv[1])=="release"?7:3;
        std::cerr<<"scalar/boundary/state suite\n";const auto scalar=scalarSuite();
        std::cerr<<"collision/full/fault/reentry suites\n";const auto collision=collisionSuite(),failure=failureSuite();
        std::cerr<<"20000 complete-wire suite\n";const auto completeWire=wireSuite();
        const json wireFailure="Separate per-site child processes";
        std::cerr<<"independent thread suite\n";const auto threads=threadSuite();
        const bool verificationOnly=argc>2&&std::string(argv[2])=="--verify-only";
        json wireCost=json::array(),formatterCost=json::array();
        if(!verificationOnly){std::cerr<<"paired isolated costs and separate allocation counts\n";wireCost=wireBench(rounds);formatterCost=scalarBench(rounds);}
        std::cout<<json{{"ok",true},{"msvcFullVersion",_MSC_FULL_VER},{"cacheSizeBytes",sizeof(before::C6aNumberTextCache)},{"assertions",assertions.load()},{"scalar",scalar},{"collision",collision},
            {"failure",failure},{"completeWire",completeWire},{"wireAllocationFaults",wireFailure},{"threads",threads},
            {"wireCosts",wireCost},{"formatterCosts",formatterCost},{"sink",sink},
            {"measurementScope","Local extracted serialization only. JSON frame construction and cache vector initialization excluded; returned text destruction included. Allocation and timing passes separate. Cold-cache uses per-event clocks; other scenarios use a batch clock."},
            {"hardwareOrProductExecuted",false},{"costMeasurementsRun",!verificationOnly},{"counting","Real vendored json.hpp/default allocator; counted normal and aligned global operator new"},
            {"oomScope","Every observed scalar-miss/fallback and ordinary cold-wire allocation site, variant-specific. No claim for all process OOM paths or different-value mutable reentrancy."}}.dump(2)<<'\n';
        return 0;
    }catch(const std::exception&e){counting=false;failAt=-1;allocationHook=nullptr;std::cerr<<"FAIL: "<<e.what()<<'\n';return 3;}
}
