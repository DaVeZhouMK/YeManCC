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
#include <map>
#include <set>
#include <string_view>
static std::uint64_t assertions=0,sink=0;
static void check(bool condition,const char*message){++assertions;if(!condition)throw std::runtime_error(message);}
static std::uint64_t random64(std::uint64_t&seed){seed+=0x9e3779b97f4a7c15ULL;auto z=seed;z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);}
static std::string wire(bool proposed,const json&frame,std::uint64_t seq=1){
    static const std::string request="frame-guid-\"-\n-\\",hash(64,'a');
    return proposed?candidate::inputHostBuildSubmitFrameText(frame,request,seq,seq+17,seq+1234,hash,"hc-same-provider-selector-v2"):
        before::inputHostBuildSubmitFrameText(frame,request,seq,seq+17,seq+1234,hash,"hc-same-provider-selector-v2");
}
static std::string oracle(const json&frame,std::uint64_t seq){
    static const std::string request="frame-guid-\"-\n-\\",hash(64,'a');
    return wireOracle(frame,request,seq,seq+17,seq+1234,hash,"hc-same-provider-selector-v2");
}
static void equalCaches(){auto&a=before::c6aNumberTextCache();auto&b=candidate::c6aNumberTextCache();check(a.used_==b.used_,"cache occupancy changed");check(a.keys_==b.keys_,"cache IEEE keys changed");check(a.vals_==b.vals_,"cache stored values changed");check(a.scratch_==b.scratch_,"cache scratch changed");}
static void resetCaches(){before::c6aNumberTextCache()=before::C6aNumberTextCache{};candidate::c6aNumberTextCache()=candidate::C6aNumberTextCache{};}
static FocusFrameContent content(std::uint64_t&seed,bool arbitrary=false){
    FocusFrameContent c;
    c.buttons=int(random64(seed)&0xffff);c.backButtons=int(random64(seed)&3);
    c.lxShort=short(random64(seed));c.lyShort=short(random64(seed));c.rxShort=short(random64(seed));c.ryShort=short(random64(seed));
    c.rawLtByte=static_cast<unsigned char>(random64(seed));c.rawRtByte=static_cast<unsigned char>(random64(seed));
    c.ltRaw=float(c.rawLtByte);c.rtRaw=float(c.rawRtByte);c.lt=c.ltRaw/255.f;c.rt=c.rtRaw/255.f;
    c.imuAdmitted=(random64(seed)&1)!=0;
    auto scalar=[&](){const auto bits=random64(seed);return arbitrary?std::bit_cast<double>(bits):double(std::int32_t(bits))/2147483648.0;};
    c.gyroX=scalar();c.gyroY=scalar();c.gyroZ=scalar();c.accelX=scalar();c.accelY=scalar();c.accelZ=scalar();c.qw=scalar();c.qx=scalar();c.qy=scalar();c.qz=scalar();
    c.touchpads.present=(random64(seed)&1)!=0;
    for(auto*p:{&c.touchpads.left,&c.touchpads.right}){p->down=(random64(seed)&1)!=0;p->click=(random64(seed)&1)!=0;p->x=short(random64(seed));p->y=short(random64(seed));p->pressure=static_cast<unsigned short>(random64(seed));}
    c.screenButtons=unsigned(random64(seed)&0xf);return c;
}
struct Outcome{std::string text,error;int id=0;};
static Outcome outcome(bool proposed,const json&frame){try{return{wire(proposed,frame),"",0};}catch(const json::exception&e){return{"",e.what(),e.id};}}
static void sameOutcome(const json&frame){const auto a=outcome(false,frame),b=outcome(true,frame);check(a.text==b.text,"full frame output mismatch");check(a.id==b.id,"exception ID changed");check(a.error==b.error,"exception text changed");}
static json paritySuite(){
    resetCaches();std::uint64_t seed=0x2026101508ULL;std::size_t bytes=0;
    for(std::uint64_t i=0;i<20000;i++){
        const auto c=content(seed,true);const auto frame=focusIsolationAssembleFrame(c);
        const auto a=wire(false,frame,i+1),b=wire(true,frame,i+1),expected=oracle(frame,i+1);
        check(a==b,"actual assembler complete-wire mismatch");check(a==expected,"before parity reference mismatch");check(b==expected,"candidate parity reference mismatch");bytes+=b.size();
        check(frame.is_object(),"assembler ordered object");
        if(i%2048==0)equalCaches();
    }
    equalCaches();
    const std::array<const char*,12>mandatory{"backButtons","buttons","imu","lt","ltRaw","lx","ly","raw","rt","rtRaw","rx","ry"};
    for(auto name:mandatory){auto frame=focusIsolationAssembleFrame(FocusFrameContent{});frame.erase(name);sameOutcome(frame);}
    for(auto v:{json(nullptr),json(true),json(123),json(-0.),json("string"),json::array(),json::object(),json::binary({1,2}),json(json::value_t::discarded)})sameOutcome(v);
    const auto base=focusIsolationAssembleFrame(FocusFrameContent{});
    const std::array<const char*,28>unknown{"","a","back","backButtons!","buttons!","command","imu!","ls","lt!","ltRaw!","lx!","lz","q","raw!","requestId","rt!","rtRaw!","rx!","rz","sampleSequence","screenButtons!","sequence","startupNonce","targetId","touchpad","touchpads!","z","zz"};
    for(std::size_t n=0;n<128;n++){
        auto f=base;
        for(std::size_t i=0;i<unknown.size();i++)if((i+n)%3==0)f[unknown[i]]=json::array({int(i),"ignored\n\"\\"});
        if(n&1)f["screenButtons"]=n;if(n&2)f["touchpads"]=json::array({1,2,3});
        sameOutcome(f);for(auto required:mandatory){auto missing=f;missing.erase(required);sameOutcome(missing);}
    }
    const std::vector<json>fieldValues{nullptr,true,false,-1,18446744073709551615ULL,-0.0,std::numeric_limits<double>::infinity(),"\"\\\b\f\n\r\t\x01\x1f UTF8 \xe4\xb8\xad",json::array({1,2,3}),json::binary({0,255}),json(json::value_t::discarded),json{{"a",1},{"z",json::array({true,nullptr})}}};
    for(auto required:mandatory)for(const auto&v:fieldValues){auto f=base;f[required]=v;sameOutcome(f);}
    for(const auto&v:fieldValues){auto f=base;f["screenButtons"]=v;f["touchpads"]=v;sameOutcome(f);}
    for(int i=0;i<4096;i++){
        auto f=base;const std::size_t erased=random64(seed)%mandatory.size();
        for(std::size_t k=0;k<mandatory.size();k++)if((random64(seed)&3)==0||k==erased)f.erase(mandatory[k]);
        for(auto key:unknown)if((random64(seed)&3)==0)f[key]=random64(seed);
        sameOutcome(f);
    }
    equalCaches();
    return{{"completeActualAssemblerWireCases",20000},{"totalComparedBytes",bytes},{"requiredMissingCases",12},{"wrongRootCases",9},{"extraFieldSets",128},{"extraAndMissingCases",1536},{"multipleMissingCases",4096},{"arbitraryValueShapes",fieldValues.size()},{"orderedCacheEffectsIdentical",true},{"optionalFieldsByteParity",true},{"noProductOrDeviceExecution",true}};
}
struct Timing{double ns=0,allocs=0,bytes=0;};
static Timing measure(bool proposed,const std::vector<json>&frames,bool countingAllocations,bool includeAssembly,const std::vector<FocusFrameContent>&contents,int repetitions){
    resetCaches();wire(proposed,frames[0]);
    allocations={};counting=countingAllocations;
    const auto start=std::chrono::steady_clock::now();
    for(int repeat=0;repeat<repetitions;repeat++)for(std::size_t i=0;i<frames.size();i++){
        if(includeAssembly){auto f=focusIsolationAssembleFrame(contents[i]);const auto s=wire(proposed,f,std::uint64_t(i+1));sink+=s.size();}
        else{const auto s=wire(proposed,frames[i],std::uint64_t(i+1));sink+=s.size();}
    }
    const auto stop=std::chrono::steady_clock::now();counting=false;const auto events=double(frames.size())*repetitions;
    return{std::chrono::duration<double,std::nano>(stop-start).count()/events,double(allocations.calls)/events,double(allocations.bytes)/events};
}
static double median(std::vector<double>x){std::sort(x.begin(),x.end());return x[x.size()/2];}
static json benchmark(){
    json results=json::array();std::uint64_t seed=0x88008800;
    for(int mode=0;mode<5;mode++){
        std::vector<FocusFrameContent>contents;std::vector<json>frames;
        for(int i=0;i<512;i++){
            auto c=mode==0?FocusFrameContent{}:content(seed,false);
            if(mode==1){c.touchpads.present=false;c.screenButtons=0;c.imuAdmitted=false;}
            if(mode==2){c.touchpads.present=true;c.screenButtons=15;c.imuAdmitted=true;}
            contents.push_back(c);auto f=focusIsolationAssembleFrame(c);
            if(mode==3)for(int j=0;j<24;j++)f["unknown-"+std::to_string(j)]=j;
            frames.push_back(f);
        }
        const bool assembly=mode==4;std::vector<double>a,b;
        measure(false,frames,false,assembly,contents,4);measure(true,frames,false,assembly,contents,4);
        for(int round=0;round<9;round++){
            Timing x,y;if(round&1){y=measure(true,frames,false,assembly,contents,12);x=measure(false,frames,false,assembly,contents,12);}else{x=measure(false,frames,false,assembly,contents,12);y=measure(true,frames,false,assembly,contents,12);}
            a.push_back(x.ns);b.push_back(y.ns);
        }
        const auto old=measure(false,frames,true,assembly,contents,2),proposed=measure(true,frames,true,assembly,contents,2);
        check(old.allocs==proposed.allocs&&old.bytes==proposed.bytes,"successful-call allocation trace changed");
        static const char*names[]{"stationary-no-touch","dynamic-no-touch-no-IMU","dynamic-IMU-touch-screen","extra-unknown-top-fields","actual-assembly-plus-wire"};
        results.push_back({{"scenario",names[mode]},{"rounds",9},{"eventsPerRound",512*12},{"beforeNs",median(a)},{"candidateNs",median(b)},{"beforeRounds",a},{"candidateRounds",b},{"beforeAllocations",old.allocs},{"candidateAllocations",proposed.allocs},{"beforeRequestedBytes",old.bytes},{"candidateRequestedBytes",proposed.bytes},{"includeActualAssembly",assembly}});
    }
    return results;
}
static int fault(int argc,char**argv){
    if(argc!=5)return 2;const bool proposed=std::string(argv[2])=="candidate";const std::string scenario=argv[3];const auto point=std::stoll(argv[4]);
    resetCaches();std::uint64_t seed=0x112233;auto f=focusIsolationAssembleFrame(content(seed,false));wire(false,f);wire(true,f);
    f=focusIsolationAssembleFrame(content(seed,false));if(scenario=="missing")f.erase("rtRaw");
    std::string text,error;bool badAllocation=false;int jsonId=0;failAt=point;std::set_terminate([](){std::fputs("TERMINATE during injected allocation\n",stderr);std::_Exit(86);});
    try{text=wire(proposed,f);}catch(const std::bad_alloc&){failAt=-1;badAllocation=true;}catch(const json::exception&e){failAt=-1;error=e.what();jsonId=e.id;}failAt=-1;
    std::cout<<json{{"variant",proposed?"candidate":"before"},{"scenario",scenario},{"point",point},{"text",text},{"error",error},{"jsonId",jsonId},{"badAllocation",badAllocation}}.dump();return 0;
}
int main(int argc,char**argv){
    if(argc>1&&std::string(argv[1])=="fault")return fault(argc,argv);
    try{const auto parity=paritySuite();const bool bench=argc>1&&std::string(argv[1])=="benchmark";const auto costs=bench?benchmark():json::array();
        std::cout<<json{{"pass",true},{"assertions",assertions},{"parity",parity},{"benchmark",costs},{"sink",sink},{"noOSSDKDeviceEntryExecution",true}}.dump(2)<<std::endl;return 0;
    }catch(const std::exception&e){counting=false;failAt=-1;std::cerr<<e.what()<<std::endl;return 1;}
}
