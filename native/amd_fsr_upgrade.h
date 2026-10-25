#pragma once
// One-shot driver preference, not a resident feature. Reads never replay writes.
#include "json.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
namespace ymcc::amdfsr {
using Json = nlohmann::json;
// One fixed page lease, no timers/threads/handles. Close is metadata-only and
// can revoke a queued/background action without waiting for the driver mutex.
struct PageLease {
    std::string owner;
    std::atomic<bool> active{true};
    explicit PageLease(std::string id):owner(std::move(id)){}
};
class PageGate {
    std::mutex mutex_;std::shared_ptr<PageLease> current_;
public:
    bool open(const std::string& owner) {
        if(owner.size()<8 || owner.size()>96 || !std::all_of(owner.begin(),owner.end(),[](unsigned char c){
            return std::isalnum(c) || c=='-' || c=='_';})) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if(current_ && current_->owner==owner && current_->active.load()) return true;
        if(current_) current_->active.store(false);
        current_=std::make_shared<PageLease>(owner);return true;
    }
    void close(const std::string& owner) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(current_ && current_->owner==owner){current_->active.store(false);current_.reset();}
    }
    std::shared_ptr<PageLease> ticket(const std::string& owner) {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_ && current_->owner==owner && current_->active.load()?current_:nullptr;
    }
};
inline Json inactive() {
    return {{"schema",1},{"ok",false},{"reason","page_inactive"},{"defaultEnabled",false},
        {"versionConfirmed",false},{"adapters",Json::array()}};
}

