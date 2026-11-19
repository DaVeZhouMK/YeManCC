#ifdef YMCC_LIBRARY_HOST_WORKER_CHAIN
#include "steam_library_worker_chain_cases.inc"
#else
#include "json.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;
static std::mutex g_steamOperationMutex;
static bool failWrite = false, steamPresent = true;
static unsigned closes = 0, writes = 0, restarts = 0;
static unsigned probes = 0, targets = 0, snapshots = 0, previews = 0;
static unsigned allCommits = 0, refreshes = 0, workers = 0, completionReads = 0;
static unsigned completionsReturned = 0, shutdownSubmissions = 0;
enum class CloseResult { confirmed, denied, uncertain };
static CloseResult closeResult = CloseResult::confirmed;
static json expectedTarget, expectedCompletion, staleSnapshot;
static std::string currentCase, currentCommand;
static std::vector<std::wstring> expectedWorkerArguments;

static void portCheck(bool ok, const char* message) {
    if (!ok) throw std::logic_error(message);
}
static bool jsonBoolOr(const json& value, const char* key, bool fallback = false) {
    const auto found = value.find(key);
    return found != value.end() && found->is_boolean() ? found->get<bool>() : fallback;
}
static std::string jsonStringOr(const json& value, const char* key, const std::string& fallback = {}) {
    if (!value.is_object()) return fallback;
    const auto found = value.find(key);
    return found != value.end() && found->is_string() ? found->get<std::string>() : fallback;
}
static void setSteamTarget(const json& target) {
    ++targets;
    portCheck(target == expectedTarget, "setSteamTarget lost invocation target");
}
static bool hostSteamRunning() {
    ++probes;
    return steamPresent;
}
static void closeSteamForShortcutCommit(const json& target) {
    if (!hostSteamRunning()) return;
    ++closes;
    portCheck(target == expectedTarget && steamPresent, "close lost target or closed absent Steam");
    if (closeResult == CloseResult::denied) throw std::runtime_error("fixture close admission denied");
    ++shutdownSubmissions;
    if (closeResult == CloseResult::uncertain) throw std::runtime_error("fixture close outcome uncertain");
    steamPresent = false;
}
static void written() {
    ++writes;
    portCheck(!steamPresent, "write reached while Steam remained present");
    if (failWrite) throw std::runtime_error("fixture transaction failed");
}
static json commitToAllSteamAccounts(const json& target) {
    ++allCommits;
    portCheck(target == expectedTarget, "all-account commit target changed");
    written();
    ++completionsReturned;
    return expectedCompletion;
}
static json refreshArtworkForAllSteamAccounts(const json& target) {
    ++refreshes;
    portCheck(target == expectedTarget, "artwork refresh target changed");
    written();
    ++completionsReturned;
    return expectedCompletion;
}
static fs::path workspacePlanPath() { return L"inert-no-file-plan.json"; }
static std::vector<std::wstring> steamTargetArguments(const json& target) {
    portCheck(target == expectedTarget, "worker target changed");
    // Inert values still prove the branch appends the target port's whole result.
    return {L"--account", L"fixture-account", L"--steam-root", L"inert-steam-root"};
}
struct ProcessResult {
    DWORD exitCode = ERROR_GEN_FAILURE;
    std::string output;
};
static ProcessResult runWorker(const std::vector<std::wstring>& arguments,
    std::atomic<bool>* cancelFlag = nullptr, HANDLE* activeProcessSlot = nullptr) {
    ++workers;
    portCheck(arguments == expectedWorkerArguments && cancelFlag == nullptr && activeProcessSlot == nullptr,
        "worker arguments or optional ports changed");
    written();
    return {0, "fixture-completion:" + currentCase};
}
static void requireSuccess(const ProcessResult& result) {
    portCheck(result.exitCode == 0, "unexpected worker failure");
}
static json readSteamAddCompletion(const ProcessResult& result, const fs::path& outputPath, const json& target) {
    ++completionReads;
    requireSuccess(result);
    portCheck(result.output == "fixture-completion:" + currentCase && outputPath == workspacePlanPath() && target == expectedTarget,
        "completion reader lost worker result, path, or target");
    ++completionsReturned;
    return expectedCompletion;
}
static void attachArtworkPreviews(json& completed) {
    ++previews;
    portCheck(completed == expectedCompletion, "previews received a stale or altered completion");
    completed["fixtureArtworkPreview"] = currentCase;
}
static json workspaceSnapshot(bool refreshPlan, const json& target) {
    ++snapshots;
    portCheck(refreshPlan == (currentCommand == "refreshSteamArtwork") && target == expectedTarget,
        "snapshot refresh flag or target changed");
    return staleSnapshot;
}
[[maybe_unused]] static void restartSteamBigPicture(const json&) { ++restarts; }
#include "production-library-no-restart.h"

