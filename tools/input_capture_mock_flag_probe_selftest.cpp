// Actual extracted product/AI gates only. No product main, devices, registry, or installation reads.
#include <windows.h>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;

static std::wstring POWER_CONTROL_DIR;
static std::wstring expectedFlag;
static std::wstring commandLine;
static std::atomic<ULONGLONG> fakeTick{10000};
static std::atomic<bool> filePresent{false};
static std::atomic<unsigned long long> probes{0}, clocks{0}, logs{0};
static bool realFiles = false, collectRealProbeTicks = false;
static std::mutex receiptMx, blockMx;
static std::condition_variable blockCv;
static bool blockFileCalls = false, releaseFileCalls = false;
static unsigned blockedFileCalls = 0;
static std::vector<ULONGLONG> probeTicks;
static std::vector<json> logRows;

static LPWSTR ProbeCommandLine() { return commandLine.data(); }
static ULONGLONG ProbeTick() {
    clocks.fetch_add(1, std::memory_order_relaxed);
    return realFiles ? ::GetTickCount64() : fakeTick.load(std::memory_order_relaxed);
}
static DWORD ProbeAttributes(LPCWSTR path) {
    if (expectedFlag != path) throw std::runtime_error("Probe escaped the isolated fixture path");
    probes.fetch_add(1, std::memory_order_relaxed);
    if (realFiles) {
        if (collectRealProbeTicks) {
            std::lock_guard<std::mutex> lock(receiptMx);
            probeTicks.push_back(::GetTickCount64());
        }
        return ::GetFileAttributesW(path); // Sole real filesystem read in this C++ fixture.
    }
    const auto present = filePresent.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(receiptMx);
        probeTicks.push_back(fakeTick.load(std::memory_order_relaxed));
    }
    {
        std::unique_lock<std::mutex> lock(blockMx);
        if (blockFileCalls) {
            ++blockedFileCalls;
            blockCv.notify_all();
            if (!blockCv.wait_for(lock, std::chrono::seconds(5), [] { return releaseFileCalls; }))
                throw std::runtime_error("Timed out releasing synthetic file receipt");
        }
    }
    return present ? FILE_ATTRIBUTE_NORMAL : INVALID_FILE_ATTRIBUTES;
}
static void appendNativeLifecycleLog(const char* event, const json& payload) {
    logs.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(receiptMx);
    logRows.push_back({{"event",event},{"payload",payload}});
}
#define GetFileAttributesW ProbeAttributes
#define GetTickCount64 ProbeTick
#define GetCommandLineW ProbeCommandLine
#include "actual-extracted.h"
#undef GetCommandLineW
#undef GetTickCount64
#undef GetFileAttributesW