inline std::string lower(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));}); return s;
}
// Display-only hint for older drivers which cannot expose the FSR interface.
// Official ADLX VendorId + Type + IsSupported remain the sole write admission.
inline bool integratedCandidate(const std::string& gpu,const std::string& cpu) {
    const auto g=lower(gpu),c=lower(cpu);
    if(g.find("radeon")==std::string::npos) return false;
    const auto rx=g.find("rx ");
    if(rx!=std::string::npos && rx+3<g.size() && std::isdigit(static_cast<unsigned char>(g[rx+3]))) return false;
    if(g.find("pro w")!=std::string::npos) return false;
    for(const auto* name:{"610m","660m","680m","740m","760m","780m","840m","860m","880m","890m","8050s","8060s"})
        if(g.find(name)!=std::string::npos) return true;
    return g.find("graphics")!=std::string::npos && (c.find("amd")!=std::string::npos || c.find("ryzen")!=std::string::npos);
}
struct Adapter {
    std::string id,name,driverVersion,fence,reason;
    bool amd=false, candidate=false, supported=false;
    // null means unreadable; never publish an optimistic ON or OFF.
    Json enabled=nullptr;
    bool integrated=false,typeRead=false;
};
inline Json describe(const Adapter& a) {
    return {{"id",a.id},{"name",a.name},{"driverVersion",a.driverVersion},{"fence",a.fence},
        {"amd",a.amd},{"hardwareCandidate",a.candidate || (a.typeRead && a.integrated)},{"gpuType",a.typeRead?(a.integrated?"integrated":"discrete"):"unknown"},
        {"supported",a.supported && a.typeRead && a.integrated},{"enabled",a.supported && a.typeRead && a.integrated?a.enabled:Json(nullptr)},
        {"upgradeRecommended",a.amd && (a.candidate || (a.typeRead && a.integrated)) && !a.supported &&
            (a.reason=="driver_missing" || a.reason=="driver_interface_unavailable" || a.reason=="not_supported" || a.reason=="gpu_type_unreadable")},
        {"reason",a.reason}};
}
struct Ops {
    virtual ~Ops()=default;
    virtual bool active() const {return true;}
    virtual std::vector<Adapter> probe()=0;
    virtual bool load(Json& policy)=0;
    virtual bool save(const Json& policy)=0;
    // Implementation must re-probe the exact adapter/fence immediately before SetEnabled.
    virtual bool write(const Adapter& expected,bool enabled)=0;
};
inline bool validPolicy(const Json& p) {
    if (!p.is_object() || !p.contains("schema") || !p["schema"].is_number_integer() || p["schema"]!=1 || !p.contains("defaultEnabled") || !p["defaultEnabled"].is_boolean() ||
        !p.contains("devices") || !p["devices"].is_object() || p["devices"].size()>64) return false;
    for (const auto& entry:p["devices"].items()) {
        const auto& v=entry.value();
        if (entry.key().empty() || entry.key().size()>4096 || !v.is_object() ||
            !v.contains("attempted") || !v["attempted"].is_boolean() ||
            !v.contains("requested") || !v["requested"].is_boolean()) return false;
    }
    return true;
}
// I3DSettingsServices3/FSR first appears in AMD's official v1.5 tag.
// This only skips known old libraries; actual support still comes from ADLX.
inline bool fsrApiVersion(unsigned major,unsigned minor) {return major>1 || (major==1 && minor>=5);}
inline Json defaults() {return {{"schema",1},{"defaultEnabled",true},{"devices",Json::object()}};}
inline bool admitted(const Adapter& a) {
    return a.amd && a.typeRead && a.integrated && a.supported && a.enabled.is_boolean() && !a.id.empty() && !a.fence.empty();
}
inline Json run(Ops& ops,const std::string& action,const Json& args=Json::object()) {
    if (!ops.active()) return inactive();
    auto adapters=ops.probe(); if (!ops.active()) return inactive();
    Json policy;
    const bool policyOk=ops.load(policy) && validPolicy(policy);
    if (!ops.active()) return inactive();
    bool ok=true; std::string reason=policyOk?"read":"policy_unreadable";
    if (!policyOk) ok=false;
    if (action=="initialize" && policyOk && policy["defaultEnabled"]==true) {
        bool changed=false;
        for (const auto& a:adapters) {
            if (!ops.active()) return inactive();
            if (!admitted(a) || policy["devices"].contains(a.id)) continue;
            if (policy["devices"].size()>=64) {ok=false;reason="policy_full";break;}
            // Durably record the attempt BEFORE writing. Failure/crash/timeout
            // never licenses a second automatic write, even after a driver update.
            policy["devices"][a.id]={{"attempted",true},{"requested",true},{"source","default"}};
            if (!ops.active()) return inactive();
            if (!ops.save(policy)) {ok=false;reason="policy_save_failed";break;}
            if (!ops.active()) return inactive();
            changed=true;
            const bool wrote=a.enabled==true || ops.write(a,true);
            if (!ops.active()) return inactive();
            const auto fresh=ops.probe();
            if (!ops.active()) return inactive();
            const auto it=std::find_if(fresh.begin(),fresh.end(),[&](const auto& now){return now.id==a.id && now.fence==a.fence;});
            const bool verified=wrote && it!=fresh.end() && admitted(*it) && it->enabled==true;
            if (!verified) {ok=false;reason="enable_unconfirmed";}
            else if (ok) reason="default_verified";
        }
        if (changed) {if (!ops.active()) return inactive();adapters=ops.probe();}
    } else if (action=="set") {
        if (!policyOk) {ok=false;reason="policy_unreadable";}
        else if (!args.is_object() || !args.contains("id") || !args["id"].is_string() ||
                 !args.contains("fence") || !args["fence"].is_string() || !args.contains("enabled") || !args["enabled"].is_boolean()) {
            ok=false;reason="invalid_request";
        } else {
            const auto id=args["id"].get<std::string>(), fence=args["fence"].get<std::string>();
            const bool target=args["enabled"].get<bool>();
            const auto it=std::find_if(adapters.begin(),adapters.end(),[&](const auto& a){return a.id==id && a.fence==fence && admitted(a);});
            if (it==adapters.end()) {ok=false;reason="adapter_changed";}
            else {
                const auto expected=*it;
                if (!ops.active()) return inactive();
                policy["devices"][id]={{"attempted",true},{"requested",target},{"source","user"}};
                if (policy["devices"].size()>64) {ok=false;reason="policy_full";}
                else if (!ops.save(policy)) {ok=false;reason="policy_save_failed";}
                else {
                    if (!ops.active()) return inactive();
                    const bool wrote=expected.enabled==target || ops.write(expected,target);
                    if (!ops.active()) return inactive();
                    adapters=ops.probe();
                    const auto fresh=std::find_if(adapters.begin(),adapters.end(),[&](const auto& a){return a.id==id && a.fence==fence && admitted(a);});
                    ok=wrote && fresh!=adapters.end() && fresh->enabled==target;
                    reason=ok?"verified":"write_unconfirmed";
                }
            }
        }
    }
    if (!ops.active()) return inactive();
    Json rows=Json::array();
    for (const auto& a:adapters) {
        if (!a.amd || (a.typeRead && !a.integrated) || (!a.typeRead && !a.candidate)) continue;
        auto row=describe(a);
        row["automaticAttempted"]=policyOk && policy["devices"].contains(a.id);
        rows.push_back(std::move(row));
    }
    return {{"schema",1},{"ok",ok},{"reason",reason},{"defaultEnabled",policyOk && policy["defaultEnabled"]==true},
        {"versionConfirmed",false},{"adapters",std::move(rows)}};
}
} // namespace ymcc::amdfsr
