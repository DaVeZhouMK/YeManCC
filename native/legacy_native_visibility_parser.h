#pragma once
#include "native_refresh_writer_record.h"
#include <array>

// Pure historical extraction; no filesystem, Windows, process or device IO.
// Caller supplies whole native-lifecycle.log/.1 byte views in oldest->newest order.
// No timestamp sorting, partial reads, arbitrary log discovery or salvaging bad tails.
namespace ymcc::legacyNativeVisibility {
using Visibility=nativeRefreshWriter::Visibility;
using J=nlohmann::json;
inline constexpr std::size_t maxFiles=2, maxBytes=8*1024*1024,
    maxLines=32768, maxLineBytes=64*1024, maxDepth=12;
enum class Status { Rejected, NoCandidate, Candidate };
struct Result {
    Status status=Status::NoCandidate;
    std::optional<Visibility> visibility;
    std::string reason;
    std::size_t fileIndex=0,lineNumber=0,linesRead=0;
    std::size_t candidateFileIndex=0,candidateLineNumber=0;
    // MayHaveIssued below is conservative migration debt, NOT a hide API receipt.
    // Never pass this directly to cleanup: independently acquire exclusive owner,
    // prove exact parent dead/current physical root+locations+target absence,
    // and persist under the production schema2 transaction before any action.
    bool legacySnapshotDeltaOnly=true;
    bool hideApiReceiptObserved=false;
    bool hardwareSuccessProven=false;
};
namespace detail {
inline bool boolean(const J& j,const char* k,bool value){
    return j.contains(k)&&j.at(k).is_boolean()&&j.at(k).get<bool>()==value;
}
inline bool count(const J& j,const char* k,std::size_t expected){
    if(!j.contains(k)||!j.at(k).is_number_unsigned())return false;
    return j.at(k).get<std::uint64_t>()==expected;
}
inline bool string(const J& j,const char* k,std::string& out){
    if(!j.contains(k)||!j.at(k).is_string())return false;
    out=j.at(k).get<std::string>();return true;
}
inline bool locations(const std::vector<std::string>& values,std::set<std::string>& out){
    if(values.empty()||values.size()>nativeRefreshWriter::maxVisibilityLocations)return false;
    for(const auto& v:values)if(!nativeRefreshWriter::detail::text(v,1024)||
        !out.insert(targetReturn::detail::fold(v)).second)return false;
    return true;
}
inline bool identity(const J& line,const J& writer,Visibility& v){
    std::uint64_t pid=0,parent=0;
    if(!line.contains("pid")||!writer.contains("parentPid")||
        !targetReturn::detail::uintValue(line.at("pid"),UINT32_MAX,pid)||
        !targetReturn::detail::uintValue(writer.at("parentPid"),UINT32_MAX,parent)||pid!=parent||
        !string(writer,"attemptId",v.transactionId)||v.transactionId.size()>nativeRefreshWriter::maxAttemptIdBytes)return false;
    // Producer format: nativePid-nativeBirthFileTime-tick-sequence. The birth
    // comes from this explicit producer identity, never pid alone or log time.
    std::array<std::string_view,4> parts;
    std::string_view rest=v.transactionId;
    for(std::size_t i=0;i<4;++i){
        const auto dash=rest.find('-');
        if((i<3&&dash==std::string_view::npos)||(i==3&&dash!=std::string_view::npos))return false;
        parts[i]=rest.substr(0,dash);
        if(!targetReturn::detail::decimal(parts[i]))return false;
        if(i<3)rest.remove_prefix(dash+1);
    }
    if(parts[0]!=std::to_string(pid))return false;
    v.parent={static_cast<std::uint32_t>(pid),std::string(parts[1])};
    if(writer.contains("parentCreationTime")&&(!writer.at("parentCreationTime").is_string()||
        writer.at("parentCreationTime").get<std::string>()!=v.parent.creationTime))return false;
    std::string fence,job;
    return targetReturn::detail::process(v.parent)&&boolean(writer,"reserved",true)&&
        string(writer,"writerFence",fence)&&fence==nativeRefreshWriter::kWriterFence&&
        string(writer,"jobName",job)&&job==std::string(nativeRefreshWriter::jobPrefix)+v.transactionId;
}
inline bool snapshot(const J& j,std::size_t selected,std::size_t owned,const char* configuration){
    std::string config;
    return j.is_object()&&count(j,"schema",1)&&boolean(j,"snapshotRead",true)&&
        boolean(j,"selectedPathsComplete",true)&&count(j,"selectedHiddenCount",selected)&&
        count(j,"ownedStillHiddenCount",owned)&&count(j,"unownedSelectedHiddenCount",0)&&
        string(j,"selectedConfiguration",config)&&config==configuration;
}
inline J parse(std::string_view bytes){
    bool duplicate=false;std::vector<std::set<std::string>> objects;
    auto callback=[&](int depth,J::parse_event_t event,J& value){
        if(depth>static_cast<int>(maxDepth))throw std::runtime_error("depth-limit");
        if(event==J::parse_event_t::object_start)objects.emplace_back();
        else if(event==J::parse_event_t::key){
            if(objects.empty()||!objects.back().insert(value.get<std::string>()).second)duplicate=true;
        }else if(event==J::parse_event_t::object_end){
            if(objects.empty())duplicate=true;else objects.pop_back();
        }return true;
    };
    J j=J::parse(bytes.begin(),bytes.end(),callback,true,false);
    if(duplicate||!objects.empty()||!j.is_object())throw std::runtime_error("object-or-duplicate-key");
    return j;
}
}
inline Result extract(const std::vector<std::string_view>& oldestToNewest,
    std::string_view expectedRoot,const std::vector<std::string>& expectedLocations){
    Result result;
    auto reject=[&](const char* why){result.status=Status::Rejected;result.visibility.reset();result.reason=why;return result;};
    try {
        std::set<std::string> wantedLocations;
        if(!nativeRefreshWriter::detail::visibilityInstance(expectedRoot,true)||
            !detail::locations(expectedLocations,wantedLocations)||oldestToNewest.empty()||
            oldestToNewest.size()>maxFiles)return reject("expected-scope-or-file-count-invalid");
        const auto root=targetReturn::detail::fold(expectedRoot);
        std::size_t bytes=0;
        for(const auto file:oldestToNewest){
            if(file.size()>maxBytes-bytes)return reject("total-byte-limit");bytes+=file.size();
        }
        // A producer birth/transaction change retires its predecessor. No carry
        // forward of snapshots/paths, and no resurrection from delayed old lines.
        std::string owner;std::set<std::string> retiredOwners;
        for(std::size_t f=0;f<oldestToNewest.size();++f){
            auto file=oldestToNewest[f];std::size_t offset=0,line=0;
            while(offset<file.size()){
                result.fileIndex=f;result.lineNumber=++line;
                if(++result.linesRead>maxLines)return reject("line-count-limit");
                const auto end=file.find('\n',offset);
                if(end==std::string_view::npos)return reject("unterminated-line");
                auto bytesView=file.substr(offset,end-offset);offset=end+1;
                if(bytesView.size()>maxLineBytes)return reject("line-byte-limit");
                if(!bytesView.empty()&&bytesView.back()=='\r')bytesView.remove_suffix(1);
                if(bytesView.empty()||bytesView.find('\0')!=std::string_view::npos||bytesView.substr(0,3)=="\xef\xbb\xbf")return reject("line-bytes-invalid");
                const auto j=detail::parse(bytesView);
                std::string event,source;
                if(!detail::string(j,"event",event)||event!="native-foreground-isolation")continue;
                if(!detail::string(j,"sourceRoot",source))return reject("native-root-missing");
                if(targetReturn::detail::fold(source)!=root)continue;
                // A later observer with no owner authority cannot erase old debt.
                // Validate independently before replacing/revoking any candidate.
                Visibility v;v.physicalRoot=source;
                std::string family,phase;
                if(!detail::string(j,"family",family)||family!="gpd"||!detail::string(j,"phase",phase)||
                    !j.contains("locationPaths")||!j.at("locationPaths").is_array()||
                    j.at("locationPaths").size()>nativeRefreshWriter::maxVisibilityLocations||
                    !j.contains("ownedPaths")||!j.at("ownedPaths").is_array()||
                    j.at("ownedPaths").size()>nativeRefreshWriter::maxVisibilityPaths||
                    !j.contains("configurationReturned")||!j.at("configurationReturned").is_boolean()||
                    !j.contains("postHideCycle")||!j.at("postHideCycle").is_boolean())continue;
                bool valid=true;for(const auto& location:j.at("locationPaths")){
                    if(!location.is_string()){valid=false;break;}v.locationPaths.push_back(location.get<std::string>());
                }
                std::set<std::string> observedLocations;
                if(!valid||!detail::locations(v.locationPaths,observedLocations)||observedLocations!=wantedLocations)continue;
                if(!j.contains("nativeRefreshWriter")||!j.at("nativeRefreshWriter").is_object())continue;
                const auto& writer=j.at("nativeRefreshWriter");
                if(!detail::identity(j,writer,v))continue;
                if(retiredOwners.count(v.transactionId))continue;
                // A complete exact writer identity establishes a new owner epoch,
                // even if its return/snapshot cannot create migration authority.
                // Null/no-identity observers never reach this boundary.
                if(owner!=v.transactionId){
                    if(!owner.empty())retiredOwners.insert(owner);
                    owner=v.transactionId;result.visibility.reset();result.status=Status::NoCandidate;
                    result.reason="new-exact-owner-invalidates-previous-debt";
                }
                std::set<std::string> paths;bool hasRoot=false;
                for(const auto& path:j.at("ownedPaths")){
                    if(!path.is_string()){valid=false;break;}
                    const auto id=path.get<std::string>(),fold=targetReturn::detail::fold(id);
                    if(!nativeRefreshWriter::detail::visibilityInstance(id)||!paths.insert(fold).second||
                        (fold.rfind("USB\\",0)==0&&fold!=root)){valid=false;break;}
                    hasRoot=hasRoot||fold==root;
                    v.paths.push_back({id,nativeRefreshWriter::VisibilityPathPhase::MayHaveIssued});
                }
                if(!valid||!nativeRefreshWriter::detail::visibility(v))continue;
                // Only complete typed native envelopes reach revocation. Invalid
                // envelopes still fail closed above, never fall back to old debt.
                if(detail::boolean(j,"configurationReturned",true)||phase=="restored"||phase=="restored-old-config"||
                    ((phase=="owner-cleared"||phase=="clear-owner")&&v.paths.empty())){
                    retiredOwners.insert(v.transactionId);
                    if(owner==v.transactionId){result.visibility.reset();result.status=Status::NoCandidate;
                        result.reason="latest-native-owner-returned-or-cleared";}
                    continue;
                }
                std::string writerPhase;
                if(v.paths.empty()||!hasRoot||!detail::boolean(j,"postHideCycle",false)||
                    !detail::string(writer,"phase",writerPhase)||
                    !((writerPhase=="created-in-exact-job"&&detail::boolean(writer,"settled",false))||
                      (writerPhase=="durably-settled"&&detail::boolean(writer,"settled",true)))||
                    (phase!="handoff-preserved"&&phase!="virtual-work-deferred"&&phase!="refresh-failed")||
                    !j.contains("beforeConfigurationEvidence")||!j.contains("hiddenConfigurationEvidence")||
                    !detail::snapshot(j.at("beforeConfigurationEvidence"),0,0,"no-filter-for-selected-paths")||
                    !detail::snapshot(j.at("hiddenConfigurationEvidence"),v.paths.size(),v.paths.size(),"hidden-from-unlisted-readers"))continue;
                
                result.visibility=std::move(v);result.status=Status::Candidate;result.reason="legacy-owned-snapshot-delta-no-hide-api-receipt";
                result.candidateFileIndex=f;result.candidateLineNumber=line;
            }
        }
        return result;
    }catch(...){return reject("invalid-json-or-extraction-error");}
}
}
