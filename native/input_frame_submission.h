#pragma once
#include <cstdint>
#include "virtual_output_status.h"

namespace ymcc::input::frameSubmission {
using Token=std::uint64_t;
enum class Verdict : unsigned {Allowed,PowerUnavailable,HostChanged,PowerChanged,IntentChanged,SourceUnavailable,SourceRetired,SourcePublicationPending};
enum class AssemblyCause {None,PowerScopeUnavailable,SharedPowerObservationRetired,SourceUnavailable,SourceContextRetired,ReadAlreadyConsumed};
inline const char* assemblyCauseName(AssemblyCause cause) noexcept {
    switch(cause){
    case AssemblyCause::PowerScopeUnavailable:return "input-power-scope-unavailable";
    case AssemblyCause::SharedPowerObservationRetired:return "shared-power-observation-retired";
    case AssemblyCause::SourceUnavailable:return "canonical-source-unavailable";
    case AssemblyCause::SourceContextRetired:return "canonical-window-or-binding-retired";
    case AssemblyCause::ReadAlreadyConsumed:return "canonical-read-already-consumed";
    default:return "none";
    }
}
struct Sample {
    VirtualOutputSourceWindow window{};
    unsigned identity=0;
    Token msiRevision=0;
    bool live=false,routeKnown=false,msi=false;
    Token msiMode=0;
};
struct Ticket {Token host=0,power=0,intent=0;bool neutral=false;};
struct Current {
    Token host=0,power=0,intent=0,msiRevision=0;
    unsigned topology=0,identity=0;
    bool ready=false,intentChanging=false,sourceEligible=false;
    Token msiMode=0;
};
inline Verdict source(const Sample& s,const Current& c,Token now) noexcept {
    if(!s.live || !s.routeKnown || !c.sourceEligible)return Verdict::SourceUnavailable;
    if(!virtualOutputSourceCurrent(s.window,now,c.power,c.topology) || s.identity!=c.identity ||
       (s.msi && (!s.msiRevision || s.msiRevision!=c.msiRevision || !s.msiMode || s.msiMode!=c.msiMode)))return Verdict::SourceRetired;
    return Verdict::Allowed;
}
// Actual canonical API-read sequence, never XInput packet/button/axis changes.
// Protected by the existing Host transaction mutex. Keep each provider's
// progress independently so a route switch cannot revive its cached old read.
class ReadProgress {
    struct Entry {
        Token sequence=0,power=0,msiRevision=0,msiMode=0;
        unsigned topology=0,identity=0;
        bool observed=false;
    } entries_[2]{};
public:
    bool advance(const Sample& s) noexcept {
        if(!s.window.readAttemptSequence)return false;
        auto& p=entries_[s.msi?1:0];
        const bool same=p.observed && p.power==s.window.readPowerGeneration &&
            p.topology==s.window.readTopologyGeneration && p.identity==s.identity &&
            (!s.msi || (p.msiRevision==s.msiRevision && p.msiMode==s.msiMode));
        if(same && s.window.readAttemptSequence<=p.sequence)return false;
        p.sequence=s.window.readAttemptSequence;
        if(!same){
            p.power=s.window.readPowerGeneration;p.topology=s.window.readTopologyGeneration;
            p.identity=s.identity;p.msiRevision=s.msi?s.msiRevision:0;p.msiMode=s.msi?s.msiMode:0;
            p.observed=true;
        }
        return true;
    }
};
// Consume once at normal-frame assembly only. Request/MayIssue revalidation
// still uses source(), so the SAME in-flight frame does not consume twice.
// A known-valid first read retains the original opening-neutral admission.
inline Verdict sourceForNewRead(const Sample& s,const Current& c,Token now,ReadProgress& progress,AssemblyCause& cause) noexcept {
    const auto verdict=source(s,c,now);
    if(verdict!=Verdict::Allowed){
        cause=verdict==Verdict::SourceUnavailable?AssemblyCause::SourceUnavailable:AssemblyCause::SourceContextRetired;
        return verdict;
    }
    if(progress.advance(s)){cause=AssemblyCause::None;return Verdict::Allowed;}
    cause=AssemblyCause::ReadAlreadyConsumed;
    return Verdict::SourceRetired;
}

// Log-only key. Read sequence and sample timestamps are evidence, not identities.
struct ReadLogContext {
    Token host=0,power=0,intent=0,msiRevision=0,msiMode=0;
    Token capturedHost=0,capturedPower=0,capturedIntent=0,samplePower=0,sampleMsiRevision=0,sampleMsiMode=0;
    Token powerObservation=0,powerPublication=0,powerNotification=0,powerSuspend=0;
    Token motionBinding=0,activeMotionBinding=0,motionChoice=0,activeMotionChoice=0;
    unsigned topology=0,identity=0,sampleTopology=0,sampleIdentity=0,powerPhase=0,powerFlags=0;
    bool msi=false,hasWindowsMotion=false,motionCurrent=false,hasMotionChoice=false;
    bool hasCaptureLease=false,captureAlive=false,hasProcessingLease=false,processingAlive=false;
    bool hasControllerMotion=false,qualified=false;
    bool operator==(const ReadLogContext&) const = default;
};
enum class ReadLogSelection {Skip,Start,Recovery,Summary,Sample,ContextChanged};
struct ReadRepeatLog {
    static constexpr Token maxGapMs=1000,summaryIntervalMs=10000;
    ReadLogContext context{};
    Token start=0,lastRejection=0,lastSummary=0,rejections=0,recoveries=0;
    bool open=false,recovered=false;

