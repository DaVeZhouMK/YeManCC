// Standalone diagnostic fixture; no startup, timer, pipe or device business calls.
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <thread>
#include <type_traits>
#include <vector>
#include "json.hpp"
#include "input_callback_cost.h"
using json=nlohmann::json;
using namespace ymcc::input::callbackCost;
static std::atomic<unsigned long long> allocations{0},checks{0};
static thread_local bool countAllocations=false;
void* operator new(std::size_t size){if(countAllocations)++allocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t s){return ::operator new(s);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void expect(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
namespace injected {
static thread_local DWORD fakeThreadId=1;
static thread_local CycleSample fakeCycles{true,1400};
static thread_local unsigned cycleCalls=0,threadCalls=0;
static DWORD GetCurrentThreadId(){++threadCalls;return fakeThreadId;}
static BOOL QueryThreadCycleTime(HANDLE,PULONG64 value){++cycleCalls;*value=fakeCycles.value;return fakeCycles.known;}
#include "helper_state.hpp"
#include "actual_helpers.hpp"
}
namespace real {
#include "helper_state.hpp"
#include "actual_helpers.hpp"
#include "benchmark_helpers.hpp"
}
static auto at(Value us){return std::chrono::steady_clock::time_point(std::chrono::microseconds(us));}
static void pureProof(){
 static_assert(!std::is_copy_constructible_v<Scope> && !std::is_move_constructible_v<Scope>);
 expect(activeContext==nullptr,"initial TLS empty");unsigned reads=0;
 {Scope off(false,Source::multimedia,[&]{++reads;return 1ULL;},[&]{++reads;return CycleSample{};},[&]{++reads;return 1U;});
 expect(!reads,"disabled invokes no providers");expect(!activeContext->enabled,"disabled owns span");auto m=measure(activeContext,true,1,200,{true,400});expect(m.elapsedReason==Reason::startDisabled && m.cyclesReason==Reason::startDisabled,"start off end on unknown");}
 expect(!activeContext,"disabled restores");
 {Scope a(true,Source::multimedia,[]{return 100ULL;},[]{return CycleSample{true,1000000};},[]{return 1U;});auto outer=activeContext;
 {Scope b(true,Source::multimedia,[]{return 140ULL;},[]{return CycleSample{true,5000000};},[]{return 2U;});expect(activeContext!=outer,"nested owns span");}
 expect(activeContext==outer,"busy/nested restores");auto m=measure(activeContext,true,1,200,{true,1000400});expect(m.elapsedUs==100 && m.cycles==400,"A isolated delta");
 auto oldU=Value(200)-Value(140),oldC=Value(1000400)-Value(5000000);expect(oldU==60 && oldC>1000000000000ULL,"old A/B overwrite counterexample");
 {Scope off(false,Source::waitableFallback,[]{return 0ULL;},[]{return CycleSample{};},[]{return 0U;});m=measure(activeContext,true,1,200,{true,1000400});expect(m.cyclesReason==Reason::startDisabled,"disabled masks outer");}
 expect(activeContext==outer,"disabled nested restore");
 try{Scope b(true,Source::waitableFallback,[]{return 300ULL;},[]{return CycleSample{true,0};},[]{return 1U;});throw 1;}catch(int){}
 expect(activeContext==outer,"unwind restores");}
 expect(!activeContext,"scope complete clears");auto m=measure(nullptr,true,1,200,{true,400});expect(m.source==Source::absent && m.elapsedReason==Reason::noScope,"missing scope explicit");
 Context c{true,Source::waitableFallback,1,0,{true,0}};
 m=measure(&c,true,1,0,{true,0});expect(m.elapsedReason==Reason::valid && m.cyclesReason==Reason::valid && !m.cycles && !m.elapsedUs,"known zero valid");
 m=measure(&c,false,1,100,{true,400});expect(m.elapsedReason==Reason::endDisabled && m.cyclesReason==Reason::endDisabled,"end disabled");
 m=measure(&c,true,2,100,{true,400});expect(m.elapsedReason==Reason::wrongThread && m.cyclesReason==Reason::wrongThread,"wrong thread");
 c.elapsedStartUs=100;c.cyclesStart={true,400};m=measure(&c,true,1,99,{true,399});expect(m.elapsedReason==Reason::backwardsElapsed && m.cyclesReason==Reason::backwardsCycles && !m.elapsedUs && !m.cycles,"no backward wrap");
 c.cyclesStart={false,400};m=measure(&c,true,1,100,{true,500});expect(m.cyclesReason==Reason::startCyclesUnavailable,"failed start");
 c.cyclesStart={true,400};m=measure(&c,true,1,100,{false,500});expect(m.cyclesReason==Reason::endCyclesUnavailable,"failed end");
 std::mt19937_64 gen(0x120015);for(int i=0;i<20000;++i){Value u=gen(),cy=gen(),end=gen(),ec=gen();c.elapsedStartUs=u;c.cyclesStart={true,cy};m=measure(&c,true,1,end,{true,ec});expect(m.elapsedReason==(end>=u?Reason::valid:Reason::backwardsElapsed),"random elapsed reason");expect(m.cyclesReason==(ec>=cy?Reason::valid:Reason::backwardsCycles),"random cycles reason");expect(m.elapsedUs==(end>=u?end-u:0),"random elapsed delta");expect(m.cycles==(ec>=cy?ec-cy:0),"random cycle delta");}
 std::atomic<bool> bad{false};std::vector<std::thread> threads;
 for(unsigned t=0;t<8;++t)threads.emplace_back([&,t]{for(unsigned j=0;j<4096;++j){Scope outer(true,Source::multimedia,[&]{return Value(j);},[&]{return CycleSample{true,Value(j)*4};},[&]{return t+1;});auto prev=activeContext;
 {Scope inner(true,Source::waitableFallback,[]{return 100000ULL;},[]{return CycleSample{true,100000};},[&]{return t+1;});auto r=measure(activeContext,true,t+1,100004,{true,100016});if(r.elapsedUs!=4||r.cycles!=16)bad=true;}
 auto r=measure(activeContext,true,t+1,Value(j)+7,{true,Value(j)*4+28});if(activeContext!=prev||r.elapsedUs!=7||r.cycles!=28)bad=true;checks.fetch_add(3);}});
 for(auto& t:threads)t.join();expect(!bad,"8 concurrent isolated streams");expect(!activeContext,"main TLS still empty");
 for(bool on:{false,true}){countAllocations=true;auto a0=allocations.load();for(unsigned i=0;i<8192;++i){Scope scope(on,Source::multimedia,[&]{return Value(i);},[&]{return CycleSample{true,Value(i)};},[]{return 1U;});auto r=measure(activeContext,true,1,Value(i)+10,{true,Value(i)+20});if(r.cyclesReason!=(on?Reason::valid:Reason::startDisabled))throw std::runtime_error("allocation loop");}auto a1=allocations.load();countAllocations=false;expect(a0==a1,"scope allocations zero");}
}
static void integratedProof(){using namespace injected;
 costGate=true;fakeThreadId=1;fakeCycles={true,1400};threadCalls=cycleCalls=0;
 {Scope a(true,Source::multimedia,[]{return 100ULL;},[]{return CycleSample{true,1000};},[]{return 1U;});tickCostRecordPreamble(at(200));}
 expect(g_tickCostCount[kTickCostPreamble]==1 && g_tickCostSumUs[kTickCostPreamble]==100,"actual elapsed accumulation");expect(g_tickCostCount[kTickCostPreambleCpu]==1 && g_tickCostSumUs[kTickCostPreambleCpu]==100,"actual checked cycle accumulation");expect(cycleCalls==1 && threadCalls==1,"end providers once");
 auto row=tickCostCallbackWindowJson();expect(row["sources"]["multimedia"]==1 && row["sumCycles"]==400 && row["maxCycles"]==400,"raw cycle evidence");expect(row["cycleReasons"]["valid"]==1 && row["elapsedReasons"]["valid"]==1,"valid count");auto empty=tickCostCallbackWindowJson();expect(empty["sumCycles"]==0 && empty["sources"]["multimedia"]==0,"window drains once");
 fakeCycles={false,0};{Scope a(true,Source::waitableFallback,[]{return 0ULL;},[]{return CycleSample{true,0};},[]{return 1U;});tickCostRecordPreamble(at(20));}
 row=tickCostCallbackWindowJson();expect(row["sources"]["waitable-fallback"]==1 && row["cycleReasons"]["end-cycles-unavailable"]==1 && row["sumCycles"]==0,"failure explicit");expect(g_tickCostCount[kTickCostPreambleCpu]==1,"failure excluded");
 fakeCycles={true,0};{Scope a(true,Source::waitableFallback,[]{return 0ULL;},[]{return CycleSample{true,0};},[]{return 1U;});tickCostRecordPreamble(at(0));}
 expect(g_tickCostCount[kTickCostPreambleCpu]==2,"known zero included");row=tickCostCallbackWindowJson();expect(row["cycleReasons"]["valid"]==1 && row["sumCycles"]==0,"zero valid count");
 fakeCycles={true,100};cycleCalls=threadCalls=0;{Scope a(false,Source::multimedia,[]{return 0ULL;},[]{return CycleSample{};},[]{return 0U;});tickCostRecordPreamble(at(200));}
 expect(!cycleCalls && !threadCalls,"unknown start no syscalls");row=tickCostCallbackWindowJson();expect(row["elapsedReasons"]["start-disabled"]==1,"start off end on recorded");tickCostRecordPreamble(at(200));row=tickCostCallbackWindowJson();expect(row["cycleReasons"]["no-scope"]==1,"missing scope recorded");
 auto samples=g_tickCostCount[kTickCostPreamble].load();costGate=false;cycleCalls=threadCalls=0;{Scope a(true,Source::multimedia,[]{return 100ULL;},[]{return CycleSample{true,0};},[]{return 1U;});tickCostRecordPreamble(at(200));}
 expect(!cycleCalls && !threadCalls && samples==g_tickCostCount[kTickCostPreamble],"end off no work");costGate=true;
 {Scope a(true,Source::multimedia,[]{return 300ULL;},[]{return CycleSample{true,1000};},[]{return 2U;});tickCostRecordPreamble(at(200));}
 row=tickCostCallbackWindowJson();expect(row["elapsedReasons"]["wrong-thread"]==1,"owner rejected");
 {Scope a(true,Source::multimedia,[]{return 300ULL;},[]{return CycleSample{true,1000};},[]{return 1U;});tickCostRecordPreamble(at(200));}
 row=tickCostCallbackWindowJson();expect(row["elapsedReasons"]["backwards-elapsed"]==1 && row["cycleReasons"]["backwards-cycles"]==1,"actual guards wrap");expect(row["gateProvenance"].get<std::string>().find("unobserved")!=std::string::npos,"gate gap labeled");
}
static double timeBatch(bool cur,bool,unsigned n){auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<n;++i){if(cur)real::currentTick();else real::baselineTick();}return double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count())/n;}
static json benchmark(){json rows=json::array();for(bool on:{true,false}){real::costGate=on;auto drained=real::tickCostCallbackWindowJson();expect(drained.is_object(),"real window available");auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(600);while(std::chrono::steady_clock::now()<end){timeBatch(false,on,1000);timeBatch(true,on,1000);}std::vector<double>b,a;json rounds=json::array();unsigned faster=0;for(unsigned r=0;r<9;++r){double x,y;if(r%2){y=timeBatch(true,on,25000);x=timeBatch(false,on,25000);}else{x=timeBatch(false,on,25000);y=timeBatch(true,on,25000);}b.push_back(x);a.push_back(y);faster+=y<x;rounds.push_back({{"beforeNs",x},{"currentNs",y}});}std::sort(b.begin(),b.end());std::sort(a.begin(),a.end());allocations=0;countAllocations=true;timeBatch(false,on,1000);auto ba=allocations.load();allocations=0;timeBatch(true,on,1000);auto aa=allocations.load();countAllocations=false;rows.push_back({{"enabled",on},{"beforeMedianNs",b[4]},{"currentMedianNs",a[4]},{"fasterRounds",faster},{"beforeAllocationCount",ba},{"currentAllocationCount",aa},{"rounds",rounds}});}return rows;}
int main(int argc,char**argv){try{pureProof();integratedProof();json result={{"pass",true},{"checks",checks.load()},{"threads",8},{"randomPairs",20000},{"usesActualRecorderAndWindow",true},{"businessCalls",false},{"cycleConversionForFixture","fixed cycles/4; calibration not timed or changed"}};if(argc>1 && std::string(argv[1])=="benchmark")result["benchmark"]=benchmark();std::cout<<result.dump(2)<<"\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
