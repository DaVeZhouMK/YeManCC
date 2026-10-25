#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ymcc::msi {
// MSI native-return isolation is additional to, not a replacement for, the
// shared foreground virtual-output neutralization. Only proven game inputs on
// the CURRENT leased physical USB location may enter the selected-source hide.
struct ForegroundIsolationFacts {
    bool knownBoard=false, targetEnabled=false, boundTarget=false, uiOwned=false;
    bool hostReady=false, modeOwned=false, exitFence=false, releaseWindow=false;
    bool sourceResolved=false, sourceVirtual=false, samePhysicalLocation=false;
    bool cloakOn=false, inverseOff=false, readerExempt=false;
    unsigned vendorId=0, productId=0, usagePage=0, usage=0;
    uint64_t epoch=0, currentEpoch=0, power=0, currentPower=0;
    uint64_t edge=0, currentEdge=0, observedTick=0, now=0;
    // Receipt-only observations. None of these fields participates in admission.
    uint32_t hostGuardObserved=0, hostGuardPassed=0;
    bool uiOwnershipFlag=false, uiEligibilityEvaluated=false, uiEligibility=false;
    bool modeArmed=false, returnOwedEvaluated=false, returnOwed=false;
    uint64_t anchorRevision=0, currentAnchorRevision=0;
};
enum class ForegroundHostGuard : unsigned {
    InputReady, WriterAllowed, ProcessPresent, Prepared, Neutralized,
    TransportHealthy, PowerAdmitted, JournalActive
};
// Capture the SAME short-circuited expression operand; never query it again.
inline bool foregroundObserveHostGuard(ForegroundIsolationFacts& f,
    ForegroundHostGuard guard, bool value) {
    const auto bit=uint32_t{1}<<static_cast<unsigned>(guard);
    f.hostGuardObserved|=bit;
    if(value) f.hostGuardPassed|=bit; else f.hostGuardPassed&=~bit;
    return value;
}

inline bool foregroundIsolationCurrent(const ForegroundIsolationFacts& f) {
    return f.knownBoard && f.targetEnabled && f.boundTarget && f.uiOwned &&
        f.hostReady && f.modeOwned && !f.exitFence && !f.releaseWindow &&
        f.epoch!=0 && f.epoch==f.currentEpoch && f.power!=0 && f.power==f.currentPower &&
        f.edge!=0 && f.edge==f.currentEdge && f.observedTick!=0 && f.now>=f.observedTick &&
        f.now-f.observedTick<=2000;
}
// Diagnostic decoder only: zero iff the UNCHANGED current predicate permits.
// Invalid timestamps are not misrepresented as a measured stale age.
inline uint32_t foregroundIsolationFailureBits(const ForegroundIsolationFacts& f) {
    uint32_t bits=0;
    if(!f.knownBoard) bits|=1u<<0;
    if(!f.targetEnabled) bits|=1u<<1;
    if(!f.boundTarget) bits|=1u<<2;
    if(!f.uiOwned) bits|=1u<<3;
    if(!f.hostReady) bits|=1u<<4;
    if(!f.modeOwned) bits|=1u<<5;
    if(f.exitFence) bits|=1u<<6;
    if(f.releaseWindow) bits|=1u<<7;
    if(!f.epoch || f.epoch!=f.currentEpoch) bits|=1u<<8;
    if(!f.power || f.power!=f.currentPower) bits|=1u<<9;
    if(!f.edge || f.edge!=f.currentEdge) bits|=1u<<10;
    const bool stampValid=f.observedTick && f.now>=f.observedTick;
    if(!stampValid) bits|=1u<<11;
    if(stampValid && f.now-f.observedTick>2000) bits|=1u<<12;
    return bits;
}
inline bool foregroundNativeInputAdmitted(const ForegroundIsolationFacts& f) {
    return foregroundIsolationCurrent(f) && f.sourceResolved && !f.sourceVirtual &&
        f.vendorId==0x0DB0 && f.productId==0x1901 && f.usagePage==1 &&
        (f.usage==4 || f.usage==5) && f.samePhysicalLocation;
}
inline bool foregroundHideAdmitted(const ForegroundIsolationFacts& f) {
    return foregroundNativeInputAdmitted(f) && f.cloakOn && f.inverseOff && f.readerExempt;
}
inline bool foregroundSiblingGameInputAdmitted(bool sameRoot,bool virtualIdentity,
    unsigned vendor,unsigned product,unsigned usagePage,unsigned usage) {
    // IG_ in a name, SDL display names or "zero buttons" are NOT capability proof.
    return sameRoot && !virtualIdentity && vendor==0x0DB0 && product==0x1901 &&
        usagePage==1 && (usage==4 || usage==5);
}
inline bool samePhysicalLocation(const std::vector<std::wstring>& owned,
                                const std::vector<std::wstring>& candidate) {
    for(const auto& a:owned) if(!a.empty())
        for(const auto& b:candidate) if(!b.empty() && a==b) return true;
    return false;
}
} // namespace ymcc::msi
