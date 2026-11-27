#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include "json.hpp"

namespace ymcc {
// UI guidance only: never authorizes a reinstall, changes admission or touches devices.
enum class HidHideSetupNotice : int {
    None=0, Incomplete=1, AdminRequired=2, CloseControllerApps=3,
    ExistingNeedsRepair=4, InstallerMissing=5, InstallerInvalid=6, RebootRequired=7, Unverified=8,
    ExistingDriverTooOld=9, UninstallFailed=10, UninstallResidue=11, UninstallUnverified=12,
    UpgradeInstallFailed=13, UninstallReboot=14, UpgradeInstallReboot=15, UpgradeInterrupted=16,
    UpgradeCloseApps=17, UpgradeAdminRequired=18
};
inline std::string setupRootIdentity(std::string root) {
    std::transform(root.begin(),root.end(),root.begin(),[](unsigned char c) {
        return c=='/' ? '\\' : static_cast<char>(std::tolower(c));
    });
    while (!root.empty() && root.back()=='\\') root.pop_back();
    return root;
}
inline HidHideSetupNotice hidHideCompletedSetupNotice(std::uint32_t code,
    const nlohmann::json& report, const std::string& root, std::uint32_t ownerPid) {
    using N=HidHideSetupNotice;
    const auto fallback=code==0 ? N::None : (code==3010 || code==1641) ? N::RebootRequired : N::Unverified;
    try {
        // A previous session's report must never turn this session's failure into success.
        if (!report.is_object() || report.value("batch",std::string{})!="GP-PREREQ-2" ||
            report.at("ownerPid").get<std::uint32_t>()!=ownerPid ||
            setupRootIdentity(report.at("installRoot").get<std::string>())!=setupRootIdentity(root) ||
            !report.at("finishedAt").is_string() || report.at("finishedAt").get<std::string>().empty() ||
            report.at("exitCode").get<std::uint32_t>()!=code) return fallback;
        const auto hh=report.value("hidhide",std::string{});
        // Generic preparation/launch/report errors are not evidence of a HidHide failure.
        // Even installer codes require a current, matching report to identify the dependency.
        const auto status=report.value("status",std::string{});
        // HidMaestro failure alone is not a HidHide failure.
        if (hh=="READY") return N::None;
        if (hh=="REBOOT_REQUIRED") {
            const auto row=report.value("hidhideTransactionAfter",report.value("hidhideTransactionBefore",nlohmann::json{}));
            const auto phase=row.is_object() ? row.value("Phase",std::string{}) : std::string{};
            if (phase=="reboot-after-uninstall") return N::UninstallReboot;
            if (phase=="reboot-after-install") return N::UpgradeInstallReboot;
            return N::RebootRequired;
        }
        if (hh=="ADMIN_REQUIRED") return N::AdminRequired;
        if (hh=="CLOSE_CONTROLLER_APPS_FIRST") return N::CloseControllerApps;
        if (hh=="PRESENT_NOT_READY_NO_REINSTALL") return N::ExistingNeedsRepair;
        if (hh=="DRIVER_TOO_OLD_NO_REINSTALL") return N::ExistingDriverTooOld;
        const bool installing=hh=="INSTALL_FRESH" || hh=="UNINSTALL_LEGACY" || hh=="INSTALL_AFTER_UNINSTALL" || hh=="RETIRE_INACTIVE_PACKAGE";
        if (installing && code==12 && status=="HIDHIDE_INSTALLER_MISSING") return N::InstallerMissing;
        if (installing && code==13 && status=="HIDHIDE_INSTALLER_HASH_MISMATCH") return N::InstallerInvalid;
        if (hh=="UNINSTALL_FAILED") return N::UninstallFailed;
        if (hh=="UNINSTALL_RESIDUE") return N::UninstallResidue;
        if (hh=="UNINSTALL_IDENTITY_UNVERIFIED") return N::UninstallUnverified;
        if (hh=="UPGRADE_INSTALL_FAILED") return N::UpgradeInstallFailed;
        if (hh=="UPGRADE_INTERRUPTED" || hh=="UPGRADE_BOOT_UNVERIFIED" || hh=="UPGRADE_DEFERRED") return N::UpgradeInterrupted;
        if (hh=="UPGRADE_CLOSE_APPS") return N::UpgradeCloseApps;
        if (hh=="UPGRADE_ADMIN_REQUIRED") return N::UpgradeAdminRequired;
        if (hh=="MISSING" || hh=="INSTALL_POSTCONDITION_UNPROVEN") return N::Incomplete;
        return fallback;
    } catch (...) { return fallback; }
}
inline int hidHideMaintenanceExit(const nlohmann::json& report, HidHideSetupNotice notice) {
    using N=HidHideSetupNotice;
    // The caller first validates the current child's report through the notice.
    if (notice!=N::UninstallFailed && notice!=N::UninstallResidue && notice!=N::UpgradeInstallFailed) return -1;
    const auto code=report.find("hidhideMaintenanceExit");
    if (code==report.end() || !code->is_number_integer()) return -1;
    const auto value=code->get<std::int64_t>();
    return value>0 && value<=INT32_MAX && value!=3010 && value!=1641 ? static_cast<int>(value) : -1;
}
inline bool gamepadMaintenanceDeferredReport(std::uint32_t code, const nlohmann::json& report,
    const std::string& root, std::uint32_t ownerPid) {
    try {
        return code==19 && hidHideCompletedSetupNotice(code,report,root,ownerPid)==HidHideSetupNotice::None &&
            report.at("prepare").get<bool>() && report.at("hidhide").get<std::string>()=="READY" &&
            report.at("status").get<std::string>()=="DRIVER_MAINTENANCE_DEFERRED" &&
            report.at("hidmaestro").get<std::string>()=="DRIVER_MAINTENANCE_DEFERRED";
    } catch (...) { return false; }
}
}
