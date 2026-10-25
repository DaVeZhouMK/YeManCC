#pragma once
#include "json.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace ymcc::steamrecord {
using Json = nlohmann::json;
using Read = std::function<bool(std::uint64_t, void*, std::size_t)>;
using Stable = std::function<bool()>;
enum class ProfileId { Legacy1791918634, Public1792618285 };
inline constexpr char kClientSha256[] = "e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e";
inline constexpr char kProfileVersion[] = "steamclient64-e9045710-cuser-record-v1";
inline const char* profileSha256(ProfileId id) {
    switch (id) {
    case ProfileId::Legacy1791918634: return kClientSha256;
    case ProfileId::Public1792618285: return "253e0a11a4415b38c78a42f7bdd6ecc426455f06c1ba6453a48090df3142dbbc";
    }
    return nullptr;
}
inline const char* profileVersion(ProfileId id) {
    switch (id) {
    case ProfileId::Legacy1791918634: return kProfileVersion;
    case ProfileId::Public1792618285: return "steamclient64-253e0a11-cuser-record-v1";
    }
    return nullptr;
}
inline std::optional<ProfileId> profileIdForSha256(std::string_view sha256) {
    // Exact full digest; no prefix, build-number or unknown-enum fallback.
    for (const auto id : {ProfileId::Legacy1791918634, ProfileId::Public1792618285})
        if (sha256 == profileSha256(id)) return id;
    return std::nullopt;
}
struct Query {
    std::uint64_t moduleBase = 0, moduleSize = 0, account64 = 0, gameId64 = 0;
    std::uint32_t expectedPid = 0;
    bool profileVerified = false;
    ProfileId profile = ProfileId::Legacy1791918634;
};
namespace detail {
inline constexpr std::uint64_t kMinAddress = 0x10000, kMaxAddress = 0x7fffffffffff;
inline constexpr std::size_t kMaxReadCalls = 2047, kMaxReadBytes = 65536, kMaxSingleRead = 64;
struct Rejected { const char* reason; };
// Independently qualified against each fixed PE. Equal fields are intentional;
// code/RIP/RTTI evidence and full function ranges are in client-profile evidence.
struct ProfileLayout {
    std::uint64_t getterRva, engineRoot, engineVtable, engineInterfaceVtable, userVtable, userInterfaceVtable;
    std::array<unsigned char, 14> getterBytes;
    std::uint64_t engineInterfaceOffset, userVectorOffset, userCountOffset, userStride, userEntryOffset;
    std::uint64_t userInterfaceOffset, accountOffset, recordsOffset, recordCountOffset, recordStride, gameIdOffset, pidPairOffset;
    std::array<std::uint64_t, 7> predicateRvas;
    std::array<std::array<unsigned char, 7>, 7> predicateBytes;
    std::array<std::size_t, 7> predicateSizes;
    std::uint64_t predicateFlagsOffset, predicateCounterOffset, predicateAlternateOffset;
    const char* predicateVersion;
};
inline const ProfileLayout& profileLayout(ProfileId id) {
    static constexpr ProfileLayout legacy {
        0xcfdc0, 0x181d008, 0x1361f38, 0x1361ff0, 0x136fa80, 0x136fbd0,
        {0x48,0x8b,0x81,0x9a,0xe3,0xff,0xff,0x48,0x89,0x02,0x48,0x8b,0xc2,0xc3},
        0xfe0, 0x1080, 0x1090, 0x10, 8, 0x1e70, 0x20a, 0x2568, 0x2578, 0x330, 8, 0x198,
        {0x9d7a9c,0x9d7bc9,0x9d7b2a,0x9d7a5f,0x9d7aef,0x9d7bb6,0x9d7bd9},
        {{{0x0f,0xb6,0x8b,0x01,0x02,0x00,0x00}, {0xff,0x83,0xa8,0x01,0x00,0x00},
          {0x8b,0x8b,0x68,0x02,0x00,0x00}, {0x88,0x83,0x00,0x02,0x00,0x00},
          {0x88,0x83,0x00,0x02,0x00,0x00}, {0x88,0x83,0x00,0x02,0x00,0x00},
          {0x88,0x83,0x00,0x02,0x00,0x00}}}, {7,6,6,6,6,6,6},
        0x200, 0x1a8, 0x268, "steamclient64-e9045710-predicate-inputs-v1"
    };
    static constexpr ProfileLayout publicBuild {
        0xcfdc0, 0x181e008, 0x1362478, 0x1362530, 0x1370030, 0x1370180,
        {0x48,0x8b,0x81,0x9a,0xe3,0xff,0xff,0x48,0x89,0x02,0x48,0x8b,0xc2,0xc3},
        0xfe0, 0x1080, 0x1090, 0x10, 8, 0x1e70, 0x20a, 0x2568, 0x2578, 0x330, 8, 0x198,
        {0x9d7b7c,0x9d7ca9,0x9d7c0a,0x9d7b3f,0x9d7bcf,0x9d7c96,0x9d7cb9},
        {{{0x0f,0xb6,0x8b,0x01,0x02,0x00,0x00}, {0xff,0x83,0xa8,0x01,0x00,0x00},
          {0x8b,0x8b,0x68,0x02,0x00,0x00}, {0x88,0x83,0x00,0x02,0x00,0x00},
          {0x88,0x83,0x00,0x02,0x00,0x00}, {0x88,0x83,0x00,0x02,0x00,0x00},
          {0x88,0x83,0x00,0x02,0x00,0x00}}}, {7,6,6,6,6,6,6},
        0x200, 0x1a8, 0x268, "steamclient64-253e0a11-predicate-inputs-v1"
    };
    switch (id) {
    case ProfileId::Legacy1791918634: return legacy;
    case ProfileId::Public1792618285: return publicBuild;
    }
    throw Rejected{"profile-unknown"};
}

inline void require(bool value, const char* reason) {
    if (!value) throw Rejected{reason};
}
inline bool inRange(std::uint64_t address, std::uint64_t bytes) {
    return address >= kMinAddress && address <= kMaxAddress && bytes != 0 &&
        bytes - 1 <= kMaxAddress - address;
}
inline std::uint64_t offset(std::uint64_t base, std::uint64_t displacement) {
    require(inRange(base, 1) && displacement <= kMaxAddress - base, "address-out-of-range");
    return base + displacement;
}
inline std::uint64_t moduleAnchor(const Query& q, std::uint64_t displacement, std::uint64_t bytes) {
    require(displacement <= q.moduleSize && bytes <= q.moduleSize - displacement,
        "module-anchor-out-of-range");
    const auto address = offset(q.moduleBase, displacement);
    require(inRange(address, bytes), "address-out-of-range");
    return address;
}

// Counts attempted callback reads as well as successful ones. The callback must
// provide an exact read on true. No native pointer is dereferenced by this core.
class Reader {
    const Read& read_;
public:
    std::size_t calls = 0, bytes = 0;
    explicit Reader(const Read& read) : read_(read) {}
    void read(std::uint64_t address, void* destination, std::size_t size) {
        require(size != 0 && size <= kMaxSingleRead, "read-size-invalid");
        require(inRange(address, size), "address-out-of-range");
        require(calls < kMaxReadCalls && size <= kMaxReadBytes - bytes, "read-budget-exceeded");
        ++calls;
        bytes += size;
        bool ok = false;
        try { ok = read_(address, destination, size); }
        catch (...) { throw Rejected{"memory-read-exception"}; }
        require(ok, "memory-read-failed");
    }
    // Steam's verified layout is little endian; decode explicitly on every host.
    std::uint64_t u64(std::uint64_t address) {
        std::array<unsigned char, 8> data{};
        read(address, data.data(), data.size());
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < data.size(); ++i) value |= std::uint64_t(data[i]) << (8 * i);
        return value;
    }
    std::uint32_t count(std::uint64_t address, std::uint32_t maximum, const char* reason) {
        std::array<unsigned char, 4> data{};
        read(address, data.data(), data.size());
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < data.size(); ++i) value |= std::uint32_t(data[i]) << (8 * i);
        // This also rejects every negative int32 representation without a
        // host-dependent unsigned-to-signed conversion.
        require(value <= maximum, reason);
        return value;
    }
};
inline void vectorRange(std::uint64_t base, std::uint32_t count, std::uint64_t stride) {
    require(count == 0 ? (base == 0 || inRange(base, 1)) : inRange(base, stride * count),
        "address-out-of-range");
}
inline void checkGetter(Reader& r, const Query& q) {
    const auto& p = profileLayout(q.profile);
    const auto& expected = p.getterBytes;
    std::array<unsigned char, 14> loaded{};
    r.read(moduleAnchor(q, p.getterRva, expected.size()), loaded.data(), loaded.size());
    require(loaded == expected, "getter-mismatch");
}
struct Selection {
    std::uint64_t engine = 0, vector = 0, user = 0;
    std::uint32_t count = 0, selectedIndex = 0;
    std::vector<std::uint64_t> entries;
    bool operator==(const Selection& other) const {
        return engine == other.engine && vector == other.vector && user == other.user &&
            count == other.count && selectedIndex == other.selectedIndex && entries == other.entries;
    }
};
inline void checkSelection(Reader& r, const Query& q, const Selection& s) {
    const auto& p = profileLayout(q.profile);
    require(r.u64(moduleAnchor(q, p.engineRoot, 8)) == s.engine, "user-selection-changed");
    require(r.u64(offset(s.engine, p.userVectorOffset)) == s.vector &&
        r.count(offset(s.engine, p.userCountOffset), 32, "user-vector-count-invalid") == s.count,
        "user-selection-changed");
    require(r.u64(offset(s.vector, p.userStride * s.selectedIndex + p.userEntryOffset)) == s.user,
        "user-selection-changed");
    require(r.u64(offset(s.user, p.accountOffset)) == q.account64, "account-changed");
}
inline Selection selectUser(Reader& r, const Query& q) {
    const auto& p = profileLayout(q.profile);
    Selection s;
    s.engine = r.u64(moduleAnchor(q, p.engineRoot, 8));
    require(r.u64(s.engine) == moduleAnchor(q, p.engineVtable, 8) &&
        r.u64(offset(s.engine, p.engineInterfaceOffset)) == moduleAnchor(q, p.engineInterfaceVtable, 8), "engine-type-mismatch");
    s.vector = r.u64(offset(s.engine, p.userVectorOffset));
    s.count = r.count(offset(s.engine, p.userCountOffset), 32, "user-vector-count-invalid");
    vectorRange(s.vector, s.count, p.userStride);
    unsigned matches = 0;
    for (std::uint32_t i = 0; i < s.count; ++i) {
        const auto user = r.u64(offset(s.vector, p.userStride * i + p.userEntryOffset));
        require(r.u64(user) == moduleAnchor(q, p.userVtable, 8) &&
            r.u64(offset(user, p.userInterfaceOffset)) == moduleAnchor(q, p.userInterfaceVtable, 8), "user-type-mismatch");
        s.entries.push_back(user);
        // Other accounts are compared transiently. Their record headers and
        // records are never read, nor are their account values exported.
        if (r.u64(offset(user, p.accountOffset)) == q.account64) {
            ++matches;
            s.selectedIndex = i;
            s.user = user;
        }
    }
    require(r.u64(offset(s.engine, p.userVectorOffset)) == s.vector &&
        r.count(offset(s.engine, p.userCountOffset), 32, "user-vector-count-invalid") == s.count,
        "user-selection-changed");
    require(matches != 0, "account-not-found");
    require(matches == 1, "account-ambiguous");
    checkSelection(r, q, s);
    return s;
}
struct Header {
    std::uint64_t records = 0;
    std::uint32_t count = 0;
    bool operator==(const Header& other) const { return records == other.records && count == other.count; }
};
inline Header readHeader(Reader& r, std::uint64_t user, ProfileId profile = ProfileId::Legacy1791918634) {
    const auto& p = profileLayout(profile);
    Header h;
    h.records = r.u64(offset(user, p.recordsOffset));
    h.count = r.count(offset(user, p.recordCountOffset), 256, "record-count-invalid");
    vectorRange(h.records, h.count, p.recordStride);
    return h;
}
struct Target {
    std::uint32_t index = 0, primary = 0, foreground = 0;
    bool operator==(const Target& other) const {
        return index == other.index && primary == other.primary && foreground == other.foreground;
    }
};
inline std::vector<Target> readTargets(Reader& r, const Query& q, const Header& h) {
    const auto& p = profileLayout(q.profile);
    std::vector<Target> targets;
    for (std::uint32_t i = 0; i < h.count; ++i) {
        const auto record = offset(h.records, p.recordStride * i);
        if (r.u64(offset(record, p.gameIdOffset)) != q.gameId64) continue;
        // +198/+19c form ONE 8-byte read, bracketed by full GameID reads.
        const auto pair = r.u64(offset(record, p.pidPairOffset));
        require(r.u64(offset(record, p.gameIdOffset)) == q.gameId64, "target-gameid-changed");
        targets.push_back(Target{i, static_cast<std::uint32_t>(pair >> 32),
            static_cast<std::uint32_t>(pair & 0xffffffffULL)});
    }
    return targets;
}
// These are saved predicate inputs, not a current liveness call or Running verdict.
inline Json predicateInputsUnknown(const char* reason, ProfileId profile = ProfileId::Legacy1791918634) {
    return {{"source", "cuser-record-liveness-inputs"},
        {"profileVersion", profileVersion(profile) ? profileLayout(profile).predicateVersion : "unknown"},
        {"status", "unavailable"}, {"available", false}, {"reason", reason},
        {"lastRecordKeepDecisionRaw", nullptr}, {"comparisonLatchRaw", nullptr},
        {"failedCheckCounterRaw", nullptr}, {"alternatePidRaw", nullptr},
        {"actualPredicateBranch", "unknown"}, {"actualPredicateReturn", nullptr},
        {"importedLivenessReturn", "not-collected"}, {"namedEventObserved", false},
        {"backendRunningVerified", false}, {"recoveryAdmission", false},
        {"atomicSnapshot", false}, {"abaExcluded", false},
        {"accountValuesPersisted", false}, {"nativePointersPersisted", false},
        {"targetCalls", 0}, {"memoryWrites", 0}};
}
struct PredicateInputsRaw {
    unsigned char lastKeep = 0, latch = 0;
    std::uint32_t counter = 0, alternate = 0;
    bool operator==(const PredicateInputsRaw& other) const {
        return lastKeep == other.lastKeep && latch == other.latch &&
            counter == other.counter && alternate == other.alternate;
    }
};
inline void checkPredicateAnchor(Reader& r, const Query& q, std::size_t index) {
    const auto& p = profileLayout(q.profile);
    require(index < p.predicateRvas.size(), "predicate-profile-anchor-invalid");
    std::array<unsigned char, 7> loaded{};
    const auto size = p.predicateSizes[index];
    require(size != 0 && size <= loaded.size(), "read-size-invalid");
    r.read(moduleAnchor(q, p.predicateRvas[index], size), loaded.data(), size);
    for (std::size_t i = 0; i < size; ++i)
        require(loaded[i] == p.predicateBytes[index][i], "predicate-profile-anchor-mismatch");
}
inline PredicateInputsRaw readPredicateInputsRaw(Reader& r, std::uint64_t record, ProfileId profile = ProfileId::Legacy1791918634) {
    const auto& p = profileLayout(profile);
    std::array<unsigned char, 2> flags{};
    r.read(offset(record, p.predicateFlagsOffset), flags.data(), flags.size());
    require(flags[0] <= 1, "predicate-keep-decision-nonboolean");
    require(flags[1] <= 1, "predicate-comparison-latch-nonboolean");
    // Unlike vector counts, failure counters are unrestricted uint32 values.
    return {flags[0], flags[1], r.count(offset(record, p.predicateCounterOffset), UINT32_MAX, "predicate-counter-invalid"),
        r.count(offset(record, p.predicateAlternateOffset), UINT32_MAX, "predicate-alternate-pid-invalid")};
}
inline Json observePredicateInputs(Reader& r, const Query& q, std::uint64_t record,
    std::size_t tailCalls, std::size_t tailBytes) {
    auto out = predicateInputsUnknown("predicate-inputs-not-collected", q.profile);
    const auto& p = profileLayout(q.profile);
    try {
        // Reserve the complete old identity/header/target revalidation tail first.
        // This optional extension never gets its own memory budget.
        constexpr std::size_t calls = 16, bytes = 96;
        require(tailCalls <= kMaxReadCalls - r.calls && calls <= kMaxReadCalls - r.calls - tailCalls &&
            tailBytes <= kMaxReadBytes - r.bytes && bytes <= kMaxReadBytes - r.bytes - tailBytes,
            "predicate-inputs-read-budget-unavailable");
        for (std::size_t i = 0; i < p.predicateRvas.size(); ++i) checkPredicateAnchor(r, q, i);
        require(r.u64(offset(record, p.gameIdOffset)) == q.gameId64, "predicate-inputs-fullid-changed");
        const auto before = readPredicateInputsRaw(r, record, q.profile);
        require(r.u64(offset(record, p.gameIdOffset)) == q.gameId64, "predicate-inputs-fullid-changed");
        const auto after = readPredicateInputsRaw(r, record, q.profile);
        require(before == after, "predicate-inputs-changed");
        require(r.u64(offset(record, p.gameIdOffset)) == q.gameId64, "predicate-inputs-fullid-changed");
        out["status"] = "observed";
        out["available"] = true;
        out["reason"] = "predicate-inputs-double-read-equal";
        out["lastRecordKeepDecisionRaw"] = before.lastKeep;
        out["comparisonLatchRaw"] = before.latch;
        out["failedCheckCounterRaw"] = before.counter;
        out["alternatePidRaw"] = before.alternate;
        out["alternatePidMatchesOriginal"] = before.alternate == q.expectedPid;
    } catch (const Rejected& e) { out["reason"] = e.reason; }
    return out;
}
inline bool stable(const Stable& check) {
    try { return check(); }
    catch (...) { throw Rejected{"identity-check-failed"}; }
}
} // namespace detail

