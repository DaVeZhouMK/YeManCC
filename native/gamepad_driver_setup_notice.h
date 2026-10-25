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
    ExistingNeedsRepair=4, InstallerMissing=5, InstallerInvalid=6, RebootRequired=7, Unverified=8
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
        if (hh=="REBOOT_REQUIRED") return N::RebootRequired;
        if (hh=="ADMIN_REQUIRED") return N::AdminRequired;
        if (hh=="CLOSE_CONTROLLER_APPS_FIRST") return N::CloseControllerApps;
        if (hh=="PRESENT_NOT_READY_NO_REINSTALL") return N::ExistingNeedsRepair;
        if (hh=="INSTALL_FRESH" && code==12 && status=="HIDHIDE_INSTALLER_MISSING") return N::InstallerMissing;
        if (hh=="INSTALL_FRESH" && code==13 && status=="HIDHIDE_INSTALLER_HASH_MISMATCH") return N::InstallerInvalid;
        if (hh=="MISSING" || hh=="INSTALL_POSTCONDITION_UNPROVEN") return N::Incomplete;
        return fallback;
    } catch (...) { return fallback; }
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
