#pragma once
// This generation can identify its executing native literal. Earlier versions
// cannot. The first VERIFIED running marked build is the one-time migration
// boundary; no old updater fields or fabricated predecessor SHA are needed.
// Caller verifies actual code + sealed installed artifact. JSON policy alone
// never trusts a public buildId, a version string or a disk-only receipt.
#include "decky_build_recovery_contract.h"
namespace ymcc::deckyupgrade::running {
using Json=nlohmann::json;
struct Build {std::string id,sha256,version;};
inline bool build(const Build& b){detail::Version v{};return detail::hash(b.id)&&detail::hash(b.sha256)&&detail::version(b.version,v);}
inline bool valid(const Json& h){try{
    if(!detail::keys(h,{"schema","owner","exePath","current","maximumVersion","builds"})||h.at("schema")!=1||h.at("owner")!="YMCC.DeckyRunningBuildRecovery"||
       !h.at("exePath").is_string()||!detail::text(h.at("exePath").get<std::string>(),32768)||!h.at("current").is_string()||
       !h.at("builds").is_object()||h.at("builds").empty()||!h.at("builds").contains(h.at("current").get<std::string>()))return false;
    detail::Version maximum{},observed{};if(!detail::version(h.at("maximumVersion").get<std::string>(),maximum))return false;
    for(auto it=h.at("builds").begin();it!=h.at("builds").end();++it){
        const auto& x=it.value();if(!detail::keys(x,{"sha256","version","granted","retired","steamTicket","page","bpm"})||
           !build({it.key(),x.at("sha256").get<std::string>(),x.at("version").get<std::string>()})||!x.at("granted").is_boolean()||!x.at("retired").is_boolean()||!x.at("steamTicket").is_string())return false;
        detail::Version v{};detail::version(x.at("version").get<std::string>(),v);if(v>observed)observed=v;
        if(it.key()!=h.at("current").get_ref<const std::string&>()&&x.at("retired")!=true)return false;
        const auto steam=x.at("steamTicket").get<std::string>();if(!steam.empty()&&!detail::ticket(steam))return false;
        for(const bool reopen:{false,true}){const auto& slot=x.at(reopen?"bpm":"page");if(slot.is_null())continue;
            if(x.at("granted")!=true||steam.empty()||!detail::reservation(slot,reopen,steam))return false;
        }
        if(steam.empty()!= (x.at("page").is_null()&&x.at("bpm").is_null()))return false;
    }
    return observed==maximum;
}catch(...){return false;}}
inline Json observe(Json h,const Build& b,const std::string& exe){try{
    if(!build(b)||!detail::text(exe,32768))return nullptr;
    const bool initial=h.is_object()&&h.empty();
    if(!initial&&(!valid(h)||h.at("exePath")!=exe))return nullptr;
    if(initial)h={{"schema",1},{"owner","YMCC.DeckyRunningBuildRecovery"},{"exePath",exe},{"current",b.id},{"maximumVersion",b.version},{"builds",Json::object()}};
    detail::Version incoming{},highest{};detail::version(b.version,incoming);detail::version(h.at("maximumVersion").get<std::string>(),highest);
    auto& builds=h["builds"];const bool seen=builds.contains(b.id);bool knownDigest=false;
    for(auto it=builds.begin();it!=builds.end();++it){
        if(it.value().at("sha256")==b.sha256)knownDigest=true;
        if(it.key()!=b.id)it.value()["retired"]=true;
    }
    if(seen){if(builds[b.id].at("sha256")!=b.sha256||builds[b.id].at("version")!=b.version)return nullptr;}
    else builds[b.id]={{"sha256",b.sha256},{"version",b.version},{"granted",!knownDigest&&incoming>=highest},
        {"retired",incoming<highest},{"steamTicket",""},{"page",nullptr},{"bpm",nullptr}};
    if(incoming<highest)builds[b.id]["retired"]=true;
    if(incoming>highest)h["maximumVersion"]=b.version;
    h["current"]=b.id;return valid(h)?h:Json(nullptr);
}catch(...){return nullptr;}}
inline Json reserve(Json h,const Build& b,const std::string& exe,bool reopen,const std::string& steam,const Json& prior,const Json& next){try{
    if(!valid(h)||!build(b)||h.at("exePath")!=exe||h.at("current")!=b.id||!detail::ticket(steam)||!detail::ledgers(prior,next,reopen,steam))return nullptr;
    auto& x=h["builds"][b.id];const auto bound=x.at("steamTicket").get<std::string>();
    if(x.at("sha256")!=b.sha256||x.at("version")!=b.version||x.at("granted")!=true||x.at("retired")!=false||
       (!bound.empty()&&bound!=steam)||!x.at(reopen?"bpm":"page").is_null())return nullptr;
    x["steamTicket"]=steam;x[reopen?"bpm":"page"]={{"steamTicket",steam},{"priorLedger",prior},{"reservedLedger",next}};
    return valid(h)?h:Json(nullptr);
}catch(...){return nullptr;}}
// Both files carry the whole permanent history. A partial publish may leave one
// older copy; merge only monotonic facts (seen ids, retired and consumed slots).
// Never infer a fresh grant from a missing/corrupt/conflicting side.
inline Json reconcile(const Json& a,const Json& b,const Build& current,const std::string& exe){try{
    const bool ma=a.is_object()&&a.empty(),mb=b.is_object()&&b.empty();
    if(ma&&mb)return Json::object();
    if((!ma&&(!valid(a)||a.at("exePath")!=exe))||(!mb&&(!valid(b)||b.at("exePath")!=exe)))return nullptr;
    if(ma)return b;if(mb)return a;
    Json h=a;
    for(auto it=b.at("builds").begin();it!=b.at("builds").end();++it){
        if(!h["builds"].contains(it.key())){h["builds"][it.key()]=it.value();continue;}
        auto& left=h["builds"][it.key()];const auto& right=it.value();
        for(const auto* key:{"sha256","version","granted"})if(left.at(key)!=right.at(key))return nullptr;
        left["retired"]=left.at("retired").get<bool>()||right.at("retired").get<bool>();
        const auto ls=left.at("steamTicket").get<std::string>(),rs=right.at("steamTicket").get<std::string>();
        if(!ls.empty()&&!rs.empty()&&ls!=rs)return nullptr;
        if(ls.empty())left["steamTicket"]=rs;
        for(const auto* slot:{"page","bpm"}){
            if(!left.at(slot).is_null()&&!right.at(slot).is_null()&&left.at(slot)!=right.at(slot))return nullptr;
            if(left.at(slot).is_null())left[slot]=right.at(slot);
        }
    }
    if(h["builds"].contains(current.id))h["current"]=current.id;
    detail::Version maximum{};std::string highest;
    for(auto it=h["builds"].begin();it!=h["builds"].end();++it){
        detail::Version v{};detail::version(it.value().at("version").get<std::string>(),v);
        if(highest.empty()||v>maximum){maximum=v;highest=it.value().at("version").get<std::string>();}
        if(it.key()!=h.at("current").get_ref<const std::string&>())it.value()["retired"]=true;
    }
    h["maximumVersion"]=highest;return valid(h)?h:Json(nullptr);
}catch(...){return nullptr;}}
} // namespace ymcc::deckyupgrade::running
