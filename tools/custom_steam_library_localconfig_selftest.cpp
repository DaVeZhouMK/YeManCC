#include "custom_steam_library_test_runtime.h"

static void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void putText(const fs::path& path, const std::string& value) {
    writeAtomic(path, std::vector<unsigned char>(value.begin(), value.end()));
}

static json fixturePlan(const fs::path& directory, const SteamShortcutTarget& target, bool real) {
    const auto executable = directory / L"games" / L"Game.exe";
    const auto quotedExecutable = quotedSteamPath(executable);
    const auto quotedStart = quotedSteamPath(executable.parent_path());
    const uint32_t shortId = 0xC0000001u;
    int32_t storedId = 0;
    std::memcpy(&storedId, &shortId, sizeof(storedId));
    return {
        {"realSteamTarget", real},
        {"shortcutsBeforeSha256", sha256(fs::is_regular_file(target.path)
            ? readBinaryFile(target.path) : std::vector<unsigned char>{})},
        {"unmatchedSelections", json::array()},
        {"summary", {{"shortcutIdConflicts", 0}, {"readyToAdd", 1}}},
        {"items", json::array({{
            {"status", "ready-to-add"}, {"formalName", "Localconfig regression game"},
            {"storedAppId", storedId}, {"shortAppId", std::to_string(shortId)},
            {"longAppId", std::to_string((static_cast<uint64_t>(shortId) << 32) | 0x02000000ull)},
            {"quotedExecutable", quotedExecutable}, {"quotedStartDirectory", quotedStart}
        }})}
    };
}

static json runCommitCase(
    const fs::path& root, const std::wstring& name, const std::optional<std::string>& contents,
    const std::string& expectedStatus, bool existingShortcuts = false,
    bool lockConfig = false, bool directoryConfig = false, bool real = true, bool createConfigDirectory = true) {
    const auto directory = root / name;
    const auto dataRoot = directory / L"data";
    const SteamShortcutTarget target{directory / L"Steam" / L"userdata" / L"19627" / L"config" / L"shortcuts.vdf", "19627"};
    const auto localConfig = target.path.parent_path() / L"localconfig.vdf";
    if (createConfigDirectory) fs::create_directories(target.path.parent_path());
    if (real) putText(directory / L"Steam" / L"steam.exe", "fixture Steam marker");
    if (contents) putText(localConfig, *contents);
    if (directoryConfig) fs::create_directory(localConfig);
    if (existingShortcuts) {
        auto document = BinaryVdfDocument::emptySteamShortcuts();
        auto* shortcuts = vdfFindObject(document.root(), "shortcuts");
        shortcuts->objectValue.push_back({"0", steamShortcutNode(123, "Existing game", "\"Existing.exe\"", "\"Existing\"", 0, {"Existing tag"})});
        writeAtomic(target.path, document.serialize());
    }
    const auto originalShortcuts = fs::is_regular_file(target.path)
        ? readBinaryFile(target.path) : std::vector<unsigned char>{};
    HANDLE locked = INVALID_HANDLE_VALUE;
    if (lockConfig) {
        locked = CreateFileW(localConfig.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        expect(locked != INVALID_HANDLE_VALUE, "Unable to hold localconfig fixture lock");
    }
    json report;
    try {
        const auto plan = fixturePlan(directory, target, real);
        const auto planPath = dataRoot / L"state" / L"plan.json";
        report = commitPreparedSteamAddPlan(dataRoot, target, planPath, true, plan);
    } catch (...) {
        if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
        throw;
    }
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
    const bool updated = expectedStatus == "updated";
    expect(report.value("committed", false), "Shortcut import did not commit");
    expect(report.value("recentSignalStatus", std::string{}) == expectedStatus, "Unexpected optional recent update status");
    expect(report.value("recentSignalSkipped", false) == (real && !updated), "Incorrect skip status");
    expect(report.value("simulatedRecentSignal", false) == updated, "Incorrect simulated recent status");
    const auto shortcuts = readShortcuts(target.path, target.accountId);
    expect(shortcuts.size() == (existingShortcuts ? 2u : 1u), "Incorrect imported shortcut count");
    if (existingShortcuts) expect(shortcuts.front().appName == "Existing game", "Existing shortcut was changed");
    const auto backup = fs::path(toWide(report.at("backup").get<std::string>()));
    expect(fs::is_regular_file(backup), "Shortcut backup missing");
    const auto backupBytes = readBinaryFile(backup);
    expect(backupBytes == originalShortcuts, "Original shortcut backup lost");
    if (updated) {
        const auto bytes = readBinaryFile(localConfig);
        const std::string after(bytes.begin(), bytes.end());
        expect(contents && after != *contents, "Valid recent metadata was not updated");
        expect(after.find("KeepThisSetting") != std::string::npos, "Unrelated localconfig settings changed");
        expect(fs::is_regular_file(backup.parent_path() / L"localconfig.before.vdf"), "Localconfig backup missing");
        expect(readBinaryFile(backup.parent_path() / L"localconfig.before.vdf") == std::vector<unsigned char>(contents->begin(), contents->end()), "Localconfig backup differs from original");
    } else {
        expect(report.at("localConfigVdf").is_null(), "Skipped localconfig path leaked into committed record");
        expect(!fs::exists(backup.parent_path() / L"localconfig.before.vdf"), "Unexpected backup for skipped localconfig");
        if (contents) expect(readBinaryFile(localConfig) == std::vector<unsigned char>(contents->begin(), contents->end()), "Skipped localconfig was modified");
        else if (directoryConfig) expect(fs::is_directory(localConfig), "Localconfig directory was modified");
        else expect(!fs::exists(localConfig), "Missing localconfig was synthesized");
    }
    expect(fs::is_regular_file(backup.parent_path() / L"transaction.complete.json"), "Transaction completion marker missing");
    expect(!fs::exists(backup.parent_path() / L"transaction.pending.json"), "Pending transaction left after commit");
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        expect(entry.path().filename().wstring().find(L".yeman-stage-") == std::wstring::npos, "Staged file left after commit");
    }
    return {{"name", toUtf8(name)}, {"status", expectedStatus}, {"passed", true}};
}