// Diagnostic evidence only. Even equal before/after reads cannot exclude ABA,
// concurrent changes between fields, PID reuse, or an unknown login generation.
// The adapter owns hash/process/account provenance and supplies Stable checks.
inline Json observe(const Query& query, const Read& read, const Stable& stable) {
    using namespace detail;
    // Copy inputs so callbacks cannot silently redirect this observation.
    const Query q = query;
    Json result = {
        {"source", "steam-native-cuser-record-diagnostic"}, {"profileVersion", profileVersion(q.profile) ? profileVersion(q.profile) : "unknown"},
        {"authority", "diagnostic-only-no-admission"}, {"status", "unavailable"}, {"available", false},
        {"reason", "identity-missing"}, {"recordState", "unknown"},
        {"matchingTargetRecordCount", nullptr}, {"originalPrimaryMatched", nullptr},
        {"readCalls", 0}, {"readBytes", 0}, {"atomicSnapshot", false}, {"abaExcluded", false},
        {"loginGenerationLease", "unknown"}, {"recordAbsenceIsNotAtomicProof", true},
        {"inputVerified", false}, {"continueVerified", false}, {"accountValuesPersisted", false},
        {"otherAccountsRecordsRead", false},
        {"predicateInputs", predicateInputsUnknown("bound-record-unavailable", q.profile)}
    };
    Reader r(read);
    bool identityStarted = false;
    std::vector<Target> targets;
    auto predicateInputs = predicateInputsUnknown("unique-target-not-bound", q.profile);
    const char* failure = nullptr;
    try {
        const auto& p = profileLayout(q.profile);
        require(q.profileVerified, "profile-unverified");
        require(q.account64 != 0 && q.gameId64 != 0 && q.expectedPid != 0, "identity-missing");
        require(static_cast<bool>(read), "reader-unavailable");
        require(static_cast<bool>(stable), "stability-check-unavailable");
        require(inRange(q.moduleBase, q.moduleSize), "module-range-invalid");
        // Validate all fixed module anchors before making any memory read.
        moduleAnchor(q, p.getterRva, p.getterBytes.size());
        for (const auto anchor : {p.engineRoot, p.engineVtable, p.engineInterfaceVtable, p.userVtable, p.userInterfaceVtable})
            moduleAnchor(q, anchor, 8);
        require(detail::stable(stable), "identity-changed");
        identityStarted = true;
        checkGetter(r, q);
        const auto selection = selectUser(r, q);
        const auto header = readHeader(r, selection.user, q.profile);
        targets = readTargets(r, q, header);
        if (targets.size() == 1) {
            // At most 140 reads to reselect 32 users, three per record in
            // the second target pass, and 22 remaining legacy reads.
            const auto tailCalls = std::size_t(162) + 3 * header.count;
            const auto tailBytes = 8 * tailCalls + 6; // 14-byte getter
            predicateInputs = observePredicateInputs(r, q,
                offset(header.records, p.recordStride * targets.front().index), tailCalls, tailBytes);
        }
        checkSelection(r, q, selection);
        require(readHeader(r, selection.user, q.profile) == header, "record-header-changed");
        const auto again = readTargets(r, q, header);
        require(targets == again, "target-records-changed");
        require(readHeader(r, selection.user, q.profile) == header, "record-header-changed");
        require(selectUser(r, q) == selection, "user-selection-changed");
        checkGetter(r, q);
        checkSelection(r, q, selection);
        require(readHeader(r, selection.user, q.profile) == header, "record-header-changed");
        checkSelection(r, q, selection);
    } catch (const Rejected& e) { failure = e.reason; }
    // Also revoke partial evidence on a failed read if the external identity
    // changed. Exception messages from either callback never enter JSON.
    if (identityStarted) {
        try { require(detail::stable(stable), "identity-changed"); }
        catch (const Rejected& e) { failure = e.reason; }
    }
    result["readCalls"] = r.calls;
    result["readBytes"] = r.bytes;
    if (failure) {
        result["reason"] = failure;
        return result;
    }
    if (targets.size() > 1) {
        result["reason"] = "target-record-ambiguous";
        result["matchingTargetRecordCount"] = targets.size();
        return result;
    }
    result["status"] = "observed";
    result["available"] = true;
    result["matchingTargetRecordCount"] = targets.size();
    result["predicateInputs"] = std::move(predicateInputs);
    if (targets.empty()) {
        result["reason"] = "target-record-not-observed";
        result["recordState"] = "not-observed";
    } else {
        const auto& target = targets.front();
        const bool matched = target.primary == q.expectedPid;
        result["reason"] = "target-record-observed";
        result["recordState"] = matched ? "present-original-primary" : "different-primary";
        result["primaryLivenessPid"] = target.primary;
        result["foregroundComparisonPid"] = target.foreground;
        result["originalPrimaryMatched"] = matched;
    }
    return result;
}
} // namespace ymcc::steamrecord
