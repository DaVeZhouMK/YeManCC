#pragma once
#include "virtual_target_return_record.h"

// Native node-refresh helper history and typed native visibility responsibility.
// Never CLI/Host provenance or device
// success. Child may be unknown: reserve precedes atomic CreateProcess job-list
// assignment. Only the caller can prove the exact nonkill Job quiet/terminal.
// A named Job missing after its last handle closes is UNKNOWN, never that proof.
namespace ymcc::nativeRefreshWriter {
using ProcessIdentity=targetReturn::ProcessIdentity;
inline constexpr unsigned kSchemaVersion=1;
inline constexpr unsigned kVisibilitySchemaVersion=2;
inline constexpr std::size_t maxVisibilityPaths=16, maxVisibilityLocations=16;
inline constexpr const char* kWriterFence="inherited-exclusive-owner-file-v1";
inline constexpr std::size_t maxRecordBytes=64*1024, maxAttemptIdBytes=100;
inline constexpr const char* jobPrefix="Local\\YMCC.Native.Refresh.";
struct Attempt {
    std::string attemptId;
    ProcessIdentity parent;
    std::optional<ProcessIdentity> child;
    std::string jobName;
    std::string physicalRoot;
    std::string deviceInstanceId;
    std::vector<std::string> locationPaths;
};
enum class VisibilityPathPhase { Reserved, MayHaveIssued };
struct VisibilityPath {
    std::string instanceId;
    VisibilityPathPhase phase=VisibilityPathPhase::Reserved;
};
struct Visibility {
    std::string transactionId;
    ProcessIdentity parent; // actual native PID + birth, never a Host identity
    std::string profile="gpd";
    std::string physicalRoot;
    std::vector<std::string> locationPaths;
    bool beforeCloaking=false; // diagnostic only; never authority to change global cloak
    std::vector<VisibilityPath> paths;
};
struct Record {
    unsigned schemaVersion=kSchemaVersion;
    std::string writerFence=kWriterFence;
    std::string machineBinding;
    std::uint64_t revision=1;
    std::optional<Attempt> active;
    std::optional<Visibility> nativeVisibility;
};
namespace detail {
inline bool fail(std::string* error,const char* reason){return targetReturn::detail::fail(error,reason);}
inline bool text(std::string_view s,std::size_t limit){
    if(!targetReturn::detail::text(s,limit))return false;
    // The shared validator already rejects malformed UTF-8 and C0/DEL; also C1.
    for(std::size_t i=0;i+1<s.size();++i)if(static_cast<unsigned char>(s[i])==0xc2&&
        static_cast<unsigned char>(s[i+1])>=0x80&&static_cast<unsigned char>(s[i+1])<=0x9f)return false;
    return true;
}
inline bool attempt(const Attempt& a) {
    if(a.attemptId.size()>maxAttemptIdBytes||!targetReturn::detail::token(a.attemptId)||
       !targetReturn::detail::process(a.parent)||a.jobName!=std::string(jobPrefix)+a.attemptId)return false;
    if((a.child&&!targetReturn::detail::process(*a.child))||
       !targetReturn::detail::instance(a.physicalRoot)||!targetReturn::detail::instance(a.deviceInstanceId)||
       a.locationPaths.empty()||a.locationPaths.size()>16)return false;
    for(const auto& location:a.locationPaths)if(!text(location,1024))return false;
    return true;
}
// Same GPD USB root grammar as nativeForeground::rootCandidate, with the
// shared strict instance/text limits. No dependency on runtime/device headers.
inline bool visibilityInstance(std::string_view value,bool root=false){
    if(!targetReturn::detail::instance(value))return false;
    const auto id=targetReturn::detail::fold(value);
    if(id.find("HIDMAESTRO")!=std::string::npos||id.find("VIGEM")!=std::string::npos||
       id.find("VIRTUAL")!=std::string::npos)return false;
    if(root)return id.compare(0,22,"USB\\VID_045E&PID_028E\\")==0;
    if(id.compare(0,21,"USB\\VID_045E&PID_028E")!=0&&id.compare(0,21,"HID\\VID_045E&PID_028E")!=0)return false;
    return id[21]=='\\'||(id[21]=='&'&&id[22]!='\\'&&
        id.find("VID_",21)==std::string::npos&&id.find("PID_",21)==std::string::npos);
}
inline bool visibility(const Visibility& v){
    if(v.transactionId.size()>maxAttemptIdBytes||!targetReturn::detail::token(v.transactionId)||
       !targetReturn::detail::process(v.parent)||v.profile!="gpd"||!visibilityInstance(v.physicalRoot,true)||
       v.locationPaths.empty()||v.locationPaths.size()>maxVisibilityLocations||v.paths.size()>maxVisibilityPaths)return false;
    std::set<std::string> locations,paths;
    for(const auto& location:v.locationPaths)if(!text(location,1024)||!locations.insert(targetReturn::detail::fold(location)).second)return false;
    for(const auto& path:v.paths)if(!visibilityInstance(path.instanceId)||
        (path.phase!=VisibilityPathPhase::Reserved&&path.phase!=VisibilityPathPhase::MayHaveIssued)||
        !paths.insert(targetReturn::detail::fold(path.instanceId)).second)return false;
    return true;
}
inline bool bump(Record& r,std::string* error){
    if(r.revision==(std::numeric_limits<std::uint64_t>::max)())return fail(error,"revision-overflow");
    ++r.revision;return true;
}
}
inline bool validRecord(const Record& r,std::string* error=nullptr){
    if((r.schemaVersion!=kSchemaVersion&&r.schemaVersion!=kVisibilitySchemaVersion)||
       (r.schemaVersion==kSchemaVersion&&r.nativeVisibility)||r.writerFence!=kWriterFence||!detail::text(r.machineBinding,4096)||!r.revision)
        return detail::fail(error,"writer-envelope-invalid");
    if(r.active&&!detail::attempt(*r.active))return detail::fail(error,"writer-attempt-invalid");
    if(r.nativeVisibility&&!detail::visibility(*r.nativeVisibility))return detail::fail(error,"visibility-invalid");
    return true;
}
inline bool encode(const Record& r,std::string& out,std::string* error=nullptr){
    try {
        if(!validRecord(r,error))return false;
        using J=nlohmann::json;J active=nullptr;
        if(r.active){const auto& a=*r.active;J child=nullptr;
            if(a.child)child={{"pid",a.child->pid},{"creationTime",a.child->creationTime}};
            active={{"attemptId",a.attemptId},{"parent",{{"pid",a.parent.pid},{"creationTime",a.parent.creationTime}}},
                {"child",child},{"jobName",a.jobName},{"physicalRoot",a.physicalRoot},
                {"deviceInstanceId",a.deviceInstanceId},{"locationPaths",a.locationPaths}};
        }
        J envelope={{"schemaVersion",r.schemaVersion},{"writerFence",r.writerFence},{"machineBinding",r.machineBinding},
                    {"revision",r.revision},{"active",active}};
        // Schema 1 remains byte-for-byte the original five-key encoding.
        if(r.schemaVersion==kVisibilitySchemaVersion){
            J visibility=nullptr;
            if(r.nativeVisibility){const auto& v=*r.nativeVisibility;J paths=J::array();
                for(const auto& path:v.paths)paths.push_back({{"instanceId",path.instanceId},
                    {"phase",path.phase==VisibilityPathPhase::Reserved?"reserved":"may-have-issued"}});
                visibility={{"transactionId",v.transactionId},{"parent",{{"pid",v.parent.pid},{"creationTime",v.parent.creationTime}}},
                    {"profile",v.profile},{"physicalRoot",v.physicalRoot},{"locationPaths",v.locationPaths},
                    {"beforeCloaking",v.beforeCloaking},{"paths",paths}};
            }
            envelope["nativeVisibility"]=std::move(visibility);
        }
        auto bytes=envelope.dump();
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
        if(duplicate||!stack.empty()||!j.is_object()||!j.contains("schemaVersion"))
            return detail::fail(error,"writer-shape-invalid");
        std::uint64_t schema=0;
        if(!targetReturn::detail::uintValue(j.at("schemaVersion"),kVisibilitySchemaVersion,schema)||
           (schema!=kSchemaVersion&&schema!=kVisibilitySchemaVersion))return detail::fail(error,"writer-schema-invalid");
        if(schema==kSchemaVersion?!targetReturn::detail::keys(j,{"schemaVersion","writerFence","machineBinding","revision","active"}):
           !targetReturn::detail::keys(j,{"schemaVersion","writerFence","machineBinding","revision","active","nativeVisibility"}))
            return detail::fail(error,"writer-shape-invalid");
        if(!j.at("writerFence").is_string()||!j.at("machineBinding").is_string())
            return detail::fail(error,"writer-schema-invalid");
        Record candidate;candidate.schemaVersion=unsigned(schema);candidate.writerFence=j.at("writerFence").get<std::string>();
        candidate.machineBinding=j.at("machineBinding").get<std::string>();
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
            if(!targetReturn::detail::keys(value,{"attemptId","parent","child","jobName","physicalRoot","deviceInstanceId","locationPaths"})||
               !value.at("attemptId").is_string()||!value.at("jobName").is_string()||
               !value.at("physicalRoot").is_string()||!value.at("deviceInstanceId").is_string()||
               !value.at("locationPaths").is_array()||value.at("locationPaths").empty()||value.at("locationPaths").size()>16)
                return detail::fail(error,"writer-attempt-shape-invalid");
            Attempt a;a.attemptId=value.at("attemptId").get<std::string>();a.jobName=value.at("jobName").get<std::string>();
            if(!process(value.at("parent"),a.parent))return detail::fail(error,"writer-parent-invalid");
            if(!value.at("child").is_null()){ProcessIdentity child;if(!process(value.at("child"),child))return detail::fail(error,"writer-child-invalid");a.child=std::move(child);}
            a.physicalRoot=value.at("physicalRoot").get<std::string>();
            a.deviceInstanceId=value.at("deviceInstanceId").get<std::string>();
            for(const auto& location:value.at("locationPaths")){
                if(!location.is_string())return detail::fail(error,"writer-location-type-invalid");
                a.locationPaths.push_back(location.get<std::string>());
            }
            candidate.active=std::move(a);
        }
        if(schema==kVisibilitySchemaVersion&&!j.at("nativeVisibility").is_null()){
            const auto& visibilityValue=j.at("nativeVisibility");
            if(!targetReturn::detail::keys(visibilityValue,{"transactionId","parent","profile","physicalRoot","locationPaths","beforeCloaking","paths"})||
               !visibilityValue.at("transactionId").is_string()||!visibilityValue.at("profile").is_string()||!visibilityValue.at("physicalRoot").is_string()||
               !visibilityValue.at("beforeCloaking").is_boolean()||!visibilityValue.at("locationPaths").is_array()||visibilityValue.at("locationPaths").empty()||
               visibilityValue.at("locationPaths").size()>maxVisibilityLocations||!visibilityValue.at("paths").is_array()||visibilityValue.at("paths").size()>maxVisibilityPaths)
                return detail::fail(error,"visibility-shape-invalid");
            Visibility v;v.transactionId=visibilityValue.at("transactionId").get<std::string>();v.profile=visibilityValue.at("profile").get<std::string>();
            v.physicalRoot=visibilityValue.at("physicalRoot").get<std::string>();v.beforeCloaking=visibilityValue.at("beforeCloaking").get<bool>();
            if(!process(visibilityValue.at("parent"),v.parent))return detail::fail(error,"visibility-parent-invalid");
            for(const auto& location:visibilityValue.at("locationPaths")){
                if(!location.is_string())return detail::fail(error,"visibility-location-type-invalid");
                v.locationPaths.push_back(location.get<std::string>());
            }
            for(const auto& path:visibilityValue.at("paths")){
                if(!targetReturn::detail::keys(path,{"instanceId","phase"})||!path.at("instanceId").is_string()||!path.at("phase").is_string())
                    return detail::fail(error,"visibility-path-shape-invalid");
                const auto phase=path.at("phase").get<std::string>();
                if(phase!="reserved"&&phase!="may-have-issued")return detail::fail(error,"visibility-path-phase-invalid");
                v.paths.push_back({path.at("instanceId").get<std::string>(),phase=="reserved"?VisibilityPathPhase::Reserved:VisibilityPathPhase::MayHaveIssued});
            }
            candidate.nativeVisibility=std::move(v);
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
    if(reserved.child||!detail::attempt(reserved))return detail::fail(error,"reservation-must-be-childless");
    Record candidate=current;candidate.active=reserved;if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
}
inline bool markChild(const Record& current,std::uint64_t expectedRevision,const std::string& attemptId,const ProcessIdentity& child,Record& out,std::string* error=nullptr){
    if(!validRecord(current,error))return false;
    if(expectedRevision!=current.revision)return detail::fail(error,"stale-revision");
    if(!current.active||current.active->attemptId!=attemptId)return detail::fail(error,"exact-attempt-required");
    if(!targetReturn::detail::process(child))return detail::fail(error,"child-identity-invalid");
    if(current.active->child.has_value()){
        if(!targetReturn::detail::sameProcess(*current.active->child,child))return detail::fail(error,"child-identity-immutable");
        out=current;return true;
    }
    Record candidate=current;candidate.active->child=child;
    if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
}
// Caller first obtains positive exact Job quiet/terminal proof, or independently
// authenticates the producer's inherited-lease protocol, reacquires the same
// share=0 owner, and proves every recorded exact parent/child identity Dead.
// This also applies to childless reservations; old records imply no lease
// protocol. OpenJobObject ERROR_FILE_NOT_FOUND is UNKNOWN, never proof.
// This transform only applies a transition; it never verifies/invents proof,
// infers death, or grants cleanup rights.
inline bool settle(const Record& current,std::uint64_t expectedRevision,const std::string& attemptId,Record& out,std::string* error=nullptr){
    if(!validRecord(current,error))return false;
    if(expectedRevision!=current.revision)return detail::fail(error,"stale-revision");
    if(!current.active||current.active->attemptId!=attemptId)return detail::fail(error,"exact-attempt-required");
    Record candidate=current;candidate.active.reset();if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
}
// These are candidate transitions only. The caller supplies every proof and
// serializes them under the same Session owner as helper history. Reserved
// grants no unhide right; only MayHaveIssued may support per-path compensation.
inline bool reserveVisibility(const Record& current,std::uint64_t expectedRevision,const Visibility& allReserved,Record& out,std::string* error=nullptr){
    if(!validRecord(current,error))return false;
    if(expectedRevision!=current.revision)return detail::fail(error,"stale-revision");
    if(current.nativeVisibility)return detail::fail(error,"visibility-already-active");
    if(!detail::visibility(allReserved)||allReserved.paths.empty())return detail::fail(error,"visibility-reservation-invalid");
    for(const auto& path:allReserved.paths)if(path.phase!=VisibilityPathPhase::Reserved)return detail::fail(error,"visibility-reservation-must-be-reserved");
    Record candidate=current;candidate.schemaVersion=kVisibilitySchemaVersion;candidate.nativeVisibility=allReserved;
    if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
}
namespace detail {
inline bool exactVisibility(const Record& current,std::uint64_t expectedRevision,const std::string& transactionId,std::string* error){
    if(!validRecord(current,error))return false;
    if(expectedRevision!=current.revision)return fail(error,"stale-revision");
    if(!current.nativeVisibility||current.nativeVisibility->transactionId!=transactionId)return fail(error,"exact-visibility-required");
    return true;
}
}
inline bool reserveVisibilityPath(const Record& current,std::uint64_t expectedRevision,const std::string& transactionId,const std::string& instanceId,Record& out,std::string* error=nullptr){
    if(!detail::exactVisibility(current,expectedRevision,transactionId,error))return false;
    if(!detail::visibilityInstance(instanceId)||current.nativeVisibility->paths.size()>=maxVisibilityPaths)return detail::fail(error,"visibility-path-invalid-or-limit");
    for(const auto& path:current.nativeVisibility->paths)if(targetReturn::detail::fold(path.instanceId)==targetReturn::detail::fold(instanceId))return detail::fail(error,"visibility-path-already-reserved");
    Record candidate=current;candidate.nativeVisibility->paths.push_back({instanceId,VisibilityPathPhase::Reserved});
    if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
}
inline bool markVisibilityMayHaveIssued(const Record& current,std::uint64_t expectedRevision,const std::string& transactionId,const std::string& instanceId,Record& out,std::string* error=nullptr){
    if(!detail::exactVisibility(current,expectedRevision,transactionId,error))return false;
    Record candidate=current;
    for(auto& path:candidate.nativeVisibility->paths)if(path.instanceId==instanceId){
        if(path.phase==VisibilityPathPhase::MayHaveIssued){out=current;return true;}
        path.phase=VisibilityPathPhase::MayHaveIssued;
        if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
    }
    return detail::fail(error,"exact-visibility-path-required");
}
// Caller has already proved this exact per-path configuration returned. A
// Reserved entry may be settled without issuing unhide. No proof is inferred.
inline bool settleVisibilityPath(const Record& current,std::uint64_t expectedRevision,const std::string& transactionId,const std::string& instanceId,Record& out,std::string* error=nullptr){
    if(!detail::exactVisibility(current,expectedRevision,transactionId,error))return false;
    Record candidate=current;auto& paths=candidate.nativeVisibility->paths;
    for(auto path=paths.begin();path!=paths.end();++path)if(path->instanceId==instanceId){
        paths.erase(path);if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
    }
    return detail::fail(error,"exact-visibility-path-required");
}
inline bool completeVisibility(const Record& current,std::uint64_t expectedRevision,const std::string& transactionId,Record& out,std::string* error=nullptr){
    if(!detail::exactVisibility(current,expectedRevision,transactionId,error))return false;
    if(current.active||!current.nativeVisibility->paths.empty())return detail::fail(error,"visibility-or-writer-pending");
    Record candidate=current;candidate.nativeVisibility.reset();
    if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
}
// Caller has already durably recorded the complete replacement Hosted ledger.
// Atomic removal transfers responsibility; it never manufactures Hosted proof.
inline bool transferVisibility(const Record& current,std::uint64_t expectedRevision,const std::string& transactionId,Record& out,std::string* error=nullptr){
    if(!detail::exactVisibility(current,expectedRevision,transactionId,error))return false;
    if(current.active)return detail::fail(error,"writer-still-active");
    Record candidate=current;candidate.nativeVisibility.reset();
    if(!detail::bump(candidate,error))return false;out=std::move(candidate);return true;
}
} // namespace ymcc::nativeRefreshWriter