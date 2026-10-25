#pragma once
#include "json.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>

// Data and candidate transforms only. A disk record is historical provenance,
// never current endpoint admission, dead-process proof or unhide permission.
namespace ymcc::targetReturn {
inline constexpr unsigned schemaVersion=1;
inline constexpr std::size_t maxRecordBytes=64*1024, maxOrigins=16, maxPaths=128;
inline constexpr std::size_t maxPathsPerOrigin=32, maxInstanceBytes=512, maxHids=128;
struct ProcessIdentity { std::uint32_t pid=0; std::string creationTime; };
struct Origin {
    std::string transactionId;
    ProcessIdentity parent,host;
    std::string hostInstanceId,runId,targetId,persona,physicalRoot;
    std::uint64_t epoch=0;
    // Immutable producer provenance only; zero is legacy/unknown, never cleanup authority.
    unsigned cliWriterProtocol=0;
};
enum class DispatchStage { Reserved, MayHaveIssued };
struct PathDebt { std::string instanceId; DispatchStage stage=DispatchStage::Reserved; };
// Only the caller can supply this from a previously accepted real State receipt.
// Deliberately no complete/valid field: decode never accepts a current PS pin.
struct AcceptedPsPin {
    std::string sdkRootInstanceId;
    std::vector<std::string> hidInstanceIds;
    std::uint64_t bindingGeneration=0;
};
struct Obligation { Origin origin; std::vector<PathDebt> paths; std::optional<AcceptedPsPin> acceptedPsPin; bool targetMayExist=false; };
struct Record {
    unsigned schema=schemaVersion;
    std::string machineBinding;
    std::uint64_t revision=1;
    std::vector<Obligation> obligations;
};
namespace detail {
inline bool fail(std::string* error,const char* reason){if(error)*error=reason;return false;}
inline std::string fold(std::string_view s){std::string out(s);for(char& c:out)if(c>='a'&&c<='z')c=char(c-'a'+'A');return out;}
inline bool text(std::string_view s,std::size_t limit,bool empty=false) noexcept {
    if(s.size()>limit||(!empty&&s.empty()))return false;
    for(std::size_t i=0;i<s.size();){const auto lead=static_cast<unsigned char>(s[i++]);if(lead<0x80){if(lead<32||lead==127)return false;continue;}
        unsigned extra=0;std::uint32_t code=0,minimum=0;
        if(lead>=0xc2&&lead<=0xdf){extra=1;code=lead&31;minimum=0x80;}
        else if(lead>=0xe0&&lead<=0xef){extra=2;code=lead&15;minimum=0x800;}
        else if(lead>=0xf0&&lead<=0xf4){extra=3;code=lead&7;minimum=0x10000;}else return false;
        if(s.size()-i<extra)return false;for(unsigned n=0;n<extra;++n){const auto b=static_cast<unsigned char>(s[i++]);if((b&0xc0)!=0x80)return false;code=(code<<6)|(b&63);}
        if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
    }return true;
}
inline bool token(std::string_view s) noexcept {
    if(s.empty()||s.size()>128)return false;
    for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='{'||c=='}'||c=='.'||c==':'))return false;
    return s!="."&&s!="..";
}
inline bool decimal(std::string_view s) noexcept {
    if(s.empty()||s.size()>20||s[0]=='0')return false;std::uint64_t v=0;
    for(char c:s){if(c<'0'||c>'9')return false;auto n=unsigned(c-'0');if(v>((std::numeric_limits<std::uint64_t>::max)()-n)/10)return false;v=v*10+n;}return v!=0;
}
inline bool process(const ProcessIdentity& p) noexcept{return p.pid!=0&&decimal(p.creationTime);}
inline bool instance(std::string_view s) noexcept {
    if(s.empty()||s.size()>maxInstanceBytes)return false;unsigned separators=0;std::size_t start=0;
    for(std::size_t i=0;i<=s.size();++i){if(i==s.size()||s[i]=='\\'){
        auto segment=s.substr(start,i-start);if(segment.empty()||segment=="."||segment=="..")return false;if(i!=s.size())++separators;start=i+1;
    }else{char c=s[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='&'||c=='_'||c=='-'||c=='{'||c=='}'||c=='.'||c=='#'))return false;}}
    return separators==2;
}
inline bool sameProcess(const ProcessIdentity&a,const ProcessIdentity&b){return a.pid==b.pid&&a.creationTime==b.creationTime;}
inline bool origin(const Origin&o,bool allowTargetOnly=false) noexcept {
    return o.cliWriterProtocol<=1&&token(o.transactionId)&&process(o.parent)&&process(o.host)&&token(o.hostInstanceId)&&token(o.runId)&&token(o.targetId)&&o.epoch&&
        (o.persona=="steamdeck"||o.persona=="dualsense-edge"||o.persona=="elite")&&
        (instance(o.physicalRoot)||(allowTargetOnly&&o.physicalRoot.empty()));
}
inline bool sameOrigin(const Origin&a,const Origin&b){return a.transactionId==b.transactionId&&sameProcess(a.parent,b.parent)&&sameProcess(a.host,b.host)&&
    a.hostInstanceId==b.hostInstanceId&&a.runId==b.runId&&a.epoch==b.epoch&&a.targetId==b.targetId&&a.persona==b.persona&&a.physicalRoot==b.physicalRoot&&a.cliWriterProtocol==b.cliWriterProtocol;}
