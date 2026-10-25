#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <algorithm>
#include <cmath>
#include <array>
#include <cstddef>

namespace ymcc::touchhaptic {
// User-selected touch-feedback tiers: 20%, 40%, 60%; gesture timing is independent.
inline constexpr int kStrengthPercent=20,kMediumStrengthPercent=kStrengthPercent*2,
    kStrongStrengthPercent=kStrengthPercent*3,kDurationMs=18;
inline constexpr bool validStrengthPercent(int strength) noexcept {
    return strength==kStrengthPercent||strength==kMediumStrengthPercent||strength==kStrongStrengthPercent;
}
inline constexpr std::uint64_t kMinIntervalMs=45,kMaxQueueAgeMs=80;
inline constexpr double kDistanceStep=0.05,kJitterFraction=0.006;
enum class Reason : unsigned { None, SourcePending, PowerTransition, ManualPulseActive,
    SourceChanged, OpenFailed, WriteFailed, IoPending, Cancelled, QueueExpired, Throttled,
    QueueFull, StopUnavailable, Count };
inline constexpr const char* reasonName(Reason r) noexcept {
    switch(r){case Reason::None:return "ok";case Reason::SourcePending:return "source-pending";
    case Reason::PowerTransition:return "power-transition";case Reason::ManualPulseActive:return "manual-pulse-active";
    case Reason::SourceChanged:return "source-changed";case Reason::OpenFailed:return "open-failed";
    case Reason::WriteFailed:return "write-failed";case Reason::IoPending:return "io-pending";
    case Reason::Cancelled:return "cancelled";case Reason::QueueExpired:return "queue-expired";
    case Reason::Throttled:return "throttled";case Reason::QueueFull:return "queue-full";
    case Reason::StopUnavailable:return "pulse-stop-unavailable";default:return "unknown";}
}
enum class Backend : unsigned { None, XInput, Msi };
struct SourceToken {
    Backend backend=Backend::None;std::uint64_t generation=0,identity=0,power=0;unsigned slot=4;std::uint64_t topology=0;
    bool ready()const noexcept{return backend!=Backend::None;}
    bool operator==(const SourceToken&)const noexcept=default;
};
// Internal result: no JSON, string lookup or per-pulse heap ownership.
struct Receipt {
    bool ok=false,managed=false,attempted=false;Backend backend=Backend::None;Reason reason=Reason::None;
    unsigned slot=4;int motor=0,strength=0,durationMs=0;std::uint32_t written=0,error=0;
    std::uint64_t pulseGeneration=0;
    bool outputBaselineAttempted=false,outputBaselineAccepted=false; // API receipts, never proof of motor response.
    std::uint16_t capabilityToken=0; // Chosen physical writer only; never a source/readiness or game-feedback permission.
};
// Detailed failure rows are event edges, not a disk flush on every failed MOVE.
// Serialized by the device writer; fixed POD and no clock/allocation required.
struct FailureLogGate {
    SourceToken key{};Reason reason=Reason::None;std::uint32_t error=0;bool active=false;
    bool publish(bool touch,bool ok,const SourceToken& token,Reason why,std::uint32_t code)noexcept {
        if(ok)active=false;if(!touch)return true;if(ok)return false;
        if(active&&key==token&&reason==why&&error==code)return false;
        key=token;reason=why;error=code;active=true;return true;
    }
};
// Pure per-contact state. No allocations, clocks, loops, I/O or background work.
// Cache geometry on DOWN. Consume skipped detents too, never replay a backlog.
struct Motion {
    double distance=0,inverseShortSide=0,jitterSquared=0;
    float anchorX=0,anchorY=0;
    std::uint64_t lastAt=0;
    bool fired=false;
    void begin(float x,float y,int width,int height,std::uint64_t now) noexcept {
        const double side=std::max(1,std::min(width,height));
        distance=0;inverseShortSide=1.0/side;
        const double jitter=side*kJitterFraction;jitterSquared=jitter*jitter;
        anchorX=x;anchorY=y;lastAt=now;fired=true;
    }
    bool move(float x,float y,std::uint64_t now) noexcept {
        if(!fired)return false;
        const double dx=(double)x-anchorX,dy=(double)y-anchorY;
        const double squared=dx*dx+dy*dy;
        if(squared<jitterSquared)return false;
        anchorX=x;anchorY=y;
        const double total=distance+std::sqrt(squared)*inverseShortSide;
        const double steps=std::floor((total+1e-8)*(1.0/kDistanceStep));
        if(steps<1){distance=total;return false;}
        distance=std::max(0.0,total-steps*kDistanceStep);
        if(now<lastAt||now-lastAt<kMinIntervalMs)return false;
        lastAt=now;return true;
    }
};
// Two haptic-only contacts. These never carry mouse/key/tap/BUS state.
// A gesture keeps this route through UP/cancel even if foreground changes.
struct FeedbackOnlyContacts {
    struct Contact {bool active=false;std::uint32_t id=0;int surface=0;Motion motion;float x=0,y=0;};
    std::array<Contact,2> contacts{};
    void cancel()noexcept{contacts={};}
    int find(std::uint32_t id,int surface)const noexcept {
        for(int i=0;i<2;++i)if(contacts[i].active&&contacts[i].id==id&&contacts[i].surface==surface)return i;
        return -1;
    }
    void adopt(int slot,int surface,std::uint32_t id,const Motion& motion,float x,float y)noexcept {
        if(slot>=0&&slot<2&&!contacts[slot].active)contacts[slot]={true,id,surface,motion,x,y};
    }
    unsigned mask()const noexcept {unsigned result=0;for(int i=0;i<2;++i)if(contacts[i].active)result|=1u<<i;return result;}
    bool event(int surface,std::uint32_t id,int phase,float x,float y,int w,int h,std::uint64_t now,bool shared,bool feedback=true)noexcept {
        int i=find(id,surface);
        if(phase==2){if(i>=0)contacts[i]={};return false;}
        if(w<=0||h<=0||!std::isfinite(x)||!std::isfinite(y))return false;
        x=std::clamp(x,0.f,(float)w);y=std::clamp(y,0.f,(float)h);
        if(phase==0){
            if(i>=0)return false;
            if(!shared){i=surface;if(i<0||i>1||contacts[i].active)return false;}
            else {for(int j=0;j<2;++j)if(!contacts[j].active){i=j;break;}}
            if(i<0)return false;
            auto& c=contacts[i];c.active=true;c.id=id;c.surface=surface;c.x=x;c.y=y;c.motion.begin(x,y,w,h,now);return feedback;
        }
        if(i<0)return false;auto& c=contacts[i];c.x=x;c.y=y;
        // Visual/gesture ownership remains live when feedback is off. Skip
        // distance/threshold math; settings changes release contacts before reuse.
        return feedback && c.motion.move(x,y,now);
    }
};
// UI-thread edge gate: repeated stationary/MOVE ownership reports do not take
// the dispatch mutex or read clocks. UP/cancel and sink replacement stay visible.
class EventGate {
public:
    bool publish(unsigned contacts,bool pulse,int strength=kStrengthPercent) noexcept {
        if(!contacts)strength=0;
        const bool changed=contacts!=contacts_||strength!=strength_;
        contacts_=contacts;strength_=strength;return changed||pulse;
    }
    void reset() noexcept {contacts_=0;strength_=0;}
private:
    unsigned contacts_=0;
    int strength_=0;
};
inline bool logPulseImmediately(bool touch,bool ok) noexcept {return !touch||!ok;}
// Owned by the existing serial worker. Successful short pulses are counted,
// not individually serialized/flushed to disk. One summary at a contact end.
struct Summary {
    std::uint64_t sent=0,dropped=0,failed=0,xinput=0,msi=0,minimal=0,medium=0,strong=0;
    std::uint64_t outputBaselineAttempts=0,outputBaselineAccepted=0;
    std::array<std::uint64_t,static_cast<std::size_t>(Reason::Count)> reasons{};
    std::array<std::uint32_t,static_cast<std::size_t>(Reason::Count)> errors{};
    void failure(Reason reason,bool drop=false)noexcept {
        if(drop)++dropped;else ++failed;
        const auto index=static_cast<std::size_t>(reason);if(index<reasons.size())++reasons[index];
    }
    void record(const Receipt& r)noexcept {
        outputBaselineAttempts+=r.outputBaselineAttempted;outputBaselineAccepted+=r.outputBaselineAccepted;
        if(!r.ok){failure(r.reason);const auto index=static_cast<std::size_t>(r.reason);if(index<errors.size())errors[index]=r.error;return;}
        record(true,r.backend==Backend::Msi,r.strength);
    }
    bool empty()const noexcept{return !(sent||dropped||failed);}
    void record(bool ok,bool msiBackend,int strength=kStrengthPercent) noexcept {
        if(!ok){++failed;return;}++sent;if(msiBackend)++msi;else ++xinput;
        if(strength==kStrengthPercent)++minimal;
        else if(strength==kMediumStrengthPercent)++medium;
        else if(strength==kStrongStrengthPercent)++strong;
    }
    Summary take() noexcept {const Summary saved=*this;*this={};return saved;}
};
// One bounded, expiring request in the existing controller serial worker.
// UI updates contact ownership; no device I/O, timer or new worker lives here.
class Dispatch {
public:
    struct Ticket { std::uint64_t epoch=0,at=0,power=0; int strength=0; SourceToken source{}; };
    std::optional<Ticket> request(unsigned contacts,bool pulse,std::uint64_t now,std::uint64_t power,int strength=kStrengthPercent,SourceToken source={}) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!contacts||!validStrengthPercent(strength)) { if(contacts_)++epoch_; contacts_=0;strength_=0;return {}; }
        // A queued pulse owns its tier. Never retarget it after a setting change.
        if(contacts_&&strength_!=strength)++epoch_;
        contacts_=contacts;strength_=strength;
        if(!pulse || pending_)return {};
        pending_=true;pendingEpoch_=epoch_;
        return Ticket{epoch_,now,power,strength,source};
    }
    Reason admitReason(const Ticket& ticket,std::uint64_t now,std::uint64_t power,bool allowed) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!pending_||executing_||pendingEpoch_!=ticket.epoch||ticket.epoch!=epoch_||!contacts_||ticket.strength!=strength_)
            return Reason::Cancelled;
        if(!allowed||power!=ticket.power)return Reason::PowerTransition;
        if(now<ticket.at||now-ticket.at>kMaxQueueAgeMs)return Reason::QueueExpired;
        if(started_&&(now<lastStart_||now-lastStart_<kMinIntervalMs))return Reason::Throttled;
        executing_=true;started_=true;lastStart_=now;return Reason::None;
    }
    bool admit(const Ticket& ticket,std::uint64_t now,std::uint64_t power,bool allowed) {
        return admitReason(ticket,now,power,allowed)==Reason::None;
    }
    void finish(const Ticket& ticket) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(pending_ && pendingEpoch_==ticket.epoch){pending_=false;executing_=false;}
    }
private:
    std::mutex mutex_;
    unsigned contacts_=0;
    int strength_=0;
    bool pending_=false,executing_=false,started_=false;
    std::uint64_t epoch_=1,pendingEpoch_=0,lastStart_=0;
};
} // namespace ymcc::touchhaptic
