#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "json.hpp"

// Pure schema/decisions only: no OS, persistence, hardware or logging calls.
namespace ymcc::msi::recovery {
inline constexpr std::uint32_t schemaVersion = 1;
inline constexpr std::size_t maxJournalBytes = 64 * 1024;
inline constexpr std::size_t maxHiddenPaths = 32;
inline constexpr std::size_t maxPathBytes = 512;
inline constexpr std::size_t maxLocationPaths = 16;
inline constexpr std::size_t maxLocationPathBytes = 1024;
inline constexpr std::size_t maxBoardKeyBytes = 1024;
inline constexpr std::size_t maxTransactionIdBytes = 128;

struct Record {
    std::uint32_t schema = schemaVersion;
    // Exact manufacturer|product|systemProduct|deviceClass; never normalized.
    std::string boardKey;
    std::uint32_t ownerPid = 0;
    std::string ownerCreationTime;
    std::string transactionId;
    bool modeReturnPending = false;
    // Last durably consumed mode1 attempt: pid:decimal64 creation time, or empty.
    std::string modeAttemptOwner;
    std::string rootInstance;
    // Durable real device-tree locations. Empty legacy evidence permits exact roots only.
    std::vector<std::string> locationPaths;
    // Exact spellings of additions owned by this transaction. The caller must
    // prove they existed and were NOT hidden beforehand, including HID parent
    // ancestry. A whitelist or an existing hidden list is not ownership proof.
    std::vector<std::string> hiddenPaths;
};

namespace detail {
inline char foldAscii(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
inline bool equalAscii(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (foldAscii(a[i]) != foldAscii(b[i])) return false;
    return true;
}
inline bool startsAscii(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && equalAscii(text.substr(0, prefix.size()), prefix);
}
inline bool decimal(std::string_view text, std::uint64_t& value) noexcept {
    if (text.empty() || text.size() > 20) return false;
    std::uint64_t result = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (result > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
            return false;
        result = result * 10 + digit;
    }
    if (result == 0) return false;
    value = result;
    return true;
}
// Bounded UTF-8, with controls rejected; printable identity bytes stay exact.
inline bool text(std::string_view s, std::size_t limit) noexcept {
    if (s.size() > limit) return false;
    for (std::size_t i = 0; i < s.size();) {
        const auto lead = static_cast<unsigned char>(s[i++]);
        if (lead < 0x80) {
            if (lead < 0x20 || lead == 0x7f) return false;
            continue;
        }
        unsigned extra = 0;
        std::uint32_t code = 0, minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { extra = 1; code = lead & 0x1f; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { extra = 2; code = lead & 0x0f; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { extra = 3; code = lead & 0x07; minimum = 0x10000; }
        else return false;
        if (s.size() - i < extra) return false;
        for (unsigned n = 0; n < extra; ++n) {
            const auto byte = static_cast<unsigned char>(s[i++]);
            if ((byte & 0xc0) != 0x80) return false;
            code = (code << 6) | (byte & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    return true;
}
inline bool instanceTail(std::string_view tail) noexcept {
    if (tail.empty()) return false;
    // Deliberately narrow PnP component grammar: no quotes, whitespace, path
    // separators, shell syntax, wildcard, GUID/interface path or traversal.
    for (char c : tail) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '&' || c == '_' || c == '-'))
            return false;
    }
    return true;
}
struct RootParts { unsigned pid = 0; std::string_view tail; };
inline bool root(std::string_view path, RootParts& out) noexcept {
    if (path.size() > maxPathBytes) return false;
    constexpr std::string_view prefix = "USB\\VID_0DB0&PID_190";
    if (!startsAscii(path, prefix) || path.size() <= prefix.size() + 2) return false;
    const char mode = path[prefix.size()];
    if ((mode != '1' && mode != '2') || path[prefix.size() + 1] != '\\') return false;
    const auto tail = path.substr(prefix.size() + 2);
    if (!instanceTail(tail)) return false;
    out = {mode == '1' ? 0x1901u : 0x1902u, tail};
    return true;
}
struct Owner { std::uint32_t pid = 0; std::uint64_t creation = 0; };
inline bool owner(std::string_view textValue, Owner& out) noexcept {
    const auto colon = textValue.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon > 10) return false;
    std::uint64_t pid = 0, creation = 0;
    if (!decimal(textValue.substr(0, colon), pid) ||
        pid > (std::numeric_limits<std::uint32_t>::max)() ||
        !decimal(textValue.substr(colon + 1), creation)) return false;
    out = {static_cast<std::uint32_t>(pid), creation};
    return true;
}
} // namespace detail

inline bool validBoardKey(std::string_view key) noexcept {
    if (!detail::text(key, maxBoardKeyBytes)) return false;
    std::size_t offset = 0;
    for (unsigned field = 0; field < 3; ++field) {
        const auto pipe = key.find('|', offset);
        if (pipe == std::string_view::npos || pipe == offset) return false;
        offset = pipe + 1;
    }
    return offset < key.size() && key.find('|', offset) == std::string_view::npos;
}
inline bool validRootInstance(std::string_view path) noexcept {
    detail::RootParts parsed;
    return detail::root(path, parsed);
}
inline bool samePhysicalRoot(std::string_view a, std::string_view b) noexcept {
    detail::RootParts left, right;
    // No PID replacement or tail-only equivalence: exact full root identity.
    return detail::root(a, left) && detail::root(b, right) && detail::equalAscii(a, b);
}
inline bool validLocationPath(std::string_view path) noexcept {
    if (path.empty() || path.size() > maxLocationPathBytes) return false;
    // DEVPKEY_Device_LocationPaths examples: PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4).
    // A safe spelling is not evidence; only real device-tree observations count.
    for (char c : path) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '#' ||
              c == '(' || c == ')' || c == '\\')) return false;
    }
    return true;
}
namespace detail {
inline bool locations(const std::vector<std::string>& paths) noexcept {
    if (paths.size() > maxLocationPaths) return false;
    for (const auto& path : paths) if (!validLocationPath(path)) return false;
    return true;
}
} // namespace detail
inline bool sameDeviceRoot(const Record& record, std::string_view currentRoot,
                           const std::vector<std::string>& currentLocations) noexcept {
    if (!validRootInstance(record.rootInstance) || !validRootInstance(currentRoot) ||
        !detail::locations(record.locationPaths) || !detail::locations(currentLocations)) return false;
    if (samePhysicalRoot(record.rootInstance, currentRoot)) return true;
    // Full, byte-exact location entries; no prefix, VID or PID/tail heuristic.
    for (const auto& saved : record.locationPaths)
        for (const auto& current : currentLocations) if (saved == current) return true;
    return false;
}
inline bool validHiddenPath(std::string_view path) noexcept {
    if (validRootInstance(path)) return true;
    if (path.size() > maxPathBytes) return false;
    constexpr std::string_view prefix = "HID\\VID_0DB0&PID_190";
    if (!detail::startsAscii(path, prefix) || path.size() <= prefix.size()) return false;
    const char mode = path[prefix.size()];
    if (mode != '1' && mode != '2') return false;
    auto rest = path.substr(prefix.size() + 1);
    // MI01 COL03 is vendor-control. MI01 COL01/02 remain forbidden.
    for (const auto collection : {std::string_view("&MI_00&COL01\\"),
                                  std::string_view("&MI_00&COL02\\"),
                                  std::string_view("&MI_01&COL03\\")}) {
        if (detail::startsAscii(rest, collection))
            return detail::instanceTail(rest.substr(collection.size()));
    }
    return false;
}
inline bool validOwner(std::string_view owner) noexcept {
    detail::Owner parsed;
    return detail::owner(owner, parsed);
}
inline bool validateRecord(const Record& r) noexcept {
    std::uint64_t creation = 0;
    if (r.schema != schemaVersion || !validBoardKey(r.boardKey) || r.ownerPid == 0 ||
        !detail::decimal(r.ownerCreationTime, creation) ||
        !detail::text(r.transactionId, maxTransactionIdBytes) ||
        (!r.modeAttemptOwner.empty() && !validOwner(r.modeAttemptOwner)) ||
        !validRootInstance(r.rootInstance) || !detail::locations(r.locationPaths) ||
        r.hiddenPaths.size() > maxHiddenPaths)
        return false;
    for (std::size_t i = 0; i < r.hiddenPaths.size(); ++i) {
        const auto& path = r.hiddenPaths[i];
        if (!validHiddenPath(path)) return false;
        // The schema preserves historical roots across modes/generations.
        // Live root/location and HID ancestry proofs belong to the caller.
        for (std::size_t j = 0; j < i; ++j)
            if (detail::equalAscii(path, r.hiddenPaths[j])) return false;
    }
    return true;
}

