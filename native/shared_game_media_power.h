#pragma once
#include "power_source_state.h"
// Settings events own this preference. Work boundaries read the shared receipt,
// never a performance profile or the business settings file.
#include <windows.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <stdexcept>
#include "json.hpp"
namespace ymcc::media::powerPolicy {
struct Ticket { uint64_t revision=0; int ac=1; };
inline std::mutex mutex;
inline bool pauseOnBattery=false,preferenceOwner=false;
inline int observedAc=1;
inline uint64_t revision=1,observedSourceRevision=0;
inline bool hasSourceReceipt=false;
inline const std::string processEpoch=std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
inline int windowsAc() {
    SYSTEM_POWER_STATUS status{};
    return ymcc::powerSource::read(status)&&status.ACLineStatus<=1?int(status.ACLineStatus):-1;
}
inline Ticket observeLocked() {
    const auto source=ymcc::powerSource::snapshot();
    const auto ac=pauseOnBattery?(source.known?source.ac:-1):1;
    // The producer can observe DC and AC before this consumer runs again. Its
    // revision preserves that interruption even when the final AC value matches.
    const bool interrupted=pauseOnBattery&&hasSourceReceipt&&observedSourceRevision!=source.revision;
    if(ac!=observedAc||interrupted){observedAc=ac;++revision;}
    observedSourceRevision=source.revision;hasSourceReceipt=true;
    return {revision,ac};
}
inline bool enabled(){std::lock_guard lock(mutex);return pauseOnBattery;}
inline Ticket ticket(){std::lock_guard lock(mutex);return observeLocked();}
inline bool configure(bool enabled){
    std::lock_guard lock(mutex);const bool changed=pauseOnBattery!=enabled;
    pauseOnBattery=enabled;observeLocked();return changed;
}
inline bool own(bool enabled){std::lock_guard lock(mutex);preferenceOwner=true;const bool changed=pauseOnBattery!=enabled;pauseOnBattery=enabled;observeLocked();return changed;}
inline void consume(bool enabled){std::lock_guard lock(mutex);if(preferenceOwner)return;pauseOnBattery=enabled;observeLocked();}
inline const char* reason(const Ticket& value){return value.ac==0?"wallpaper-on-battery":value.ac<0?"wallpaper-power-unknown":"";}
inline std::string reason(){return reason(ticket());}
inline bool blocked(){return ticket().ac!=1;}
inline bool current(const Ticket& expected){const auto value=ticket();return value.ac==1&&value.revision==expected.revision;}
inline bool allowed(const nlohmann::json& request){
    const auto value=ticket();if(value.ac!=1)return false;
    try{return request.value("wallpaperPowerEpoch",std::string{})!=processEpoch||
        request.value("wallpaperPowerRevision",uint64_t{0})==value.revision;}catch(...){return false;}
}
inline void require(const nlohmann::json& request){
    const auto value=ticket();if(value.ac!=1)throw std::runtime_error(reason(value));
    if(!allowed(request))throw std::runtime_error("media-stale-wallpaper-power");
}
inline void stamp(nlohmann::json& request){const auto value=ticket();if(value.ac!=1)throw std::runtime_error(reason(value));request["wallpaperPowerEpoch"]=processEpoch;request["wallpaperPowerRevision"]=value.revision;}
} // namespace ymcc::media::powerPolicy
