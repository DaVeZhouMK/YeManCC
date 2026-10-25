#pragma once
// Portable OEM semantic contract. No OS, timer, HID, service or virtual-target I/O.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ymcc::oem::gamepad {
inline constexpr unsigned contractVersion = 1;
enum class Vendor { Unknown, Msi, AsusRog, Lenovo };
enum class Intent { NativeGamepad, VirtualCapture };
inline bool validIntent(Intent i){return i==Intent::NativeGamepad||i==Intent::VirtualCapture;}
enum class InputProtocol { Unknown, XInput, DirectInput, RawHid };
// The route ID is adapter-owned, NEVER a global numeric firmware mode.
struct PhysicalRoute {
    Vendor vendor=Vendor::Unknown;
    InputProtocol protocol=InputProtocol::Unknown;
    std::string id;
    // Composite capture (e.g. primary XInput + vendor HID) is not DInput mode.
    std::vector<InputProtocol> auxiliaryProtocols;
    bool operator==(const PhysicalRoute&) const = default;
    bool supported() const {
        if((vendor!=Vendor::Msi&&vendor!=Vendor::AsusRog&&vendor!=Vendor::Lenovo) ||
           (protocol!=InputProtocol::XInput&&protocol!=InputProtocol::DirectInput&&protocol!=InputProtocol::RawHid) || id.empty() || id.size()>96) return false;
        if(auxiliaryProtocols.size()>2) return false;
        for(size_t i=0;i<auxiliaryProtocols.size();++i) {
            const auto p=auxiliaryProtocols[i];
            if((p!=InputProtocol::XInput&&p!=InputProtocol::DirectInput&&p!=InputProtocol::RawHid) || p==protocol) return false;
            for(size_t j=0;j<i;++j) if(auxiliaryProtocols[j]==p) return false;
        }
        for(char c:id) if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='.')) return false;
        return true;
    }
};
inline const PhysicalRoute unsupportedRoute{};
// Independently classify observed hardware state; transport does not identify
// DInput firmware state, and topology/FPS/manual choices are not OEM auto writes.
enum class HardwareState { Unknown, NativeGamepad, VirtualCaptureCompatible,
    Desktop, FrontendExclusive, DetachedDualInput, FpsHardware, ControllerOff };
enum class TransitionOrigin { Unknown, ExplicitUserIntent, OemAutoProven,
    OemUiProven, PhysicalTopology, PhysicalSwitch, OfficialShortcut, PowerResume };
enum class RegistryView { View32, View64 };
enum class ReadState { Value, MissingValue, MissingKey, Unknown };
struct Value {
    bool exists = false;
    uint32_t type = 0;
    std::vector<uint8_t> bytes;
    bool operator==(const Value&) const = default;
};
struct Address {
    RegistryView view = RegistryView::View32;
    std::wstring subkey, name; // Relative to HKLM; no embedded WOW6432Node.
    bool operator==(const Address&) const = default;
};
struct ReadResult { ReadState state = ReadState::Unknown; Value value; };
struct Patch { Address address; Value desired; };
// This is a serial-worker lease identity, NOT evidence of a human gesture.
struct Owner {
    std::string processNonce;
    uint64_t session = 0, hostEpoch = 0, powerGeneration = 0, intentRevision = 0;
    bool operator==(const Owner&) const = default;
};
inline bool validOwner(const Owner& o) {
    if (o.processNonce.size() < 16 || o.processNonce.size() > 64 ||
        !o.session || !o.hostEpoch || !o.powerGeneration || !o.intentRevision) return false;
    for (char c : o.processNonce)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}