inline bool samePin(const AcceptedPsPin&a,const AcceptedPsPin&b){return a.sdkRootInstanceId==b.sdkRootInstanceId&&a.hidInstanceIds==b.hidInstanceIds&&a.bindingGeneration==b.bindingGeneration;}
inline bool pin(const AcceptedPsPin&p){
    if(!p.bindingGeneration||!instance(p.sdkRootInstanceId)||p.hidInstanceIds.empty()||p.hidInstanceIds.size()>maxHids)return false;
    std::set<std::string>seen;for(const auto&h:p.hidInstanceIds)if(!instance(h)||fold(h).rfind("HID\\",0)!=0||!seen.insert(fold(h)).second)return false;return true;
}
inline bool keys(const nlohmann::json&j,std::initializer_list<const char*>required,std::initializer_list<const char*>optional={}){
    if(!j.is_object())return false;std::set<std::string>allowed;
    for(auto k:required){allowed.insert(k);if(!j.contains(k))return false;}for(auto k:optional)allowed.insert(k);
    for(const auto&item:j.items())if(!allowed.count(item.key()))return false;return true;
}
inline bool uintValue(const nlohmann::json&j,std::uint64_t max,std::uint64_t&out){
    if(!j.is_number_unsigned())return false;out=j.get<std::uint64_t>();return out>0&&out<=max;
}
inline bool bump(Record& r,std::string*e){if(r.revision==(std::numeric_limits<std::uint64_t>::max)())return fail(e,"revision-overflow");++r.revision;return true;}
}
inline bool sameOrigin(const Origin&a,const Origin&b){return detail::sameOrigin(a,b);}
inline bool validRecord(const Record&r,std::string*error=nullptr){
    if(r.schema!=schemaVersion||!detail::text(r.machineBinding,4096)||!r.revision||r.obligations.size()>maxOrigins)return detail::fail(error,"record-envelope-invalid");
    std::set<std::string>transactions,paths;std::size_t count=0;
    for(const auto&o:r.obligations){
        if(!detail::origin(o.origin,o.targetMayExist||o.acceptedPsPin.has_value())||!transactions.insert(o.origin.transactionId).second)return detail::fail(error,"origin-invalid-or-duplicate");
        if(o.paths.size()>maxPathsPerOrigin||(o.paths.empty()&&!o.acceptedPsPin&&!o.targetMayExist)||(!o.paths.empty()&&o.origin.physicalRoot.empty()))return detail::fail(error,"obligation-empty-or-over-limit");
        if(o.acceptedPsPin&&(o.origin.persona!="dualsense-edge"||!detail::pin(*o.acceptedPsPin)))return detail::fail(error,"ps-pin-invalid");
        for(const auto&p:o.paths){if(!detail::instance(p.instanceId)||(p.stage!=DispatchStage::Reserved&&p.stage!=DispatchStage::MayHaveIssued)||!paths.insert(detail::fold(p.instanceId)).second||++count>maxPaths)return detail::fail(error,"path-invalid-or-conflicting");}
    }return true;
}
inline bool encode(const Record&r,std::string&out,std::string*error=nullptr){
    try{if(!validRecord(r,error))return false;using J=nlohmann::json;J groups=J::array();
        for(const auto&g:r.obligations){const auto&o=g.origin;J paths=J::array();for(const auto&p:g.paths)paths.push_back({{"instanceId",p.instanceId},{"stage",p.stage==DispatchStage::Reserved?"pre-dispatch-reservation":"may-have-issued"}});
            J pin=nullptr;if(g.acceptedPsPin){const auto&p=*g.acceptedPsPin;pin={{"sdkRootInstanceId",p.sdkRootInstanceId},{"hidInstanceIds",p.hidInstanceIds},{"bindingGeneration",p.bindingGeneration}};}
            groups.push_back({{"origin",{{"transactionId",o.transactionId},{"parent",{{"pid",o.parent.pid},{"creationTime",o.parent.creationTime}}},{"host",{{"pid",o.host.pid},{"creationTime",o.host.creationTime}}},
                {"hostInstanceId",o.hostInstanceId},{"runId",o.runId},{"epoch",o.epoch},{"targetId",o.targetId},{"persona",o.persona},{"physicalRoot",o.physicalRoot}}},{"paths",paths},{"acceptedPsPin",pin},{"targetMayExist",g.targetMayExist}});
            // Preserve the legacy representation for unknown provenance; never promote it.
            if(o.cliWriterProtocol)groups.back()["origin"]["cliWriterProtocol"]=o.cliWriterProtocol;
        }auto bytes=J({{"schemaVersion",r.schema},{"machineBinding",r.machineBinding},{"revision",r.revision},{"obligations",groups}}).dump();if(bytes.size()>maxRecordBytes)return detail::fail(error,"record-byte-limit");out=std::move(bytes);return true;
    }catch(...){return detail::fail(error,"record-encode-failed");}
}
inline bool decode(std::string_view bytes,Record&out,std::string*error=nullptr){
    if(bytes.empty()||bytes.size()>maxRecordBytes||bytes.find('\0')!=std::string_view::npos||bytes.substr(0,3)=="\xef\xbb\xbf")return detail::fail(error,"record-bytes-invalid");
    try{using J=nlohmann::json;bool duplicate=false,depthInvalid=false;std::vector<std::set<std::string>>stack;
        auto callback=[&](int depth,J::parse_event_t event,J&parsed){if(depth>12){depthInvalid=true;throw std::runtime_error("record-depth-limit");}
            if(event==J::parse_event_t::object_start)stack.emplace_back();else if(event==J::parse_event_t::key){if(stack.empty()||!stack.back().insert(parsed.get<std::string>()).second)duplicate=true;}
            else if(event==J::parse_event_t::object_end){if(stack.empty())duplicate=true;else stack.pop_back();}return true;};
        const auto j=J::parse(bytes.begin(),bytes.end(),callback,true,false);if(duplicate||depthInvalid||!detail::keys(j,{"schemaVersion","machineBinding","revision","obligations"}))return detail::fail(error,"record-shape-invalid");
        std::uint64_t n=0;if(!detail::uintValue(j.at("schemaVersion"),schemaVersion,n)||n!=schemaVersion||!j.at("machineBinding").is_string())return detail::fail(error,"record-schema-invalid");
        Record r;r.machineBinding=j.at("machineBinding").get<std::string>();if(!detail::uintValue(j.at("revision"),(std::numeric_limits<std::uint64_t>::max)(),r.revision))return detail::fail(error,"record-revision-invalid");
        if(!j.at("obligations").is_array()||j.at("obligations").size()>maxOrigins)return detail::fail(error,"record-origins-invalid");
        auto proc=[&](const J&v,ProcessIdentity&p){std::uint64_t pid=0;if(!detail::keys(v,{"pid","creationTime"})||!detail::uintValue(v.at("pid"),(std::numeric_limits<std::uint32_t>::max)(),pid)||!v.at("creationTime").is_string())return false;p.pid=std::uint32_t(pid);p.creationTime=v.at("creationTime").get<std::string>();return detail::process(p);};
        for(const auto&v:j.at("obligations")){
            if(!detail::keys(v,{"origin","paths"},{"acceptedPsPin","targetMayExist"}))return detail::fail(error,"obligation-shape-invalid");const auto&o=v.at("origin");
            if(!detail::keys(o,{"transactionId","parent","host","hostInstanceId","runId","epoch","targetId","persona","physicalRoot"},{"cliWriterProtocol"}))return detail::fail(error,"origin-shape-invalid");
            Obligation g;
            if(o.contains("cliWriterProtocol")){const auto& protocol=o.at("cliWriterProtocol");
                if(!protocol.is_number_unsigned()||protocol.get<std::uint64_t>()>1)return detail::fail(error,"cli-writer-protocol-invalid");
                g.origin.cliWriterProtocol=protocol.get<unsigned>();}
            if(v.contains("targetMayExist")){if(!v.at("targetMayExist").is_boolean())return detail::fail(error,"target-witness-type-invalid");g.targetMayExist=v.at("targetMayExist").get<bool>();}
            if(!proc(o.at("parent"),g.origin.parent)||!proc(o.at("host"),g.origin.host))return detail::fail(error,"process-identity-invalid");
            for(const auto key:{"transactionId","hostInstanceId","runId","targetId","persona","physicalRoot"})if(!o.at(key).is_string())return detail::fail(error,"origin-text-invalid");
            g.origin.transactionId=o.at("transactionId");g.origin.hostInstanceId=o.at("hostInstanceId");g.origin.runId=o.at("runId");g.origin.targetId=o.at("targetId");g.origin.persona=o.at("persona");g.origin.physicalRoot=o.at("physicalRoot");
            if(!detail::uintValue(o.at("epoch"),(std::numeric_limits<std::uint64_t>::max)(),g.origin.epoch)||!v.at("paths").is_array()||v.at("paths").size()>maxPathsPerOrigin)return detail::fail(error,"paths-or-epoch-invalid");
            for(const auto&p:v.at("paths")){if(!detail::keys(p,{"instanceId","stage"})||!p.at("instanceId").is_string()||!p.at("stage").is_string())return detail::fail(error,"path-shape-invalid");const auto stage=p.at("stage").get<std::string>();
                if(stage!="pre-dispatch-reservation"&&stage!="may-have-issued")return detail::fail(error,"path-stage-invalid");g.paths.push_back({p.at("instanceId"),stage=="may-have-issued"?DispatchStage::MayHaveIssued:DispatchStage::Reserved});}
            if(v.contains("acceptedPsPin")&&!v.at("acceptedPsPin").is_null()){const auto&p=v.at("acceptedPsPin");if(!detail::keys(p,{"sdkRootInstanceId","hidInstanceIds","bindingGeneration"})||!p.at("sdkRootInstanceId").is_string()||!p.at("hidInstanceIds").is_array()||p.at("hidInstanceIds").size()>maxHids)return detail::fail(error,"ps-pin-shape-invalid");
                AcceptedPsPin pin;pin.sdkRootInstanceId=p.at("sdkRootInstanceId");if(!detail::uintValue(p.at("bindingGeneration"),(std::numeric_limits<std::uint64_t>::max)(),pin.bindingGeneration))return detail::fail(error,"ps-pin-generation-invalid");for(const auto&id:p.at("hidInstanceIds")){if(!id.is_string())return detail::fail(error,"ps-pin-hid-type-invalid");pin.hidInstanceIds.push_back(id);}g.acceptedPsPin=std::move(pin);}
            r.obligations.push_back(std::move(g));
        }if(!validRecord(r,error))return false;out=std::move(r);return true;
    }catch(...){return detail::fail(error,"record-decode-failed");}
}
inline bool revisionMatches(const Record&r,std::uint64_t expected,std::string*e){return validRecord(r,e)&&(r.revision==expected||detail::fail(e,"stale-revision"));}
// Caller-authenticated PREPARE dispatch evidence, even if its ACK is lost.
// This reserves historical target duty only; it never admits current output.
struct TargetCreationProof { bool dispatchBoundaryReached=false, authenticatedHostTuple=false; };
inline bool reserveTargetCreation(const Record&current,std::uint64_t expected,const Origin&origin,TargetCreationProof proof,Record&candidate,std::string*error=nullptr){
    if(!revisionMatches(current,expected,error)||!detail::origin(origin,true)||!proof.dispatchBoundaryReached||!proof.authenticatedHostTuple)return detail::fail(error,"target-reservation-proof-or-revision-invalid");
    Record next=current;for(auto&g:next.obligations)if(g.origin.transactionId==origin.transactionId){
        if(!sameOrigin(g.origin,origin))return detail::fail(error,"origin-rewrite-rejected");
        if(g.targetMayExist){candidate=current;return true;}g.targetMayExist=true;
        if(!detail::bump(next,error)||!validRecord(next,error))return false;candidate=std::move(next);return true;
    }
    next.obligations.push_back({origin,{},{},true});if(!detail::bump(next,error)||!validRecord(next,error))return false;candidate=std::move(next);return true;
}
struct ReservationProof { bool dispatchBoundaryReached=false, exactPhysicalAncestry=false, absentFromPriorHiddenSet=false; };
// One path only at the actual dispatch boundary, never a preclaimed batch tail.
inline bool reservePath(const Record&current,std::uint64_t expected,const Origin&origin,const std::string&path,ReservationProof proof,Record&candidate,std::string*error=nullptr){
    if(!revisionMatches(current,expected,error)||!detail::origin(origin)||!detail::instance(path)||!proof.dispatchBoundaryReached||!proof.exactPhysicalAncestry||!proof.absentFromPriorHiddenSet)return detail::fail(error,"reservation-authority-or-revision-invalid");
    Record next=current;Obligation*group=nullptr;
    for(auto&g:next.obligations){if(g.origin.transactionId==origin.transactionId){if(!sameOrigin(g.origin,origin))return detail::fail(error,"origin-rewrite-rejected");group=&g;}
        for(const auto&p:g.paths)if(detail::fold(p.instanceId)==detail::fold(path)){if(!sameOrigin(g.origin,origin))return detail::fail(error,"cross-origin-path-conflict");candidate=current;return true;}}
    if(!group){next.obligations.push_back({origin,{},{}});group=&next.obligations.back();}group->paths.push_back({path,DispatchStage::Reserved});
    if(!detail::bump(next,error)||!validRecord(next,error))return false;candidate=std::move(next);return true;
}
inline bool markMayHaveIssued(const Record&current,std::uint64_t expected,const Origin&origin,const std::string&path,Record&candidate,std::string*error=nullptr){
    if(!revisionMatches(current,expected,error))return false;Record next=current;for(auto&g:next.obligations)if(sameOrigin(g.origin,origin))for(auto&p:g.paths)if(detail::fold(p.instanceId)==detail::fold(path)){
        if(p.stage==DispatchStage::MayHaveIssued){candidate=current;return true;}p.stage=DispatchStage::MayHaveIssued;if(!detail::bump(next,error))return false;candidate=std::move(next);return true;}return detail::fail(error,"exact-origin-path-not-found");
}
inline bool attachAcceptedPsPin(const Record&current,std::uint64_t expected,const Origin&origin,const AcceptedPsPin&pin,Record&candidate,std::string*error=nullptr){
    if(!revisionMatches(current,expected,error)||!detail::origin(origin,true)||origin.persona!="dualsense-edge"||!detail::pin(pin))return detail::fail(error,"accepted-pin-data-invalid");
    Record next=current;for(auto&g:next.obligations)if(g.origin.transactionId==origin.transactionId){if(!sameOrigin(g.origin,origin))return detail::fail(error,"origin-rewrite-rejected");
        if(g.acceptedPsPin){if(!detail::samePin(*g.acceptedPsPin,pin))return detail::fail(error,"pin-rewrite-rejected");candidate=current;return true;}g.acceptedPsPin=pin;if(!detail::bump(next,error))return false;candidate=std::move(next);return true;}
    next.obligations.push_back({origin,{},pin});if(!detail::bump(next,error)||!validRecord(next,error))return false;candidate=std::move(next);return true;
}
// Caller separately proves target absence / path return. Neither operation does
// device work. Publish candidate only AFTER store(...).committed().
inline bool consumeOrigin(const Record&current,std::uint64_t expected,const Origin&origin,Record&candidate,std::string*error=nullptr){
    if(!revisionMatches(current,expected,error))return false;Record next=current;auto it=std::find_if(next.obligations.begin(),next.obligations.end(),[&](const auto&g){return sameOrigin(g.origin,origin);});
    if(it==next.obligations.end())return detail::fail(error,"exact-origin-not-found");next.obligations.erase(it);if(!detail::bump(next,error))return false;candidate=std::move(next);return true;
}
inline bool consumePath(const Record&current,std::uint64_t expected,const Origin&origin,const std::string&path,Record&candidate,std::string*error=nullptr){
    if(!revisionMatches(current,expected,error))return false;Record next=current;for(auto g=next.obligations.begin();g!=next.obligations.end();++g)if(sameOrigin(g->origin,origin)){
        auto p=std::find_if(g->paths.begin(),g->paths.end(),[&](const auto&v){return detail::fold(v.instanceId)==detail::fold(path);});if(p==g->paths.end())return detail::fail(error,"exact-origin-path-not-found");g->paths.erase(p);if(g->paths.empty()&&!g->acceptedPsPin&&!g->targetMayExist)next.obligations.erase(g);if(!detail::bump(next,error))return false;candidate=std::move(next);return true;}return detail::fail(error,"exact-origin-not-found");
}
inline bool mergeForCarry(const Record&current,std::uint64_t expected,const Record&inherited,Record&candidate,std::string*error=nullptr){
    if(!revisionMatches(current,expected,error)||!validRecord(inherited,error)||current.machineBinding!=inherited.machineBinding)return detail::fail(error,"carry-machine-or-record-mismatch");
    Record next=current;bool changed=false;
    for(const auto&old:inherited.obligations){auto g=std::find_if(next.obligations.begin(),next.obligations.end(),[&](const auto&v){return v.origin.transactionId==old.origin.transactionId;});
        if(g!=next.obligations.end()&&!sameOrigin(g->origin,old.origin))return detail::fail(error,"origin-rewrite-rejected");
        for(const auto&p:old.paths)for(const auto&existing:next.obligations)if(!sameOrigin(existing.origin,old.origin))for(const auto&v:existing.paths)if(detail::fold(v.instanceId)==detail::fold(p.instanceId))return detail::fail(error,"cross-origin-path-conflict");
        if(g==next.obligations.end()){next.obligations.push_back(old);changed=true;continue;}
        if(old.targetMayExist&&!g->targetMayExist){g->targetMayExist=true;changed=true;}
        if(old.acceptedPsPin){if(g->acceptedPsPin&&!detail::samePin(*g->acceptedPsPin,*old.acceptedPsPin))return detail::fail(error,"pin-rewrite-rejected");if(!g->acceptedPsPin){g->acceptedPsPin=old.acceptedPsPin;changed=true;}}
        for(const auto&p:old.paths){auto v=std::find_if(g->paths.begin(),g->paths.end(),[&](const auto&x){return detail::fold(x.instanceId)==detail::fold(p.instanceId);});if(v==g->paths.end()){g->paths.push_back(p);changed=true;}else if(v->stage==DispatchStage::Reserved&&p.stage==DispatchStage::MayHaveIssued){v->stage=p.stage;changed=true;}}
    }if(changed&&!detail::bump(next,error))return false;if(!validRecord(next,error))return false;candidate=std::move(next);return true;
}
} // namespace ymcc::targetReturn
