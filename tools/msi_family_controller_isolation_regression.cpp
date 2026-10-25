// Offline full-production-TU regression. Never enters product wWinMain,
// opens a window, accesses a HID/driver, or starts Steam/InputHost.
#include <iostream>
#include "../native/main.cpp"
#include "msi_virtual_wake_regression.h"
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    g_yemanccDataDir = argv[1];
    fspath::create_directories(g_yemanccDataDir);
    const bool wake = msiVirtualWakeNativeRegression(argv[2]);
    unsigned int cases = 0, failures = 0; std::ostringstream report;
    auto check = [&](const char* name, bool ok) {
        ++cases; if (!ok) ++failures;
        report << (ok ? "PASS " : "FAIL ") << name << "\n";
    };
    g_machineIdentity.family = YmccFamily::MsiClaw;
    g_machineIdentity.manufacturer = "MICRO-STAR INTERNATIONAL CO., LTD.";
    g_machineIdentity.product = "MS-1T91"; g_machineIdentity.resolved = true;
    for (const std::string persona : {"steamdeck", "dualshock4", "elite", "dualsense-edge"}) {
        check("authorized G3E current bound target is isolated", msiUiBoundTargetAdmitted(persona));
        g_machineIdentity.manufacturer = "ASUSTeK COMPUTER INC.";
        check("MSI board text with non-MSI manufacturer never gains isolation", !msiUiBoundTargetAdmitted(persona));
        g_machineIdentity.manufacturer = "MICRO-STAR INTERNATIONAL CO., LTD.";
    }
    check("disabled/empty/unknown binding has no MSI isolation", !msiUiBoundTargetAdmitted("disabled") && !msiUiBoundTargetAdmitted("") && !msiUiBoundTargetAdmitted("unknown"));
    for (const auto family : {YmccFamily::AsusRogAlly, YmccFamily::AsusRogAllyX, YmccFamily::AsusRogAllyClassic, YmccFamily::AsusRogAllyXClassic, YmccFamily::LenovoLegionGo, YmccFamily::ValveSteamDeck, YmccFamily::Gpd, YmccFamily::Ayanero, YmccFamily::OneXPlayer, YmccFamily::ZotacGamingZone, YmccFamily::Ayn, YmccFamily::Aokzoe, YmccFamily::Minisforum, YmccFamily::SuiPlay, YmccFamily::Unknown}) {
        g_machineIdentity.family = family;
        check("other native families cannot acquire MSI-only policy", !msiUiBoundTargetAdmitted("elite"));
        check("other native families retain generic wake ceiling", inputHostWakeMaxAgeMs() == kWakeRescanMaxAgeMs);
    }
    g_machineIdentity.family = YmccFamily::MsiClaw; g_machineIdentity.product = "UNKNOWN-MSI-BOARD";
    check("unregistered MSI model remains unmodified", !msiUiBoundTargetAdmitted("elite"));
    g_machineIdentity.product = "MS-1T91";
    g_msiDinputRouteDinputArmed = false; g_inputHostFreshNeutralAdmitted = false; g_inputHostFreshNeutralFrames = 0;
    check("MSI disarmed/fallback route can still clear OLD output with neutral only", inputHostPhysicalPublicationSourceAdmitted(false, true));
    check("source absence still refuses ALL live data after fallback", !inputHostPhysicalPublicationSourceAdmitted(false, false));
    check("virtual neutral cleanup cannot manufacture physical neutral evidence", !g_inputHostFreshNeutralAdmitted && g_inputHostFreshNeutralFrames == 0);
    g_machineIdentity.family = YmccFamily::Unknown;
    check("other-family source absence preserves original no-write policy", !inputHostPhysicalPublicationSourceAdmitted(false, true));
    g_machineIdentity.family = YmccFamily::MsiClaw;
    g_inputHostBoundPersona = "elite";
    auto plan = focusIsolationPlanTick(true);
    check("actual plan snapshots bound MSI Elite while preserving Host-unavailable skip", plan.msiTarget && !plan.steamDeckTarget && !plan.ps5Target && plan.decision == FocusDecision::SkipHostUnavailable);
    g_realStickTest.virtualTargetPersona = "steamdeck"; g_inputHostBoundPersona.clear();
    plan = focusIsolationPlanTick(true);
    check("actual requested-only persona cannot become bound MSI authority", !plan.msiTarget && !plan.steamDeckTarget);
    g_inputHostBoundPersona = "elite"; g_machineIdentity.family = YmccFamily::Unknown;
    plan = focusIsolationPlanTick(true);
    check("actual unknown family plan keeps MSI field false", !plan.msiTarget);
    report << "cases=" << cases << " failed=" << failures << "\n";
    std::ofstream out(fspath::path(g_yemanccDataDir) / L"msi-isolation-bound-native-entry.txt");
    out << report.str(); out.close();
    json summary{{"wakePassed", wake}, {"isolationBoundCases", cases}, {"isolationBoundFailures", failures},
        {"actualProductionTuExecuted", true}, {"realHardwareAccess", false}, {"realSteamOperations", 0}};
    std::cout << summary.dump(2) << "\n";
    return wake && failures == 0 && out.good() ? 0 : 3;
}