static json runMustFailCase(const fs::path& root, const std::wstring& name, bool corruptShortcuts, bool confirmed, bool changedAfterPlanning, bool steamRunning = false) {
    const auto directory = root / name;
    const SteamShortcutTarget target{directory / L"Steam" / L"userdata" / L"19627" / L"config" / L"shortcuts.vdf", "19627"};
    fs::create_directories(target.path.parent_path());
    putText(directory / L"Steam" / L"steam.exe", "fixture Steam marker");
    if (corruptShortcuts) putText(target.path, "not a binary VDF");
    const auto plan = fixturePlan(directory, target, true);
    if (changedAfterPlanning) putText(target.path, "changed after planning");
    bool failed = false;
    selftestSteamRunning = steamRunning;
    try { commitPreparedSteamAddPlan(directory / L"data", target, directory / L"data" / L"state" / L"plan.json", confirmed, plan); }
    catch (const std::exception&) { failed = true; }
    selftestSteamRunning = false;
    expect(failed, "Required transaction safety failure was swallowed");
    expect(!fs::exists(target.path.parent_path() / L"localconfig.vdf"), "Required failure wrote localconfig");
    return {{"name", toUtf8(name)}, {"passed", true}};
}

int wmain(int argc, wchar_t** argv) {
    try {
        expect(argc == 2, "Pass a new fixture directory inside workspace Build");
        const auto root = fs::absolute(argv[1]);
        const auto workspaceBuild = fs::absolute(fs::path(__FILE__).parent_path() / L".." / L".." / L".." / L"Build").lexically_normal();
        expect(pathWithin(root, workspaceBuild), "Fixture root must be inside workspace Build");
        expect(!fs::exists(root), "Fixture directory must be new");
        fs::create_directories(root);
        const std::string valid = "\"localconfig\"\n{\n\t\"KeepThisSetting\"\t\t\"original\"\n\t\"RecentLocalPlayedGameIDs\"\t\t\"\"\n}\n";
        json cases = json::array();
        cases.push_back(runCommitCase(root, L"missing-first-import", std::nullopt, "localconfig-missing"));
        cases.push_back(runCommitCase(root, L"missing-entire-config-directory", std::nullopt, "localconfig-missing", false, false, false, true, false));
        cases.push_back(runCommitCase(root, L"missing-existing-shortcuts", std::nullopt, "localconfig-missing", true));
        cases.push_back(runCommitCase(root, L"valid-first-import", valid, "updated"));
        cases.push_back(runCommitCase(root, L"valid-existing-shortcuts", valid, "updated", true));
        cases.push_back(runCommitCase(root, L"valid-no-recent-field", "\"localconfig\"\n{\n\"KeepThisSetting\" \"original\"\n}\n", "updated"));
        cases.push_back(runCommitCase(root, L"empty-config", std::string{}, "localconfig-invalid"));
        cases.push_back(runCommitCase(root, L"malformed-config", "damaged config", "localconfig-invalid"));
        cases.push_back(runCommitCase(root, L"invalid-recent-record", "\"localconfig\" {\"RecentLocalPlayedGameIDs\" \"00\"}", "localconfig-invalid"));
        cases.push_back(runCommitCase(root, L"locked-config", valid, "localconfig-unreadable", false, true));
        cases.push_back(runCommitCase(root, L"config-is-directory", std::nullopt, "localconfig-not-a-file", false, false, true));
        cases.push_back(runCommitCase(root, L"simulated-target", valid, "not-requested", false, false, false, false));
        cases.push_back(runMustFailCase(root, L"corrupt-shortcuts", true, true, false));
        cases.push_back(runMustFailCase(root, L"steam-close-not-confirmed", false, false, false));
        cases.push_back(runMustFailCase(root, L"shortcuts-changed-after-planning", false, true, true));
        cases.push_back(runMustFailCase(root, L"steam-still-running", false, true, false, true));
        const json report{{"allPassed", true}, {"caseCount", cases.size()}, {"cases", cases}, {"fixtures", toUtf8(root.wstring())},
            {"realSteamFilesModified", false}, {"steamStoppedOrLaunched", false}};
        writeJsonAtomic(root / L"summary.json", report);
        std::cout << report.dump(2) << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "LOCALCONFIG_SELFTEST_FAILED: " << error.what() << "\n";
        return 1;
    }
}
