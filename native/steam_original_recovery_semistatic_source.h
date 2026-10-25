#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace ymcc::steamOriginalSemiStaticSource {

inline constexpr std::uint32_t kMaxReadCalls = 64;
inline constexpr std::uint32_t kMaxRequestedBytes = 1024;
inline constexpr std::uint32_t kSuccessfulReadCalls = 32;
inline constexpr std::uint32_t kSuccessfulRequestedBytes = 218;

enum class Reason {
    Available,
    FixedProfileUnqualified,
    InvalidUser,
    InvalidUserMap,
    AddressOverflow,
    ModuleRangeInvalid,
    ReadFailed,
    ReadException,
    ReadBudgetExceeded,
    AnchorMismatch,
    OwnerMismatch,
    VtableMismatch,
    RouteMismatch,
    UserMapMismatch,
    UserMapChanged,
    UserInterfaceMismatch,
    UserInterfaceChanged,
    UiPipeMismatch,
    UiPipeChanged,
    RawChanged
};

inline const char* reasonName(Reason reason) noexcept {
    switch (reason) {
    case Reason::Available: return "available";
    case Reason::FixedProfileUnqualified: return "fixed-profile-unqualified";
    case Reason::InvalidUser: return "invalid-user";
    case Reason::InvalidUserMap: return "invalid-user-map";
    case Reason::AddressOverflow: return "address-overflow";
    case Reason::ModuleRangeInvalid: return "module-range-invalid";
    case Reason::ReadFailed: return "read-failed";
    case Reason::ReadException: return "read-exception";
    case Reason::ReadBudgetExceeded: return "read-budget-exceeded";
    case Reason::AnchorMismatch: return "anchor-mismatch";
    case Reason::OwnerMismatch: return "owner-mismatch";
    case Reason::VtableMismatch: return "vtable-mismatch";
    case Reason::RouteMismatch: return "route-mismatch";
    case Reason::UserMapMismatch: return "user-map-mismatch";
    case Reason::UserMapChanged: return "user-map-changed";
    case Reason::UserInterfaceMismatch: return "user-interface-mismatch";
    case Reason::UserInterfaceChanged: return "user-interface-changed";
    case Reason::UiPipeMismatch: return "ui-pipe-mismatch";
    case Reason::UiPipeChanged: return "ui-pipe-changed";
    case Reason::RawChanged: return "raw-changed";
    }
    return "unknown";
}

struct Result {
    bool available = false;
    Reason reason = Reason::FixedProfileUnqualified;
    std::uint32_t readCalls = 0;
    std::uint32_t requestedBytes = 0;
    bool userMapMatched = false;
    bool userInterfaceMatched = false;
    bool uiPipeMatched = false;
    std::uint32_t cmStateRaw = 0;
    bool cmConnectionPresent = false;
    std::uint8_t reconnectFlagRaw = 0;
    std::uint32_t uiLocalPipe = 0;
};

