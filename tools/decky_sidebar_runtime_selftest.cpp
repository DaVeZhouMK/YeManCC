// Production runtime header, preflight permanently blocks any external process.
#include "../native/decky_sidebar_runtime.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <stdexcept>
using ymcc::deckysidebar::Runtime;
static int checks = 0;
static void check(bool value, const char* label) { if (!value) throw std::runtime_error(label); ++checks; }
int main() {
    Runtime runtime;
    std::mutex mutex; std::condition_variable changed;
    std::atomic<int> attempts{0};
    runtime.initialize({L"NEVER-LAUNCH.exe", L"NEVER-CREATE-DIRECTORY", [&] { ++attempts; return std::string("test-no-launch"); },
        [&](const auto&) { changed.notify_all(); }});
    auto awaitPhase = [&](const char* phase) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(2), [&] { return runtime.snapshot().phase == phase; });
    };
    check(!runtime.snapshot().enabled, "default must be disabled");
    runtime.observeSteam(1); check(attempts == 0, "Steam cannot start disabled loader");
    runtime.observeSteam(0); check(runtime.setEnabled(true), "enable worker");
    check(awaitPhase("waiting-steam"), "wait for Steam, no polling"); check(attempts == 0, "no preflight when Steam absent");
    runtime.observeSteam(1); check(awaitPhase("unavailable"), "explicit source preflight blocks startup");
    check(attempts == 1, "one startup attempt"); check(runtime.snapshot().pid == 0, "no external process started");
    runtime.observeSteam(1); check(attempts == 1, "duplicate Steam state no retry");
    runtime.observeSteam(0); check(awaitPhase("waiting-steam"), "Steam exit releases linked runtime");
    runtime.observeSteam(1); check(awaitPhase("unavailable"), "genuine next session retries once"); check(attempts == 2, "bounded session retry");
    runtime.setEnabled(false); check(runtime.snapshot().phase == "disabled", "disable stops native worker synchronously");
    const int before = attempts; runtime.observeSteam(1, true); check(attempts == before, "new Steam session while off does not preflight");
    runtime.setEnabled(true); check(awaitPhase("unavailable"), "explicit re-enable works"); runtime.shutdown();
    check(runtime.snapshot().pid == 0, "shutdown no owned PID remains");
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    {
        Runtime waiting;
        std::atomic<int> beginnings{0},cancellations{0};
        waiting.initialize({L"NEVER-LAUNCH.exe",L"NO-WRITE",
            [&]{SetEvent(entered);const auto canceled=WaitForSingleObject(cancel,2500);return canceled==WAIT_OBJECT_0?std::string("mirror-bootstrap-canceled"):std::string("test-no-launch");},
            [](const auto&){},[]{return std::string("NO-REBIND");},
            [&]{++beginnings;ResetEvent(cancel);},[&]{++cancellations;SetEvent(cancel);}});
        waiting.observeSteam(1);waiting.setEnabled(true);
        check(WaitForSingleObject(entered,1000)==WAIT_OBJECT_0,"preflight actually entered its cancellation wait");
        const auto started=GetTickCount64();waiting.setEnabled(false);
        check(GetTickCount64()-started<1000,"off cancels pending bootstrap before joining worker");
        check(beginnings==1&&cancellations>=1,"one bootstrap operation token and explicit cancel callback");
        check(waiting.snapshot().phase=="disabled"&&waiting.snapshot().pid==0,"canceled preflight cannot launch external process");
        ResetEvent(entered);waiting.setEnabled(true);
        check(WaitForSingleObject(entered,1000)==WAIT_OBJECT_0,"explicit enable gets fresh cancellation operation");
        waiting.observeSteam(0);
        const auto until=GetTickCount64()+1000;while(waiting.snapshot().phase!="waiting-steam"&&GetTickCount64()<until)Sleep(5);
        check(waiting.snapshot().phase=="waiting-steam","Steam exit cancels pending binding and returns to wait state");
        waiting.shutdown();
    }
    // Hold cancellation while the previous operation finishes. A replacement intent
    // must not renew its token until its predecessor's cancellation has completed.
    HANDLE oldEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr),oldRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE cancelEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr),cancelRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE newBeginning=CreateEventW(nullptr,TRUE,FALSE,nullptr),newPreflight=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    {
        Runtime handoff;
        std::atomic<int> starts{0},reads{0};
        std::atomic<bool> gate{false},tokenCanceled{false},freshToken{false};
        handoff.initialize({L"NEVER-LAUNCH.exe",L"NO-WRITE",
            [&]{if(++reads==1){SetEvent(oldEntered);WaitForSingleObject(oldRelease,2000);}else{freshToken=!tokenCanceled.load();SetEvent(newPreflight);}return std::string("test-no-launch");},
            [](const auto&){},[]{return std::string("NO-REBIND");},
            [&]{tokenCanceled=false;if(++starts==2)SetEvent(newBeginning);},
            [&]{if(gate){SetEvent(cancelEntered);WaitForSingleObject(cancelRelease,2000);}tokenCanceled=true;}});
        handoff.observeSteam(1);handoff.setEnabled(true);
        check(WaitForSingleObject(oldEntered,1000)==WAIT_OBJECT_0,"handoff fixture entered predecessor operation");
        gate=true;
        std::thread observer([&]{handoff.observeSteam(1,true);});
        const bool cancellationEntered=WaitForSingleObject(cancelEntered,1000)==WAIT_OBJECT_0;
        SetEvent(oldRelease);
        const bool serialized=WaitForSingleObject(newBeginning,200)==WAIT_TIMEOUT;
        SetEvent(cancelRelease);observer.join();gate=false;
        const bool replacementRan=WaitForSingleObject(newPreflight,1000)==WAIT_OBJECT_0;
        handoff.shutdown();
        check(cancellationEntered,"handoff cancellation actually gated");
        check(serialized,"replacement token cannot renew during predecessor cancellation");
        check(replacementRan&&freshToken,"replacement operation has a fresh uncanceled token");
        check(starts==2&&handoff.snapshot().pid==0,"serialized handoff performs no extra attempts or external launch");
    }
    for(HANDLE event:{oldEntered,oldRelease,cancelEntered,cancelRelease,newBeginning,newPreflight})CloseHandle(event);
    CloseHandle(entered);CloseHandle(cancel);
    std::cout << "YMCC Decky sidebar native no-launch tests passed: " << checks << "\n";
}

