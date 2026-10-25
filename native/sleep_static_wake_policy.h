#pragma once
// One-shot policy. No Windows APIs, threads, timers or implicit startup actions.
// The write-ahead journal is REQUIRED recovery evidence, never a verbose-log gate.
#include "json.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ymcc { namespace staticwake {
using Json = nlohmann::json;
struct Ports {
    // Missing file returns a null document; any other read/parse error fails closed.
    std::function<bool(Json&, std::string&)> load;
    // Atomically replace + flush. Returning true promises durable bytes.
    std::function<bool(const Json&, std::string&)> persist;
    std::function<bool(Json&, Json&, Json&, std::string&)> collect;
    std::function<bool(const Json&, Json&, std::string&)> read;
    std::function<bool(const Json&, const Json&, std::string&)> write;
    std::function<std::uint64_t()> monotonicMs;
    std::function<std::uint64_t()> epoch;
    std::function<bool()> sourceReady;
};
inline Json emptyJournal() {
    return {{"schema",1},{"owner","ymcc.static-wake.v1"},{"enabled",false},
        {"lastScanEpoch",0},{"entries",Json::array()},{"history",Json::array()},
        {"protectedDevices",Json::array()},{"scanErrors",Json::array()}};
}
inline bool valueValid(const Json& item,const Json& value) {
    const auto kind=item.value("kind",std::string{});
    if(kind=="devices")return value.is_boolean();
    if(kind=="timers")return value.is_number_unsigned() || (value.is_number_integer() && value.get<std::int64_t>()>=0 && value.get<std::int64_t>()<=0xffffffffLL);
    if(kind!="software"||!value.is_array()||value.size()>3)return false;
    std::set<std::string> seen;
    for(const auto& v:value) {
        if(!v.is_string())return false;
        const auto s=v.get<std::string>();
        if((s!="SYSTEM"&&s!="DISPLAY"&&s!="AWAYMODE")||!seen.insert(s).second)return false;
    }
    return true;
}
inline bool itemValid(const Json& item) {
    if(!item.is_object()||!item.contains("key")||!item["key"].is_string()||item["key"].get<std::string>().empty()||
        !item.contains("name")||!item["name"].is_string()||!item.contains("kind")||!item["kind"].is_string()||
        !item.contains("target")||!item["target"].is_object()||!item.contains("expected"))return false;
    return valueValid(item,item["expected"]);
}
class Policy {
    Ports p_;
    Json j_=emptyJournal();
    std::string error_;
    bool loaded_=false,storageFault_=false;
    std::uint64_t deadline_=0;
    bool time() const {return p_.monotonicMs()<deadline_;}
    bool save(Json next) {
        std::string e;
        if(!p_.persist(next,e)){error_="journal-persist: "+e;storageFault_=true;return false;}
        j_=std::move(next);return true;
    }
    bool entry(std::size_t i,Json record) {
        auto next=j_;next["entries"][i]=std::move(record);return save(std::move(next));
    }
    void fail(std::size_t i,const std::string& why) {
        auto r=j_["entries"][i];r["error"]=why;
        if(r.value("owned",false))r["stage"]="retry-pending";
        (void)entry(i,std::move(r));
    }
    void scanError(const std::string& why) {
        auto next=j_;next["scanErrors"].push_back({{"error",why}});(void)save(std::move(next));
    }
    Json result() const {auto s=status();return {{"ok",s["failed"].get<std::size_t>()==0&&s["restorePending"].get<std::size_t>()==0},{"status",s}};}
public:
    explicit Policy(Ports p):p_(std::move(p)) {
        try {
            Json doc;std::string e;
            if(!p_.load(doc,e)){error_="journal-load: "+e;return;}
            if(doc.is_null()){loaded_=true;return;}
            if(!doc.is_object()||doc.value("schema",0)!=1||doc.value("owner",std::string{})!="ymcc.static-wake.v1"||
                !doc.contains("enabled")||!doc["enabled"].is_boolean()||!doc.contains("lastScanEpoch")||!doc["lastScanEpoch"].is_number_integer()||
                !doc.contains("entries")||!doc["entries"].is_array()||!doc.contains("history")||!doc["history"].is_array()||
                !doc.contains("protectedDevices")||!doc["protectedDevices"].is_array()||!doc.contains("scanErrors")||!doc["scanErrors"].is_array()) {
                error_="invalid-journal-schema";return;
            }
            std::set<std::string> keys;
            for(const auto& r:doc["entries"]) {
                if(!itemValid(r)||!r.contains("baseline")||!valueValid(r,r["baseline"])||!r.contains("owned")||!r["owned"].is_boolean()||
                    !r.contains("stage")||!r["stage"].is_string()||!r.contains("error")||!r["error"].is_string()||
                    !keys.insert(r["key"].get<std::string>()).second){error_="invalid-journal-entry";return;}
            }
            j_=std::move(doc);loaded_=true;
        }catch(const std::exception& e){error_=std::string("journal-exception: ")+e.what();}
    }
    Json status() const {
        Json s={{"enabled",j_.value("enabled",false)},{"phase","idle"},{"lastScanEpoch",j_.value("lastScanEpoch",std::uint64_t{0})},
            {"devices",Json::array()},{"software",Json::array()},{"timers",Json::array()},
            {"protectedDevices",j_["protectedDevices"]},{"failed",j_["scanErrors"]},{"restorePending",Json::array()},
            {"diagnosticSourceReady",p_.sourceReady?p_.sourceReady():false},{"lastError",error_},{"externalChanges",Json::array()}};
        for(const auto& r:j_["entries"]) {
            s[r["kind"].get<std::string>()].push_back(r);
            if(!r.value("error",std::string{}).empty())s["failed"].push_back({{"key",r["key"]},{"error",r["error"]}});
            if(r.value("owned",false)&&(!j_.value("enabled",false)||r.value("stage",std::string{})!="applied"))s["restorePending"].push_back(r["key"]);
            if(r.value("stage",std::string{})=="external-change")s["externalChanges"].push_back(r["key"]);
        }
        if(!error_.empty())s["failed"].push_back({{"error",error_}});
        if(!s["restorePending"].empty())s["phase"]=j_.value("enabled",false)?"partial":"restore-pending";
        else if(!s["failed"].empty())s["phase"]="partial";
        else if(j_.value("enabled",false))s["phase"]="applied";
        if(s["lastError"].get<std::string>().empty()&&!s["failed"].empty())s["lastError"]=s["failed"].back().value("error",std::string{});
        s["items"]={{"devices",s["devices"]},{"software",s["software"]},{"timers",s["timers"]}};
        for(const auto* kind:{"devices","software","timers"}) {
            std::size_t applied=0;
            for(const auto& r:s[kind])if(r.value("stage",std::string{})=="applied"||r.value("stage",std::string{})=="preexisting")++applied;
            s[kind]=applied;
        }
        s["failures"]=s["failed"];s["failed"]=s["failures"].size();
        s["restorePendingItems"]=s["restorePending"];s["restorePending"]=s["restorePendingItems"].size();
        s["protectedDeviceDetails"]=s["protectedDevices"];s["protectedDevices"]=s["protectedDeviceDetails"].size();
        return s;
    }
    Json scan(std::uint64_t budgetMs=180000) {
        deadline_=p_.monotonicMs()+budgetMs;
        if(!loaded_)return result();
        // OFF failures must settle before a fresh ownership generation starts.
        if(!j_.value("enabled",false)) {
            for(const auto& r:j_["entries"])if(r.value("owned",false)){error_="restore-pending: retry OFF before new scan";return result();}
            auto next=j_;
            if(!next["entries"].empty())next["history"].push_back({{"lastScanEpoch",next["lastScanEpoch"]},{"entries",next["entries"]}});
            next["entries"]=Json::array();next["enabled"]=true;
            if(!save(std::move(next)))return result();
        }
        auto next=j_;next["lastScanEpoch"]=p_.epoch();next["scanErrors"]=Json::array();next["protectedDevices"]=Json::array();
        if(!save(std::move(next)))return result();
        Json items=Json::array(),protectedItems=Json::array(),errors=Json::array();std::string e;
        const bool complete=p_.collect(items,protectedItems,errors,e);
        next=j_;next["protectedDevices"]=std::move(protectedItems);next["scanErrors"]=std::move(errors);
        if(!complete)next["scanErrors"].push_back({{"error",e.empty()?"incomplete-snapshot":e}});
        if(!save(std::move(next)))return result();
        std::set<std::string> seen;
        for(const auto& item:items) {
            if(storageFault_)break;
            if(!time()){scanError("scan-deadline-180s");break;}
            if(!itemValid(item)||!seen.insert(item.value("key",std::string{})).second){scanError("invalid-or-duplicate-candidate");continue;}
            std::size_t i=0;
            for(;i<j_["entries"].size();++i)if(j_["entries"][i]["key"]==item["key"])break;
            Json current;
            if(!p_.read(item,current,e)||!valueValid(item,current)) {
                if(i<j_["entries"].size())fail(i,"read: "+e);else scanError(item["key"].get<std::string>()+": read: "+e);
                continue;
            }
            if(i==j_["entries"].size()) {
                auto r=item;r["baseline"]=current;r["owned"]=false;r["stage"]="observed";r["error"]="";
                next=j_;next["entries"].push_back(std::move(r));
                if(!save(std::move(next)))break; // Baseline must precede EVERY first write.
            }
            auto r=j_["entries"][i];
            // Stable identity/expected contract cannot silently change on rescan.
            if(r["target"]!=item["target"]||r["expected"]!=item["expected"]){fail(i,"candidate-identity-changed");continue;}
            const auto stage=r.value("stage",std::string{});
            if(stage=="external-change")continue; // No automatic reacquisition in this generation.
            if(current==r["expected"]) {
                r["error"]="";r["stage"]=r.value("owned",false)?"applied":"preexisting";
                if(!entry(i,std::move(r)))break;
                continue;
            }
            if((r.value("owned",false)&&stage=="applied")||current!=r["baseline"]||stage=="preexisting") {
                r["owned"]=false;r["stage"]="external-change";r["error"]="external-change-preserved";
                if(!entry(i,std::move(r)))break;
                continue;
            }
            if(!time()){scanError("scan-deadline-180s");break;}
            r["owned"]=true;r["stage"]="prepared";r["error"]="";
            if(!entry(i,r))break; // REQUIRED durable intent; readback failure remains retryable.
            // Recheck immediately after durable preparation, including retries.
            Json before;
            if(!p_.read(item,before,e)){fail(i,"prewrite-read: "+e);continue;}
            if(before!=current) {
                r["owned"]=false;r["stage"]="external-change";r["error"]="prewrite-race-preserved";
                if(!entry(i,std::move(r)))break;
                continue;
            }
            if(!time()){fail(i,"scan-deadline-180s");break;}
            const bool wrote=p_.write(item,r["expected"],e);const auto writeError=e;
            Json after;const bool read=p_.read(item,after,e);
            if(!wrote||!read||after!=r["expected"]) {
                fail(i,!wrote?"write: "+writeError:(!read?"readback: "+e:"readback-mismatch"));continue;
            }
            r["stage"]="applied";r["error"]="";if(!entry(i,std::move(r)))break;
        }
        return result();
    }
    Json restore(std::uint64_t budgetMs=180000) {
        deadline_=p_.monotonicMs()+budgetMs;
        if(!loaded_)return result();
        auto next=j_;next["enabled"]=false;next["scanErrors"]=Json::array();
        if(!save(std::move(next)))return result();
        for(std::size_t i=0;i<j_["entries"].size();++i) {
            if(storageFault_)break;
            auto r=j_["entries"][i];
            if(!r.value("owned",false))continue;
            if(!time()){error_="restore-deadline-180s";break;}
            Json current;std::string e;
            if(!p_.read(r,current,e)){fail(i,"restore-read: "+e);continue;}
            if(current!=r["expected"]) {
                r["owned"]=false;r["stage"]=current==r["baseline"]?"restored":"external-change";
                r["error"]="";if(!entry(i,std::move(r)))break;continue;
            }
            r["stage"]="restore-prepared";r["error"]="";
            if(!entry(i,r))break;
            Json before;
            if(!p_.read(r,before,e)){fail(i,"restore-prewrite-read: "+e);continue;}
            if(before!=r["expected"]) {
                r["owned"]=false;r["stage"]="external-change";if(!entry(i,std::move(r)))break;continue;
            }
            if(!time()){fail(i,"restore-deadline-180s");break;}
            const bool wrote=p_.write(r,r["baseline"],e);const auto writeError=e;
            Json after;const bool read=p_.read(r,after,e);
            if(!wrote||!read||after!=r["baseline"]) {fail(i,!wrote?"restore-write: "+writeError:(!read?"restore-readback: "+e:"restore-readback-mismatch"));continue;}
            r["owned"]=false;r["stage"]="restored";r["error"]="";if(!entry(i,std::move(r)))break;
        }
        return result();
    }
};
}} // namespace ymcc::staticwake
