// R34 offline fixed ASCII key fixture. Actual serializer, helpers, frame and
// touch snapshot assembly are bound to immutable current source snapshots.
#include <algorithm>
#include <array>
#include <bit>
#include <type_traits>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <map>
#include <mutex>
#include <new>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

struct Allocations {
    unsigned long long ordinaryRequests=0, ordinaryBytes=0, alignedRequests=0, alignedBytes=0;
    void add(const Allocations& x) {
        ordinaryRequests+=x.ordinaryRequests; ordinaryBytes+=x.ordinaryBytes;
        alignedRequests+=x.alignedRequests; alignedBytes+=x.alignedBytes;
    }
};
static bool counting=false;
static Allocations allocated;
void* operator new(std::size_t n) {
    if(counting) { ++allocated.ordinaryRequests; allocated.ordinaryBytes+=n; }
    if(void* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n,std::align_val_t a) {
    if(counting) { ++allocated.alignedRequests; allocated.alignedBytes+=n; }
    if(void* p=_aligned_malloc(n?n:1,static_cast<std::size_t>(a))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n,std::align_val_t a) { return ::operator new(n,a); }
void operator delete(void* p,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p,std::size_t,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept { _aligned_free(p); }
void* operator new(std::size_t n,const std::nothrow_t&) noexcept { try{return ::operator new(n);}catch(...){return nullptr;} }
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept { return ::operator new(n,std::nothrow); }
void operator delete(void* p,const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p,const std::nothrow_t&) noexcept { ::operator delete[](p); }
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { try{return ::operator new(n,a);}catch(...){return nullptr;} }
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { return ::operator new(n,a,std::nothrow); }
void operator delete(void* p,std::align_val_t a,const std::nothrow_t&) noexcept { ::operator delete(p,a); }
void operator delete[](void* p,std::align_val_t a,const std::nothrow_t&) noexcept { ::operator delete[](p,a); }

#include "json.hpp"
using json=nlohmann::json;
#include "source-bound.h"
static unsigned long long assertions=0,failures=0,cases=0,differences=0,oracleDifferences=0,typedChecks=0,exceptionCases=0;
static json failureDetails=json::array();static volatile unsigned long long sink=0;
static std::uint64_t digest=1469598103934665603ULL;
static void check(bool pass,const std::string& why) {++assertions;if(!pass) {++failures;if(failureDetails.size()<80)failureDetails.push_back(why);}}
static void consume(const std::string& s) {for(unsigned char c:s) {digest^=c;digest*=1099511628211ULL;}digest^=0xff;digest*=1099511628211ULL;}
static std::string snapshot(const json& value) {
    std::string s=std::to_string(static_cast<int>(value.type()))+":";
    if(value.is_number_float())s+=std::to_string(std::bit_cast<std::uint64_t>(value.get<double>()));
    else if(value.is_object())for(auto it=value.begin();it!=value.end();++it)s+=std::to_string(it.key().size())+":"+it.key()+snapshot(it.value());
    else if(value.is_array())for(const auto& x:value)s+=snapshot(x);
    else if(value.is_string())s+=std::to_string(value.get_ref<const std::string&>().size())+":"+value.get_ref<const std::string&>();
    else s+=value.dump();
    return s+";";
}
struct Identity {
    std::string run="01234567-89AB-4CDE-8123-0123456789AB",nonce="89ABCDEF-0123-4567-89AB-CDEF01234567";
    std::string request="ABCDEF01-2345-4678-9ABC-DEF012345678",hash=std::string(64,'a'),motion="pair-unproven-safe-zero";
    std::string persona="steamdeck",profile="steam-deck-composite";
    unsigned long long sequence=41,sample=97,tick=123456789,epoch=23,config=17,power=29;
};
static void bind(const Identity& x) {g_inputHostRunId=x.run;g_inputHostStartupNonce=x.nonce;fixturePersona=x.persona;fixtureProfile=x.profile;g_inputHostEpoch=x.epoch;g_inputHostConfigRevision=x.config;fixturePower=x.power;}
static json identityState() {return {{"run",g_inputHostRunId},{"nonce",g_inputHostStartupNonce},{"persona",fixturePersona},{"profile",fixtureProfile},{"epoch",g_inputHostEpoch},{"config",g_inputHostConfigRevision},{"power",fixturePower}};}
using Builder=std::string(*)(const json&,const std::string&,unsigned long long,unsigned long long,unsigned long long,const std::string&,const char*);
static std::string wire(bool after,const json& frame,const Identity& x) {
    Builder fn=after?candidate::inputHostBuildSubmitFrameText:baseline::inputHostBuildSubmitFrameText;
    return fn(frame,x.request,x.sequence,x.sample,x.tick,x.hash,x.motion.c_str());
}
struct Outcome {std::string bytes,error;int id=0;};
static Outcome outcome(bool after,const json& frame,const Identity& x) {
    try {return {wire(after,frame,x),"",0};}
    catch(const json::exception& e) {return {"",e.what(),e.id};}
    catch(const std::exception& e) {return {"",e.what(),-1};}
}
static void equalCaches() {
    const auto& b=baseline::c6aNumberTextCache();const auto& a=candidate::c6aNumberTextCache();
    check(b.used_==a.used_&&b.keys_==a.keys_&&b.vals_==a.vals_&&b.scratch_==a.scratch_,"all actual cache slots, keys, texts and scratch side effects identical");
}
static void compare(const json& frame,const Identity& x,bool requireOracle=true) {
    ++cases;bind(x);const auto original=snapshot(frame);const auto globals=identityState();
    const auto b=outcome(false,frame,x),a=outcome(true,frame,x);
    const bool same=a.bytes==b.bytes&&a.error==b.error&&a.id==b.id;differences+=!same;check(same,"complete wire/exception ID/message parity");
    check(original==snapshot(frame),"input JSON types, NaN/negative-zero bits, strings and ownership preserved");
    check(identityState()==globals,"serializer does not mutate identity source globals");
    if(a.id) {++exceptionCases;check(!a.error.empty(),"exception message retained");}
    else if(requireOracle) {
        const auto expected=wireOracle(frame,x.request,x.sequence,x.sample,x.tick,x.hash,x.motion.c_str());
        const bool equal=a.bytes==expected&&b.bytes==expected;oracleDifferences+=!equal;check(equal,"full UTF8 byte parity against actual production parity oracle");
        const auto parsed=json::parse(a.bytes,nullptr,false),reference=json::parse(expected,nullptr,false);
        check(parsed.is_discarded()==reference.is_discarded(),"malformed arbitrary-value behavior retained");
        if(!reference.is_discarded()) {++typedChecks;check(snapshot(parsed)==snapshot(reference),"all wire JSON fields and numeric/string/container types match oracle");}
    }
    consume(a.bytes);consume(b.bytes);consume(a.error);consume(std::to_string(a.id));
    if(cases%128==0)equalCaches();
}
static std::uint64_t rng(std::uint64_t& x) {x^=x<<13;x^=x>>7;x^=x<<17;return x;}
static FocusFrameContent frameContent(int scenario) {
    FocusFrameContent c;
    if(scenario) {
        c.buttons=0xA55A;c.backButtons=3;c.lxShort=-32768;c.lyShort=32767;c.rxShort=-12345;c.ryShort=23456;
        c.lt=.25f;c.rt=.75f;c.ltRaw=64.f;c.rtRaw=192.f;c.rawLtByte=64;c.rawRtByte=192;
        c.imuAdmitted=true;c.gyroX=1.25;c.gyroY=-2.5;c.gyroZ=3.75;c.accelX=.125;c.accelY=-.25;c.accelZ=1.;
        c.qw=.5;c.qx=-.5;c.qy=.25;c.qz=-.25;
    }
    if(scenario==2) {c.touchpads.present=true;c.touchpads.left={true,true,100,200,300};c.touchpads.right={true,false,1900,1000,65535};c.screenButtons=0xf;}
    return c;
}
static json contentState(const FocusFrameContent& c) {
    return {{"buttons",c.buttons},{"shorts",{c.lxShort,c.lyShort,c.rxShort,c.ryShort}},
        {"floats",{std::bit_cast<unsigned>(c.lt),std::bit_cast<unsigned>(c.rt),std::bit_cast<unsigned>(c.ltRaw),std::bit_cast<unsigned>(c.rtRaw)}},
        {"raw",{c.rawLtByte,c.rawRtByte}},{"imuAdmitted",c.imuAdmitted},
        {"doubles",{std::bit_cast<std::uint64_t>(c.gyroX),std::bit_cast<std::uint64_t>(c.gyroY),std::bit_cast<std::uint64_t>(c.gyroZ),std::bit_cast<std::uint64_t>(c.accelX),std::bit_cast<std::uint64_t>(c.accelY),std::bit_cast<std::uint64_t>(c.accelZ),std::bit_cast<std::uint64_t>(c.qw),std::bit_cast<std::uint64_t>(c.qx),std::bit_cast<std::uint64_t>(c.qy),std::bit_cast<std::uint64_t>(c.qz)}},
        {"touchPresent",c.touchpads.present},{"touch",ymcc::screenpads::snapshotJson(c.touchpads)},{"screenButtons",c.screenButtons}};
}
static void assembled(const FocusFrameContent& c,const Identity& x={}) {
    const auto before=contentState(c);const auto frame=focusIsolationAssembleFrame(c);compare(frame,x);check(contentState(c)==before,"actual assembler preserves every content/touch field");
}
static void correctness() {
    static_assert(std::is_same_v<decltype(baseline::inputHostBuildSubmitFrameText(json{},std::string{},0,0,0,std::string{},"")),std::string>);
    check(r34FixedKeyCount>20,"mechanically enumerated fixed source keys");
    Identity id;
    for(int scenario=0;scenario<3;++scenario)assembled(frameContent(scenario));
    const std::vector<short> shorts={-32768,-32767,-1,0,1,32766,32767};
    for(short n:shorts)for(unsigned char trigger:std::array<unsigned char,4>{0,1,254,255}) {
        auto c=frameContent(2);c.lxShort=c.lyShort=c.rxShort=c.ryShort=n;c.rawLtByte=c.rawRtByte=trigger;c.lt=c.rt=trigger/255.f;c.ltRaw=c.rtRaw=trigger;assembled(c);
    }
    for(double n:std::vector<double>{0.,-0.,1.,-1.,std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::min(),std::numeric_limits<double>::max(),std::numeric_limits<double>::lowest(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto c=frameContent(2);c.gyroX=c.gyroY=c.gyroZ=c.accelX=c.accelY=c.accelZ=c.qw=c.qx=c.qy=c.qz=n;
        c.lt=c.rt=c.ltRaw=c.rtRaw=static_cast<float>(n);assembled(c);
    }
    for(auto n:{0ULL,1ULL,0x7fffffffULL,0xffffffffULL,0x7fffffffffffffffULL,0xffffffffffffffffULL}) {
        auto x=id;x.sequence=x.sample=x.tick=x.epoch=x.config=x.power=n;assembled(frameContent(2),x);
    }
    for(const auto& text:std::vector<std::string>{"","ascii","\"\\\b\f\n\r\t\x01\x1f","中文-😀",std::string("a\0b",3),std::string(4096,'L')}) {
        auto x=id;x.run=x.nonce=x.request=x.hash=x.motion=x.persona=x.profile=text;assembled(frameContent(2),x);
    }
    for(bool present:{false,true})for(bool down:{false,true})for(bool click:{false,true})for(unsigned screen:{0U,1U,15U,0xffffffffU}) {
        auto c=frameContent(2);c.touchpads.present=present;c.touchpads.left={down,click,-32768,32767,65535};c.touchpads.right={click,down,32767,-32768,0};c.screenButtons=screen;assembled(c);
    }
    std::uint64_t seed=0x2026101534ULL;
    for(int i=0;i<4096;++i) {
        auto c=frameContent(i%3);c.buttons=static_cast<int>(rng(seed));c.backButtons=static_cast<int>(rng(seed));
        c.lxShort=static_cast<short>(rng(seed));c.lyShort=static_cast<short>(rng(seed));c.rxShort=static_cast<short>(rng(seed));c.ryShort=static_cast<short>(rng(seed));
        c.gyroX=std::bit_cast<double>(rng(seed));c.gyroY=std::bit_cast<double>(rng(seed));c.gyroZ=std::bit_cast<double>(rng(seed));
        c.accelX=std::bit_cast<double>(rng(seed));c.accelY=std::bit_cast<double>(rng(seed));c.accelZ=std::bit_cast<double>(rng(seed));
        c.qw=std::bit_cast<double>(rng(seed));c.qx=std::bit_cast<double>(rng(seed));c.qy=std::bit_cast<double>(rng(seed));c.qz=std::bit_cast<double>(rng(seed));
        c.lt=std::bit_cast<float>(static_cast<unsigned>(rng(seed)));c.rt=std::bit_cast<float>(static_cast<unsigned>(rng(seed)));
        c.rawLtByte=static_cast<unsigned char>(rng(seed));c.rawRtByte=static_cast<unsigned char>(rng(seed));assembled(c);
    }
    bind(id);const auto base=focusIsolationAssembleFrame(frameContent(2));
    const std::array<const char*,12> required={"backButtons","buttons","imu","lt","ltRaw","lx","ly","raw","rt","rtRaw","rx","ry"};
    for(const char* name:required) {auto f=base;f.erase(name);compare(f,id,false);}
    for(const auto& root:std::vector<json>{nullptr,true,123,-0.,"root",json::array(),json::binary({1,2}),json(json::value_t::discarded)})compare(root,id,false);
    std::vector<json> values={nullptr,true,false,-1,0xffffffffffffffffULL,-0.,std::numeric_limits<double>::infinity(),"\"\\中文😀",std::string("n\0ul",4),json::array({1,2,3}),json{{"nested\"\\",std::string("v\0x",3)},{"中文键",-0.}},json::binary({0,255}),json(json::value_t::discarded)};
    for(const auto& value:values) {
        for(const char* name:required) {auto f=base;f[name]=value;compare(f,id);}
        for(const char* name:{"touchpads","screenButtons"}) {auto f=base;f[name]=value;compare(f,id);}
    }
    for(const auto& key:std::vector<std::string>{"","\"\\\n","中文键😀",std::string("k\0z",3)}) {auto f=base;f["imu"]=json::object();f["imu"][key]=json{{"nested",values[8]}};compare(f,id);}
    equalCaches();
    // Output owns its UTF8 bytes after the source frame and identity values die.
    std::string b,a,expected;
    {auto f=base;bind(id);b=wire(false,f,id);a=wire(true,f,id);expected=wireOracle(f,id.request,id.sequence,id.sample,id.tick,id.hash,id.motion.c_str());f.clear();g_inputHostRunId.assign(4000,'X');g_inputHostStartupNonce.clear();}
    check(a==expected&&b==expected,"owned complete wire survives input mutation/destruction");bind(id);
}
struct Measurement {double ns;Allocations allocations;unsigned long long checksum,operations;};
static Measurement measure(int scenario,int scope,bool after,int batches,bool count) {
    Identity id;bind(id);const auto content=frameContent(scenario);const auto prepared=focusIsolationAssembleFrame(content);
    Builder fn=after?candidate::inputHostBuildSubmitFrameText:baseline::inputHostBuildSubmitFrameText;
    unsigned long long checksum=0;allocated={};counting=count;const auto begin=std::chrono::steady_clock::now();
    for(int batch=0;batch<batches;++batch)for(int i=0;i<32;++i) {
        if(scope==0) {const auto out=fn(prepared,id.request,id.sequence,id.sample,id.tick,id.hash,id.motion.c_str());checksum+=out.size()+static_cast<unsigned char>(out.front())+static_cast<unsigned char>(out.back());}
        else {const auto frame=focusIsolationAssembleFrame(content);const auto out=fn(frame,id.request,id.sequence,id.sample,id.tick,id.hash,id.motion.c_str());checksum+=out.size()+static_cast<unsigned char>(out.front())+static_cast<unsigned char>(out.back());}
    }
    const auto end=std::chrono::steady_clock::now();counting=false;const auto counts=allocated;sink=checksum;
    const auto n=static_cast<unsigned long long>(32*batches);return {std::chrono::duration<double,std::nano>(end-begin).count()/n,counts,checksum,n};
}
static double median(std::vector<double> x) {std::sort(x.begin(),x.end());return x[x.size()/2];}
static json allocationJson(const Measurement& m) {const auto& c=m.allocations;return {{"ordinaryRequests",c.ordinaryRequests},{"ordinaryRequestedBytes",c.ordinaryBytes},{"alignedRequests",c.alignedRequests},{"alignedRequestedBytes",c.alignedBytes},{"operations",m.operations},{"requestsPerOperation",(c.ordinaryRequests+c.alignedRequests)/static_cast<double>(m.operations)},{"requestedBytesPerOperation",(c.ordinaryBytes+c.alignedBytes)/static_cast<double>(m.operations)}};}
static json formal(int scenario,int scope,int campaign) {
    measure(scenario,scope,false,6,false);measure(scenario,scope,true,6,false);
    std::vector<double>b,a,p;json pairs=json::array();int wins=0;bool same=true;
    for(int round=0;round<11;++round) {
        Measurement x,y;const bool baselineFirst=((round+campaign)&1)==0;
        if(baselineFirst) {x=measure(scenario,scope,false,128,false);y=measure(scenario,scope,true,128,false);}else {y=measure(scenario,scope,true,128,false);x=measure(scenario,scope,false,128,false);}
        b.push_back(x.ns);a.push_back(y.ns);p.push_back((x.ns-y.ns)/x.ns);wins+=y.ns<x.ns;same=same&&x.checksum==y.checksum;
        pairs.push_back({{"round",round+1},{"baselineFirst",baselineFirst},{"beforeNsPerOperation",x.ns},{"afterNsPerOperation",y.ns},{"pairedReduction",p.back()},{"beforeChecksum",x.checksum},{"afterChecksum",y.checksum}});
    }
    const auto x=measure(scenario,scope,false,4,true),y=measure(scenario,scope,true,4,true);
    const bool alloc=y.allocations.ordinaryRequests<=x.allocations.ordinaryRequests&&y.allocations.ordinaryBytes<=x.allocations.ordinaryBytes&&y.allocations.alignedRequests<=x.allocations.alignedRequests&&y.allocations.alignedBytes<=x.allocations.alignedBytes;
    same=same&&x.checksum==y.checksum;const double paired=median(p),ratio=1.-median(a)/median(b);
    const char* names[]={"synthetic-neutral","synthetic-dynamic-no-touch","synthetic-imu-touch-screen"};
    return {{"scenario",names[scenario]},{"scope",scope==0?"full-serializer-and-output-disposal":"current-assembly-serializer-and-disposal"},{"campaign",campaign},{"fasterRounds",wins},{"pairedRounds",11},{"medianPairedReduction",paired},{"ratioOfMediansReduction",ratio},{"beforeMedianNsPerOperation",median(b)},{"afterMedianNsPerOperation",median(a)},{"allocationsBefore",allocationJson(x)},{"allocationsAfter",allocationJson(y)},{"allocationRequestsAndBytesDoNotIncrease",alloc},{"checksumsAgree",same},{"pairs",pairs},{"pass",wins>=10&&paired>=.05&&ratio>=.05&&alloc&&same}};
}
int main(int argc,char** argv) {
    try {
        if(argc<2)throw std::runtime_error("usage: fixture verify OR fixture benchmark campaign");
        if(std::string(argv[1])=="verify") {
            correctness();json workloads=json::array();Identity id;bind(id);
            for(int scenario=0;scenario<3;++scenario) {const auto frame=focusIsolationAssembleFrame(frameContent(scenario));workloads.push_back({{"scenario",scenario},{"synthetic",true},{"frame",frame},{"wire",wire(false,frame,id)}});}
            std::cout<<json({{"pass",failures==0},{"assertions",assertions},{"failureCount",failures},{"candidateSourceDifferences",differences},{"oracleDifferences",oracleDifferences},{"wireCases",cases},{"jsonFieldTypeComparisons",typedChecks},{"exceptionCases",exceptionCases},{"fixedKeyCalls",r34FixedKeyCount},{"fullOutputDigestFNV1a64",std::to_string(digest)},{"failureDetails",failureDetails},{"declaredSyntheticWorkloads",workloads},{"benchmarkExecuted",false},{"liveApisExecuted",false}}).dump(2)<<'\n';return failures?2:0;
        }
        if(argc!=3||std::string(argv[1])!="benchmark")throw std::runtime_error("invalid benchmark args");
        int campaign=std::stoi(argv[2]);if(campaign<1||campaign>2)throw std::runtime_error("invalid campaign");
        json rows=json::array();bool pass=true;
        for(int scenario=0;scenario<3;++scenario)for(int scope=0;scope<2;++scope) {auto row=formal(scenario,scope,campaign);pass=pass&&row.at("pass").get<bool>();rows.push_back(std::move(row));}
        std::cout<<json({{"pass",pass},{"campaign",campaign},{"rows",rows},{"syntheticOnly",true},{"wholeHardwareClaim",false}}).dump(2)<<'\n';return pass?0:2;
    }catch(const std::exception& e) {counting=false;std::cerr<<e.what()<<'\n';return 1;}
}
