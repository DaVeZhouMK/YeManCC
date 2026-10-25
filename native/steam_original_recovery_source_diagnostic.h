#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ymcc::steamOriginalSourceDiagnostic {

enum class Reason {
    Available,
    FixedProfileUnqualified,
    InvalidUser,
    AddressOverflow,
    ModuleRangeInvalid,
    ReadFailed,
    ReadException,
    AnchorMismatch,
    OwnerMismatch,
    VtableMismatch,
    GetterSlotMismatch,
    RawChanged
};

inline const char* reasonName(Reason reason) noexcept {
    switch (reason) {
    case Reason::Available: return "available";
    case Reason::FixedProfileUnqualified: return "fixed-profile-unqualified";
    case Reason::InvalidUser: return "invalid-user";
    case Reason::AddressOverflow: return "address-overflow";
    case Reason::ModuleRangeInvalid: return "module-range-invalid";
    case Reason::ReadFailed: return "read-failed";
    case Reason::ReadException: return "read-exception";
    case Reason::AnchorMismatch: return "anchor-mismatch";
    case Reason::OwnerMismatch: return "owner-mismatch";
    case Reason::VtableMismatch: return "vtable-mismatch";
    case Reason::GetterSlotMismatch: return "getter-slot-mismatch";
    case Reason::RawChanged: return "raw-changed";
    }
    return "unknown";
}

struct Result {
    bool available = false;
    Reason reason = Reason::FixedProfileUnqualified;
    bool ownerMatched = false;
    bool getterVerified = false;
    std::uint32_t userStateRaw = 0;
    std::array<std::uint8_t, 4> postLogonProgressRaw{};
    std::uint8_t logonWaitingRaw = 0;
    std::uint32_t readCalls = 0;
    std::uint32_t requestedBytes = 0;
};

// Fixed steamclient64 SHA256:
// e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e.
// Caller qualifies full image hashes and selects the original account's CUser.
// This optional observation does not grant recovery admission or affect Running.
// No private getter invocation: slot +0x120 (byte offset) must point at a fully
// verified mov rax,rcx; ret leaf. ctor 501730 passes U+48 to 5b8420, whose
// 5b8430 store puts U at (U+48)+668 = U+6b0.
// Repeated reads are bounded consistency checks, NOT an atomic snapshot, ABA
// detection, a complete logon generation, or proof any PostLogon task executed.
// All output fields are uninterpreted raw bytes. No payload or account is read.
// Read(addr, destination, size) -> bool must fill the entire buffer on success.
template<class Read>
Result observe(Read&& read, std::uint64_t clientBase, std::uint64_t imageSize,
               std::uint64_t selectedUser, bool fixedProfileQualified) noexcept {
    Result result;
    if (!fixedProfileQualified) return result;
    if (selectedUser < 0x10000) {
        result.reason = Reason::InvalidUser;
        return result;
    }
    constexpr auto maxAddress = (std::numeric_limits<std::uint64_t>::max)();
    if (imageSize > maxAddress - clientBase || selectedUser > maxAddress - 0x2297) {
        result.reason = Reason::AddressOverflow;
        return result;
    }
    // All client ranges are checked before the first callback. Object fields
    // belong to U, not to the module; only their address arithmetic is checked.
    const auto inModule = [=](std::uint64_t offset, std::uint64_t size) noexcept {
        return offset <= imageSize && size <= imageSize - offset;
    };
    if (clientBase == 0 || !inModule(0x501a0a, 3) || !inModule(0x501a0d, 4) ||
        !inModule(0x501a11, 5) || !inModule(0x5b8430, 7) ||
        !inModule(0x6bc4b0, 4) || !inModule(0x136fa80, 8) ||
        !inModule(0x136fba0, 8)) {
        result.reason = Reason::ModuleRangeInvalid;
        return result;
    }
    const auto fetch = [&](std::uint64_t address, void* dest, std::size_t size) noexcept {
        ++result.readCalls;
        result.requestedBytes += static_cast<std::uint32_t>(size);
        try {
            if (read(address, dest, size)) return true;
            result.reason = Reason::ReadFailed;
        } catch (...) {
            result.reason = Reason::ReadException;
        }
        return false;
    };
    const auto matches = [&]<std::size_t N>(std::uint64_t offset,
                                          const std::array<std::uint8_t, N>& expected) noexcept {
        std::array<std::uint8_t, N> actual{};
        if (!fetch(clientBase + offset, actual.data(), N)) return false;
        if (actual == expected) return true;
        result.reason = Reason::AnchorMismatch;
        return false;
    };
    const auto pointerMatches = [&](std::uint64_t address, std::uint64_t expected,
                                    Reason mismatch) noexcept {
        std::array<std::uint8_t, 8> bytes{};
        if (!fetch(address, bytes.data(), bytes.size())) return false;
        std::uint64_t actual = 0;
        for (std::size_t i = 0; i < bytes.size(); ++i)
            actual |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
        if (actual == expected) return true;
        result.reason = mismatch;
        return false;
    };
    const auto guards = [&]() noexcept {
        return matches(0x501a0a, std::array<std::uint8_t, 3>{0x48, 0x8b, 0xd7}) &&
            matches(0x501a0d, std::array<std::uint8_t, 4>{0x48, 0x8d, 0x4f, 0x48}) &&
            matches(0x501a11, std::array<std::uint8_t, 5>{0xe8, 0x0a, 0x6a, 0x0b, 0x00}) &&
            matches(0x5b8430, std::array<std::uint8_t, 7>{0x48, 0x89, 0x91, 0x68, 0x06, 0x00, 0x00}) &&
            matches(0x6bc4b0, std::array<std::uint8_t, 4>{0x48, 0x8b, 0xc1, 0xc3}) &&
            pointerMatches(selectedUser + 0x6b0, selectedUser, Reason::OwnerMismatch) &&
            pointerMatches(selectedUser, clientBase + 0x136fa80, Reason::VtableMismatch) &&
            pointerMatches(clientBase + 0x136fba0, clientBase + 0x6bc4b0, Reason::GetterSlotMismatch);
    };
    struct Raw {
        std::array<std::uint8_t, 4> state{};
        std::array<std::uint8_t, 4> progress{};
        std::uint8_t waiting = 0;
    };
    const auto raw = [&](Raw& value) noexcept {
        return fetch(selectedUser + 0x178, value.state.data(), value.state.size()) &&
            fetch(selectedUser + 0x2294, value.progress.data(), value.progress.size()) &&
            fetch(selectedUser + 0x17c, &value.waiting, 1);
    };
    Raw first{}, second{};
    // Both raw collections are enclosed by complete guard collections.
    // Successful path: 2 * (5 anchors + 3 pointers + 3 raw) = 22 calls, 112 B.
    if (!guards() || !raw(first) || !raw(second) || !guards()) return result;
    if (first.state != second.state || first.progress != second.progress ||
        first.waiting != second.waiting) {
        result.reason = Reason::RawChanged;
        return result;
    }
    for (std::size_t i = 0; i < first.state.size(); ++i)
        result.userStateRaw |= static_cast<std::uint32_t>(first.state[i]) << (i * 8);
    result.postLogonProgressRaw = first.progress;
    result.logonWaitingRaw = first.waiting;
    result.ownerMatched = true;
    result.getterVerified = true;
    result.reason = Reason::Available;
    result.available = true;
    return result;
}

} // namespace ymcc::steamOriginalSourceDiagnostic
