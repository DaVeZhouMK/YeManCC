#pragma once
#include <cstddef>
#include <cstdint>

namespace ymcc::sleeppower {
// Read-only projection of the one Native sleep owner. This ABI carries no
// resource-cleanup receipt and grants no device-specific capability.
struct SharedPowerV1 {
    std::uint32_t size=sizeof(SharedPowerV1),version=1;
    std::uint64_t generation=0,publicationRevision=0,observedNotificationSequence=0;
    std::uint32_t phase=0,flags=0;
    std::uint64_t suspendSerial=0;
};
static_assert(sizeof(SharedPowerV1)==48);
static_assert(offsetof(SharedPowerV1,generation)==8);
static_assert(offsetof(SharedPowerV1,phase)==32);
static_assert(offsetof(SharedPowerV1,suspendSerial)==40);
inline constexpr std::uint32_t kKnown=1,kHardwareAllowed=2,kNotificationPending=4;
// phases: 0 Ready, 1 Suspending, 2 Suspended, 3 Resuming.
// Return 1 only for a coherent current snapshot; 0 means UNKNOWN, never Ready.
// A successful call writes the entire 48-byte value. No caller initialization
// is required. The callback never samples Windows, waits, or loads a component.
using Reader=int(__cdecl*)(SharedPowerV1*,std::uint32_t bytes);

// Snapshot admission is shared; a device/Host capability still needs its owner.
inline constexpr bool powerAdmitted(const SharedPowerV1& value) noexcept {
    return value.size==sizeof(SharedPowerV1)&&value.version==1&&value.generation&&value.phase==0&&
        (value.flags&(kKnown|kHardwareAllowed))==(kKnown|kHardwareAllowed)&&!(value.flags&kNotificationPending);
}
inline constexpr bool samePowerRevocation(const SharedPowerV1& captured,const SharedPowerV1& current) noexcept {
    return powerAdmitted(captured)&&powerAdmitted(current)&&captured.generation==current.generation&&
        captured.suspendSerial==current.suspendSerial&&
        captured.observedNotificationSequence==current.observedNotificationSequence&&
        captured.phase==current.phase&&captured.flags==current.flags&&
        current.publicationRevision>=captured.publicationRevision;
}
inline bool capturePower(Reader read,SharedPowerV1& output) noexcept {
    output=SharedPowerV1{};
    if(!read)return false;
    SharedPowerV1 observed;
    try{if(read(&observed,sizeof(observed))!=1||!powerAdmitted(observed))return false;}
    catch(...){return false;}
    output=observed;return true;
}
inline bool powerCurrent(Reader read,const SharedPowerV1& captured) noexcept {
    SharedPowerV1 current;
    return powerAdmitted(captured)&&capturePower(read,current)&&samePowerRevocation(captured,current);
}
// Consumers needing the exact publication may choose this stricter comparison.
inline bool powerPublicationCurrent(Reader read,const SharedPowerV1& captured) noexcept {
    SharedPowerV1 current;
    return powerAdmitted(captured)&&capturePower(read,current)&&samePowerRevocation(captured,current)&&
        captured.publicationRevision==current.publicationRevision;
}
}