struct Case {
    std::string name, command, account;
    bool failure = false;
    int legacy = -1;
    bool explicitOnly = false;
    bool present = true, force = true;
    CloseResult closing = CloseResult::confirmed;
    unsigned expectedWrites = 1, expectedCloses = 1;
    std::string expectedError;
};
static std::vector<Case> caseTable() {
    std::vector<Case> result;
    // The original command2/failure2/legacy3/account2 order and 24 defaults.
    for (const auto& command : {"commit", "refreshSteamArtwork"})
        for (bool failure : {false, true})
            for (int legacy : {-1, 0, 1})
                for (const auto& account : {"all-steam-accounts", "single"}) {
                    Case value;
                    value.command = command; value.account = account;
                    value.failure = failure; value.legacy = legacy;
                    value.name = value.command + "/failure=" + (failure ? "1" : "0") +
                        "/legacyRestart=" + std::to_string(legacy) + "/account=" + account;
                    if (failure) value.expectedError = "fixture transaction failed";
                    result.push_back(value);
                }
    Case denied;
    denied.name = "close-admission-denied-no-write"; denied.command = "commit";
    denied.account = "all-steam-accounts"; denied.explicitOnly = true;
    denied.closing = CloseResult::denied; denied.expectedWrites = 0;
    denied.expectedError = "fixture close admission denied";
    result.push_back(denied);
    Case noForce;
    noForce.name = "present-without-force-no-write"; noForce.command = "commit";
    noForce.account = "single"; noForce.explicitOnly = true; noForce.force = false;
    noForce.expectedWrites = 0; noForce.expectedCloses = 0;
    // Production's UTF-8 diagnostic is checked by the invariant below, not translated.
    result.push_back(noForce);
    Case offline;
    offline.name = "offline-no-close"; offline.command = "commit";
    offline.account = "single"; offline.explicitOnly = true; offline.present = false;
    offline.expectedCloses = 0;
    result.push_back(offline);
    Case uncertain;
    uncertain.name = "close-uncertain-no-write"; uncertain.command = "refreshSteamArtwork";
    uncertain.account = "all-steam-accounts"; uncertain.explicitOnly = true;
    uncertain.closing = CloseResult::uncertain; uncertain.expectedWrites = 0;
    uncertain.expectedError = "fixture close outcome uncertain";
    result.push_back(uncertain);
    return result;
}

static json runCase(const Case& value) {
    failWrite = value.failure; steamPresent = value.present; closeResult = value.closing;
    closes = writes = restarts = probes = targets = snapshots = previews = 0;
    allCommits = refreshes = workers = completionReads = completionsReturned = shutdownSubmissions = 0;
    currentCase = value.name; currentCommand = value.command;
    expectedTarget = {{"selectionMode", value.account}, {"forceCloseSteam", value.force},
        {"accountId", "fixture-account"}, {"steamRoot", "inert-steam-root"},
        {"selectedGameDirectories", json::array({"inert-game"})}, {"fixtureInvocation", value.name}};
    if (value.legacy >= 0) expectedTarget["restartSteamBigPicture"] = value.legacy == 1;
    expectedCompletion = {{"schemaVersion", 1}, {"committed", true},
        {"mode", value.command == "commit" ? (value.account == "single" ? "steam-library-add-commit" : "steam-library-add-commit-all-accounts") : "steam-artwork-refresh-all-accounts"},
        {"accountId", "fixture-account"}, {"fixtureInvocation", value.name},
        {"items", json::array({{{"name", "current-completion"}}})},
        {"accountReadWarning", "current-account-warning:" + value.name}};
    staleSnapshot = {{"ok", true}, {"steamPlan", {{"fixtureInvocation", "stale-snapshot"}}},
        {"steamPlanSource", "stale-source"}, {"steamPlanError", "stale-error"},
        {"steamAccountWriteWarning", "stale-warning"}, {"snapshotSentinel", value.name}};
    expectedWorkerArguments = {L"--commit-add-library-to-steam", L"--confirm-steam-closed",
        L"--account", L"fixture-account", L"--steam-root", L"inert-steam-root",
        L"--output", workspacePlanPath().wstring()};
    bool caught = false;
    std::string error;
    json actual;
    try { actual = productionLibraryCommand(value.command, expectedTarget); }
    catch (const std::runtime_error& failure) { caught = true; error = failure.what(); }
    const bool shouldThrow = value.failure || value.expectedWrites == 0;
    const bool success = !shouldThrow;
    const unsigned wrote = value.expectedWrites;
    const bool singleCommit = value.command == "commit" && value.account == "single";
    const unsigned expectedSubmissions = value.expectedCloses && value.closing != CloseResult::denied ? 1u : 0u;
    bool completionOk = false;
    if (success) {
        json expected = staleSnapshot;
        if (value.command == "commit") {
            auto completed = expectedCompletion;
            completed["fixtureArtworkPreview"] = value.name;
            expected["steamPlan"] = std::move(completed);
            expected["steamPlanSource"] = "verified-steam-add-completion";
            expected["steamPlanError"] = nullptr;
        } else expected["steamAccountWriteWarning"] = expectedCompletion["accountReadWarning"];
        completionOk = actual == expected;
    } else completionOk = actual.is_null() && snapshots == 0 && previews == 0 && completionsReturned == 0;
    const bool errorOk = value.name == "present-without-force-no-write" ?
        (caught && !error.empty()) : error == value.expectedError;
    const bool routesOk = allCommits == (wrote && value.command == "commit" && !singleCommit ? 1u : 0u) &&
        refreshes == (wrote && value.command == "refreshSteamArtwork" ? 1u : 0u) &&
        workers == (wrote && singleCommit ? 1u : 0u) && completionReads == (success && singleCommit ? 1u : 0u);
    const bool ok = restarts == 0 && writes == value.expectedWrites && closes == value.expectedCloses &&
        caught == shouldThrow && errorOk && completionOk && routesOk && probes == 1 && targets == 1 &&
        snapshots == (success ? 1u : 0u) && previews == (success && value.command == "commit" ? 1u : 0u) &&
        completionsReturned == (success ? 1u : 0u) && shutdownSubmissions == expectedSubmissions &&
        steamPresent == (value.present && (value.expectedCloses == 0 || value.closing != CloseResult::confirmed));
    std::cout << (ok ? "PASS " : "FAIL ") << value.command << " failure=" << value.failure
        << " legacyRestart=" << value.legacy << " account=" << value.account << " restartCalls=" << restarts
        << " case=" << value.name << '\n';
    return {{"name", value.name}, {"ok", ok}, {"explicitOnly", value.explicitOnly},
        {"command", value.command}, {"failure", value.failure}, {"legacyRestart", value.legacy}, {"account", value.account},
        {"writes", writes}, {"closes", closes}, {"restartCalls", restarts}, {"caught", caught}, {"error", error},
        {"completionValidated", completionOk}, {"routesValidated", routesOk},
        {"completionReads", completionReads}, {"completionsReturned", completionsReturned},
        {"snapshots", snapshots}, {"previews", previews}, {"presenceQueries", probes},
        {"shutdownSubmissions", shutdownSubmissions}, {"steamPresentAfter", steamPresent}};
}

