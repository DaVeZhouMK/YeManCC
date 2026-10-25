#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "native_process_cost.h"
using json=nlohmann::json;
static unsigned checks=0;
static void require(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
static void absentRate(const json& j){require(!j.at("cpuRateAvailable").get<bool>(),"rate unexpectedly available");require(!j.contains("cpuMsPerSec"),"unavailable rate represented as zero");}
namespace fake {
static DWORD error=777;static unsigned cpuCalls=0,memoryCalls=0,clockCalls=0;
static bool cpuOk=true,memoryOk=true;static DWORD pid=42;static ULONGLONG tick=1000,birth=123456789;
static ULONGLONG kernel=10000000,user=20000000;static SIZE_T working=12345678,privateBytes=23456789;
static DWORD GetLastError(){return error;}static void SetLastError(DWORD x){error=x;}
static DWORD GetCurrentProcessId(){return pid;}static HANDLE GetCurrentProcess(){return reinterpret_cast<HANDLE>(1);}
static ULONGLONG GetTickCount64(){++clockCalls;error=888;return tick;}
static void fill(FILETIME* ft,ULONGLONG n){ft->dwLowDateTime=static_cast<DWORD>(n);ft->dwHighDateTime=static_cast<DWORD>(n>>32);}
static BOOL fakeGetProcessTimes(HANDLE,FILETIME* c,FILETIME* e,FILETIME* k,FILETIME* u){
 ++cpuCalls;if(!cpuOk){error=5;return FALSE;}fill(c,birth);fill(e,0);fill(k,kernel);fill(u,user);error=81;return TRUE;
}
static BOOL fakeGetProcessMemoryInfo(HANDLE,PROCESS_MEMORY_COUNTERS* m,DWORD bytes){
 ++memoryCalls;if(!memoryOk){error=8;return FALSE;}require(bytes==sizeof(PROCESS_MEMORY_COUNTERS_EX),"memory struct size");
 auto* ex=reinterpret_cast<PROCESS_MEMORY_COUNTERS_EX*>(m);ex->WorkingSetSize=working;ex->PrivateUsage=privateBytes;error=82;return TRUE;
}
#undef GetProcessMemoryInfo
#define GetProcessTimes fakeGetProcessTimes
#define GetProcessMemoryInfo fakeGetProcessMemoryInfo
#include "adapter-extracted.h"
#undef GetProcessTimes
#undef GetProcessMemoryInfo
#define GetProcessMemoryInfo K32GetProcessMemoryInfo
static void reset(){
 cpuOk=memoryOk=true;pid=42;tick=1000;birth=123456789;kernel=10000000;user=20000000;
 working=12345678;privateBytes=23456789;error=777;require(inputNativeProcessCostSnapshot(false).is_null(),"off null");
 cpuCalls=memoryCalls=clockCalls=0;
}
}
namespace real {
#include "adapter-extracted.h"
}
static void testAdapter(){
 using namespace fake;
 reset();for(int i=0;i<32;++i)require(inputNativeProcessCostSnapshot(false).is_null(),"closed gate result");
 require(cpuCalls==0&&memoryCalls==0&&clockCalls==0,"closed gate performed OS reads");require(error==777,"closed gate changed last error");
 auto first=inputNativeProcessCostSnapshot(true);absentRate(first);require(first.at("cpuStatus")=="baseline","baseline status");
 require(first.at("kernelTotal100ns")==kernel&&first.at("userTotal100ns")==user,"raw cpu fields");
 require(first.at("processCreation100ns")==birth&&first.at("pid")==pid,"process identity");
 require(first.at("workingSetBytes")==working&&first.at("privateCommitBytes")==privateBytes,"memory distinction");
 require(cpuCalls==1&&memoryCalls==1&&clockCalls==1,"one snapshot read counts");require(error==777,"success last error");
 tick=2000;kernel+=1000000;user+=2000000;auto next=inputNativeProcessCostSnapshot(true);
 require(next.at("cpuRateAvailable")==true,"steady available");require(next.at("intervalMs")==1000,"steady interval");
 require(next.at("kernelCpuMsPerSec")==100.0&&next.at("userCpuMsPerSec")==200.0&&next.at("cpuMsPerSec")==300.0,"100ns unit normalization");
 tick=3000;kernel+=20000000;user+=30000000;next=inputNativeProcessCostSnapshot(true);require(next.at("cpuMsPerSec")==5000.0,"multi-thread rate clamped");
 tick=4000;cpuOk=false;next=inputNativeProcessCostSnapshot(true);absentRate(next);require(next.at("cpuError")==5,"cpu failure lost before memory query");
 require(next.at("memoryAvailable")==true&&next.at("workingSetBytes")==working,"cpu failure removed memory");require(error==777,"cpu error leaked");
 cpuOk=true;tick=5000;next=inputNativeProcessCostSnapshot(true);absentRate(next);require(next.at("cpuStatus")=="baseline","failure did not reset baseline");
 tick=6000;memoryOk=false;kernel+=1000000;next=inputNativeProcessCostSnapshot(true);require(next.at("cpuRateAvailable")==true,"memory failure removed cpu");
 require(next.at("memoryError")==8&&!next.contains("workingSetBytes")&&!next.contains("privateCommitBytes"),"memory failure represented as zero");require(error==777,"memory error leaked");
 memoryOk=true;pid=43;tick=7000;next=inputNativeProcessCostSnapshot(true);absentRate(next);require(next.at("cpuStatus")=="process-identity-changed","pid identity");
 birth++;tick=8000;next=inputNativeProcessCostSnapshot(true);absentRate(next);require(next.at("cpuStatus")=="process-identity-changed","birth identity");
 tick=7900;next=inputNativeProcessCostSnapshot(true);absentRate(next);require(next.at("cpuStatus")=="clock-regressed","clock backwards");
 next=inputNativeProcessCostSnapshot(true);absentRate(next);require(next.at("cpuStatus")=="interval-not-positive","equal clock");
 tick=8900;kernel--;next=inputNativeProcessCostSnapshot(true);absentRate(next);require(next.at("cpuStatus")=="counter-regressed","kernel regression");
 tick=9900;user--;next=inputNativeProcessCostSnapshot(true);absentRate(next);require(next.at("cpuStatus")=="counter-regressed","user regression");
 tick=12900;next=inputNativeProcessCostSnapshot(true);require(next.at("cpuRateAvailable")==true,"exact max gap");require(next.at("cpuMsPerSec")==0.0,"observed idle zero should stay available");
 tick=15901;next=inputNativeProcessCostSnapshot(true);absentRate(next);require(next.at("cpuStatus")=="observation-gap","sleep or long gap averaged away");
 tick=16901;kernel+=500000;user+=500000;next=inputNativeProcessCostSnapshot(true);require(next.at("cpuMsPerSec")==100.0,"gap rebaseline");
 inputNativeProcessCostSnapshot(false);tick=17901;next=inputNativeProcessCostSnapshot(true);absentRate(next);require(next.at("cpuStatus")=="baseline","gate rearm included closed period");
 require(error==777,"later last error");require(!next.contains("memoryLeak"),"counter observation became leak verdict");
}
static void testUnsignedLimits(){
 ymcc::nativeCost::Window w;ymcc::nativeCost::Counters c;c.cpuKnown=c.memoryKnown=true;c.processId=42;c.creation100ns=9;c.tick=1;
 absentRate(ymcc::nativeCost::toJson(w.observe(c)));c.tick=1001;c.kernel100ns=c.user100ns=std::numeric_limits<std::uint64_t>::max()-5;
 c.workingSetBytes=c.privateCommitBytes=std::numeric_limits<std::uint64_t>::max();auto row=ymcc::nativeCost::toJson(w.observe(c));
 require(row.at("cpuRateAvailable")==true&&std::isfinite(row.at("cpuMsPerSec").get<double>()),"wide deltas overflowed");
 require(row.at("kernelDelta100ns").is_number_unsigned(),"raw units lost uint64 type");
 require(json::parse(row.dump()).at("privateCommitBytes").get<std::uint64_t>()==c.privateCommitBytes,"wide memory field changed");
 w.reset();absentRate(ymcc::nativeCost::toJson(w.observe(c)));
}
static json testOwnProcessRead(){
 ::SetLastError(4321);require(real::inputNativeProcessCostSnapshot(false).is_null(),"real closed gate");
 auto before=real::inputNativeProcessCostSnapshot(true);require(::GetLastError()==4321,"real read last error");
 require(before.at("cpuCounterAvailable")==true&&before.at("memoryAvailable")==true,"own process counters unavailable");
 const auto start=std::chrono::steady_clock::now();volatile unsigned long long work=0;
 while(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(60)){for(unsigned i=0;i<10000;++i)work=work+i;}
 const auto readStart=std::chrono::steady_clock::now();auto after=real::inputNativeProcessCostSnapshot(true);
 const auto readUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-readStart).count();
 require(after.at("cpuRateAvailable")==true,"real counter interval");require(after.at("kernelTotal100ns")>=before.at("kernelTotal100ns")&&after.at("userTotal100ns")>=before.at("userTotal100ns"),"own process counter monotonic");
 require(after.at("pid").get<DWORD>()==::GetCurrentProcessId(),"read wrong process");require(after.at("cpuMsPerSec").get<double>()>0,"busy process cpu zero");require(::GetLastError()==4321,"real late error");
 return {{"before",before},{"after",after},{"oneObservedReadUs",readUs},{"scope","Fixture's own process only; no YMCC, Host, Steam or controller execution"}};
}
int main(){try{testAdapter();testUnsignedLimits();auto real=testOwnProcessRead();std::cout<<json{{"pass",true},{"checks",checks},{"realOwnProcessRead",real},{"inputHostNativeMainOrHardwareExecuted",false}}.dump();return 0;}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
