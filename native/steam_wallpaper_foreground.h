#pragma once
// A native foreground lease, independent of renderer focus or cached media.
#include <cstdint>
#include <mutex>
#include <string>
#include "json.hpp"
namespace ymcc::steamwallpaperForeground {
struct Facts {
    bool known=false, physical=false, enabled=false, ready=false;
    std::uint32_t pid=0, steamPid=0;
    std::uint64_t birth=0, window=0, steamBirth=0, power=0;
    std::string modeEpoch;
    bool operator==(const Facts&) const = default;
};
inline bool bigPictureSurface(const std::wstring& type,const std::wstring& title) {
    if(type!=L"SDL_app")return false;
    // Window text is an additional fail-closed surface check. Process-wide
    // Big Picture mode alone cannot distinguish Friends/Menu/Overlay windows.
    return title==L"Steam 大屏幕模式"||title==L"Steam 大螢幕模式"||
        title==L"Steam Big Picture Mode"||title==L"Steam Big Picture";
}
class Authority {
    static constexpr std::uint64_t maximumRevision=9007199254740991ULL;
    std::mutex mutex_;
    const std::string seed_;
    std::uint64_t revision_=0;
    Facts last_;
    bool has_=false, exhausted_=false;
    void advance(){if(revision_>=maximumRevision){exhausted_=true;return;}++revision_;}
    nlohmann::json proof(const Facts& facts,bool concurrent=false) const {
        const bool current=!concurrent&&!exhausted_&&facts.known&&facts.physical&&facts.enabled&&facts.ready&&
            facts.pid&&facts.birth&&facts.window&&facts.steamPid&&facts.steamBirth&&!facts.modeEpoch.empty();
        return {{"source","native-steam-bigpicture-foreground"},{"known",facts.known},
            {"foreground",facts.physical},{"current",current},{"mode",facts.known?"bigpicture":"unknown"},
            {"pid",facts.pid},{"birth",std::to_string(facts.birth)},{"window",std::to_string(facts.window)},
            {"ownerEpoch",seed_+":"+std::to_string(revision_)},{"revision",revision_},
            {"reason",concurrent?"foreground-changed-during-read":exhausted_?"foreground-revision-exhausted":
                !facts.enabled?"wallpaper-disabled":!facts.ready?"foreground-lifecycle-inactive":
                !facts.known?"bigpicture-not-confirmed":!facts.physical?"bigpicture-not-foreground":""}};
    }
public:
    explicit Authority(std::string seed):seed_(std::move(seed)){}
    std::uint64_t ticket(){std::lock_guard lock(mutex_);return revision_;}
    void revoke(){std::lock_guard lock(mutex_);advance();has_=false;}
    nlohmann::json observe(const Facts& facts,std::uint64_t ticket){
        std::lock_guard lock(mutex_);
        if(ticket!=revision_)return proof(Facts{},true);
        if(!has_||!(facts==last_)){advance();last_=facts;has_=true;}
        return proof(facts);
    }
};
} // namespace ymcc::steamwallpaperForeground