#ifdef YMCC_LIBRARY_FIFO
#include "steam_library_fifo_cases.inc"
#endif
int main(int argc, char** argv) {
#ifdef YMCC_LIBRARY_FIFO
    if(argc>=3&&std::string(argv[2])=="--fifo-case")return LibraryFifoFixture::main(argc,argv);
#endif
    try {
        if ((argc != 2 && argc != 4) || std::string(argv[1]).empty() || std::string(argv[1]).starts_with('-')) {
            std::cerr << "usage: selftest out [--case exact-name]\n"; return 2;
        }
        const bool directed = argc == 4;
        if (directed && (std::string(argv[2]) != "--case" || std::string(argv[3]).empty())) {
            std::cerr << "invalid case selection\n"; return 2;
        }
        std::vector<Case> selected;
        for (const auto& value : caseTable()) {
            if (directed ? value.name == argv[3] : !value.explicitOnly) selected.push_back(value);
        }
        if (selected.size() != (directed ? 1u : 24u)) {
            std::cerr << "unknown or invalid exact case selection\n"; return 2;
        }
        const fs::path out = fs::absolute(argv[1]);
        if (fs::exists(out)) { std::cerr << "output already exists\n"; return 2; }
        fs::create_directories(out.parent_path());
        if (!fs::create_directory(out)) throw std::runtime_error("output already exists");
        unsigned checks = 0, failures = 0;
        json cases = json::array(), names = json::array(), executedNames = json::array();
        for (const auto& value : selected) names.push_back(value.name);
        for (const auto& value : selected) {
            auto result = runCase(value);
            ++checks;
            if (!result["ok"].get<bool>()) ++failures;
            executedNames.push_back(value.name);
            cases.push_back(std::move(result));
            if (failures) break; // Preserve the first actual failure; do not start later cases.
        }
        const json receipt{{"ok", failures == 0 && checks == selected.size()}, {"checks", checks}, {"failures", failures},
            {"mode", directed ? "directed" : "default"}, {"selectedCases", names}, {"executedCases", executedNames}, {"cases", cases},
            {"requestedCase", directed ? json(argv[3]) : json(nullptr)}, {"requestedCount", selected.size()},
            {"defaultMatrixCount", 24}, {"explicitOnlyCaseCount", 4}, {"realSteamOperations", 0},
            {"source", "Exact production commit/refresh command branches with memory-only file/process/completion ports"}};
        std::ofstream stream(out / "result.json", std::ios::binary | std::ios::out);
        if (!stream) throw std::runtime_error("cannot create result.json");
        stream << receipt.dump(2) << '\n';
        stream.close();
        if (!stream) throw std::runtime_error("cannot write result.json");
        std::cout << receipt.dump() << '\n';
        return receipt["ok"].get<bool>() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "fixture failed: " << error.what() << '\n'; return 1;
    }
}

#endif // YMCC_LIBRARY_HOST_WORKER_CHAIN
