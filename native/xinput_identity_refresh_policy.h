#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace ymcc::input::identityRefresh {
using Tick = std::uint64_t;
enum class Kind { Unknown, Physical, Virtual };
enum class Stop { Ready, Wait, Retired };
struct Proof {
    Tick episode=0,identityEpoch=0,power=0;
    bool valid=false,legacyFallback=false;
};
struct Probe {
    bool available=false,edge=false,requested=false,unknownBefore=false;
    std::wstring_view path;
    Kind kind=Kind::Unknown;
    Tick epoch=0,currentEpoch=0,power=0,currentPower=0;
};
struct Update { bool advanceGeneration=true,notifyStop=false,retry=false; };

// One entry per slot, protected by the EXISTING gamepad-read mutex. A proof
// names a path/class/provider/epoch/power episode. The original pathless
// XInput fallback has an explicit weaker legacy marker; it is not path proof. No device I/O, feedback or scheduling lives here.
class Slot {
    std::wstring path_;
    Kind kind_=Kind::Unknown;
    Tick episode_=1,epoch_=0,power_=0;
    bool known_=false,haveKey_=false,provider_=false,exhausted_=false;
    static wchar_t fold(wchar_t c) noexcept { return c>=L'a'&&c<=L'z'?c-(L'a'-L'A'):c; }
    static bool samePath(std::wstring_view a,std::wstring_view b) noexcept {
        if(a.size()!=b.size())return false;
        for(std::size_t i=0;i<a.size();++i)if(fold(a[i])!=fold(b[i]))return false;
        return true;
    }
    void revoke() noexcept {
        known_=false;haveKey_=false;path_.clear();
        if(episode_==std::numeric_limits<Tick>::max())exhausted_=true;else ++episode_;
    }
public:
    void disconnect() noexcept { revoke();provider_=false; }
    Update observe(const Probe& p) noexcept {
        try {
            const bool epochDrift=p.epoch!=p.currentEpoch||p.power!=p.currentPower;
            const bool authorityChanged=provider_!=p.available;
            // Unknown clears the old probed bit. Its ensuing edge probe is a
            // retry of a continuously connected unknown slot, not a reconnect.
            // The legacy feedback-generation increment remains forced there.
            const bool forceEpisode=p.requested||(p.edge&&!p.unknownBefore);
            if(epochDrift){revoke();provider_=p.available;return {true,true,true};}
            if(forceEpisode||authorityChanged||epoch_!=p.epoch||power_!=p.power)revoke();
            provider_=p.available;epoch_=p.epoch;power_=p.power;
            if(!p.available){
                // Preserve the original OpenXInput-unavailable compatibility.
                // A change of provider already retired the previous proof.
                const bool same=haveKey_&&kind_==Kind::Physical&&path_.empty();
                if(!same){revoke();kind_=Kind::Physical;haveKey_=true;}
                known_=true;
                return {p.edge||p.requested||p.unknownBefore||!same,true,false};
            }
            if(p.path.empty()||p.kind==Kind::Unknown){
                // Retain the previous episode for its outstanding old stop. It cannot authorize a new pulse while unknown.
                known_=false;return {true,false,false};
            }
            const bool same=haveKey_&&kind_==p.kind&&samePath(path_,p.path);
            if(!same){revoke();path_.assign(p.path);kind_=p.kind;haveKey_=true;}
            known_=true;
            return {p.edge||p.requested||p.unknownBefore||!same,true,false};
        } catch(...) { revoke();return {true,true,false}; }
    }
    Proof capture(Tick epoch,Tick power) const noexcept {
        const bool valid=!exhausted_&&known_&&haveKey_&&kind_==Kind::Physical&&epoch_==epoch&&power_==power;
        return {episode_,epoch_,power_,valid,!provider_};
    }
    Stop stop(const Proof& p,Tick epoch,Tick power) const noexcept {
        if(!p.valid||exhausted_||p.legacyFallback==provider_||!haveKey_||p.episode!=episode_||p.identityEpoch!=epoch||p.power!=power||epoch_!=epoch||power_!=power)return Stop::Retired;
        if(!known_)return Stop::Wait;
        return kind_==Kind::Physical?Stop::Ready:Stop::Retired;
    }
};

struct StopPlan { bool retain=false; unsigned delayMs=0; bool note=false; };
// The existing pulse mutex owns plan/clear. Sampling only reads the atomics to
// notify the existing stop worker after a real probe resolution/disconnect.
// At most one retired stop exists, because the pulse owner is already singular.
class DeferredStop {
    Tick generation_=0,deadline_=0;
    bool dormant_=false;
    std::atomic<Tick> pending_{0};
    std::atomic<unsigned> slot_{4};
public:
    void clear() noexcept { generation_=deadline_=0;dormant_=false;pending_.store(0,std::memory_order_release);slot_.store(4,std::memory_order_relaxed); }
    StopPlan plan(Tick generation,unsigned slot,Tick now,unsigned budgetMs) noexcept {
        const bool first=generation_!=generation;
        if(first){
            generation_=generation;dormant_=false;
            deadline_=now>std::numeric_limits<Tick>::max()-budgetMs?std::numeric_limits<Tick>::max():now+budgetMs;
            slot_.store(slot,std::memory_order_relaxed);pending_.store(generation,std::memory_order_release);
        }
        // One bounded retry is followed by dormant ownership, not amnesia.
        // A later existing identity probe/disconnect wakes this same stop.
        if(now>=deadline_){const bool note=first||!dormant_;dormant_=true;return {true,0,note};}
        const auto remaining=deadline_-now;
        return {true,static_cast<unsigned>(remaining>std::numeric_limits<unsigned>::max()?std::numeric_limits<unsigned>::max():remaining),first};
    }
    Tick pendingFor(unsigned slot) const noexcept {
        const auto a=pending_.load(std::memory_order_acquire);
        const auto owner=slot_.load(std::memory_order_relaxed);
        const auto b=pending_.load(std::memory_order_acquire);
        return a&&a==b&&owner==slot?a:0;
    }
};

// A source-resolution notification may race a newer pulse. Keep the newer
// generation's deadline; for the same owner keep the earliest deadline.
inline bool replaceDeadline(Tick oldGeneration,Tick oldDue,Tick generation,Tick due) noexcept {
    return generation>oldGeneration||(generation==oldGeneration&&due<oldDue);
}
} // namespace ymcc::input::identityRefresh
