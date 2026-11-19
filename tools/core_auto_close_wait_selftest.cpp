// Runs extracted production worker bodies against fake process/window APIs.
// The separate ScanWait tests use real condition variables and synthetic threads only.
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include "auto_close_wait.h"
static void fixtureRealSleep(DWORD ms){::Sleep(ms);}
struct Scenario {
    std::string mode;
    unsigned stopAt=60000;
    bool operator==(const Scenario&)const=default;
};
struct Observation {
    std::vector<unsigned> scans;
    std::vector<std::pair<unsigned,DWORD>> closed;
    unsigned waits=0,now=0,snapshotCloses=0;
};
#define Sleep fixtureSleep
#define GetCurrentProcessId fixtureGetCurrentProcessId
#define CreateToolhelp32Snapshot fixtureCreateToolhelp32Snapshot
#define Process32FirstW fixtureProcess32FirstW
#define Process32NextW fixtureProcess32NextW
#define CloseHandle fixtureCloseHandle
#define GetWindowThreadProcessId fixtureGetWindowThreadProcessId
#define PostMessageW fixturePostMessageW
#define EnumWindows fixtureEnumWindows
namespace before {
#include "auto-close-shims.inc"
#include "auto-close-before.inc"
}
namespace after {
#include "auto-close-shims.inc"
#include "auto-close-after.inc"
}
#undef Sleep
#undef GetCurrentProcessId
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW
#undef CloseHandle
#undef GetWindowThreadProcessId
#undef PostMessageW
#undef EnumWindows
template<class Expect> static void testParkedStop(Expect&& expect) {
        std::atomic<bool> stopped{false};ymcc::autoclose::ScanWait wait(stopped);
        std::promise<void> entered;
        auto result=std::async(std::launch::async,[&]{entered.set_value();return wait.waitForNextScan(std::chrono::milliseconds(5000));});
        entered.get_future().get();
        // Short fixture-only delay ensures cancellation also covers a parked waiter.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        wait.requestStop();
        expect(result.wait_for(std::chrono::milliseconds(1000))==std::future_status::ready&&!result.get(),"parked real 5s waiter exits without waiting for deadline");
    }

