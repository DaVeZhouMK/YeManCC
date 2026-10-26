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
struct SoftwareSnapshotFailure {std::string error;std::uint64_t attempts=0;};
// Explicit global-query failure scope. An ordinary item validation/write failure
// must not trip this circuit. Never retains successful values. No clock/thread.
class SoftwareSnapshotCircuit {
    SoftwareSnapshotFailure state_;
    bool stopped_=false;
public:
    void reset() {state_={};stopped_=false;}
    bool failure(SoftwareSnapshotFailure& out) const {if(!stopped_)return false;out=state_;return true;}
    // REQUIRED ordinary end-of-operation evidence; never performs a query or reset.
    Json evidence() const {
        Json out={{"loadAttempts",state_.attempts},{"stoppedOnGlobalFailure",stopped_},
            {"attemptScope","loader-invocations-including-caller-validation;not-powercfg-process-count"}};
        if(stopped_)out["firstFailure"]=state_.error;
        return out;
    }
    template<class Load> bool query(Load load,std::string& error) {
        if(stopped_){error=state_.error;return false;}
        ++state_.attempts;std::string why;
        if(load(why)){error.clear();return true;}
        stopped_=true;state_.error=why.empty()?"global-software-snapshot-unavailable":std::move(why);
        error=state_.error;return false;
    }
};
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
    std::function<bool()> canceled; // optional, cheap read-only generation/sleep predicate
    // Planning only. Never substitutes for fresh prewrite/readback or OFF reads.
    std::function<bool(const Json&, Json&, std::string&)> observe;
    std::function<void()> beginOperation; // invalidates observations + failure circuit
    // Explicitly global snapshot failures only; absent means ordinary item errors.
    std::function<bool(SoftwareSnapshotFailure&)> softwareSnapshotFailure;
};
// Bounded observations, with no timer, worker or state shared across operations.
// Values and failures may be reused for at most 64 items / 1000ms. The caller
// must still obtain a fresh read before any write and refresh after every write.
// Kept here so the injected mock and native ports consume the same cache code.
template<class T> class ObservationCache {
    T value_{};
    std::string error_;
    std::uint64_t sampledAt_=0;
    unsigned uses_=0;
    bool present_=false,ok_=false;
public:
    static constexpr std::uint64_t maxAgeMs=1000;
    static constexpr unsigned maxUses=64;
    void invalidate() {present_=false;ok_=false;uses_=0;value_=T{};error_.clear();}
    template<class Load> bool refresh(std::uint64_t now,Load load,std::string& error) {
        invalidate();T next{};std::string why;
        const bool ok=load(next,why);
        value_=std::move(next);error_=std::move(why);sampledAt_=now;uses_=1;present_=true;ok_=ok;
        if(!ok_&&error_.empty())error_="observation-snapshot-unavailable";
        error=ok_?std::string{}:error_;return ok_;
    }
    template<class Load> bool observe(std::uint64_t now,Load load,std::string& error) {
        if(!present_||now<sampledAt_||now-sampledAt_>=maxAgeMs||uses_>=maxUses)return refresh(now,std::move(load),error);
        ++uses_;error=ok_?std::string{}:error_;return ok_;
    }
    const T& value() const {return value_;}
};
inline bool fanHostProcessName(std::string name) {
    for(auto& c:name)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return name=="yemanfanhost.exe";
}
inline bool protectedFanProcess(const Json& item) {
    if(!item.is_object()||item.value("kind",std::string{})!="software"||!item.contains("target")||!item["target"].is_object())return false;
    return item["target"].value("type",std::string{})=="PROCESS"&&fanHostProcessName(item["target"].value("name",std::string{}));
}
// Ordering only: keep every existing caller, readback and ownership gate.
// Timers/devices lead; Steam precedes the large ordinary software tail so it
// cannot be starved by process enumeration order within the one-shot budget.
inline bool preferredSoftwareCaller(const Json& item) {
    if(!item.is_object()||item.value("kind",std::string{})!="software"||
        !item.contains("target")||!item["target"].is_object())return false;
    const auto& target=item["target"];const auto type=target.value("type",std::string{});
    auto name=target.value("name",std::string{});
    for(auto& c:name)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return type=="PROCESS"?(name=="steam.exe"||name=="steamwebhelper.exe"||name=="steamservice.exe"):
        type=="SERVICE"&&name=="steam client service";
}
inline void orderScanCandidates(Json& items) {
    const auto afterTimers=std::stable_partition(items.begin(),items.end(),[](const Json& item){return item.value("kind",std::string{})=="timers";});
    const auto afterDevices=std::stable_partition(afterTimers,items.end(),[](const Json& item){return item.value("kind",std::string{})=="devices";});
    std::stable_partition(afterDevices,items.end(),preferredSoftwareCaller);
}
inline Json emptyJournal() {
    return {{"schema",1},{"owner","ymcc.static-wake.v1"},{"enabled",false},
        {"lastScanEpoch",0},{"scanInProgress",false},{"entries",Json::array()},{"history",Json::array()},
        {"protectedDevices",Json::array()},{"protectedSoftware",Json::array()},{"scanErrors",Json::array()}};
}
inline bool valueValid(const Json& item,const Json& value) {
    const auto kind=item.value("kind",std::string{});
    if(kind=="devices")return value.is_boolean();
    if(kind=="timers")return value.is_number_unsigned() ? value.get<std::uint64_t>()<=0xffffffffULL : (value.is_number_integer() && value.get<std::int64_t>()>=0 && value.get<std::int64_t>()<=0xffffffffLL);
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
    std::vector<std::string> softwarePlanned_;
    std::size_t softwareVisited_=0;
    bool softwareStopped_=false;
    void planSoftware(const Json& items,bool ownedOnly=false) {
        softwarePlanned_.clear();softwareVisited_=0;softwareStopped_=false;std::set<std::string> keys;
        for(const auto& item:items)if(itemValid(item)&&item["kind"]=="software"&&(!ownedOnly||item.value("owned",false))&&keys.insert(item["key"].get<std::string>()).second)
            softwarePlanned_.push_back(item["key"].get<std::string>());
    }
    bool globalSoftwareFailure(const Json& item,const char* phase,const std::string& itemError={}) {
        if(item["kind"]!="software"||!p_.softwareSnapshotFailure)return false;
        SoftwareSnapshotFailure failure;if(!p_.softwareSnapshotFailure(failure))return false;
        if(softwareStopped_)return true;
        softwareStopped_=true;
        const auto unvisited=softwarePlanned_.size()>softwareVisited_?softwarePlanned_.size()-softwareVisited_:0;
        // REQUIRED ordinary recovery/coverage evidence: one global failure record,
        // not one false write-failure and whole-journal flush per affected caller.
        Json aggregate={{"kind","software"},{"scope","global-software-snapshot"},{"error","global-software-snapshot: "+failure.error},
            {"queryAttempts",failure.attempts},{"phase",phase},{"firstAffectedKey",item["key"]},
            {"planned",softwarePlanned_.size()},{"visited",softwareVisited_},{"unvisited",unvisited},{"affected",unvisited+1},
            {"coverage","UNKNOWN"},{"writeFailuresClaimed",false},{"retry","next-independent-operation"}};
        if(!itemError.empty())aggregate["itemError"]=itemError;
        auto next=j_;next["scanErrors"].push_back(std::move(aggregate));(void)save(std::move(next));
        return true;
    }
    bool time() const {return (!p_.canceled||!p_.canceled())&&p_.monotonicMs()<deadline_;}
    bool save(Json next) {
        if(!time()){error_="operation-canceled-or-deadline";storageFault_=true;return false;}
        std::string e;
        if(!p_.persist(next,e)){error_="journal-persist: "+e;storageFault_=true;return false;}
        j_=std::move(next);return true;
    }
    bool entry(std::size_t i,Json record) {
        // Re-scan unchanged items without another whole-journal disk flush.
        // A changed baseline/intent/result still must be durable before progress.
        if(j_["entries"][i]==record) {
            if(!time()){error_="operation-canceled-or-deadline";storageFault_=true;return false;}
            return true;
        }
        auto next=j_;next["entries"][i]=std::move(record);return save(std::move(next));
    }
    void fail(std::size_t i,const std::string& why) {
        auto r=j_["entries"][i];r["error"]=why;
        // A failed observation must never erase earlier successful apply proof:
        // later seeing baseline may be an external edit, not permission to retry.
        if(r.value("owned",false)) {
            if(r.value("stage",std::string{})=="applied")r["everApplied"]=true;
            r["stage"]="retry-pending";
        }
        (void)entry(i,std::move(r));
    }
    void scanError(const std::string& why) {
        auto next=j_;next["scanErrors"].push_back({{"error",why}});(void)save(std::move(next));
    }
    // A successful disable removes a device from wake_armed. Old uncertain
    // results must still get a fresh read, without treating absence as success
    // or as authority for another write. Preserve baseline/ownership for OFF.
    bool reconcileAbsentOwnedDevices(const Json& candidates) {
        if(storageFault_)return false;
        if(!j_.value("enabled",false))return true;
        std::set<std::string> listed;
        for(const auto& item:candidates)if(itemValid(item))listed.insert(item["key"].get<std::string>());
        for(std::size_t i=0;i<j_["entries"].size();++i) {
            const auto item=j_["entries"][i];const auto stage=item.value("stage",std::string{});
            if(item["kind"]!="devices"||!item.value("owned",false)||
                (stage!="retry-pending"&&stage!="prepared")||item["baseline"]!=true||item["expected"]!=false||
                listed.count(item["key"].get<std::string>()))continue;
            if(!time()){error_="scan-canceled-or-deadline";return false;}
            const auto verificationEpoch=p_.epoch();
            Json current;std::string why;const bool read=p_.read(item,current,why);
            if(!time()){error_="scan-canceled-or-deadline";return false;}
            auto checked=item;checked["lastVerificationEpoch"]=verificationEpoch;
            if(!read||!valueValid(item,current)) {
                checked["stage"]="retry-pending";
                checked["error"]="retained-device-read: "+(why.empty()?"unconfirmed-state":why);
                checked["lastVerificationResult"]=read?"invalid-state":"read-failed";
            } else if(current==item["expected"]) {
                checked["stage"]="applied";checked["error"]="";
                checked["lastVerificationResult"]="verified";
            } else {
                checked["stage"]="retry-pending";checked["error"]="retained-device-not-at-expected-state";
                checked["lastVerificationResult"]="mismatch";
            }
            // Same durable result update as before; no extra flush or device
            // write. Newly protected targets retain their original OFF owner.
            if(!entry(i,std::move(checked)))return false;
        }
        return true;
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
            if(!doc.contains("protectedSoftware"))doc["protectedSoftware"]=Json::array();
            if(!doc["protectedSoftware"].is_array()){error_="invalid-journal-software-protection";return;}
            if(!doc.contains("scanInProgress"))doc["scanInProgress"]=false;
            if(!doc["scanInProgress"].is_boolean()){error_="invalid-journal-scan-marker";return;}
            std::set<std::string> keys;
            for(const auto& r:doc["entries"]) {
                if(!itemValid(r)||!r.contains("baseline")||!valueValid(r,r["baseline"])||!r.contains("owned")||!r["owned"].is_boolean()||
                    !r.contains("stage")||!r["stage"].is_string()||
                    (r.contains("everApplied")&&!r["everApplied"].is_boolean())||
                    !std::set<std::string>{"observed","prepared","applied","preexisting","retry-pending","external-change","restore-prepared","restored"}.count(r["stage"].get<std::string>())||!r.contains("error")||!r["error"].is_string()||
                    !keys.insert(r["key"].get<std::string>()).second){error_="invalid-journal-entry";return;}
            }
            j_=std::move(doc);loaded_=true;
        }catch(const std::exception& e){error_=std::string("journal-exception: ")+e.what();}
    }
    Json status() const {
        Json s={{"enabled",j_.value("enabled",false)},{"phase","idle"},{"lastScanEpoch",j_.value("lastScanEpoch",std::uint64_t{0})},
            {"devices",Json::array()},{"software",Json::array()},{"timers",Json::array()},
            {"protectedDevices",j_["protectedDevices"]},{"failed",j_["scanErrors"]},{"restorePending",Json::array()},
            {"protectedSoftwareDetails",j_["protectedSoftware"]},{"diagnosticSourceReady",p_.sourceReady?p_.sourceReady():false},{"lastError",error_},{"externalChanges",Json::array()}};
        for(const auto& r:j_["entries"]) {
            s[r["kind"].get<std::string>()].push_back(r);
            if(!r.value("error",std::string{}).empty())s["failed"].push_back({{"key",r["key"]},{"error",r["error"]}});
            if(r.value("owned",false)&&(!j_.value("enabled",false)||r.value("stage",std::string{})!="applied"))s["restorePending"].push_back(r["key"]);
            if(r.value("stage",std::string{})=="external-change")s["externalChanges"].push_back(r["key"]);
        }
        if(j_.value("scanInProgress",false))s["failed"].push_back({{"error","scan-incomplete-or-interrupted"}});
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
        if(!time()){error_="scan-canceled-or-deadline";return result();}
        if(p_.beginOperation)p_.beginOperation();
        // OFF failures must settle before a fresh ownership generation starts.
        if(!j_.value("enabled",false)) {
            for(const auto& r:j_["entries"])if(r.value("owned",false)){error_="restore-pending: retry OFF before new scan";return result();}
            auto next=j_;
            if(!next["entries"].empty())next["history"].push_back({{"lastScanEpoch",next["lastScanEpoch"]},{"entries",next["entries"]}});
            next["entries"]=Json::array();next["enabled"]=true;next["scanInProgress"]=true;
            if(!save(std::move(next)))return result();
        }
        auto next=j_;next["lastScanEpoch"]=p_.epoch();next["scanInProgress"]=true;next["scanErrors"]=Json::array();next["protectedDevices"]=Json::array();next["protectedSoftware"]=Json::array();
        if(!save(std::move(next)))return result();
        Json items=Json::array(),protectedItems=Json::array(),errors=Json::array();std::string e;
        const bool complete=p_.collect(items,protectedItems,errors,e);
        if(!time()){error_="scan-canceled-or-deadline";return result();}
        next=j_;next["protectedDevices"]=Json::array();next["protectedSoftware"]=Json::array();next["scanErrors"]=std::move(errors);
        for(const auto& protectedItem:protectedItems)next[protectedItem.value("kind",std::string{})=="software"?"protectedSoftware":"protectedDevices"].push_back(protectedItem);
        Json filtered=Json::array();
        for(const auto& item:items) {
            if(protectedFanProcess(item))next["protectedSoftware"].push_back({{"kind","software"},{"name",item.value("name",std::string{})},{"reason","ymcc-fanhost-owner"}});
            else filtered.push_back(item);
        }
        items=std::move(filtered);
        if(!complete)next["scanErrors"].push_back({{"error",e.empty()?"incomplete-snapshot":e}});
        if(!save(std::move(next)))return result();
        planSoftware(items);
        if(!reconcileAbsentOwnedDevices(items))return result();
        std::set<std::string> seen;
        for(const auto& item:items) {
            if(storageFault_)break;
            if(!time()){scanError("scan-canceled-or-deadline");break;}
            if(!itemValid(item)||!seen.insert(item.value("key",std::string{})).second){scanError("invalid-or-duplicate-candidate");continue;}
            if(item["kind"]=="software") {if(softwareStopped_)continue;++softwareVisited_;}
            std::size_t i=0;
            for(;i<j_["entries"].size();++i)if(j_["entries"][i]["key"]==item["key"])break;
            Json current;
            const bool observed=p_.observe?p_.observe(item,current,e):p_.read(item,current,e);
            if(!observed||!valueValid(item,current)) {
                if(!observed&&globalSoftwareFailure(item,"scan-observe"))continue;
                if(i<j_["entries"].size())fail(i,"read: "+e);else scanError(item["key"].get<std::string>()+": read: "+e);
                continue;
            }
            if(!time()){error_="scan-canceled-or-deadline";break;}
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
            if((r.value("owned",false)&&(stage=="applied"||r.value("everApplied",false)))||current!=r["baseline"]||stage=="preexisting") {
                r["owned"]=false;r["stage"]="external-change";r["error"]="external-change-preserved";
                if(!entry(i,std::move(r)))break;
                continue;
            }
            if(!time()){scanError("scan-canceled-or-deadline");break;}
            r["owned"]=true;r["stage"]="prepared";r["error"]="";
            if(!entry(i,r))break; // REQUIRED durable intent; readback failure remains retryable.
            // Recheck immediately after durable preparation, including retries.
            Json before;
            if(!time()){error_="scan-canceled-or-deadline";break;}
            if(!p_.read(item,before,e)) {
                if(!globalSoftwareFailure(item,"scan-prewrite"))fail(i,"prewrite-read: "+e);
                continue;
            }
            if(before!=current) {
                r["owned"]=false;r["stage"]="external-change";r["error"]="prewrite-race-preserved";
                if(!entry(i,std::move(r)))break;
                continue;
            }
            if(!time()){fail(i,"scan-canceled-or-deadline");break;}
            const bool wrote=p_.write(item,r["expected"],e);const auto writeError=e;
            if(!time()){error_="scan-canceled-or-deadline";break;} // Durable intent owns any uncertain write.
            Json after;const bool read=p_.read(item,after,e);
            if(!read&&globalSoftwareFailure(item,"scan-readback",wrote?std::string{}:"write: "+writeError))continue;
            if(!wrote||!read||after!=r["expected"]) {
                fail(i,!wrote?"write: "+writeError:(!read?"readback: "+e:"readback-mismatch"));continue;
            }
            r["stage"]="applied";r["error"]="";if(!entry(i,std::move(r)))break;
        }
        if(!storageFault_&&time()) {next=j_;next["scanInProgress"]=false;(void)save(std::move(next));}
        return result();
    }
    Json restore(std::uint64_t budgetMs=180000) {
        deadline_=p_.monotonicMs()+budgetMs;
        if(!loaded_)return result();
        if(!time()){error_="restore-canceled-or-deadline";return result();}
        if(p_.beginOperation)p_.beginOperation();
        auto next=j_;next["enabled"]=false;next["scanInProgress"]=false;next["scanErrors"]=Json::array();
        if(!save(std::move(next)))return result();
        planSoftware(j_["entries"],true);
        for(std::size_t i=0;i<j_["entries"].size();++i) {
            if(storageFault_)break;
            auto r=j_["entries"][i];
            if(!r.value("owned",false)) {
                if(!r.value("error",std::string{}).empty()){r["error"]="";if(!entry(i,std::move(r)))break;}
                continue;
            }
            if(!time()){error_="restore-canceled-or-deadline";break;}
            if(r["kind"]=="software") {if(softwareStopped_)continue;++softwareVisited_;}
            Json current;std::string e;
            if(!p_.read(r,current,e)) {
                if(!globalSoftwareFailure(r,"restore-observe"))fail(i,"restore-read: "+e);
                continue;
            }
            if(current!=r["expected"]) {
                r["owned"]=false;r["stage"]=current==r["baseline"]?"restored":"external-change";
                r["error"]="";if(!entry(i,std::move(r)))break;continue;
            }
            r["stage"]="restore-prepared";r["error"]="";
            if(!entry(i,r))break;
            Json before;
            if(!time()){error_="restore-canceled-or-deadline";break;}
            if(!p_.read(r,before,e)) {
                if(!globalSoftwareFailure(r,"restore-prewrite"))fail(i,"restore-prewrite-read: "+e);
                continue;
            }
            if(before!=r["expected"]) {
                r["owned"]=false;r["stage"]="external-change";if(!entry(i,std::move(r)))break;continue;
            }
            if(!time()){fail(i,"restore-canceled-or-deadline");break;}
            const bool wrote=p_.write(r,r["baseline"],e);const auto writeError=e;
            if(!time()){error_="restore-canceled-or-deadline";break;}
            Json after;const bool read=p_.read(r,after,e);
            if(!read&&globalSoftwareFailure(r,"restore-readback",wrote?std::string{}:"restore-write: "+writeError))continue;
            if(!wrote||!read||after!=r["baseline"]) {fail(i,!wrote?"restore-write: "+writeError:(!read?"restore-readback: "+e:"restore-readback-mismatch"));continue;}
            r["owned"]=false;r["stage"]="restored";r["error"]="";if(!entry(i,std::move(r)))break;
        }
        return result();
    }
};
}} // namespace ymcc::staticwake
