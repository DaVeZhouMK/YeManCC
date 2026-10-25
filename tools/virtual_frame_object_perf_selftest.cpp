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
static std::atomic<long long> outstandingAllocations{0};
static bool trackOutstandingAllocations=false;
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
void* operator new(size_t n){observeAllocation(n);if(void*p=std::malloc(n?n:1)){if(trackOutstandingAllocations)++outstandingAllocations;return p;}throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{if(p&&trackOutstandingAllocations)--outstandingAllocations;std::free(p);}
void operator delete[](void*p)noexcept{if(p&&trackOutstandingAllocations)--outstandingAllocations;std::free(p);}
void operator delete(void*p,size_t)noexcept{if(p&&trackOutstandingAllocations)--outstandingAllocations;std::free(p);}
void operator delete[](void*p,size_t)noexcept{if(p&&trackOutstandingAllocations)--outstandingAllocations;std::free(p);}
void* operator new(size_t n,std::align_val_t a){observeAllocation(n);if(void*p=_aligned_malloc(n?n:1,size_t(a))){if(trackOutstandingAllocations)++outstandingAllocations;return p;}throw std::bad_alloc();}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t)noexcept{if(p&&trackOutstandingAllocations)--outstandingAllocations;_aligned_free(p);}
void operator delete[](void*p,std::align_val_t)noexcept{if(p&&trackOutstandingAllocations)--outstandingAllocations;_aligned_free(p);}
void operator delete(void*p,size_t,std::align_val_t)noexcept{if(p&&trackOutstandingAllocations)--outstandingAllocations;_aligned_free(p);}
void operator delete[](void*p,size_t,std::align_val_t)noexcept{if(p&&trackOutstandingAllocations)--outstandingAllocations;_aligned_free(p);}
#include "context.hpp"
#include "before.hpp"
#include "candidate.hpp"
#include <string_view>
static std::uint64_t assertions=0,sink=0;
static bool faultFrameConstructed=false;
static void check(bool condition,const char*message){++assertions;if(!condition)throw std::runtime_error(message);}
static std::uint64_t random64(std::uint64_t&seed){seed+=0x9e3779b97f4a7c15ULL;auto z=seed;z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);}
static std::string wire(bool proposed,const json&frame,std::uint64_t seq=1){static const std::string request="frame-guid-\"-\n-\\",hash(64,'a');return proposed?candidate::inputHostBuildSubmitFrameText(frame,request,seq,seq+17,seq+1234,hash,"hc-same-provider-selector-v2"):before::inputHostBuildSubmitFrameText(frame,request,seq,seq+17,seq+1234,hash,"hc-same-provider-selector-v2");}
static std::string oracle(const json&frame,std::uint64_t seq){static const std::string request="frame-guid-\"-\n-\\",hash(64,'a');return wireOracle(frame,request,seq,seq+17,seq+1234,hash,"hc-same-provider-selector-v2");}
static void equalCaches(){auto&a=before::c6aNumberTextCache();auto&b=candidate::c6aNumberTextCache();check(a.used_==b.used_,"cache occupancy changed");check(a.keys_==b.keys_,"cache IEEE keys changed");check(a.vals_==b.vals_,"cache stored values changed");check(a.scratch_==b.scratch_,"cache scratch changed");}
static void resetCaches(){before::c6aNumberTextCache()=before::C6aNumberTextCache{};candidate::c6aNumberTextCache()=candidate::C6aNumberTextCache{};}
static FocusFrameContent content(std::uint64_t&seed,bool arbitrary=false){
    FocusFrameContent c;c.buttons=int(random64(seed)&0xffff);c.backButtons=int(random64(seed)&3);c.lxShort=short(random64(seed));c.lyShort=short(random64(seed));c.rxShort=short(random64(seed));c.ryShort=short(random64(seed));
    c.rawLtByte=static_cast<unsigned char>(random64(seed));c.rawRtByte=static_cast<unsigned char>(random64(seed));c.ltRaw=float(c.rawLtByte);c.rtRaw=float(c.rawRtByte);c.lt=c.ltRaw/255.f;c.rt=c.rtRaw/255.f;
    c.imuAdmitted=(random64(seed)&1)!=0;auto scalar=[&](){const auto bits=random64(seed);return arbitrary?std::bit_cast<double>(bits):double(std::int32_t(bits))/2147483648.0;};c.gyroX=scalar();c.gyroY=scalar();c.gyroZ=scalar();c.accelX=scalar();c.accelY=scalar();c.accelZ=scalar();c.qw=scalar();c.qx=scalar();c.qy=scalar();c.qz=scalar();
    c.touchpads.present=(random64(seed)&1)!=0;for(auto*p:{&c.touchpads.left,&c.touchpads.right}){p->down=(random64(seed)&1)!=0;p->click=(random64(seed)&1)!=0;p->x=short(random64(seed));p->y=short(random64(seed));p->pressure=static_cast<unsigned short>(random64(seed));}c.screenButtons=unsigned(random64(seed)&0xf);return c;
}
static void sameTree(const json&a,const json&b){
    check(a.type()==b.type(),"JSON field type changed");
    if(a.is_number_float()){check(std::bit_cast<std::uint64_t>(a.get<double>())==std::bit_cast<std::uint64_t>(b.get<double>()),"JSON IEEE64 bits changed");return;}
    if(a.is_object()){check(a.size()==b.size(),"object field count changed");auto x=a.cbegin(),y=b.cbegin();for(;x!=a.cend();++x,++y){check(x.key()==y.key(),"object key/order changed");sameTree(x.value(),y.value());}return;}
    if(a.is_array()){check(a.size()==b.size(),"array count changed");for(size_t i=0;i<a.size();i++)sameTree(a[i],b[i]);return;}
    check(a==b,"field value changed");
}
static void compare(const FocusFrameContent&c,std::uint64_t seq,bool completeWire=true){
    const auto old=before::focusIsolationAssembleFrame(c),proposed=candidate::focusIsolationAssembleFrame(c);sameTree(old,proposed);
    check(old.dump()==proposed.dump(),"complete frame JSON bytes changed");
    if(completeWire){const auto a=wire(false,old,seq),b=wire(true,proposed,seq);check(a==b,"complete SUBMIT_FRAME bytes changed");check(a==oracle(old,seq),"original parity reference differs");}
}
static json paritySuite(){
    resetCaches();std::uint64_t seed=0x20261015123ULL;
    for(std::uint64_t i=0;i<10000;i++){const auto c=content(seed,true);compare(c,i+1);if(i%1000==0)equalCaches();}equalCaches();
    for(int value=-32768;value<=32767;value++){FocusFrameContent c;c.lxShort=short(value);c.lyShort=short(32767-value-32768);c.rxShort=short(value);c.ryShort=short(value);compare(c,std::uint64_t(value+32769),false);}
    for(unsigned i=0;i<256;i++){FocusFrameContent c;c.rawLtByte=static_cast<unsigned char>(i);c.rawRtByte=static_cast<unsigned char>(255-i);c.ltRaw=float(i);c.rtRaw=float(255-i);c.lt=c.ltRaw/255.f;c.rt=c.rtRaw/255.f;compare(c,i+1);}
    const std::array<double,13>values{0.,-0.,1.,-1.,std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max(),std::numeric_limits<double>::lowest(),1.2345678901234567,std::bit_cast<double>(0x7ff8000000000042ULL),std::bit_cast<double>(0xfff8000000000042ULL)};
    for(bool admitted:{false,true})for(auto v:values){FocusFrameContent c;c.imuAdmitted=admitted;c.gyroX=c.gyroY=c.gyroZ=c.accelX=c.accelY=c.accelZ=c.qw=c.qx=c.qy=c.qz=v;c.lt=c.rt=c.ltRaw=c.rtRaw=static_cast<float>(v);compare(c,1);}
    for(unsigned bits=0;bits<512;bits++){
        FocusFrameContent c;c.touchpads.present=(bits&1)!=0;c.touchpads.left={bool(bits&2),bool(bits&4),-32768,32767,65535};c.touchpads.right={bool(bits&8),bool(bits&16),32767,-32768,65535};c.screenButtons=bits>>5;c.imuAdmitted=(bits&32)!=0;compare(c,bits+1);
    }
    for(std::size_t i=0;i<2000;i++){auto c=content(seed);auto d=c;d.buttons=~c.buttons;d.touchpads.left.x=short(~c.touchpads.left.x);const auto old=candidate::focusIsolationAssembleFrame(c);const auto saved=old.dump();auto next=candidate::focusIsolationAssembleFrame(d);next["buttons"]=111;next["imu"]["gyroDps"]["x"]=222.;check(old.dump()==saved,"later frame mutated prior owned tree");}
    return{{"randomIEEE64ActualSourceFrameCases",10000},{"allShortAxisCases",65536},{"triggerByteCases",256},{"imuBoundaryAndMaskedCases",26},{"touchScreenCombinations",512},{"freshTreeOwnershipCases",2000},{"typesAndFloatBitsCompared",true},{"actualCompleteWireAndParityReferenceCompared",true},{"cacheEffectsEqual",true},{"noProductOrDeviceExecution",true}};
}
struct Timing{double ns=0,allocs=0,bytes=0;};
static Timing measure(bool proposed,const std::vector<FocusFrameContent>&contents,bool allocationsOn,bool includeWire,int repetitions){
    resetCaches();auto warm=proposed?candidate::focusIsolationAssembleFrame(contents[0]):before::focusIsolationAssembleFrame(contents[0]);wire(proposed,warm);allocations={};counting=allocationsOn;const auto start=std::chrono::steady_clock::now();
    for(int repeat=0;repeat<repetitions;repeat++)for(std::size_t i=0;i<contents.size();i++){
        const auto frame=proposed?candidate::focusIsolationAssembleFrame(contents[i]):before::focusIsolationAssembleFrame(contents[i]);
        if(includeWire){const auto text=wire(proposed,frame,i+1);sink+=text.size();}else{sink+=frame.size();}
    }
    const auto stop=std::chrono::steady_clock::now();counting=false;const auto events=double(contents.size())*repetitions;return{std::chrono::duration<double,std::nano>(stop-start).count()/events,double(allocations.calls)/events,double(allocations.bytes)/events};
}
static double median(std::vector<double>x){std::sort(x.begin(),x.end());return x[x.size()/2];}
static json benchmark(){
    std::uint64_t seed=0x88443322;json rows=json::array();
    for(int mode=0;mode<6;mode++){
        const bool includeWire=mode>=3;std::vector<FocusFrameContent>contents;
        for(int i=0;i<512;i++){auto c=mode%3==0?FocusFrameContent{}:content(seed,false);c.touchpads.present=mode%3==2;c.screenButtons=mode%3==2?15:0;c.imuAdmitted=mode%3==2;contents.push_back(c);}
        measure(false,contents,false,includeWire,2);measure(true,contents,false,includeWire,2);
        std::vector<double>a,b;for(int round=0;round<9;round++){Timing x,y;if(round&1){y=measure(true,contents,false,includeWire,6);x=measure(false,contents,false,includeWire,6);}else{x=measure(false,contents,false,includeWire,6);y=measure(true,contents,false,includeWire,6);}a.push_back(x.ns);b.push_back(y.ns);}
        const auto old=measure(false,contents,true,includeWire,2),proposed=measure(true,contents,true,includeWire,2);check(old.allocs>proposed.allocs&&old.bytes>proposed.bytes,"object allocation reduction absent");
        static const char*names[]{"neutral-object","dynamic-no-touch-object","dynamic-IMU-touch-object","neutral-object-plus-wire","dynamic-no-touch-plus-wire","dynamic-IMU-touch-plus-wire"};
        rows.push_back({{"scenario",names[mode]},{"rounds",9},{"eventsPerRound",512*6},{"beforeNs",median(a)},{"candidateNs",median(b)},{"beforeRounds",a},{"candidateRounds",b},{"beforeAllocations",old.allocs},{"candidateAllocations",proposed.allocs},{"beforeRequestedBytes",old.bytes},{"candidateRequestedBytes",proposed.bytes},{"includeWire",includeWire}});
    }return rows;
}
static int faults(int argc,char**argv){
    trackOutstandingAllocations=true;
    if(argc!=3&&argc!=4)return 2;const bool proposed=std::string(argv[2])=="candidate";std::uint64_t seed=0x5566;auto c=content(seed,false);c.touchpads.present=true;c.screenButtons=15;
    allocations={};counting=true;{const auto measured=proposed?candidate::focusIsolationAssembleFrame(c):before::focusIsolationAssembleFrame(c);sink+=measured.size();}counting=false;const auto sites=allocations.calls;
    if(argc==3){std::cout<<json{{"pass",true},{"variant",proposed?"candidate":"before"},{"allocationSites",sites}}.dump()<<std::endl;return 0;}
    const auto expected=before::focusIsolationAssembleFrame(c).dump();std::size_t failures=0;
    const auto point=std::stoull(argv[3]);if(point>=sites)return 2;
    std::set_terminate([](){std::fputs(faultFrameConstructed?"TERMINATE during injected construction allocation; frameConstructed=true\n":"TERMINATE during injected construction allocation; frameConstructed=false\n",stderr);std::_Exit(86);});
    for(std::uint64_t site=point;site<point+1;site++){
        const auto live=outstandingAllocations.load();bool failed=false;faultFrameConstructed=false;failAt=static_cast<long long>(site);
        try{const auto value=proposed?candidate::focusIsolationAssembleFrame(c):before::focusIsolationAssembleFrame(c);faultFrameConstructed=true;sink+=value.size();}catch(const std::bad_alloc&){failed=true;failAt=-1;}failAt=-1;
        check(failed,"construction allocation site did not fail");check(outstandingAllocations.load()==live,"failed construction leaked allocated objects");
        const auto recovered=proposed?candidate::focusIsolationAssembleFrame(c):before::focusIsolationAssembleFrame(c);check(recovered.dump()==expected,"retry after fault changed full frame");failures++;
    }
    std::cout<<json{{"pass",true},{"variant",proposed?"candidate":"before"},{"allocationSites",sites},{"badAllocSitesVerified",failures},{"liveAllocationBalanceRetained",true},{"retryFrameBytesEqual",true},{"assertions",assertions}}.dump(2)<<std::endl;return 0;
}
int main(int argc,char**argv){try{if(argc>1&&std::string(argv[1])=="fault-sweep")return faults(argc,argv);const auto parity=paritySuite();const bool bench=argc>1&&std::string(argv[1])=="benchmark";const auto costs=bench?benchmark():json::array();std::cout<<json{{"pass",true},{"assertions",assertions},{"parity",parity},{"benchmark",costs},{"sink",sink},{"noOSSDKDeviceEntryExecution",true}}.dump(2)<<std::endl;return 0;}catch(const std::exception&e){counting=false;failAt=-1;std::cerr<<e.what()<<std::endl;return 1;}}
