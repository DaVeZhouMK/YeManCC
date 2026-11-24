#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <chrono>
#include <tlhelp32.h>
#include <shlobj.h>
#include <wrl/implements.h>
#include "lossless_scale.h"
#include "process_basic_candidates.h"
#include "sleep_power_revocation.h"
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"uuid.lib")
namespace ymcc::lossless {
struct RuntimeReceipt {
    bool known=false,running=false,readable=false;
    Identity identity;
    std::wstring button,status;
    std::string reason="unobserved";
    std::uint64_t configRevision=0,observedAt=0;
    ymcc::sleeppower::SharedPowerV1 powerScope;
};
inline RuntimeReceipt selectRuntime(const std::vector<Identity>& candidates,bool unreadable){
    RuntimeReceipt r;
    if(unreadable){r.reason="runtime-identity-unavailable";return r;}
    if(candidates.size()>1){r.reason="multiple-lossless-instances";return r;}
    r.known=true;r.reason.clear();if(candidates.empty())return r;
    r.running=true;r.identity=candidates.front();return r;
}
inline void appendRuntimeCandidate(DWORD pid, DWORD session,
    std::vector<Identity>& candidates, bool& unreadable) {
    DWORD peer=0;if(!ProcessIdToSessionId(pid,&peer)){unreadable=true;return;}if(peer!=session)return;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
    if(!process){unreadable=true;return;}
    wchar_t image[32768];DWORD size=32768;FILETIME c{},e{},k{},u{};
    if(WaitForSingleObject(process,0)==WAIT_TIMEOUT&&QueryFullProcessImageNameW(process,0,image,&size)&&GetProcessTimes(process,&c,&e,&k,&u))
        { const std::wstring path(image,size); const auto leaf=path.substr(path.find_last_of(L"\\/")+1);
          DWORD heldSession=0;
          if(_wcsicmp(leaf.c_str(),L"LosslessScaling.exe")||GetProcessId(process)!=pid||
             !ProcessIdToSessionId(pid,&heldSession)||heldSession!=session)unreadable=true;
          else candidates.push_back({pid,(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime,path}); }
    else if(WaitForSingleObject(process,0)==WAIT_TIMEOUT)unreadable=true;
    CloseHandle(process);
}
inline RuntimeReceipt discoverRuntime(){
    DWORD session=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&session))return {};
    std::vector<DWORD> pids;DWORD basicError=ERROR_SUCCESS;
    const auto status=ymcc::processNames::candidates(L"LosslessScaling.exe",pids,basicError,
        ymcc::processNames::resolveBasicQuery(),ymcc::processNames::NameMatch::CrtIgnoreCase);
    if(status!=ymcc::processNames::BasicResult::Complete)return {};
    std::vector<Identity> candidates;bool unreadable=false;
    for(const auto pid:pids)appendRuntimeCandidate(pid,session,candidates,unreadable);
    return selectRuntime(candidates,unreadable);
}
inline RuntimeReceipt inspectRuntime(const std::function<bool()>& current={}){
    if(current&&!current())return {};
    auto r=discoverRuntime();r.observedAt=GetTickCount64();
    if(current&&!current())return {};
    if(!r.known||!r.running)return r;
    bool available=false;auto s=observeScaling(r.identity,available,current);
    if(current&&!current())return {};
    r.readable=available;r.button=s.button;r.status=s.status;
    if(!available)r.reason="scaling-state-unavailable";r.observedAt=GetTickCount64();return r;
}
struct RuntimeSignal {
    HANDLE wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);std::atomic<bool> ended{false};
    ~RuntimeSignal(){if(wake)CloseHandle(wake);}
    void notify(){if(!ended.load()&&wake)SetEvent(wake);}
};
class RuntimePropertyHandler final:public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,IUIAutomationPropertyChangedEventHandler>{
    std::shared_ptr<RuntimeSignal> signal_;
public:
    explicit RuntimePropertyHandler(std::shared_ptr<RuntimeSignal> signal):signal_(std::move(signal)){}
    IFACEMETHODIMP HandlePropertyChangedEvent(IUIAutomationElement*,PROPERTYID,VARIANT)override{signal_->notify();return S_OK;}
};
// One native observer for the whole YMCC process. Never launches, closes,
// invokes, changes focus or writes settings. Consumers only receive receipts.
struct RuntimePowerAccess {
    ymcc::sleeppower::Reader read=nullptr;
    ymcc::sleeppower::RevocationMirrors* revocations=nullptr;
};
class RuntimeMonitor {
    std::mutex mutex_,stopMutex_;std::thread worker_;std::shared_ptr<RuntimeSignal> signal_=std::make_shared<RuntimeSignal>();
    std::function<RuntimeReceipt()> discover_;
    bool active_=false,detailed_=false,receiptValid_=false;std::uint64_t requestRevision_=0;
    RuntimePowerAccess power_;ymcc::sleeppower::SharedPowerV1 notifiedPower_;bool notifiedKnown_=false;
    RuntimeReceipt receipt_;std::function<void(const RuntimeReceipt&)> emit_;
    static bool same(const RuntimeReceipt& a,const RuntimeReceipt& b){return a.known==b.known&&a.running==b.running&&a.readable==b.readable&&a.identity.pid==b.identity.pid&&a.identity.created==b.identity.created&&a.identity.path==b.identity.path&&a.button==b.button&&a.status==b.status&&a.reason==b.reason&&a.configRevision==b.configRevision;}
    bool startLocked() noexcept {
        if(!signal_->wake||signal_->ended.load())return false;
        if(active_&&!worker_.joinable())try{worker_=std::thread([this]{run();});}catch(...){return false;}
        signal_->notify();return true;
    }
    void run(){
        using namespace ymcc::sleeppower;
        HANDLE process=nullptr,changes=INVALID_HANDLE_VALUE;std::unique_ptr<NoFocusAutomation> automation;std::unique_ptr<ScaleProcess> heldProcess;
        Microsoft::WRL::ComPtr<RuntimePropertyHandler> handler;
        Identity bound;HWND window=nullptr;bool events=false;std::uint64_t configRevision=0,lastRead=0,lastDiscovery=0;RuntimeReceipt discovered;
        std::wstring configFolder;
        auto release=[&]{if(automation)automation->unlisten();handler.Reset();automation.reset();heldProcess.reset();if(process)CloseHandle(process);process=nullptr;bound={};window=nullptr;events=false;};
        RevocationMirrors::Registration cancellation;SharedPowerV1 captured;std::uint64_t scopeRevision=0;
        auto releaseScope=[&]{
            release();if(changes!=INVALID_HANDLE_VALUE)FindCloseChangeNotification(changes);changes=INVALID_HANDLE_VALUE;
            // The borrowed cancellation HANDLE outlives all observer resources.
            cancellation.reset();scopeRevision=0;discovered={};lastRead=lastDiscovery=0;
            std::lock_guard<std::mutex> lock(mutex_);receiptValid_=false;
        };
        struct ReturnResources {decltype(releaseScope)& release;~ReturnResources(){release();}} returned{releaseScope};
        while(!signal_->ended.load()){
          try{
            bool active=false,detailed=false;std::uint64_t revision=0;RuntimePowerAccess access;
            {std::lock_guard<std::mutex> lock(mutex_);active=active_;detailed=detailed_;revision=requestRevision_;access=power_;}
            if(!active){releaseScope();if(!signal_->ended.load())WaitForSingleObject(signal_->wake,INFINITE);continue;}
            SharedPowerV1 now;
            if(!access.revocations||!capturePower(access.read,now)){
                releaseScope();if(!signal_->ended.load())WaitForSingleObject(signal_->wake,2000);continue;
            }
            if(scopeRevision!=revision||!cancellation||cancellation.cancelled()||!samePowerRevocation(captured,now)){
                releaseScope();captured=now;cancellation=access.revocations->observe(access.read,captured);
                if(!cancellation){if(!signal_->ended.load())WaitForSingleObject(signal_->wake,2000);continue;}
                scopeRevision=revision;
            }
            const auto current=[&]{
                {std::lock_guard<std::mutex> lock(mutex_);if(!active_||requestRevision_!=revision||signal_->ended.load())return false;}
                return !cancellation.cancelled()&&powerCurrent(access.read,captured);
            };
            const auto pause=[&](DWORD delay){HANDLE handles[]={signal_->wake,cancellation.event()};return WaitForMultipleObjects(2,handles,FALSE,delay);};
            if(!current()){releaseScope();continue;}
            // Event storms are coalesced to at most two full reads per second.
            while(lastRead&&current()){const auto elapsed=GetTickCount64()-lastRead;if(elapsed>=500)break;pause(static_cast<DWORD>(500-elapsed));}
            if(!current()){releaseScope();continue;}
            if(!process||WaitForSingleObject(process,0)!=WAIT_TIMEOUT||GetTickCount64()-lastDiscovery>=2000){discovered=discover_();lastDiscovery=GetTickCount64();}
            if(!current()){releaseScope();continue;}
            auto r=discovered;r.configRevision=configRevision;
            if(configFolder.empty()){
                struct LocalFolder {PWSTR value=nullptr;~LocalFolder(){CoTaskMemFree(value);}} local;
                if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local.value)))configFolder=std::wstring(local.value)+L"\\Lossless Scaling";
            }
            if(!current()){releaseScope();continue;}
            if(changes==INVALID_HANDLE_VALUE&&!configFolder.empty())changes=FindFirstChangeNotificationW(configFolder.c_str(),FALSE,FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_FILE_NAME);
            if(!current()){releaseScope();continue;}
            if(!r.running||!r.known||r.identity.pid!=bound.pid||r.identity.created!=bound.created||r.identity.path!=bound.path){
                release();
                if(r.known&&r.running){
                    bound=r.identity;process=OpenProcess(SYNCHRONIZE,FALSE,bound.pid);

                }
            }
            if(!current()){releaseScope();continue;}
            // A transient client failure must not pin this same live instance
            // to an unreadable observer forever. Reuse the original read cadence.
            if(!automation&&r.known&&r.running&&current()){
                automation=std::make_unique<NoFocusAutomation>();
                if(!current()||!automation->begin())automation.reset();
            }
            if(!current()){releaseScope();continue;}
            heldProcess.reset();if(r.running)heldProcess=std::make_unique<ScaleProcess>();
            const bool identityValid=!r.running||heldProcess->open(r.identity);
            if(!current()){releaseScope();continue;}
            if(r.known&&r.running&&automation&&identityValid){
                const auto main=scaleMainWindow(bound.pid);
                if(!current()){releaseScope();continue;}
                if(main!=window||!events){automation->unlisten();window=main;handler=Microsoft::WRL::Make<RuntimePropertyHandler>(signal_);events=window&&automation->listen(window,handler.Get());}
                if(!current()){releaseScope();continue;}
                const auto s=window?automation->observe(window,L"",current):ScaleSnapshot{};
                r.readable=s.readable;r.button=s.button;r.status=s.status;
            }
            if(!current()){releaseScope();continue;}
            if(r.running&&(!identityValid||!heldProcess->alive())){r.known=false;r.readable=false;r.reason="runtime-identity-unavailable";}
            if(r.running&&r.known&&!r.readable)r.reason="scaling-state-unavailable";
            lastRead=GetTickCount64();r.observedAt=lastRead;r.powerScope=captured;
            std::function<void(const RuntimeReceipt&)> emit;
            {std::lock_guard<std::mutex> lock(mutex_);
                if(!active_||requestRevision_!=revision||signal_->ended.load()||cancellation.cancelled()||!powerCurrent(access.read,captured))continue;
                if(!receiptValid_||!same(r,receipt_)){receipt_=r;receiptValid_=true;emit=emit_;}
            }
            // No application callback runs under the observer or registry lock.
            // An in-progress callback may finish; stop joins it before return.
            if(emit&&current())emit(r);
            if(!current()){releaseScope();continue;}
            HANDLE handles[4]={signal_->wake,cancellation.event()};DWORD count=2,processIndex=MAXDWORD,changeIndex=MAXDWORD;
            if(process){processIndex=count;handles[count++]=process;}
            if(changes!=INVALID_HANDLE_VALUE){changeIndex=count;handles[count++]=changes;}
            const DWORD interval=!r.running||!r.known?2000:detailed?3000:events&&changes!=INVALID_HANDLE_VALUE?INFINITE:5000;
            const auto wait=WaitForMultipleObjects(count,handles,FALSE,interval);
            if(!current()){releaseScope();continue;}
            if(wait==WAIT_FAILED){WaitForSingleObject(signal_->wake,2000);release();}
            if(changeIndex!=MAXDWORD&&wait==WAIT_OBJECT_0+changeIndex){++configRevision;if(!FindNextChangeNotification(changes)){FindCloseChangeNotification(changes);changes=INVALID_HANDLE_VALUE;}}
            if(processIndex!=MAXDWORD&&wait==WAIT_OBJECT_0+processIndex)release();
          }catch(...){
            // Keep the original desired observer alive after read/allocation or
            // subscriber failure. The existing discovery delay bounds retries.
            releaseScope();if(!signal_->ended.load())WaitForSingleObject(signal_->wake,2000);
          }
        }
    }