namespace detail {
// SAX rejects at the first invalid token, before building an unbounded DOM.
// Decoded keys are checked before assignment, including escaped duplicates.
class RecordSax final : public nlohmann::json_sax<nlohmann::json> {
    enum Field : std::size_t { Schema, BoardKey, OwnerPid, Creation, Transaction,
                             Pending, AttemptOwner, Root, Paths, Locations, Count };
    std::array<bool, Count> seen_{};
    Field field_ = Schema;
    bool started_ = false, awaiting_ = false, paths_ = false, locations_ = false, completed_ = false;
    bool value(Field f) const noexcept { return started_ && !completed_ && !paths_ && !locations_ && awaiting_ && field_ == f; }
public:
    Record record;
    bool completed() const noexcept { return completed_; }
    bool null() override { return false; }
    bool boolean(bool v) override {
        if (!value(Pending)) return false;
        record.modeReturnPending = v; awaiting_ = false; return true;
    }
    bool number_integer(number_integer_t) override { return false; }
    bool number_unsigned(number_unsigned_t v) override {
        if (value(Schema) && v == schemaVersion) record.schema = schemaVersion;
        else if (value(OwnerPid) && v > 0 && v <= (std::numeric_limits<std::uint32_t>::max)())
            record.ownerPid = static_cast<std::uint32_t>(v);
        else return false;
        awaiting_ = false; return true;
    }
    bool number_float(number_float_t, const string_t&) override { return false; }
    bool string(string_t& v) override {
        if (locations_) {
            if (record.locationPaths.size() >= maxLocationPaths || !validLocationPath(v)) return false;
            record.locationPaths.push_back(std::move(v)); return true;
        }
        if (paths_) {
            if (record.hiddenPaths.size() >= maxHiddenPaths || !validHiddenPath(v)) return false;
            for (const auto& prior : record.hiddenPaths) if (equalAscii(prior, v)) return false;
            record.hiddenPaths.push_back(std::move(v)); return true;
        }
        if (!awaiting_ || completed_) return false;
        std::uint64_t creation = 0;
        switch (field_) {
        case BoardKey: if (!validBoardKey(v)) return false; record.boardKey = std::move(v); break;
        case Creation: if (!decimal(v, creation)) return false; record.ownerCreationTime = std::move(v); break;
        case Transaction: if (!text(v, maxTransactionIdBytes)) return false; record.transactionId = std::move(v); break;
        case AttemptOwner: if (!v.empty() && !validOwner(v)) return false; record.modeAttemptOwner = std::move(v); break;
        case Root: if (!validRootInstance(v)) return false; record.rootInstance = std::move(v); break;
        default: return false;
        }
        awaiting_ = false; return true;
    }
    bool binary(binary_t&) override { return false; }
    bool start_object(std::size_t) override {
        if (started_) return false;
        started_ = true; return true;
    }
    bool key(string_t& v) override {
        if (!started_ || completed_ || paths_ || locations_ || awaiting_) return false;
        constexpr std::array<std::string_view, Count> names{{"schema", "boardKey", "ownerPid",
            "ownerCreationTime", "transactionId", "modeReturnPending", "modeAttemptOwner",
            "rootInstance", "hiddenPaths", "locationPaths"}};
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (v != names[i]) continue;
            if (seen_[i]) return false;
            seen_[i] = true; field_ = static_cast<Field>(i); awaiting_ = true; return true;
        }
        return false;
    }
    bool end_object() override {
        if (!started_ || completed_ || awaiting_ || paths_ || locations_) return false;
        for (bool seen : seen_) if (!seen) return false;
        completed_ = validateRecord(record); return completed_;
    }
    bool start_array(std::size_t) override {
        if (value(Paths)) paths_ = true;
        else if (value(Locations)) locations_ = true;
        else return false;
        return true;
    }
    bool end_array() override {
        if (!paths_ && !locations_) return false;
        paths_ = false; locations_ = false; awaiting_ = false; return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }
};
} // namespace detail

