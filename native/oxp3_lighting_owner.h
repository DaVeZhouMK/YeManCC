#pragma once

// Pure owner-admission state for the existing OneX vendor owner. The native
// owner remains the only writer of the shared collection; helper processes are
// admitted through a generation-specific revocation event plus an authoritative
// handoff record only after this lease is active.
#include "onex_x2_protocol.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ymcc::oxp3_owner {

// One manual-reset event per admitted generation. A later lease never resets or
// reuses an earlier event object, so a stale helper cannot be revived by a new
// reservation.
inline constexpr wchar_t kRevokedEventPrefix[] = L"Local\\YeManCC-OXP3-VendorRevoked-";
// Authoritative cross-process handoff record. The helper must match the current
// published generation token, exact endpoint and a fresh owner heartbeat before
// opening or writing; the event is only a revocation notification.
inline constexpr wchar_t kHandoffRecordName[] = L"Local\\YeManCC-OXP3-OwnerLease";
inline constexpr std::uint32_t kHandoffMagic = 0x4F585033u;
inline constexpr std::uint32_t kHandoffVersion = 2u;
inline constexpr std::uint32_t kX2MiniProModelTag = 2u;
inline constexpr std::uint32_t kOneXPlayer3ModelTag = 3u;
static_assert(static_cast<std::uint32_t>(ymcc::onex_x2::Model::X2MiniPro) == kX2MiniProModelTag);
static_assert(static_cast<std::uint32_t>(ymcc::onex_x2::Model::OneXPlayer3) == kOneXPlayer3ModelTag);
inline constexpr const char* kGenerationSeparator = ":";
inline constexpr std::size_t kMaxGenerationToken = 40;
inline constexpr std::size_t kMaxInterfacePath = 520;

enum class OwnerState : std::uint32_t { Idle = 0, Admitting = 1, Active = 2, Retiring = 3 };

#pragma pack(push, 1)
struct HandoffRecord {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint64_t sequence;
    std::uint64_t generation;
    std::uint64_t topology;
    std::uint64_t admission;
    std::uint64_t heartbeat;
    std::uint32_t state;
    std::uint32_t modelTag; // v2 binds the source-specific writer without changing record size
    char token[kMaxGenerationToken];
    wchar_t interfacePath[kMaxInterfacePath];
};
#pragma pack(pop)
static_assert(sizeof(HandoffRecord) == 1136, "OXP3 handoff record layout");
static_assert(offsetof(HandoffRecord, modelTag) == 52, "OneX owner model tag offset");

// Each admission gets a unique identity in addition to the live device
// generation/topology, so two sequential leases on the same unchanged device
// never share a token or event object.
inline std::string makeAdmissionToken(std::uint64_t generation, std::uint64_t topology,
                                      std::uint64_t admission) {
    return std::to_string(generation) + kGenerationSeparator + std::to_string(topology) +
        kGenerationSeparator + std::to_string(admission);
}

struct Lease {
    std::atomic<bool> active{false};
    std::atomic<bool> revoked{false};
    std::atomic<std::uint64_t> generation{0};
    std::atomic<std::uint64_t> topology{0};
    std::atomic<std::uint64_t> admission{0};
    std::atomic<std::uint32_t> modelTag{0};
    std::string generationToken;
    std::string interfacePath;
};

inline bool writeAllowed(bool ownerAllowed, const Lease& lease) noexcept {
    return ownerAllowed && !lease.active.load(std::memory_order_acquire);
}

inline bool bindAllowed(bool ownerAllowed, const Lease& lease) noexcept {
    return ownerAllowed && !lease.active.load(std::memory_order_acquire);
}

inline bool sequenceCurrent(const Lease& lease, std::uint64_t generation,
                            std::uint64_t topology, const std::string& interfacePath) noexcept {
    return lease.active.load(std::memory_order_acquire) &&
        !lease.revoked.load(std::memory_order_acquire) &&
        lease.generation.load(std::memory_order_acquire) == generation &&
        lease.topology.load(std::memory_order_acquire) == topology &&
        lease.interfacePath == interfacePath;
}

// Only the owner holding this exact admission token may release the lease. A
// late receipt from an older request cannot free a newer owner.
inline bool releaseAllowed(const Lease& lease, const std::string& token) noexcept {
    return lease.active.load(std::memory_order_acquire) && !lease.generationToken.empty() &&
        lease.generationToken == token;
}

} // namespace ymcc::oxp3_owner