public:
    explicit RuntimeMonitor(std::function<RuntimeReceipt()> discover=discoverRuntime,RuntimePowerAccess power={}):discover_(std::move(discover)),power_(power){}
    ~RuntimeMonitor(){stop();}
    bool configure(bool active,bool detailed,std::function<void(const RuntimeReceipt&)> emit,RuntimePowerAccess power={}){
        std::lock_guard<std::mutex> lock(mutex_);if(signal_->ended.load())return false;
        if(power.read||power.revocations){if(power.read!=power_.read||power.revocations!=power_.revocations)++requestRevision_;power_=power;}
        if(active!=active_||detailed!=detailed_)++requestRevision_;
        active_=active;detailed_=detailed;emit_=std::move(emit);
        return startLocked();
    }
    // The original common publication wakes this existing worker. No new poller
    // or consumer power state; a diagnostic publication retains the same scope.
    void powerChanged(){
        std::lock_guard<std::mutex> lock(mutex_);ymcc::sleeppower::SharedPowerV1 now;
        const bool known=ymcc::sleeppower::capturePowerObservation(power_.read,now);
        const bool changed=known!=notifiedKnown_||(known&&!ymcc::sleeppower::samePowerObservation(notifiedPower_,now));
        notifiedKnown_=known;notifiedPower_=now;
        if(changed||(active_&&!worker_.joinable()))(void)startLocked();
    }
    void stop(){
        // Serialize callers through actual return, and retire the worker under
        // the same lock that creates it. Worker cleanup needs mutex_ to return.
        std::lock_guard<std::mutex> serial(stopMutex_);std::thread worker;
        {std::lock_guard<std::mutex> lock(mutex_);signal_->ended=true;if(signal_->wake)SetEvent(signal_->wake);worker=std::move(worker_);}
        if(worker.joinable())worker.join();
    }
};
inline RuntimeMonitor& runtimeMonitor(){static RuntimeMonitor monitor;return monitor;}
} // namespace ymcc::lossless