    ReadLogSelection reject(const ReadLogContext& key,Token now) noexcept {
        if(!open || key!=context || now<lastRejection || now-lastRejection>maxGapMs){
            *this={};context=key;start=lastSummary=now;open=true;
            lastRejection=now;return ReadLogSelection::Start;
        }
        lastRejection=now;++rejections;
        if(now-lastSummary>=summaryIntervalMs)return ReadLogSelection::Summary;
        return ReadLogSelection::Sample;
    }
    ReadLogSelection accept(const ReadLogContext& key,Token now,bool followsRepeat) noexcept {
        if(key!=context || now<lastRejection){*this={};return ReadLogSelection::ContextChanged;}
        if(!recovered){recovered=true;return ReadLogSelection::Recovery;}
        if(now-lastRejection>maxGapMs){*this={};return ReadLogSelection::Skip;}
        if(!followsRepeat)return ReadLogSelection::Skip;
        ++recoveries;return ReadLogSelection::Sample;
    }
    void summarized(Token now) noexcept {lastSummary=now;rejections=recoveries=0;}
};
inline Verdict decide(const Ticket& t,const Sample& s,const Current& c,Token now) noexcept {
    if(!c.ready)return Verdict::PowerUnavailable;
    if(!t.host || t.host!=c.host)return Verdict::HostChanged;
    if(!t.power || t.power!=c.power)return Verdict::PowerChanged;
    if(c.intentChanging || t.intent!=c.intent)return Verdict::IntentChanged;
    // Full neutral releases the prior report of this SAME target. Source
    // absence/change cannot prevent it; target/power/intent still must match.
    return t.neutral?Verdict::Allowed:source(s,c,now);
}
inline const char* name(Verdict v) noexcept {
    switch(v){
    case Verdict::Allowed:return "current-frame";
    case Verdict::PowerUnavailable:return "power-or-target-unavailable";
    case Verdict::HostChanged:return "host-epoch-changed";
    case Verdict::PowerChanged:return "power-generation-changed";
    case Verdict::IntentChanged:return "target-intent-changing-or-changed";
    case Verdict::SourceUnavailable:return "canonical-source-unavailable";
    case Verdict::SourceRetired:return "canonical-read-context-retired";
    case Verdict::SourcePublicationPending:return "windows-source-publication-not-committed";
    }
    return "unknown";
}
} // namespace ymcc::input::frameSubmission
