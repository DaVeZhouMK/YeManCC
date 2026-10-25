// Runs extracted production worker bodies against fake process/window APIs.
// The separate ScanWait tests use real condition variables and synthetic threads only.
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include "auto_close_wait.h"
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
int main() {
    unsigned checks=0;std::vector<std::string> failed;
    const auto expect=[&](bool pass,const std::string& name){++checks;if(!pass)failed.push_back(name);};
    Observation minuteBefore,minuteAfter;
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
    {
        std::atomic<bool> stopped{false};ymcc::autoclose::ScanWait wait(stopped);
        std::promise<void> entered;
        auto result=std::async(std::launch::async,[&]{entered.set_value();return wait.waitForNextScan(std::chrono::milliseconds(5000));});
        entered.get_future().get();
        // Short fixture-only delay ensures cancellation also covers a parked waiter.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        wait.requestStop();
        expect(result.wait_for(std::chrono::milliseconds(1000))==std::future_status::ready&&!result.get(),"parked real 5s waiter exits without waiting for deadline");
    }
    std::cout<<"{\"pass\":"<<(failed.empty()?"true":"false")<<",\"checks\":"<<checks
      <<",\"minuteBeforeWaits\":"<<minuteBefore.waits<<",\"minuteAfterWaits\":"<<minuteAfter.waits
      <<",\"minuteBeforeScans\":"<<minuteBefore.scans.size()<<",\"minuteAfterScans\":"<<minuteAfter.scans.size()
      <<",\"failed\":[";
    for(size_t i=0;i<failed.size();++i){if(i)std::cout<<',';std::cout<<'"'<<failed[i]<<'"';}
    std::cout<<"],\"realProcessEnumeration\":false,\"systemWindowsClosed\":false,\"realConditionVariables\":true}\n";
    return failed.empty()?0:1;
}
