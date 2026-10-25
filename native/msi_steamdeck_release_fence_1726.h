#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// Explicit MSI+SD Host-session contract. No hardware/PnP/mode/driver action.
// Trust is scoped to the already-authenticated Host tuple and exact pinned
// endpoint IDs; never substitute an XInput mask or dispose ACK for removal.
namespace ymcc::msi::sdRelease1726 {
class Session {
    bool supported_=false,required_=false,pinned_=false,absent_=false;
    std::string run_;
    uint64_t epoch_=0;
    std::set<std::string> targets_;
    std::string reason_="not-required";
    template<class Json> bool identity(const Json& e) const {
        if(!e.is_object())return false;
        auto str=[&](const char* k,const std::string& value){auto it=e.find(k);return it!=e.end()&&it->is_string()&&it->template get<std::string>()==value;};
        auto integer=[&](const char* k,uint64_t value){auto it=e.find(k);return it!=e.end()&&it->is_number_integer()&&*it==value;};
        auto r=e.find("required");
        return integer("version",1)&&r!=e.end()&&r->is_boolean()&&r->template get<bool>()&&
            str("runId",run_)&&integer("epoch",epoch_)&&str("persona","steamdeck")&&str("profileIdentity","steam-deck-composite");
    }
    template<class Json> bool readTargets(const Json& e,std::set<std::string>& targets) const {
        auto ids=e.find("ownedTargets"),count=e.find("ownedTargetCount");
        if(ids==e.end()||!ids->is_array()||ids->empty()||ids->size()>16||
            count==e.end()||!count->is_number_integer()||*count!=ids->size())return false;
        for(const auto& id:*ids){
            if(!id.is_string())return false;
            const auto text=id.template get<std::string>();
            if(text.empty()||text.size()>=512)return false;
            std::string upper;upper.reserve(text.size());
            for(unsigned char c:text){if(c<32||c>126)return false;upper.push_back(c>='a'&&c<='z'?char(c-'a'+'A'):char(c));}
            if(upper.rfind("USB\\VID_28DE&PID_1205\\",0)!=0 && upper.rfind("HID\\VID_28DE&PID_1205&MI_",0)!=0)return false;
            if(!targets.insert(upper).second)return false;
        }
        return true;
    }
    template<class Json> bool exactBool(const Json& e,const char* key,bool value) const {
        auto it=e.find(key);return it!=e.end()&&it->is_boolean()&&it->template get<bool>()==value;
    }
public:
    void clear() noexcept {supported_=required_=pinned_=absent_=false;run_.clear();epoch_=0;targets_.clear();reason_="not-required";}
    template<class Json> void hello(const Json& receipt) noexcept {
        supported_=false;
        try {auto it=receipt.find("msiSteamDeckReleaseFenceVersion");supported_=it!=receipt.end()&&it->is_number_integer()&&*it==1;}catch(...){}
    }
    bool begin(bool knownMsiBoard,const std::string& persona,const std::string& run,uint64_t epoch) {
        required_=knownMsiBoard&&persona=="steamdeck";pinned_=absent_=false;targets_.clear();run_=run;epoch_=epoch;
        reason_=!required_?"not-required":!supported_?"host-capability-missing":run.empty()||epoch==0?"invalid-session-identity":"waiting-for-prepared-pin";
        return !required_||(supported_&&!run.empty()&&epoch>0);
    }
    // RECREATE can replace IDs even when its ACK is lost or unbound. Keep the
    // old set as diagnostic ownership history, never as proof about a new target.
    void replacementRequested() noexcept {
        if(required_){pinned_=absent_=false;reason_="replacement-awaiting-current-pin";}
    }
    const std::set<std::string>& ownedTargets() const noexcept{return targets_;}
    const std::string& run() const noexcept{return run_;}
    uint64_t epoch() const noexcept{return epoch_;}
    bool acceptOwnedPnpAbsence(bool complete,const std::set<std::string>& present) noexcept {
        absent_=false;
        if(!required_||!pinned_||!complete){reason_="owned-PnP-recheck-unavailable";return false;}
        for(const auto& id:targets_)if(present.count(id)){reason_="owned-PnP-target-still-present";return false;}
        absent_=true;reason_="owned-PnP-absence-rechecked-after-host-exit";return true;
    }
    bool required() const noexcept{return required_;}
    bool pinned() const noexcept{return pinned_;}
    // Ownership survives a mutable writer-health flag, but never another tuple.
    bool currentPinned(const std::string& run,uint64_t epoch) const noexcept {
        return required_&&pinned_&&!run.empty()&&epoch>0&&run==run_&&epoch==epoch_;
    }
    bool verified(const std::string& run,uint64_t epoch) const noexcept{return required_&&pinned_&&absent_&&run==run_&&epoch==epoch_;}
    const std::string& reason() const noexcept{return reason_;}
    template<class Json> bool acceptPrepare(const Json& receipt) noexcept {
        if(!required_)return true;
        pinned_=absent_=false;
        try{
            auto it=receipt.find("msiSteamDeckReleaseEvidence");std::set<std::string> targets;
            if(it==receipt.end()||!identity(*it)||!exactBool(*it,"probeSucceeded",true)||!exactBool(*it,"absentVerified",false)||!readTargets(*it,targets)) {reason_="prepared-pin-invalid";return false;}
            auto remaining=it->find("remainingOwnedTargetCount");
            if(remaining==it->end()||!remaining->is_number_integer()||*remaining<=0||*remaining>targets.size()){reason_="prepared-pin-count-invalid";return false;}
            targets_=std::move(targets);pinned_=true;reason_="prepared-pin-accepted";return true;
        }catch(...){reason_="prepared-pin-exception";return false;}
    }
    template<class Json> bool acceptRelease(const Json& receipt) noexcept {
        if(!required_)return true;
        absent_=false;
        try{
            auto it=receipt.find("msiSteamDeckReleaseEvidence");std::set<std::string> targets;
            if(!pinned_||it==receipt.end()||!identity(*it)||!exactBool(*it,"probeSucceeded",true)||!exactBool(*it,"absentVerified",true)||!readTargets(*it,targets)||targets!=targets_){reason_="owned-endpoint-removal-unproven";return false;}
            auto remaining=it->find("remainingOwnedTargetCount");
            if(remaining==it->end()||!remaining->is_number_integer()||*remaining!=0){reason_="owned-endpoint-still-present";return false;}
            absent_=true;reason_="owned-endpoint-absence-verified";return true;
        }catch(...){reason_="release-proof-exception";return false;}
    }
};
}
