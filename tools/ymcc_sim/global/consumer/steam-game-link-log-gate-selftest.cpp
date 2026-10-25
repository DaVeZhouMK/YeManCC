// Execute extracted production functions; only surrounding OS metadata and log roots are fixtures.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"
#include "log_frequency_policy.h"
#include "log_snapshot_state.h"
#include "log_jsonl_boundary.h"
#include "steam_overlay_provider_cache_diagnostic.h"
using json = nlohmann::json;
namespace fspath = std::filesystem;
static fspath::path fixtureRoot;
static std::wstring app_data_dir() { return fixtureRoot.wstring(); }
static HWND g_hwnd = nullptr;
enum class PowerLifecycle { Ready };
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static const char* powerLifecycleName(PowerLifecycle) { return "ready-fixture"; }
static std::atomic<unsigned long long> g_powerGeneration{1};
static std::atomic<bool> g_domainLogEnabled[2]{};
// Domain mirrors are outside this consumer; no domain files/configuration are opened.
static void mirrorLifecycleToDomains(const char*, const json&) {}
static std::mutex g_nativeLifecycleLogMx;
static std::ofstream g_nativeLifecycleLog, g_nativeDetailLog;
static uint32_t g_nativeLifecycleFrameWrites = 0, g_nativeDetailWrites = 0;
static ULONGLONG g_nativeDetailLastFlushTick = 0;
static constexpr unsigned long long kLogMaxBytes = 5ull * 1024ull * 1024ull;
static void rotateOneLogFile(const std::wstring&, unsigned long long = kLogMaxBytes);
#include "extracted-main.h"
namespace SteamGameLink {
#include "extracted-publish.h"
}
static json checks = json::array();
static void check(const char* name, bool pass) { checks.push_back({{"name", name}, {"pass", pass}}); }
static std::vector<json> rows(const wchar_t* name) {
    if (g_nativeLifecycleLog.is_open()) g_nativeLifecycleLog.flush();
    if (g_nativeDetailLog.is_open()) g_nativeDetailLog.flush();
    std::ifstream input(fixtureRoot / name); std::string line; std::vector<json> out;
    while (std::getline(input, line)) if (!line.empty()) out.push_back(json::parse(line));
    return out;
}
static std::shared_ptr<SteamGameLink::Control> scenario(const fspath::path& root, const char* name, bool detail = false) {
    g_nativeLifecycleLog.close(); g_nativeLifecycleLog.clear();
    g_nativeDetailLog.close(); g_nativeDetailLog.clear();
    g_nativeLifecycleFrameWrites = g_nativeDetailWrites = 0; g_nativeDetailLastFlushTick = 0;
    g_domainLogEnabled[1].store(detail); fixtureRoot = root / name;
    fspath::create_directories(fixtureRoot);
    return std::make_shared<SteamGameLink::Control>();
}
static json idle() { return {{"phase", "idle"}, {"reason", "waiting-unique-original-instance"}, {"linked", false}, {"active", false}}; }
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const fspath::path root = fspath::u8path(argv[1]);
    const bool ordinaryOnly = argc > 2 && std::string(argv[2]) == "--ordinary-only";
    {
        auto c = scenario(root, "ordinary-off");
        const auto value = idle();
        const auto lane = ymcc::logging::nativeLane(nativeLifecycleEventIsBase("steam-game-link-state"),
            nativeLifecycleRecordIsStreaming("steam-game-link-state", value), g_domainLogEnabled[1].load());
        check("ordinary-gate-off-selects-event-lane", lane == ymcc::logging::NativeLane::event);
        SteamGameLink::publish(c, value, true);
        const auto baseRows = rows(L"native-lifecycle.log");
        check("ordinary-gate-off-publish-reaches-native-file", baseRows.size() == 1 && baseRows[0].value("event", "") == "steam-game-link-state");
        check("ordinary-gate-off-no-detailed-file", !fspath::exists(fixtureRoot / L"native-detail.log"));
        check("ordinary-gate-off-does-not-claim-contact", baseRows.size() == 1 && !baseRows[0].value("contactConnected", true) && !baseRows[0].value("transportConnected", true));
    }
    if (!ordinaryOnly) {
        {
            auto c = scenario(root, "no-periodic-logging");
            SteamGameLink::publish(c, idle(), true);
            for (unsigned i = 0; i < 10000; ++i) {
                auto next = idle(); next["observationSequence"] = std::to_string(i); next["dueTick"] = i + 5000;
                SteamGameLink::publish(c, std::move(next), false);
            }
            check("10000-unchanged-publishes-emit-no-extra-rows", rows(L"native-lifecycle.log").size() == 1);
            check("unchanged-publish-still-refreshes-snapshot", c->snapshot["observationSequence"] == "9999");
            check("unchanged-publish-does-not-grow-history", c->history.size() == 1);
            auto changed = idle(); changed["phase"] = "degraded"; changed["reason"] = "transport-unavailable";
            SteamGameLink::publish(c, changed, true);
            const auto log = rows(L"native-lifecycle.log");
            check("actual-state-edge-is-preserved", log.size() == 2 && log.back()["phase"] == "degraded");
        }
        {
            auto c = scenario(root, "snapshot-history-bound");
            for (unsigned i = 0; i < 40; ++i) { auto next = idle(); next["reason"] = "fixture-edge-" + std::to_string(i); SteamGameLink::publish(c, next, true); }
            const auto log = rows(L"native-lifecycle.log");
            check("production-history-ring-stays-at-16", c->history.size() == 16 && c->snapshot["history"].size() == 16);
            check("logged-state-never-nests-snapshot-history", log.size() == 40 && std::all_of(log.begin(), log.end(), [](const json& row) { return !row.contains("history"); }));
        }
        {
            auto c = scenario(root, "privacy-and-diagnostics");
            const std::string secret = "FOREIGN_TARGET_SENTINEL";
            auto detail = idle();
            detail["originalGame"] = {{"pid", 1234}, {"created", "12345678901234567"}, {"windowsSession", 1}, {"path", secret}};
            detail["steamClient"] = {{"pid", 2345}, {"created", "23456789012345678"}, {"root", secret}, {"account", secret}};
            detail["binding"] = {{"candidates", json::array({secret})}};
            detail["sharedCandidate"] = {{"pid", secret}}; detail["osForeground"] = {{"pid", secret}};
            detail["steamUI"] = {{"focusables", json::array({secret})}, {"raw", std::string(2 * 1024 * 1024, 'x')}};
            detail["overlayPopupCacheDiagnostic"] = {{"available", true}, {"popupCount", 2}, {"pidCandidates", json::array({secret})}, {"expectedPidCandidateMatch", false}, {"steamContactVerified", false}};
            detail["overlayProviderCacheDiagnostic"] = {{"available", true}, {"originalPid", 1234}, {"fullGameIdExactString", "18446744069414584320"},
                {"targetMembership", "present-in-two-cache-reads"}, {"originalIdentityBracketMatched", true}, {"steamContactVerified", false}, {"foreignRows", json::array({secret})},
                {"targetFieldsTagged", {{"m_unPID", {{"rawType", "number"}, {"value", 1234}, {"extra", secret}}}, {"m_gameID", {{"rawType", "bigint"}, {"value", "18446744069414584320"}}}, {"foreign", secret}}}};
            detail["nativeAppLifetimeFeedback"] = {{"latestEventSequence", 7}, {"lastEvent", {{"otherPid", secret}}}};
            SteamGameLink::publish(c, detail, true);
            const auto log = rows(L"native-lifecycle.log"); check("privacy-fixture-actually-emitted", log.size() == 1);
            if (!log.empty()) {
                const auto& row = log[0];
                check("foreign-identities-paths-and-raw-ui-do-not-escape", row.dump().find(secret) == std::string::npos && !row.contains("binding") && !row.contains("steamUI"));
                check("only-original-process-identity-is-retained", row["originalGame"]["pid"] == 1234 && row["steamClient"]["pid"] == 2345 && !row["originalGame"].contains("path"));
                check("popup-count-and-match-retained-without-pid-inventory", row["overlayPopupCacheDiagnostic"]["popupCount"] == 2 && !row["overlayPopupCacheDiagnostic"].contains("pidCandidates"));
                check("provider-exact-gameid-and-target-tags-preserved", row["overlayProviderCacheDiagnostic"]["fullGameIdExactString"] == "18446744069414584320" && row["overlayProviderCacheDiagnostic"]["targetFieldsTagged"]["m_unPID"]["value"] == 1234);
                check("diagnostic-membership-never-forges-contact", !row.value("contactConnected", true) && !row["overlayProviderCacheDiagnostic"].value("steamContactVerified", true));
                check("lifetime-edge-sequence-retained", row["nativeAppLifetimeFeedback"]["latestEventSequence"] == 7);
                check("raw-ui-megabytes-do-not-enter-log", row.dump().size() < 8192 + 512);
            }
            check("projection-does-not-mutate-published-snapshot", c->snapshot["steamUI"] == detail["steamUI"] && c->snapshot["binding"] == detail["binding"] && c->snapshot["originalGame"]["path"] == secret);
        }
        {
            auto c = scenario(root, "same-gameid-candidate-privacy");
            auto detail = idle();
            json fields = {{"m_unPID", {{"rawType", "number"}, {"value", 202}}},
                {"m_nBrowserID", {{"rawType", "number"}, {"value", 7}}},
                {"m_eBrowserType", {{"rawType", "number"}, {"value", 1}}},
                {"m_unAppID", {{"rawType", "number"}, {"value", 4131894616ULL}}},
                {"m_gameID", {{"rawType", "bigint"}, {"value", "17746352246272032768"}}},
                {"m_eUIMode", {{"rawType", "number"}, {"value", 4}}}};
            json wire = {{"implementation", "steam-overlay-provider-cache-v1"}, {"readOnly", true},
                {"sourceMatched", true}, {"sourcePathQualified", true}, {"available", true},
                {"reason", "source-qualified-cache-read"}, {"mapEntryCount", 1},
                {"passesComplete", json::array({true, true})},
                {"rows", json::array({{{"entryKey", "17746352246272032768:202"}, {"fields", fields}}})},
                {"snapshotAtomic", false}, {"originalProviderObjectVerified", false},
                {"steamContactVerified", false}, {"actualInputVerified", false},
                {"actualContinueVerified", false}, {"recoveryAdmission", false}};
            detail["overlayProviderCacheDiagnostic"] = ymcc::steamoverlayprovidercache::summarize(
                {{"ok", true}, {"mutated", false}, {"overlayProviderCacheDiagnostic", wire}},
                4131894616ULL, 101, "17746352246272032768");
            detail["overlayProviderCacheDiagnostic"]["originalIdentityBracketMatched"] = true;
            // Inject a private extra only after the production sanitizer. The
            // real log-copy projection must still discard it.
            detail["overlayProviderCacheDiagnostic"]["sameGameIdCandidatesTagged"][0]["private"] = "CANDIDATE_PRIVATE_SENTINEL";
            SteamGameLink::publish(c, detail, true); const auto log = rows(L"native-lifecycle.log");
            check("same-gameid-candidate-gets-base-log", log.size() == 1 && log[0]["overlayProviderCacheDiagnostic"]["sameGameIdCandidatesTagged"][0]["m_unPID"]["value"] == 202);
            check("same-gameid-candidate-private-fields-not-exported", log.size() == 1 && log[0].dump().find("CANDIDATE_PRIVATE_SENTINEL") == std::string::npos);
            check("same-gameid-candidate-does-not-change-target-or-contact", log.size() == 1 && log[0]["overlayProviderCacheDiagnostic"]["targetMembership"] == "absent-in-two-cache-reads" && !log[0].value("contactConnected", true));
#ifdef YMCC_STEAM_LOG_PROJECTION
            detail["overlayProviderCacheDiagnostic"]["originalIdentityBracketMatched"] = false;
            check("same-gameid-candidate-bracket-rejection-removes-tags", !nativeSteamGameLinkLogDetail(detail)["overlayProviderCacheDiagnostic"].contains("sameGameIdCandidatesTagged"));
            detail["overlayProviderCacheDiagnostic"]["originalIdentityBracketMatched"] = true;
            detail["overlayProviderCacheDiagnostic"]["sameGameIdCandidatesTagged"] = json::array({fields, fields, fields});
            check("same-gameid-candidate-array-cap-enforced-at-log", !nativeSteamGameLinkLogDetail(detail)["overlayProviderCacheDiagnostic"].contains("sameGameIdCandidatesTagged"));
#endif
        }
        {
            auto c = scenario(root, "escaped-byte-budget");
            auto detail = idle(); const std::string escaped(256, '\x01');
            for (const auto* key : {"linkSerial", "configEpoch", "observationSequence", "observedAtTick", "dueTick", "readFailures", "appid", "trackerGameIdExactString", "originalPid", "originalCreated", "verifiedReadAvailable", "gameIdentitySame", "steamIdentitySame", "accountMatched", "trackerExact", "nativePrimaryExact", "ymccSinglePidAccepted", "nativeOverviewAssociated"}) detail[key] = escaped;
#ifdef YMCC_STEAM_LOG_PROJECTION
            check("production-projection-is-at-most-8192-serialized-bytes", nativeSteamGameLinkLogDetail(detail).dump().size() <= 8192);
#endif
            SteamGameLink::publish(c, detail, true); const auto log = rows(L"native-lifecycle.log");
            check("escaped-byte-budget-has-explicit-omission", log.size() == 1 && log[0].value("logDetailOmitted", "") == "steam-log-byte-budget");
            check("overflow-still-records-factual-status", log.size() == 1 && log[0]["phase"] == "idle" && !log[0].value("contactConnected", true));
            check("overflow-body-bounded-before-native-metadata", log.size() == 1 && log[0].dump().size() < 8192 + 512);
        }
        {
            auto c = scenario(root, "oversized-scalar"); auto detail = idle(); detail["reason"] = std::string(257, 'x');
            detail["overlayProviderCacheDiagnostic"] = {{"targetFieldsTagged", {{"m_gameID", {{"rawType", "string"}, {"value", std::string(257, 'x')}}}}}};
            SteamGameLink::publish(c, detail, true); const auto log = rows(L"native-lifecycle.log");
            check("oversized-scalars-are-omitted-without-truncating-values", log.size() == 1 && !log[0].contains("reason") && !log[0]["overlayProviderCacheDiagnostic"]["targetFieldsTagged"]["m_gameID"].contains("value"));
        }
        {
            auto c = scenario(root, "contact-only-snapshot-boundary");
            SteamGameLink::publish(c, idle(), true); auto detail = idle(); detail["contactConnected"] = true;
            SteamGameLink::publish(c, detail, false);
            check("contact-only-history-change-does-not-add-a-log-trigger", c->snapshot.value("contactConnected", false) && c->history.size() == 2 && rows(L"native-lifecycle.log").size() == 1);
        }
        {
            auto c = scenario(root, "gate-on-and-unrelated-events", true);
            SteamGameLink::publish(c, idle(), true); appendNativeLifecycleLog("steam-fixture-unknown", json::object());
            check("gate-on-steam-transition-still-base", rows(L"native-lifecycle.log").size() == 1);
            check("unknown-event-remains-detailed-when-enabled", rows(L"native-detail.log").size() == 1);
            g_domainLogEnabled[1].store(false); appendNativeLifecycleLog("steam-game-link-state-typo", json::object());
            check("typo-does-not-inherit-steam-whitelist", rows(L"native-lifecycle.log").size() == 1 && !nativeLifecycleEventIsBase("steam-game-link-state-typo"));
        }
    }
    g_nativeLifecycleLog.close(); g_nativeDetailLog.close();
    const bool ok = std::all_of(checks.begin(), checks.end(), [](const json& c) { return c["pass"].get<bool>(); });
    std::cout << json{{"ok", ok}, {"mode", ordinaryOnly ? "ordinary-only" : "full"}, {"checks", checks},
        {"scope", "extracted production publish -> classification/nativeLane -> actual JSONL writer; isolated fixture roots; no product process or Steam API"}}.dump(2) << '\n';
    return ok ? 0 : 1;
}