struct WaitCostRow {
    bool after=false,known=false,noEnumeration=false;
    unsigned waits=0;unsigned long long cycles=0,cpu100ns=0;
    double wallMs=0;
};
static unsigned long long costFt(FILETIME value){return (static_cast<unsigned long long>(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
static WaitCostRow waitCost(bool useCandidate) {
    WaitCostRow row;row.after=useCandidate;std::promise<void> entered;
    auto ready=entered.get_future();
    std::thread worker([&]{
        if(useCandidate){after::observed={};after::realWaitCost=true;after::g_summonQuit=false;after::g_autoCloseEnabled=false;}
        else {before::observed={};before::realWaitCost=true;before::g_summonQuit=false;before::g_autoCloseEnabled=false;}
        FILETIME b{},e{},k0{},u0{},k1{},u1{};ULONG64 first=0,last=0;LARGE_INTEGER q0{},q1{},frequency{};
        bool known=QueryThreadCycleTime(GetCurrentThread(),&first)&&GetThreadTimes(GetCurrentThread(),&b,&e,&k0,&u0);
        QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&q0);entered.set_value();
        if(useCandidate)after::autoCloseThread(nullptr);else before::autoCloseThread(nullptr);
        QueryPerformanceCounter(&q1);
        known=known&&QueryThreadCycleTime(GetCurrentThread(),&last)&&GetThreadTimes(GetCurrentThread(),&b,&e,&k1,&u1);
        row.known=known&&last>=first;row.cycles=last-first;
        row.cpu100ns=costFt(k1)+costFt(u1)-costFt(k0)-costFt(u0);
        row.wallMs=double(q1.QuadPart-q0.QuadPart)/frequency.QuadPart*1000;
        const auto& observed=useCandidate?after::observed:before::observed;
        row.waits=observed.waits;row.noEnumeration=observed.scans.empty()&&observed.closed.empty();
    });
    ready.get();fixtureRealSleep(30000);
    if(useCandidate)after::g_autoCloseWait.requestStop();else before::g_autoCloseWait.requestStop();
    worker.join();return row;
}
static int runWaitCost(){
    std::vector<WaitCostRow> rows;
    // One fixed ABBA campaign; no frequency conversion, retiming or best-run selection.
    for(const bool after:{false,true,true,false})rows.push_back(waitCost(after));
    double oldCycles=0,newCycles=0,oldMs=0,newMs=0;unsigned oldWaits=0,newWaits=0;bool pass=true;
    std::cout<<"{\"kind\":\"actual-extracted-worker-real-disabled-wait-cost\",\"rows\":[";
    for(size_t i=0;i<rows.size();++i){const auto& r=rows[i];if(i)std::cout<<',';
        pass=pass&&r.known&&r.noEnumeration&&r.wallMs>=29900&&r.wallMs<32000;
        if(r.after){newCycles+=r.cycles;newMs+=r.wallMs;newWaits+=r.waits;}
        else {oldCycles+=r.cycles;oldMs+=r.wallMs;oldWaits+=r.waits;}
        std::cout<<"{\"after\":"<<(r.after?"true":"false")<<",\"counterKnown\":"<<(r.known?"true":"false")
          <<",\"noEnumerationOrWindowCalls\":"<<(r.noEnumeration?"true":"false")<<",\"waitCalls\":"<<r.waits
          <<",\"threadCycles\":"<<r.cycles<<",\"threadCpu100ns\":"<<r.cpu100ns<<",\"wallMs\":"<<r.wallMs<<'}';
    }
    const double ratio=(newCycles/newMs)/(oldCycles/oldMs);
    pass=pass&&oldCycles>0&&oldWaits>=500&&newWaits<=16&&newWaits*25<=oldWaits&&ratio<=0.95;
    std::cout<<"],\"cycleRateRatio\":"<<ratio<<",\"targetMaximumRatio\":0.95,\"oldWaitCalls\":"<<oldWaits
      <<",\"newWaitCalls\":"<<newWaits<<",\"pass\":"<<(pass?"true":"false")
      <<",\"wholeProcessCpuAcceptance\":false,\"realApplicationsControlled\":false,\"realProcessesEnumerated\":false}\n";
    return pass?0:1;
}
int main(int argc,char** argv) {
    const bool parkedOnly=argc==3&&std::string(argv[1])=="--case"&&std::string(argv[2])=="parked-stop";
    if(argc==2&&std::string(argv[1])=="--wait-cost")return runWaitCost();
    if(argc!=1&&!parkedOnly){std::cerr<<"unknown or mixed autoclose selection\n";return 2;}

    unsigned checks=0;std::vector<std::string> failed;
    const auto expect=[&](bool pass,const std::string& name){++checks;if(!pass)failed.push_back(name);};
    Observation minuteBefore,minuteAfter;
    if(!parkedOnly){
    for(const auto& scenario:std::vector<Scenario>{{"enabled",60000},{"disabled",60000},{"empty",60000},{"mixed",60000},{"enabled",7500},{"enabled",0},{"disabled",100},{"snapshot-failure",15000}}){
        const auto a=before::run(scenario),b=after::run(scenario);
        expect(a.scans==b.scans,scenario.mode+" scan deadlines identical; stop="+std::to_string(scenario.stopAt));
        expect(a.closed==b.closed,scenario.mode+" exact gentle-close destinations identical");
        expect(a.snapshotCloses==b.snapshotCloses,scenario.mode+" snapshot cleanup identical");
        expect(b.now<=scenario.stopAt,scenario.mode+" exit interrupts current wait");
        if(scenario.mode=="enabled"&&scenario.stopAt==60000){minuteBefore=a;minuteAfter=b;}
        if(scenario.mode=="disabled"||scenario.mode=="empty")expect(b.scans.empty(),scenario.mode+" performs no process enumeration");
        if(scenario.mode=="disabled"&&scenario.stopAt==60000)expect(a.waits==600&&b.waits==12,"default disabled feature also reduces waits 600 -> 12");
    }
    expect(minuteBefore.waits==600&&minuteAfter.waits==12,"one minute waits 600 -> 12");
    expect(minuteAfter.scans.size()==11&&minuteAfter.scans.front()==5000&&minuteAfter.scans.back()==55000,"5s scan phase preserved up to stop boundary");
    expect(minuteAfter.closed.size()==22,"two matched non-self windows per scan");
    {
        std::atomic<bool> stopped{false};ymcc::autoclose::ScanWait wait(stopped);
        const auto began=std::chrono::steady_clock::now();
        expect(wait.waitForNextScan(std::chrono::milliseconds(25)),"real idle timeout admits next scan");
        expect(std::chrono::steady_clock::now()-began>=std::chrono::milliseconds(25),"real wait never returns before scan interval");
        wait.requestStop();wait.requestStop();
        expect(!wait.waitForNextScan(std::chrono::milliseconds(5000)),"stop-before-wait and repeated stop are retained");
    }
    for(unsigned round=0;round<32;++round){
        std::atomic<bool> stopped{false};ymcc::autoclose::ScanWait wait(stopped);
        auto result=std::async(std::launch::async,[&]{return wait.waitForNextScan(std::chrono::milliseconds(5000));});
        wait.requestStop();
        expect(result.wait_for(std::chrono::milliseconds(1000))==std::future_status::ready,"concurrent stop cannot be lost round "+std::to_string(round));
        expect(!result.get(),"concurrent stop rejects scan round "+std::to_string(round));
    }
    }
    testParkedStop(expect);
    std::cout<<"{\"pass\":"<<(failed.empty()?"true":"false")<<",\"checks\":"<<checks
      <<",\"minuteBeforeWaits\":"<<minuteBefore.waits<<",\"minuteAfterWaits\":"<<minuteAfter.waits
      <<",\"minuteBeforeScans\":"<<minuteBefore.scans.size()<<",\"minuteAfterScans\":"<<minuteAfter.scans.size()
      <<",\"failed\":[";
    for(size_t i=0;i<failed.size();++i){if(i)std::cout<<',';std::cout<<'"'<<failed[i]<<'"';}
    std::cout<<"],\"realProcessEnumeration\":false,\"systemWindowsClosed\":false,\"realConditionVariables\":true}\n";
    return failed.empty()?0:1;
}
