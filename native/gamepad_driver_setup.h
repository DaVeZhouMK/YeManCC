#pragma once
#include "process_efficiency.h"
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <memory>
#include <functional>
#include "gamepad_driver_setup_notice.h"

// GP-PREREQ-2: one child per application session, outside the input/driver lifecycle.
// No device operations, background thread, restart, repair loop or installer termination here.
namespace ymcc {
class GamepadDriverSetup {
public:
    enum State { NotStarted=0, Pending=1, Ready=2, Failed=3, RebootRequired=4, Slow=5, InstallerMissing=6, InstallerInvalid=7, InventoryUnverified=8, MaintenanceDeferred=9 };
    static constexpr State ResultState(DWORD code) {
        return code==0 ? Ready :
            (code==3010 || code==1641) ? RebootRequired :
            code==12 ? InstallerMissing :
            code==13 ? InstallerInvalid :
            code==18 ? InventoryUnverified : code==19 ? MaintenanceDeferred : Failed;
    }
    // Prefer the package next to this application over an inherited test-resource override.
    // This is bounded file metadata lookup at startup, never a driver/device probe.
    static std::wstring ResolveRoot(const std::wstring& appDirectory, const std::wstring& powerControlDirectory) {
        const auto directory=[](const std::wstring& text) {
            auto p=std::filesystem::path(text).lexically_normal();
            while (p.has_relative_path() && p.filename().empty()) p=p.parent_path();
            return p;
        };
        const auto app=directory(appDirectory);
        const auto configured=directory(powerControlDirectory).parent_path();
        for (const auto& root:std::vector<std::filesystem::path>{app.parent_path(),app,configured}) {
            if (root.empty()) continue;
            std::error_code ec;
            if (std::filesystem::is_regular_file(root/L"PowerControl"/L"redist"/L"HIDMaestroSetup.exe",ec) && !ec)
                return root.wstring();
        }
        // Preserve the configured diagnostic path and its launch error when no package exists.
        return configured.wstring();
    }
    // Environment preparation is advisory; target admission belongs to the actual backend.
    static constexpr bool HasWarning(State s) { return s!=NotStarted && s!=Ready; }
    ~GamepadDriverSetup() { if (process_) CloseHandle(process_); }
    State state() const { return state_.load(std::memory_order_acquire); }
    DWORD result() const { return result_.load(std::memory_order_acquire); }
    bool warnsTargets() const { return HasWarning(state()); }
    HidHideSetupNotice hidHideNotice() const { return notice_.load(std::memory_order_acquire); }
    const std::wstring& redistDirectory() const { return redistDirectory_; }
    const std::string& sessionToken() const { return sessionToken_; }
    bool active() const { const auto s=state(); return s==Pending || s==Slow; }
    nlohmann::json usbipUpgradeSnapshot() const {
        const auto snapshot=std::atomic_load_explicit(&usbipPublished_,std::memory_order_acquire);
        return snapshot ? *snapshot : nlohmann::json{};
    }
    nlohmann::json usbipMaintenanceSnapshot() const {
        const auto snapshot=std::atomic_load_explicit(&usbipMaintenancePublished_,std::memory_order_acquire);
        return snapshot ? *snapshot : nlohmann::json{};
    }
    // Called only from the IPC IO pool on explicit page/foreground demand.
    // Cached snapshots remain lock-free while this bounded read-only child runs.
    bool refreshUsbipObservation(const std::function<nlohmann::json(const std::wstring&)>& read) {
        if (state()==NotStarted || active()) return false;
        std::unique_lock<std::mutex> observation(observationMutex_,std::try_to_lock);
        if (!observation.owns_lock()) return false;
        const auto now=GetTickCount64();
        if (observationStarted_ && now-lastObservationAt_<1000) return false;
        observationStarted_=true;lastObservationAt_=now;
        std::wstring root;std::string session;
        { std::lock_guard<std::mutex> guard(mutex_);if(active())return false;root=root_;session=sessionToken_; }
        const std::wstring command=quote(root+L"\\PowerControl\\redist\\HIDMaestroSetup.exe")+
            L" --usbip-observe --install-root "+quote(root)+L" --owner-pid "+std::to_wstring(GetCurrentProcessId())+
            L" --setup-session "+quote(std::wstring(session.begin(),session.end()));
        try {
            const auto doc=read(command);
            const auto rootBytes=std::filesystem::path(root).u8string();
            if (!doc.is_object() || doc.value("batch",std::string{})!="GP-USBIP-OBSERVE-1" ||
                doc.at("ownerPid").get<DWORD>()!=GetCurrentProcessId() || doc.value("session",std::string{})!=session ||
                setupRootIdentity(doc.at("installRoot").get<std::string>())!=setupRootIdentity(std::string(rootBytes.begin(),rootBytes.end()))) return false;
            auto next=doc.at("usbipUpgrade");
            if (!next.is_object() || next.value("schemaVersion",0)!=1 || !next.at("phase").is_string() ||
                !next.at("installedVersion").is_string() || !next.at("targetVersion").is_string() || !next.at("detail").is_string()) return false;
            const auto phase=next.at("phase").get<std::string>();
            if (phase!="current" && phase!="newer-preserved" && phase!="not-installed" && phase!="update-found" && phase!="reboot-required" && phase!="unverified") return false;
            if (next.at("installedVersion").get_ref<const std::string&>().size()>128 || next.at("targetVersion").get_ref<const std::string&>().size()>128 || next.at("detail").get_ref<const std::string&>().size()>4096) return false;
            if ((phase=="current" || phase=="newer-preserved") && (next.at("installedVersion")=="" || next.at("targetVersion")=="")) return false;
            std::lock_guard<std::mutex> guard(mutex_);
            if (active() || sessionToken_!=session) return false;
            // A page read cannot clear a required reboot within this app session.
            if (usbipUpgrade_.value("phase",std::string{})=="reboot-required" && phase!="reboot-required") return false;
            next["observationOnly"]=true;
            next["sequence"]=usbipUpgrade_.value("sequence",0u);
            if (next==usbipUpgrade_) return false;
            // Keep the startup attempt intact as history, separate from current inventory.
            if (!usbipUpgrade_.value("observationOnly",false)) {
                auto previous=std::make_shared<const nlohmann::json>(usbipUpgrade_);
                std::atomic_store_explicit(&usbipMaintenancePublished_,std::move(previous),std::memory_order_release);
            }
            next["sequence"]=usbipUpgrade_.value("sequence",0u)+1u;
            usbipUpgrade_=std::move(next);publishUsbipLocked();return true;
        } catch (...) { return false; }
    }
    void start(const std::wstring& root, const std::wstring& report, const std::wstring& updateCommitState = L"") {
        std::lock_guard<std::mutex> guard(mutex_);
        if (state()!=NotStarted) return;
        report_=report; root_=root; redistDirectory_=root+L"\\PowerControl\\redist";
        // PID values can be reused by Windows. An unchanged previous report is never current evidence.
        reportPreviouslyPresent_=GetFileAttributesExW(report_.c_str(),GetFileExInfoStandard,&previousReportInfo_)!=FALSE;
        const DWORD reportError=reportPreviouslyPresent_ ? ERROR_SUCCESS : GetLastError();
        reportPreviouslyKnown_=reportPreviouslyPresent_ || reportError==ERROR_FILE_NOT_FOUND || reportError==ERROR_PATH_NOT_FOUND;
        started_=GetTickCount64();
        sessionToken_=std::to_string(GetCurrentProcessId())+":"+std::to_string(started_);
        usbipUpgrade_={{"schemaVersion",1},{"phase","checking"},{"installedVersion",""},
            {"targetVersion",""},{"detail","正在核对 HIDM 配套的 USB/IP。"},{"sequence",0}};
        publishUsbipLocked();
        const std::wstring executable=redistDirectory_+L"\\HIDMaestroSetup.exe";
        std::wstring command=quote(executable)+L" --prepare --owner-pid "+std::to_wstring(GetCurrentProcessId())+
            L" --install-root "+quote(root)+L" --report "+quote(report)+
            L" --setup-session "+quote(std::wstring(sessionToken_.begin(),sessionToken_.end()));
        if (!updateCommitState.empty()) command += L" --wait-update-commit " + quote(updateCommitState);
        std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(L'\0');
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (!ymcc::processEfficiency::createProcess(executable.c_str(),buffer.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                nullptr,(root+L"\\PowerControl\\redist").c_str(),&si,&pi)) {
            result_.store(GetLastError(),std::memory_order_release);
            usbipUpgrade_={{"schemaVersion",1},{"phase","failed"},{"installedVersion",""},{"targetVersion",""},
                {"detail","USB/IP 准备程序未启动。请检查随包文件和管理员权限。"},{"sequence",1}};
            publishUsbipLocked();
            notice_.store(HidHideSetupNotice::Unverified,std::memory_order_release);
            state_.store(Failed,std::memory_order_release);return;
        }
        CloseHandle(pi.hThread);process_=pi.hProcess;state_.store(Pending,std::memory_order_release);
    }
    bool poll() {
        if (!active()) return false;
        std::lock_guard<std::mutex> guard(mutex_);
        if (!process_) return false;
        const bool usbipChanged=refreshUsbipUpgradeLocked();
        DWORD wait=WaitForSingleObject(process_,0);
        if (wait==WAIT_OBJECT_0 || wait==WAIT_FAILED) {
            // The child can publish its final stage after the first read but
            // before exit is observed. Exit is the ordering barrier for this
            // final read; never permanently downgrade that completed stage.
            if (wait==WAIT_OBJECT_0) refreshUsbipUpgradeLocked();
            DWORD code=ERROR_PROCESS_ABORTED;
            if (wait==WAIT_OBJECT_0 && !GetExitCodeProcess(process_,&code)) code=GetLastError();
            if (wait==WAIT_FAILED) code=GetLastError();
            CloseHandle(process_);process_=nullptr;
            const auto usbipPhase=usbipUpgrade_.value("phase",std::string{});
            if (usbipPhase=="checking" || usbipPhase=="update-found" || usbipPhase=="removing" ||
                usbipPhase=="installing" || usbipPhase=="verifying") {
                usbipUpgrade_["phase"]="unverified";
                usbipUpgrade_["detail"]="USB/IP 准备未取得最终结果。请导出日志；原有手柄模式未被自动重试。";
                usbipUpgrade_["sequence"]=usbipUpgrade_.value("sequence",0u)+1u;
                publishUsbipLocked();
            }
            const auto report=wait==WAIT_OBJECT_0 ? completedReport() : nlohmann::json{};
            const auto rootUtf8=std::filesystem::path(root_).u8string();
            const std::string root(rootUtf8.begin(),rootUtf8.end());
            notice_.store(wait==WAIT_OBJECT_0 ? hidHideCompletedSetupNotice(code,report,root,GetCurrentProcessId()) :
                HidHideSetupNotice::Unverified,std::memory_order_release);
            auto completed=ResultState(code);
            if (completed==MaintenanceDeferred && !gamepadMaintenanceDeferredReport(code,report,root,GetCurrentProcessId()))
                completed=Failed;
            result_.store(code,std::memory_order_release);state_.store(completed,std::memory_order_release);return true;
        }
        if (state()==Pending && GetTickCount64()-started_>=180000) {
            // Report the slow preparation and keep observing this SAME child; target attempts remain allowed.
            // An MSI transaction is never killed and another installer is never launched.
            state_.store(Slow,std::memory_order_release);return true;
        }
        return usbipChanged;
    }
private:
    void publishUsbipLocked() {
        auto snapshot=std::make_shared<const nlohmann::json>(usbipUpgrade_);
        std::atomic_store_explicit(&usbipPublished_,std::move(snapshot),std::memory_order_release);
    }
    bool refreshUsbipUpgradeLocked() {
        try {
            std::ifstream input(std::filesystem::path(report_+L".usbip.json"),std::ios::binary|std::ios::ate);
            const auto length=input.tellg();
            if (!input || length<=0 || length>8192) return false;
            std::string bytes(static_cast<size_t>(length),'\0');input.seekg(0);
            if (!input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()))) return false;
            const auto doc=nlohmann::json::parse(bytes,nullptr,false);
            const auto rootBytes=std::filesystem::path(root_).u8string();
            const std::string root(rootBytes.begin(),rootBytes.end());
            if (!doc.is_object() || doc.value("batch",std::string{})!="GP-USBIP-UPGRADE-1" ||
                doc.at("ownerPid").get<DWORD>()!=GetCurrentProcessId() ||
                doc.value("session",std::string{})!=sessionToken_ ||
                setupRootIdentity(doc.at("installRoot").get<std::string>())!=setupRootIdentity(root)) return false;
            const auto& value=doc.at("usbipUpgrade");
            if (value.at("schemaVersion").get<int>()!=1 || !value.at("phase").is_string() ||
                !value.at("detail").is_string() || !value.at("targetVersion").is_string() ||
                !value.at("installedVersion").is_string() || !value.at("sequence").is_number_unsigned()) return false;
            if (value.at("sequence").get<unsigned>()<=usbipUpgrade_.value("sequence",0u)) return false;
            usbipUpgrade_=value;publishUsbipLocked();return true;
        } catch (...) {return false;}
    }
    nlohmann::json completedReport() const {
        nlohmann::json report;
        try {
            // Read the bounded report once, only after our child has exited.
            WIN32_FILE_ATTRIBUTE_DATA currentInfo{};
            const bool metadataKnown=GetFileAttributesExW(report_.c_str(),GetFileExInfoStandard,&currentInfo)!=FALSE;
            const bool unchanged=reportPreviouslyPresent_ && metadataKnown &&
                currentInfo.nFileSizeHigh==previousReportInfo_.nFileSizeHigh &&
                currentInfo.nFileSizeLow==previousReportInfo_.nFileSizeLow &&
                CompareFileTime(&currentInfo.ftLastWriteTime,&previousReportInfo_.ftLastWriteTime)==0;
            if (!reportPreviouslyKnown_ || !metadataKnown || unchanged)
                return nlohmann::json{};
            std::ifstream input(std::filesystem::path(report_),std::ios::binary | std::ios::ate);
            const auto length=input.tellg();
            if (input && length>0 && length<=131072) {
                std::string bytes(static_cast<size_t>(length),'\0');input.seekg(0);
                if (input.read(bytes.data(),static_cast<std::streamsize>(bytes.size())))
                    report=nlohmann::json::parse(bytes,nullptr,false);
            }
            return report;
        } catch (...) {
            return nlohmann::json{};
        }
    }
    static std::wstring quote(const std::wstring& value) {
        std::wstring out=L"\"";unsigned slashes=0;
        for (wchar_t c:value) {
            if (c==L'\\') {++slashes;continue;}
            if (c==L'\"') {out.append(slashes*2+1,L'\\');out+=c;}
            else {out.append(slashes,L'\\');out+=c;}
            slashes=0;
        }
        out.append(slashes*2,L'\\');out+=L'\"';return out;
    }
    mutable std::mutex mutex_;
    std::mutex observationMutex_;
    ULONGLONG lastObservationAt_=0;
    bool observationStarted_=false;
    std::shared_ptr<const nlohmann::json> usbipMaintenancePublished_;
    nlohmann::json usbipUpgrade_=nullptr;
    std::shared_ptr<const nlohmann::json> usbipPublished_;
    std::atomic<State> state_{NotStarted};
    std::atomic<DWORD> result_{0};
    std::atomic<HidHideSetupNotice> notice_{HidHideSetupNotice::None};
    std::wstring report_,root_,redistDirectory_;
    std::string sessionToken_;
    WIN32_FILE_ATTRIBUTE_DATA previousReportInfo_{};
    bool reportPreviouslyPresent_=false,reportPreviouslyKnown_=false;
    HANDLE process_=nullptr;
    ULONGLONG started_=0;
};
static_assert(!GamepadDriverSetup::HasWarning(GamepadDriverSetup::NotStarted));
static_assert(!GamepadDriverSetup::HasWarning(GamepadDriverSetup::Ready));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::Pending));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::Slow));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::Failed));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::RebootRequired));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::InstallerMissing));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::InstallerInvalid));
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::InventoryUnverified));
static_assert(GamepadDriverSetup::ResultState(18)==GamepadDriverSetup::InventoryUnverified);
static_assert(GamepadDriverSetup::HasWarning(GamepadDriverSetup::MaintenanceDeferred));
static_assert(GamepadDriverSetup::ResultState(19)==GamepadDriverSetup::MaintenanceDeferred);
static_assert(GamepadDriverSetup::ResultState(20)==GamepadDriverSetup::Failed);
static_assert(GamepadDriverSetup::ResultState(3010)==GamepadDriverSetup::RebootRequired);
static_assert(GamepadDriverSetup::ResultState(12)==GamepadDriverSetup::InstallerMissing);
static_assert(GamepadDriverSetup::ResultState(13)==GamepadDriverSetup::InstallerInvalid);
static_assert(GamepadDriverSetup::ResultState(1603)==GamepadDriverSetup::Failed);
}