// Failure leaves the caller's output unchanged. Decode RAW bytes, never an
// already-parsed JSON object: a DOM has already discarded duplicate keys.
inline bool decode(std::string_view bytes, Record& out) {
    if (bytes.empty() || bytes.size() > maxJournalBytes ||
        bytes.find('\0') != std::string_view::npos ||
        (bytes.size() >= 3 && bytes.substr(0, 3) == "\xef\xbb\xbf")) return false;
    try {
        detail::RecordSax sax;
        if (!nlohmann::json::sax_parse(bytes.begin(), bytes.end(), &sax,
                nlohmann::json::input_format_t::json, true, false) || !sax.completed()) return false;
        out = std::move(sax.record); return true;
    } catch (const nlohmann::json::exception&) { return false; }
}
inline bool encode(const Record& r, std::string& out) {
    if (!validateRecord(r)) return false;
    try {
        nlohmann::json j = {{"schema", r.schema}, {"boardKey", r.boardKey}, {"ownerPid", r.ownerPid},
            {"ownerCreationTime", r.ownerCreationTime}, {"transactionId", r.transactionId},
            {"modeReturnPending", r.modeReturnPending}, {"modeAttemptOwner", r.modeAttemptOwner},
            {"rootInstance", r.rootInstance}, {"hiddenPaths", r.hiddenPaths}, {"locationPaths", r.locationPaths}};
        auto bytes = j.dump();
        if (bytes.size() > maxJournalBytes) return false;
        out = std::move(bytes); return true;
    } catch (const nlohmann::json::exception&) { return false; }
}