// The caller's existing snapshot must qualify BOTH full module hashes:
// client e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e
// UI     1b9457943205dc3af0bd53463ba331cbd2a381388699705c02271aac7a2b1252
// and select the original CUser. This observer does not hash, discover users,
// invoke getters/private methods, touch queues, write, or grant admission.
// Read(address, destination, size) -> bool must fill the whole buffer on true;
// its implementation owns safe remote-memory access. C++ exceptions are caught.
// Calls/bytes count callback ATTEMPTS, including failure/exception, not bytes
// actually returned. Unqualified profiles do zero reads. No pointers/account
// identifiers escape Result; raw/matched fields are published only on success.
// Guards -> raw -> raw -> guards is a bounded consistency observation, NOT an
// atomic snapshot, ABA detection or a complete identity/logon generation.
// Values are uninterpreted; even an unknown raw state/flag can be available.
// Success: 2 * (13 guard reads / 100B + 3 raw reads / 9B) = 32 reads / 218B.
template<class Read>
Result observe(Read&& read, std::uint64_t clientBase, std::uint64_t clientSize,
               std::uint64_t uiBase, std::uint64_t uiSize,
               std::uint64_t selectedUser, bool fixedProfileQualified) noexcept {
    Result result;
    if (!fixedProfileQualified) return result;
    constexpr auto maxAddress = (std::numeric_limits<std::uint64_t>::max)();
    constexpr std::uint64_t minimumPointer = 0x10000;
    constexpr std::uint64_t userSpan = 0x6922;
    if (selectedUser < minimumPointer) {
        result.reason = Reason::InvalidUser;
        return result;
    }
    if (clientSize > maxAddress - clientBase || uiSize > maxAddress - uiBase ||
        selectedUser > maxAddress - userSpan) {
        result.reason = Reason::AddressOverflow;
        return result;
    }
    const auto inModule = [](std::uint64_t size, std::uint64_t offset,
                             std::uint64_t bytes) noexcept {
        return offset <= size && bytes <= size - offset;
    };
    // Validate every static range before ANY read/address addition. Object and
    // map fields are heap ranges; their arithmetic is checked separately.
    if (clientBase < minimumPointer || uiBase < minimumPointer ||
        !inModule(clientSize, 0x136fa80, 8) ||
        !inModule(clientSize, 0x136fbd0, 8) ||
        !inModule(clientSize, 0x136fc08, 8) ||
        !inModule(clientSize, 0x1329288, 8) ||
        !inModule(clientSize, 0x9d71f0, 12) ||
        !inModule(clientSize, 0x9d71e0, 16) ||
        !inModule(uiSize, 0x51fdb0, 8) ||
        !inModule(uiSize, 0x118b908, 8) ||
        !inModule(uiSize, 0x118ba98, 4)) {
        result.reason = Reason::ModuleRangeInvalid;
        return result;
    }
    const auto fetch = [&](std::uint64_t address, void* destination,
                           std::size_t bytes) noexcept {
        if (result.readCalls >= kMaxReadCalls ||
            bytes > kMaxRequestedBytes - result.requestedBytes) {
            result.reason = Reason::ReadBudgetExceeded;
            return false;
        }
        if (address < minimumPointer ||
            static_cast<std::uint64_t>(bytes) > maxAddress - address) {
            result.reason = Reason::AddressOverflow;
            return false;
        }
        ++result.readCalls;
        result.requestedBytes += static_cast<std::uint32_t>(bytes);
        try {
            if (read(address, destination, bytes)) return true;
            result.reason = Reason::ReadFailed;
        } catch (...) {
            result.reason = Reason::ReadException;
        }
        return false;
    };
    const auto scalar = [&](std::uint64_t address, auto& value) noexcept {
        using Value = std::remove_reference_t<decltype(value)>;
        static_assert(std::is_integral_v<Value> && std::is_unsigned_v<Value> &&
                      sizeof(Value) <= sizeof(std::uint64_t));
        std::array<std::uint8_t, sizeof(Value)> bytes{};
        if (!fetch(address, bytes.data(), bytes.size())) return false;
        std::uint64_t decoded = 0;
        for (std::size_t i = 0; i < bytes.size(); ++i)
            decoded |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
        value = static_cast<Value>(decoded);
        return true;
    };
    const auto pointer = [&](std::uint64_t address, std::uint64_t expected,
                             Reason mismatch) noexcept {
        std::uint64_t actual = 0;
        if (!scalar(address, actual)) return false;
        if (actual == expected) return true;
        result.reason = mismatch;
        return false;
    };
    const auto anchor = [&](std::uint64_t address, const auto& expected) noexcept {
        auto actual = expected;
        actual.fill(0);
        if (!fetch(address, actual.data(), actual.size())) return false;
        if (actual == expected) return true;
        result.reason = Reason::AnchorMismatch;
        return false;
    };
    struct Guard {
        std::uint64_t map = 0;
        std::uint32_t localUser = 0;
        std::uint32_t pipe = 0;
    };
    const auto guards = [&](Guard& value, const Guard* previous) noexcept {
        if (!pointer(selectedUser, clientBase + 0x136fa80, Reason::VtableMismatch) ||
            !pointer(selectedUser + 0x6b0, selectedUser, Reason::OwnerMismatch) ||
            !pointer(selectedUser + 0x1e70, clientBase + 0x136fbd0, Reason::VtableMismatch) ||
            !pointer(clientBase + 0x136fc08, clientBase + 0x9d71f0, Reason::RouteMismatch) ||
            !anchor(clientBase + 0x9d71f0, std::array<std::uint8_t, 12>{
                0x48,0x81,0xe9,0x70,0x1e,0x00,0x00,0xe9,0xe4,0xff,0xff,0xff}) ||
            !anchor(clientBase + 0x9d71e0, std::array<std::uint8_t, 16>{
                0xc6,0x81,0x21,0x69,0x00,0x00,0x01,0x48,
                0x83,0xc1,0x48,0xe9,0x00,0x99,0xbd,0xff}) ||
            !anchor(uiBase + 0x51fdb0, std::array<std::uint8_t, 8>{
                0x48,0x8b,0x05,0x51,0xbb,0xc6,0x00,0xc3}) ||
            !scalar(uiBase + 0x118b908, value.map)) return false;
        if (previous != nullptr && value.map != previous->map) {
            result.reason = Reason::UserMapChanged;
            return false;
        }
        if (value.map < minimumPointer) {
            result.reason = Reason::InvalidUserMap;
            return false;
        }
        if (value.map > maxAddress - 0x10) {
            result.reason = Reason::AddressOverflow;
            return false;
        }
        if (!pointer(value.map, clientBase + 0x1329288, Reason::UserMapMismatch) ||
            !scalar(value.map + 8, value.localUser)) return false;
        std::uint32_t ownerUser = 0;
        if (!scalar(selectedUser + 0x40, ownerUser)) return false;
        if (value.localUser != ownerUser) {
            result.reason = Reason::UserInterfaceMismatch;
            return false;
        }
        if (previous != nullptr && value.localUser != previous->localUser) {
            result.reason = Reason::UserInterfaceChanged;
            return false;
        }
        if (!scalar(value.map + 0xc, value.pipe)) return false;
        std::uint32_t uiPipe = 0;
        if (!scalar(uiBase + 0x118ba98, uiPipe)) return false;
        if (value.pipe == 0 || value.pipe != uiPipe) {
            result.reason = Reason::UiPipeMismatch;
            return false;
        }
        if (previous != nullptr && value.pipe != previous->pipe) {
            result.reason = Reason::UiPipeChanged;
            return false;
        }
        return true;
    };
    struct Raw {
        std::uint32_t state = 0;
        std::uint32_t connection = 0;
        std::uint8_t flag = 0;
    };
    const auto raw = [&](Raw& value) noexcept {
        return scalar(selectedUser + 0x178, value.state) &&
            scalar(selectedUser + 0x60c, value.connection) &&
            scalar(selectedUser + 0x6921, value.flag);
    };
    Guard firstGuard{}, secondGuard{};
    Raw first{}, second{};
    if (!guards(firstGuard, nullptr) || !raw(first) || !raw(second) ||
        !guards(secondGuard, &firstGuard)) return result;
    if (first.state != second.state || first.connection != second.connection ||
        first.flag != second.flag) {
        result.reason = Reason::RawChanged;
        return result;
    }
    result.cmStateRaw = first.state;
    result.cmConnectionPresent = first.connection != 0;
    result.reconnectFlagRaw = first.flag;
    result.uiLocalPipe = firstGuard.pipe;
    result.userMapMatched = true;
    result.userInterfaceMatched = true;
    result.uiPipeMatched = true;
    result.reason = Reason::Available;
    result.available = true;
    return result;
}

} // namespace ymcc::steamOriginalSemiStaticSource
