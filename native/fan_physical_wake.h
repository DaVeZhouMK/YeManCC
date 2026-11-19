#pragma once
#include <cstdint>
#include <mutex>
namespace ymcc::fan_wake {
// Fan-only physical cycles. Shared policy/generation is never written here.
// One serialized Close owner retains the newest sleep while an older Close runs.
class Observation {
    mutable std::mutex mutex_;
    std::uint64_t shared_=1, generation_=1, sharedBound_=1;
    std::uint64_t sleep_=0, wake_=0, lastFact_=0, cutoff_=0, published_=0, unpairedWake_=0, hibernateWake_=0;
    std::uint64_t closeOwner_=0, closePending_=0, closed_=0, closeUnstarted_=0;
    bool boundary_=false, firstRaw_=false, sharedAwake_=true;
    void syncLocked(std::uint64_t shared) {
        if(shared>shared_){shared_=shared;generation_=(generation_>=shared?generation_+1:shared);
            sharedBound_=generation_;boundary_=false;firstRaw_=false;sharedAwake_=true;
            sleep_=wake_=cutoff_=published_=unpairedWake_=hibernateWake_=0;}
    }
    bool physicalLocked() const {return boundary_ && sleep_ && wake_>sleep_ && wake_>cutoff_;}
    bool readyLocked(std::uint64_t g) const {
        return g && g==generation_ && (!boundary_ || sharedAwake_ || physicalLocked())
            && !closeOwner_ && !closePending_ && (!boundary_ || closed_==g);
    }
public:
    enum class CloseAdmission { Ignored, Queued, StartWorker };
    std::uint64_t generation(std::uint64_t shared) {std::lock_guard<std::mutex> l(mutex_);syncLocked(shared);return generation_;}
    std::uint64_t confirmedSuspend(std::uint64_t shared) {
        std::lock_guard<std::mutex> l(mutex_);
        if(!shared || shared<shared_)return 0;
        if(shared>shared_)syncLocked(shared);
        // Duplicate PBT/506 cannot erase a newer private physical cycle.
        if(boundary_)return sharedBound_==generation_?generation_:0;
        if(generation_==1 && shared==1)++generation_;
        sharedBound_=generation_;boundary_=true;firstRaw_=true;sharedAwake_=false;
        sleep_=wake_=cutoff_=published_=0;return generation_;
    }
    std::uint64_t recordSleep(std::uint64_t shared,std::uint64_t at) {
        std::lock_guard<std::mutex> l(mutex_);
        if(!shared || shared<shared_ || !at || at<=lastFact_ || (shared==shared_ && at<=unpairedWake_))return 0;
        syncLocked(shared);
        if(!firstRaw_ || sharedBound_!=generation_)++generation_;
        firstRaw_=false;boundary_=true;sharedAwake_=false;
        sleep_=lastFact_=at;wake_=published_=hibernateWake_=0;return generation_;
    }
    bool recordWake(std::uint64_t shared,std::uint64_t at,bool retry) {
        std::lock_guard<std::mutex> l(mutex_);
        // The public retry owner revokes wake at retryBoundary(). A suppressed
        // 507 might be older than the current wake; it has no authority to revoke it.
        if(retry)return false;
        // OS wake facts outlive a control intent; only publication/Host work
        // is activity-gated. Otherwise idle sleep survives a real wake forever.
        // An unpaired wake is only an ordering watermark in this shared cycle.
        // A delayed older 506 must not revoke an already current Ready state;
        // the watermark never manufactures a sleep, Close or physical wake.
        if(!shared || shared<shared_ || !at || at<=lastFact_ || at<=cutoff_)return false;
        syncLocked(shared);
        if(!sleep_){if(at>unpairedWake_)unpairedWake_=at;return false;}
        if(at<=sleep_)return false;
        wake_=lastFact_=at;return true;
    }
    std::uint64_t sharedResume(std::uint64_t shared) {
        std::lock_guard<std::mutex> l(mutex_);syncLocked(shared);
        if(!shared || shared!=shared_ || (sharedBound_!=generation_ && !hibernateWake_))return 0;
        sharedAwake_=true;return generation_;
    }
    // The public S4 handler has accepted the original 107. It can end the
    // newest Fan cycle even when Windows sends no matching S0 exit (507).
    bool recordSharedHibernateWake(std::uint64_t shared,std::uint64_t at) {
        std::lock_guard<std::mutex> l(mutex_);
        if(!shared || shared<shared_ || !at || at<=lastFact_)return false;
        syncLocked(shared);
        if(at<=cutoff_ || at<=unpairedWake_)return false;
        // Even a shared-bound cycle must fence a raw 506 delivered after S4.
        lastFact_=at;
        firstRaw_=false;
        if(sharedBound_==generation_ || !sleep_ || physicalLocked())return false;
        hibernateWake_=at;
        return true;
    }
    bool sharedHibernateWake(std::uint64_t g) const {
        std::lock_guard<std::mutex> l(mutex_);return g==generation_ && hibernateWake_!=0;
    }
    void retryBoundary(std::uint64_t shared,std::uint64_t at) {
        std::lock_guard<std::mutex> l(mutex_);if(shared!=shared_)return;
        if(at>cutoff_)cutoff_=at;wake_=published_=hibernateWake_=0;sharedAwake_=false;
    }
    CloseAdmission admitClose(std::uint64_t g, bool resumeUnstarted=false) {
        std::lock_guard<std::mutex> l(mutex_);
        if(!g || g!=generation_ || closed_==g)return CloseAdmission::Ignored;
        if(resumeUnstarted && (closeUnstarted_!=g || (!sharedAwake_ && !physicalLocked())))
            return CloseAdmission::Ignored;
        if(closeOwner_){
            if(g>closeOwner_ && g>closePending_){closePending_=g;return CloseAdmission::Queued;}
            return CloseAdmission::Ignored;
        }
        closeUnstarted_=0;closeOwner_=g;return CloseAdmission::StartWorker;
    }
    bool beginClose(std::uint64_t g) {return admitClose(g)==CloseAdmission::StartWorker;}
    std::uint64_t completeClose(std::uint64_t g) {
        std::lock_guard<std::mutex> l(mutex_);if(!g || closeOwner_!=g)return 0;
        if(g>closed_)closed_=g;
        closeOwner_=closePending_;closePending_=0;return closeOwner_;
    }
    void abandonClose(std::uint64_t g) {
        std::lock_guard<std::mutex> l(mutex_);
        if(!g || closeOwner_!=g)return;
        // No worker was installed. If a newer Close queued behind this owner,
        // it also never reached the Host; retain only the current generation.
        if(generation_==g || generation_==closePending_)closeUnstarted_=generation_;
        closeOwner_=closePending_=0;
    }
    bool needsClose(std::uint64_t g) const {std::lock_guard<std::mutex> l(mutex_);return g==generation_ && boundary_ && closed_!=g;}
    bool explicitResume(std::uint64_t g) {
        std::lock_guard<std::mutex> l(mutex_);if(!g || g!=generation_)return false;
        sharedAwake_=true;return true;
    }
    bool current(std::uint64_t g) const {std::lock_guard<std::mutex> l(mutex_);return g && g==generation_;}
    bool physical(std::uint64_t g) const {std::lock_guard<std::mutex> l(mutex_);return g==generation_ && physicalLocked();}
    bool awaitingWake(std::uint64_t g) const {std::lock_guard<std::mutex> l(mutex_);return g==generation_ && boundary_ && !sharedAwake_ && !physicalLocked();}
    bool ready(std::uint64_t g) const {std::lock_guard<std::mutex> l(mutex_);return readyLocked(g);}
    bool readyUnpublished(std::uint64_t g) const {
        std::lock_guard<std::mutex> l(mutex_);return boundary_ && readyLocked(g) && published_!=g;
    }
    bool claimReady(std::uint64_t g) {
        std::lock_guard<std::mutex> l(mutex_);if(!boundary_ || !readyLocked(g) || published_==g)return false;
        published_=g;return true;
    }
};
}
