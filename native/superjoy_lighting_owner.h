#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

// Native owner for the distinct SuperJoy vendor endpoint. No HID enumeration,
// report, HC factory, controller reader or route admission is performed here.
// Begin requires a current write-allowed context and a separately selected path.
namespace ymcc::superjoy_owner {
inline constexpr std::uint32_t kMagic=0x534A4C31u;
inline constexpr std::uint32_t kVersion=2;
inline constexpr wchar_t kMappingPrefix[]=L"Local\\YeManCC-SuperJoy-LightingOwner-v2-";
inline constexpr wchar_t kRevokedPrefix[]=L"Local\\YeManCC-SuperJoy-LightingRevoked-v2-";
inline constexpr std::size_t kTokenLength=128,kModelLength=96,kPathLength=520;
enum class Phase:std::uint32_t{Idle=0,Admitting=1,Active=2,Retiring=3};
struct HandoffRecord{
    std::uint32_t magic,version;
    std::uint64_t sequence;
    std::uint32_t parentPid,state;
    std::uint64_t parentBirth,generation,topology,admission,heartbeat;
    std::uint32_t childPid,reserved;
    std::uint64_t childBirth;
    char token[kTokenLength],model[kModelLength];
    wchar_t path[kPathLength];
};
static_assert(sizeof(wchar_t)==2&&sizeof(HandoffRecord)==1344);
static_assert(offsetof(HandoffRecord,sequence)==8&&offsetof(HandoffRecord,parentPid)==16&&
    offsetof(HandoffRecord,parentBirth)==24&&offsetof(HandoffRecord,generation)==32&&
    offsetof(HandoffRecord,heartbeat)==56&&offsetof(HandoffRecord,childPid)==64&&offsetof(HandoffRecord,childBirth)==72&&
    offsetof(HandoffRecord,token)==80&&offsetof(HandoffRecord,model)==208&&offsetof(HandoffRecord,path)==304);
inline bool sourceModel(const std::string& model){
    return model=="HandheldCompanion.Devices.AYANEO3"||model=="HandheldCompanion.Devices.AYANEONEXT2"||
        model=="HandheldCompanion.Devices.OneXPlayerApex";
}
inline bool sourcePath(const std::string& model,const std::wstring& path){
    if(!sourceModel(model))return false;
    const std::wstring prefix=model=="HandheldCompanion.Devices.OneXPlayerApex"?
        L"\\\\?\\hid#vid_1a2c&pid_b001":L"\\\\?\\hid#vid_1c4f&pid_0002";
    return path.size()>prefix.size()&&path.size()<kPathLength&&path.find(L'\0')==std::wstring::npos&&
        std::equal(prefix.begin(),prefix.end(),path.begin(),[](wchar_t a,wchar_t b){return std::towlower(a)==std::towlower(b);});
}
inline bool sourcePath(const std::wstring& path){
    // Topology notifications may invalidate either audited endpoint.
    return sourcePath("HandheldCompanion.Devices.AYANEO3",path)||
        sourcePath("HandheldCompanion.Devices.OneXPlayerApex",path);
}
inline bool sourceDescriptor(const std::string& model,std::uint16_t vendorId,std::uint16_t productId,
    std::uint16_t usagePage,std::uint16_t usage,std::uint16_t inputReportByteLength,std::uint16_t outputReportByteLength){
    if(model=="HandheldCompanion.Devices.OneXPlayerApex")
        return vendorId==0x1A2C&&productId==0xB001&&usagePage==0xFF01&&usage==1&&outputReportByteLength==65;
    return sourceModel(model)&&vendorId==0x1C4F&&productId==2&&usagePage==0xFF00&&usage==1&&
        inputReportByteLength==65&&outputReportByteLength==65;
}
inline std::wstring widen(const std::string& text){return std::wstring(text.begin(),text.end());}
inline std::wstring mappingName(DWORD pid,std::uint64_t birth){
    return std::wstring(kMappingPrefix)+std::to_wstring(pid)+L"-"+std::to_wstring(birth);
}
inline std::wstring eventName(std::string token){
    std::replace(token.begin(),token.end(),':','-');return std::wstring(kRevokedPrefix)+widen(token);
}
struct Context{std::uint64_t generation=0,topology=0;bool writesAllowed=false;std::string model;};
struct Admission{std::string token,model;std::wstring path;std::uint64_t generation=0,topology=0,number=0;DWORD parentPid=0;std::uint64_t parentBirth=0;};
struct Observation{Phase phase=Phase::Idle;bool parentOutstanding=false,childRetained=false;std::uint64_t observedReturns=0;std::string token;};
struct WaitResult{bool returned=false,deadlineExpired=false,revoked=false,terminationAttempted=false,terminationSucceeded=false;DWORD exitCode=STILL_ACTIVE,waitError=0,terminationError=0;};
class Owner{
    mutable std::mutex mutex_;
    const DWORD parentPid_=GetCurrentProcessId();
    std::uint64_t parentBirth_=0,counter_=0,returns_=0;
    DWORD childPid_=0;std::uint64_t childBirth_=0;
    Phase phase_=Phase::Idle;
    bool outstanding_=false,waiting_=false;
    Admission admission_;
    HANDLE mapping_=nullptr,event_=nullptr,child_=nullptr;
    HandoffRecord* record_=nullptr;
    bool currentLocked(const Context& context)const{
        return context.writesAllowed&&context.generation==admission_.generation&&context.topology==admission_.topology&&context.model==admission_.model;
    }
    bool ensureMappingLocked(){
        if(record_)return true;
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(HandoffRecord),mappingName(parentPid_,parentBirth_).c_str());
        if(!mapping_)return false;
        if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mapping_);mapping_=nullptr;return false;}
        record_=static_cast<HandoffRecord*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(HandoffRecord)));
        if(!record_){CloseHandle(mapping_);mapping_=nullptr;return false;}
        std::memset(record_,0,sizeof(HandoffRecord));return true;
    }
    void publishLocked(){
        if(!record_)return;
        auto sequence=std::atomic_ref<std::uint64_t>(record_->sequence);
        const auto prior=sequence.load(std::memory_order_acquire);
        const auto writing=prior+1+(prior&1u);
        sequence.store(writing,std::memory_order_release);
        record_->magic=kMagic;record_->version=kVersion;record_->parentPid=parentPid_;record_->state=static_cast<std::uint32_t>(phase_);
        record_->parentBirth=parentBirth_;record_->generation=admission_.generation;record_->topology=admission_.topology;
        record_->admission=admission_.number;record_->heartbeat=GetTickCount64();
        record_->childPid=childPid_;record_->reserved=0;record_->childBirth=childBirth_;
        std::memset(record_->token,0,sizeof(record_->token));std::memset(record_->model,0,sizeof(record_->model));std::memset(record_->path,0,sizeof(record_->path));
        std::copy(admission_.token.begin(),admission_.token.end(),record_->token);
        std::copy(admission_.model.begin(),admission_.model.end(),record_->model);
        std::copy(admission_.path.begin(),admission_.path.end(),record_->path);
        sequence.store(writing+1,std::memory_order_release);
    }
    void retireLocked(){
        if(phase_==Phase::Idle)return;
        phase_=Phase::Retiring;if(event_)SetEvent(event_);publishLocked();
    }
    bool settleLocked(){
        if(outstanding_||waiting_)return false;
        if(child_){
            if(WaitForSingleObject(child_,0)!=WAIT_OBJECT_0)return false;
            CloseHandle(child_);child_=nullptr;childPid_=0;childBirth_=0;++returns_;
        }
        if(phase_==Phase::Idle)return true;
        if(event_){CloseHandle(event_);event_=nullptr;}
        phase_=Phase::Idle;admission_=Admission{};publishLocked();return true;
    }
