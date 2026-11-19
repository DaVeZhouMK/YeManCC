#pragma once
// Shared read-only wire facts. No OS discovery, cache, action permission or
// account inference belongs in this header, including in independent EXEs.
#include "json.hpp"
#include "steam_startup_window_facts.h"
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace ymcc::steaminfo {
using Json=nlohmann::json;
inline constexpr std::uint32_t protocol=1;
enum Mask : std::uint32_t { Installations=1, Session=2, Presence=4, All=7, StartupWindow=8, Supported=15 };
struct Installation {std::string path,source;std::uint32_t error=0;};
struct Facts {
    StartupWindowFacts startupWindow;
    bool available=false;
    std::uint32_t requested=0;
    std::uint64_t observedAt=0;
    std::vector<Installation> installations;
    int presence=-1; // -1 unknown; only a complete fresh table proves absence.
    std::uint32_t processPid=0;
    std::uint64_t processBirth=0;
    std::string processPath;
    std::string root;
    std::uint32_t rootError=21,registryAccount=0,accountError=21;
    std::uint32_t uiAccount=0;
    bool uiAccountKnown=false; // Actual shared UI observation, never derived.
    std::uint64_t identityRevision=0,processEpoch=0,accountEpoch=0;
    // Rejection-only expectation from Native. Not a lease or action permission.
    std::uint64_t actionPowerGeneration=0,actionPowerSequence=0,actionPowerSerial=0;
};
// Source authority is supplied only by the public Steam owner. Metadata
// cannot construct a lease or implement current() from a retained JSON value.
enum class Selection { Unknown, RegistryCandidate, UiLocal };
struct SourceSnapshot {
    Facts facts;
    std::uint64_t sourceRevision=0,modeRevision=0,powerGeneration=0;
    std::string mode,modeEpoch;
    Selection selection=Selection::Unknown;
    std::uint32_t selectedAccount=0;
};
class SourceLease {
public:
    virtual ~SourceLease()=default;
    virtual const SourceSnapshot& snapshot()const noexcept=0;
    virtual bool current()noexcept=0; // Once invalid, never becomes current again.
    virtual void* cancellationEvent()const noexcept=0; // Borrowed native HANDLE.
};
std::shared_ptr<SourceLease> acquireSource()noexcept; // Implemented by public owner.
inline Json encode(const Facts& f){
    Json candidates=Json::array();for(const auto& c:f.installations)candidates.push_back({{"path",c.path},{"source",c.source},{"error",c.error}});
    Json result={{"available",f.available},{"version",protocol},{"requested",f.requested},{"observedAt",f.observedAt},{"installations",candidates},
        {"presence",f.presence},{"processPid",f.processPid},{"processBirth",f.processBirth},{"processPath",f.processPath},
        {"root",f.root},{"rootError",f.rootError},{"registryAccount",f.registryAccount},{"accountError",f.accountError},
        {"uiAccount",f.uiAccount},{"uiAccountKnown",f.uiAccountKnown},{"identityRevision",f.identityRevision},
        {"processEpoch",f.processEpoch},{"accountEpoch",f.accountEpoch},
        {"actionPowerGeneration",f.actionPowerGeneration},{"actionPowerSequence",f.actionPowerSequence},{"actionPowerSerial",f.actionPowerSerial}};
    if(f.requested&StartupWindow)result["startupWindow"]={
        {"known",f.startupWindow.known},{"completed",f.startupWindow.completed},{"window",f.startupWindow.window},{"pid",f.startupWindow.pid},
        {"birth",f.startupWindow.processBirth},{"observedAt",f.startupWindow.observedAt},
        {"processEpoch",f.startupWindow.processEpoch},{"accountEpoch",f.startupWindow.accountEpoch},
        {"windowsSession",f.startupWindow.windowsSession},{"root",f.startupWindow.root}};
    return result;
}
inline std::optional<Facts> decode(const Json& value,std::uint32_t requested){
    try{
        if(!requested||(requested&~Supported)||!value.is_object()||!value.contains("available")||!value["available"].is_boolean()||!value["available"].get<bool>())return {};
        auto number=[&](const char* key,std::uint64_t maximum)->std::uint64_t{
            const auto& n=value.at(key);if(!n.is_number_unsigned()&&!n.is_number_integer())throw 0;
            if(n.is_number_integer()&&!n.is_number_unsigned()&&n.get<std::int64_t>()<0)throw 0;
            const auto result=n.get<std::uint64_t>();if(result>maximum)throw 0;return result;
        };
        auto text=[&](const Json& n)->std::string{if(!n.is_string())throw 0;auto s=n.get<std::string>();if(s.size()>131072||s.find('\0')!=std::string::npos)throw 0;return s;};
        if(number("version",UINT32_MAX)!=protocol||number("requested",UINT32_MAX)!=requested)return {};
        Facts f;f.requested=requested;f.observedAt=number("observedAt",UINT64_MAX);
        const auto& list=value.at("installations");if(!list.is_array()||list.size()>7)return {};
        for(const auto& c:list){if(!c.is_object()||!c.at("error").is_number_unsigned())return {};
            const auto error=c.at("error").get<std::uint64_t>();if(error>UINT32_MAX)return {};
            auto source=text(c.at("source"));if(source.empty()||source.size()>64)return {};
            f.installations.push_back({text(c.at("path")),std::move(source),static_cast<std::uint32_t>(error)});
        }
        const auto& presence=value.at("presence");if(!presence.is_number_integer()||(presence!=-1&&presence!=0&&presence!=1))return {};f.presence=presence.get<int>();
        f.processPid=static_cast<std::uint32_t>(number("processPid",UINT32_MAX));f.processBirth=number("processBirth",UINT64_MAX);f.processPath=text(value.at("processPath"));
        f.root=text(value.at("root"));f.rootError=static_cast<std::uint32_t>(number("rootError",UINT32_MAX));
        f.registryAccount=static_cast<std::uint32_t>(number("registryAccount",UINT32_MAX));f.accountError=static_cast<std::uint32_t>(number("accountError",UINT32_MAX));
        f.uiAccount=static_cast<std::uint32_t>(number("uiAccount",UINT32_MAX));if(!value.at("uiAccountKnown").is_boolean())return {};f.uiAccountKnown=value.at("uiAccountKnown").get<bool>();
        f.identityRevision=number("identityRevision",UINT64_MAX);f.processEpoch=number("processEpoch",UINT64_MAX);f.accountEpoch=number("accountEpoch",UINT64_MAX);
        if(value.contains("actionPowerGeneration")||value.contains("actionPowerSequence")||value.contains("actionPowerSerial")) {
            f.actionPowerGeneration=number("actionPowerGeneration",UINT64_MAX);f.actionPowerSequence=number("actionPowerSequence",UINT64_MAX);f.actionPowerSerial=number("actionPowerSerial",UINT64_MAX);
        }
        if((f.uiAccountKnown&&!f.uiAccount)||(f.presence!=1&&(f.processPid||f.processBirth||!f.processPath.empty())))return {};
        if(!(requested&Installations)&&!f.installations.empty())return {};
        if(requested&StartupWindow){
            const auto& w=value.at("startupWindow");if(!w.is_object()||w.size()!=10||!w.at("known").is_boolean()||!w.at("completed").is_boolean())return {};
            auto scalar=[&](const char* key,std::uint64_t maximum){
                const auto& v=w.at(key);if(!v.is_number_unsigned()&&!v.is_number_integer())throw 0;
                if(v.is_number_integer()&&!v.is_number_unsigned()&&v.get<std::int64_t>()<0)throw 0;
                const auto result=v.get<std::uint64_t>();if(result>maximum)throw 0;return result;
            };
            auto& target=f.startupWindow;target.completed=w.at("completed").get<bool>();target.known=w.at("known").get<bool>();target.window=scalar("window",UINT64_MAX);
            target.pid=static_cast<std::uint32_t>(scalar("pid",UINT32_MAX));target.processBirth=scalar("birth",UINT64_MAX);
            target.observedAt=scalar("observedAt",UINT64_MAX);target.processEpoch=scalar("processEpoch",UINT64_MAX);
            target.accountEpoch=scalar("accountEpoch",UINT64_MAX);target.windowsSession=static_cast<std::uint32_t>(scalar("windowsSession",UINT32_MAX));target.root=text(w.at("root"));
            if(!target.completed){
                if(target.known||target.window||target.pid||target.processBirth||target.observedAt||target.processEpoch||target.accountEpoch||target.windowsSession||!target.root.empty())return {};
            }else{
                if(!target.observedAt||!target.processEpoch||!target.accountEpoch||target.processEpoch!=f.processEpoch||target.accountEpoch!=f.accountEpoch)return {};
                if(f.presence!=0&&f.presence!=1)return {};
                if(target.known){if(f.presence!=1||!target.window||!target.pid||!target.processBirth||target.root.empty())return {};}
                else if(target.window||target.pid||target.processBirth)return {};
                if(f.presence==1&&target.root.empty())return {};
                if(f.presence==0&&(target.known||target.windowsSession||!target.root.empty()))return {};
            }
        }else if(value.contains("startupWindow"))return {};
        f.available=true;return f;
    }catch(...){return {};}
}
} // namespace ymcc::steaminfo