// Convenience integration API. Invalid bytes never expose a usable record.
enum class DecodeState { Invalid, Valid };
struct DecodeResult {
    DecodeState state = DecodeState::Invalid;
    Record record;
};
inline bool validRecord(const Record& r) noexcept { return validateRecord(r); }
inline bool validOwnedPath(std::string_view path) noexcept { return validHiddenPath(path); }
// Empty text is the failure sentinel; a valid encoding is always a JSON object.
inline std::string encodeRecord(const Record& r) {
    std::string bytes;
    if (!encode(r, bytes)) return {};
    return bytes;
}
inline DecodeResult decodeRecord(std::string_view bytes) {
    DecodeResult result;
    if (decode(bytes, result.record)) result.state = DecodeState::Valid;
    return result;
}
// Idempotent, atomic union. Preserve original spellings/order and ALL debts.
// Additions require the caller's pre-existing/not-already-hidden ownership proof.
inline bool mergeOwnedPaths(Record& r, const std::vector<std::string>& additions) {
    if (!validateRecord(r) || additions.size() > maxHiddenPaths) return false;
    Record candidate = r;
    for (const auto& path : additions) {
        if (!validHiddenPath(path)) return false;
        bool exists = false;
        for (const auto& prior : candidate.hiddenPaths)
            if (detail::equalAscii(path, prior)) { exists = true; break; }
        if (exists) continue;
        if (candidate.hiddenPaths.size() >= maxHiddenPaths) return false;
        candidate.hiddenPaths.push_back(path);
    }
    if (!validateRecord(candidate)) return false;
    r = std::move(candidate); return true;
}

// This is only an in-memory consume. Persist the whole candidate atomically and
// wait for DURABLE completion before mode1, then recheck live facts. Pending or
// failed persistence grants nothing. A failed/timeout write does not erase debt.
// No facts are checked here. The caller serializes recovery owners; a previous
// owner may be superseded only after proven dead, so it cannot return later.
// Numeric comparison prevents alternate leading-zero spellings bypassing a gate.
inline bool claimModeAttempt(Record& r, std::string_view newOwner) {
    detail::Owner next, previous;
    if (!validateRecord(r) || !r.modeReturnPending || !detail::owner(newOwner, next)) return false;
    if (!r.modeAttemptOwner.empty() && detail::owner(r.modeAttemptOwner, previous) &&
        next.pid == previous.pid && next.creation == previous.creation) return false;
    auto canonical = std::to_string(next.pid) + ":" + std::to_string(next.creation);
    r.modeAttemptOwner = std::move(canonical); return true;
}

struct Facts {
    // All defaults deny. boardMatch compares ALL four exact identity components.
    // oldOwnerDead requires positive PID + creation-time evidence for the
    // transaction owner and any previous attempt owner (unknown denies).
    // hostAbsent covers surviving/shared hosts. sourceFresh/currentRootMatches
    // come from a current, independently observed physical source and ancestry.
    bool knownBoard = false, boardMatch = false, oldOwnerDead = false;
    bool virtualDisabled = false, hostAbsent = false, currentRootMatches = false;
    bool sourceFresh = false, currentDinputObserved = false;
};
enum class RecoveryDecision {
    UnknownBoard, BoardMismatch, OldOwnerAliveOrUnknown, VirtualStillEnabled,
    HostPresentOrUnknown, RootMismatch, SourceStale, DinputNotObserved,
    RestoreOwnedPathsAndClaimModeAttempt
};
// Planning only: no success/discharge inference from a claim, send, pending
// write, or settings. Clear debts only after real effects are observed and the
// revised record is durable. The caller still checks debt and per-owner consume.
inline RecoveryDecision decideRecovery(const Facts& f) noexcept {
    if (!f.knownBoard) return RecoveryDecision::UnknownBoard;
    if (!f.boardMatch) return RecoveryDecision::BoardMismatch;
    if (!f.oldOwnerDead) return RecoveryDecision::OldOwnerAliveOrUnknown;
    if (!f.virtualDisabled) return RecoveryDecision::VirtualStillEnabled;
    if (!f.hostAbsent) return RecoveryDecision::HostPresentOrUnknown;
    if (!f.currentRootMatches) return RecoveryDecision::RootMismatch;
    if (!f.sourceFresh) return RecoveryDecision::SourceStale;
    if (!f.currentDinputObserved) return RecoveryDecision::DinputNotObserved;
    return RecoveryDecision::RestoreOwnedPathsAndClaimModeAttempt;
}
} // namespace ymcc::msi::recovery
