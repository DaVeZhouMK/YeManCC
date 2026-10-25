#pragma once
#include <cstddef>
#include <cstdint>

#if !defined(_WIN32) || !defined(_M_X64)
#error This ABI requires Windows x64 and MSVC-compatible SEH.
#endif
#if defined(OSD_USE_DLL)
#define OSD_API __declspec(dllimport)
#else
#define OSD_API
#endif

namespace osd {
inline constexpr std::uint32_t kAbiVersion = 1;
inline constexpr std::size_t kGetterSize = 14;
inline constexpr std::size_t kSelfBackOffset = 0x1c66;
inline constexpr std::uint8_t kGetterBytes[kGetterSize] = {
    0x48, 0x8b, 0x81, 0x9a, 0xe3, 0xff, 0xff,
    0x48, 0x89, 0x02, 0x48, 0x8b, 0xc2, 0xc3
};
using Getter = std::uint64_t* (__fastcall*)(void* self, std::uint64_t* out);
enum class CallbackResult : std::uint32_t { Completed = 0, Rejected = 1 };
// Synchronous, bounded callback on the qualifying thread. No exceptions, management
// API calls, thread waits, freeing self/out/cookie, or changing owner/account binding.
// Recursive getter calls and OsdGetStatus are allowed. Violations are not sandboxed;
// unknown exceptions propagate and permanently consume the attempt.
using Callback = CallbackResult (__fastcall*)(void* cookie);
inline constexpr std::uint32_t kCallbackContractV1 = 1;
struct Nonce { std::uint64_t low; std::uint64_t high; };

enum class State : std::uint32_t {
    Empty, Prepared, Armed, Running, Completed, CallbackRejected,
    CallbackAborted, Expired, Disarmed, Cleaned
};
enum class Error : std::uint32_t {
    Ok, InvalidArgument, BadOriginalBytes, BadTargetRegion, TargetBytesMismatch,
    Busy, WrongState, NonceReused, NonceMismatch, NonceCapacity,
    AllocationFailed, ProtectFailed, FlushFailed, DeadlineExpired,
    MissingQuiescenceProof, EntryNotRestored, ReleaseFailed
};
enum class Rejection : std::uint32_t {
    None, NotArmed, Owner, Account, ThreadId, ThreadBirth, ThreadQueryFailed, Deadline
};
struct PrepareRequest {
    std::uint32_t size;
    std::uint32_t abiVersion;
    Getter originalGetter;
    std::uint8_t originalBytes[kGetterSize];
    std::uint8_t reserved[2];
    void* expectedSelf;
    std::uint64_t expectedAccount;
    std::uint32_t expectedThreadId;
    std::uint32_t callbackContract;
    std::uint64_t expectedThreadBirth; // FILETIME creation time, 100 ns since 1601.
    std::uint64_t deadlineTick;        // Exclusive upper bound, GetTickCount64 ms.
    Nonce nonce;                      // Nonzero and never reused within this module.
    Callback callback;
    void* cookie;
};
struct PrepareResult {
    std::uint32_t size;
    std::uint32_t abiVersion;
    std::uint8_t detourBytes[kGetterSize]; // FF 25 00 00 00 00 + hook VA (LE).
    std::uint8_t reserved[2];
    Getter trampoline; // RX, private allocation; never log its address.
};
struct Status {
    std::uint32_t size;
    std::uint32_t abiVersion;
    State state;
    Rejection lastRejection;
    std::uint64_t inFlight;
    std::uint64_t hookCalls;
    std::uint64_t ownerRejects;
    std::uint64_t accountRejects;
    std::uint64_t threadIdRejects;
    std::uint64_t threadBirthRejects;
    std::uint64_t threadQueryRejects;
    std::uint64_t notArmedCalls;
    std::uint64_t deadlineRejects;
    std::uint64_t callbackAttempts;
    std::uint64_t callbackCompleted;
    std::uint64_t callbackRejected;
    std::uint64_t callbackAborted;
    std::uint32_t trampolineAllocated;
    std::uint32_t reserved;
};
inline constexpr std::uint32_t kEntryRestored = 1;
inline constexpr std::uint32_t kAllHookExecutionsQuiesced = 2;
inline constexpr std::uint32_t kCleanupProof = kEntryRestored | kAllHookExecutionsQuiesced;
static_assert(sizeof(void*) == 8);
static_assert(sizeof(PrepareRequest) == 104);
static_assert(sizeof(PrepareResult) == 32);
static_assert(sizeof(Status) == 128);
} // namespace osd

// One active request per linked module. No functions write the original entry.
// Prepare and Cleanup require external exclusion of ALL possible hook executions,
// including parked entrants / return addresses not represented by inFlight.
extern "C" {
OSD_API osd::Error OsdPrepare(const osd::PrepareRequest* request, osd::PrepareResult* result);
OSD_API osd::Error OsdArm(osd::Nonce nonce);
// Serializes with callback admission; returns only after any admitted callback ends.
// Does not drain ordinary hook calls. Never call from callback / loader lock.
OSD_API osd::Error OsdDisarm(osd::Nonce nonce);
// Atomic, individually sampled fields: telemetry only, NOT a quiescence proof.
OSD_API osd::Error OsdGetStatus(osd::Status* status);
// Explicit caller attestation + restored-byte check + nonblocking inFlight/gate check.
// Keep code/module/cookie/self alive until this succeeds and all hook frames drain.
OSD_API osd::Error OsdCleanupAfterRestore(osd::Nonce nonce, std::uint32_t proof);
}

