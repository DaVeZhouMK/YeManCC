#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "sleep_wake_password.h"
#include <objbase.h>
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#pragma comment(lib, "ole32.lib")

using namespace ymcc::sleep_wake_password;
using json = nlohmann::json;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static std::string guidText(const GUID& guid) {
    wchar_t text[40]{}; check(StringFromGUID2(guid, text, 40) > 0, "Format GUID");
    std::string result; for (const wchar_t c : std::wstring(text)) result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return result;
}
struct FakePowerApi {
    DWORD ac = 0, dc = 0;
    bool yemanActive = true;
    int reads = 0, writes = 0, queries = 0, activations = 0;
    int failReadAt = 0, failWriteAt = 0, failActivateAt = 0;
    bool ignoreEnabling = false;
    std::vector<std::pair<bool, DWORD>> written;
    DWORD read(bool plugged, DWORD& value) {
        if (++reads == failReadAt) return ERROR_FILE_NOT_FOUND;
        value = plugged ? ac : dc; return ERROR_SUCCESS;
    }
    DWORD write(bool plugged, DWORD value) {
        written.emplace_back(plugged, value);
        if (++writes == failWriteAt) return ERROR_ACCESS_DENIED;
        if (ignoreEnabling && value == 1) return ERROR_SUCCESS;
        if (plugged) ac = value; else dc = value;
        return ERROR_SUCCESS;
    }
    DWORD activeIsYeman(bool& active) { ++queries; active = yemanActive; return ERROR_SUCCESS; }
    DWORD reactivateYeman() {
        check(yemanActive, "Never activate YMCC while another scheme is active");
        if (++activations == failActivateAt) return ERROR_ACCESS_DENIED;
        return ERROR_SUCCESS;
    }
};
int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 2, "Output directory required");
        const std::filesystem::path out(argv[1]); std::filesystem::create_directories(out);
        std::vector<std::string> cases;
        check(guidText(Scheme) == "{1cb8b882-a900-4b9f-9bac-99d151e64441}", "Fixed YMCC scheme ID");
        check(guidText(Subgroup) == "{fea3413e-7e05-4911-9a71-700331f1c294}", "Subgroup ID");
        check(guidText(Setting) == "{0e796bdb-100d-47d6-a2d5-f7d2daa51f51}", "Wake-password setting ID");
        cases.push_back("Three GUIDs exactly match user-specified IDs; Windows adapter has no caller-selectable scheme");
        {
            FakePowerApi api;
            check(api.reads == 0 && api.writes == 0 && !Snapshot{}.enabled(), "No constructor/default IO");
            cases.push_back("Default state is off and constructing the domain performs zero system reads or writes");
            const auto result = apply(api, true);
            check(result.ok && result.state.ac == 1 && result.state.dc == 1 && result.reactivated && api.activations == 1, "Enable AC/DC=1");
            check(api.written == std::vector<std::pair<bool, DWORD>>{{true, 1}, {false, 1}}, "Exactly AC and DC written");
            cases.push_back("User enable writes AC=DC=1, reads both back and reapplies only an already-active YMCC scheme");
            const auto disabled = apply(api, false);
            check(disabled.ok && !disabled.state.enabled() && api.ac == 0 && api.dc == 0, "Disable AC/DC=0");
            cases.push_back("User disable writes AC=DC=0 (no password)");
        }
        {
            FakePowerApi api; api.yemanActive = false;
            const auto enabled = apply(api, true);
            check(enabled.ok && !enabled.reactivated && api.activations == 0 && !api.yemanActive, "Preserve Balanced active plan");
            const auto disabled = apply(api, false);
            check(disabled.ok && api.activations == 0 && api.ac == 0 && api.dc == 0, "Inactive YMCC edit");
            cases.push_back("When Balanced/another plan is active, only YMCC saved indices change; no plan activation occurs");
        }
        {
            FakePowerApi api; const auto result = apply(api, false);
            check(result.ok && !result.changed && api.writes == 0 && api.queries == 0 && api.activations == 0, "No unnecessary apply");
            cases.push_back("An explicit user operation already matching AC/DC avoids all writes and activation");
        }
        {
            FakePowerApi api; api.dc = 1;
            check(read(api).mixed() && read(api).enabled(), "Detect mixed state");
            check(apply(api, false).ok && api.ac == 0 && api.dc == 0, "Unify mixed AC/DC");
            cases.push_back("Previously mixed AC/DC indices become identical on user toggle");
        }
        {
            FakePowerApi api; api.failReadAt = 1;
            const auto result = apply(api, true);
            check(!result.ok && result.error == ERROR_FILE_NOT_FOUND && api.writes == 0 && api.activations == 0, "Missing plan no writes");
            cases.push_back("Missing/unreadable YMCC plan fails without creating a plan or falling back to Balanced");
        }
        {
            FakePowerApi api; api.failWriteAt = 2;
            const auto result = apply(api, true);
            check(!result.ok && std::string(result.stage) == "write-dc" && result.rollbackOk && api.ac == 0 && api.dc == 0, "Partial write rollback");
            cases.push_back("DC write failure restores original AC/DC; no half-enabled success ACK");
        }
        {
            FakePowerApi api; api.failWriteAt = 1;
            const auto result = apply(api, true);
            check(!result.ok && result.error == ERROR_ACCESS_DENIED && result.rollbackOk && api.ac == 0 && api.dc == 0, "Access denied rollback");
            cases.push_back("Access/policy write rejection reports failure and preserves original values");
        }
        {
            FakePowerApi api; api.ignoreEnabling = true;
            const auto result = apply(api, true);
            check(!result.ok && std::string(result.stage) == "readback" && result.rollbackOk && api.ac == 0 && api.dc == 0, "Silent policy readback failure");
            cases.push_back("Silent no-op writes are detected by readback, rather than claiming the setting took effect");
        }
        {
            FakePowerApi api; api.failActivateAt = 1;
            const auto result = apply(api, true);
            check(!result.ok && std::string(result.stage) == "apply" && result.rollbackOk && api.ac == 0 && api.dc == 0 && api.activations == 2, "Activation rollback");
            cases.push_back("YMCC reactivation failure restores/reapplies original indices");
        }
        {
            FakePowerApi api; api.failReadAt = 5;
            const auto result = apply(api, true);
            check(!result.ok && std::string(result.stage) == "final-readback" && result.rollbackOk && api.ac == 0 && api.dc == 0, "Final readback failure");
            cases.push_back("Final readback failure also rolls back, so UI never reports unverified success");
        }
        {
            FakePowerApi api; api.ac = 2;
            check(!apply(api, true).ok && api.writes == 0 && api.activations == 0, "Invalid index rejection");
            cases.push_back("Invalid/unsupported indices are rejected without writes");
        }
        // The only live-system action in this selftest is reading the fixed plan.
        // There are deliberately no calls to WindowsPowerApi::write/reactivate.
        WindowsPowerApi system;
        const auto live = read(system);
        cases.push_back("Actual Windows fixed-plan AC/DC snapshot is read-only; no installed power values are changed");
        const json report = {{"suite", "Sleep wake password native"}, {"passed", cases.size()}, {"cases", cases},
            {"liveReadOnly", {{"known", live.known}, {"ac", live.ac}, {"dc", live.dc}, {"win32Error", live.error}}},
            {"scope", "Production fixed-GUID transaction with fake writes; actual YMCC scheme read-only; no Balanced/installed settings writes"}};
        std::ofstream(out / "native-results.json") << report.dump(2);
        std::cout << report.dump(2) << '\n'; return 0;
    } catch (const std::exception& e) {
        std::cerr << "Sleep wake password selftest failed: " << e.what() << '\n'; return 1;
    }
}
