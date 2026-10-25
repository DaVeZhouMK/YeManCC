#pragma once
#include "json.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

// Bounded JSON-only cache diagnostics. No process, Steam, input, or recovery API.
namespace ymcc::steamoverlayprovidercache {
using Json = nlohmann::json;
namespace detail {
constexpr std::uint64_t safeInteger = 9007199254740991ULL;
constexpr std::uint64_t maxPid = 0xffffffffULL;
constexpr std::size_t maxRows = 128;
inline constexpr std::array<const char*, 6> fieldNames = {
    "m_unPID", "m_nBrowserID", "m_eBrowserType", "m_unAppID", "m_gameID", "m_eUIMode"};

template<std::size_t N>
inline bool shape(const Json& value, const std::array<const char*, N>& names) {
    if (!value.is_object() || value.size() != N) return false;
    for (const auto* name : names) if (!value.contains(name)) return false;
    return true;
}
inline bool boolean(const Json& value, const char* name, bool expected) {
    const auto found = value.find(name);
    return found != value.end() && found->is_boolean() && found->get<bool>() == expected;
}
inline bool integer(const Json& value, std::int64_t& number) {
    if (!value.is_number_integer()) return false; // Includes JSON unsigned integers, never bool/float.
    if (value.is_number_unsigned()) {
        const auto raw = value.get<std::uint64_t>();
        if (raw > safeInteger) return false;
        number = static_cast<std::int64_t>(raw);
    } else {
        number = value.get<std::int64_t>();
        if (number < -static_cast<std::int64_t>(safeInteger) || number > static_cast<std::int64_t>(safeInteger)) return false;
    }
    return true;
}
inline bool uint32(const Json& value, std::uint32_t& number) {
    std::int64_t raw = 0;
    if (!integer(value, raw) || raw < 0 || static_cast<std::uint64_t>(raw) > maxPid) return false;
    number = static_cast<std::uint32_t>(raw); return true;
}
inline bool decimal(const std::string& text, std::uint64_t limit, std::uint64_t& number) {
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text[0] == '0')) return false;
    std::uint64_t parsed = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<unsigned>(c - '0');
        if (parsed > (limit - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    number = parsed; return true;
}
inline bool signedDecimal(const std::string& text) {
    if (text.empty() || text.size() > 21) return false;
    const std::size_t begin = text[0] == '-' ? 1U : 0U;
    if (begin == text.size() || (text[begin] == '0' && (begin != 0 || text.size() != 1))) return false;
    for (std::size_t i = begin; i < text.size(); ++i) if (text[i] < '0' || text[i] > '9') return false;
    return true;
}
// nlohmann stores UTF-8. Count UTF-16 units, rejecting malformed UTF-8 as well
// as overlong strings, without dumping or walking arbitrary nested JSON.
inline bool shortString(const std::string& text) {
    if (text.size() > 256) return false;
    std::size_t units = 0, offset = 0;
    while (offset < text.size()) {
        const auto first = static_cast<unsigned char>(text[offset++]);
        std::uint32_t point = first; unsigned remaining = 0;
        if (first < 0x80) {}
        else if (first >= 0xc2 && first <= 0xdf) { point = first & 0x1fU; remaining = 1; }
        else if (first >= 0xe0 && first <= 0xef) { point = first & 0x0fU; remaining = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { point = first & 0x07U; remaining = 3; }
        else return false;
        if (offset + remaining > text.size()) return false;
        for (unsigned i = 0; i < remaining; ++i) {
            const auto next = static_cast<unsigned char>(text[offset++]);
            if ((next & 0xc0U) != 0x80U) return false;
            point = (point << 6) | (next & 0x3fU);
        }
        if ((remaining == 1 && point < 0x80) || (remaining == 2 && point < 0x800) ||
            (remaining == 3 && point < 0x10000) || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
        units += point > 0xffff ? 2U : 1U;
        if (units > 64) return false;
    }
    return true;
}
inline bool tag(const Json& value) {
    if (!shape(value, std::array<const char*, 2>{"rawType", "value"}) || !value.at("rawType").is_string()) return false;
    const auto& type = value.at("rawType").get_ref<const std::string&>();
    const auto& raw = value.at("value");
    if (type == "undefined" || type == "null" || type == "object" || type == "function" || type == "symbol") return raw.is_null();
    if (type == "boolean") return raw.is_boolean();
    if (type == "number") { std::int64_t ignored = 0; return raw.is_null() || integer(raw, ignored); }
    if (type == "bigint") return raw.is_string() && signedDecimal(raw.get_ref<const std::string&>());
    if (type == "string") return raw.is_string() && shortString(raw.get_ref<const std::string&>());
    return false;
}
// Caller validates the exact tag shape first. Null Number values are never
// repaired using the entry key, AppID, or any other rounded representation.
inline bool identity(const Json& tagged, std::uint64_t limit, bool allowZero, std::uint64_t& number) {
    const auto& type = tagged.at("rawType").get_ref<const std::string&>();
    const auto& raw = tagged.at("value");
    if (type == "number") {
        std::int64_t parsed = 0;
        if (!integer(raw, parsed) || parsed < 0 || static_cast<std::uint64_t>(parsed) > limit) return false;
        number = static_cast<std::uint64_t>(parsed);
    } else if (type == "string" || type == "bigint") {
        if (!decimal(raw.get_ref<const std::string&>(), limit, number)) return false;
    } else return false;
    return allowZero || number != 0;
}
inline bool appid(const Json& tagged, std::uint32_t& number) {
    if (tagged.at("rawType") == "number") {
        std::int64_t raw = 0;
        if (!integer(tagged.at("value"), raw) || raw < -2147483648LL || raw > 4294967295LL) return false;
        number = static_cast<std::uint32_t>(raw); return number != 0;
    }
    std::uint64_t raw = 0;
    if (!identity(tagged, maxPid, false, raw)) return false;
    number = static_cast<std::uint32_t>(raw); return true;
}
inline bool belongs(std::uint64_t game, std::uint32_t app) {
    return (game != 0 && game <= 0xffffffULL && game == app) ||
        ((game & 0xffffffffULL) == 0x02000000ULL && (game >> 32) == app);
}
inline const char* reason(const Json& value) {
    if (!value.is_string()) return nullptr;
    const auto& text = value.get_ref<const std::string&>();
    for (const auto* label : {"source-qualified-cache-read", "no-target", "provider-store-unavailable",
        "provider-source-unverified", "data-path-unqualified", "map-brand-unqualified", "map-budget",
        "field-unavailable", "cache-changed", "read-window-expired", "summary-budget",
        "cache-read-unavailable", "diagnostic-unavailable", "original-identity-bracket-unqualified"})
        if (text == label) return label;
    return nullptr;
}
} // namespace detail

// All four parameters stay JSON so wrong types cannot be silently narrowed.
// fullGameId is the existing native decimal GameID, never derived from AppID.
inline Json summarize(const Json& read, const Json& targetAppid, const Json& originalPid, const Json& fullGameId) {
    Json result = {{"implementation", "steam-overlay-provider-cache-v1"}, {"readOnly", true},
        {"sourceMatched", false}, {"sourcePathQualified", false}, {"available", false}, {"reason", "diagnostic-unavailable"},
        {"appid", nullptr}, {"originalPid", nullptr}, {"fullGameIdExactString", nullptr}, {"mapEntryCount", nullptr},
        {"targetMembership", "unknown"}, {"samePidOtherGameIdCount", nullptr}, {"targetFieldsTagged", nullptr},
        {"sameGameIdCandidateCount", nullptr}, {"sameGameIdCandidatesComplete", false}, {"sameGameIdCandidatesTagged", nullptr},
        {"snapshotAtomic", false}, {"originalProviderObjectVerified", false}, {"steamContactVerified", false},
        {"actualInputVerified", false}, {"actualContinueVerified", false}, {"recoveryAdmission", false}, {"pidMatchIsIdentityProof", false}};
    try {
        std::uint32_t expectedApp = 0, expectedPid = 0; std::uint64_t expectedGame = 0;
        if (!detail::uint32(targetAppid, expectedApp) || expectedApp == 0) return result;
        result["appid"] = expectedApp;
        if (!detail::uint32(originalPid, expectedPid) || expectedPid == 0) return result;
        result["originalPid"] = expectedPid;
        if (!fullGameId.is_string() || !detail::decimal(fullGameId.get_ref<const std::string&>(),
            (std::numeric_limits<std::uint64_t>::max)(), expectedGame) || !detail::belongs(expectedGame, expectedApp)) return result;
        result["fullGameIdExactString"] = fullGameId;
        if (!read.is_object() || !detail::boolean(read, "ok", true) || !detail::boolean(read, "mutated", false)) return result;
        const auto found = read.find("overlayProviderCacheDiagnostic");
        if (found == read.end()) return result;
        const auto& wire = *found;
        if (!detail::shape(wire, std::array<const char*, 15>{"implementation", "readOnly", "sourceMatched", "sourcePathQualified",
            "available", "reason", "mapEntryCount", "passesComplete", "rows", "snapshotAtomic", "originalProviderObjectVerified",
            "steamContactVerified", "actualInputVerified", "actualContinueVerified", "recoveryAdmission"})) return result;
        if (wire.at("implementation") != "steam-overlay-provider-cache-v1" || !detail::boolean(wire, "readOnly", true)) return result;
        for (const auto* key : {"snapshotAtomic", "originalProviderObjectVerified", "steamContactVerified", "actualInputVerified", "actualContinueVerified", "recoveryAdmission"})
            if (!detail::boolean(wire, key, false)) return result;
        for (const auto* key : {"sourceMatched", "sourcePathQualified", "available"}) if (!wire.at(key).is_boolean()) return result;
        const char* why = detail::reason(wire.at("reason"));
        if (!why) return result;
        result["sourceMatched"] = wire.at("sourceMatched"); result["sourcePathQualified"] = wire.at("sourcePathQualified");
        const auto& rows = wire.at("rows"); const auto& passes = wire.at("passesComplete");
        if (!wire.at("available").get<bool>()) {
            if (wire.at("mapEntryCount").is_null() && rows.is_null() && passes.is_null() && std::string(why) != "source-qualified-cache-read") result["reason"] = why;
            return result;
        }
        if (!detail::boolean(wire, "sourceMatched", true) || !detail::boolean(wire, "sourcePathQualified", true) ||
            std::string(why) != "source-qualified-cache-read") return result;
        if (!passes.is_array() || passes.size() != 2 || !passes[0].is_boolean() || !passes[1].is_boolean() ||
            !passes[0].get<bool>() || !passes[1].get<bool>()) return result;
        std::uint32_t count = 0;
        if (!detail::uint32(wire.at("mapEntryCount"), count) || count > detail::maxRows || !rows.is_array() || rows.size() > detail::maxRows) {
            result["reason"] = "map-budget"; return result;
        }
        if (rows.size() != count) return result;
        std::array<std::string, detail::maxRows> keys{}; std::size_t used = 0;
        const Json* selected = nullptr; std::uint32_t samePidOther = 0; bool appConflict = false;
        std::array<const Json*, 2> sameGameCandidates{}; std::uint32_t sameGameCount = 0;
        for (const auto& row : rows) {
            result["reason"] = "field-unavailable";
            if (!detail::shape(row, std::array<const char*, 2>{"entryKey", "fields"}) || !row.at("entryKey").is_string()) return result;
            const auto& key = row.at("entryKey").get_ref<const std::string&>();
            if (!detail::shortString(key)) return result;
            for (std::size_t i = 0; i < used; ++i) if (keys[i] == key) return result;
            keys[used++] = key;
            const auto& fields = row.at("fields");
            if (!detail::shape(fields, detail::fieldNames)) return result;
            for (const auto* name : detail::fieldNames) if (!detail::tag(fields.at(name))) return result;
            std::uint64_t pid = 0, game = 0;
            if (!detail::identity(fields.at("m_unPID"), detail::maxPid, true, pid) ||
                !detail::identity(fields.at("m_gameID"), (std::numeric_limits<std::uint64_t>::max)(), false, game) ||
                key != std::to_string(game) + ":" + std::to_string(pid)) return result;
            if (pid == expectedPid && game != expectedGame) ++samePidOther;
            if (game == expectedGame) {
                std::uint32_t rowApp = 0;
                if (!detail::appid(fields.at("m_unAppID"), rowApp) || rowApp != expectedApp) appConflict = true;
                else {
                    if (sameGameCount < sameGameCandidates.size()) sameGameCandidates[sameGameCount] = &fields;
                    ++sameGameCount;
                }
                if (pid == expectedPid) selected = &fields;
            }
        }
        // The producer promises an 8KiB transport. Check it only after the
        // closed, bounded scalar schema has validated every row.
        if (wire.dump().size() > 8192) { result["reason"] = "summary-budget"; return result; }
        // Aggregate only a completely validated map; foreign keys/fields never escape.
        auto valid = result;
        valid["mapEntryCount"] = count; valid["samePidOtherGameIdCount"] = samePidOther;
        if (samePidOther != 0 || appConflict) { valid["reason"] = "field-unavailable"; return valid; }
        valid["available"] = true; valid["reason"] = selected ? "source-qualified-cache-read" : "no-target";
        valid["targetMembership"] = selected ? "present-in-two-cache-reads" : "absent-in-two-cache-reads";
        if (selected) valid["targetFieldsTagged"] = *selected;
        // Diagnostic descriptor tags retain their raw PID domain and never affect target membership.
        valid["sameGameIdCandidateCount"] = sameGameCount;
        if (sameGameCount <= sameGameCandidates.size()) {
            valid["sameGameIdCandidatesTagged"] = Json::array();
            for (std::uint32_t i = 0; i < sameGameCount; ++i)
                valid["sameGameIdCandidatesTagged"].push_back(*sameGameCandidates[i]);
            valid["sameGameIdCandidatesComplete"] = true;
        }
        return valid;
    } catch (...) { return result; } // Failure cannot change the existing read or admission.
}
} // namespace ymcc::steamoverlayprovidercache
