#pragma once
#include "process_efficiency.h"
// Included only after native trace/lifecycle declarations. Observer I/O stays
// off BUS and UI: startup entry or the already-existing lifecycle worker.
static ymcc::msi::visibility::wake::Policy g_msiVisibilityWakePolicy;
static std::atomic<unsigned long long> g_msiVisibilityWakePending{0};
static std::mutex g_msiVisibilityObserverMx;
#if defined(YMCC_FAULT_INJECT) || defined(YMCC_MSI_FOREGROUND_ISOLATION_TEST)
static std::function<ymcc::msi::visibility::wake::Active()> g_msiVisibilityObserverActiveForTest;
static std::function<bool(const ymcc::msi::visibility::wake::Decision&)> g_msiVisibilityObserverLaunchForTest;
static std::function<void()> g_msiVisibilityObserverMetadataForTest;
#else
struct MsiVisibilityObserverProcess {
    HANDLE process=nullptr;DWORD pid=0;uint64_t generation=0;unsigned episode=0;
    ~MsiVisibilityObserverProcess(){if(process)CloseHandle(process);}
};
static MsiVisibilityObserverProcess g_msiVisibilityObserverProcess;
#endif
static bool msiVisibilityWakeDiagnosticEligible(bool requireReady) {
    if constexpr (!ymcc::msi::visibility::wake::buildEnabled)return false;
    if(inputHostExitFenceRaised()||g_exitRequested.load(std::memory_order_acquire)||
        !msiClawKnownBoardAuthority()||!domainLogEnabled("virtual"))return false;
    return !requireReady||g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready;
}
static ymcc::msi::visibility::wake::Active msiVisibilityObserverActive() {
    using ymcc::msi::visibility::wake::Active;
#if defined(YMCC_FAULT_INJECT) || defined(YMCC_MSI_FOREGROUND_ISOLATION_TEST)
    // Missing hook is unknown, never a fallthrough to process/device APIs.
    return g_msiVisibilityObserverActiveForTest?g_msiVisibilityObserverActiveForTest():Active::Unknown;
#else
    auto& handle=g_msiVisibilityObserverProcess.process;if(!handle)return Active::Absent;
    const DWORD status=WaitForSingleObject(handle,0);
    if(status==WAIT_TIMEOUT)return Active::Running;
    if(status==WAIT_OBJECT_0){DWORD code=0;const bool exitKnown=GetExitCodeProcess(handle,&code)!=FALSE;msiControllerTraceLog("independent-observer-exit",{{"observerPid",g_msiVisibilityObserverProcess.pid},{"wakeGeneration",g_msiVisibilityObserverProcess.generation},{"wakeEpisode",g_msiVisibilityObserverProcess.episode},{"exitCodeKnown",exitKnown},{"exitCode",code},{"scope","process exit only; not controller consumption proof"}},false);CloseHandle(handle);handle=nullptr;return Active::Absent;}
    return Active::Unknown; // never terminate/replace a child with unknown liveness
#endif
}
static bool msiVisibilityObserverRequestCurrent(const ymcc::msi::visibility::wake::Decision& d) {
    if(!msiVisibilityWakeDiagnosticEligible(true)||currentPowerGeneration()!=d.generation)return false;
    const auto now=GetTickCount64();return d.armedTick&&now>=d.armedTick&&now-d.armedTick<ymcc::msi::visibility::wake::pendingMaxAgeMs;
}
static void msiVisibilityObserverLaunch(const ymcc::msi::visibility::wake::Decision& d) {
    using namespace ymcc::msi::visibility::wake;
    if(d.action!=Action::Launch){if(d.log)msiControllerTraceLog("independent-observer-wake-decision",{
        {"action",d.action==Action::Wait?"waiting":"refused"},{"reason",d.reason},{"wakeGeneration",d.generation},
        {"wakeEpisode",d.episode},{"pendingMaxAgeMs",pendingMaxAgeMs},{"modeWriteIssued",false}},false);return;}
    bool started=false,parentIdentityAvailable=false;DWORD error=0,observerPid=0;
#if defined(YMCC_FAULT_INJECT) || defined(YMCC_MSI_FOREGROUND_ISOLATION_TEST)
    const std::wstring executable=L"offline:YeManControllerVisibilityProbe";
    const std::wstring log=L"offline:controller-visibility-evidence";
    if(g_msiVisibilityObserverMetadataForTest)g_msiVisibilityObserverMetadataForTest();
#else
    const std::wstring workingDirectory=exe_dir();
    const std::wstring executable=workingDirectory+L"\\YeManControllerVisibilityProbe.exe";
    const std::wstring log=app_data_dir()+(d.episode?L"\\msi-controller-visibility-wake-"+std::to_wstring(d.episode)+L".log":L"\\msi-controller-visibility.log");
#endif
    // Recheck after metadata resolution, which may block or admit UI/power
    // changes. Refusal consumes only this reader attempt; never queues control.
    if(!msiVisibilityObserverRequestCurrent(d)){msiControllerTraceLog("independent-observer-wake-decision",{{"action","refused"},{"reason","request-stale-after-metadata"},{"wakeGeneration",d.generation},{"wakeEpisode",d.episode},{"modeWriteIssued",false}},false);return;}
#if defined(YMCC_FAULT_INJECT) || defined(YMCC_MSI_FOREGROUND_ISOLATION_TEST)
    // No CreateProcess/GetProcessTimes fallback in the offline build.
    if(g_msiVisibilityObserverLaunchForTest)started=g_msiVisibilityObserverLaunchForTest(d);
    else error=ERROR_NOT_SUPPORTED;
#else
    FILETIME created{},exited{},kernel{},user{};ULARGE_INTEGER stamp{};
    const bool timeOk=GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=FALSE;
    stamp.LowPart=created.dwLowDateTime;stamp.HighPart=created.dwHighDateTime;
    if(!timeOk){error=GetLastError();}
    else {
        parentIdentityAvailable=true;
        const std::wstring phase=d.episode?L"msi-evidence-wake":L"msi-evidence";
        std::wstring command=L"\""+executable+L"\" \""+log+L"\" "+phase+L" "+std::to_wstring(sessionSeconds)+L" "+std::to_wstring(GetCurrentProcessId())+L" "+std::to_wstring(stamp.QuadPart);
        if(d.episode)command+=L" "+std::to_wstring(d.generation)+L" "+std::to_wstring(d.episode);
        command+=ymcc::msi::visibility::observerRawReceiptSuffix();
        command+=msiUsbNameContextCommandSuffix();
        std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(0);
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
        if(!msiVisibilityObserverRequestCurrent(d)){msiControllerTraceLog("independent-observer-wake-decision",{{"action","refused"},{"reason","request-stale-before-create"},{"wakeGeneration",d.generation},{"wakeEpisode",d.episode},{"modeWriteIssued",false}},false);return;}
        started=ymcc::processEfficiency::createProcess(executable.c_str(),buffer.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,workingDirectory.c_str(),&si,&pi)!=FALSE;
        error=started?0:GetLastError();observerPid=started?pi.dwProcessId:0;
        if(started){CloseHandle(pi.hThread);g_msiVisibilityObserverProcess.process=pi.hProcess;g_msiVisibilityObserverProcess.pid=pi.dwProcessId;g_msiVisibilityObserverProcess.generation=d.generation;g_msiVisibilityObserverProcess.episode=d.episode;}
    }
#endif
    msiControllerTraceLog("independent-observer-start",{{"started",started},{"observerPid",observerPid},{"error",error},
        {"executable",W2U(executable)},{"log",W2U(log)},{"durationSec",sessionSeconds},{"autoRestart",false},
        {"reason",d.reason},{"wakeGeneration",d.episode?d.generation:0},{"requestPowerGeneration",d.generation},{"requestCurrentAfterCreate",msiVisibilityObserverRequestCurrent(d)},{"wakeEpisode",d.episode},{"maxWakeAttempts",maxWakeAttempts},{"nativeInputReady",g_inputReady.load(std::memory_order_acquire)},
        {"writesHardware",false},{"addsApplicationExemption",false},{"parentIdentityBound",parentIdentityAvailable},{"creationReceiptOnly",true},
        {"scope","independent reader diagnostic; reads may perturb observation; not Steam consumption or filter-attachment proof"}},false);
}
static void msiVisibilityObserverStartupDiagnostic() noexcept {
    if constexpr (!ymcc::msi::visibility::wake::buildEnabled)return;
    try{if(!msiVisibilityWakeDiagnosticEligible(true))return;
        std::lock_guard<std::mutex> lock(g_msiVisibilityObserverMx);
        if(!g_msiVisibilityWakePolicy.reserveStartup(true))return;
        const auto active=msiVisibilityObserverActive();using namespace ymcc::msi::visibility::wake;
        msiVisibilityObserverLaunch({active==Active::Absent?Action::Launch:Action::Refuse,
            active==Active::Absent?"initial-entry":active==Active::Running?"observer-already-running":"observer-status-unknown",currentPowerGeneration(),0,true,GetTickCount64()});
    }catch(...){/* no target/control permission derives from diagnostics */}
}
// Event receipt only: duplicate automatic/user resume notifications do not
// extend the clock. No process/device API from this UI callback.
static void msiVisibilityObserverNoteResume(unsigned long long generation) noexcept {
    if constexpr (!ymcc::msi::visibility::wake::buildEnabled)return;
    try{if(!generation||!msiVisibilityWakeDiagnosticEligible(false))return;g_msiVisibilityWakePolicy.noteResume(true,generation,GetTickCount64());}catch(...){}
}
static void msiVisibilityObserverWakeReady(unsigned long long generation) noexcept {
    if constexpr (!ymcc::msi::visibility::wake::buildEnabled)return;
    try{if(!generation||generation!=currentPowerGeneration()||!msiVisibilityWakeDiagnosticEligible(true))return;if(g_msiVisibilityWakePolicy.arm(true,generation,GetTickCount64()))
        g_msiVisibilityWakePending.store(generation,std::memory_order_release);
    }catch(...){}
}
// Existing lifecycle worker cadence only; at most 60s of nonblocking child
// liveness polling per real resume, then stop. No hardware reads or new timer.
static void msiVisibilityObserverWakeMaintenance() noexcept {
    if constexpr (!ymcc::msi::visibility::wake::buildEnabled)return;
    msiUsbNameContextRefresh(); // metadata only; no hub or device I/O in this worker
    const auto requested=g_msiVisibilityWakePending.load(std::memory_order_acquire);if(!requested)return;
    try{std::lock_guard<std::mutex> lock(g_msiVisibilityObserverMx);
        const bool eligible=msiVisibilityWakeDiagnosticEligible(true);const auto now=eligible?GetTickCount64():0;
        const auto state=g_msiVisibilityWakePolicy.snapshot();
        const bool current=state.pendingGeneration==requested&&eligible&&currentPowerGeneration()==state.pendingGeneration&&now>=state.armedTick&&now-state.armedTick<ymcc::msi::visibility::wake::pendingMaxAgeMs;
        const auto d=g_msiVisibilityWakePolicy.poll(eligible,currentPowerGeneration(),now,current?msiVisibilityObserverActive():ymcc::msi::visibility::wake::Active::Unknown,requested);
        if(d.action!=ymcc::msi::visibility::wake::Action::Wait){auto expected=requested;g_msiVisibilityWakePending.compare_exchange_strong(expected,0,std::memory_order_acq_rel);}
        msiVisibilityObserverLaunch(d);
    }catch(...){try{g_msiVisibilityWakePolicy.cancel(requested);msiControllerTraceLog("independent-observer-wake-decision",{{"action","refused"},{"reason","diagnostic-exception"},{"wakeGeneration",requested},{"modeWriteIssued",false}},false);}catch(...){}
        auto expected=requested;g_msiVisibilityWakePending.compare_exchange_strong(expected,0,std::memory_order_acq_rel);/* do not affect input */}
}
