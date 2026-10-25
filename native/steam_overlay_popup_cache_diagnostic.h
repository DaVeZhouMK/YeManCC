#pragma once
#include "json.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

// Steam's popup store is only a cache prerequisite. It proves neither process
// identity/contact nor actual input/Continue, and never participates in admission.
namespace ymcc::steamoverlaycache {
using Json = nlohmann::json;
namespace detail {
inline bool uint32(const Json& value, std::uint32_t& out) {
    if (!value.is_number_integer() && !value.is_number_unsigned()) return false;
    if (!value.is_number_unsigned() && value.get<std::int64_t>() < 0) return false;
    const auto number = value.get<std::uint64_t>();
    if (number > (std::numeric_limits<std::uint32_t>::max)()) return false;
    out = static_cast<std::uint32_t>(number); return true;
}
inline bool boolean(const Json& value, const char* key, bool expected) {
    const auto found = value.find(key);
    return found != value.end() && found->is_boolean() && found->get<bool>() == expected;
}
inline const char* reason(const Json& value) {
    if (!value.is_string()) return "diagnostic-unavailable";
    const auto& text = value.get_ref<const std::string&>();
    for (const auto* label : {"source-qualified-cache-read", "no-target", "window-store-unavailable",
        "map-unavailable", "map-budget", "getter-source-unverified", "target-entry-unknown",
        "cache-changed", "cache-read-unavailable"})
        if (text == label) return label;
    return "diagnostic-unavailable";
}
} // namespace detail

// JSON parameters intentionally keep bool/float/negative/oversize IDs from being
// silently narrowed at the caller. The original pinned PID is only a candidate
// comparison: PID 0 in a cache stays raw data and is never an expected game PID.
inline Json summarize(const Json& read, const Json& targetAppid, const Json& originalPid = nullptr) {
    Json result = {{"implementation", "steam-overlay-popup-cache-v1"}, {"readOnly", true},
        {"appid", nullptr}, {"sourceMatched", false}, {"available", false},
        {"reason", "diagnostic-unavailable"}, {"popupCount", nullptr}, {"pidCandidates", nullptr},
        {"expectedPidCandidateMatch", nullptr}, {"normalRaisePrerequisite", "unknown"},
        {"steamContactVerified", false}, {"actualInputVerified", false}, {"actualContinueVerified", false},
        {"recoveryAdmission", false}, {"pidMatchIsIdentityProof", false}};
    try {
        std::uint32_t appid = 0, expectedPid = 0;
        if (!detail::uint32(targetAppid, appid) || appid == 0) return result;
        result["appid"] = appid;
        const bool expectedValid = detail::uint32(originalPid, expectedPid) && expectedPid != 0;
        if (!read.is_object() || !detail::boolean(read, "ok", true) || !detail::boolean(read, "mutated", false)) return result;
        const auto input = read.find("overlayPopupCacheDiagnostic");
        if (input == read.end() || !input->is_object()) return result;
        const auto& diagnostic = *input;
        const auto implementation = diagnostic.find("implementation");
        if (implementation == diagnostic.end() || !implementation->is_string() ||
            implementation->get_ref<const std::string&>() != "steam-overlay-popup-cache-v1" ||
            !detail::boolean(diagnostic, "readOnly", true)) return result;
        const auto source = diagnostic.find("sourceMatched"), rows = diagnostic.find("rows");
        if (source == diagnostic.end() || !source->is_boolean() || rows == diagnostic.end() ||
            !rows->is_array() || rows->size() > 16) return result;
        result["sourceMatched"] = source->get<bool>();
        const Json* selected = nullptr;
        for (const auto& row : *rows) {
            if (!row.is_object()) return result;
            const auto id = row.find("appid"); std::uint32_t rowAppid = 0;
            if (id == row.end() || !detail::uint32(*id, rowAppid)) return result;
            if (rowAppid != appid) continue;
            if (selected) return result; // No first/last-wins target selection.
            selected = &row;
        }
        if (!selected) { result["reason"] = "no-target"; return result; }
        const auto available = selected->find("available"), why = selected->find("reason");
        const auto count = selected->find("popupCount"), pids = selected->find("pidCandidates");
        if (available == selected->end() || !available->is_boolean() || why == selected->end() ||
            count == selected->end() || pids == selected->end()) return result;
        const char* label = detail::reason(*why);
        if (std::string(label) == "diagnostic-unavailable") return result;
        if (!available->get<bool>()) {
            if (!count->is_null() || !pids->is_null() || std::string(label) == "source-qualified-cache-read") return result;
            result["reason"] = label; return result;
        }
        // Only a stable, source-qualified successful cache read may publish a
        // count or candidates. Missing/unverified/failed/changed data stays null.
        if (!source->get<bool>() || std::string(label) != "source-qualified-cache-read") return result;
        std::uint32_t popupCount = 0;
        if (!detail::uint32(*count, popupCount) || popupCount > 64 || !pids->is_array() ||
            pids->size() > 64 || pids->size() != popupCount) return result;
        std::array<std::uint32_t, 64> unique{}; std::size_t used = 0; bool match = false;
        Json candidates = Json::array();
        for (const auto& value : *pids) {
            std::uint32_t pid = 0;
            if (!detail::uint32(value, pid)) return result;
            for (std::size_t i = 0; i < used; ++i) if (unique[i] == pid) return result;
            unique[used++] = pid; candidates.push_back(pid);
            if (expectedValid && pid == expectedPid) match = true;
        }
        auto valid = result; // Commit only after every count/PID has validated.
        valid["available"] = true; valid["reason"] = label;
        valid["popupCount"] = popupCount; valid["pidCandidates"] = std::move(candidates);
        if (expectedValid) valid["expectedPidCandidateMatch"] = match;
        valid["normalRaisePrerequisite"] = popupCount == 0 ? "empty" : "nonempty";
        return valid;
    } catch (...) { return result; } // Diagnostic failure never rejects the read.
}
} // namespace ymcc::steamoverlaycache
