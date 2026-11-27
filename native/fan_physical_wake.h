#pragma once
#include <cstdint>
#include <mutex>
namespace ymcc::fan_wake {
// Fan-only physical cycles. Shared policy/generation is never written here.
// One serialized Close owner retains the newest sleep while an older Close runs.
class Observation {
public:
    enum class SharedWake { None, Hibernate, CriticalBroadcast };
    enum class OffStatus { Applied, Sealed, Retired, Superseded };
    struct OffRequest { std::uint64_t view, navigation, sequence; };
private:
    mutable std::mutex mutex_;
    std::uint64_t shared_=1, generation_=1, sharedBound_=1;
    std::uint64_t sleep_=0, wake_=0, lastFact_=0, cutoff_=0, published_=0, unpairedWake_=0, finalWakeAt_=0, confirmedAt_=0;
    SharedWake finalWake_=SharedWake::None;
    std::uint64_t closeOwner_=0, closePending_=0, closed_=0, closeUnstarted_=0;
    std::uint64_t startupWake_=0, startupUntil_=0, unpairedStartupUntil_=0, startupSuspend_=0, startupIntent_=0;
    std::uint64_t startupBoundaryAt_=0, startupOffAt_=0;
    std::uint64_t startupSerial_=0, startupPbt_=0, unpairedStartupSerial_=0, unpairedStartupPbt_=0;
    OffRequest startupOffRequest_{};
    std::uint64_t startupOffFence_=0;
    bool startupOffApplied_=false;
    bool startupClaimed_=false, startupPublished_=false;
    bool boundary_=false, firstRaw_=false, sharedAwake_=true;
    void clearStartupLocked() {
        startupWake_=startupUntil_=startupSuspend_=startupIntent_=0;startupClaimed_=startupPublished_=false;
        startupSerial_=startupPbt_=startupBoundaryAt_=0;
    }
    void syncLocked(std::uint64_t shared) {
        if(shared>shared_){shared_=shared;generation_=(generation_>=shared?generation_+1:shared);
            sharedBound_=generation_;boundary_=false;firstRaw_=false;sharedAwake_=true;
            sleep_=wake_=cutoff_=published_=unpairedWake_=finalWakeAt_=confirmedAt_=0;
            clearStartupLocked();unpairedStartupUntil_=0;
            finalWake_=SharedWake::None;}
    }
    bool physicalLocked() const {return boundary_ && sleep_ && wake_>sleep_ && wake_>cutoff_;}
    bool supersededWakeLocked(std::uint64_t latestEntry) const {
        // The coordinator can receive the first raw 506 before our window
        // consumes it. An earlier Ready is not authority across that entry.
        if(!boundary_)return latestEntry>lastFact_ && latestEntry>unpairedWake_;
        if(finalWakeAt_ && latestEntry>finalWakeAt_)return true;
        if(physicalLocked())return latestEntry>wake_;
        // An ordinary public wake can finish a known 506 without recording a 507.
        return !finalWakeAt_ && boundary_ && sleep_ && sharedAwake_ && latestEntry>sleep_;
    }
    bool readyLocked(std::uint64_t g,std::uint64_t latestEntry=0) const {
        return g && g==generation_ && !supersededWakeLocked(latestEntry) &&
            (!boundary_ || sharedAwake_ || physicalLocked())
            && !closeOwner_ && !closePending_ && (!boundary_ || closed_==g);
    }
    bool sameOffDocumentLocked(OffRequest request) const {
        return startupOffRequest_.view==request.view && startupOffRequest_.navigation==request.navigation;
    }
    bool cancelStartupLocked(std::uint64_t fence) {
        const bool wasPublished=startupPublished_ && startupBoundaryAt_>startupOffAt_;
        if(fence>startupOffAt_)startupOffAt_=fence;
        return wasPublished && startupBoundaryAt_<=startupOffAt_;
    }
public:
    enum class CloseAdmission { Ignored, Queued, StartWorker };
    std::uint64_t generation(std::uint64_t shared) {std::lock_guard<std::mutex> l(mutex_);syncLocked(shared);return generation_;}
    std::uint64_t confirmedSuspend(std::uint64_t shared,std::uint64_t at=0) {
        std::lock_guard<std::mutex> l(mutex_);
        if(!shared || shared<shared_)return 0;
        if(shared>shared_)syncLocked(shared);
        // Duplicate PBT/506 cannot erase a newer private physical cycle.
        if(boundary_)return sharedBound_==generation_?generation_:0;
        if(generation_==1 && shared==1)++generation_;
        sharedBound_=generation_;boundary_=true;firstRaw_=true;sharedAwake_=false;
        confirmedAt_=at;sleep_=wake_=cutoff_=published_=0;return generation_;
    }
    std::uint64_t recordSleep(std::uint64_t shared,std::uint64_t at) {
        std::lock_guard<std::mutex> l(mutex_);
        if(!shared || shared<shared_ || !at || at<=lastFact_)return 0;
        syncLocked(shared);
        // A 507 alone is only a watermark. Pair a delayed 506 with it only
        // inside the still-parked public suspend that expected its first 506.
        const bool reverse=at<=unpairedWake_;
        if(reverse && (!firstRaw_ || sharedBound_!=generation_ || sharedAwake_ ||
            !confirmedAt_ || at<confirmedAt_ || at<=cutoff_ || at==unpairedWake_)){
            // Remember an already retired entry even when a new public
            // generation clears the unpaired exit watermark.
            if(!boundary_)lastFact_=at;
            return 0;
        }
        const auto boundaryAt=firstRaw_ && sharedBound_==generation_ && confirmedAt_ && confirmedAt_<=at ? confirmedAt_:at;
        if(!firstRaw_ || sharedBound_!=generation_)++generation_;
        firstRaw_=false;boundary_=true;sharedAwake_=false;
        sleep_=at;wake_=reverse?unpairedWake_:0;lastFact_=reverse?wake_:at;
        clearStartupLocked();
        startupBoundaryAt_=boundaryAt;
        if(reverse && unpairedStartupUntil_){startupWake_=wake_;startupUntil_=unpairedStartupUntil_;
            startupSerial_=unpairedStartupSerial_;startupPbt_=unpairedStartupPbt_;}
        unpairedStartupUntil_=0;
        published_=finalWakeAt_=0;finalWake_=SharedWake::None;return generation_;
    }
    bool recordWake(std::uint64_t shared,std::uint64_t at,bool retry,std::uint64_t startupUntil=0,
                    std::uint64_t startupSerial=0,std::uint64_t startupPbt=0) {
        std::lock_guard<std::mutex> l(mutex_);
        // The public retry owner revokes wake at retryBoundary(). A suppressed
        // 507 might be older than the current wake; it has no authority to revoke it.
        if(retry)return false;
        // OS wake facts outlive a control intent; only publication/Host work
        // is activity-gated. Otherwise idle sleep survives a real wake forever.
        // An unpaired wake is an ordering watermark until a matching 506 of
        // a confirmed public suspend arrives.
        // A delayed older 506 must not revoke an already current Ready state;
        // the watermark never manufactures a sleep, Close or physical wake.
        if(!shared || shared<shared_ || !at || at<=lastFact_ || at<=cutoff_)return false;
        syncLocked(shared);
        if(!sleep_){if(at>unpairedWake_){unpairedWake_=at;unpairedStartupUntil_=startupUntil;
            unpairedStartupSerial_=startupSerial;unpairedStartupPbt_=startupPbt;}return false;}
        if(at<=sleep_)return false;
        if(startupUntil&&!startupWake_){startupWake_=at;startupUntil_=startupUntil;
            startupSerial_=startupSerial;startupPbt_=startupPbt;}
        wake_=lastFact_=at;return true;
    }
    std::uint64_t sharedResume(std::uint64_t shared, std::uint64_t latestEntry=0) {
        std::lock_guard<std::mutex> l(mutex_);syncLocked(shared);
        if(!shared || shared!=shared_ || supersededWakeLocked(latestEntry) ||
           (sharedBound_!=generation_ && (!finalWakeAt_ || latestEntry>finalWakeAt_)))return 0;
        sharedAwake_=true;return generation_;
    }
    // Only the public owner can supply a final wake without a matching 507.
    // It still needs public admission and the existing serialized Close.
    bool recordSharedFinalWake(std::uint64_t shared,std::uint64_t at,SharedWake source) {
        std::lock_guard<std::mutex> l(mutex_);
        if(!shared || shared<shared_ || !at || at<=lastFact_)return false;
        syncLocked(shared);
        if(source==SharedWake::None || at<=cutoff_ || at<=unpairedWake_ || finalWakeAt_)return false;
        // Fence a delayed raw 506 even if the public owner already handles Close.
        lastFact_=at;
        firstRaw_=false;
        if(sharedBound_==generation_ || !sleep_ || physicalLocked())return false;
        finalWake_=source;finalWakeAt_=at;
        return true;
    }
    bool recordSharedHibernateWake(std::uint64_t shared,std::uint64_t at) {
        return recordSharedFinalWake(shared,at,SharedWake::Hibernate);
    }
    SharedWake sharedFinalWake(std::uint64_t g) const {
        std::lock_guard<std::mutex> l(mutex_);return g==generation_?finalWake_:SharedWake::None;
    }
    void retryBoundary(std::uint64_t shared,std::uint64_t at) {
        std::lock_guard<std::mutex> l(mutex_);if(shared!=shared_)return;
        if(at>cutoff_)cutoff_=at;wake_=published_=finalWakeAt_=0;finalWake_=SharedWake::None;sharedAwake_=false;
        clearStartupLocked();unpairedStartupUntil_=0;
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
    bool physical(std::uint64_t g,std::uint64_t latestEntry=0) const {
        std::lock_guard<std::mutex> l(mutex_);return g==generation_ && physicalLocked() && latestEntry<=wake_;
    }
    bool needsPhysicalDelivery(std::uint64_t g,std::uint64_t latestEntry) const {
        std::lock_guard<std::mutex> l(mutex_);
        return g==generation_ && physicalLocked() && latestEntry<=wake_ && published_!=g &&
            (readyLocked(g,latestEntry) || (closeUnstarted_==g && !closeOwner_ && !closePending_));
    }
    bool startupCandidate(std::uint64_t g,std::uint64_t latestEntry,std::uint64_t now) const {
        std::lock_guard<std::mutex> l(mutex_);
        return startupCurrentLocked(g,latestEntry,now)&&!startupClaimed_;
    }
    OffStatus cancelStartup(OffRequest request,std::uint64_t fence,bool& withdraw) {
        std::lock_guard<std::mutex> l(mutex_);
        withdraw=false;
        if(request.sequence){
            if(sameOffDocumentLocked(request)){
                if(request.sequence<startupOffRequest_.sequence)return OffStatus::Superseded;
                if(request.sequence==startupOffRequest_.sequence)return startupOffApplied_?OffStatus::Applied:OffStatus::Sealed;
            }
            startupOffRequest_=request;startupOffApplied_=true;startupOffFence_=0;
        }
        withdraw=cancelStartupLocked(fence);
        return OffStatus::Applied;
    }
    OffStatus observeStartupOff(OffRequest request,std::uint64_t fence,std::uint64_t g,
                                std::uint64_t entry,std::uint64_t now,bool& withdraw) {
        std::lock_guard<std::mutex> l(mutex_);
        withdraw=false;
        if(!sameOffDocumentLocked(request)||request.sequence>startupOffRequest_.sequence){
            // Seal once. The cold gate belongs to Native, even when a new
            // document has no receipt or Close was already admitted.
            startupOffRequest_=request;startupOffApplied_=false;startupOffFence_=fence;
            withdraw=cancelStartupLocked(fence);
            return OffStatus::Sealed;
        }
        if(request.sequence<startupOffRequest_.sequence)return OffStatus::Superseded;
        if(startupOffApplied_)return OffStatus::Applied;
        return startupOffFence_ && startupBoundaryAt_>startupOffFence_ && startupCurrentLocked(g,entry,now)
            ? OffStatus::Retired:OffStatus::Sealed;
    }
    bool startupScopeCurrent(std::uint64_t g,std::uint64_t entry,std::uint64_t serial,std::uint64_t pbt) const {
        std::lock_guard<std::mutex> l(mutex_);
        return g==generation_ && startupWake_ && entry==sleep_ && startupSerial_==serial && startupPbt_==pbt &&
            startupBoundaryAt_>startupOffAt_;
    }
    bool claimStartup(std::uint64_t g,std::uint64_t latestEntry,std::uint64_t now,std::uint64_t suspendSerial,
                      std::uint64_t intent,OffRequest off) {
        std::lock_guard<std::mutex> l(mutex_);
        if(startupClaimed_||!startupCurrentLocked(g,latestEntry,now))return false;
        if(off.sequence && (!sameOffDocumentLocked(off)||off.sequence!=startupOffRequest_.sequence||
            (!startupOffApplied_&&(!startupOffFence_||startupBoundaryAt_<=startupOffFence_))))return false;
        startupClaimed_=true;startupSuspend_=suspendSerial;startupIntent_=intent;return true;
    }
    bool startupClaimCurrent(std::uint64_t g,std::uint64_t latestEntry,std::uint64_t now,std::uint64_t suspendSerial,std::uint64_t intent) const {
        std::lock_guard<std::mutex> l(mutex_);
        return startupClaimed_&&startupSuspend_==suspendSerial&&startupIntent_==intent&&startupCurrentLocked(g,latestEntry,now);
    }
    bool publishStartup(std::uint64_t g,std::uint64_t latestEntry,std::uint64_t now,std::uint64_t suspendSerial,std::uint64_t intent) {
        std::lock_guard<std::mutex> l(mutex_);
        if(!startupClaimed_||startupSuspend_!=suspendSerial||startupIntent_!=intent||
           !startupCurrentLocked(g,latestEntry,now)||!readyLocked(g,latestEntry))return false;
        startupPublished_=true;published_=g;return true;
    }
    bool startupPublicationCurrent(std::uint64_t g,std::uint64_t intent,std::uint64_t now) const {
        std::lock_guard<std::mutex> l(mutex_);
        return g==generation_ && startupPublished_ && published_==g && startupIntent_==intent &&
            now<startupUntil_ && startupBoundaryAt_>startupOffAt_;
    }
private:
    bool startupCurrentLocked(std::uint64_t g,std::uint64_t latestEntry,std::uint64_t now) const {
        return g==generation_ && published_!=g && physicalLocked() && startupWake_>sleep_ &&
            latestEntry==sleep_ && now<startupUntil_ && startupBoundaryAt_>startupOffAt_;
    }
public:
    bool supersededWake(std::uint64_t g,std::uint64_t latestEntry) const {
        std::lock_guard<std::mutex> l(mutex_);return g==generation_ && supersededWakeLocked(latestEntry);
    }
    bool awaitingWake(std::uint64_t g,std::uint64_t latestEntry=0) const {
        std::lock_guard<std::mutex> l(mutex_);
        return g==generation_ && (supersededWakeLocked(latestEntry) ||
            (boundary_ && !physicalLocked() && !sharedAwake_));
    }
    bool ready(std::uint64_t g,std::uint64_t latestEntry=0) const {
        std::lock_guard<std::mutex> l(mutex_);return readyLocked(g,latestEntry);
    }
    bool readyUnpublished(std::uint64_t g) const {
        std::lock_guard<std::mutex> l(mutex_);return boundary_ && readyLocked(g) && published_!=g;
    }
    bool claimReady(std::uint64_t g,std::uint64_t latestEntry=0) {
        std::lock_guard<std::mutex> l(mutex_);if(!boundary_ || !readyLocked(g,latestEntry) || published_==g)return false;
        published_=g;return true;
    }
    std::uint64_t publishedReadyGeneration(std::uint64_t shared,std::uint64_t latestEntry=0,
            std::uint64_t startupNow=0,std::uint64_t startupIntent=0,
            std::uint64_t startupSerial=0,std::uint64_t startupPbt=0) const {
        std::lock_guard<std::mutex> l(mutex_);
        return shared==shared_ && published_==generation_ && readyLocked(generation_,latestEntry) &&
            (!startupPublished_ || (startupBoundaryAt_>startupOffAt_ && (!startupNow ||
                (startupNow<startupUntil_ && startupIntent_==startupIntent && latestEntry==sleep_ &&
                 startupSerial_==startupSerial && startupPbt_==startupPbt)))) ? generation_ : 0;
    }
};
}
