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
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"uuid.lib")
namespace ymcc::lossless {
struct RuntimeReceipt {
    bool known=false,running=false,readable=false;
    Identity identity;
    std::wstring button,status;
    std::string reason="unobserved";
    std::uint64_t configRevision=0,observedAt=0;
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
inline RuntimeReceipt inspectRuntime(){
    auto r=discoverRuntime();r.observedAt=GetTickCount64();if(!r.known||!r.running)return r;
    bool available=false;auto s=observeScaling(r.identity,available);r.readable=available;r.button=s.button;r.status=s.status;
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
class RuntimeMonitor {
    std::mutex mutex_;std::thread worker_;std::shared_ptr<RuntimeSignal> signal_=std::make_shared<RuntimeSignal>();
    std::function<RuntimeReceipt()> discover_;
    bool active_=false,detailed_=false;RuntimeReceipt receipt_;std::function<void(const RuntimeReceipt&)> emit_;
    static bool same(const RuntimeReceipt& a,const RuntimeReceipt& b){return a.known==b.known&&a.running==b.running&&a.readable==b.readable&&a.identity.pid==b.identity.pid&&a.identity.created==b.identity.created&&a.identity.path==b.identity.path&&a.button==b.button&&a.status==b.status&&a.reason==b.reason&&a.configRevision==b.configRevision;}
    void publish(RuntimeReceipt r){std::function<void(const RuntimeReceipt&)> emit;{std::lock_guard<std::mutex> lock(mutex_);if(!active_||signal_->ended.load())return;if(same(r,receipt_))return;receipt_=r;emit=emit_;}if(emit)emit(r);}
    void run(){
        HANDLE process=nullptr,changes=INVALID_HANDLE_VALUE;std::unique_ptr<NoFocusAutomation> automation;
        Microsoft::WRL::ComPtr<RuntimePropertyHandler> handler;
        Identity bound;HWND window=nullptr;bool events=false;std::uint64_t configRevision=0,lastRead=0,lastDiscovery=0;RuntimeReceipt discovered;
        std::wstring configFolder;
        auto release=[&]{if(automation)automation->unlisten();handler.Reset();automation.reset();if(process)CloseHandle(process);process=nullptr;bound={};window=nullptr;events=false;};
        PWSTR local=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))){configFolder=std::wstring(local)+L"\\Lossless Scaling";CoTaskMemFree(local);changes=FindFirstChangeNotificationW(configFolder.c_str(),FALSE,FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_FILE_NAME);}
        DWORD wait=0;
        while(!signal_->ended.load()){
            bool active=false,detailed=false;{std::lock_guard<std::mutex> lock(mutex_);active=active_;detailed=detailed_;}
            if(!active){release();WaitForSingleObject(signal_->wake,INFINITE);continue;}
            // Event storms are coalesced to at most two full reads per second.
            const auto elapsed=GetTickCount64()-lastRead;
            while(lastRead&&!signal_->ended.load()){const auto elapsed=GetTickCount64()-lastRead;if(elapsed>=500)break;WaitForSingleObject(signal_->wake,static_cast<DWORD>(500-elapsed));}
            if(signal_->ended.load())break;
            if(!process||WaitForSingleObject(process,0)!=WAIT_TIMEOUT||GetTickCount64()-lastDiscovery>=2000){discovered=discover_();lastDiscovery=GetTickCount64();}
            auto r=discovered;r.configRevision=configRevision;
            if(changes==INVALID_HANDLE_VALUE&&!configFolder.empty())changes=FindFirstChangeNotificationW(configFolder.c_str(),FALSE,FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_FILE_NAME);
            if(!r.running||!r.known||r.identity.pid!=bound.pid||r.identity.created!=bound.created||r.identity.path!=bound.path){
                release();
                if(r.known&&r.running){
                    bound=r.identity;process=OpenProcess(SYNCHRONIZE,FALSE,bound.pid);
                    automation=std::make_unique<NoFocusAutomation>();if(!automation->begin())automation.reset();
                }
            }
            ScaleProcess held;const bool identityValid=!r.running||held.open(r.identity);
            if(r.known&&r.running&&automation&&identityValid){
                const auto main=scaleMainWindow(bound.pid);
                if(main!=window||!events){automation->unlisten();window=main;handler=Microsoft::WRL::Make<RuntimePropertyHandler>(signal_);events=window&&automation->listen(window,handler.Get());}
                const auto s=window?automation->observe(window,L""):ScaleSnapshot{};
                r.readable=s.readable;r.button=s.button;r.status=s.status;
            }
            if(r.running&&(!identityValid||!held.alive())){r.known=false;r.readable=false;r.reason="runtime-identity-unavailable";}
            if(r.running&&r.known&&!r.readable)r.reason="scaling-state-unavailable";
            lastRead=GetTickCount64();r.observedAt=lastRead;publish(r);
            HANDLE handles[3]={signal_->wake};DWORD count=1,processIndex=MAXDWORD,changeIndex=MAXDWORD;
            if(process){processIndex=count;handles[count++]=process;}
            if(changes!=INVALID_HANDLE_VALUE){changeIndex=count;handles[count++]=changes;}
            const DWORD interval=!r.running||!r.known?2000:detailed?3000:events&&changes!=INVALID_HANDLE_VALUE?INFINITE:5000;
            wait=WaitForMultipleObjects(count,handles,FALSE,interval);
            if(wait==WAIT_FAILED){WaitForSingleObject(signal_->wake,2000);release();}
            if(changeIndex!=MAXDWORD&&wait==WAIT_OBJECT_0+changeIndex){++configRevision;if(!FindNextChangeNotification(changes)){FindCloseChangeNotification(changes);changes=INVALID_HANDLE_VALUE;}}
            if(processIndex!=MAXDWORD&&wait==WAIT_OBJECT_0+processIndex)release();
        }
        release();if(changes!=INVALID_HANDLE_VALUE)FindCloseChangeNotification(changes);
    }
public:
    explicit RuntimeMonitor(std::function<RuntimeReceipt()> discover=discoverRuntime):discover_(std::move(discover)){}
    ~RuntimeMonitor(){stop();}
    void configure(bool active,bool detailed,std::function<void(const RuntimeReceipt&)> emit){
        std::lock_guard<std::mutex> lock(mutex_);if(signal_->ended.load())return;
        active_=active;detailed_=detailed;emit_=std::move(emit);
        if(active&&!worker_.joinable())worker_=std::thread([this]{run();});signal_->notify();
    }
    void stop(){signal_->ended=true;if(signal_->wake)SetEvent(signal_->wake);if(worker_.joinable())worker_.join();}
};
inline RuntimeMonitor& runtimeMonitor(){static RuntimeMonitor monitor;return monitor;}
} // namespace ymcc::lossless
