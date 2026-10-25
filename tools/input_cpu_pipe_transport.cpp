// Isolated transport regression and cost fixture; no product or device entrypoint.
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
namespace metric {
inline bool enabled = false;
inline std::uint64_t allocations = 0, allocationBytes = 0, resizes = 0, grownBytes = 0;
inline std::uint64_t reads = 0, writes = 0, resets = 0, queries = 0, waits = 0;
inline void clear() { allocations=allocationBytes=resizes=grownBytes=reads=writes=resets=queries=waits=0; }
}
void* operator new(std::size_t n) {
    if (metric::enabled) { ++metric::allocations; metric::allocationBytes += n; }
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
static void inputCpuResize(std::size_t previous, std::size_t next) {
    #ifdef INPUT_CPU_COUNT_RESIZE
    if (metric::enabled) { ++metric::resizes; if (next > previous) metric::grownBytes += next-previous; }
    #else
    (void)previous; (void)next;
    #endif
}
static unsigned checks = 0;
static void check(bool b, const char* why) { ++checks; if (!b) throw std::runtime_error(why); }
// Only the Runtime's API boundary is synthetic. Real owned event handles still
// exercise adopt/close/reuse ownership. Timing excludes kernel pipe latency.
namespace script {
inline DWORD requested = 0, count = 0;
inline bool overrideCount = false, rejectGuard = false, reading = false;
inline unsigned guardCalls = 0;
inline std::vector<unsigned char> written;
inline const unsigned char receipt[] = {0x51,0x12,0x00,0xA7,0xFF,0x3C,0x77};
inline BOOL write(HANDLE, const void* data, DWORD n, DWORD*, OVERLAPPED*) {
    requested=n; reading=false; if (metric::enabled) ++metric::writes;
    // Capture bytes in correctness only, never in the timed/allocation region.
    if (!metric::enabled) written.assign(static_cast<const unsigned char*>(data),static_cast<const unsigned char*>(data)+n);
    return TRUE;
}
inline BOOL read(HANDLE, void* data, DWORD n, DWORD*, OVERLAPPED*) {
    requested=n; reading=true; if (metric::enabled) ++metric::reads;
    std::memcpy(data,receipt,(std::min)(static_cast<std::size_t>(n),sizeof(receipt)));
    return TRUE;
}
inline BOOL query(HANDLE, OVERLAPPED*, DWORD* n, BOOL) {
    if (metric::enabled) ++metric::queries;
    *n=overrideCount?count:(reading?(std::min)(requested,static_cast<DWORD>(sizeof(receipt))):requested); return TRUE;
}
inline DWORD wait(HANDLE, DWORD) { if(metric::enabled) ++metric::waits; return WAIT_OBJECT_0; }
inline BOOL reset(HANDLE) { if(metric::enabled) ++metric::resets; return TRUE; }
inline BOOL disconnect(HANDLE) { return TRUE; }
inline bool guard(void*) noexcept { ++guardCalls; return !rejectGuard; }
}
#define WriteFile script::write
#define ReadFile script::read
#define GetOverlappedResult script::query
#define WaitForSingleObject script::wait
#define ResetEvent script::reset
#define DisconnectNamedPipe script::disconnect
#define ymcc baseline_ymcc
#include "before.h"
#undef ymcc
#include "after.h"
#undef WriteFile
#undef ReadFile
#undef GetOverlappedResult
#undef WaitForSingleObject
#undef ResetEvent
#undef DisconnectNamedPipe
#include "input_callback_cost.h"
#include "input_frame_submission.h"
namespace before = baseline_ymcc::inputHost::pipeTransport;
namespace after = ymcc::inputHost::pipeTransport;
template<class Runtime, class Kind> struct Session {
    Runtime* owner = new Runtime();
    HANDLE source = nullptr;
    std::vector<unsigned char> request, response;
    explicit Session(DWORD size):request(size,0xC3),response(16*1024,0xD4) {
        source=CreateEventW(nullptr,TRUE,FALSE,nullptr); check(source!=nullptr,"source event");
        DWORD e=0; check(owner->adopt(source,L"input-cpu-script",e),"adopt");
    }
    ~Session() { owner->close(source); owner->sealAndReap(); }
    std::uint64_t one() {
        auto w=owner->run(source,Kind::Write,request.data(),static_cast<DWORD>(request.size()),1000);
        auto r=owner->run(source,Kind::Read,response.data(),static_cast<DWORD>(response.size()),1000);
        if(!w.ok || !r.ok) throw std::runtime_error("scripted round trip");
        return w.operation+r.operation+r.transferred+response[0];
    }
};
static volatile std::uint64_t sink = 0;
static double median(std::vector<double> values) { std::sort(values.begin(),values.end()); return values[values.size()/2]; }
struct Sample { double ns=0, cycles=0; };
template<class S> static Sample timing(S& session,unsigned n) {
    std::uint64_t c0=0,c1=0,sum=0;
    check(QueryThreadCycleTime(GetCurrentThread(),&c0)!=FALSE,"start cycles");
    const auto start=std::chrono::steady_clock::now();
    metric::enabled=true;
    for(unsigned i=0;i<n;++i) sum+=session.one();
    metric::enabled=false;
    const auto end=std::chrono::steady_clock::now();
    check(QueryThreadCycleTime(GetCurrentThread(),&c1)!=FALSE && c1>=c0,"end cycles");
    sink=sum;
    return {std::chrono::duration<double,std::nano>(end-start).count()/n,static_cast<double>(c1-c0)/n};
}
template<class S> static json counts(S& session) {
    metric::clear(); metric::enabled=true;
    for(unsigned i=0;i<1000;++i) sink=session.one();
    metric::enabled=false;
    return {{"roundTrips",1000},{"allocations",metric::allocations},{"allocationBytes",metric::allocationBytes},
        {"resizeCalls",metric::resizes},{"valueInitializedBytes",metric::grownBytes},
        {"writes",metric::writes},{"reads",metric::reads},{"resets",metric::resets},{"queries",metric::queries},{"waits",metric::waits}};
}
static json benchmark(DWORD size) {
    Session<before::Runtime,before::Kind> a(size); Session<after::Runtime,after::Kind> b(size);
    for(unsigned i=0;i<256;++i) { sink=a.one(); sink=b.one(); }
    std::vector<double> an,bn,ac,bc,ratios;
    for(unsigned i=0;i<7;++i) {
        Sample x,y;
        if(i%2) { y=timing(b,131072); x=timing(a,131072); }
        else { x=timing(a,131072); y=timing(b,131072); }
        an.push_back(x.ns);bn.push_back(y.ns);ac.push_back(x.cycles);bc.push_back(y.cycles);ratios.push_back(1-y.ns/x.ns);
    }
    auto ca=counts(a),cb=counts(b);
    #ifdef INPUT_CPU_COUNT_RESIZE
    check(ca["valueInitializedBytes"]==static_cast<std::uint64_t>(1000)*(16384-size),"baseline growth count");
    check(cb["valueInitializedBytes"]<=ca["valueInitializedBytes"],"candidate increased buffer growth");
    #ifdef INPUT_CPU_EXPECT_GROWTH_ELIMINATED
    check(cb["valueInitializedBytes"]==0,"candidate still grows warmed buffer");
    #endif
    #endif
    for(const char* field:{"allocations","allocationBytes","writes","reads","resets","queries","waits"}) check(ca[field]==cb[field],"I/O or allocation count changed");
    return {{"requestBytes",size},{"readCapacity",16384},{"samples",7},{"iterationsPerSample",131072},
        {"baselineNs",an},{"candidateNs",bn},{"baselineCycles",ac},{"candidateCycles",bc},
        {"baselineMedianNs",median(an)},{"candidateMedianNs",median(bn)},
        {"pairedMedianElapsedReduction",median(ratios)},{"baselineMedianCycles",median(ac)},{"candidateMedianCycles",median(bc)},
        {"baselineCounts",ca},{"candidateCounts",cb}};
}
template<class Runtime,class Kind> static json correctness() {
    Session<Runtime,Kind> s(512); json rows=json::array();
    // Warm a full buffer, then require exact short writes and short read limits.
    sink=s.one();
    for(DWORD size:{0u,1u,8u,4096u,16384u,17u}) {
        std::vector<unsigned char> wire(size,static_cast<unsigned char>(size+11));
        auto w=s.owner->run(s.source,Kind::Write,wire.data(),size,1000);
        check(w.ok&&script::requested==size&&script::written==wire,"write length or bytes changed after long read");
        std::vector<unsigned char> data(16384,0xCC);
        script::overrideCount=true;script::count=(std::min)(size,static_cast<DWORD>(sizeof(script::receipt)));
        auto r=s.owner->run(s.source,Kind::Read,data.data(),size,1000);
        check(r.ok&&r.byteCountKnown&&r.transferred==script::count&&script::requested==size,"read length/count changed");
        check(std::equal(data.begin(),data.begin()+script::count,script::receipt),"read prefix changed");
        check(std::all_of(data.begin()+script::count,data.end(),[](auto v){return v==0xCC;}),"unused caller tail changed");
        rows.push_back({{"size",size},{"writeCount",w.transferred},{"readCount",r.transferred}});
    }
    std::vector<unsigned char> data(16384,0xA5);
    script::count=9;
    auto bad=s.owner->run(s.source,Kind::Read,data.data(),8,1000);
    check(!bad.ok&&!bad.byteCountKnown&&bad.error==ERROR_INVALID_DATA,"oversize count accepted using retained storage size");
    check(std::all_of(data.begin(),data.end(),[](auto v){return v==0xA5;}),"unknown read copied bytes");
    script::overrideCount=false;
    const auto stats=s.owner->snapshot(); script::guardCalls=0;script::rejectGuard=true;
    auto reject=s.owner->run(s.source,Kind::Write,data.data(),32,1000,50,script::guard,nullptr);
    check(reject.contextRejected&&!reject.admitted&&script::guardCalls==1,"preissue guard bypassed");
    check(s.owner->snapshot().admitted==stats.admitted,"guard admitted operation");script::rejectGuard=false;
    auto invalid=s.owner->run(s.source,Kind::Read,data.data(),16385,1000);
    check(!invalid.admitted&&invalid.error==ERROR_INVALID_PARAMETER,"message cap bypassed");
    auto final=s.owner->snapshot();check(final.pending==0&&final.payloadBytes==0,"terminal accounting");
    return {{"shapes",rows},{"oversizeCountRejected",true},{"preissueGuardPreserved",true},{"messageCapPreserved",true}};
}
static json pureHeaders() {
    namespace cc=ymcc::input::callbackCost;
    unsigned elapsed=0,cycles=0,threads=0;
    auto re=[&]{++elapsed;return cc::Value{100};};
    auto rc=[&]{++cycles;return cc::CycleSample{true,500};};
    auto rt=[&]{++threads;return std::uint32_t{7};};
    check(cc::activeContext==nullptr,"initial TLS");
    { cc::Scope off(false,cc::Source::multimedia,re,rc,rt);check(elapsed+cycles+threads==0,"disabled scope reads clock or thread"); }
    { cc::Scope outer(true,cc::Source::multimedia,re,rc,rt);const auto* saved=cc::activeContext;
      { cc::Scope inner(false,cc::Source::waitableFallback,re,rc,rt);check(cc::activeContext!=saved&&!cc::activeContext->enabled,"disabled inner leaks enabled scope"); }
      check(cc::activeContext==saved,"nested TLS restore");
      auto m=cc::measure(saved,true,7,125,{true,550});check(m.elapsedUs==25&&m.cycles==50,"enabled measurement"); }
    check(cc::activeContext==nullptr,"final TLS");
    namespace fs=ymcc::input::frameSubmission;
    fs::Current current;current.ready=true;current.host=1;current.power=2;current.intent=3;
    fs::Ticket ticket{1,2,3,true};fs::Sample sample;
    check(fs::decide(ticket,sample,current,100)==fs::Verdict::Allowed,"neutral source absence");
    ticket.neutral=false;check(fs::decide(ticket,sample,current,100)==fs::Verdict::SourceUnavailable,"source gate");
    ticket.neutral=true;current.power=4;check(fs::decide(ticket,sample,current,100)==fs::Verdict::PowerChanged,"neutral power gate");
    return {{"disabledElapsedReads",0},{"disabledCycleReads",0},{"disabledThreadReads",0},{"nestedScopeRestored",true},{"neutralStillChecksPower",true}};
}
int main(int argc,char**) {
    try {
        DWORD handles0=0,handles1=0;check(GetProcessHandleCount(GetCurrentProcess(),&handles0)!=FALSE,"initial handles");
        auto a=correctness<before::Runtime,before::Kind>();auto b=correctness<after::Runtime,after::Kind>();check(a==b,"public result parity");
        auto pure=pureHeaders();json rows=json::array();
        DWORD_PTR allowed=0,system=0,previousAffinity=0;
        if(argc>1 && GetProcessAffinityMask(GetCurrentProcess(),&allowed,&system) && allowed) {
            const DWORD cpu=GetCurrentProcessorNumber();
            const DWORD_PTR current=cpu<sizeof(DWORD_PTR)*8?(static_cast<DWORD_PTR>(1)<<cpu):0;
            previousAffinity=SetThreadAffinityMask(GetCurrentThread(),(current&allowed)?current:(allowed&(~allowed+1)));
        }
        if(argc>1) for(unsigned campaign=0;campaign<3;++campaign)
            for(DWORD size:{256u,1024u,4096u,12288u,16384u}) {
                auto row=benchmark(size);row["campaign"]=campaign;rows.push_back(std::move(row));
            }
        if(previousAffinity) SetThreadAffinityMask(GetCurrentThread(),previousAffinity);
        check(GetProcessHandleCount(GetCurrentProcess(),&handles1)!=FALSE&&handles0==handles1,"handle leak");
        std::cout<<json({{"pass",true},{"checks",checks},{"correctness",b},{"pureHeaders",pure},{"benchmark",rows},
            {"processHandlesBefore",handles0},{"processHandlesAfter",handles1},{"syntheticApiBoundary",true},
            {"affinityApplied",previousAffinity!=0},{"kernelPipeLatencyMeasured",false},{"productOrInputDevicesUsed",false}}).dump(2)<<'\n';
        return 0;
    } catch(const std::exception& e) { metric::enabled=false;std::cerr<<e.what()<<'\n';return 1; }
}
