#pragma once
#include <string>
#include <vector>
#include <set>
#include <limits>
#include "json.hpp"

namespace ymcc::sleeprecovery {
using Json=nlohmann::json;
inline unsigned long long number(const Json& j,const char* key) {
    auto i=j.find(key);if(i==j.end())return 0;
    if(i->is_number_unsigned())return i->get<unsigned long long>();
    if(i->is_number_integer()&&i->get<long long>()>=0)return static_cast<unsigned long long>(i->get<long long>());
    return 0;
}
inline bool flag(const Json& j,const char* key) {auto i=j.find(key);return i!=j.end()&&i->is_boolean()&&i->get<bool>();}
inline std::string text(const Json& j,const char* key) {auto i=j.find(key);return i!=j.end()&&i->is_string()?i->get<std::string>():std::string();}
inline std::string field(const std::string& s,const std::string& key) {
    const std::string token=key+"=";size_t start=0;std::string found;bool seen=false;
    while(start<s.size()){const size_t end=s.find('|',start);const auto part=s.substr(start,end==std::string::npos?s.size()-start:end-start);
        if(part.rfind(token,0)==0){if(seen)return {};seen=true;found=part.substr(token.size());}
        if(end==std::string::npos)break;start=end+1;
    }return found;
}
inline bool decimal(const std::string& s,unsigned long long& out) {
    if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)return false;
    try{out=std::stoull(s);return true;}catch(...){return false;}
}
inline std::string repairGate(bool confirmed,bool isolated,bool exiting,bool ready,bool writesAllowed,
    bool retry,bool pending,bool tokenKnown,unsigned long long issued,unsigned long long now,
    unsigned long long expectedGeneration,unsigned long long currentGeneration,bool markersMatch) {
    if(!confirmed)return "confirmation-required";
    if(isolated)return "isolated-session";
    if(exiting)return "app-exiting";
    if(!tokenKnown)return "inspection-required";
    if(now<issued||now-issued>30000)return "inspection-expired";
    if(expectedGeneration!=currentGeneration||!markersMatch)return "state-changed";
    if(!ready||!writesAllowed)return "power-not-ready";
    if(retry)return "sleep-retry-active";
    if(pending)return "recovery-in-progress";
    return {};
}
inline bool inputReason(const std::string& reason,unsigned long long& generation,unsigned long long& epoch) {
    const std::string prefix="sleep-repair-input:";if(reason.rfind(prefix,0)!=0)return false;
    const auto tail=reason.substr(prefix.size());const auto sep=tail.find(':');
    return sep!=std::string::npos&&decimal(tail.substr(0,sep),generation)&&decimal(tail.substr(sep+1),epoch)&&generation&&epoch;
}
inline bool autoInputReason(const std::string& reason,unsigned long long& generation,unsigned long long& epoch,unsigned long long& configEpoch) {
    const std::string prefix="sleep-auto-input:";if(reason.rfind(prefix,0)!=0)return false;
    const auto tail=reason.substr(prefix.size());const auto p=tail.find(':');const auto q=p==std::string::npos?p:tail.find(':',p+1);
    return p!=std::string::npos&&q!=std::string::npos&&decimal(tail.substr(0,p),generation)&&
        decimal(tail.substr(p+1,q-p-1),epoch)&&decimal(tail.substr(q+1),configEpoch)&&generation&&epoch&&configEpoch;
}
// Resident/Modern Standby Hosts intentionally retain their original transport
// generation. Accept that only with the native kept-target proof for this wake.
inline bool inputBindingAllowed(unsigned long long bound,unsigned long long generation,unsigned long long keptGeneration) {
    return bound&&generation&&(bound==generation||(bound<generation&&keptGeneration==generation));
}
inline bool inputGate(bool isolated,bool exiting,bool ready,bool writesAllowed,bool inputReady,
    bool wanted,bool alive,bool prepared,bool quarantined,bool transportFault,
    unsigned long long wantedGeneration,unsigned long long generation,unsigned long long wantedEpoch,unsigned long long epoch) {
    return !isolated&&!exiting&&ready&&writesAllowed&&inputReady&&wanted&&alive&&prepared&&!quarantined&&!transportFault&&
        wantedGeneration&&wantedGeneration==generation&&wantedEpoch&&wantedEpoch==epoch;
}
inline Json checks(const Json& state,const std::vector<std::string>& lines) {
    Json result=Json::array();auto add=[&](const char* id,const char* severity,const char* detail){result.push_back({{"id",id},{"severity",severity},{"detail",detail}});};
    unsigned skipped=0,empty=0,retained=0;bool inputResultKnown=false,inputApplied=false;std::set<std::string> seen;
    for(const auto& line:lines){if(!seen.insert(line).second)continue;auto row=Json::parse(line,nullptr,false);if(!row.is_object())continue;
        const auto event=text(row,"event");auto d=row.find("details");if(d==row.end()||!d->is_object())continue;
        if(event=="sleep-game-pause-skipped"&&flag(*d,"alreadyPaused"))++skipped;
        if(event=="sleep-game-recovery-queued"&&d->contains("targets")&&number(*d,"targets")==0)++empty;
        if(event=="sleep-game-recovery-complete")retained=static_cast<unsigned>(number(*d,"retainedTargets"));
        if(event=="sleep-input-user-repair-result"&&number(*d,"generation")==number(state,"generation")&&number(*d,"epoch")==number(state,"inputEpoch")){inputResultKnown=true;inputApplied=flag(*d,"applied");}
    }
    const auto count=number(state,"leaseCount");
    if(!count&&!flag(state,"ownerPresent")&&(flag(state,"inSuspend")||flag(state,"gameSuspended")))
        add("stale-flags","warning","没有睡眠租约却仍标记为暂停；可尝试对账恢复。");
    if(count)add("retained-leases","warning","仍有睡眠游戏租约；恢复只处理原 PID 与创建时间匹配的实例。");
    if(flag(state,"recoveryPending"))add("recovery-pending","info","自动恢复正在有界确认中，请等待后重新检查。");
    if(skipped&&empty)add("historical-empty-recovery","info","日志出现过已暂停与空恢复队列；历史记录不等于当前仍故障，需连续两轮验证。");
    if(retained&&count)add("recovery-retained","warning","最近恢复仍保留租约；API 接受不等于线程、画面和输入已恢复。");
    if(!flag(state,"inputReady"))add("input-not-ready","warning","输入采集尚未就绪；不会强行打开电源闸门或重建手柄。");
    if(inputResultKnown)add(inputApplied?"input-refresh-applied":"input-refresh-rejected",inputApplied?"info":"warning",inputApplied?"本代日志记录原输入会话刷新已执行；没有重建目标，真实游戏输入仍未验。":"本代原输入会话刷新被安全闸门拒绝；不会以重建手柄代替恢复，请保留反馈。");
    if(flag(state,"inputFault"))add("input-fault","warning","输入后台有故障或隔离状态，请保留反馈包。");
    if(flag(state,"markerLimit"))add("marker-limit","warning","租约数量超出有界检查上限，禁止批量修复，请反馈日志。");
    if(!flag(state,"steamAvailable"))add("steam-unknown","unknown","Steam 只读运行态不可用；不会自动打开调试或重启 Steam。");
    else if(!flag(state,"steamTargetKnown"))add("steam-target-unknown","unknown","Steam 本机状态已读取，但目标快捷方式关联尚未核验。");
    if(flag(state,"steamMismatch"))add("steam-association-suspected","warning","指定快捷方式显示待启动，而游戏实例仍存在；需核对编号及大屏状态，不会伪造运行集合。");
    if(lines.empty())add("no-sleep-log","unknown","尚无可用睡眠日志，请记录一次睡前与唤醒后的检查。");
    add("game-input-unverified","unknown","真实游戏按键、摇杆和 Steam 继续状态需人工确认；检查通过不是全链修复证明。");
    return result;
}
}