public:
    Owner(){
        FILETIME creation{},exit{},kernel{},user{};
        if(!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user))throw std::runtime_error("SUPERJOY_PARENT_BIRTH_UNAVAILABLE");
        parentBirth_=(static_cast<std::uint64_t>(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;
    }
    Owner(const Owner&)=delete;Owner&operator=(const Owner&)=delete;
    ~Owner(){
        std::lock_guard<std::mutex> lock(mutex_);retireLocked();
        // Destruction is process teardown, never evidence that a child returned.
        if(child_)CloseHandle(child_);if(event_)CloseHandle(event_);
        if(record_)UnmapViewOfFile(record_);if(mapping_)CloseHandle(mapping_);
    }
    std::optional<Admission> begin(const Context& context,const std::wstring& path){
        std::lock_guard<std::mutex> lock(mutex_);(void)settleLocked();
        if(phase_!=Phase::Idle||outstanding_||child_||!context.writesAllowed||!context.generation||!context.topology||
            !sourceModel(context.model)||!sourcePath(context.model,path)||counter_==(std::numeric_limits<std::uint64_t>::max)())return std::nullopt;
        if(!ensureMappingLocked())return std::nullopt;
        Admission next;next.parentPid=parentPid_;next.parentBirth=parentBirth_;next.generation=context.generation;
        next.topology=context.topology;next.number=++counter_;next.model=context.model;next.path=path;
        next.token=std::to_string(parentPid_)+":"+std::to_string(parentBirth_)+":"+std::to_string(next.generation)+":"+
            std::to_string(next.topology)+":"+std::to_string(next.number);
        if(next.token.size()>=kTokenLength||next.model.size()>=kModelLength)return std::nullopt;
        const auto name=eventName(next.token);event_=CreateEventW(nullptr,TRUE,FALSE,name.c_str());
        if(!event_)return std::nullopt;
        if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(event_);event_=nullptr;return std::nullopt;}
        admission_=std::move(next);phase_=Phase::Admitting;outstanding_=true;publishLocked();return admission_;
    }
    // Transfers the exact suspended child handle, including a revoked request.
    // Caller must not close it afterwards. No duplicate/rights fallback can lose debt.
    bool attachChild(const std::string& token,HANDLE& process,const Context& context){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!outstanding_||token!=admission_.token||child_||!process||process==INVALID_HANDLE_VALUE)return false;
        const auto pid=GetProcessId(process);if(pid==parentPid_)return false;
        child_=process;process=nullptr;
        FILETIME creation{},exit{},kernel{},user{};
        if(!pid||!GetProcessTimes(child_,&creation,&exit,&kernel,&user)){retireLocked();return false;}
        childPid_=pid;childBirth_=(static_cast<std::uint64_t>(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;
        if(phase_!=Phase::Admitting||!currentLocked(context)){retireLocked();return false;}
        phase_=Phase::Active;publishLocked();return true;
    }
    bool cancelBeforeSpawn(const std::string& token){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!outstanding_||child_||token!=admission_.token)return false;
        retireLocked();outstanding_=false;return settleLocked();
    }
    bool current(const std::string& token,const Context& context)const{
        std::lock_guard<std::mutex> lock(mutex_);
        return phase_==Phase::Active&&outstanding_&&child_&&token==admission_.token&&currentLocked(context);
    }
    void refresh(const Context& context){
        std::lock_guard<std::mutex> lock(mutex_);
        if((phase_==Phase::Active||phase_==Phase::Admitting)&&!currentLocked(context))retireLocked();
        if(phase_==Phase::Active&&record_)std::atomic_ref<std::uint64_t>(record_->heartbeat).store(GetTickCount64(),std::memory_order_release);
        if(phase_==Phase::Retiring&&!outstanding_)(void)settleLocked();
    }
    void retire(){std::lock_guard<std::mutex> lock(mutex_);retireLocked();}
    bool finish(const std::string& token){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!outstanding_||waiting_||token!=admission_.token)return false;
        retireLocked();outstanding_=false;return settleLocked();
    }
    bool observeReturn(){std::lock_guard<std::mutex> lock(mutex_);return phase_==Phase::Retiring&&!outstanding_&&settleLocked();}
    Observation observation()const{std::lock_guard<std::mutex> lock(mutex_);return {phase_,outstanding_,child_!=nullptr,returns_,admission_.token};}
    WaitResult waitChild(const std::string& token,DWORD budgetMs,DWORD terminationMs,const std::function<Context()>& context){
        WaitResult result;HANDLE child;
        {std::lock_guard<std::mutex> lock(mutex_);if(!outstanding_||waiting_||token!=admission_.token||!child_){result.waitError=ERROR_INVALID_HANDLE;return result;}child=child_;waiting_=true;}
        struct WaitGuard{Owner& owner;~WaitGuard(){std::lock_guard<std::mutex> lock(owner.mutex_);owner.waiting_=false;}} guard{*this};
        const auto deadline=GetTickCount64()+budgetMs;ULONGLONG retiringDeadline=0;
        for(;;){
            refresh(context());
            {std::lock_guard<std::mutex> lock(mutex_);if(phase_!=Phase::Active){result.revoked=true;if(!retiringDeadline)retiringDeadline=GetTickCount64()+500;}}
            const auto status=WaitForSingleObject(child,0);
            if(status==WAIT_OBJECT_0){result.returned=true;GetExitCodeProcess(child,&result.exitCode);return result;}
            if(status==WAIT_FAILED){result.waitError=GetLastError();retire();return result;}
            const auto now=GetTickCount64();
            if(now>=deadline||(retiringDeadline&&now>=retiringDeadline)){
                result.deadlineExpired=now>=deadline;retire();result.terminationAttempted=true;
                result.terminationSucceeded=TerminateProcess(child,1)!=FALSE;result.terminationError=result.terminationSucceeded?0:GetLastError();
                const auto returned=WaitForSingleObject(child,terminationMs);
                if(returned==WAIT_OBJECT_0){result.returned=true;GetExitCodeProcess(child,&result.exitCode);}
                else if(returned==WAIT_FAILED)result.waitError=GetLastError();
                return result;
            }
            const auto step=std::min<ULONGLONG>(50,deadline-now);
            (void)WaitForSingleObject(child,static_cast<DWORD>(step));
        }
    }
};
} // namespace ymcc::superjoy_owner
