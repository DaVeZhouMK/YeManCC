#pragma once
#include "json.hpp"
#include "steam_desktop_binding_evidence.h"
#include "steamdeck_mouse_vdf.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

// Pure collector over the supplied existing-session Ops. No file enumeration,
// process discovery, live Steam, fallback account, platform APIs or IO here.
namespace ymcc::steamDesktopCapture {
using json = nlohmann::json;
inline constexpr std::uint32_t perFileCap = 2u * 1024u * 1024u;
inline constexpr std::uint32_t libraryFoldersCap = 1024u * 1024u;
inline constexpr std::uint32_t selectionCap = 256u * 1024u;
inline constexpr std::uint64_t totalByteCap = 8ull * 1024ull * 1024ull;
inline constexpr std::size_t libraryLimit = 16, readCallLimit = 66, bodyCandidateLimit = 4;
inline constexpr std::uint64_t checkpointBudgetMs = 1000;
namespace detail {
inline bool normalizeRoot(std::wstring value, std::wstring& result) {
    if (value.empty() || value.size() > 32767) return false;
    for (auto& c : value) {
        if (c == L'/') c = L'\\';
        if (c < 32 || c == L'"' || c == L'?' || c == L'*' || c == L'<' || c == L'>' || c == L'|') return false;
    }
    std::wstring prefix; std::size_t pos = 0;
    if (value.size() >= 3 && ((value[0] >= L'a' && value[0] <= L'z') || (value[0] >= L'A' && value[0] <= L'Z')) && value[1] == L':' && value[2] == L'\\') {
        prefix = value.substr(0, 3); if (prefix[0] >= L'a' && prefix[0] <= L'z') prefix[0] -= L'a' - L'A'; pos = 3;
    } else if (value.size() >= 5 && value[0] == L'\\' && value[1] == L'\\') {
        const auto serverEnd = value.find(L'\\', 2);
        if (serverEnd == std::wstring::npos || serverEnd == 2) return false;
        const auto shareEnd = value.find(L'\\', serverEnd + 1);
        const auto share = value.substr(serverEnd + 1, shareEnd == std::wstring::npos ? shareEnd : shareEnd - serverEnd - 1);
        const auto server = value.substr(2, serverEnd - 2);
        if (share.empty() || share == L"." || share == L".." || server == L"." || server == L"..") return false;
        if (server.back() == L'.' || server.back() == L' ' || share.back() == L'.' || share.back() == L' ') return false;
        prefix = L"\\\\" + server + L"\\" + share + L"\\";
        pos = shareEnd == std::wstring::npos ? value.size() : shareEnd + 1;
    } else return false;
    if (value.find(L':', 2) != std::wstring::npos || prefix.find(L':', 2) != std::wstring::npos) return false;
    std::wstring suffix;
    for (; pos < value.size();) {
        const auto end = value.find(L'\\', pos);
        const auto segment = value.substr(pos, end == std::wstring::npos ? end : end - pos);
        pos = end == std::wstring::npos ? value.size() : end + 1;
        if (segment.empty() || segment == L".") continue;
        if (segment == L"..") {
            if (suffix.empty()) return false;
            suffix.pop_back(); const auto slash = suffix.find_last_of(L'\\');
            suffix = slash == std::wstring::npos ? L"" : suffix.substr(0, slash + 1); continue;
        }
        if (segment.back() == L'.' || segment.back() == L' ' || segment.find(L':') != std::wstring::npos) return false;
        suffix += segment + L"\\";
    }
    result = prefix + suffix; return true;
}
inline std::wstring rootKey(std::wstring root) {
    for (auto& c : root) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    return root;
}
inline bool sha256Hex(std::string& hash) {
    if (hash.size() != 64) return false;
    for (auto& c : hash) {
        if (c >= 'A' && c <= 'F') c += 'a' - 'A';
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}
struct Read {
    std::string bytes;
    json evidence = {{"status", "not-read"}, {"readBytes", 0}, {"originalBytes", nullptr}, {"sha256", nullptr}};
    bool complete = false;
};
template<class Ops, class Client> struct Reader {
    Ops& ops; const Client& client;
    std::uint64_t begin, last, bytes = 0;
    std::size_t reads = 0, errors = 0;
    bool stopped = false, stale = false, sessionUnknown = false;
    std::string stopCode;
    bool checkpoint() {
        if (stopped) return false;
        last = ops.now();
        if (last < begin) { stopped = true; stopCode = "clock-regressed"; }
        else if (last - begin >= checkpointBudgetMs) { stopped = true; stopCode = "checkpoint-time-budget"; }
        return !stopped;
    }
    Read read(const std::wstring& path, std::uint32_t requestedCap) {
        Read result;
        if (!checkpoint()) { result.evidence["status"] = stopCode; return result; }
        if (reads >= readCallLimit || bytes >= totalByteCap) {
            stopped = true; stopCode = reads >= readCallLimit ? "read-call-budget" : "total-byte-budget";
            result.evidence["status"] = stopCode; return result;
        }
        try {
            if (!ops.sameSession(client)) { stopped = stale = true; stopCode = "session-changed"; result.evidence["status"] = stopCode; return result; }
        } catch (...) { stopped = sessionUnknown = true; ++errors; stopCode = "session-check-error"; result.evidence["status"] = stopCode; return result; }
        const auto cap = static_cast<std::uint32_t>((std::min)({std::uint64_t(requestedCap), std::uint64_t(perFileCap), totalByteCap - bytes}));
        ++reads; result.evidence["readOrdinal"] = reads; result.evidence["requestedCap"] = cap;
        try {
            auto raw = ops.read(path, cap);
            const auto size = static_cast<std::uint64_t>(raw.bytes.size());
            bytes = size > (std::numeric_limits<std::uint64_t>::max)() - bytes ? (std::numeric_limits<std::uint64_t>::max)() : bytes + size;
            result.bytes = std::move(raw.bytes);
            result.evidence["readBytes"] = size; result.evidence["originalBytes"] = raw.originalBytes;
            if (raw.available) {
                try { auto hash = ops.hash(result.bytes); if (sha256Hex(hash)) result.evidence["sha256"] = std::move(hash); else { result.evidence["hashStatus"] = "hash-invalid"; ++errors; } }
                catch (...) { result.evidence["hashStatus"] = "hash-error"; ++errors; }
            }
            if (size > cap || bytes > totalByteCap) { stopped = true; stopCode = "read-contract-budget-exceeded"; result.evidence["status"] = stopCode; }
            else if (!raw.available) result.evidence["status"] = "unavailable";
            else if (raw.truncated || raw.originalBytes > cap) {
                result.evidence["status"] = cap < requestedCap ? "total-byte-budget" : "truncated";
                if (cap < requestedCap) { stopped = true; stopCode = "total-byte-budget"; }
            }
            else if (raw.originalBytes != size) result.evidence["status"] = "short-or-inconsistent-read";
            else { result.complete = true; result.evidence["status"] = "complete"; }
        } catch (...) { ++errors; result.evidence["status"] = "read-error"; }
        checkpoint();
        return result;
    }
};
inline json limits() {
    return {{"perFileBytes", perFileCap}, {"libraryFoldersBytes", libraryFoldersCap}, {"selectionBytes", selectionCap},
        {"totalBytes", totalByteCap}, {"readCalls", readCallLimit}, {"libraries", libraryLimit}, {"bodyCandidates", bodyCandidateLimit},
        {"checkpointMs", checkpointBudgetMs}, {"singleIoHardBound", false}};
}
} // namespace detail

template<class Ops> json collect(bool detailedEnabled, Ops& ops) {
    json out = {{"schemaVersion", 1}, {"status", detailedEnabled ? "unknown" : "disabled"}, {"detailedEnabled", detailedEnabled},
        {"client", nullptr}, {"captureBeginTick", nullptr}, {"captureEndTick", nullptr}, {"budgets", detail::limits()},
        {"storedDesktopCandidates", json::array()}, {"libraryObservations", json::array()},
        {"declaredLibrariesStatus", "not-read"}, {"declaredLibraryCoverageComplete", false}, {"overallCoverageComplete", false}, {"sessionEstablished", false},
        {"libraryCount", 0}, {"duplicateLibraryRoots", 0}, {"rejectedLibraryRoots", 0}, {"omittedLibraryRoots", 0},
        {"observedDesktopCandidateCount", 0}, {"omittedBodyCandidates", 0}, {"readCalls", 0}, {"readBytes", 0},
        {"captureErrors", 0}, {"staleSession", false}, {"sameSessionAtEnd", nullptr}, {"sessionState", "unverified"},
        {"active", false}, {"activePresetKnown", false}, {"producer", false}, {"sourceAttributionKnown", false},
        {"deliveryProven", false}, {"absenceProven", false}, {"exclusiveLayoutProven", false},
        {"coverageScope", "declaredLibraryCoverageComplete covers declared-root resolution only; overallCoverageComplete additionally covers reads, summaries, rechecks and omissions"},
        {"rootDeduplication", "absolute lexical slash/dot normalization and ASCII case; no filesystem identity resolution"},
        {"scope", "Stored current-session/current-account desktop autosave evidence only. No live selection, source, delivery, exclusivity or absence proof; selection/body re-reads are sequential, not atomic. Time budget is checked at checkpoints and cannot bound a single Ops read."}};
    if (!detailedEnabled) return out;
    try {
        const auto begin = ops.now(); out["captureBeginTick"] = begin;
        const auto client = ops.session();
        detail::Reader<Ops, decltype(client)> reader{ops, client, begin, begin};
        bool declaredCoverage = false, overallCoverage = true, established = false; std::size_t observed = 0, omitted = 0;
        auto finish = [&]() {
            if (established) {
                try { const bool same = ops.sameSession(client); out["sameSessionAtEnd"] = same; if (!same) reader.stale = true; }
                catch (...) { reader.sessionUnknown = true; ++reader.errors; }
            }
            try { out["captureEndTick"] = ops.now(); } catch (...) { ++reader.errors; out["captureEndTick"] = nullptr; }
            out["readCalls"] = reader.reads; out["readBytes"] = reader.bytes; out["captureErrors"] = reader.errors;
            out["staleSession"] = reader.stale;
            out["sessionState"] = reader.stale ? "stale" : reader.sessionUnknown ? "unknown" : out["sameSessionAtEnd"] == true ? "unchanged-at-checkpoints" : "unverified";
            if (reader.stale) out["status"] = "stale-session";
            else if (reader.sessionUnknown) out["status"] = "session-check-error";
            else if (reader.stopped) out["status"] = reader.stopCode;
            out["sessionEstablished"] = established;
            out["declaredLibraryCoverageComplete"] = established && declaredCoverage && !reader.stale && !reader.sessionUnknown;
            out["overallCoverageComplete"] = established && declaredCoverage && overallCoverage && !reader.stopped && !reader.stale && !reader.sessionUnknown && !omitted && !reader.errors;
            out["observedDesktopCandidateCount"] = observed; out["omittedBodyCandidates"] = omitted;
            out["ambiguousStoredCandidates"] = observed > 1;
        };
        try {
            if (!ops.validSession(client) || client.account == 0) { out["status"] = "invalid-session"; declaredCoverage = overallCoverage = false; finish(); return out; }
            if (!ops.sameSession(client)) { out["status"] = "initial-session-unverified"; declaredCoverage = overallCoverage = false; finish(); return out; }
            established = true;
            out["client"] = ops.clientIdentity(client);
            std::wstring root;
            if (!detail::normalizeRoot(client.root, root)) { out["status"] = "invalid-session-root"; declaredCoverage = overallCoverage = false; finish(); return out; }
            std::array<std::wstring, libraryLimit> libraries{};
            std::array<std::wstring, 65> keys{}; // existing libraryPaths accepts at most 64 declarations plus root
            libraries[0] = root; keys[0] = detail::rootKey(root); std::size_t libraryCount = 1, seenCount = 1;
            const auto folders = reader.read(root + L"steamapps\\libraryfolders.vdf", libraryFoldersCap);
            out["libraryFoldersRead"] = folders.evidence;
            if (folders.complete && !folders.bytes.empty() && !reader.stopped) {
                try {
                    const auto declared = steamdeck::libraryPaths(folders.bytes); out["declaredLibrariesStatus"] = "parsed"; declaredCoverage = true;
                    for (const auto& path : declared) {
                        std::wstring normalized;
                        try { if (!detail::normalizeRoot(ops.widen(path), normalized)) { ++out["rejectedLibraryRoots"].template get_ref<json::number_integer_t&>(); declaredCoverage = false; continue; } }
                        catch (...) { ++reader.errors; ++out["rejectedLibraryRoots"].template get_ref<json::number_integer_t&>(); declaredCoverage = false; continue; }
                        const auto key = detail::rootKey(normalized); bool duplicate = false;
                        for (std::size_t i = 0; i < seenCount; ++i) if (keys[i] == key) { duplicate = true; break; }
                        if (duplicate) { ++out["duplicateLibraryRoots"].template get_ref<json::number_integer_t&>(); continue; }
                        keys[seenCount++] = key;
                        if (libraryCount >= libraryLimit) { ++out["omittedLibraryRoots"].template get_ref<json::number_integer_t&>(); declaredCoverage = false; continue; }
                        libraries[libraryCount] = std::move(normalized); ++libraryCount;
                    }
                } catch (...) { out["declaredLibrariesStatus"] = "invalid-or-ambiguous"; declaredCoverage = false; }
            } else { out["declaredLibrariesStatus"] = folders.complete ? "empty-unknown" : "unavailable-or-incomplete"; declaredCoverage = false; }
            out["libraryCount"] = libraryCount;
            for (std::size_t ordinal = 0; ordinal < libraryCount && reader.checkpoint(); ++ordinal) {
                const auto config = libraries[ordinal] + L"steamapps\\common\\Steam Controller Configs\\" + std::to_wstring(client.account) + L"\\config\\";
                const auto selectionPath = config + L"configset_controller_neptune.vdf";
                auto selection = reader.read(selectionPath, selectionCap);
                json observation = {{"libraryOrdinal", ordinal}, {"selection", selection.evidence}, {"status", "selection-unknown"}};
                bool autosave = false;
                if (selection.complete && !reader.stopped) {
                    try { autosave = steamdeck::desktopAutosave(selection.bytes); observation["status"] = autosave ? "desktop-autosave-reference" : "desktop-autosave-not-confirmed"; }
                    catch (...) { observation["status"] = "selection-invalid-or-ambiguous"; overallCoverage = false; }
                } else overallCoverage = false;
                out["libraryObservations"].push_back(std::move(observation));
                if (!autosave || reader.stopped) continue;
                ++observed;
                if (out["storedDesktopCandidates"].size() >= bodyCandidateLimit) { ++omitted; overallCoverage = false; continue; }
                json candidate = {{"libraryOrdinal", ordinal}, {"status", "unknown"}, {"summary", nullptr},
                    {"selectionFirst", selection.evidence}, {"selectionSecond", nullptr}, {"bodyFirst", nullptr}, {"bodySecond", nullptr},
                    {"selectionBytesStable", nullptr}, {"bodyBytesStable", nullptr}, {"exactBytesStable", nullptr},
                    {"selectionRecheckAutosave", nullptr}, {"active", false}, {"activePresetKnown", false}, {"producer", false}, {"absenceProven", false}};
                const auto bodyPath = config + L"413080\\controller_neptune.vdf";
                auto body = reader.read(bodyPath, perFileCap); candidate["bodyFirst"] = body.evidence;
                if (body.complete && !reader.stopped) {
                    try { candidate["summary"] = steamdeck::bindingevidence::summarize(body.bytes);
                        const bool ok = candidate["summary"]["ok"] == true, complete = candidate["summary"]["coverageComplete"] == true;
                        candidate["status"] = !ok ? "summary-unrecognized" : complete ? "summarized" : "summary-partial";
                        if (!ok || !complete) overallCoverage = false; }
                    catch (...) { candidate["status"] = "summary-error"; ++reader.errors; }
                } else { candidate["status"] = "body-unavailable-or-incomplete"; overallCoverage = false; }
                if (reader.checkpoint()) {
                    auto secondSelection = reader.read(selectionPath, selectionCap); candidate["selectionSecond"] = secondSelection.evidence;
                    if (secondSelection.complete) {
                        candidate["selectionBytesStable"] = selection.bytes == secondSelection.bytes;
                        try { candidate["selectionRecheckAutosave"] = steamdeck::desktopAutosave(secondSelection.bytes); }
                        catch (...) { candidate["selectionRecheckStatus"] = "invalid-or-ambiguous"; }
                    }
                    auto secondBody = reader.read(bodyPath, perFileCap); candidate["bodySecond"] = secondBody.evidence;
                    if (body.complete && secondBody.complete) candidate["bodyBytesStable"] = body.bytes == secondBody.bytes;
                    if (candidate["selectionBytesStable"].is_boolean() && candidate["bodyBytesStable"].is_boolean()) candidate["exactBytesStable"] = candidate["selectionBytesStable"].template get<bool>() && candidate["bodyBytesStable"].template get<bool>();
                }
                if (candidate["exactBytesStable"] != true) overallCoverage = false;
                out["storedDesktopCandidates"].push_back(std::move(candidate));
            }
            out["status"] = !observed ? (declaredCoverage && overallCoverage && !reader.errors ? "no-candidate-observed" : "unknown") : declaredCoverage && overallCoverage && !omitted && !reader.errors ? "captured" : "partial";
        } catch (...) { out["status"] = "capture-error"; overallCoverage = false; ++reader.errors; }
        finish();
    } catch (...) { out["status"] = "capture-error"; out["captureErrors"] = out["captureErrors"].template get<std::size_t>() + 1; }
    return out;
}
} // namespace ymcc::steamDesktopCapture
