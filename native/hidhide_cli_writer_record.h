#pragma once
#include "virtual_target_return_record.h"

// Historical CLI writer identity only. No executable path, command, process
// lookup, Job operation, target provenance or cold-cleanup authority lives here.
namespace ymcc::hidhideCliWriter {
using ProcessIdentity=targetReturn::ProcessIdentity;
inline constexpr unsigned kSchemaVersion=1;
inline constexpr std::size_t maxRecordBytes=64*1024, maxAttemptIdBytes=100;
inline constexpr const char* jobPrefix="Local\\YMCC.HidHide.Cli.";
enum class Stage { Reserved, MayRun };
struct Attempt {
    std::string attemptId;
    ProcessIdentity parent;
    std::optional<ProcessIdentity> child;
    std::string jobName;
    Stage stage=Stage::Reserved;
};
struct Record {
    unsigned schemaVersion=kSchemaVersion;
    std::string machineBinding;
    std::uint64_t revision=1;
    std::optional<Attempt> active;
};
namespace detail {
inline bool fail(std::string* error,const char* reason){return targetReturn::detail::fail(error,reason);}
inline bool attempt(const Attempt& a) {
    if(a.attemptId.size()>maxAttemptIdBytes||!targetReturn::detail::token(a.attemptId)||
       !targetReturn::detail::process(a.parent)||a.jobName!=std::string(jobPrefix)+a.attemptId)return false;
    switch(a.stage){
    case Stage::Reserved:return !a.child;
    case Stage::MayRun:return a.child&&targetReturn::detail::process(*a.child);
    }
    return false;
}
inline bool bump(Record& r,std::string* error){
    if(r.revision==(std::numeric_limits<std::uint64_t>::max)())return fail(error,"revision-overflow");
    ++r.revision;return true;
}
}
inline bool validRecord(const Record& r,std::string* error=nullptr){
    if(r.schemaVersion!=kSchemaVersion||!targetReturn::detail::text(r.machineBinding,4096)||!r.revision)
        return detail::fail(error,"writer-envelope-invalid");
    if(r.active&&!detail::attempt(*r.active))return detail::fail(error,"writer-attempt-invalid");
    return true;
}
inline bool encode(const Record& r,std::string& out,std::string* error=nullptr){
    try {
        if(!validRecord(r,error))return false;
        using J=nlohmann::json;J active=nullptr;
        if(r.active){const auto& a=*r.active;J child=nullptr;
            if(a.child)child={{"pid",a.child->pid},{"creationTime",a.child->creationTime}};
            active={{"attemptId",a.attemptId},{"parent",{{"pid",a.parent.pid},{"creationTime",a.parent.creationTime}}},
                {"child",child},{"jobName",a.jobName},{"stage",a.stage==Stage::Reserved?"reserved":"may-run"}};
        }
        auto bytes=J({{"schemaVersion",r.schemaVersion},{"machineBinding",r.machineBinding},
                      {"revision",r.revision},{"active",active}}).dump();
        if(bytes.size()>maxRecordBytes)return detail::fail(error,"writer-byte-limit");
        out=std::move(bytes);return true;
    }catch(...){return detail::fail(error,"writer-encode-failed");}
}
inline bool decode(std::string_view bytes,Record& out,std::string* error=nullptr){
    if(bytes.empty()||bytes.size()>maxRecordBytes||bytes.find('\0')!=std::string_view::npos||bytes.substr(0,3)=="\xef\xbb\xbf")
        return detail::fail(error,"writer-bytes-invalid");
    try {
        using J=nlohmann::json;bool duplicate=false;std::vector<std::set<std::string>> stack;
        auto callback=[&](int depth,J::parse_event_t event,J& parsed){
            if(depth>8)throw std::runtime_error("writer-depth-limit");
            if(event==J::parse_event_t::object_start)stack.emplace_back();
            else if(event==J::parse_event_t::key){if(stack.empty()||!stack.back().insert(parsed.get<std::string>()).second)duplicate=true;}
            else if(event==J::parse_event_t::object_end){if(stack.empty())duplicate=true;else stack.pop_back();}
            return true;
        };
        const auto j=J::parse(bytes.begin(),bytes.end(),callback,true,false);
        if(duplicate||!stack.empty()||!targetReturn::detail::keys(j,{"schemaVersion","machineBinding","revision","active"}))
            return detail::fail(error,"writer-shape-invalid");
        std::uint64_t schema=0;
        if(!targetReturn::detail::uintValue(j.at("schemaVersion"),kSchemaVersion,schema)||schema!=kSchemaVersion||!j.at("machineBinding").is_string())
            return detail::fail(error,"writer-schema-invalid");
        Record candidate;candidate.machineBinding=j.at("machineBinding").get<std::string>();
        if(!targetReturn::detail::uintValue(j.at("revision"),(std::numeric_limits<std::uint64_t>::max)(),candidate.revision))
            return detail::fail(error,"writer-revision-invalid");
        auto process=[](const J& v,ProcessIdentity& p){
            std::uint64_t pid=0;
            if(!targetReturn::detail::keys(v,{"pid","creationTime"})||
               !targetReturn::detail::uintValue(v.at("pid"),(std::numeric_limits<std::uint32_t>::max)(),pid)||!v.at("creationTime").is_string())return false;
            p.pid=std::uint32_t(pid);p.creationTime=v.at("creationTime").get<std::string>();return targetReturn::detail::process(p);
        };
        const auto& value=j.at("active");
        if(!value.is_null()){
            if(!targetReturn::detail::keys(value,{"attemptId","parent","child","jobName","stage"})||
               !value.at("attemptId").is_string()||!value.at("jobName").is_string()||!value.at("stage").is_string())
                return detail::fail(error,"writer-attempt-shape-invalid");
            Attempt a;a.attemptId=value.at("attemptId").get<std::string>();a.jobName=value.at("jobName").get<std::string>();
            if(!process(value.at("parent"),a.parent))return detail::fail(error,"writer-parent-invalid");
            if(!value.at("child").is_null()){ProcessIdentity child;if(!process(value.at("child"),child))return detail::fail(error,"writer-child-invalid");a.child=std::move(child);}
            const auto stage=value.at("stage").get<std::string>();
            if(stage=="reserved")a.stage=Stage::Reserved;
            else if(stage=="may-run")a.stage=Stage::MayRun;
            else return detail::fail(error,"writer-stage-invalid");
            candidate.active=std::move(a);
        }
        if(!validRecord(candidate,error))return false;
        out=std::move(candidate);return true;
    }catch(...){return detail::fail(error,"writer-parse-failed");}
}
// Pure candidate transforms. Even an otherwise idempotent request must carry
// the exact current revision. Parent/job binding can never be replaced in place.
inline bool reserve(const Record& current,std::uint64_t expectedRevision,const Attempt& reserved,Record& out,std::string* error=nullptr){
    if(!validRecord(current,error))return false;
    if(expectedRevision!=current.revision)return detail::fail(error,"stale-revision");
    if(current.active)return detail::fail(error,"writer-already-active");
    if(reserved.stage!=Stage::Reserved||!detail::attempt(reserved))return detail::fail(error,"reservation-must-be-childless");
    Record candidate=current;candidate.active=reserved;if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
}
inline bool markMayRun(const Record& current,std::uint64_t expectedRevision,const std::string& attemptId,const ProcessIdentity& child,Record& out,std::string* error=nullptr){
    if(!validRecord(current,error))return false;
    if(expectedRevision!=current.revision)return detail::fail(error,"stale-revision");
    if(!current.active||current.active->attemptId!=attemptId)return detail::fail(error,"exact-attempt-required");
    if(!targetReturn::detail::process(child))return detail::fail(error,"child-identity-invalid");
    if(current.active->stage==Stage::MayRun){
        if(!targetReturn::detail::sameProcess(*current.active->child,child))return detail::fail(error,"child-identity-immutable");
        out=current;return true;
    }
    Record candidate=current;candidate.active->child=child;candidate.active->stage=Stage::MayRun;
    if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
}
// The caller must first prove the exact writer exited. This only clears the
// exact active attempt; it cannot infer death, target state or cleanup rights.
inline bool settle(const Record& current,std::uint64_t expectedRevision,const std::string& attemptId,Record& out,std::string* error=nullptr){
    if(!validRecord(current,error))return false;
    if(expectedRevision!=current.revision)return detail::fail(error,"stale-revision");
    if(!current.active||current.active->attemptId!=attemptId)return detail::fail(error,"exact-attempt-required");
    Record candidate=current;candidate.active.reset();if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
}
} // namespace ymcc::hidhideCliWriter