struct Capability {
    Vendor vendor = Vendor::Unknown;
    // Bound adapter semantics/identity, NOT a live policy readback or mode lock.
    bool policyCapabilityVerified = false;
    // Separate source/firmware capabilities; all are declarations, not live proof.
    bool firmwareModeCapabilityVerified = false;
    bool captureCapabilityVerified = false;
    bool allModeWritersExcluded = false; // MSI's two settings cannot establish this.
    PhysicalRoute nativeRoute = unsupportedRoute;
    PhysicalRoute virtualRoute = unsupportedRoute;
    std::string binding, evidence;
};
struct Request { Owner owner; Intent intent = Intent::VirtualCapture; bool userAuthorized = false; };
struct Entry { Patch patch; Value original; bool mayHaveWritten = false; };
struct Record {
    unsigned schema = contractVersion;
    Vendor vendor = Vendor::Unknown;
    Owner owner;
    Intent intent = Intent::VirtualCapture;
    std::string binding;
    bool pending = true;
    std::vector<Entry> entries;
};
enum class JournalState { Absent, Record, Invalid };
struct JournalRead { JournalState state = JournalState::Invalid; Record record; };
struct RegistryIo {
    virtual ~RegistryIo() = default;
    virtual ReadResult read(const Address&) = 0;
    // Only existing keys may be opened. No RegCreateKey or cross-view fallback.
    virtual bool write(const Address&, const Value&) = 0;
};
struct JournalStore {
    virtual ~JournalStore() = default;
    virtual JournalRead load() = 0;
    // Return true ONLY after a durable write-ahead record has been committed.
    virtual bool save(const Record&) = 0;
};
struct AuthorityGate {
    virtual ~AuthorityGate() = default;
    // Revalidate the latest owner/request immediately before each config write.
    virtual bool current(const Owner&) = 0;
};
struct Adapter {
    virtual ~Adapter() = default;
    virtual Capability capability() const = 0;
    virtual std::vector<Patch> patches(Intent) const = 0;
    // Validate journal identity, addresses, desired bytes, original types/bounds.
    virtual bool validRecord(const Record&) const = 0;
};
enum class Status {
    Acquired, AlreadyOwned, Released, Recovered, NoLease,
    Unsupported, Unauthorized, InvalidOwner, Busy, Stale, ReadUnknown,
    JournalInvalid, JournalFailure, Conflict, WriteFailure, CleanupPending
};
struct Result {
    Status status = Status::Unsupported;
    unsigned attemptedWrites = 0, verifiedWrites = 0, externalChangesPreserved = 0;
    bool policyVerified = false, cleanupPending = false, leaseActive = false;
    // Policy/ACK success is never physical-mode or external-input proof.
    bool hardwareModeVerified = false, allModeWritersExcluded = false;
    PhysicalRoute requestedRoute = unsupportedRoute;
};
inline const char* statusName(Status s) {
    switch(s) {
    case Status::Acquired:return "acquired"; case Status::AlreadyOwned:return "already-owned";
    case Status::Released:return "released"; case Status::Recovered:return "recovered";
    case Status::NoLease:return "no-lease"; case Status::Unsupported:return "unsupported";
    case Status::Unauthorized:return "unauthorized"; case Status::InvalidOwner:return "invalid-owner";
    case Status::Busy:return "busy"; case Status::Stale:return "stale";
    case Status::ReadUnknown:return "read-unknown"; case Status::JournalInvalid:return "journal-invalid";
    case Status::JournalFailure:return "journal-failure"; case Status::Conflict:return "conflict";
    case Status::WriteFailure:return "write-failure"; case Status::CleanupPending:return "cleanup-pending";
    }
    return "unknown";
}
// Future providers stay explicitly unavailable; a vendor name alone is no authority.
class UnverifiedAdapter final : public Adapter {
    Vendor vendor_;
public:
    explicit UnverifiedAdapter(Vendor v):vendor_(v){}
    Capability capability() const override { Capability c;c.vendor=vendor_;return c; }
    std::vector<Patch> patches(Intent) const override { return {}; }
    bool validRecord(const Record&) const override { return false; }
};
} // namespace ymcc::oem::gamepad






