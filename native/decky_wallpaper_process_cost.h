#pragma once
// Read-only counters on the existing opt-in diagnostic 1Hz lane. No timers,
// enumeration, control handles, settings changes or media requests here.
#include <windows.h>
#include <array>
#include <mutex>
#include <string>
#include <algorithm>
#include <cwctype>
#include "native_process_cost.h"
namespace ymcc::deckycost {
using Json=nlohmann::json;
struct Target {
    DWORD pid=0; ULONGLONG birth=0;
    std::wstring expectedImage;
    bool cefSubtree=false;
    const char* role="unknown";
    const char* attribution="unknown";
};
inline std::wstring normalized(std::wstring value){std::replace(value.begin(),value.end(),L'/',L'\\');for(auto& c:value)c=static_cast<wchar_t>(towlower(c));return value;}
inline ULONGLONG ticks(const FILETIME& t){return (ULONGLONG(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
struct CachedContext {
    DWORD sharedPid=0,steamPid=0;ULONGLONG sharedBirth=0,steamBirth=0,identityTick=0;
    bool peerKnown=false,connected=false,visibleKnown=false,requestedVisible=false,enabledKnown=false,enabled=false,frameQueued=false;
    std::uint64_t seq=0,changeTick=0;std::string kind="unknown";
};
class ContextCache {
    std::mutex mx_;CachedContext value_;
public:
    CachedContext snapshot(bool& available){std::unique_lock lock(mx_,std::try_to_lock);available=lock.owns_lock();return available?value_:CachedContext{};}
    void binding(const Json& detail,DWORD steamPid,ULONGLONG steamBirth,ULONGLONG now) noexcept {
        try{std::unique_lock lock(mx_,std::try_to_lock);if(!lock)return;
            const auto stage=detail.value("bootstrapStage",std::string{});
            if(stage=="failed"||stage=="terminal"){value_.sharedPid=0;value_.sharedBirth=0;return;}
            if(stage!="injected"&&stage!="context-attached")return;
            const auto p=detail.find("contextPid"),b=detail.find("contextCreated");
            if(p==detail.end()||b==detail.end()||!p->is_number_unsigned()||!b->is_number_unsigned())return;
            const auto pid=p->get<std::uint64_t>(),birth=b->get<std::uint64_t>();if(!pid||pid>MAXDWORD||!birth)return;
            value_.sharedPid=DWORD(pid);value_.sharedBirth=birth;value_.steamPid=steamPid;value_.steamBirth=steamBirth;value_.identityTick=now;
        }catch(...){}
    }
    void peer(bool connected,ULONGLONG now) noexcept {try{std::unique_lock lock(mx_,std::try_to_lock);if(!lock)return;value_.peerKnown=true;value_.connected=connected;value_.enabledKnown=false;value_.enabled=false;value_.visibleKnown=false;value_.requestedVisible=false;value_.frameQueued=false;value_.seq=0;value_.kind="unknown";value_.changeTick=now;}catch(...){}}
    void request(const Json& request,ULONGLONG now) noexcept {
        try{std::unique_lock lock(mx_,std::try_to_lock);if(!lock||!value_.connected)return;
            if(request.value("command",std::string{})!="wallpaper.context")return;
            const auto s=request.find("contextSeq"),v=request.find("visible");
            if(s==request.end()||!s->is_number_unsigned()||s->get<std::uint64_t>()>9007199254740991ull||v==request.end()||!v->is_boolean())return;
            const auto seq=s->get<std::uint64_t>();if(seq<=value_.seq)return;
            value_.seq=seq;value_.visibleKnown=true;value_.requestedVisible=v->get<bool>();value_.frameQueued=false;value_.kind="unknown";value_.changeTick=now;
        }catch(...){}
    }
    void reply(const Json& message,ULONGLONG now) noexcept {
        try{std::unique_lock lock(mx_,std::try_to_lock);if(!lock||!value_.connected)return;
            const auto type=message.value("type",std::string{});if(type!="wallpaper"&&type!="wallpaper-ready")return;
            const auto enabled=message.find("enabled");if(enabled!=message.end()&&enabled->is_boolean()){value_.enabledKnown=true;value_.enabled=enabled->get<bool>();if(!value_.enabled)value_.frameQueued=false;}
            const auto seq=message.find("contextSeq");
            if(type!="wallpaper"||seq==message.end()||!seq->is_number_unsigned()||seq->get<std::uint64_t>()!=value_.seq)return;
            const auto e=message.find("entry");value_.frameQueued=e!=message.end()&&e->is_object();value_.kind="unknown";
            if(value_.frameQueued){const auto kind=e->value("kind",std::string{});if(kind=="video"||kind=="image")value_.kind=kind;}
            value_.changeTick=now;
        }catch(...){}
    }
};
class Sampler {
    struct Slot {DWORD pid=0;ULONGLONG expectedBirth=0;HANDLE process=nullptr;std::wstring image;bool cefSubtree=false;nativeCost::Window window;bool used=false;};
    std::array<Slot,12> slots_{};ULONGLONG generation_=0;bool haveGeneration_=false;
    static void close(Slot& s) noexcept {if(s.process)CloseHandle(s.process);s.process=nullptr;s.pid=0;s.expectedBirth=0;s.image.clear();s.window.reset();}
public:
    ~Sampler(){reset();}
    bool active() const noexcept {return haveGeneration_;}
    void reset() noexcept {if(!haveGeneration_)return;for(auto& s:slots_)close(s);haveGeneration_=false;}
    Json sample(bool enabled,ULONGLONG now,ULONGLONG generation,unsigned logicalProcessors,const std::array<Target,12>& targets,std::size_t count) {
        if(!enabled){reset();return nullptr;}
        if(!haveGeneration_||generation_!=generation){reset();generation_=generation;haveGeneration_=true;}
        for(auto& s:slots_)s.used=false;
        Json rows=Json::array();count=(std::min)(count,targets.size());
        for(std::size_t n=0;n<count;++n){const auto& target=targets[n];
            Json row={{"role",target.role},{"attribution",target.attribution},{"pid",target.pid},{"cpuRateAvailable",false}};
            if(!target.pid||target.expectedImage.empty()){row["cpuStatus"]="identity-unavailable";rows.push_back(std::move(row));continue;}
            bool shared=false;
            for(std::size_t prior=0;prior<rows.size();++prior)if(rows[prior].value("pid",DWORD{0})==target.pid&&rows[prior].value("identityVerified",false)&&
                targets[prior].cefSubtree==target.cefSubtree&&normalized(targets[prior].expectedImage)==normalized(target.expectedImage)&&
                (!target.birth||rows[prior].value("processCreation100ns",ULONGLONG{0})==target.birth)){
                const auto previousRole=rows[prior].value("role",std::string{});row=rows[prior];row["role"]=target.role;row["attribution"]=target.attribution;row["sameProcessAsRole"]=previousRole;rows.push_back(std::move(row));shared=true;break;
            }
            if(shared)continue;
            const auto expected=normalized(target.expectedImage);Slot* slot=nullptr;
            for(auto& s:slots_)if(!s.used&&s.pid==target.pid&&s.expectedBirth==target.birth&&s.image==expected&&s.cefSubtree==target.cefSubtree){slot=&s;break;}
            if(!slot)for(auto& s:slots_)if(!s.used){close(s);slot=&s;slot->pid=target.pid;slot->expectedBirth=target.birth;slot->image=expected;slot->cefSubtree=target.cefSubtree;break;}
            if(!slot){row["cpuStatus"]="sample-capacity";rows.push_back(std::move(row));continue;}
            slot->used=true;
            if(!slot->process)slot->process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,target.pid);
            if(!slot->process){row["cpuStatus"]="process-open-failed";row["win32Error"]=GetLastError();slot->window.reset();rows.push_back(std::move(row));continue;}
            DWORD size=32768,observedSession=MAXDWORD,ownSession=0;wchar_t image[32768]{};
            FILETIME created{},exited{},kernel{},user{};
            if(WaitForSingleObject(slot->process,0)!=WAIT_TIMEOUT){row["cpuStatus"]="process-exited";close(*slot);rows.push_back(std::move(row));continue;}
            const bool imageKnown=QueryFullProcessImageNameW(slot->process,0,image,&size)!=FALSE;
            const auto actual=imageKnown?normalized(std::wstring(image,size)):std::wstring{};
            const bool matching=target.cefSubtree?actual.starts_with(expected+L"\\bin\\cef\\")&&actual.ends_with(L"\\steamwebhelper.exe"):actual==expected;
            if(!imageKnown||!matching||!ProcessIdToSessionId(target.pid,&observedSession)||!ProcessIdToSessionId(GetCurrentProcessId(),&ownSession)||observedSession!=ownSession){row["cpuStatus"]="process-identity-mismatch";close(*slot);rows.push_back(std::move(row));continue;}
            if(!GetProcessTimes(slot->process,&created,&exited,&kernel,&user)){row["cpuStatus"]="cpu-read-failed";row["win32Error"]=GetLastError();slot->window.reset();rows.push_back(std::move(row));continue;}
            if(target.birth&&ticks(created)!=target.birth){row["cpuStatus"]="process-birth-mismatch";close(*slot);rows.push_back(std::move(row));continue;}
            nativeCost::Counters c;c.tick=now;c.processId=target.pid;c.creation100ns=ticks(created);c.cpuKnown=true;c.kernel100ns=ticks(kernel);c.user100ns=ticks(user);c.memoryError=ERROR_NOT_SUPPORTED;
            const auto observation=slot->window.observe(c);const auto counters=nativeCost::toJson(observation);
            for(const auto& [key,value]:counters.items())if(key!="scope"&&key!="memorySource"&&key!="memoryAvailable"&&key!="memoryError")row[key]=value;
            row["identityVerified"]=true;row["logicalProcessors"]=logicalProcessors;
            if(observation.rateKnown){const auto rate=counters.at("cpuMsPerSec").get<double>();row["oneLogicalCpuPercent"]=rate/10.0;if(logicalProcessors)row["machineCpuPercent"]=rate/(10.0*logicalProcessors);}
            rows.push_back(std::move(row));
        }
        for(auto& s:slots_)if(!s.used)close(s);
        return {{"schemaVersion",1},{"sampleTick",now},{"powerGeneration",generation},{"intervalPolicyMs",1000},{"logicalProcessors",logicalProcessors},
            {"cpuScope","Whole identified OS process; Steam owner/UI candidates and other plugins may share work. Not exclusive wallpaper or Decky JS CPU. GPU engine utilization and video-decode attribution unavailable; candidate process type not guaranteed."},
            {"processes",std::move(rows)}};
    }
};
}