using Gate = bool (*)();
static std::uint64_t assertions = 0;
static void check(bool value, const char* label) {
    ++assertions;
    if (!value) throw std::runtime_error(label);
}
static bool at(Gate gate, ULONGLONG tick, bool exists) {
    fakeTick.store(tick, std::memory_order_relaxed);
    filePresent.store(exists, std::memory_order_relaxed);
    return gate();
}
static void checkLogPayload() {
    check(!logRows.empty(), "Expected an ignored-flag log");
    const json expected = {{"event","gyro-mock-flag-ignored"}, {"payload",{
        {"reason","user-path-unreachable-by-ruling"},
        {"switch","--gyro-mock-handshake"},{"source","operator-ruling-20260923"}}}};
    for (const auto& row : logRows) check(row == expected, "Ignored event or payload changed");
}
static void simultaneous(Gate gate, unsigned workers) {
    std::barrier start(static_cast<std::ptrdiff_t>(workers));
    std::atomic<unsigned> trueReturns{0};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < workers; ++i) threads.emplace_back([&] {
        start.arrive_and_wait();
        if (gate()) ++trueReturns;
    });
    for (auto& thread : threads) thread.join();
    check(trueReturns == 0, "Product branch enabled mock concurrently");
}
static void waitBlocked(unsigned count) {
    std::unique_lock<std::mutex> lock(blockMx);
    check(blockCv.wait_for(lock, std::chrono::seconds(5), [&] { return blockedFileCalls >= count; }),
        "Expected number of concurrent file calls did not arrive");
}
static void releaseBlocked() {
    std::lock_guard<std::mutex> lock(blockMx);
    releaseFileCalls = true;
    blockCv.notify_all();
}

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 5) throw std::runtime_error("Usage: fixture.exe before|current CASE ISOLATED_ROOT ITERATIONS");
        const bool current = std::wstring(argv[1]) == L"current";
        const std::wstring wideCase = argv[2];
        const std::string testCase(wideCase.begin(), wideCase.end());
        POWER_CONTROL_DIR = argv[3];
        expectedFlag = POWER_CONTROL_DIR + L"\\feature-assets\\gyro-motion\\mock-handshake.flag";
        commandLine = testCase.starts_with("ai-") ? L"fixture --gyro-mock-handshake" : L"fixture";
        const auto iterations = std::stoull(argv[4]);
        Gate gate = current ? current_impl::inputCaptureMockHandshakeFlag : before_impl::inputCaptureMockHandshakeFlag;
        json extra = json::object();
        if (testCase == "product-missing" || testCase == "ai-missing") {
            for (unsigned i=0; i<1250; ++i) check(!at(gate,10000+i*8,false),"Missing flag enabled mock");
            check(probes == ((current || testCase == "ai-missing") ? 10ULL : 1250ULL),"125Hz probe-count negative control failed");
            check(!at(gate,20000,false),"Missing boundary enabled mock");
            check(probes == ((current || testCase == "ai-missing") ? 11ULL : 1251ULL),"1000ms boundary probe count failed");
            check(logs == 0,"Missing flag logged ignored event");
        } else if (testCase == "product-late-flag") {
            check(!at(gate,10000,false),"Initial product result changed");
            check(!at(gate,10001,true),"Flag alone enabled product mock");
            check(!at(gate,10999,true),"Flag alone enabled product mock at deadline-1");
            check(logs == (current ? 0ULL : 1ULL),"Diagnostic-delay negative control failed");
            check(!at(gate,11000,true),"Flag alone enabled product mock at deadline");
            check(logs == 1,"Persistent flag did not log once by 1000ms");
            check(probes == 2,"Appearance query count changed");
            for(unsigned i=0;i<2000;++i) check(!at(gate,11001+i*8,i%2),"Logged product enabled mock");
            check(probes == 2 && logs == 1,"Product kept probing after ignored event");
            checkLogPayload();
            extra["appearanceAtMs"]=10001; extra["loggedAtMs"]=current?11000:10001;
            extra["discoveryDelayMs"]=current?999:0;
        } else if (testCase == "product-present") {
            check(!at(gate,0,true),"First present file enabled product mock");
            for(unsigned i=0;i<1250;++i) check(!at(gate,i*8,i%2),"Product result changed after log");
            check(probes == 1 && logs == 1,"Present-first flag log/probe not once"); checkLogPayload();
        } else if (testCase == "product-transient") {
            check(!at(gate,10000,false),"Initial product result changed");
            check(!at(gate,10001,true),"Transient flag enabled product mock");
            check(!at(gate,10999,false),"Removed transient flag enabled mock");
            check(!at(gate,11000,false),"Missing product boundary enabled mock");
            check(logs == (current?0ULL:1ULL),"Transient diagnostic difference not exposed");
            extra["transientDiagnosticCanBeMissed"]=current;
        } else if (testCase == "product-zero-and-gap") {
            check(!at(gate,0,false),"Zero tick enabled mock");
            check(!at(gate,999,false),"Before deadline enabled mock");
            check(probes == (current?1ULL:2ULL),"Zero-tick negative cache changed");
            check(!at(gate,1000,false),"Exact deadline enabled mock");
            check(!at(gate,86400000,false),"Large forward clock gap enabled mock");
            check(probes == (current?3ULL:4ULL),"Gap probe count changed");
        } else if (testCase == "ai-events") {
            check(!at(gate,10000,false),"AI initial missing result changed");
            check(!at(gate,10001,true),"AI appearance bypassed existing negative cache");
            check(!at(gate,10999,true),"AI deadline-1 appearance changed");
            check(at(gate,11000,true),"AI deadline appearance changed");
            check(at(gate,11001,true),"AI present file no longer rechecked each invocation");
            check(!at(gate,11002,false),"AI removal was not immediate");
            check(!at(gate,11003,true),"AI reappearance bypassed new negative cache");
            check(!at(gate,12001,true),"AI second deadline-1 changed");
            check(at(gate,12002,true),"AI second deadline appearance changed");
            check(probes == 5 && logs == 0,"AI query trace or ignored log changed");
            check(probeTicks == std::vector<ULONGLONG>{10000,11000,11001,11002,12002},"AI exact probe trace changed");
        } else if (testCase == "product-concurrent-missing") {
            for (unsigned phase=0; phase<32; ++phase) {
                fakeTick = 10000+phase*1000; filePresent = false;
                simultaneous(gate,32);
                check(probes == (current?phase+1ULL:phase*64ULL+32ULL),"Concurrent deadline reserved more than one probe");
                fakeTick = 10999+phase*1000;
                simultaneous(gate,32);
                check(probes == (current?phase+1ULL:(phase+1ULL)*64ULL),"Concurrent cache window probed");
            }
            check(logs == 0,"Concurrent missing file logged");
        } else if (testCase == "product-concurrent-present") {
            fakeTick = 10000; filePresent = true; blockFileCalls = true;
            const unsigned workers = 32;
            std::barrier start(static_cast<std::ptrdiff_t>(workers));
            std::atomic<unsigned> trueReturns{0}; std::vector<std::thread> threads;
            for(unsigned i=0;i<workers;++i) threads.emplace_back([&] { start.arrive_and_wait(); if(gate())++trueReturns; });
            waitBlocked(current?1:workers); releaseBlocked();
            for(auto& thread:threads)thread.join();
            check(trueReturns==0,"Concurrent present flag enabled product mock");
            check(probes==(current?1ULL:32ULL) && logs==(current?1ULL:32ULL),"One-time log concurrency negative control failed");
            checkLogPayload();
        } else if (testCase == "product-overlap-expiry") {
            fakeTick=10000; filePresent=true; blockFileCalls=true;
            bool first=true, second=true;
            std::thread a([&]{ first=gate(); }); waitBlocked(1);
            fakeTick=11000;
            std::thread b([&]{ second=gate(); }); waitBlocked(2);
            filePresent=false; releaseBlocked(); a.join(); b.join();
            check(!first && !second,"Delayed product file receipt enabled mock");
            check(probes==2 && logs==(current?1ULL:2ULL),"Overlapping expired-window log not once");
            check(!at(gate,12000,true),"Product post-log result changed");
            check(probes==2,"Probe continued after delayed ignored log"); checkLogPayload();
            extra["scope"]="Two file receipts may overlap if one lasts beyond 1s; only one reserved query per clock window, log only once.";
        } else if (testCase == "real-benchmark") {
            realFiles=true;
            check(!gate(),"Real missing file enabled mock");
            const auto warmProbes=probes.load();
            const auto beforeProbes=probes.load(); const auto beforeClocks=clocks.load();
            const auto started=std::chrono::steady_clock::now();
            std::uint64_t trueReturns=0;
            for(std::uint64_t i=0;i<iterations;++i) trueReturns += gate()?1:0;
            const auto elapsed=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-started).count();
            check(trueReturns==0 && logs==0,"Real benchmark result changed");
            check(warmProbes==1,"Real benchmark warmup did not perform a Win32 query");
            if(!current)check(probes-beforeProbes==iterations,"Before real syscall negative control failed");
            if(current)check(probes-beforeProbes<=static_cast<std::uint64_t>(elapsed/1e9)+1,"Current real query count exceeds 1s cache bound");
            extra={{"iterations",iterations},{"elapsedNs",elapsed},{"nsPerCall",elapsed/iterations},
                {"measuredGetFileAttributesWCalls",probes-beforeProbes},{"measuredGetTickCount64Calls",clocks-beforeClocks},
                {"warmupGetFileAttributesWCalls",warmProbes},{"instrumentation","Both paths include identical atomic file-call counter and path equality guard; timing is function-only, not whole-process CPU."}};
        } else if (testCase == "real-rate") {
            realFiles=true; collectRealProbeTicks=true;
            const auto started=::GetTickCount64(); std::uint64_t invocations=0;
            while(::GetTickCount64()-started<iterations) {
                check(!gate(),"Real rate test enabled product mock"); ++invocations; ::Sleep(8);
            }
            const auto elapsed=::GetTickCount64()-started;
            std::vector<ULONGLONG> gaps;
            for(std::size_t i=1;i<probeTicks.size();++i) {
                gaps.push_back(probeTicks[i]-probeTicks[i-1]);
                if(current)check(gaps.back()>=1000,"Real missing file queries occurred less than 1s apart");
            }
            check(logs==0,"Real rate missing file logged");
            if(!current)check(probes==invocations,"Before rate negative control missed queries");
            if(current)check(probes<=elapsed/1000+1 && probes>=2,"After real rate not bounded or did not expire");
            extra={{"elapsedMs",elapsed},{"busLikeInvocations",invocations},{"getFileAttributesWCalls",probes.load()},
                {"probeGapsMs",gaps},{"queryTicks",probeTicks},{"invocationPeriodMs",8},
                {"startupProbeCountedSeparatelyFromSteadyRate",true}};
        } else throw std::runtime_error("Unknown case");
        std::cout << json({{"pass",true},{"variant",current?"current":"before"},{"case",testCase},
            {"assertions",assertions},{"probes",probes.load()},{"clocks",clocks.load()},{"logs",logs.load()},
            {"probeTicks",probeTicks},{"extra",extra},{"productionMainOrDeviceApiExecuted",false}}).dump() << '\n';
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n'; return 3;
    }
}
