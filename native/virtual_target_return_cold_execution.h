#pragma once
#include "virtual_target_return_record.h"
#include <sstream>

// One bounded execution pass over the existing durable return duties. The
// caller supplies the existing R3 ticket/deadline and owns the Host transaction.
// No timer, Start/Stop, retry admission, model authority or current pin lives here.
// Environment leaves may observe OS/driver state; this is the actual shared
// consumer used by production and offline fault fixtures, not a copied model.
namespace ymcc::targetReturn::coldExecution {
enum class State { Clean, Deferred };
enum class Reason {
    Complete, ContextRetired, StoreUnavailable, WeakBinding, LegacyWriter,
    NativeWriterUnproven, HostWriterUnproven, CliWriterUnproven,
    TargetAbsenceUnproven, VisibilityUnproven, PhysicalAncestryUnproven,
    PathStillHidden, PathCommitFailed, OriginCommitFailed, Exception
};
struct Result {
    State state=State::Deferred;
    Reason reason=Reason::ContextRetired;
    unsigned unhideAttempts=0,settledPaths=0,settledOrigins=0;
    std::string originKey,pathInstanceId;
    bool clean()const noexcept{return state==State::Clean;}
};
inline const char* reasonName(Reason r) noexcept {
    switch(r){
    case Reason::Complete:return "complete";
    case Reason::ContextRetired:return "original-context-retired-or-paused";
    case Reason::StoreUnavailable:return "return-store-unavailable";
    case Reason::WeakBinding:return "historical-machine-binding-unproven";
    case Reason::LegacyWriter:return "legacy-cli-writer-protocol-unproven";
    case Reason::NativeWriterUnproven:return "recorded-native-alive-or-unknown";
    case Reason::HostWriterUnproven:return "recorded-host-alive-or-unknown";
    case Reason::CliWriterUnproven:return "recorded-cli-writer-unproven";
    case Reason::TargetAbsenceUnproven:return "complete-target-absence-unproven";
    case Reason::VisibilityUnproven:return "owned-path-readback-unavailable";
    case Reason::PhysicalAncestryUnproven:return "exact-physical-ancestry-unproven";
    case Reason::PathStillHidden:return "owned-path-still-hidden";
    case Reason::PathCommitFailed:return "owned-path-settlement-not-committed";
    case Reason::OriginCommitFailed:return "origin-settlement-not-committed";
    default:return "cold-return-exception";}
}
inline bool containsPath(const std::vector<std::string>& paths,const std::string& exact){
    return std::any_of(paths.begin(),paths.end(),[&](const auto& p){return detail::fold(p)==detail::fold(exact);});
}
// The existing CLI merges stdout/stderr. Do not silently drop an error or
// malformed line and then claim an empty hidden set. Query success and complete
// capture are proved by hidHideRun; this parser proves the entire list shape.
inline bool parseHidden(std::string_view raw,std::vector<std::string>& out) {
    if(raw.size()>1024*1024||raw.find('\0')!=std::string_view::npos)return false;
    std::vector<std::string> parsed;std::istringstream lines{std::string(raw)};std::string line;
    constexpr std::string_view prefix="--dev-hide \"";
    while(std::getline(lines,line)){
        const auto begin=line.find_first_not_of(" \t\r");if(begin==std::string::npos)continue;
        const auto end=line.find_last_not_of(" \t\r");line=line.substr(begin,end-begin+1);
        if(line.size()<=prefix.size()||line.compare(0,prefix.size(),prefix)!=0||line.back()!='"')return false;
        auto id=line.substr(prefix.size(),line.size()-prefix.size()-1);
        if(!detail::instance(id)||containsPath(parsed,id)||parsed.size()>=4096)return false;
        parsed.push_back(std::move(id));
    }
    out=std::move(parsed);return true;
}
template<class Environment> inline Result run(Environment& env) noexcept {
    Result result;
    const auto defer=[&](Reason reason){result.reason=reason;return result;};
    try {
        if(!env.current())return defer(Reason::ContextRetired);
        const auto* record=env.record();
        if(!record)return defer(Reason::StoreUnavailable);
        if(record->obligations.empty()){result.state=State::Clean;result.reason=Reason::Complete;return result;}
        if(record->machineBinding.rfind("windows-machine-guid:",0)!=0)return defer(Reason::WeakBinding);
        const auto obligations=record->obligations;
        const auto guard=[&](const Origin& origin){
            result.originKey=origin.transactionId;
            if(!env.current())return Reason::ContextRetired;
            if(origin.cliWriterProtocol!=1)return Reason::LegacyWriter;
            if(!env.nativeDead(origin.parent))return Reason::NativeWriterUnproven;
            if(!env.current())return Reason::ContextRetired;
            if(!env.hostDead(origin.host))return Reason::HostWriterUnproven;
            if(!env.current())return Reason::ContextRetired;
            if(!env.writersQuiet())return Reason::CliWriterUnproven;
            if(!env.current())return Reason::ContextRetired;
            return Reason::Complete;
        };
        std::vector<std::string> knownIds;
        for(const auto& group:obligations){
            const auto reason=guard(group.origin);if(reason!=Reason::Complete)return defer(reason);
            if(group.acceptedPsPin){
                const auto add=[&](const std::string& id){if(!containsPath(knownIds,id))knownIds.push_back(id);};
                add(group.acceptedPsPin->sdkRootInstanceId);
                for(const auto& id:group.acceptedPsPin->hidInstanceIds)add(id);
            }
        }
        // A historical pin is only a negative witness. The census also checks
        // every supported virtual family even if this list is empty.
        if(!env.targetAbsent(knownIds))return defer(Reason::TargetAbsenceUnproven);
        for(const auto& group:obligations){
            for(const auto& path:group.paths){
                result.pathInstanceId=path.instanceId;
                auto reason=guard(group.origin);if(reason!=Reason::Complete)return defer(reason);
                std::vector<std::string> hidden;
                if(!env.visibility(hidden))return defer(Reason::VisibilityUnproven);
                if(containsPath(hidden,path.instanceId)){
                    if(!env.pathProven(group.origin.physicalRoot,path.instanceId))return defer(Reason::PhysicalAncestryUnproven);
                    reason=guard(group.origin);if(reason!=Reason::Complete)return defer(reason);
                    // ACK loss is not completion. Conversely, a lost ACK may
                    // still have taken effect; only the later readback settles.
                    (void)env.unhide(path.instanceId);
                    ++result.unhideAttempts;
                    if(!env.current())return defer(Reason::ContextRetired);
                    hidden.clear();
                    if(!env.visibility(hidden))return defer(Reason::VisibilityUnproven);
                    if(containsPath(hidden,path.instanceId))return defer(Reason::PathStillHidden);
                }
                reason=guard(group.origin);if(reason!=Reason::Complete)return defer(reason);
                if(!env.targetAbsent(knownIds))return defer(Reason::TargetAbsenceUnproven);
                if(!env.current())return defer(Reason::ContextRetired);
                if(!env.consumePath(group.origin,path.instanceId))return defer(Reason::PathCommitFailed);
                ++result.settledPaths;
            }
            const auto* live=env.record();if(!live)return defer(Reason::StoreUnavailable);
            const bool remaining=std::any_of(live->obligations.begin(),live->obligations.end(),[&](const auto& value){return detail::sameOrigin(value.origin,group.origin);});
            if(remaining){
                const auto reason=guard(group.origin);if(reason!=Reason::Complete)return defer(reason);
                if(!env.targetAbsent(knownIds))return defer(Reason::TargetAbsenceUnproven);
                if(!env.current())return defer(Reason::ContextRetired);
                if(!env.consumeOrigin(group.origin))return defer(Reason::OriginCommitFailed);
            }
            ++result.settledOrigins;result.pathInstanceId.clear();
        }
        const auto* finalRecord=env.record();
        if(!finalRecord||!finalRecord->obligations.empty())return defer(Reason::OriginCommitFailed);
        result.state=State::Clean;result.reason=Reason::Complete;return result;
    }catch(...){return defer(Reason::Exception);}
}
} // namespace ymcc::targetReturn::coldExecution
