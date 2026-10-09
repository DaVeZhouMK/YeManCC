#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <xinput.h>
#include <wrl.h>
#include <WebView2.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include "json.hpp"
#include "custom_steam_library_paths.h"
#include "custom_steam_library_inventory.h"
#include "custom_steam_library_identity_cache.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "xinput9_1_0.lib")

using json = nlohmann::json;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

static constexpr UINT WM_WORKSPACE_RESULT = WM_APP + 0x41;
static constexpr UINT WM_WORKSPACE_DIRECTORY_DIRTY = WM_APP + 0x42;
static constexpr UINT WM_WORKSPACE_NETWORK_RESTORED = WM_APP + 0x43;
static constexpr UINT_PTR DIRECTORY_DEBOUNCE_TIMER = 0x51;
static constexpr UINT_PTR GAMEPAD_TIMER = 0x52;
static constexpr UINT_PTR UPDATE_HEALTH_EXIT_TIMER = 0x53;
static constexpr UINT WM_WORKSPACE_TITLEBAR_ACTION = WM_APP + 0x44;
static constexpr int kWorkspaceTitlebarHeight = 46;
static constexpr UINT_PTR TITLEBAR_ACTION_MINIMIZE = 1;
static constexpr UINT_PTR TITLEBAR_ACTION_MAXIMIZE = 2;
static constexpr UINT_PTR TITLEBAR_ACTION_CLOSE = 3;
// Window properties are the process-independent ownership marker used by
// YeManCC to distinguish a standalone host window from a parent-owned child.
// Keep these names/values in sync with native/main.cpp.
static constexpr wchar_t kInputOwnerProperty[] = L"YeManSteamLibrary.InputOwner";
static constexpr wchar_t kParentPidProperty[] = L"YeManSteamLibrary.ParentPid";
static constexpr ULONG_PTR kHostInputOwnerMarker = 1;
static constexpr ULONG_PTR kParentInputOwnerMarker = 2;
// Automatic maintenance is temporarily disabled while the workspace UI is
// being simplified.  Explicitly opening the custom library still runs the
// existing scan/first-pass recognition pipeline.
static constexpr bool kAutomaticLibraryMaintenanceEnabled = false;
static constexpr UINT WM_WEBVIEW_PROCESS_FAILED = WM_APP + 0x45;
static constexpr wchar_t kWorkspaceUiHost[] = L"steam-library.localhost";
static constexpr wchar_t kWorkspaceUiHostLegacy[] = L"steam-library.local";
static constexpr wchar_t kWorkspaceDataHost[] = L"steam-library-data.localhost";
static constexpr wchar_t kWorkspaceDataHostLegacy[] = L"steam-library-data.local";
static constexpr wchar_t kWorkspaceUiUrl[] = L"https://steam-library.localhost/index.html";
static constexpr int kWebViewMaxNavigationRetries = 2;
static constexpr int kWebViewMaxProcessRecoveries = 3;

static std::atomic<HWND> g_window{nullptr};
static std::mutex g_resultPostMutex;
static std::atomic<bool> g_shuttingDown{false};
static HWND g_titlebar = nullptr;
static HWND g_minimizeButton = nullptr;
static HWND g_maximizeButton = nullptr;
static HWND g_closeButton = nullptr;
static HFONT g_titlebarFont = nullptr;
static HFONT g_titlebarSubtitleFont = nullptr;
static HFONT g_titlebarButtonFont = nullptr;
static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;
static ComPtr<ICoreWebView2Environment> g_webviewEnvironment;
static std::atomic<UINT64> g_webviewExpectedNavigationId{0};
static std::atomic<bool> g_webviewMainDocumentReady{false};
static std::atomic<int> g_webviewNavigationRetries{0};
static std::atomic<int> g_webviewProcessRecoveries{0};
static std::atomic<bool> g_webviewInitializeBusy{false};
static std::mutex g_hostExitReasonMutex;
static std::string g_hostExitKind = "wm-close";
static std::string g_hostExitDetail;
static fs::path g_labRoot;
static fs::path g_dataRoot;
static fs::path g_worker;
static std::atomic<uint64_t> g_jsonBackupSequence{0};
static std::atomic<bool> g_hostCrashReportInProgress{false};
static std::atomic<bool> g_hostExitRecordWritten{false};
// Host-side JSON mutations must not interleave.  Background recognition and
// UI edits use different worker paths, so this lock protects the local
// read/backup/replace section whenever the host writes a JSON document.
static std::mutex g_jsonWriteMutex;
// A background scan/recognition owns this boundary for its read/worker/write
// sequence.  An edit/save command must acquire it before touching library
// state, which closes the small race between checking a task flag and starting
// the actual worker thread.
static std::mutex g_taskBoundaryMutex;
// Steam shortcut operations share the selected target and the real Steam
// file boundary, but they must not hold the local-library transaction lock
// while waiting on Steam or a bounded Worker process.
static std::mutex g_steamOperationMutex;
static std::mutex g_uiErrorMutex;

// Keep the standalone product startup visual identical to YeManCC.  This is
// the same native, transparent GDI+ ring used by the main program; it is not
// a WebView/CSS loader, so it remains visible while WebView2 is still being
// created and the page has not painted its first navigation yet.
static HWND g_splash = nullptr;
static double g_splashAngle = 0.0;
static double g_splashPhase = 0.0;
static LARGE_INTEGER g_splashLastQpc{};
static LARGE_INTEGER g_splashQpcFrequency{};
static constexpr UINT_PTR SPLASH_ANIMATION_TIMER_ID = 1;
static constexpr UINT_PTR SPLASH_FALLBACK_TIMER_ID = 2;
static constexpr UINT SPLASH_TIMER_INTERVAL_MS = 8;
static constexpr UINT SPLASH_FALLBACK_TIMEOUT_MS = 8000;
static constexpr double SPLASH_ROTATION_DEGREES_PER_SECOND = 420.0;
static ULONG_PTR g_splashGdiplusToken = 0;
static HDC g_splashDc = nullptr;
static HBITMAP g_splashBitmap = nullptr;
static void* g_splashBits = nullptr;
static int g_splashSurfaceW = 0;
static int g_splashSurfaceH = 0;

static fs::path workspaceJobsRoot() {
    return custom_steam_library::jobsRoot(g_dataRoot);
}
static std::atomic<bool> g_taskRunning{false};
static std::atomic<bool> g_cancelRequested{false};
static std::mutex g_activeProcessMutex;
static HANDLE g_activeProcess = nullptr;
// Every process started by this host joins this job when Windows permits it.
// Closing the host then closes the job and prevents a stale Worker from a
// crashed instance writing into a new instance's scrape output.
static std::mutex g_workerProcessJobMutex;
static HANDLE g_workerProcessJob = nullptr;
// Network scraping runs outside the UI command lane.
static std::atomic<bool> g_libraryPipelineRunning{false};
static std::atomic<bool> g_scrapeQueueRunning{false};
static std::atomic<bool> g_scrapeQueueForceRequested{false};
static std::atomic<uint64_t> g_scrapeBatchSequence{0};
static std::atomic<bool> g_manualIdentifyRunning{false};
static std::mutex g_manualSubmissionMutex;
static std::atomic<bool> g_identitySearchRunning{false};
static std::atomic<bool> g_artworkTaskRunning{false};
static std::atomic<bool> g_networkWakeRunning{false};
static std::atomic<bool> g_networkWakePending{false};
static std::atomic<bool> g_networkWakeCancelRequested{false};
static HANDLE g_networkWakeActiveProcess = nullptr;
static std::mutex g_scrapeStateMutex;
static json g_scrapeState = json::object();

static void initializeWorkerProcessJob() noexcept {
    try {
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (!job) return;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION information{};
        information.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                     &information, sizeof(information))) {
            CloseHandle(job);
            return;
        }
        std::lock_guard<std::mutex> lock(g_workerProcessJobMutex);
        g_workerProcessJob = job;
    } catch (...) {
        // A host can still run if a policy prevents nested job assignment.
    }
}

static void assignProcessToHostWorkerJob(HANDLE process) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_workerProcessJobMutex);
        if (g_workerProcessJob) AssignProcessToJobObject(g_workerProcessJob, process);
    } catch (...) {
        // Assignment can be denied when an integrated parent owns a job.
    }
}

static void closeWorkerProcessJob() noexcept {
    HANDLE job = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_workerProcessJobMutex);
        job = g_workerProcessJob;
        g_workerProcessJob = nullptr;
    }
    if (job) CloseHandle(job);
}
// Recognition, candidate search, and artwork requests are independent UI
// jobs.  Keep their cancellation handles and generations per request so a
// slow job for one game cannot reject or overwrite an edit on another game.
struct BackgroundJobControl {
    std::atomic<bool> cancelRequested{false};
    HANDLE activeProcess = nullptr;
    std::string key;
    std::string jobId;
    uint64_t generation = 0;
};
using BackgroundJob = std::shared_ptr<BackgroundJobControl>;
static std::mutex g_backgroundJobsMutex;
static std::vector<BackgroundJob> g_manualIdentifyJobs;
static std::vector<BackgroundJob> g_identitySearchJobs;
static std::vector<BackgroundJob> g_artworkJobs;
static std::vector<BackgroundJob> g_libraryPipelineJobs;
static std::vector<BackgroundJob> g_scrapeQueueJobs;
static std::vector<BackgroundJob> g_networkWakeJobs;
static std::unordered_map<std::string, uint64_t> g_manualIdentifyGenerations;
static std::unordered_map<std::string, uint64_t> g_identitySearchGenerations;
static std::unordered_map<std::string, uint64_t> g_artworkGenerations;
static std::unordered_map<std::string, uint64_t> g_libraryPipelineGenerations;
static std::unordered_map<std::string, uint64_t> g_scrapeQueueGenerations;
static std::unordered_map<std::string, uint64_t> g_networkWakeGenerations;
// One game may be reached from several UI paths at once: opening the library,
// pressing manual refresh, and saving an editor AppID.  These paths use
// different BackgroundJob vectors, so the vector-level generations alone do
// not prevent two workers from writing the same manifest.  Keep a small
// per-executable lease table as the final coordination boundary.
struct GameTaskLease {
    std::string key;
    std::string owner;
    uint64_t generation = 0;
};
struct GameTaskLeaseState {
    uint64_t generation = 0;
    std::string owner;
};
static std::mutex g_gameTaskLeaseMutex;
static std::unordered_map<std::string, GameTaskLeaseState> g_gameTaskLeases;
static HANDLE g_networkNotification = nullptr;
static WORD g_previousGamepadButtons = 0;
static int g_gamepadNavigationX = 0;
static int g_gamepadNavigationY = 0;
static ULONGLONG g_gamepadNavigationLastTick = 0;
static WORD g_gamepadShoulderPending = 0;
static ULONGLONG g_gamepadShoulderPendingTick = 0;
static bool g_gamepadShoulderComboHeld = false;
static ULONGLONG g_gamepadShoulderSuppressUntil = 0;
static std::atomic<bool> g_directoryDirty{false};
// The selected Steam target is UI state, not library state.  It is kept only
// for the lifetime of this isolated workspace process so refresh/scan actions
// continue to plan against the account the user selected.
static json g_steamTarget = json::object();
static std::mutex g_steamTargetMutex;

static void setSteamTarget(const json& target) {
    std::lock_guard<std::mutex> lock(g_steamTargetMutex);
    g_steamTarget = target;
}

static json steamTargetSnapshot() {
    std::lock_guard<std::mutex> lock(g_steamTargetMutex);
    return g_steamTarget;
}

// Keep a worker from occupying a detached lane forever when a provider or
// proxy stops completing its request. The worker has its own retry policy;
// this is only the host-side last-resort process bound.
static constexpr DWORD kWorkerDefaultTimeoutMs = 15u * 60u * 1000u;
static constexpr DWORD kWeakNetworkWorkerTimeoutMs = 8u * 60u * 1000u;
static constexpr DWORD kArtworkWorkerTimeoutMs = 3u * 60u * 1000u;
// A library refresh must remain responsive even when Steam's search endpoint
// stops completing requests.  The worker already has its own bounded HTTP
// retry/circuit-breaker policy; these are only host-side process fences for a
// single queue attempt.  Keeping the queue fence below two minutes prevents
// one dead provider from occupying one of the two game lanes for the whole
// batch.
static constexpr DWORD kScrapeIdentityWorkerTimeoutMs = 90u * 1000u;
static constexpr DWORD kScrapeArtworkWorkerTimeoutMs = 120u * 1000u;
static constexpr int kScrapeRetryCount = 3;
static constexpr DWORD kScrapeRetryDelayMs[] = {1000u, 3000u, 5000u};
// The retry budget is shared by the complete identity -> Steam artwork ->
// IGDB fallback chain for one game. Without this outer deadline, four
// identity attempts plus four Steam-artwork attempts plus four IGDB attempts
// can keep one card in "scraping" long after useful work has stopped.
static constexpr DWORD kScrapePerGameBudgetMs = 5u * 60u * 1000u;
// Queue/semaphore waiting is not provider work. Suspend only idle time;
// retries and all worker stages still share the same finite five-minute budget.
struct ScrapeGameTimeBudget {
    using Clock = std::chrono::steady_clock;
    Clock::time_point deadline;
    std::optional<Clock::time_point> idleSince;
    explicit ScrapeGameTimeBudget(Clock::time_point now)
        : deadline(now + std::chrono::milliseconds(kScrapePerGameBudgetMs)) {}
    void suspend(Clock::time_point now) { if (!idleSince) idleSince = now; }
    void resume(Clock::time_point now) {
        if (!idleSince) return;
        if (now > *idleSince) deadline += now - *idleSince;
        idleSince.reset();
    }
    DWORD remaining(Clock::time_point now) const {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - (idleSince ? *idleSince : now)).count();
        return left <= 0 ? 0 : static_cast<DWORD>((std::min)(left, int64_t{kScrapePerGameBudgetMs}));
    }
};
static bool acquireScrapeArtworkSlot(std::mutex& mutex, std::condition_variable& cv,
    int& active, int limit, const std::atomic<bool>& cancelled, const std::atomic<bool>& jobCancelled) {
    std::unique_lock<std::mutex> lock(mutex);
    // Cancellation does not own this stack-local CV. A bounded wait ensures
    // ALL pending lanes leave, even if no active worker remains to notify them.
    while (active >= limit && !cancelled.load() && !jobCancelled.load())
        cv.wait_for(lock, std::chrono::milliseconds(200));
    if (cancelled.load() || jobCancelled.load()) return false;
    ++active;
    return true;
}
static constexpr DWORD kLocalScanWorkerTimeoutMs = 2u * 60u * 1000u;
static constexpr ULONGLONG kGamepadShoulderDecisionMs = 50ULL;
static constexpr ULONGLONG kGamepadNavigationRepeatMs = 220ULL;
static constexpr SHORT kGamepadThumbThreshold = 16000;

static bool backgroundLibraryTaskRunning() {
    return g_libraryPipelineRunning.load() || g_scrapeQueueRunning.load() ||
        g_manualIdentifyRunning.load() || g_identitySearchRunning.load() ||
        g_artworkTaskRunning.load() || g_networkWakeRunning.load();
}

// Input ownership is deliberately explicit.  The independent product owns
// XInput in host mode; hidden YeManCC integration mode accepts only semantic
// actions from the parent and never starts a second XInput poller here.
struct LaunchOptions {
    std::wstring inputOwner = L"host";
    std::wstring integration;
    std::wstring protocol;
    DWORD parentPid = 0;
    std::wstring updateHealthHandshakePath;
    std::wstring updateHealthHandshakeToken;
    std::wstring updateHealthPackageVersion;
    bool updateHealthOnly = false;
};

static LaunchOptions g_launchOptions;
static bool g_updateHealthOnly = false;
static bool g_updateHealthDataRootWritable = false;

static LaunchOptions parseLaunchOptions() {
    LaunchOptions options;
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments) return options;
    for (int index = 1; index < argumentCount; ++index) {
        const std::wstring argument(arguments[index]);
        constexpr std::wstring_view inputPrefix = L"--input-owner=";
        constexpr std::wstring_view integrationPrefix = L"--integration=";
        constexpr std::wstring_view protocolPrefix = L"--protocol=";
        constexpr std::wstring_view parentPrefix = L"--parent-pid=";
        if (argument.rfind(inputPrefix.data(), 0) == 0) {
            options.inputOwner = argument.substr(inputPrefix.size());
        } else if (argument.rfind(integrationPrefix.data(), 0) == 0) {
            options.integration = argument.substr(integrationPrefix.size());
        } else if (argument.rfind(protocolPrefix.data(), 0) == 0) {
            options.protocol = argument.substr(protocolPrefix.size());
        } else if (argument.rfind(parentPrefix.data(), 0) == 0) {
            try {
                const auto value = argument.substr(parentPrefix.size());
                const auto parsed = std::stoull(value);
                if (parsed <= (std::numeric_limits<DWORD>::max)()) options.parentPid = static_cast<DWORD>(parsed);
            } catch (...) {
                options.parentPid = 0;
            }
        } else if (argument == L"--update-health-only") {
            options.updateHealthOnly = true;
        } else if (argument.rfind(L"--update-health-handshake=", 0) == 0) {
            options.updateHealthHandshakePath = argument.substr(26);
        } else if (argument == L"--update-health-handshake" && index + 1 < argumentCount) {
            options.updateHealthHandshakePath = arguments[++index];
        } else if (argument.rfind(L"--update-health-handshake-token=", 0) == 0) {
            options.updateHealthHandshakeToken = argument.substr(32);
        } else if (argument == L"--update-health-handshake-token" && index + 1 < argumentCount) {
            options.updateHealthHandshakeToken = arguments[++index];
        } else if (argument.rfind(L"--update-health-package-version=", 0) == 0) {
            options.updateHealthPackageVersion = argument.substr(32);
        } else if (argument == L"--update-health-package-version" && index + 1 < argumentCount) {
            options.updateHealthPackageVersion = arguments[++index];
        }
    }
    LocalFree(arguments);
    return options;
}

static bool parentInputModeEnabled() {
    return g_launchOptions.inputOwner == L"parent" &&
        g_launchOptions.integration == L"YeManCC" &&
        g_launchOptions.protocol == L"1" &&
        g_launchOptions.parentPid != 0;
}

static ULONG_PTR windowPropertyValue(HWND window, const wchar_t* name) {
    return reinterpret_cast<ULONG_PTR>(GetPropW(window, name));
}

static bool windowUsesParentInput(HWND window) {
    return window && windowPropertyValue(window, kInputOwnerProperty) == kParentInputOwnerMarker;
}

static DWORD windowParentPid(HWND window) {
    return static_cast<DWORD>(windowPropertyValue(window, kParentPidProperty));
}

static bool isParentSemanticAction(const std::wstring& action) {
    static const std::unordered_set<std::wstring> allowed = {
        L"navigate-left", L"navigate-right", L"navigate-up", L"navigate-down",
        L"accept", L"back", L"tab-previous", L"tab-next", L"edit",
    };
    return allowed.contains(action);
}

struct ParentWindowLookup {
    DWORD pid = 0;
    HWND window = nullptr;
};

static BOOL CALLBACK findParentWindowCallback(HWND window, LPARAM parameter) {
    auto* lookup = reinterpret_cast<ParentWindowLookup*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != lookup->pid || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) return TRUE;
    lookup->window = window;
    return FALSE;
}

static void restoreParentWindow() {
    if (!parentInputModeEnabled()) return;
    ParentWindowLookup lookup{g_launchOptions.parentPid};
    EnumWindows(findParentWindowCallback, reinterpret_cast<LPARAM>(&lookup));
    if (!lookup.window || !IsWindow(lookup.window)) return;
    ShowWindow(lookup.window, SW_RESTORE);
    SetForegroundWindow(lookup.window);
}

static void focusWorkspaceWindow() {
    if (!g_window || !IsWindow(g_window)) return;
    ShowWindow(g_window, IsIconic(g_window) ? SW_RESTORE : SW_SHOW);
    SetActiveWindow(g_window);
    SetForegroundWindow(g_window);
    // Setting focus on the outer host window is not enough for WebView2:
    // XInput/semantic actions are dispatched by the native timer, but the
    // page still needs the WebView child to become the active keyboard/focus
    // surface. Mouse clicks used to repair this implicitly, which made the
    // first gamepad navigation start from the wrong document focus.
    if (g_controller) {
        const HRESULT moved = g_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        if (FAILED(moved)) SetFocus(g_window);
    } else {
        SetFocus(g_window);
    }
}

static std::wstring toWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring output(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), count);
    return output;
}

static std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), count, nullptr, nullptr);
    return output;
}

static std::wstring quoteArgument(const std::wstring& value) {
    if (value.empty()) return L"\"\"";
    if (value.find_first_of(L" \t\n\v\"") == std::wstring::npos) return value;
    std::wstring output = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++slashes;
        } else if (ch == L'\"') {
            output.append(slashes * 2 + 1, L'\\');
            output.push_back(L'\"');
            slashes = 0;
        } else {
            output.append(slashes, L'\\');
            slashes = 0;
            output.push_back(ch);
        }
    }
    output.append(slashes * 2, L'\\');
    output.push_back(L'\"');
    return output;
}

struct HostIoHandle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~HostIoHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

static bool hostRegularFile(const fs::path& path) {
    std::error_code error;
    const auto status = fs::status(custom_steam_library::ioPath(path), error);
    if (error == std::errc::no_such_file_or_directory) return false;
    if (error) throw std::runtime_error("Cannot inspect JSON file: " + toUtf8(path.wstring()));
    if (status.type() == fs::file_type::not_found) return false;
    if (!fs::is_regular_file(status)) throw std::runtime_error("JSON path is not a regular file");
    return true;
}

static bool hostUserConfigPath(const fs::path& path) {
    return !g_dataRoot.empty() && path.parent_path().lexically_normal() ==
        (g_dataRoot / L"config").lexically_normal();
}

static bool hostConfigDocumentValid(const fs::path& path, const json& value) {
    if (!hostUserConfigPath(path)) return true;
    if (!value.is_object()) return false;
    if (path.filename() == L"manual-overrides.json")
        return !value.contains("items") || value["items"].is_object();
    if (path.filename() == L"library-config.json") {
        for (const auto* key : {"roots", "trainerRoots", "manualGameDirectories", "excludedGameDirectories"})
            if (value.contains(key) && !value[key].is_array()) return false;
        for (const auto* key : {"manualPrimary", "manualBuckets", "firstRunDefaultScan"})
            if (value.contains(key) && !value[key].is_object()) return false;
    }
    return true;
}

static std::string readText(const fs::path& path, size_t maximum = 64u << 20) {
    std::error_code error;
    const auto ioPath = custom_steam_library::ioPath(path);
    if (!hostRegularFile(path)) return {};
    HostIoHandle input{CreateFileW(ioPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (input.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot read JSON file");
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(input.value, &length) || length.QuadPart < 0 ||
        static_cast<uint64_t>(length.QuadPart) > maximum) throw std::runtime_error("JSON file is too large");
    std::string result(static_cast<size_t>(length.QuadPart), '\0');
    size_t offset = 0;
    while (offset < result.size()) {
        DWORD read = 0;
        const DWORD count = static_cast<DWORD>((std::min)(result.size() - offset, static_cast<size_t>(1u << 20)));
        if (!ReadFile(input.value, result.data() + offset, count, &read, nullptr) || read == 0)
            throw std::runtime_error("Cannot completely read JSON file");
        offset += read;
    }
    LARGE_INTEGER after{};
    if (!GetFileSizeEx(input.value, &after) || after.QuadPart != length.QuadPart)
        throw std::runtime_error("JSON file changed while reading");
    return result;
}

static void writeHostBytes(const fs::path& path, std::string_view bytes) {
    HostIoHandle output{CreateFileW(custom_steam_library::ioPath(path).c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (output.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        throw std::runtime_error("无法写入隔离素材配置（系统错误码：" + std::to_string(error) + "）");
    }
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const DWORD count = static_cast<DWORD>((std::min)(bytes.size() - offset, static_cast<size_t>(1u << 20)));
        if (!WriteFile(output.value, bytes.data() + offset, count, &written, nullptr) || written == 0)
            throw std::runtime_error("无法完成隔离素材配置写入");
        offset += written;
    }
    if (!FlushFileBuffers(output.value)) throw std::runtime_error("无法刷新隔离素材配置文件");
}

static std::vector<fs::path> jsonRecoveryCandidates(const fs::path& path) {
    std::vector<fs::path> candidates;
    candidates.push_back(path.parent_path() / (path.filename().wstring() + L".bak"));
    if (g_dataRoot.empty()) return candidates;
    const auto relative = path.lexically_relative(g_dataRoot);
    if (relative.empty() || relative.is_absolute()) return candidates;
    for (const auto& part : relative) if (part == L"..") return candidates;
    const auto section = relative.parent_path().filename().wstring();
    const auto backupDirectory = g_dataRoot / L"backups" /
        ((section == L"config" || section == L"state") ? section : L"runtime");
    const auto prefix = toUtf8(path.stem().wstring()) + "-";
    std::error_code error;
    const auto status = fs::status(custom_steam_library::ioPath(backupDirectory), error);
    if (error && error != std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect JSON recovery directory");
    if (!error && status.type() != fs::file_type::not_found) {
        if (!fs::is_directory(status)) throw std::runtime_error("JSON recovery path is not a directory");
        for (const auto& entry : fs::directory_iterator(custom_steam_library::ioPath(backupDirectory))) {
            const auto filename = toUtf8(entry.path().filename().wstring());
            if (entry.path().extension() == L".json" && filename.starts_with(prefix) &&
                entry.is_regular_file()) candidates.push_back(entry.path());
        }
    }
    std::sort(candidates.begin() + 1, candidates.end(), [](const fs::path& left, const fs::path& right) {
        return left.filename().wstring() > right.filename().wstring();
    });
    return candidates;
}

static json readJson(const fs::path& path, json fallback) {
    bool damaged = false;
    const auto userConfig = hostUserConfigPath(path);
    const auto readCandidate = [&](const fs::path& candidate) -> std::optional<json> {
        if (!hostRegularFile(candidate)) return std::nullopt;
        // I/O is outside the parse catch. A locked/unreadable file is not a
        // corrupt file and must not publish old or empty user configuration.
        const auto bytes = readText(candidate);
        try {
            auto value = json::parse(bytes);
            if (!hostConfigDocumentValid(path, value)) { damaged = true; return std::nullopt; }
            return value;
        } catch (const json::parse_error&) {
            damaged = true;
            return std::nullopt;
        }
    };
    if (auto value = readCandidate(path)) return std::move(*value);
    // Do not enumerate recovery storage for a healthy main document.
    for (const auto& backup : jsonRecoveryCandidates(path))
        if (auto value = readCandidate(backup)) return std::move(*value);
    if (userConfig && damaged)
        throw std::runtime_error("自定义游戏库配置及可用备份均损坏；未写入默认值");
    return fallback;
}

// nlohmann::json::value() throws when a present field is null.  Config files
// are user-portable and can be interrupted mid-edit, so every boolean switch
// crossing the UI/worker boundary must treat null or a wrong type as default.
static bool jsonBoolOr(const json& value, const char* key, bool fallback = false) {
    if (!value.is_object()) return fallback;
    const auto it = value.find(key);
    return it != value.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

static std::wstring normalizedPathKey(const fs::path& value) {
    std::error_code error;
    auto normalized = fs::weakly_canonical(value, error);
    if (error) normalized = fs::absolute(value, error);
    if (error) normalized = value;
    auto key = normalized.lexically_normal().wstring();
    std::replace(key.begin(), key.end(), L'/', L'\\');
    if (!key.empty()) CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
    while (key.size() > 3 && key.back() == L'\\') key.pop_back();
    return key;
}

static bool samePath(const fs::path& left, const fs::path& right) {
    return normalizedPathKey(left) == normalizedPathKey(right);
}

static std::string backgroundExecutableKey(const std::string& executable) {
    return toUtf8(normalizedPathKey(fs::path(toWide(executable))));
}

static GameTaskLease reserveManualGameTask(const std::string& executable) {
    const auto key = backgroundExecutableKey(executable);
    std::lock_guard<std::mutex> lock(g_gameTaskLeaseMutex);
    auto& state = g_gameTaskLeases[key];
    state.generation += 1;
    state.owner = "manual-identify";
    return {key, state.owner, state.generation};
}

static std::optional<GameTaskLease> tryAcquireScrapeGameTask(
    const std::string& executable, const std::string& stage) {
    const auto key = backgroundExecutableKey(executable);
    std::lock_guard<std::mutex> lock(g_gameTaskLeaseMutex);
    auto& state = g_gameTaskLeases[key];
    if (!state.owner.empty()) return std::nullopt;
    state.generation += 1;
    state.owner = "scrape-" + stage;
    return GameTaskLease{key, state.owner, state.generation};
}

static bool gameTaskLeaseIsCurrent(const GameTaskLease& lease) {
    if (lease.key.empty() || lease.generation == 0) return false;
    std::lock_guard<std::mutex> lock(g_gameTaskLeaseMutex);
    const auto it = g_gameTaskLeases.find(lease.key);
    return it != g_gameTaskLeases.end() && it->second.generation == lease.generation &&
        it->second.owner == lease.owner;
}

static void releaseGameTask(const GameTaskLease& lease) {
    if (lease.key.empty() || lease.generation == 0) return;
    std::lock_guard<std::mutex> lock(g_gameTaskLeaseMutex);
    const auto it = g_gameTaskLeases.find(lease.key);
    if (it == g_gameTaskLeases.end() || it->second.generation != lease.generation ||
        it->second.owner != lease.owner) return;
    it->second.owner.clear();
}

static void refreshBackgroundJobFlagsLocked() {
    g_manualIdentifyRunning = !g_manualIdentifyJobs.empty();
    g_identitySearchRunning = !g_identitySearchJobs.empty();
    g_artworkTaskRunning = !g_artworkJobs.empty();
}

static bool jobIsCurrent(
    const std::unordered_map<std::string, uint64_t>& generations,
    const BackgroundJob& job) {
    if (!job || job->cancelRequested.load()) return false;
    std::lock_guard<std::mutex> lock(g_backgroundJobsMutex);
    const auto it = generations.find(job->key);
    return it != generations.end() && it->second == job->generation;
}

static bool cancelBackgroundJobs(
    std::vector<BackgroundJob>& jobs,
    const std::string& key = {}) {
    std::vector<BackgroundJob> targets;
    {
        std::lock_guard<std::mutex> lock(g_backgroundJobsMutex);
        for (const auto& job : jobs) {
            if (job && (key.empty() || job->key == key)) {
                job->cancelRequested = true;
                targets.push_back(job);
            }
        }
    }
    bool requested = !targets.empty();
    // Do not hold g_backgroundJobsMutex while taking g_activeProcessMutex:
    // runProcess takes the latter before a worker leaves, and reversing the
    // order here would make cancellation able to deadlock shutdown.
    std::lock_guard<std::mutex> processLock(g_activeProcessMutex);
    for (const auto& job : targets) {
        if (job && job->activeProcess) {
            TerminateProcess(job->activeProcess, ERROR_CANCELLED);
            requested = true;
        }
    }
    return requested;
}

static BackgroundJob beginBackgroundJob(
    const char* kind,
    std::vector<BackgroundJob>& jobs,
    std::unordered_map<std::string, uint64_t>& generations,
    const std::string& key) {
    // Repeating the same action for one game replaces the older request. A
    // different game receives its own control and continues independently.
    cancelBackgroundJobs(jobs, key);
    auto job = std::make_shared<BackgroundJobControl>();
    job->key = key;
    {
        std::lock_guard<std::mutex> lock(g_backgroundJobsMutex);
        job->generation = ++generations[key];
        job->jobId = std::string(kind) + ":" + key + ":" + std::to_string(job->generation);
        jobs.push_back(job);
        refreshBackgroundJobFlagsLocked();
    }
    return job;
}

static void finishBackgroundJob(
    std::vector<BackgroundJob>& jobs,
    const BackgroundJob& job) {
    std::lock_guard<std::mutex> lock(g_backgroundJobsMutex);
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [&](const auto& item) {
        return item == job;
    }), jobs.end());
    refreshBackgroundJobFlagsLocked();
}

static bool cancelAllBackgroundJobs() {
    bool requested = false;
    requested = cancelBackgroundJobs(g_manualIdentifyJobs) || requested;
    requested = cancelBackgroundJobs(g_identitySearchJobs) || requested;
    requested = cancelBackgroundJobs(g_artworkJobs) || requested;
    requested = cancelBackgroundJobs(g_libraryPipelineJobs) || requested;
    requested = cancelBackgroundJobs(g_scrapeQueueJobs) || requested;
    requested = cancelBackgroundJobs(g_networkWakeJobs) || requested;
    return requested;
}

static std::string mappedDataUrl(const fs::path& path) {
    if (path.empty() || !fs::is_regular_file(path)) return {};
    std::error_code error;
    auto relative = fs::relative(path, g_dataRoot, error);
    if (error || relative.empty()) return {};
    for (const auto& part : relative) {
        if (part == L"..") return {};
    }
    auto value = toUtf8(relative.generic_wstring());
    std::ostringstream encoded;
    encoded << "https://steam-library-data.localhost/";
    static constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/') {
            encoded << static_cast<char>(ch);
        } else {
            encoded << '%' << hex[ch >> 4] << hex[ch & 15];
        }
    }
    return encoded.str();
}

static std::string jsonStringOr(const json& value, const char* key, const std::string& fallback = {});
static json refreshArtworkForAllSteamAccounts(const json& requestedTarget);

static bool hostProcessRunningByName(const wchar_t* wantedName, std::wstring* firstPath = nullptr) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, wantedName) != 0) continue;
            running = true;
            if (firstPath && firstPath->empty()) {
                HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (process) {
                    wchar_t path[MAX_PATH * 4]{};
                    DWORD length = static_cast<DWORD>(std::size(path));
                    if (QueryFullProcessImageNameW(process, 0, path, &length)) *firstPath = std::wstring(path, length);
                    CloseHandle(process);
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return running;
}

static void waitForHostProcessExit(const wchar_t* wantedName, DWORD timeoutMs) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (hostProcessRunningByName(wantedName) && GetTickCount64() < deadline) Sleep(100);
}

static void closeSteamForShortcutCommit() {
    std::wstring steamPath;
    if (!hostProcessRunningByName(L"steam.exe", &steamPath)) return;
    if (steamPath.empty() || !fs::is_regular_file(fs::path(steamPath))) {
        throw std::runtime_error("无法定位正在运行的 Steam 程序，未执行关闭，也未写入 Steam 文件");
    }

    // Steam's supported shutdown switch lets Steam flush shortcuts.vdf and
    // localconfig.vdf before exiting.  Do not fall back to a hard process
    // kill: that is precisely what can leave an incomplete transaction.
    std::wstring command = quoteArgument(steamPath) + L" -shutdown";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(0);
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            steamPath.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, fs::path(steamPath).parent_path().c_str(),
            &startup, &process)) {
        const auto error = GetLastError();
        throw std::runtime_error("无法请求 Steam 温和退出（系统错误码：" + std::to_string(error) + "），未写入 Steam 文件");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    waitForHostProcessExit(L"steam.exe", 20000);
    if (hostProcessRunningByName(L"steam.exe")) {
        throw std::runtime_error("Steam 未在温和退出期限内关闭，未写入 Steam 文件；请先手动退出 Steam");
    }
}

static fs::path steamLaunchPathForTarget(const json& target) {
    const auto explicitExecutable = fs::path(toWide(jsonStringOr(target, "steamExecutable", {})));
    if (!explicitExecutable.empty() && fs::is_regular_file(explicitExecutable)) return explicitExecutable;
    const auto root = fs::path(toWide(jsonStringOr(target, "steamRoot")));
    if (!root.empty() && fs::is_regular_file(root / L"steam.exe")) return root / L"steam.exe";
    std::wstring runningPath;
    hostProcessRunningByName(L"steam.exe", &runningPath);
    if (!runningPath.empty() && fs::is_regular_file(fs::path(runningPath))) return fs::path(runningPath);
    return {};
}

static void restartSteamBigPicture(const json& target) {
    const auto executable = steamLaunchPathForTarget(target);
    if (executable.empty()) throw std::runtime_error("Steam 已关闭，但找不到 steam.exe，快捷方式已写入；请手动启动 Steam");
    std::wstring command = quoteArgument(executable.wstring()) + L" -bigpicture";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(0);
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
        executable.parent_path().c_str(), &startup, &process)) {
        throw std::runtime_error("快捷方式已写入，但 Steam 大屏重启失败");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

static int64_t jsonIntegerOr(const json& value, const char* key, int64_t fallback = 0) {
    if (!value.is_object()) return fallback;
    const auto it = value.find(key);
    if (it == value.end()) return fallback;
    if (it->is_number_integer()) return it->get<int64_t>();
    if (it->is_number_unsigned()) {
        const auto raw = it->get<uint64_t>();
        return raw <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()) ? static_cast<int64_t>(raw) : fallback;
    }
    return fallback;
}
static std::string jsonStringOr(const json& value, const char* key, const std::string& fallback) {
    if (!value.is_object()) return fallback;
    const auto it = value.find(key);
    return it != value.end() && it->is_string() ? it->get<std::string>() : fallback;
}
// Workspace state is assembled from manifests produced by several providers
// and from older schema revisions. Those files legitimately contain null for
// optional fields; never let json::value() throw while building a UI snapshot.
static json jsonArrayOr(const json& value, const char* key) {
    if (!value.is_object()) return json::array();
    const auto it = value.find(key);
    return it != value.end() && it->is_array() ? *it : json::array();
}
static json jsonObjectOr(const json& value, const char* key) {
    if (!value.is_object()) return json::object();
    const auto it = value.find(key);
    return it != value.end() && it->is_object() ? *it : json::object();
}
static double jsonNumberOr(const json& value, const char* key, double fallback = 0.0) {
    if (!value.is_object()) return fallback;
    const auto it = value.find(key);
    if (it == value.end() || !it->is_number()) return fallback;
    return it->get<double>();
}

static std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static std::string trimHost(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// Candidate cache filtering must understand Chinese and full-width text too.
// Keep this deliberately small and dependency-free: fold the reversible
// full-width ASCII range, lowercase with the invariant Windows locale, and
// retain only Unicode letters/digits so punctuation differences do not make a
// valid cached result look unrelated.
static std::string identitySearchKey(const std::string& value) {
    std::wstring wide = toWide(value);
    for (auto& ch : wide) {
        if (ch >= 0xFF01 && ch <= 0xFF5E) ch = static_cast<wchar_t>(ch - 0xFEE0);
        else if (ch == 0x3000) ch = L' ';
    }
    const int needed = LCMapStringEx(
        LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING,
        wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr, 0);
    if (needed > 0) {
        std::wstring lowered(static_cast<size_t>(needed), L'\0');
        if (LCMapStringEx(
                LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING,
                wide.data(), static_cast<int>(wide.size()), lowered.data(), needed,
                nullptr, nullptr, 0) > 0) wide = std::move(lowered);
    }
    std::wstring key;
    for (const auto ch : wide) {
        WORD kind = 0;
        if (GetStringTypeW(CT_CTYPE1, &ch, 1, &kind) != 0 &&
            (kind & (C1_ALPHA | C1_DIGIT)) != 0) key.push_back(ch);
    }
    return toUtf8(key);
}

static bool supportedArtworkFile(const fs::path& path) {
    const auto extension = lowerAscii(toUtf8(path.extension().wstring()));
    return extension == ".jpg" || extension == ".jpeg" || extension == ".png" ||
        extension == ".webp" || extension == ".bmp" || extension == ".ico";
}

struct ArtworkDimensions {
    int width = 0;
    int height = 0;
    bool known = false;
};

static ArtworkDimensions inspectArtworkDimensions(const fs::path& path) {
    ArtworkDimensions result;
    std::error_code error;
    if (!fs::is_regular_file(path, error)) return result;
    std::ifstream input(custom_steam_library::ioPath(path), std::ios::binary);
    if (!input) return result;
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > (8ull << 20)) return result;
    input.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) return result;
    if (bytes.size() >= 24 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G' &&
        bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) {
        result.width = (static_cast<int>(bytes[16]) << 24) | (static_cast<int>(bytes[17]) << 16) |
            (static_cast<int>(bytes[18]) << 8) | bytes[19];
        result.height = (static_cast<int>(bytes[20]) << 24) | (static_cast<int>(bytes[21]) << 16) |
            (static_cast<int>(bytes[22]) << 8) | bytes[23];
        result.known = result.width > 0 && result.height > 0;
        return result;
    }
    if (bytes.size() >= 4 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        size_t pos = 2;
        while (pos + 4 < bytes.size()) {
            while (pos < bytes.size() && bytes[pos] != 0xFF) ++pos;
            while (pos < bytes.size() && bytes[pos] == 0xFF) ++pos;
            if (pos >= bytes.size()) break;
            const unsigned char marker = bytes[pos++];
            if (marker == 0xD8 || marker == 0xD9 || marker == 0x01) continue;
            if (pos + 2 > bytes.size()) break;
            const size_t length = (static_cast<size_t>(bytes[pos]) << 8) | bytes[pos + 1];
            if (length < 2 || pos + length > bytes.size()) break;
            const bool sof = (marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
                (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF);
            if (sof && length >= 7) {
                result.height = (static_cast<int>(bytes[pos + 3]) << 8) | bytes[pos + 4];
                result.width = (static_cast<int>(bytes[pos + 5]) << 8) | bytes[pos + 6];
                result.known = result.width > 0 && result.height > 0;
                return result;
            }
            pos += length;
        }
    }
    return result;
}

static json artworkSizeRule(const std::string& type) {
    if (type == "wallpaper") return {{"minimumWidth", 800}, {"minimumHeight", 250}, {"preferredWidth", 1920}, {"preferredHeight", 620}};
    if (type == "long") return {{"minimumWidth", 300}, {"minimumHeight", 140}, {"preferredWidth", 920}, {"preferredHeight", 430}};
    return {{"minimumWidth", 240}, {"minimumHeight", 360}, {"preferredWidth", 600}, {"preferredHeight", 900}};
}

static std::string safeArtworkSegment(std::string value) {
    for (auto& ch : value) {
        const auto ok = std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_';
        if (!ok) ch = '_';
    }
    if (value.empty()) value = "item";
    return value;
}

static std::string artworkGameKey(const fs::path& executable) {
    const auto normalized = toUtf8(normalizedPathKey(executable));
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char ch : normalized) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    std::ostringstream result;
    result << std::hex << hash;
    return result.str();
}

static int64_t unixTimeMsHost();

static fs::path hostCentralBackupDirectory(const fs::path& path) {
    if (g_dataRoot.empty()) return path.parent_path() / L".backups";
    std::error_code error;
    const auto relative = path.lexically_relative(g_dataRoot);
    if (error || relative.empty() || relative.is_absolute()) return g_dataRoot / L"backups" / L"runtime";
    for (const auto& part : relative) if (part == L"..") return g_dataRoot / L"backups" / L"runtime";
    const auto section = relative.parent_path().filename().wstring();
    return g_dataRoot / L"backups" /
        ((section == L"config" || section == L"state") ? section : L"runtime");
}

static fs::path nextHostBackupPath(const fs::path& directory, const fs::path& target) {
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) throw std::runtime_error("无法创建配置备份目录");
    const auto prefix = target.stem().wstring() + L"-" + std::to_wstring(unixTimeMsHost()) + L"-";
    for (uint64_t attempt = 0; attempt < 1000; ++attempt) {
        const auto sequence = g_jsonBackupSequence.fetch_add(1) + 1;
        const auto candidate = directory / (prefix + std::to_wstring(sequence) + L".json");
        if (!fs::exists(candidate, error)) return candidate;
        error.clear();
    }
    throw std::runtime_error("无法生成配置备份文件名");
}

struct HostStagedFile {
    fs::path path;
    ~HostStagedFile() noexcept { try { std::error_code error; if (!path.empty()) fs::remove(custom_steam_library::ioPath(path), error); } catch (...) {} }
};

static fs::path hostTemporaryPath(const fs::path& target) {
    return target.parent_path() / (target.filename().wstring() + L".tmp-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(g_jsonBackupSequence.fetch_add(1) + 1));
}

static bool hostTransientFileError(DWORD error, const fs::path& target) {
    if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED &&
        error != ERROR_LOCK_VIOLATION) return false;
    const auto attributes = GetFileAttributesW(custom_steam_library::ioPath(target).c_str());
    return attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY));
}

static void activateHostStagedFile(const fs::path& staged, const fs::path& target) {
    DWORD error = ERROR_SUCCESS;
    for (int attempt = 1; attempt <= 20; ++attempt) {
        if (MoveFileExW(custom_steam_library::ioPath(staged).c_str(), custom_steam_library::ioPath(target).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
        error = GetLastError();
        if (!hostTransientFileError(error, target)) break;
        if (g_shuttingDown.load()) break;
        if (attempt < 20) Sleep(static_cast<DWORD>((std::min)(attempt * 60, 720)));
    }
    throw std::runtime_error("原子文件替换失败：" + toUtf8(target.wstring()) +
        "（系统错误码：" + std::to_string(error) + "）；原文件未主动删除或截断，请检查持续占用、只读属性或目录权限");
}

class HostDataTransaction {
    HANDLE mutex_ = nullptr;
public:
    HostDataTransaction() {
        mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\YeManCustomSteamLibraryDataTransaction");
        if (!mutex_) throw std::runtime_error("无法创建自定义游戏库数据互斥体");
        const auto wait = WaitForSingleObject(mutex_, 30000);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
            CloseHandle(mutex_); mutex_ = nullptr;
            throw std::runtime_error("等待自定义游戏库数据事务超时，未覆盖原文件");
        }
    }
    HostDataTransaction(const HostDataTransaction&) = delete;
    HostDataTransaction& operator=(const HostDataTransaction&) = delete;
    void release() { if (mutex_) { ReleaseMutex(mutex_); CloseHandle(mutex_); mutex_ = nullptr; } }
    ~HostDataTransaction() { release(); }
};

static void copyHostFileAtomic(const fs::path& source, const fs::path& target) {
    if (samePath(source, target)) {
        std::error_code sameError;
        if (fs::is_regular_file(custom_steam_library::ioPath(source), sameError) && !sameError) return;
    }
    std::error_code error;
    fs::create_directories(custom_steam_library::ioPath(target.parent_path()), error);
    if (error) throw std::runtime_error("无法创建文件目录：" + toUtf8(target.parent_path().wstring()));
    HostStagedFile staged{hostTemporaryPath(target)};
    for (int attempt = 1; ; ++attempt) {
        if (CopyFileW(custom_steam_library::ioPath(source).c_str(), custom_steam_library::ioPath(staged.path).c_str(), FALSE)) break;
        const auto code = GetLastError();
        if (attempt >= 20 || !hostTransientFileError(code, source)) {
            throw std::runtime_error("无法准备文件副本：" + toUtf8(source.wstring()) +
                "（系统错误码：" + std::to_string(code) + "）；原文件已保留");
        }
        Sleep(static_cast<DWORD>((std::min)(attempt * 60, 720)));
    }
    activateHostStagedFile(staged.path, target);
}

static void writeHostBytesAtomic(const fs::path& target, std::string_view bytes) {
    std::error_code error;
    fs::create_directories(custom_steam_library::ioPath(target.parent_path()), error);
    if (error) throw std::runtime_error("无法创建配置备份目录");
    HostStagedFile staged{hostTemporaryPath(target)};
    writeHostBytes(staged.path, bytes);
    activateHostStagedFile(staged.path, target);
}

static void writeJsonWithLocalBackup(
    const fs::path& path, const json& document, bool writeCentralBackup = true) {
    const auto text = document.dump(2) + '\n';
    // Take the cross-process lock first: callers may already own it for RMW.
    // The reverse order deadlocks an RMW caller with a standalone cache writer.
    HostDataTransaction transaction;
    std::lock_guard<std::mutex> lock(g_jsonWriteMutex);
    std::error_code error;
    fs::create_directories(custom_steam_library::ioPath(path.parent_path()), error);
    if (error) throw std::runtime_error("无法创建配置目录");
    const bool exists = hostRegularFile(path);
    const auto before = exists ? readText(path) : std::string{};
    bool valid = false;
    if (exists) {
        try { const auto parsed = json::parse(before); valid = hostConfigDocumentValid(path, parsed); }
        catch (const json::parse_error&) { }
    }
    HostStagedFile temporary{hostTemporaryPath(path)};
    writeHostBytes(temporary.path, text);
    if (exists && valid) {
        const auto adjacent = path.parent_path() / (path.filename().wstring() + L".bak");
        // Back up the bytes we actually validated, not a second, potentially
        // changed read. Both recovery copies are atomic and checked.
        writeHostBytesAtomic(adjacent, before);
        if (writeCentralBackup)
            writeHostBytesAtomic(nextHostBackupPath(hostCentralBackupDirectory(path), path), before);
    } else if (exists) {
        const auto archive = (g_dataRoot.empty() ? path.parent_path() / L".backups" : g_dataRoot / L"backups") / L"corrupt";
        writeHostBytesAtomic(nextHostBackupPath(archive, path), before);
    }
    activateHostStagedFile(temporary.path, path);
}

static void appendUiError(const json& payload) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_uiErrorMutex);
        const auto path = g_dataRoot / L"state" / L"ui-errors.jsonl";
        std::error_code error;
        fs::create_directories(path.parent_path(), error);
        if (error) return;
        std::ofstream output(custom_steam_library::ioPath(path), std::ios::binary | std::ios::app);
        if (!output) return;
        auto record = payload.is_object() ? payload : json::object();
        record["recordedAt"] = unixTimeMsHost();
        record["processId"] = GetCurrentProcessId();
        output << record.dump() << '\n';
    } catch (...) {
        // Diagnostics must never become a new source of UI failure.
    }
}

static fs::path copyArtworkToIsolation(const fs::path& source, const std::string& gameKey, const std::string& type) {
    std::error_code error;
    if (!fs::is_regular_file(source, error)) throw std::runtime_error("指定图片不存在");
    if (!supportedArtworkFile(source)) throw std::runtime_error("仅支持 JPG、PNG、WEBP、BMP 或 ICO 图片");
    const auto size = fs::file_size(source, error);
    if (error || size == 0 || size > (64ull << 20)) throw std::runtime_error("图片大小必须在 1 字节到 64 MB 之间");
    const auto dimensions = inspectArtworkDimensions(source);
    const auto sizeRule = artworkSizeRule(type);
    if (dimensions.known && (dimensions.width < sizeRule.value("minimumWidth", 0) || dimensions.height < sizeRule.value("minimumHeight", 0))) {
        throw std::runtime_error(type == "wallpaper"
            ? "壁纸尺寸过小，至少需要 800x250；请重新选择更大的图片"
            : type == "long" ? "横版封面尺寸过小，至少需要 300x140；请重新选择更大的图片"
            : "封面尺寸过小，至少需要 240x360；请重新选择更大的图片");
    }
    const auto directory = g_dataRoot / L"artwork" / L"overrides" / toWide(safeArtworkSegment(gameKey));
    fs::create_directories(directory, error);
    if (error) throw std::runtime_error("无法创建隔离素材目录");
    const auto fileName = toWide(type + lowerAscii(toUtf8(source.extension().wstring())));
    const auto destination = directory / fileName;
    copyHostFileAtomic(source, destination);
    return destination;
}

static std::string urlEncodeUtf8(const std::string& value) {
    std::ostringstream encoded;
    static constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') encoded << static_cast<char>(ch);
        else encoded << '%' << hex[ch >> 4] << hex[ch & 15];
    }
    return encoded.str();
}

static int64_t unixTimeMsHost() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Keep field diagnostics next to the portable package, rather than inside the
// data root.  This makes a failed scrape on another PC easy to collect without
// also copying its artwork/cache data.
static fs::path scrapeHistoryRoot() {
    return g_labRoot / L"logs" / L"scrape-history";
}

static fs::path hostCrashRoot() {
    return g_labRoot / L"logs" / L"host-crash";
}

static fs::path hostExitRoot() {
    return g_labRoot / L"logs" / L"host-exit";
}

static std::string diagnosticJsonString(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const unsigned char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character < 0x20) {
                    static constexpr char hex[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += hex[character >> 4];
                    escaped += hex[character & 15];
                } else {
                    escaped += static_cast<char>(character);
                }
        }
    }
    return escaped;
}

static void pruneHostDiagnosticRecords(const fs::path& root) noexcept {
    constexpr size_t kRetainedHostCrashRecords = 3;
    try {
        std::error_code error;
        if (!fs::is_directory(root, error)) return;
        std::vector<fs::directory_entry> records;
        for (const auto& entry : fs::directory_iterator(root, error)) {
            if (error) { error.clear(); continue; }
            if (entry.is_regular_file(error) && entry.path().extension() == L".json") records.push_back(entry);
            error.clear();
        }
        std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
            std::error_code leftError;
            std::error_code rightError;
            const auto leftTime = left.last_write_time(leftError);
            const auto rightTime = right.last_write_time(rightError);
            if (leftError != rightError) return !leftError;
            if (!leftError && leftTime != rightTime) return leftTime > rightTime;
            return left.path().filename().wstring() > right.path().filename().wstring();
        });
        for (size_t index = kRetainedHostCrashRecords; index < records.size(); ++index) {
            fs::remove(records[index].path(), error);
            error.clear();
        }
    } catch (...) {
        // Crash reporting must not mask the original failure.
    }
}

static void writeHostCrashRecord(
    std::string_view kind, DWORD exceptionCode = 0, std::string_view detail = {}) noexcept {
    bool expected = false;
    if (!g_hostCrashReportInProgress.compare_exchange_strong(expected, true)) return;
    try {
        if (g_labRoot.empty()) return;
        std::error_code error;
        const auto root = hostCrashRoot();
        fs::create_directories(root, error);
        if (error) return;
        const auto path = root /
            (L"crash-" + std::to_wstring(unixTimeMsHost()) + L"-" +
             std::to_wstring(GetCurrentProcessId()) + L".json");
        std::ofstream output(custom_steam_library::ioPath(path), std::ios::binary | std::ios::trunc);
        if (!output) return;
        output << "{\n"
            << "  \"schemaVersion\": 1,\n"
            << "  \"kind\": \"" << diagnosticJsonString(kind) << "\",\n"
            << "  \"at\": " << unixTimeMsHost() << ",\n"
            << "  \"processId\": " << GetCurrentProcessId() << ",\n"
            << "  \"threadId\": " << GetCurrentThreadId() << ",\n"
            << "  \"exceptionCode\": " << exceptionCode << ",\n"
            << "  \"detail\": \"" << diagnosticJsonString(detail) << "\"\n"
            << "}\n";
        output.flush();
        if (output.good()) pruneHostDiagnosticRecords(root);
    } catch (...) {
        // An out-of-memory or damaged filesystem failure is not recoverable
        // here, but the original process failure must continue unmodified.
    }
}

static void writeHostExitRecord(std::string_view fallbackKind = "wm-close") noexcept {
    bool expected = false;
    if (!g_hostExitRecordWritten.compare_exchange_strong(expected, true)) return;
    try {
        if (g_labRoot.empty()) return;
        std::string kind(fallbackKind);
        std::string detail;
        {
            std::lock_guard<std::mutex> lock(g_hostExitReasonMutex);
            if (!g_hostExitKind.empty()) kind = g_hostExitKind;
            detail = g_hostExitDetail;
        }
        std::error_code error;
        const auto root = hostExitRoot();
        fs::create_directories(root, error);
        if (error) return;
        const auto path = root /
            (L"exit-" + std::to_wstring(unixTimeMsHost()) + L"-" +
             std::to_wstring(GetCurrentProcessId()) + L".json");
        std::ofstream output(custom_steam_library::ioPath(path), std::ios::binary | std::ios::trunc);
        if (!output) return;
        output << "{\n"
            << "  \"schemaVersion\": 1,\n"
            << "  \"kind\": \"" << diagnosticJsonString(kind) << "\",\n"
            << "  \"detail\": \"" << diagnosticJsonString(detail) << "\",\n"
            << "  \"at\": " << unixTimeMsHost() << ",\n"
            << "  \"processId\": " << GetCurrentProcessId() << ",\n"
            << "  \"scrapeQueueRunning\": " << (g_scrapeQueueRunning.load() ? "true" : "false") << "\n"
            << "}\n";
        output.flush();
        if (output.good()) pruneHostDiagnosticRecords(root);
    } catch (...) {
        // A normal close must remain available even if diagnostics fail.
    }
}

static std::string activeCppExceptionDetail() noexcept {
    try {
        const auto exception = std::current_exception();
        if (!exception) return "no active C++ exception";
        std::rethrow_exception(exception);
    } catch (const std::exception& error) {
        return error.what();
    } catch (...) {
        return "non-standard C++ exception";
    }
}

static void hostTerminateHandler() noexcept {
    writeHostCrashRecord("std-terminate", 0, activeCppExceptionDetail());
    std::abort();
}

static LONG WINAPI hostUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers) {
    const DWORD code = exceptionPointers && exceptionPointers->ExceptionRecord
        ? exceptionPointers->ExceptionRecord->ExceptionCode : 0;
    writeHostCrashRecord("unhandled-seh", code, "unhandled Windows exception");
    return EXCEPTION_CONTINUE_SEARCH;
}

static std::string retainedDiagnosticOutput(const std::string& output, size_t maxBytes) {
    if (output.size() <= maxBytes) return output;
    const auto omitted = output.size() - maxBytes;
    return "[earlier worker output omitted: " + std::to_string(omitted) + " bytes]\n" +
        output.substr(omitted);
}

static bool writeScrapeHistoryRecord(const fs::path& path, const json& document) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_jsonWriteMutex);
        std::error_code error;
        fs::create_directories(path.parent_path(), error);
        if (error) return false;
        const auto temporary = path.parent_path() /
            (path.filename().wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(g_jsonBackupSequence.fetch_add(1) + 1));
        {
            std::ofstream output(custom_steam_library::ioPath(temporary), std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output << document.dump(2) << '\n';
            output.flush();
            if (!output.good()) return false;
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

static void pruneScrapeHistory() noexcept {
    constexpr size_t kRetainedScrapeHistoryRounds = 3;
    try {
        std::lock_guard<std::mutex> lock(g_jsonWriteMutex);
        std::error_code error;
        const auto root = scrapeHistoryRoot();
        if (!fs::is_directory(root, error)) return;
        std::vector<fs::directory_entry> records;
        for (const auto& entry : fs::directory_iterator(root, error)) {
            if (error) { error.clear(); continue; }
            const auto name = entry.path().filename().wstring();
            if (entry.is_regular_file(error) && entry.path().extension() == L".json" &&
                name.rfind(L"round-", 0) == 0) {
                records.push_back(entry);
            }
            error.clear();
        }
        std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) {
            std::error_code leftError;
            std::error_code rightError;
            const auto leftTime = left.last_write_time(leftError);
            const auto rightTime = right.last_write_time(rightError);
            if (leftError != rightError) return !leftError;
            if (!leftError && leftTime != rightTime) return leftTime > rightTime;
            return left.path().filename().wstring() > right.path().filename().wstring();
        });
        for (size_t index = kRetainedScrapeHistoryRounds; index < records.size(); ++index) {
            fs::remove(records[index].path(), error);
            error.clear();
        }
    } catch (...) {
        // History retention is diagnostic-only and must not affect scraping.
    }
}

static void markInterruptedScrapeHistory() noexcept {
    try {
        std::error_code error;
        const auto root = scrapeHistoryRoot();
        if (!fs::is_directory(root, error)) return;
        for (const auto& entry : fs::directory_iterator(root, error)) {
            if (error) { error.clear(); continue; }
            if (!entry.is_regular_file(error) || entry.path().extension() != L".json") {
                error.clear();
                continue;
            }
            const auto record = readJson(entry.path(), json::object());
            if (!record.is_object() || jsonStringOr(record, "status") != "running") continue;
            auto interrupted = record;
            interrupted["status"] = "interrupted";
            interrupted["finishedAt"] = unixTimeMsHost();
            interrupted["interruptionReason"] = "宿主进程在本轮刮削完成前退出";
            writeScrapeHistoryRecord(entry.path(), interrupted);
        }
    } catch (...) {
        // Retention diagnostics must never interfere with a new scrape.
    }
}

static void markScrapeHistoryFailed(const fs::path& path, std::string_view error) noexcept {
    try {
        if (path.empty()) return;
        auto record = readJson(path, json::object());
        if (!record.is_object()) record = json::object();
        record["status"] = "failed";
        record["finishedAt"] = unixTimeMsHost();
        record["error"] = error;
        record["failureSource"] = "host-scrape-queue-thread";
        writeScrapeHistoryRecord(path, record);
    } catch (...) {
        // The host-crash record remains available if the batch record cannot
        // be updated after a scheduler failure.
    }
}

static json compactFailureRecord(const json& failure) {
    json compact = json::object();
    if (!failure.is_object()) return compact;
    for (const auto* key : {"executable", "gameDirectory", "identificationTarget", "failure", "networkRetry"}) {
        const auto value = failure.find(key);
        if (value != failure.end()) compact[key] = *value;
    }
    const auto arbitration = failure.find("arbitration");
    if (arbitration != failure.end() && arbitration->is_object()) {
        json summary = json::object();
        for (const auto* key : {"decision", "resolverVersion"}) {
            const auto value = arbitration->find(key);
            if (value != arbitration->end()) summary[key] = *value;
        }
        compact["arbitration"] = std::move(summary);
    }
    const auto candidates = failure.find("manualReviewCandidates");
    if (candidates != failure.end() && candidates->is_array()) {
        json shortlist = json::array();
        for (const auto& candidate : *candidates) {
            if (!candidate.is_object() || shortlist.size() >= 3) continue;
            json summary = json::object();
            for (const auto* key : {"name", "steamAppId", "igdbId", "provider", "score"}) {
                const auto value = candidate.find(key);
                if (value != candidate.end()) summary[key] = *value;
            }
            shortlist.push_back(std::move(summary));
        }
        if (!shortlist.empty()) compact["manualReviewCandidates"] = std::move(shortlist);
    }
    return compact;
}

static json scrapeArtifactSummary(const fs::path& outputDirectory, const std::string& executable) {
    json summary = {{"outputDirectory", toUtf8(outputDirectory.wstring())}};
    try {
        std::vector<fs::path> manifestPaths;
        std::error_code error;
        if (fs::is_regular_file(outputDirectory / L"manifest.json", error)) {
            manifestPaths.push_back(outputDirectory / L"manifest.json");
        }
        error.clear();
        if (fs::is_directory(outputDirectory, error)) {
            for (const auto& entry : fs::recursive_directory_iterator(outputDirectory, error)) {
                if (error) { error.clear(); continue; }
                if (entry.is_regular_file(error) && entry.path().filename() == L"manifest.json" &&
                    entry.path() != outputDirectory / L"manifest.json") {
                    manifestPaths.push_back(entry.path());
                }
                error.clear();
            }
        }
        std::sort(manifestPaths.begin(), manifestPaths.end(), [](const auto& left, const auto& right) {
            std::error_code leftError;
            std::error_code rightError;
            const auto leftTime = fs::last_write_time(left, leftError);
            const auto rightTime = fs::last_write_time(right, rightError);
            if (leftError != rightError) return !leftError;
            if (!leftError && leftTime != rightTime) return leftTime > rightTime;
            return left.wstring() > right.wstring();
        });
        fs::path manifestPath;
        json manifest;
        for (const auto& candidate : manifestPaths) {
            const auto document = readJson(candidate, json::object());
            if (!document.is_object()) continue;
            const auto manifestExe = jsonStringOr(document, "exe");
            if (!executable.empty() && !manifestExe.empty() &&
                !samePath(fs::path(toWide(manifestExe)), fs::path(toWide(executable)))) continue;
            manifestPath = candidate;
            manifest = document;
            break;
        }
        if (manifestPath.empty()) {
            const auto failedPath = outputDirectory / L"rounds-failed.json";
            if (fs::is_regular_file(failedPath, error)) {
                const auto failure = readJson(failedPath, json::object());
                if (failure.is_object()) {
                    summary["failureRecordPath"] = toUtf8(failedPath.wstring());
                    summary["failure"] = compactFailureRecord(failure);
                }
            }
            return summary;
        }
        summary["manifestPath"] = toUtf8(manifestPath.wstring());
        if (!manifest.is_object()) return summary;

        json manifestSummary = json::object();
        for (const auto* key : {"ok", "metadataOnly", "successCount", "required", "failure", "networkRetry"}) {
            const auto value = manifest.find(key);
            if (value != manifest.end()) manifestSummary[key] = *value;
        }
        const auto match = manifest.find("match");
        if (match != manifest.end() && match->is_object()) {
            json matchSummary = json::object();
            for (const auto* key : {"appId", "storefrontAppId", "igdbId", "displayName", "formalName",
                                    "identityStatus", "steamVerificationStatus", "primaryProvider", "score",
                                    "resolverVersion", "source"}) {
                const auto value = match->find(key);
                if (value != match->end()) matchSummary[key] = *value;
            }
            manifestSummary["match"] = std::move(matchSummary);
        }
        const auto policy = manifest.find("networkPolicy");
        if (policy != manifest.end() && policy->is_object()) {
            const auto diagnostics = policy->find("acceleratorDiagnostics");
            if (diagnostics != policy->end()) manifestSummary["acceleratorDiagnostics"] = *diagnostics;
        }
        const auto artwork = manifest.find("artwork");
        if (artwork != manifest.end() && artwork->is_array()) {
            json artworkSummary = json::array();
            for (const auto& item : *artwork) {
                if (!item.is_object()) continue;
                json itemSummary = json::object();
                for (const auto* key : {"type", "ok", "provider", "bytes", "width", "height",
                                        "candidateCount", "lowQualityOnlyCandidate", "networkRetry"}) {
                    const auto value = item.find(key);
                    if (value != item.end()) itemSummary[key] = *value;
                }
                json network = json::array();
                const auto attempts = item.find("attempts");
                if (attempts != item.end() && attempts->is_array()) {
                    size_t candidateCount = 0;
                    for (const auto& attempt : *attempts) {
                        if (!attempt.is_object() || candidateCount++ >= 8) continue;
                        const auto networkAttempts = attempt.find("networkAttempts");
                        if (networkAttempts == attempt.end() || !networkAttempts->is_array()) continue;
                        for (const auto& networkAttempt : *networkAttempts) {
                            if (!networkAttempt.is_object() || network.size() >= 16) continue;
                            json networkSummary = json::object();
                            for (const auto* key : {"provider", "networkRoute", "fallbackMode", "status", "ok",
                                                    "elapsedMs", "error", "winHttpError", "win32Error", "reason"}) {
                                const auto value = networkAttempt.find(key);
                                if (value != networkAttempt.end()) networkSummary[key] = *value;
                            }
                            const auto health = networkAttempt.find("acceleratorHealth");
                            if (health != networkAttempt.end()) networkSummary["acceleratorHealth"] = *health;
                            network.push_back(std::move(networkSummary));
                        }
                    }
                }
                if (!network.empty()) itemSummary["networkAttempts"] = std::move(network);
                artworkSummary.push_back(std::move(itemSummary));
            }
            manifestSummary["artwork"] = std::move(artworkSummary);
        }
        summary["manifest"] = std::move(manifestSummary);
    } catch (...) {
        summary["artifactReadError"] = true;
    }
    return summary;
}

static void persistResolvedIdentityCache(const fs::path& manifestPath) {
    const auto manifest = readJson(manifestPath, json::object());
    const auto executable = jsonStringOr(manifest, "exe");
    const auto matchIt = manifest.find("match");
    if (executable.empty() || matchIt == manifest.end() || !matchIt->is_object()) return;
    const auto& match = *matchIt;
    const auto appId = jsonIntegerOr(match, "storefrontAppId", jsonIntegerOr(match, "appId", 0));
    const auto igdbId = jsonIntegerOr(match, "igdbId", 0);
    const auto formalName = jsonStringOr(match, "formalName", jsonStringOr(match, "displayName"));
    if (appId <= 0 && igdbId <= 0 && formalName.empty()) return;
    const auto cachePath = g_dataRoot / L"cache" / L"identity" / toWide(safeArtworkSegment(toUtf8(normalizedPathKey(fs::path(toWide(executable))))) + ".json");
    const auto previous = readJson(cachePath, json::object());
    if (custom_steam_library::verifiedSteamIdentityMatch(jsonObjectOr(previous, "match")) &&
        !custom_steam_library::verifiedSteamIdentityMatch(match)) return;
    json value = {{"executable", executable}, {"manifestPath", toUtf8(manifestPath.wstring())},
        {"updatedAt", unixTimeMsHost()}, {"match", match}, {"metadata", jsonObjectOr(manifest, "metadata")}};
    if (!formalName.empty()) value["formalName"] = formalName;
    if (appId > 0) value["steamAppId"] = appId;
    if (igdbId > 0) value["igdbId"] = igdbId;
    writeJsonWithLocalBackup(cachePath, value);
}

static bool persistResolvedIdentityFromOutput(
    const fs::path& outputDirectory, const std::string& executable) {
    std::error_code error;
    for (const auto& entry : fs::recursive_directory_iterator(outputDirectory, error)) {
        if (error) { error.clear(); break; }
        if (!entry.is_regular_file(error) || entry.path().filename() != L"manifest.json") {
            error.clear();
            continue;
        }
        const auto manifest = readJson(entry.path(), json::object());
        if (samePath(fs::path(toWide(jsonStringOr(manifest, "exe"))),
                     fs::path(toWide(executable)))) {
            persistResolvedIdentityCache(entry.path());
            return true;
        }
        error.clear();
    }
    return false;
}

static int outputVerifiedSteamAppId(
    const fs::path& outputDirectory, const std::string& executable) {
    std::error_code error;
    for (const auto& entry : fs::recursive_directory_iterator(outputDirectory, error)) {
        if (error) { error.clear(); break; }
        if (!entry.is_regular_file(error) || entry.path().filename() != L"manifest.json") {
            error.clear();
            continue;
        }
        const auto manifest = readJson(entry.path(), json::object());
        if (samePath(fs::path(toWide(jsonStringOr(manifest, "exe"))),
                     fs::path(toWide(executable)))) {
            const auto match = jsonObjectOr(manifest, "match");
            const auto provider = jsonStringOr(match, "primaryProvider");
            const auto status = jsonStringOr(match, "steamVerificationStatus");
            const auto appId = jsonIntegerOr(match, "appId",
                jsonIntegerOr(match, "storefrontAppId", 0));
            if (provider == "steam" && status == "steam-verified" && appId > 0) {
                return static_cast<int>(appId);
            }
        }
        error.clear();
    }
    return 0;
}

static uint64_t outputResolvedIgdbId(
    const fs::path& outputDirectory, const std::string& executable) {
    std::error_code error;
    for (const auto& entry : fs::recursive_directory_iterator(outputDirectory, error)) {
        if (error) { error.clear(); break; }
        if (!entry.is_regular_file(error) || entry.path().filename() != L"manifest.json") {
            error.clear();
            continue;
        }
        const auto manifest = readJson(entry.path(), json::object());
        if (samePath(fs::path(toWide(jsonStringOr(manifest, "exe"))),
                     fs::path(toWide(executable)))) {
            const auto match = jsonObjectOr(manifest, "match");
            const auto igdbId = jsonIntegerOr(match, "igdbId", 0);
            if (igdbId > 0) return static_cast<uint64_t>(igdbId);
        }
        error.clear();
    }
    return 0;
}

static bool outputHasVerifiedSteamIdentity(
    const fs::path& outputDirectory, const std::string& executable) {
    return outputVerifiedSteamAppId(outputDirectory, executable) > 0;
}
static void attachResolvedIdentityCache(json& snapshot) {
    json byExecutable = json::object();
    const auto identityCacheRoot = g_dataRoot / L"cache" / L"identity";
    std::vector<fs::path> roots{identityCacheRoot};
    std::error_code error;
    bool hasDurableIdentityCache = false;
    if (fs::is_directory(identityCacheRoot, error)) {
        for (const auto& entry : fs::directory_iterator(identityCacheRoot, error)) {
            if (error) { error.clear(); break; }
            if (entry.is_regular_file(error) && entry.path().extension() == L".json") {
                hasDurableIdentityCache = true;
                break;
            }
            error.clear();
        }
    }
    if (!hasDurableIdentityCache) {
        // One migration read preserves pre-index results. Once current jobs
        // have populated cache/identity, never recursively parse every old
        // worker artifact during ordinary startup or queue dispatch.
        roots.insert(roots.end(), {
            workspaceJobsRoot() / L"library-batch-identification",
            workspaceJobsRoot() / L"workspace-identification",
            g_labRoot / L"output" / L"library-batch-identification",
            g_labRoot / L"output" / L"workspace-identification"
        });
    }
    for (const auto& root : roots) {
        error.clear();
        if (!fs::is_directory(root, error)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(root, error)) {
            if (error) { error.clear(); break; }
            if (!entry.is_regular_file(error)) { error.clear(); continue; }
            const bool cachedIdentity = entry.path().extension() == L".json" && entry.path().filename() != L"manifest.json";
            if (!cachedIdentity && entry.path().filename() != L"manifest.json") { error.clear(); continue; }
            try {
                const auto document = readJson(entry.path(), json::object());
                const auto executable = jsonStringOr(document, "executable", jsonStringOr(document, "exe"));
                const auto matchIt = document.find("match");
                json match = matchIt != document.end() && matchIt->is_object() ? *matchIt : document;
                if (cachedIdentity && (matchIt == document.end() || !matchIt->is_object())) {
                    const auto source = readJson(fs::path(toWide(jsonStringOr(document, "manifestPath"))), json::object());
                    if (!executable.empty() && samePath(fs::path(toWide(executable)), fs::path(toWide(jsonStringOr(source, "exe")))))
                        match = jsonObjectOr(source, "match");
                }
                if (executable.empty() || !match.is_object() ||
                    custom_steam_library::builtinExcludedSteamTool({{"executable", executable}, {"match", match}})) {
                    error.clear(); continue;
                }
                const auto appId = jsonIntegerOr(match, "steamAppId", jsonIntegerOr(match, "storefrontAppId", jsonIntegerOr(match, "appId", 0)));
                const auto igdbId = jsonIntegerOr(match, "igdbId", 0);
                const auto formalName = jsonStringOr(match, "formalName", jsonStringOr(match, "displayName"));
                const auto provider = jsonStringOr(match, "primaryProvider");
                const auto identityStatus = jsonStringOr(match, "identityStatus");
                const auto steamVerificationStatus = jsonStringOr(match, "steamVerificationStatus");
                if (appId <= 0 && igdbId <= 0 && formalName.empty()) { error.clear(); continue; }
                const auto key = toUtf8(normalizedPathKey(fs::path(toWide(executable))));
                json value = match;
                value["executable"] = executable;
                value["manifestPath"] = toUtf8(entry.path().wstring());
                if (!formalName.empty()) value["formalName"] = formalName;
                if (appId > 0) value["steamAppId"] = appId;
                if (igdbId > 0) value["igdbId"] = igdbId;
                if (!provider.empty()) value["primaryProvider"] = provider;
                if (!identityStatus.empty()) value["identityStatus"] = identityStatus;
                if (!steamVerificationStatus.empty()) value["steamVerificationStatus"] = steamVerificationStatus;
                const auto prior = byExecutable.find(key);
                if (prior == byExecutable.end() || !custom_steam_library::verifiedSteamIdentityMatch(*prior) ||
                    custom_steam_library::verifiedSteamIdentityMatch(value)) byExecutable[key] = std::move(value);
            } catch (...) {
                // Cache and historical manifests are optional; malformed files
                // must never stop the isolated workspace from starting.
            }
            error.clear();
        }
    }
    snapshot["resolvedIdentities"] = std::move(byExecutable);
}
static void attachFailureCandidates(json& snapshot) {
    json byExecutable = json::object();
    std::map<std::string, fs::file_time_type> newestByExecutable;
    std::vector<fs::path> roots;
    roots.push_back(g_dataRoot / L"state" / L"failures");
    const auto outputRoot = workspaceJobsRoot();
    std::error_code error;
    if (fs::is_directory(outputRoot, error)) {
        for (const auto& entry : fs::directory_iterator(outputRoot, error)) {
            if (error) { error.clear(); break; }
            if (entry.is_directory(error)) roots.push_back(entry.path() / L"failures");
            error.clear();
        }
    }
    for (const auto& root : roots) {
        error.clear();
        if (!fs::is_directory(root, error)) continue;
        for (const auto& entry : fs::directory_iterator(root, error)) {
            if (error) { error.clear(); continue; }
            if (!entry.is_regular_file(error) || entry.path().extension() != L".json") { error.clear(); continue; }
            const auto document = readJson(entry.path(), json::object());
            const auto executable = jsonStringOr(document, "executable");
            const auto candidates = jsonArrayOr(document, "manualReviewCandidates");
            if (!executable.empty() && candidates.is_array() && !candidates.empty()) {
                const auto key = toUtf8(normalizedPathKey(fs::path(toWide(executable))));
                std::error_code timeError;
                const auto modified = fs::last_write_time(entry.path(), timeError);
                const auto existing = newestByExecutable.find(key);
                if (existing != newestByExecutable.end() && !timeError && modified <= existing->second) {
                    error.clear();
                    continue;
                }
                if (!timeError) newestByExecutable[key] = modified;
                byExecutable[key] = {
                    {"executable", executable},
                    {"failureFile", toUtf8(entry.path().wstring())},
                    {"candidates", candidates}
                };
            }
            error.clear();
        }
    }
    snapshot["identityCandidates"] = std::move(byExecutable);
}
static json manualArtworkOverrideForExecutable(const json& overrides, const std::string& executable);
static void attachExistingSteamArtwork(json& plan) {
    if (!plan.contains("items") || !plan["items"].is_array()) return;
    const auto overrides = readJson(g_dataRoot / L"config" / L"manual-overrides.json", json::object());
    for (auto& item : plan["items"]) {
        const auto status = jsonStringOr(item, "status");
        if (status != "already-in-steam" && status != "added-to-steam") continue;
        const auto gridText = jsonStringOr(item, "gridDirectory");
        if (gridText.empty()) continue;
        const auto grid = fs::path(toWide(gridText));
        const auto shortId = jsonStringOr(item, "shortAppId");
        if (shortId.empty()) continue;
        std::string accountId = jsonStringOr(item, "accountId");
        if (accountId.empty()) {
            const auto parent = grid.parent_path().parent_path().filename().wstring();
            accountId = toUtf8(parent);
        }
        const auto cacheKey = safeArtworkSegment(accountId.empty() ? "steam" : accountId) + "_" + safeArtworkSegment(shortId);
        const auto cacheDirectory = g_dataRoot / L"cache" / L"existing-steam-artwork" / toWide(cacheKey);
        std::error_code cacheError;
        fs::create_directories(cacheDirectory, cacheError);
        json previews = jsonObjectOr(item, "artworkPreview");
        const auto protection = jsonObjectOr(manualArtworkOverrideForExecutable(overrides,
            jsonStringOr(item, "primaryExecutable")), "artworkProtection");
        const std::array<std::pair<std::string, std::string>, 5> types = {{
            {"tall", shortId + "p"}, {"long", shortId}, {"hero", shortId + "_hero"},
            {"logo", shortId + "_logo"}, {"icon", shortId + "_icon"}
        }};
        for (const auto& [type, stem] : types) {
            const auto slot = type == "tall" ? "cover" : type == "hero" ? "wallpaper" : type.c_str();
            if (jsonStringOr(protection, slot) == "deleted") { previews.erase(type); continue; }
            if (previews.contains(type) && previews[type].is_object() && !jsonStringOr(previews[type], "url").empty()) continue;
            std::error_code error;
            // Steam can hold its original grid files during a read-only scan.
            // Keep the verified local cache if recopying fails or is unavailable.
            if (fs::is_directory(cacheDirectory, error)) for (const auto& cached : fs::directory_iterator(cacheDirectory, error)) {
                if (error) break;
                if (cached.path().stem().wstring() != toWide(type) || !supportedArtworkFile(cached.path())) continue;
                const auto url = mappedDataUrl(cached.path());
                if (!url.empty()) previews[type] = {{"url", url}, {"provider", "steam-grid-existing"},
                    {"portableFile", toUtf8(cached.path().wstring())}, {"source", "automatic"}, {"priority", "automatic"}};
                break;
            }
            error.clear();
            if (!fs::is_directory(grid, error)) continue;
            for (const auto& entry : fs::directory_iterator(grid, error)) {
                if (error) { error.clear(); break; }
                if (!entry.is_regular_file(error)) { error.clear(); continue; }
                const auto stemName = toUtf8(entry.path().stem().wstring());
                if (stemName != stem) { error.clear(); continue; }
                if (!supportedArtworkFile(entry.path())) { error.clear(); continue; }
                const auto destination = cacheDirectory / (toWide(type) + entry.path().extension().wstring());
                try { copyHostFileAtomic(entry.path(), destination); }
                catch (...) { continue; }
                const auto url = mappedDataUrl(destination);
                if (url.empty()) continue;
                previews[type] = {{"url", url}, {"provider", "steam-grid-existing"},
                    {"file", toUtf8(entry.path().wstring())}, {"portableFile", toUtf8(destination.wstring())}, {"source", "automatic"}, {"priority", "automatic"}};
                break;
            }
        }
        if (!previews.empty()) item["artworkPreview"] = std::move(previews);
        else item.erase("artworkPreview");
    }
}
static std::vector<fs::path> directNativeSteamRoots(const json& item) {
    std::vector<fs::path> result;
    std::set<std::wstring> seen;
    auto appendRoot = [&](const fs::path& root) {
        if (root.empty()) return;
        std::error_code error;
        if (!fs::is_directory(root, error)) return;
        const auto key = normalizedPathKey(root);
        if (seen.insert(key).second) result.push_back(root);
    };
    const auto native = jsonObjectOr(item, "nativeSteam");
    const auto manifestText = jsonStringOr(item, "nativeSteamManifestPath",
        jsonStringOr(native, "manifestPath"));
    if (!manifestText.empty()) {
        const auto manifest = fs::path(toWide(manifestText));
        appendRoot(manifest.parent_path().parent_path());
    }
    const auto gameDirectoryText = jsonStringOr(item, "nativeSteamGameDirectory",
        jsonStringOr(native, "gameDirectory"));
    if (!gameDirectoryText.empty()) {
        const auto gameDirectory = fs::path(toWide(gameDirectoryText));
        appendRoot(gameDirectory.parent_path().parent_path().parent_path());
    }
    auto registryPath = [](HKEY root, const wchar_t* key, const wchar_t* valueName) -> std::optional<fs::path> {
        DWORD bytes = 0;
        if (RegGetValueW(root, key, valueName, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
            bytes < sizeof(wchar_t)) return std::nullopt;
        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        if (RegGetValueW(root, key, valueName, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS) {
            return std::nullopt;
        }
        while (!value.empty() && value.back() == L'\0') value.pop_back();
        std::replace(value.begin(), value.end(), L'/', L'\\');
        return value.empty() ? std::nullopt : std::optional<fs::path>(fs::path(value));
    };
    for (const auto& path : {
        registryPath(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"),
        registryPath(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"InstallPath"),
        registryPath(HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\Valve\\Steam", L"InstallPath"),
        registryPath(HKEY_LOCAL_MACHINE, L"Software\\Valve\\Steam", L"InstallPath")
    }) {
        if (path) appendRoot(*path);
    }
    auto environmentPath = [](const wchar_t* name, const fs::path& fallback) {
        const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
        if (needed <= 1) return fallback;
        std::wstring value(needed, L'\0');
        const DWORD length = GetEnvironmentVariableW(name, value.data(), needed);
        if (length == 0 || length >= needed) return fallback;
        value.resize(length);
        return fs::path(value);
    };
    appendRoot(environmentPath(L"ProgramFiles(x86)", fs::path(L"C:\\Program Files (x86)")) / L"Steam");
    appendRoot(environmentPath(L"ProgramFiles", fs::path(L"C:\\Program Files")) / L"Steam");
    appendRoot(fs::path(L"C:\\Steam"));
    return result;
}

static std::vector<std::pair<std::string, fs::path>> directNativeSteamGridDirectories(
    const json& item, const json& steamAccounts) {
    std::vector<std::pair<std::string, fs::path>> result;
    std::set<std::wstring> seen;
    auto appendGrid = [&](const std::string& accountId, const fs::path& grid) {
        std::error_code error;
        if (!fs::is_directory(grid, error)) return;
        const auto key = normalizedPathKey(grid);
        if (!seen.insert(key).second) return;
        result.push_back({accountId.empty() ? "native" : accountId, grid});
    };
    for (const auto& account : jsonArrayOr(steamAccounts, "accounts")) {
        const auto shortcutsText = jsonStringOr(account, "shortcutsVdf");
        if (shortcutsText.empty()) continue;
        appendGrid(jsonStringOr(account, "accountId", "native"),
            fs::path(toWide(shortcutsText)).parent_path() / L"grid");
    }
    for (const auto& root : directNativeSteamRoots(item)) {
        const auto userdata = root / L"userdata";
        std::error_code error;
        for (const auto& entry : fs::directory_iterator(userdata, error)) {
            if (error) { error.clear(); break; }
            if (!entry.is_directory(error)) { error.clear(); continue; }
            appendGrid(toUtf8(entry.path().filename().wstring()),
                entry.path() / L"config" / L"grid");
        }
    }
    appendGrid("native", fs::path(toWide(jsonStringOr(item, "nativeSteamGridDirectory"))));
    return result;
}

static std::optional<fs::path> directNativeSteamLibraryCacheArtwork(
    const fs::path& steamRoot, const std::string& appId, const std::string& type) {
    const auto cache = steamRoot / L"appcache" / L"librarycache" / toWide(appId);
    std::error_code error;
    if (!fs::is_directory(cache, error)) return std::nullopt;
    const std::vector<std::string> prefixes = type == "cover"
        ? std::vector<std::string>{"library_600x900", "library_capsule"}
        : type == "long" ? std::vector<std::string>{"header"}
        : std::vector<std::string>{"library_hero"};
    std::vector<fs::path> candidates;
    for (fs::recursive_directory_iterator it(cache, fs::directory_options::skip_permission_denied, error), end;
         it != end; it.increment(error)) {
        if (error) { error.clear(); continue; }
        if (!it->is_regular_file(error)) { error.clear(); continue; }
        const auto name = lowerAscii(toUtf8(it->path().filename().wstring()));
        if (name.find("blur") != std::string::npos || !supportedArtworkFile(it->path()) ||
            std::none_of(prefixes.begin(), prefixes.end(), [&](const auto& prefix) {
                return name.starts_with(prefix);
            })) { error.clear(); continue; }
        candidates.push_back(it->path());
    }
    std::sort(candidates.begin(), candidates.end(), [&](const auto& left, const auto& right) {
        const auto leftName = lowerAscii(toUtf8(left.filename().wstring()));
        const auto rightName = lowerAscii(toUtf8(right.filename().wstring()));
        const auto prefixRank = [&](const std::string& name) {
            for (size_t index = 0; index < prefixes.size(); ++index) {
                if (name.starts_with(prefixes[index])) return index;
            }
            return prefixes.size();
        };
        const auto leftRank = prefixRank(leftName);
        const auto rightRank = prefixRank(rightName);
        if (leftRank != rightRank) return leftRank < rightRank;
        const auto leftPrefix = prefixes[leftRank];
        const auto rightPrefix = prefixes[rightRank];
        const auto leftExact = leftName == leftPrefix + ".jpg" || leftName == leftPrefix + ".png";
        const auto rightExact = rightName == rightPrefix + ".jpg" || rightName == rightPrefix + ".png";
        if (leftExact != rightExact) return leftExact > rightExact;
        return normalizedPathKey(left) < normalizedPathKey(right);
    });
    return candidates.empty() ? std::nullopt : std::optional<fs::path>(candidates.front());
}

static fs::path nativeSteamArtworkCachePath() {
    return g_dataRoot / L"cache" / L"native-steam-artwork-cache.json";
}

static json nativeSteamArtworkCacheDocument() {
    auto cache = readJson(nativeSteamArtworkCachePath(), json::object());
    if (!cache.is_object()) cache = json::object();
    cache["schemaVersion"] = 1;
    if (!cache.contains("items") || !cache["items"].is_object()) cache["items"] = json::object();
    return cache;
}

static json nativeSteamArtworkFromCache(const json& cached) {
    json previews = json::object();
    for (const auto& [type, value] : cached.items()) {
        if (!value.is_object()) continue;
        const auto portable = fs::path(toWide(jsonStringOr(value, "portableFile")));
        const auto original = fs::path(toWide(jsonStringOr(value, "file")));
        const auto source = fs::is_regular_file(portable) ? portable : original;
        if (source.empty() || !fs::is_regular_file(source)) continue;
        const auto url = mappedDataUrl(source);
        if (url.empty()) continue;
        auto preview = value;
        preview["url"] = url;
        if (preview.contains("file") && preview["file"].is_string()) {
            preview["file"] = jsonStringOr(preview, "file");
        }
        previews[type] = std::move(preview);
    }
    return previews;
}

static void attachNativeSteamArtwork(json& plan, const json& steamAccounts, bool refreshDirect) {
    if (!plan.contains("items") || !plan["items"].is_array()) return;
    auto cache = nativeSteamArtworkCacheDocument();
    bool cacheDirty = false;
    for (auto& item : plan["items"]) {
        if (!jsonBoolOr(item, "steamNative", false)) continue;
        const auto appId = jsonStringOr(item, "nativeSteamAppId",
            std::to_string(jsonIntegerOr(item, "steamStoreAppId", 0)));
        if (appId.empty() || appId == "0") continue;
        if (!refreshDirect) {
            const auto cached = cache["items"].find(appId);
            if (cached != cache["items"].end() && cached->is_object()) {
                const auto previews = nativeSteamArtworkFromCache(
                    jsonObjectOr(*cached, "artworkPreview"));
                if (!previews.empty()) item["artworkPreview"] = previews;
                else item.erase("artworkPreview");
                auto nativeState = jsonObjectOr(*cached, "nativeSteamArtwork");
                if (!nativeState.empty()) {
                    nativeState["directRead"] = false;
                    nativeState["cacheRead"] = true;
                    item["nativeSteamArtwork"] = std::move(nativeState);
                }
            } else {
                item.erase("artworkPreview");
                item.erase("nativeSteamArtwork");
            }
            continue;
        }

        // A startup/editor refresh is the only path allowed to inspect Steam's
        // local grid and appcache.  Begin with an empty preview set so stale
        // plan data can never survive a deliberate local refresh.
        json previews = json::object();
        bool localTallFound = false;
        bool localLongFound = false;
        bool localHeroFound = false;
        for (const auto& [accountId, grid] : directNativeSteamGridDirectories(item, steamAccounts)) {
            std::error_code error;
            if (!fs::is_directory(grid, error)) continue;
            const auto cacheKey = safeArtworkSegment("native_" + accountId + "_" + appId);
            const auto cacheDirectory = g_dataRoot / L"cache" / L"existing-steam-artwork" / toWide(cacheKey);
            fs::create_directories(cacheDirectory, error);
            const std::array<std::pair<std::string, std::string>, 5> types = {{
                {"tall", appId + "p"}, {"long", appId}, {"hero", appId + "_hero"},
                {"logo", appId + "_logo"}, {"icon", appId + "_icon"}
            }};
            for (const auto& [type, stem] : types) {
                error.clear();
                for (const auto& entry : fs::directory_iterator(grid, error)) {
                    if (error) break;
                    if (!entry.is_regular_file(error)) { error.clear(); continue; }
                    if (toUtf8(entry.path().stem().wstring()) != stem || !supportedArtworkFile(entry.path())) {
                        error.clear();
                        continue;
                    }
                    const auto destination = cacheDirectory / (toWide(type) + entry.path().extension().wstring());
                    try { copyHostFileAtomic(entry.path(), destination); }
                    catch (...) { continue; }
                    const auto url = mappedDataUrl(destination);
                    if (url.empty()) continue;
                    previews[type] = {
                        {"url", url}, {"provider", "steam-grid-native"},
                        {"file", toUtf8(entry.path().wstring())},
                        {"portableFile", toUtf8(destination.wstring())},
                        {"steamAppId", std::stoi(appId)}, {"accountId", accountId}
                    };
                    if (type == "tall") localTallFound = true;
                    if (type == "long") localLongFound = true;
                    if (type == "hero") localHeroFound = true;
                    item["nativeSteamGridDirectory"] = toUtf8(grid.wstring());
                    break;
                }
            }
            // Steam's local grid is authoritative for native games.  Do not
            // let a stale YeManCC preview stop the local lookup; stop only
            // after this account supplied a fresh local cover or hero.
            if (localTallFound || localHeroFound) break;
        }
        // Native Steam artwork is normally stored in Steam's local
        // appcache\librarycache, not in the custom-shortcut grid directory.
        // Read it directly by AppID as the default source; this is local I/O,
        // not a network scrape and never enters the background queue.
        for (const auto& root : directNativeSteamRoots(item)) {
            const auto cacheKey = safeArtworkSegment("native_appcache_" + appId);
            const auto cacheDirectory = g_dataRoot / L"cache" / L"existing-steam-artwork" / toWide(cacheKey);
            std::error_code error;
            fs::create_directories(cacheDirectory, error);
            for (const auto& [type, sourceType] : {std::pair<std::string, std::string>{"tall", "cover"}, {"long", "long"}, {"hero", "wallpaper"}}) {
                if ((type == "tall" && localTallFound) || (type == "long" && localLongFound) || (type == "hero" && localHeroFound)) continue;
                const auto source = directNativeSteamLibraryCacheArtwork(root, appId, sourceType);
                if (!source) continue;
                const auto destination = cacheDirectory / (toWide(type) + source->extension().wstring());
                try { copyHostFileAtomic(*source, destination); }
                catch (...) { continue; }
                const auto url = mappedDataUrl(destination);
                if (url.empty()) continue;
                previews[type] = {
                    {"url", url}, {"provider", "steam-librarycache-native"},
                    {"file", toUtf8(source->wstring())},
                    {"portableFile", toUtf8(destination.wstring())},
                    {"steamAppId", std::stoi(appId)}, {"source", "steam-local-appcache"}
                };
                if (type == "tall") localTallFound = true;
                if (type == "long") localLongFound = true;
                if (type == "hero") localHeroFound = true;
            }
            if (localTallFound && localLongFound && localHeroFound) break;
        }
        item["nativeSteamArtwork"] = {
            {"source", localTallFound || localHeroFound ? "steam-local" : "steam-local-missing"},
            {"directRead", true},
            {"cacheRead", false},
            {"coverAvailable", localTallFound},
            {"longAvailable", localLongFound},
            {"wallpaperAvailable", localHeroFound},
            {"gridDirectory", jsonStringOr(item, "nativeSteamGridDirectory")}
        };
        if (!previews.empty()) item["artworkPreview"] = std::move(previews);
        else item.erase("artworkPreview");

        json cacheItem = {
            {"appId", std::stoi(appId)},
            {"gameDirectory", jsonStringOr(item, "gameDirectory")},
            {"artworkPreview", jsonObjectOr(item, "artworkPreview")},
            {"nativeSteamArtwork", jsonObjectOr(item, "nativeSteamArtwork")},
            {"updatedAt", unixTimeMsHost()}
        };
        auto& cachedItem = cache["items"][appId];
        if (cachedItem != cacheItem) {
            cachedItem = std::move(cacheItem);
            cacheDirty = true;
        }
    }
    if (refreshDirect && cacheDirty) writeJsonWithLocalBackup(nativeSteamArtworkCachePath(), cache, false);
}
static json manualArtworkOverrideForExecutable(const json& overrides, const std::string& executable);
static std::optional<std::pair<json, fs::path>> findPortableArtworkManifestForHost(const std::string& executable);
static fs::path hostManifestArtworkSource(const json& artwork);

static void attachArtworkPreviews(json& plan) {
    if (!plan.contains("items") || !plan["items"].is_array()) return;
    const auto overrides = readJson(g_dataRoot / L"config" / L"manual-overrides.json", json::object());
    for (auto& item : plan["items"]) {
        auto manifestText = jsonStringOr(item, "artworkManifest");
        json manifest = manifestText.empty() ? json::object() : readJson(fs::path(toWide(manifestText)), json::object());
        const auto executable = jsonStringOr(item, "primaryExecutable", jsonStringOr(item, "exe"));
        if (manifest.empty() || !samePath(fs::path(toWide(jsonStringOr(manifest, "exe"))), fs::path(toWide(executable)))) {
            if (const auto durable = findPortableArtworkManifestForHost(executable)) {
                manifest = durable->first;
                manifestText = toUtf8(durable->second.wstring());
            } else manifest = json::object();
        }
        if (!manifestText.empty() && !manifest.empty()) item["artworkManifest"] = manifestText;
        const auto manual = manualArtworkOverrideForExecutable(overrides, executable);
        const auto protection = jsonObjectOr(manual, "artworkProtection");
        json previews = json::object();
        for (const auto& artwork : jsonArrayOr(manifest, "artwork")) {
            if (!jsonBoolOr(artwork, "ok", false)) continue;
            const auto type = jsonStringOr(artwork, "type");
            if (type.empty()) continue;
            const auto source = hostManifestArtworkSource(artwork);
            const auto url = mappedDataUrl(source);
            if (url.empty()) continue;
            previews[type] = {
                {"url", url},
                {"width", jsonIntegerOr(artwork, "width", 0)},
                {"height", jsonIntegerOr(artwork, "height", 0)},
                {"provider", jsonStringOr(artwork, "provider")},
                {"source", jsonStringOr(artwork, "provider") == "manual-override" ? "manual" : "automatic"},
                {"priority", jsonStringOr(artwork, "provider") == "manual-override" ? "manual" : "automatic"},
                {"portableFile", toUtf8(source.wstring())},
                {"quality", jsonObjectOr(artwork, "quality")}
            };
        }
        for (const auto& [slot, type] : {std::pair<const char*, const char*>{"cover", "tall"}, {"long", "long"}, {"wallpaper", "hero"}}) {
            if (jsonStringOr(protection, slot) == "deleted") { previews.erase(type); continue; }
            const auto asset = jsonObjectOr(manual, slot);
            const auto source = hostManifestArtworkSource(asset);
            const auto url = mappedDataUrl(source);
            if (!url.empty()) previews[type] = {{"url", url}, {"portableFile", toUtf8(source.wstring())},
                {"provider", "manual-override"}, {"source", "manual"}, {"priority", "manual"}};
        }
        if (!previews.empty()) item["artworkPreview"] = std::move(previews);
        else if (!jsonBoolOr(item, "steamNative", false)) item.erase("artworkPreview");
    }
}

// All refresh sources must attach the same layers before publishing a
// snapshot. Portable-only refreshes used to erase joined Steam grid previews.
static void attachWorkspaceArtwork(json& plan, const json& accounts, bool refreshNative = false) {
    attachArtworkPreviews(plan);
    attachExistingSteamArtwork(plan);
    attachNativeSteamArtwork(plan, accounts, refreshNative);
}

static std::optional<std::pair<json, fs::path>> findPortableArtworkManifestForHost(
    const std::string& executable) {
    const auto root = g_dataRoot / L"artwork";
    std::error_code error;
    if (!fs::is_directory(root, error)) return std::nullopt;
    size_t inspected = 0;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
    while (!error && it != end && ++inspected <= 20000 && !g_shuttingDown.load()) {
        const auto path = it->path();
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            it.disable_recursion_pending();
        } else if (it->is_regular_file(error) && path.filename() == L"manifest.json") {
            try {
                const auto manifest = readJson(path, json::object());
                if (manifest.is_object() && !executable.empty() &&
                    samePath(fs::path(toWide(jsonStringOr(manifest, "exe"))), fs::path(toWide(executable))))
                    return std::make_pair(manifest, path);
            } catch (...) { /* optional cache: one damaged row is not fatal */ }
        }
        error.clear();
        it.increment(error);
    }
    return std::nullopt;
}

static fs::path hostManifestArtworkSource(const json& artwork) {
    const auto portable = fs::path(toWide(jsonStringOr(artwork, "portableFile")));
    if (!portable.empty() && fs::is_regular_file(portable)) return portable;
    const auto original = fs::path(toWide(jsonStringOr(artwork, "file")));
    return !original.empty() && fs::is_regular_file(original) ? original : fs::path{};
}

static json hostArtworkReadiness(const json& manifest, const json& manualOverride) {
    bool tall = false;
    bool longArtwork = false;
    bool hero = false;
    const auto manual = manualOverride.is_object() ? manualOverride : json::object();
    const auto cover = manual.find("cover");
    const auto longCover = manual.find("long");
    const auto wallpaper = manual.find("wallpaper");
    if (cover != manual.end() && cover->is_object()) tall =
        fs::is_regular_file(fs::path(toWide(jsonStringOr(*cover, "portableFile")))) ||
        fs::is_regular_file(fs::path(toWide(jsonStringOr(*cover, "file"))));
    if (longCover != manual.end() && longCover->is_object()) longArtwork =
        fs::is_regular_file(fs::path(toWide(jsonStringOr(*longCover, "portableFile")))) ||
        fs::is_regular_file(fs::path(toWide(jsonStringOr(*longCover, "file"))));
    if (wallpaper != manual.end() && wallpaper->is_object()) hero =
        fs::is_regular_file(fs::path(toWide(jsonStringOr(*wallpaper, "portableFile")))) ||
        fs::is_regular_file(fs::path(toWide(jsonStringOr(*wallpaper, "file"))));
    for (const auto& artwork : jsonArrayOr(manifest, "artwork")) {
        if (!jsonBoolOr(artwork, "ok", false) || hostManifestArtworkSource(artwork).empty()) continue;
        const auto type = jsonStringOr(artwork, "type");
        if (type == "tall") tall = true;
        if (type == "long") longArtwork = true;
        if (type == "hero") hero = true;
    }
    const auto protection = jsonObjectOr(manual, "artworkProtection");
    const bool tallDeleted = jsonStringOr(protection, "cover") == "deleted";
    const bool longDeleted = jsonStringOr(protection, "long") == "deleted";
    const bool heroDeleted = jsonStringOr(protection, "wallpaper") == "deleted";
    if (tallDeleted) tall = false;
    if (longDeleted) longArtwork = false;
    if (heroDeleted) hero = false;
    return {{"automaticSlotsComplete", (tall || tallDeleted) && (longArtwork || longDeleted) && (hero || heroDeleted)}, {"minimumComplete", tall && hero}, {"tall", tall}, {"long", longArtwork}, {"hero", hero}};
}

static bool steamPlanItemAlreadyOwned(const json& item) {
    const auto status = jsonStringOr(item, "status");
    return status == "already-in-steam" || status == "added-to-steam" ||
        status == "steam-native" || jsonBoolOr(item, "steamNative", false);
}

// A local artwork refresh has no authority to change Steam shortcut ownership.
// Only reuse ownership for the same library, directory AND selected executable;
// a fresh authoritative Steam plan can still report a removed shortcut.
static json buildLocalSteamPlanSnapshot(const json& snapshot, const json& ownershipPlan = json::object()) {
    const auto library = jsonObjectOr(snapshot, "library");
    const auto games = custom_steam_library::uniqueExecutableInventory(jsonArrayOr(library, "games"), [](const std::string& path) {
        return toUtf8(normalizedPathKey(fs::path(toWide(path))));
    });
    const auto overrides = jsonObjectOr(snapshot, "manualOverrides");
    const auto identities = jsonObjectOr(snapshot, "resolvedIdentities");
    json items = json::array();
    const auto ownershipLibrary = jsonStringOr(ownershipPlan, "libraryState");
    const auto ownedItems = !ownershipLibrary.empty() && samePath(
        fs::path(toWide(ownershipLibrary)), g_dataRoot / L"state" / L"library-scan.json")
        ? jsonArrayOr(ownershipPlan, "items") : json::array();
    size_t alreadyInSteam = 0;
    size_t readyToAdd = 0;
    size_t needsPrimary = 0;
    size_t needsIdentity = 0;
    size_t needsSteamVerification = 0;
    size_t needsArtwork = 0;
    size_t steamNativeGames = 0;
    size_t nonGames = 0;

    auto cachedIdentityFor = [&](const std::string& executable) {
        const auto wanted = toUtf8(normalizedPathKey(fs::path(toWide(executable))));
        const auto direct = identities.find(wanted);
        if (direct != identities.end() && direct->is_object()) return *direct;
        for (const auto& [key, value] : identities.items()) {
            if (value.is_object() && samePath(fs::path(toWide(jsonStringOr(value, "executable"))),
                                               fs::path(toWide(executable)))) return value;
        }
        return json::object();
    };

    for (const auto& game : games) {
        if (!game.is_object()) continue;
        const auto executable = jsonStringOr(game, "primaryExecutable");
        json item = {
            {"gameDirectory", jsonStringOr(game, "gameDirectory")},
            {"directoryName", jsonStringOr(game, "directoryName")},
            {"primaryExecutable", executable.empty() ? json(nullptr) : json(executable)},
            {"primarySelectionStatus", jsonStringOr(game, "status", "unknown")},
            {"selectionSource", jsonStringOr(game, "selectionSource")},
            {"selectedForAdd", true}
        };
        if (jsonBoolOr(game, "steamNative", false) ||
            jsonStringOr(game, "contentType") == "steam-native") {
            item["status"] = "already-in-steam";
            item["steamNative"] = true;
            item["steamOwnership"] = "native-steam";
            item["reason"] = "steam-native-game-excluded";
            const int nativeAppId = static_cast<int>(jsonIntegerOr(game, "nativeSteamAppId", 0));
            item["nativeSteamAppId"] = nativeAppId;
            item["steamStoreAppId"] = nativeAppId;
            item["storefrontAppId"] = nativeAppId;
            item["nativeSteamExecutable"] = executable.empty() ? json(nullptr) : json(executable);
            item["nativeSteamGameDirectory"] = jsonStringOr(game, "nativeSteamGameDirectory",
                jsonStringOr(game, "gameDirectory"));
            item["nativeSteamManifestPath"] = jsonStringOr(game, "nativeSteamManifestPath");
            item["nativeSteam"] = game.contains("nativeSteam") && game["nativeSteam"].is_object()
                ? game["nativeSteam"] : json{
                    {"appId", nativeAppId},
                    {"executablePath", executable.empty() ? json(nullptr) : json(executable)},
                    {"gameDirectory", jsonStringOr(game, "gameDirectory")},
                    {"manifestPath", jsonStringOr(game, "nativeSteamManifestPath")},
                    {"manifestName", jsonStringOr(game, "directoryName")}
                };
            ++steamNativeGames;
            items.push_back(std::move(item));
            continue;
        }
        const auto owned = std::find_if(ownedItems.begin(), ownedItems.end(), [&](const json& previous) {
            const auto previousExe = jsonStringOr(previous, "primaryExecutable");
            const auto previousDirectory = jsonStringOr(previous, "gameDirectory");
            const auto directory = jsonStringOr(game, "gameDirectory");
            bool directoryMatch = samePath(fs::path(toWide(directory)), fs::path(toWide(previousDirectory)));
            for (const auto& alias : jsonArrayOr(game, "alternateGameDirectories"))
                if (alias.is_string() && samePath(fs::path(toWide(alias.get<std::string>())), fs::path(toWide(previousDirectory)))) directoryMatch = true;
            return steamPlanItemAlreadyOwned(previous) && !executable.empty() && !previousExe.empty() &&
                !directory.empty() && !previousDirectory.empty() &&
                samePath(fs::path(toWide(executable)), fs::path(toWide(previousExe))) &&
                directoryMatch;
        });
        if (owned != ownedItems.end()) {
            // Preserve shortcut IDs, grid paths and existing previews as well as
            // the category. Artwork/online identity is not ownership evidence.
            items.push_back(*owned);
            if (jsonBoolOr(*owned, "steamNative", false)) ++steamNativeGames;
            else ++alreadyInSteam;
            continue;
        }
        if (jsonStringOr(game, "contentType") == "non-game" ||
            jsonBoolOr(game, "unrecognizedTool", false) ||
            jsonStringOr(game, "status") == "unrecognized-tool") {
            item["status"] = "non-game";
            ++nonGames;
            items.push_back(std::move(item));
            continue;
        }
        if (executable.empty() || jsonStringOr(game, "status") != "ready" ||
            !fs::is_regular_file(fs::path(toWide(executable)))) {
            item["status"] = "needs-primary-confirmation";
            ++needsPrimary;
            items.push_back(std::move(item));
            continue;
        }

        const auto cachedIdentity = cachedIdentityFor(executable);
        const auto manifestResult = findPortableArtworkManifestForHost(executable);
        const json manifest = manifestResult ? manifestResult->first : json::object();
        const auto match = jsonObjectOr(manifest, "match");
        const auto manualIdentity = manualArtworkOverrideForExecutable(overrides, executable);
        const bool idCleared = jsonBoolOr(manualIdentity, "idCleared", false);
        const auto identityMatch = idCleared ? json::object() :
            ((match.empty() || (!custom_steam_library::verifiedSteamIdentityMatch(match) &&
                custom_steam_library::verifiedSteamIdentityMatch(cachedIdentity))) ? cachedIdentity : match);
        const auto provider = jsonStringOr(identityMatch, "primaryProvider");
        const auto identityStatus = jsonStringOr(identityMatch, "identityStatus");
        const auto steamVerificationStatus = jsonStringOr(identityMatch, "steamVerificationStatus");
        const int steamAppId = jsonIntegerOr(identityMatch, "appId",
            jsonIntegerOr(identityMatch, "storefrontAppId", jsonIntegerOr(identityMatch, "steamAppId", 0)));
        const int igdbId = jsonIntegerOr(identityMatch, "igdbId", 0);
        const bool steamReady = custom_steam_library::verifiedSteamIdentityMatch(identityMatch);
        const bool metadataReady = provider == "playnite-igdb" && igdbId > 0 &&
            (identityStatus.empty() || identityStatus == "metadata-verified");
        const auto formalName = jsonStringOr(identityMatch, "formalName",
            jsonStringOr(identityMatch, "displayName", jsonStringOr(game, "directoryName")));
        if (!formalName.empty()) item["formalName"] = formalName;
        if (steamAppId > 0) {
            item["steamStoreAppId"] = steamAppId;
            item["storefrontAppId"] = steamAppId;
        }
        if (igdbId > 0) item["igdbId"] = igdbId;
        if (!provider.empty()) item["primaryProvider"] = provider;
        if (!identityStatus.empty()) item["identityStatus"] = identityStatus;
        if (!steamVerificationStatus.empty()) item["steamVerificationStatus"] = steamVerificationStatus;
        if (manifestResult) item["artworkManifest"] = toUtf8(manifestResult->second.wstring());

        const auto manual = manualArtworkOverrideForExecutable(overrides, executable);
        const auto artwork = hostArtworkReadiness(manifest, manual);
        item["artwork"] = artwork;
        item["artworkFallbackComplete"] = artwork.value("minimumComplete", false);
        if (!steamReady) {
            item["status"] = artwork.value("minimumComplete", false)
                ? "waiting-steam-verification"
                : (metadataReady ? "needs-steam-verification" : "needs-identity-confirmation");
            if (metadataReady) item["metadataGameSignal"] = true;
            ++needsIdentity;
            ++needsSteamVerification;
        } else if (!artwork.value("minimumComplete", false)) {
            item["status"] = "needs-minimum-artwork";
            ++needsArtwork;
        } else {
            item["status"] = "ready-to-add";
            ++readyToAdd;
        }
        items.push_back(std::move(item));
    }

    return {
        {"schemaVersion", 1},
        {"isolated", true},
        {"mode", "portable-scrape-plan"},
        {"createdAt", unixTimeMsHost()},
        {"libraryState", toUtf8((g_dataRoot / L"state" / L"library-scan.json").wstring())},
        {"selectionMode", "all-ready"},
        {"summary", {
            {"scannedGames", games.size()}, {"readyToAdd", readyToAdd},
            {"readyNotSelected", 0}, {"alreadyInSteam", alreadyInSteam},
            {"needsPrimaryConfirmation", needsPrimary},
            {"needsIdentity", needsIdentity},
            {"needsSteamVerification", needsSteamVerification},
            {"needsMinimumArtwork", needsArtwork}, {"nonGames", nonGames},
            {"steamNativeGames", steamNativeGames},
            {"shortcutIdConflicts", 0}
        }},
        {"items", items}
    };
}

struct ProcessResult {
    DWORD exitCode = ERROR_GEN_FAILURE;
    std::string output;
};

static fs::path resolveExecutablePath(const fs::path& executable) {
    if (executable.is_absolute()) {
        std::error_code error;
        if (!fs::is_regular_file(executable, error))
            throw std::runtime_error("Cannot locate isolated worker executable: " + toUtf8(executable.wstring()));
        return executable;
    }

    // CreateProcess only performs PATH resolution when lpApplicationName is
    // null. Resolve command names here so GUI-hosted work does not depend on
    // console inheritance or the caller's current directory.
    std::vector<wchar_t> pathBuffer(32768);
    const DWORD length = SearchPathW(nullptr, executable.c_str(), nullptr,
        static_cast<DWORD>(pathBuffer.size()), pathBuffer.data(), nullptr);
    if (length == 0 || length >= pathBuffer.size()) {
        std::ostringstream detail;
        detail << "Cannot locate isolated worker executable (Win32=" << GetLastError() << ")";
        detail << "\nrequested=" << toUtf8(executable.wstring());
        throw std::runtime_error(detail.str());
    }
    return fs::path(std::wstring(pathBuffer.data(), length));
}

struct HostProcessAttributes {
    std::vector<unsigned char> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    ~HostProcessAttributes() { if (list) DeleteProcThreadAttributeList(list); }
    void setInheritedHandles(HANDLE* handles, size_t count) {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        storage.resize(bytes);
        auto candidate = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(candidate, 1, 0, &bytes))
            throw std::runtime_error("Cannot initialize isolated worker handle list");
        list = candidate;
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            handles, count * sizeof(HANDLE), nullptr, nullptr))
            throw std::runtime_error("Cannot restrict isolated worker inherited handles");
    }
};

static ProcessResult runProcess(
    const fs::path& executable,
    const std::vector<std::wstring>& arguments,
    // Never let a host-side worker silently inherit an hour-long default.
    // Search and scrape callers use a shorter explicit fence; this default is
    // only for legacy local commands which have not supplied one.
    DWORD timeoutMs = kWorkerDefaultTimeoutMs,
    std::atomic<bool>* cancelFlag = nullptr,
    HANDLE* activeProcessSlot = nullptr) {
    auto* effectiveCancelFlag = cancelFlag ? cancelFlag : &g_cancelRequested;
    auto* effectiveProcessSlot = activeProcessSlot ? activeProcessSlot : &g_activeProcess;
    if (g_shuttingDown.load() || effectiveCancelFlag->load()) {
        return ProcessResult{ERROR_CANCELLED, "\nTASK_CANCELLED"};
    }
    const fs::path executablePath = resolveExecutablePath(executable);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 64u << 10)) throw std::runtime_error("Cannot create worker pipe");
    if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(readPipe); CloseHandle(writePipe);
        throw std::runtime_error("Cannot isolate worker output pipe");
    }

    std::wstring command = quoteArgument(executablePath.wstring());
    for (const auto& argument : arguments) command += L" " + quoteArgument(argument);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(0);

    STARTUPINFOEXW extendedStartup{};
    extendedStartup.StartupInfo.cb = sizeof(extendedStartup);
    auto& startup = extendedStartup.StartupInfo;
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    // GUI processes do not necessarily own a valid standard-input handle.
    // Supplying NUL keeps CREATE_NO_WINDOW workers isolated and avoids an
    // ERROR_INVALID_HANDLE failure when inheriting console handles.
    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) {
        CloseHandle(writePipe);
        CloseHandle(readPipe);
        throw std::runtime_error("Cannot create isolated worker input");
    }
    startup.hStdInput = nullInput;
    HostProcessAttributes attributes;
    HANDLE inherited[] = {writePipe, nullInput};
    try { attributes.setInheritedHandles(inherited, std::size(inherited)); }
    catch (...) { CloseHandle(nullInput); CloseHandle(writePipe); CloseHandle(readPipe); throw; }
    extendedStartup.lpAttributeList = attributes.list;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        executablePath.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, g_labRoot.c_str(), &startup, &process);
    CloseHandle(nullInput);
    CloseHandle(writePipe);
    if (!created) {
        const DWORD error = GetLastError();
        CloseHandle(readPipe);
        std::ostringstream detail;
        detail << "Cannot start isolated worker (Win32=" << error << ")";
        detail << "\nworker=" << toUtf8(executablePath.wstring());
        detail << "\nworkingDirectory=" << toUtf8(g_labRoot.wstring());
        throw std::runtime_error(detail.str());
    }
    assignProcessToHostWorkerJob(process.hProcess);

    bool cancelAfterStart = false;
    {
        std::lock_guard<std::mutex> lock(g_activeProcessMutex);
        cancelAfterStart = g_shuttingDown.load() || effectiveCancelFlag->load();
        if (!cancelAfterStart) *effectiveProcessSlot = process.hProcess;
    }
    if (cancelAfterStart) TerminateProcess(process.hProcess, ERROR_CANCELLED);
    else if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        TerminateProcess(process.hProcess, ERROR_PROCESS_ABORTED);
        WaitForSingleObject(process.hProcess, 5000);
        { std::lock_guard<std::mutex> lock(g_activeProcessMutex);
          if (*effectiveProcessSlot == process.hProcess) *effectiveProcessSlot = nullptr; }
        CloseHandle(readPipe); CloseHandle(process.hThread); CloseHandle(process.hProcess);
        if (g_shuttingDown.load() || effectiveCancelFlag->load())
            return ProcessResult{ERROR_CANCELLED, "\nTASK_CANCELLED"};
        throw std::runtime_error("Cannot resume isolated worker");
    }
    ProcessResult result;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    char buffer[8192];
    bool finished = false;
    bool forcedTermination = false;
    bool outputTruncated = false;
    ULONGLONG lastOutputAt = GetTickCount64();
    constexpr size_t maxOutputBytes = 4u << 20;
    auto appendOutput = [&](const char* data, size_t bytes) {
        const auto room = maxOutputBytes - (std::min)(result.output.size(), maxOutputBytes);
        result.output.append(data, (std::min)(room, bytes));
        if (bytes > room) outputTruncated = true;
    };
    while (!finished) {
        if (cancelAfterStart || g_shuttingDown.load() || effectiveCancelFlag->load()) {
            TerminateProcess(process.hProcess, ERROR_CANCELLED);
            result.output += "\nTASK_CANCELLED";
            forcedTermination = true;
            finished = true;
        }
        DWORD available = 0;
        bool hadOutput = false;
        // Bound each pump so a flooding worker cannot starve cancellation
        // or the timeout check. Continue draining after the retention cap.
        for (int pump = 0; pump < 32 && PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0; ++pump) {
            DWORD read = 0;
            if (!ReadFile(readPipe, buffer, (std::min)(available, static_cast<DWORD>(sizeof(buffer))), &read, nullptr) || read == 0) break;
            appendOutput(buffer, read);
            hadOutput = true;
            lastOutputAt = GetTickCount64();
            if (outputTruncated) {
                TerminateProcess(process.hProcess, ERROR_BUFFER_OVERFLOW);
                if (result.output.find("WORKER_OUTPUT_TRUNCATED") == std::string::npos)
                    result.output += "\nWORKER_OUTPUT_TRUNCATED";
                forcedTermination = true;
                finished = true;
                break;
            }
        }
        const DWORD idleDelay = GetTickCount64() - lastOutputAt < 500 ? 1 : 20;
        const DWORD wait = WaitForSingleObject(process.hProcess, hadOutput ? 0 : idleDelay);
        if (wait == WAIT_OBJECT_0) finished = true;
        else if (wait == WAIT_FAILED || GetTickCount64() >= deadline) {
            TerminateProcess(process.hProcess, ERROR_TIMEOUT);
            finished = true;
            forcedTermination = true;
            result.output += "\n错误：隔离任务超时";
        }
    }
    if (forcedTermination) {
        // TerminateProcess is asynchronous.  Waiting briefly before the
        // drain prevents a still-open inherited pipe from making the GUI
        // thread block forever in the final ReadFile.
        const auto terminated = WaitForSingleObject(process.hProcess, 5000);
        if (terminated != WAIT_OBJECT_0) {
            CloseHandle(readPipe);
            readPipe = nullptr;
        }
    }
    if (readPipe) {
        // A worker's descendant may retain stdout after the worker exits.
        // Never issue a blocking final read or wait for that descendant.
        const auto drainDeadline = GetTickCount64() + 250;
        DWORD available = 0;
        while (GetTickCount64() < drainDeadline &&
               PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD read = 0;
            if (!ReadFile(readPipe, buffer, (std::min)(available, static_cast<DWORD>(sizeof(buffer))), &read, nullptr) || read == 0) break;
            appendOutput(buffer, read);
        }
    }
    if (outputTruncated && result.output.find("WORKER_OUTPUT_TRUNCATED") == std::string::npos)
        result.output += "\nWORKER_OUTPUT_TRUNCATED";
    GetExitCodeProcess(process.hProcess, &result.exitCode);
    if (g_shuttingDown.load() || effectiveCancelFlag->load()) {
        result.exitCode = ERROR_CANCELLED;
        if (result.output.find("TASK_CANCELLED") == std::string::npos) result.output += "\nTASK_CANCELLED";
    }
    {
        std::lock_guard<std::mutex> lock(g_activeProcessMutex);
        if (*effectiveProcessSlot == process.hProcess) *effectiveProcessSlot = nullptr;
    }
    CloseHandle(readPipe);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    // Worker stderr is not guaranteed to be UTF-8. Replacement decoding
    // prevents one diagnostic byte from breaking the entire WebView reply.
    result.output = toUtf8(toWide(result.output));
    return result;
}

static ProcessResult runWorker(
    const std::vector<std::wstring>& arguments,
    std::atomic<bool>* cancelFlag = nullptr,
    HANDLE* activeProcessSlot = nullptr) {
    if (!fs::is_regular_file(g_worker)) throw std::runtime_error("SteamArtworkLab worker is missing");
    return runProcess(g_worker, arguments, kWorkerDefaultTimeoutMs, cancelFlag, activeProcessSlot);
}

static ProcessResult runWorkerBounded(
    const std::vector<std::wstring>& arguments,
    DWORD timeoutMs,
    std::atomic<bool>* cancelFlag = nullptr,
    HANDLE* activeProcessSlot = nullptr) {
    if (!fs::is_regular_file(g_worker)) throw std::runtime_error("SteamArtworkLab worker is missing");
    return runProcess(g_worker, arguments, timeoutMs, cancelFlag, activeProcessSlot);
}

static fs::path workspacePlanPath() {
    return g_dataRoot / L"state" / L"steam-add-plan.json";
}

static fs::path batchPlanPath() {
    return g_dataRoot / L"state" / L"library-steam-add-plan.json";
}

struct CachedSteamPlan {
    json document = json::object();
    fs::path source;
    fs::file_time_type modified{};
};

static bool isPlanForCurrentLibrary(const json& plan) {
    if (!plan.is_object() || !jsonArrayOr(plan, "items").is_array() ||
        !jsonObjectOr(plan, "summary").is_object()) {
        return false;
    }
    const auto libraryState = jsonStringOr(plan, "libraryState");
    return !libraryState.empty() && samePath(
        fs::path(toWide(libraryState)), g_dataRoot / L"state" / L"library-scan.json");
}

static CachedSteamPlan newestCachedSteamPlan(bool allowBatchPlan) {
    CachedSteamPlan newest;
    std::vector<fs::path> candidates = {workspacePlanPath()};
    if (allowBatchPlan) candidates.push_back(batchPlanPath());
    for (const auto& candidate : candidates) {
        std::error_code timeError;
        const auto modified = fs::last_write_time(candidate, timeError);
        // library-scan.json is rewritten after recognition/artwork updates.
        // Its timestamp therefore says nothing about whether the Steam plan
        // is usable. Rejecting an older-but-valid plan here briefly replaced
        // authoritative already-in-Steam states with the local fallback
        // plan, making cards jump to "需处理" and then back to "已加入".
        // The plan is still constrained by isPlanForCurrentLibrary() and is
        // matched only to current executable/directory keys by the UI.
        if (timeError) continue;
        const auto document = readJson(candidate, json::object());
        if (!isPlanForCurrentLibrary(document)) continue;
        if (newest.source.empty() || modified > newest.modified) {
            newest.document = document;
            newest.source = candidate;
            newest.modified = modified;
        }
    }
    return newest;
}

static std::vector<std::wstring> steamTargetArguments(const json& target) {
    std::vector<std::wstring> arguments;
    const auto account = jsonStringOr(target, "accountId");
    const auto root = jsonStringOr(target, "steamRoot");
    const auto selectionMode = jsonStringOr(target, "selectionMode");
    if (!account.empty()) {
        arguments.push_back(L"--account");
        arguments.push_back(toWide(account));
    }
    if (!root.empty()) {
        arguments.push_back(L"--steam-root");
        arguments.push_back(toWide(root));
    }
    if (selectionMode == "all-ready" || selectionMode == "explicit-game-directories") {
        arguments.push_back(L"--selection-mode");
        arguments.push_back(toWide(selectionMode));
    } else if (selectionMode == "all-steam-accounts") {
        const auto selected = jsonArrayOr(target, "selectedGameDirectories");
        arguments.push_back(L"--selection-mode");
        arguments.push_back(selected.empty() ? L"all-ready" : L"explicit-game-directories");
    }
    for (const auto& directory : jsonArrayOr(target, "selectedGameDirectories")) {
        if (!directory.is_string()) continue;
        arguments.push_back(L"--select-game");
        arguments.push_back(toWide(directory.get<std::string>()));
    }
    return arguments;
}

static json readSteamAccounts(
    const json& requestedRoots = json::array(),
    const BackgroundJob& backgroundJob = nullptr) {
    std::vector<std::wstring> arguments = {L"--list-steam-accounts"};
    for (const auto& root : requestedRoots) {
        if (!root.is_string() || root.get<std::string>().empty()) continue;
        arguments.push_back(L"--steam-root");
        arguments.push_back(toWide(root.get<std::string>()));
    }
    const auto outputPath = g_dataRoot / L"state" / L"steam-accounts.json";
    arguments.push_back(L"--output");
    arguments.push_back(outputPath.wstring());
    // Account discovery is part of the read-only planning path.  Bound it
    // like plan generation so a damaged or disconnected Steam install cannot
    // hold a foreground snapshot request indefinitely.
    auto* cancelFlag = backgroundJob ? &backgroundJob->cancelRequested : nullptr;
    auto* activeProcess = backgroundJob ? &backgroundJob->activeProcess : nullptr;
    const auto result = runWorkerBounded(arguments, kLocalScanWorkerTimeoutMs, cancelFlag, activeProcess);
    if (result.exitCode != 0) {
        return json{{"schemaVersion", 1}, {"accounts", json::array()},
            {"steamStatus", "error"},
            {"steamDownloadUrl", "https://store.steampowered.com/about/"},
            {"error", result.output.empty() ? "Steam 账户扫描失败" : result.output}};
    }
    return readJson(outputPath, json{{"accounts", json::array()}});
}

struct NativeSteamArtworkTarget {
    json record = json::object();
    int64_t appId = 0;
    std::vector<std::pair<std::string, fs::path>> grids;
};

static int64_t positiveIntegerTextHost(const std::string& text) {
    if (text.empty() || text.size() > 20 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isdigit(ch); })) return 0;
    try {
        const auto value = std::stoll(text);
        return value > 0 ? value : 0;
    } catch (...) {
        return 0;
    }
}

static bool isNativeSteamRecord(const json& value);

static bool nativeSteamRecordMatchesExecutable(const json& value, const fs::path& executable) {
    if (!value.is_object()) return false;
    if (!isNativeSteamRecord(value)) return false;
    const auto native = jsonObjectOr(value, "nativeSteam");
    const std::array<std::string, 4> fields = {
        jsonStringOr(value, "primaryExecutable"),
        jsonStringOr(value, "nativeSteamExecutable"),
        jsonStringOr(native, "executablePath"),
        jsonStringOr(native, "executable")
    };
    return std::any_of(fields.begin(), fields.end(), [&](const auto& field) {
        return !field.empty() && samePath(executable, fs::path(toWide(field)));
    });
}

static bool isNativeSteamRecord(const json& value) {
    if (!value.is_object()) return false;
    const auto native = jsonObjectOr(value, "nativeSteam");
    return jsonBoolOr(value, "steamNative", false) ||
        jsonStringOr(value, "contentType") == "steam-native" ||
        jsonStringOr(value, "steamOwnership") == "native-steam" ||
        jsonBoolOr(native, "steamNative", false) ||
        jsonStringOr(native, "ownership") == "native-steam";
}

static int64_t nativeSteamAppIdFromRecord(const json& value) {
    const auto native = jsonObjectOr(value, "nativeSteam");
    for (const auto* key : {"nativeSteamAppId", "steamStoreAppId", "storefrontAppId"}) {
        const auto number = jsonIntegerOr(value, key, 0);
        if (number > 0) return number;
        const auto text = positiveIntegerTextHost(jsonStringOr(value, key));
        if (text > 0) return text;
    }
    for (const auto* key : {"appId", "steamAppId"}) {
        const auto number = jsonIntegerOr(native, key, 0);
        if (number > 0) return number;
        const auto text = positiveIntegerTextHost(jsonStringOr(native, key));
        if (text > 0) return text;
    }
    return 0;
}

static std::optional<json> nativeSteamRecordForExecutable(const fs::path& executable) {
    const std::array<fs::path, 2> sources = {
        workspacePlanPath(), g_dataRoot / L"state" / L"library-scan.json"
    };
    for (const auto& source : sources) {
        const auto document = readJson(source, json::object());
        const auto values = document.contains("items") && document["items"].is_array()
            ? document["items"] : jsonArrayOr(document, "games");
        for (const auto& value : values) {
            if (!isNativeSteamRecord(value) || !nativeSteamRecordMatchesExecutable(value, executable)) continue;
            return value;
        }
    }
    return std::nullopt;
}

static NativeSteamArtworkTarget locateNativeSteamArtworkTarget(const fs::path& executable) {
    NativeSteamArtworkTarget target;
    const auto record = nativeSteamRecordForExecutable(executable);
    if (!record.has_value()) return target;
    target.record = *record;
    target.appId = nativeSteamAppIdFromRecord(target.record);
    if (target.appId <= 0) return target;

    json accountDocument = readJson(g_dataRoot / L"state" / L"steam-accounts.json", json::object());
    auto accounts = jsonArrayOr(accountDocument, "accounts");
    std::set<std::wstring> seen;
    auto appendAccountGrids = [&](const json& values) {
        for (const auto& account : values) {
            const auto shortcutsText = jsonStringOr(account, "shortcutsVdf");
            if (shortcutsText.empty()) continue;
            const auto grid = fs::path(toWide(shortcutsText)).parent_path() / L"grid";
            std::error_code error;
            if (!fs::is_directory(grid, error)) continue;
            const auto key = normalizedPathKey(grid);
            if (!seen.insert(key).second) continue;
            auto accountId = jsonStringOr(account, "accountId", "native");
            if (accountId.empty()) accountId = "native";
            target.grids.push_back({accountId, grid});
        }
    };
    appendAccountGrids(accounts);
    // A portable YeManCC data directory may contain an account path from a
    // different machine.  Re-discover Steam accounts when that cached path no
    // longer resolves instead of treating the stale cache as authoritative.
    if (target.grids.empty()) appendAccountGrids(jsonArrayOr(readSteamAccounts(), "accounts"));

    // A plan generated before account discovery can still carry the exact
    // grid directory used for previews.  Keep it as a portable fallback.
    const auto gridText = jsonStringOr(target.record, "nativeSteamGridDirectory");
    if (!gridText.empty()) {
        const auto grid = fs::path(toWide(gridText));
        std::error_code error;
        if (fs::is_directory(grid, error) && seen.insert(normalizedPathKey(grid)).second) {
            target.grids.push_back({"native", grid});
        }
    }
    return target;
}

static std::vector<fs::path> nativeSteamArtworkFiles(
    const fs::path& grid, const std::string& stem) {
    std::vector<fs::path> result;
    std::error_code error;
    if (!fs::is_directory(grid, error)) return result;
    for (const auto& entry : fs::directory_iterator(grid, error)) {
        if (error) { error.clear(); break; }
        if (!entry.is_regular_file(error)) { error.clear(); continue; }
        if (toUtf8(entry.path().stem().wstring()) != stem || !supportedArtworkFile(entry.path())) {
            error.clear();
            continue;
        }
        result.push_back(entry.path());
    }
    return result;
}

static std::string nativeSteamArtworkStem(int64_t appId, const std::string& type) {
    const auto id = std::to_string(appId);
    if (type == "cover") return id + "p";
    if (type == "long") return id;
    if (type == "wallpaper") return id + "_hero";
    return {};
}

static void backupNativeSteamArtworkFile(const fs::path& source, const fs::path& directory) {
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) throw std::runtime_error("无法创建原生 Steam 素材备份目录");
    const auto destination = directory / source.filename();
    copyHostFileAtomic(source, destination);
}

static void writeNativeSteamArtwork(
    const NativeSteamArtworkTarget& target,
    const std::string& type,
    const std::string& sourceText,
    bool clear) {
    if (target.appId <= 0 || target.grids.empty()) {
        throw std::runtime_error("未找到原生 Steam 的本地 grid 目录；请先登录 Steam 后重试");
    }
    HostDataTransaction transaction;
    const auto stem = nativeSteamArtworkStem(target.appId, type);
    if (stem.empty()) throw std::runtime_error("原生 Steam 素材类型不受支持");

    fs::path staged;
    HostStagedFile stagedCleanup{};
    if (!clear) {
        const auto source = fs::path(toWide(sourceText));
        std::error_code error;
        if (!fs::is_regular_file(source, error) || !supportedArtworkFile(source)) {
            throw std::runtime_error("手动素材必须是本地 JPG、PNG、WEBP、BMP 或 ICO 文件");
        }
        const auto stageDirectory = g_dataRoot / L"cache" / L"native-steam-artwork-write";
        fs::create_directories(stageDirectory, error);
        if (error) throw std::runtime_error("无法创建原生 Steam 素材临时目录");
        staged = stageDirectory / toWide(
            safeArtworkSegment(std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(unixTimeMsHost()) + "-" + type) + source.extension().string());
        stagedCleanup.path = staged;
        copyHostFileAtomic(source, staged);
    }

    const auto backupRoot = g_dataRoot / L"backups" / L"native-steam-grid" /
        toWide(std::to_string(unixTimeMsHost())) / toWide(std::to_string(target.appId));
    for (const auto& [accountId, grid] : target.grids) {
        const auto accountBackup = backupRoot / toWide(safeArtworkSegment(accountId));
        const auto existing = nativeSteamArtworkFiles(grid, stem);
        for (const auto& path : existing) backupNativeSteamArtworkFile(path, accountBackup);
        const auto destination = clear ? fs::path{} : grid / (toWide(stem) + staged.extension().wstring());
        if (!clear) copyHostFileAtomic(staged, destination);
        // On any copy/activation failure, all previous extensions still exist.
        // Only an explicit delete or successfully activated replacement may
        // remove the old variants. Backups remain available on cleanup error.
        for (const auto& path : existing) {
            if (!clear && samePath(path, destination)) continue;
            std::error_code error;
            fs::remove(path, error);
            if (error) throw std::runtime_error("原生 Steam 素材已备份，但旧扩展名清理失败：" + toUtf8(path.wstring()));
        }
    }
    if (!staged.empty()) {
        std::error_code error;
        fs::remove(staged, error);
    }
}

static void requireSuccess(const ProcessResult& result) {
    if (result.exitCode != 0) {
        if (result.output.find("TASK_CANCELLED") != std::string::npos) {
            throw std::runtime_error("任务已取消，残留事务将在下次打开时自动清理");
        }
        if (result.output.find("原子输出文件激活失败") != std::string::npos) {
            throw std::runtime_error(result.output);
        }
        if (result.output.find("错误：隔离任务超时") != std::string::npos) {
            throw std::runtime_error("隔离任务超时，残留事务将在下次打开时自动清理");
        }
        if (result.output.find("No library roots are configured") != std::string::npos) {
            throw std::runtime_error("还没有配置游戏库扫描目录；请点击“添加扫描目录”，或重新打开游戏库完成首次自动发现。 ");
        }
        throw std::runtime_error(result.output.empty() ? "隔离任务失败" : "隔离任务失败：" + result.output);
    }
}

static json workspaceSnapshot(
    bool refreshPlan,
    const json& requestedTarget = json::object(),
    const BackgroundJob& backgroundJob = nullptr,
    bool refreshNativeArtwork = false) {
    json snapshot;
    snapshot["config"] = readJson(g_dataRoot / L"config" / L"library-config.json", json::object());
    snapshot["manualOverrides"] = readJson(
        g_dataRoot / L"config" / L"manual-overrides.json", json{{"items", json::object()}});
    // Availability is UI metadata only; never rewrite user overrides here.
    if (!snapshot["manualOverrides"].is_object()) snapshot["manualOverrides"] = {{"items", json::object()}};
    auto& manualItems = snapshot["manualOverrides"]["items"];
    if (manualItems.is_object()) for (auto& [key, item] : manualItems.items()) {
        if (!item.is_object()) continue;
        for (const auto* type : {"cover", "long", "wallpaper"}) {
            if (!item.contains(type) || !item[type].is_object()) continue;
            const auto source = hostManifestArtworkSource(item[type]);
            item[type]["available"] = !source.empty();
            if (!source.empty()) item[type]["portableFile"] = toUtf8(source.wstring());
        }
    }
    attachFailureCandidates(snapshot);
    attachResolvedIdentityCache(snapshot);
    snapshot["library"] = readJson(g_dataRoot / L"state" / L"library-scan.json", json{{"games", json::array()}});
    for (const auto* section : {"games", "unscannedGames", "missingGames"}) {
        snapshot["library"][section] = custom_steam_library::uniqueExecutableInventory(
            jsonArrayOr(snapshot["library"], section), [](const std::string& path) {
                return toUtf8(normalizedPathKey(fs::path(toWide(path))));
            });
    }
    // Ordinary state refreshes must stay local and fast. Steam account
    // discovery is an explicit command/plan operation, not part of every
    // manual edit or artwork refresh.
    snapshot["steamAccounts"] = readJson(
        g_dataRoot / L"state" / L"steam-accounts.json",
        json{{"schemaVersion", 1}, {"accounts", json::array()}});
    const auto selectedTarget = steamTargetSnapshot();
    const auto localFallbackPlan = buildLocalSteamPlanSnapshot(snapshot);
    if (refreshPlan) {
        auto target = requestedTarget.empty() ? selectedTarget : requestedTarget;
        // Account discovery is detached from the ordinary local snapshot, but
        // every explicit plan refresh should publish its result to the UI.
        // This distinguishes no Steam, no signed-in userdata account, and an
        // installed account whose shortcuts.vdf is still empty.
        if (jsonStringOr(target, "selectionMode") == "all-steam-accounts" || target.empty()) {
            const auto discoveredAccounts = readSteamAccounts(json::array(), backgroundJob);
            snapshot["steamAccounts"] = discoveredAccounts;
        }
        if (jsonStringOr(target, "selectionMode") == "all-steam-accounts" && jsonStringOr(target, "accountId").empty()) {
            const auto accounts = jsonArrayOr(snapshot["steamAccounts"], "accounts");
            if (!accounts.empty()) {
                target["accountId"] = jsonStringOr(accounts.front(), "accountId");
                target["steamRoot"] = jsonStringOr(accounts.front(), "steamRoot");
            }
        }
        std::vector<std::wstring> arguments = {L"--plan-add-library-to-steam"};
        const auto targetArguments = steamTargetArguments(target);
        arguments.insert(arguments.end(), targetArguments.begin(), targetArguments.end());
        arguments.push_back(L"--output");
        arguments.push_back(workspacePlanPath().wstring());
        // Plan generation is read-only and may touch Steam files.  Keep the
        // snapshot path bounded so a slow/disconnected Steam install cannot
        // hold the UI request forever; the newest matching cached plan is
        // returned below when this worker times out or fails.
        auto* cancelFlag = backgroundJob ? &backgroundJob->cancelRequested : nullptr;
        auto* activeProcess = backgroundJob ? &backgroundJob->activeProcess : nullptr;
        const auto result = runWorkerBounded(arguments, kLocalScanWorkerTimeoutMs, cancelFlag, activeProcess);
        if (result.exitCode == 0) {
            const auto refreshed = readJson(workspacePlanPath(), json::object());
            if (isPlanForCurrentLibrary(refreshed)) {
                snapshot["steamPlan"] = refreshed;
                snapshot["steamPlanSource"] = toUtf8(workspacePlanPath().wstring());
                snapshot["steamPlanError"] = nullptr;
            } else {
                const auto cached = newestCachedSteamPlan(selectedTarget.empty());
                snapshot["steamPlan"] = cached.source.empty() ? localFallbackPlan : cached.document;
                snapshot["steamPlanSource"] = cached.source.empty()
                    ? "local-scrape-fallback" : toUtf8(cached.source.wstring());
                snapshot["steamPlanError"] = "Steam 计划已生成，但与当前游戏库状态不一致";
            }
        } else {
            // Keep the newest matching read-only plan visible when a local
            // re-plan fails.  The batch plan is eligible only before the UI
            // has selected a target, so it can never replace a per-account or
            // per-selection plan.
            const auto cached = newestCachedSteamPlan(selectedTarget.empty());
            snapshot["steamPlan"] = cached.source.empty() ? localFallbackPlan : cached.document;
            snapshot["steamPlanSource"] = cached.source.empty()
                ? "local-scrape-fallback" : toUtf8(cached.source.wstring());
            snapshot["steamPlanError"] = result.output;
        }
    } else {
        const auto cached = newestCachedSteamPlan(selectedTarget.empty());
        snapshot["steamPlan"] = cached.source.empty() ? localFallbackPlan : cached.document;
        snapshot["steamPlanSource"] = cached.source.empty()
            ? "local-scrape-fallback" : toUtf8(cached.source.wstring());
        snapshot["steamPlanError"] = nullptr;
    }
    snapshot["steamPlan"]["items"] = custom_steam_library::uniqueExecutableInventory(
        jsonArrayOr(snapshot["steamPlan"], "items"), [](const std::string& path) {
            return toUtf8(normalizedPathKey(fs::path(toWide(path))));
        });
    attachWorkspaceArtwork(snapshot["steamPlan"], snapshot["steamAccounts"], refreshNativeArtwork);
    snapshot["taskRunning"] = g_taskRunning.load();
    snapshot["backgroundTaskRunning"] = backgroundLibraryTaskRunning();
    {
        std::lock_guard<std::mutex> lock(g_scrapeStateMutex);
        snapshot["scrape"] = g_scrapeState;
    }
    snapshot["dataRoot"] = toUtf8(g_dataRoot.wstring());
    return snapshot;
}

static void prepareMissingLibraryData() {
    std::vector<std::wstring> arguments = {
        L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File",
        (g_labRoot / L"prepare-library-for-steam.ps1").wstring()
    };
    requireSuccess(runProcess(L"powershell.exe", arguments));
}

static bool automaticScrapingEnabled() {
    const auto config = readJson(g_dataRoot / L"config" / L"library-config.json", json::object());
    return jsonBoolOr(config, "automaticScrapingEnabled", true);
}
static json manualArtworkOverrideForExecutable(const json& overrides, const std::string& executable) {
    if (!overrides.is_object()) return json::object();
    const auto items = jsonObjectOr(overrides, "items");
    const auto wanted = normalizedPathKey(fs::path(toWide(executable)));
    for (const auto& [key, value] : items.items()) {
        if (normalizedPathKey(fs::path(toWide(key))) == wanted && value.is_object()) return value;
    }
    return json::object();
}
static bool manualArtworkProtected(const json& overrides, const std::string& executable) {
    const auto item = manualArtworkOverrideForExecutable(overrides, executable);
    const auto policy = jsonObjectOr(item, "artworkProtection");
    // Only a game with every UI slot deliberately controlled by the user can
    // skip automatic artwork. One manual cover must not block its background.
    for (const auto* type : {"cover", "long", "wallpaper"}) {
        if (jsonStringOr(policy, type) != "deleted" && hostManifestArtworkSource(jsonObjectOr(item, type)).empty()) return false;
    }
    return true;
}

static constexpr int kAutomaticIdentityWorkerRetries = 3;
static constexpr int64_t kAutomaticIdentityRetryBaseMs = 30000;
static constexpr int64_t kAutomaticIdentityRetryMaxMs = 10 * 60 * 1000;
static constexpr int64_t kAutomaticIdentitySuccessCooldownMs = 30 * 60 * 1000;

static bool automaticIdentityCacheExists(const std::string& executable) {
    try {
        const auto cachePath = g_dataRoot / L"cache" / L"identity" /
            toWide(safeArtworkSegment(toUtf8(normalizedPathKey(fs::path(toWide(executable)))) ) + ".json");
        std::error_code error;
        return fs::is_regular_file(cachePath, error) && !error;
    } catch (...) {
        return false;
    }
}

static bool automaticIdentityAttempted(const json& overrides, const std::string& executable) {
    const auto item = manualArtworkOverrideForExecutable(overrides, executable);
    if (!item.is_object()) return false;
    // Success cooldown only applies while its durable identity cache exists.
    if (jsonStringOr(item, "automaticIdentityLastStatus") == "completed" &&
        !automaticIdentityCacheExists(executable)) return false;
    const auto retryAt = jsonIntegerOr(item, "automaticIdentityRetryAfterAt", 0);
    if (retryAt > unixTimeMsHost()) return true;
    // Older configuration files only have automaticIdentityAttemptedAt.  Keep
    // their old suppression behavior for a short cooldown, then allow the
    // improved retry policy to take over instead of abandoning the game.
    const auto attemptedAt = jsonIntegerOr(item, "automaticIdentityAttemptedAt", 0);
    return attemptedAt > 0 && attemptedAt + kAutomaticIdentityRetryBaseMs > unixTimeMsHost();
}
static void markAutomaticIdentityAttempt(const std::string& executable) {
    // Only the read/modify/write transaction is protected.  Do not use the
    // task boundary as a lock for the whole weak-network scrape job: manual
    // edits and Steam commit commands must remain available while scraping.
    std::lock_guard<std::mutex> boundaryLock(g_taskBoundaryMutex);
    const auto path = g_dataRoot / L"config" / L"manual-overrides.json";
    HostDataTransaction configTransaction;

    auto overrides = readJson(path, json{{"schemaVersion", 1}, {"items", json::object()}});
    if (!overrides.is_object()) overrides = json::object();
    overrides["schemaVersion"] = 1;
    if (!overrides.contains("items") || !overrides["items"].is_object()) overrides["items"] = json::object();
    auto& item = overrides["items"][toUtf8(normalizedPathKey(fs::path(toWide(executable))))];
    if (!item.is_object()) item = json::object();
    const auto attempts = jsonIntegerOr(item, "automaticIdentityAttemptCount", 0);
    const auto now = unixTimeMsHost();
    item["automaticIdentityAttemptedAt"] = unixTimeMsHost();
    item["automaticIdentityAttemptCount"] = attempts + 1;
    item["automaticIdentityRetryAfterAt"] = now + kAutomaticIdentityRetryBaseMs;
    item["automaticIdentityLastStatus"] = "running";
    item["automaticIdentityAttemptSource"] = "new-game-first-pass";
    item["updatedAt"] = now;
    writeJsonWithLocalBackup(path, overrides, false);

    configTransaction.release();
}

static void recordAutomaticIdentityResult(
    const std::string& executable, bool success, const std::string& error = {}) {
    std::lock_guard<std::mutex> boundaryLock(g_taskBoundaryMutex);
    const auto path = g_dataRoot / L"config" / L"manual-overrides.json";
    HostDataTransaction configTransaction;

    auto overrides = readJson(path, json{{"schemaVersion", 1}, {"items", json::object()}});
    if (!overrides.is_object()) overrides = json::object();
    overrides["schemaVersion"] = 1;
    if (!overrides.contains("items") || !overrides["items"].is_object()) {
        overrides["items"] = json::object();
    }
    auto& item = overrides["items"][toUtf8(normalizedPathKey(fs::path(toWide(executable))))];
    if (!item.is_object()) item = json::object();
    const auto attempts = jsonIntegerOr(item, "automaticIdentityAttemptCount", 1);
    const auto now = unixTimeMsHost();
    int64_t delay = kAutomaticIdentitySuccessCooldownMs;
    if (!success) {
        const auto exponent = (std::max)(int64_t{0}, (std::min)(attempts - 1, int64_t{8}));
        delay = (std::min)(kAutomaticIdentityRetryMaxMs,
            kAutomaticIdentityRetryBaseMs * (int64_t{1} << exponent));
    }
    item["automaticIdentityAttemptedAt"] = now;
    item["automaticIdentityLastStatus"] = success ? "completed" : "failed";
    item["automaticIdentityRetryAfterAt"] = now + delay;
    item["automaticIdentityLastError"] = error.empty() ? json(nullptr) : json(error);
    item["updatedAt"] = now;
    writeJsonWithLocalBackup(path, overrides, false);

    configTransaction.release();
}

// Forward declarations for the non-blocking open-library pipeline.
static void startLibraryPipeline(bool allowFirstRunDefaults = false);
static bool startScrapeQueue(bool forceManual = false);
static bool startManualIdentifyTask(const json& arguments);
static json startIdentitySearchTask(const json& arguments);
static void startArtworkTask(uint64_t id, std::string command, json arguments);
static void startNetworkWakeTask(uint64_t id);
static void postPipelineEvent(const std::string& event, json payload = json::object()) noexcept;
static json identitySearchCandidatesFromDirectory(const fs::path& outputDirectory);
static json identitySearchCachedCandidates(const fs::path& executable, const std::string& query = {});
static json commitToAllSteamAccounts(const json& requestedTarget);
static json deleteFromAllSteamAccounts(const json& requestedTarget);
static void collectIdentitySearchCandidates(const json& value, std::vector<json>& output, const std::string& providerHint = {});

static bool firstRunDefaultScanPending() {
    const auto config = readJson(g_dataRoot / L"config" / L"library-config.json", json::object());
    const auto roots = jsonArrayOr(config, "roots");
    // Explicitly clearing the last root is a valid user choice, not first run.
    if (config.value("scanRootsExplicitlyCleared", false)) return false;
    // A zero-root configuration is never a usable completed scan.  Older
    // builds could persist status=completed after discovering no candidates,
    // and then a later open called --scan-library without --first-run-defaults
    // and surfaced the raw worker error.  Treat every empty-root state as
    // retryable; this is local directory discovery only and remains opt-in to
    // network scraping through the existing automaticScrapingEnabled switch.
    return roots.empty();
}

static std::vector<std::wstring> libraryScanWorkerArguments(bool allowFirstRunDefaults = true) {
    std::vector<std::wstring> arguments = {L"--scan-library"};
    if (allowFirstRunDefaults && firstRunDefaultScanPending())
        arguments.push_back(L"--first-run-defaults");
    return arguments;
}

// Automatic identity search is a background lane in its own right.  Keep the
// request boundary cheap: it does not walk historical cache/output trees or
// wait for a provider.  Candidates are read from this request's worker output
// when its completion event is posted.  The shared BackgroundJob/generation
// coordinator coalesces repeated requests for the same executable while
// allowing different executables and the library scrape queue to continue
// independently.
static json startIdentitySearchRequest(const json& arguments) {
    custom_steam_library::rejectBuiltinExcludedSteamTool(arguments);
    const auto executableText = jsonStringOr(arguments, "executable");
    const auto query = trimHost(jsonStringOr(arguments, "query"));
    if (executableText.empty() || query.empty()) throw std::runtime_error("搜索游戏需要主程序和名称");
    const fs::path executable = fs::path(toWide(executableText));
    if (!fs::is_regular_file(executable)) throw std::runtime_error("游戏主程序不存在");

    auto task = startIdentitySearchTask(arguments);
    task["query"] = query;
    task["pending"] = true;
    task["cached"] = false;
    task["candidates"] = json::array();
    return task;
}

static json executeWorkspaceCommand(
    const std::string& command,
    const json& arguments,
    const BackgroundJob& backgroundJob = nullptr) {
    if (command == "closeWorkspace") {
        // The old entry page used B to return to the first-level menu.  The
        // standalone executable now opens directly into the library, so B
        // and the header action close this workspace.  Parent mode still
        // closes only this child window and lets YeManCC restore itself.
        PostMessageW(g_window, WM_CLOSE, 0, 0);
        return json{{"closing", true}};
    }
    if (command == "uiError") {
        appendUiError(arguments);
        return json{{"recorded", true}};
    }
    // A state refresh must also rebuild the local add plan.  Recognition can
    // update library-scan.json and the batch plan while the workspace stays
    // open; merely checking that an older plan file exists left the UI stuck
    // on its previous ready-to-add count.
    if (command == "state") return workspaceSnapshot(false);
    if (command == "steamAccounts") {
        json roots = json::array();
        for (const auto& root : jsonArrayOr(arguments, "steamRoots")) {
            if (root.is_string() && !root.get<std::string>().empty()) roots.push_back(root);
        }
        return readSteamAccounts(roots);
    }
    if (command == "steamCommitPreflight") {
        std::wstring runningPath;
        const bool running = hostProcessRunningByName(L"steam.exe", &runningPath);
        return json{{"steamRunning", running}, {"steamPath", toUtf8(runningPath)},
            {"requiresClose", running}, {"restartBigPicture", running}};
    }
    if (command == "closeSteam") {
        std::wstring runningPath;
        const bool wasRunning = hostProcessRunningByName(L"steam.exe", &runningPath);
        if (wasRunning) closeSteamForShortcutCommit();
        std::wstring remainingPath;
        const bool stillRunning = hostProcessRunningByName(L"steam.exe", &remainingPath);
        return json{{"steamRunning", stillRunning}, {"steamPath", toUtf8(stillRunning ? remainingPath : runningPath)},
            {"closed", wasRunning && !stillRunning}};
    }
    if (command == "startup") {
        // Startup is the one automatic native-Steam artwork refresh.  The
        // result is copied into the portable cache; opening the library and
        // every background scrape then use that cache without touching Steam.
        return workspaceSnapshot(false, json::object(), nullptr, true);
    }
    if (command == "refreshNativeArtwork") {
        const auto executableText = jsonStringOr(arguments, "executable");
        if (executableText.empty()) throw std::runtime_error("缺少原生 Steam 游戏主程序路径");
        const auto executable = fs::path(toWide(executableText));
        if (!nativeSteamRecordForExecutable(executable).has_value()) {
            throw std::runtime_error("当前游戏不是原生 Steam 游戏");
        }
        // Manual editor entry is an explicit refresh point for local Steam
        // artwork.  It does not enqueue scraping or use network providers.
        return workspaceSnapshot(false, json::object(), nullptr, true);
    }
    // Opening the full library is intentionally non-blocking.  Return the
    // cached/local snapshot immediately, then refresh the directory and run
    // the weak-network scrape queue outside the UI command lane.
    if (command == "openLibrary") {
        startLibraryPipeline(true);
        return workspaceSnapshot(false);
    }
    if (command == "scan") {
        // Manual scan is also a first-run entry point.  New users often press
        // this button before opening the library once; without the flag the
        // worker had no --root and returned "No library roots...".
        startLibraryPipeline(true);
        return workspaceSnapshot(false);
    }
    if (command == "plan") {
        setSteamTarget(arguments);
        return workspaceSnapshot(true, arguments);
    }
    if (command == "prepare") {
        const bool started = startScrapeQueue(true);
        auto result = workspaceSnapshot(false);
        result["scrapeRequest"] = {
            {"started", started},
            {"queued", !started},
            {"mode", "manual-refresh"}
        };
        return result;
    }
    if (command == "commit") {
        std::lock_guard<std::mutex> steamOperationLock(g_steamOperationMutex);
        setSteamTarget(arguments);
        const auto target = arguments;
        const bool forceClose = jsonBoolOr(target, "forceCloseSteam", false);
        const bool restartBigPicture = jsonBoolOr(target, "restartSteamBigPicture", false);
        if (forceClose && hostProcessRunningByName(L"steam.exe")) closeSteamForShortcutCommit();
        try {
            if (target.value("selectionMode", std::string{}) == "all-steam-accounts") {
                commitToAllSteamAccounts(target);
            } else {
                std::vector<std::wstring> workerArguments = {L"--commit-add-library-to-steam", L"--confirm-steam-closed"};
                const auto targetArguments = steamTargetArguments(target);
                workerArguments.insert(workerArguments.end(), targetArguments.begin(), targetArguments.end());
                workerArguments.push_back(L"--output");
                workerArguments.push_back(workspacePlanPath().wstring());
                requireSuccess(runWorker(workerArguments));
            }
        } catch (...) {
            if (restartBigPicture) {
                try { restartSteamBigPicture(target); } catch (...) { }
            }
            throw;
        }
        if (restartBigPicture) restartSteamBigPicture(target);
        return workspaceSnapshot(true, target);
    }
    if (command == "refreshSteamArtwork") {
        std::lock_guard<std::mutex> steamOperationLock(g_steamOperationMutex);
        setSteamTarget(arguments);
        const auto target = arguments;
        const bool forceClose = jsonBoolOr(target, "forceCloseSteam", false);
        const bool restartBigPicture = jsonBoolOr(target, "restartSteamBigPicture", false);
        if (forceClose && hostProcessRunningByName(L"steam.exe")) closeSteamForShortcutCommit();
        try {
            refreshArtworkForAllSteamAccounts(target);
        } catch (...) {
            if (restartBigPicture) { try { restartSteamBigPicture(target); } catch (...) { } }
            throw;
        }
        if (restartBigPicture) restartSteamBigPicture(target);
        return workspaceSnapshot(true, target);
    }
    if (command == "deleteFromSteam") {
        std::lock_guard<std::mutex> steamOperationLock(g_steamOperationMutex);
        setSteamTarget(arguments);
        const auto target = arguments;
        if (target.value("selectionMode", std::string{}) == "all-steam-accounts") {
            deleteFromAllSteamAccounts(target);
            return workspaceSnapshot(true, target);
        }
        std::vector<std::wstring> workerArguments = {L"--commit-delete-library-from-steam", L"--confirm-steam-closed"};
        const auto targetArguments = steamTargetArguments(target);
        workerArguments.insert(workerArguments.end(), targetArguments.begin(), targetArguments.end());
        for (const auto& directory : jsonArrayOr(arguments, "selectedGameDirectories")) {
            if (directory.is_string()) { workerArguments.push_back(L"--select-game"); workerArguments.push_back(toWide(directory.get<std::string>())); }
        }
        workerArguments.push_back(L"--output");
        workerArguments.push_back((g_dataRoot / L"state" / L"steam-delete-plan.json").wstring());
        requireSuccess(runWorker(workerArguments));
        return workspaceSnapshot(false);
    }
    if (command == "searchIdentity") {
        return startIdentitySearchRequest(arguments);
    }
    if (command == "setPrimary") {
        const auto gameDirectory = toWide(jsonStringOr(arguments, "gameDirectory"));
        const auto executable = toWide(jsonStringOr(arguments, "executable"));
        if (gameDirectory.empty() || executable.empty()) throw std::runtime_error("Missing primary executable selection");
        requireSuccess(runWorker({L"--set-library-primary", L"--game-dir", gameDirectory, L"--exe", executable}));
        startLibraryPipeline();
        return workspaceSnapshot(false);
    }
    if (command == "identifyOne") {
        custom_steam_library::rejectBuiltinExcludedSteamTool(arguments);
        const auto executableText = jsonStringOr(arguments, "executable");
        const auto manualName = jsonStringOr(arguments, "manualName");
        const auto steamId = jsonStringOr(arguments, "steamId");
        const auto igdbId = jsonStringOr(arguments, "igdbId");
        if (executableText.empty()) throw std::runtime_error("请选择需要识别的主 EXE");
        if (!steamId.empty() && !igdbId.empty()) throw std::runtime_error("Steam AppID 与 IGDB ID 只能填写一个");
        if (manualName.empty() && steamId.empty() && igdbId.empty()) {
            throw std::runtime_error("请填写游戏名称、Steam AppID 或 IGDB ID");
        }
        if (manualName.size() > 300) throw std::runtime_error("游戏名称过长");
        auto validateId = [](const std::string& value) {
            return !value.empty() && value.size() <= 20 &&
                std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch); }) &&
                value != "0";
        };
        if ((!steamId.empty() && !validateId(steamId)) || (!igdbId.empty() && !validateId(igdbId))) {
            throw std::runtime_error("商店 ID 必须是正整数");
        }
        const fs::path executable = fs::path(toWide(executableText));
        if (!fs::is_regular_file(executable)) throw std::runtime_error("主 EXE 不存在");
        bool knownPrimary = false;
        bool nativeSteam = false;
        const auto library = readJson(g_dataRoot / L"state" / L"library-scan.json", json::object());
        for (const auto& game : jsonArrayOr(library, "games")) {
            const auto primary = jsonStringOr(game, "primaryExecutable");
            if (!primary.empty() && samePath(executable, fs::path(toWide(primary)))) {
                knownPrimary = true;
                nativeSteam = isNativeSteamRecord(game);
                break;
            }
        }
        if (!knownPrimary) throw std::runtime_error("该 EXE 不是当前扫描状态中的已确认主程序");
        if (nativeSteam && (!steamId.empty() || !igdbId.empty())) {
            throw std::runtime_error("原生 Steam AppID 由 Steam appmanifest 管理，不能通过手动识别改写");
        }
        // Manual identity is a two-phase operation like automatic scraping:
        // commit the user's identity choice first, then let a detached worker
        // retry network enrichment.  Artwork failure must never roll back the
        // name/AppID that the user explicitly selected.
        {
            std::lock_guard<std::mutex> submissionLock(g_manualSubmissionMutex);
            std::lock_guard<std::mutex> boundaryLock(g_taskBoundaryMutex);
            const auto overridesPath = g_dataRoot / L"config" / L"manual-overrides.json";
            HostDataTransaction configTransaction;

            auto overrides = readJson(overridesPath, json{{"schemaVersion", 1}, {"items", json::object()}});
            if (!overrides.is_object()) overrides = json::object();
            overrides["schemaVersion"] = 1;
            if (!overrides.contains("items") || !overrides["items"].is_object()) overrides["items"] = json::object();
            const auto key = toUtf8(normalizedPathKey(executable));
            auto& item = overrides["items"][key];
            if (!item.is_object()) item = json::object();
            if (!manualName.empty()) { item["name"] = manualName; item["source"] = "candidate-selection"; }
            if (nativeSteam) {
                item["identityStatus"] = "native-steam";
                item["retryable"] = false;
                item["lastError"] = nullptr;
                item.erase("steamId");
                item.erase("igdbId");
                item.erase("idCleared");
                item["updatedAt"] = unixTimeMsHost();
                writeJsonWithLocalBackup(overridesPath, overrides);

                configTransaction.release();
                return workspaceSnapshot(false);
            }
            if (!steamId.empty()) { item["steamId"] = std::stoll(steamId); item.erase("igdbId"); item.erase("idCleared"); }
            if (!igdbId.empty()) { item["igdbId"] = std::stoll(igdbId); item.erase("steamId"); item.erase("idCleared"); }
            item["identityStatus"] = "pending-network";
            item["retryable"] = true;
            item["lastError"] = nullptr;
            item["attempts"] = 0;
            item["updatedAt"] = unixTimeMsHost();
            writeJsonWithLocalBackup(overridesPath, overrides);

            configTransaction.release();
            if (!startManualIdentifyTask(arguments)) {
                throw std::runtime_error("无法启动后台识别任务；名称和 AppID 已保留，可稍后重试");
            }
        }
        return workspaceSnapshot(false);
    }
    if (command == "addRoot" || command == "removeRoot") {
        const auto requested = fs::path(toWide(jsonStringOr(arguments, "path")));
        if (requested.empty()) throw std::runtime_error("扫描根目录不能为空");
        if (command == "addRoot" && !fs::is_directory(requested)) {
            throw std::runtime_error("扫描根目录不存在");
        }
        const auto config = readJson(g_dataRoot / L"config" / L"library-config.json", json::object());
        std::vector<fs::path> roots;
        for (const auto& value : jsonArrayOr(config, "roots")) {
            if (value.is_string()) roots.emplace_back(toWide(value.get<std::string>()));
        }
        if (command == "addRoot") {
            if (std::none_of(roots.begin(), roots.end(), [&](const auto& root) { return samePath(root, requested); })) {
                roots.push_back(requested);
            }
        } else {
            roots.erase(std::remove_if(roots.begin(), roots.end(),
                [&](const auto& root) { return samePath(root, requested); }), roots.end());
        }
        std::vector<std::wstring> workerArguments = {L"--scan-library"};
        if (roots.empty()) workerArguments.push_back(L"--clear-roots");
        for (const auto& root : roots) {
            workerArguments.push_back(L"--root");
            workerArguments.push_back(root.wstring());
        }
        workerArguments.push_back(L"--save-roots");
        requireSuccess(runWorker(workerArguments));
        return workspaceSnapshot(true);
    }
    if (command == "addGame") {
        const auto requested = fs::path(toWide(jsonStringOr(arguments, "path")));
        if (requested.empty()) throw std::runtime_error("游戏目录或 EXE 路径不能为空");
        std::error_code pathError;
        fs::path directory = requested;
        if (fs::is_regular_file(requested, pathError)) {
            if (lowerAscii(toUtf8(requested.extension().wstring())) != ".exe") {
                throw std::runtime_error("单独添加只能选择 EXE 文件或游戏目录");
            }
            directory = requested.parent_path();
        }
        pathError.clear();
        if (!fs::is_directory(directory, pathError)) {
            throw std::runtime_error("请选择已存在的游戏目录，或选择一个已存在的 EXE 文件");
        }
        std::error_code canonicalError;
        const auto canonicalDirectory = fs::weakly_canonical(directory, canonicalError);
        const auto directoryToAdd = canonicalError ? fs::absolute(directory) : canonicalDirectory;
        requireSuccess(runWorker({L"--add-library-game", L"--game-dir", directoryToAdd.wstring()}));
        requireSuccess(runWorker(libraryScanWorkerArguments(true)));
        return workspaceSnapshot(true);
    }
    if (command == "removeGame") {
        const auto directory = toWide(jsonStringOr(arguments, "gameDirectory"));
        if (directory.empty()) throw std::runtime_error("游戏目录不能为空");
        requireSuccess(runWorker({L"--remove-library-game", L"--game-dir", directory}));
        requireSuccess(runWorker(libraryScanWorkerArguments(true)));
        return workspaceSnapshot(true);
    }
    if (command == "backup") {
        requireSuccess(runWorker({L"--backup-library-data"}));
        return workspaceSnapshot(false);
    }
    if (command == "setEnabled") {
        requireSuccess(runWorker({L"--set-library-scan-enabled", jsonBoolOr(arguments, "enabled", true) ? L"on" : L"off"}));
        return workspaceSnapshot(false);
    }
    if (command == "setScrapingEnabled") {
        requireSuccess(runWorker({L"--set-library-scraping-enabled", jsonBoolOr(arguments, "enabled", true) ? L"on" : L"off"}));
        return workspaceSnapshot(false);
    }
    if (command == "saveManualName") {
        std::lock_guard<std::mutex> boundaryLock(g_taskBoundaryMutex);
        const auto executableText = jsonStringOr(arguments, "executable");
        const auto name = trimHost(jsonStringOr(arguments, "name"));
        if (executableText.empty() || name.empty()) throw std::runtime_error("保存内容需要主程序和名称");
        const fs::path executable = fs::path(toWide(executableText));
        if (!fs::is_regular_file(executable)) throw std::runtime_error("游戏主程序不存在");
        const auto overridesPath = g_dataRoot / L"config" / L"manual-overrides.json";
        HostDataTransaction configTransaction;

        auto overrides = readJson(overridesPath, json{{"schemaVersion", 1}, {"items", json::object()}});
        if (!overrides.is_object()) overrides = json::object();
        overrides["schemaVersion"] = 1;
        if (!overrides.contains("items") || !overrides["items"].is_object()) overrides["items"] = json::object();
        const auto key = toUtf8(normalizedPathKey(executable));
        auto& item = overrides["items"][key];
        if (!item.is_object()) item = json::object();
        item["name"] = name;
        item["source"] = "manual";
        item["updatedAt"] = unixTimeMsHost();
        writeJsonWithLocalBackup(overridesPath, overrides);

        configTransaction.release();
        return workspaceSnapshot(false);
    }
    if (command == "clearManualSteamId") {
        std::lock_guard<std::mutex> boundaryLock(g_taskBoundaryMutex);
        const auto executableText = jsonStringOr(arguments, "executable");
        if (executableText.empty()) throw std::runtime_error("清空 Steam AppID 需要主程序");
        const fs::path executable = fs::path(toWide(executableText));
        if (!fs::is_regular_file(executable)) throw std::runtime_error("游戏主程序不存在");
        const auto overridesPath = g_dataRoot / L"config" / L"manual-overrides.json";
        HostDataTransaction configTransaction;

        auto overrides = readJson(overridesPath, json{{"schemaVersion", 1}, {"items", json::object()}});
        if (!overrides.is_object()) overrides = json::object();
        overrides["schemaVersion"] = 1;
        if (!overrides.contains("items") || !overrides["items"].is_object()) overrides["items"] = json::object();
        const auto key = toUtf8(normalizedPathKey(executable));
        auto it = overrides["items"].find(key);
        if (it == overrides["items"].end()) {
            overrides["items"][key] = json::object();
            it = overrides["items"].find(key);
        }
        if (it != overrides["items"].end() && it->is_object()) {
            // Explicit tombstone prevents historical plans/caches from
            // resurrecting a user-cleared Steam AppID on the next refresh.
            (*it)["idCleared"] = true;
            it->erase("steamId");
            it->erase("storefrontSteamId");
            it->erase("formalName");
            it->erase("igdbId");
            it->erase("identityStatus");
            it->erase("retryable");
            it->erase("lastError");
            it->erase("attempts");
            it->operator[]("updatedAt") = unixTimeMsHost();
            if (it->size() == 1 && it->contains("updatedAt")) overrides["items"].erase(key);
        }
        writeJsonWithLocalBackup(overridesPath, overrides);

        configTransaction.release();
        return workspaceSnapshot(false);
    }
    if (command == "setBucket") {
        const auto directory = toWide(jsonStringOr(arguments, "gameDirectory"));
        const auto bucket = toWide(jsonStringOr(arguments, "bucket"));
        if (directory.empty() || bucket.empty()) throw std::runtime_error("Missing game category selection");
        requireSuccess(runWorker({L"--set-library-bucket", L"--game-dir", directory, L"--bucket", bucket}));
        return workspaceSnapshot(false);
    }
    if (command == "searchArtwork") {
        custom_steam_library::rejectBuiltinExcludedSteamTool(arguments);
        const auto query = jsonStringOr(arguments, "query");
        const auto type = jsonStringOr(arguments, "type", "cover");
        const auto steamAppId = jsonStringOr(arguments, "steamAppId");
        if (query.empty()) throw std::runtime_error("搜索词不能为空");
        if (type != "cover" && type != "long" && type != "wallpaper") throw std::runtime_error("素材类型不受支持");
        std::vector<std::wstring> searchArgs = {L"--search-artwork", toWide(query), L"--type", toWide(type)};
        if (!steamAppId.empty()) { searchArgs.push_back(L"--steam-app-id"); searchArgs.push_back(toWide(steamAppId)); }
        auto* cancelFlag = backgroundJob ? &backgroundJob->cancelRequested : &g_cancelRequested;
        auto* activeProcess = backgroundJob ? &backgroundJob->activeProcess : &g_activeProcess;
        const auto result = runWorkerBounded(searchArgs, kArtworkWorkerTimeoutMs, cancelFlag, activeProcess);
        if (result.exitCode != 0) requireSuccess(result);
        const std::string marker = "ARTWORK_SEARCH_JSON: ";
        const auto position = result.output.rfind(marker);
        if (position == std::string::npos) throw std::runtime_error("素材搜索没有返回候选数据");
        auto payload = json::parse(result.output.substr(position + marker.size()));
        payload["opened"] = false;
        payload["sizeRule"] = artworkSizeRule(type);
        payload["filterInstruction"] = type == "wallpaper"
            ? "Steam 官方优先，Playnite-IGDB 次之，百度图片兜底；至少 800×250，1920×620 或更大优先。"
            : type == "long" ? "Steam 官方优先，Playnite-IGDB 次之，百度图片兜底；至少 300×140，1196×559 或更大优先。"
            : "Steam 官方优先，Playnite-IGDB 次之，百度图片兜底；至少 240×360，600×900 或更大优先。";
        return payload;
    }
    if (command == "downloadArtworkCandidate") {
        const auto executableText = jsonStringOr(arguments, "executable");
        const auto url = jsonStringOr(arguments, "url");
        const auto type = jsonStringOr(arguments, "type", "cover");
        if (executableText.empty() || url.empty()) throw std::runtime_error("缺少素材候选或游戏主程序");
        if (type != "cover" && type != "long" && type != "wallpaper") throw std::runtime_error("素材类型不受支持");
        const fs::path executable = fs::path(toWide(executableText));
        if (!fs::is_regular_file(executable)) throw std::runtime_error("游戏主程序不存在");
        const auto output = g_dataRoot / L"cache" / L"artwork-search" /
            toWide(artworkGameKey(executable) + "-" + type + "-" + std::to_string(unixTimeMsHost()) + ".jpg");
        auto* cancelFlag = backgroundJob ? &backgroundJob->cancelRequested : &g_cancelRequested;
        auto* activeProcess = backgroundJob ? &backgroundJob->activeProcess : &g_activeProcess;
        const auto result = runWorkerBounded({L"--download-artwork", L"--url", toWide(url), L"--type", toWide(type), L"--output", output.wstring()}, kArtworkWorkerTimeoutMs, cancelFlag, activeProcess);
        if (result.exitCode != 0) requireSuccess(result);
        const std::string marker = "ARTWORK_DOWNLOAD_JSON: ";
        const auto position = result.output.rfind(marker);
        if (position == std::string::npos) throw std::runtime_error("候选图片下载没有返回结果");
        auto payload = json::parse(result.output.substr(position + marker.size()));
        payload["url"] = url;
        payload["type"] = type;
        return payload;
    }
    if (command == "pickArtwork") {
        wchar_t fileName[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = g_window;
        dialog.lpstrFile = fileName;
        dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
        dialog.lpstrFilter = L"图片文件\0*.jpg;*.jpeg;*.png;*.webp;*.bmp;*.ico\0所有文件\0*.*\0";
        dialog.nFilterIndex = 1;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog)) return json{{"cancelled", true}};
        return json{{"cancelled", false}, {"path", toUtf8(fileName)}};
    }
    if (command == "pickExecutable") {
        wchar_t fileName[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = g_window;
        dialog.lpstrFile = fileName;
        dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
        dialog.lpstrFilter = L"游戏主程序\0*.exe\0所有文件\0*.*\0";
        dialog.nFilterIndex = 1;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog)) return json{{"cancelled", true}};
        return json{{"cancelled", false}, {"path", toUtf8(fileName)}};
    }
    if (command == "pickDirectory") {
        ComPtr<IFileOpenDialog> dialog;
        HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool uninitialize = SUCCEEDED(initResult);
        const HRESULT createResult = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        if (FAILED(createResult) || !dialog) {
            if (uninitialize) CoUninitialize();
            throw std::runtime_error("无法打开文件夹选择器");
        }
        FILEOPENDIALOGOPTIONS options{};
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dialog->SetTitle(L"选择自定义游戏库文件夹");
        const HRESULT showResult = dialog->Show(g_window);
        if (showResult == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            if (uninitialize) CoUninitialize();
            return json{{"cancelled", true}};
        }
        if (FAILED(showResult)) {
            if (uninitialize) CoUninitialize();
            throw std::runtime_error("无法打开文件夹选择器");
        }
        ComPtr<IShellItem> item;
        if (FAILED(dialog->GetResult(&item)) || !item) {
            if (uninitialize) CoUninitialize();
            throw std::runtime_error("未能读取所选文件夹");
        }
        PWSTR selectedPath = nullptr;
        const HRESULT pathResult = item->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath);
        if (FAILED(pathResult) || !selectedPath) {
            if (uninitialize) CoUninitialize();
            throw std::runtime_error("未能读取所选文件夹");
        }
        const std::string path = toUtf8(selectedPath);
        CoTaskMemFree(selectedPath);
        if (uninitialize) CoUninitialize();
        return json{{"cancelled", false}, {"path", path}};
    }
    if (command == "saveArtworkOverride") {
        std::lock_guard<std::mutex> boundaryLock(g_taskBoundaryMutex);
        const auto executableText = jsonStringOr(arguments, "executable");
        if (executableText.empty()) throw std::runtime_error("缺少游戏主程序路径");
        const fs::path executable = fs::path(toWide(executableText));
        if (!fs::is_regular_file(executable)) throw std::runtime_error("游戏主程序不存在");
        const auto coverPath = jsonStringOr(arguments, "coverPath");
        const auto longPath = jsonStringOr(arguments, "longPath");
        const auto wallpaperPath = jsonStringOr(arguments, "wallpaperPath");
        const bool clearCover = jsonBoolOr(arguments, "clearCover", false);
        const bool clearLong = jsonBoolOr(arguments, "clearLong", false);
        const bool clearWallpaper = jsonBoolOr(arguments, "clearWallpaper", false);
        if (coverPath.empty() && longPath.empty() && wallpaperPath.empty() && !clearCover && !clearLong && !clearWallpaper) {
            throw std::runtime_error("没有需要保存的素材变更");
        }
        const auto nativeRecord = nativeSteamRecordForExecutable(executable);
        NativeSteamArtworkTarget nativeTarget;
        const bool isNativeSteam = nativeRecord.has_value();
        if (isNativeSteam) {
            if (hostProcessRunningByName(L"steam.exe")) {
                throw std::runtime_error("原生 Steam 素材写入需要先关闭 Steam；请先退出 Steam 后再保存编辑内容");
            }
            nativeTarget = locateNativeSteamArtworkTarget(executable);
            if (nativeTarget.appId <= 0 || nativeTarget.grids.empty()) {
                throw std::runtime_error("未找到原生 Steam 的本地 grid 目录；请先登录 Steam 后重试");
            }
        }
        const auto overridesPath = g_dataRoot / L"config" / L"manual-overrides.json";
        HostDataTransaction configTransaction;

        auto overrides = readJson(overridesPath, json{{"schemaVersion", 1}, {"items", json::object()}});
        if (!overrides.is_object()) overrides = json::object();
        overrides["schemaVersion"] = 1;
        if (!overrides.contains("items") || !overrides["items"].is_object()) overrides["items"] = json::object();
        const auto key = toUtf8(normalizedPathKey(executable));
        auto& item = overrides["items"][key];
        if (!item.is_object()) item = json::object();
        const auto cacheKey = artworkGameKey(executable);
        auto& artworkProtection = item["artworkProtection"];
        if (!artworkProtection.is_object()) artworkProtection = json::object();
        auto saveOne = [&](const std::string& type, const std::string& sourceText, bool clear) {
            if (clear) {
                item.erase(type);
                // A manual deletion is also a durable user decision: automatic
                // scraping must not immediately recreate this artwork.
                artworkProtection[type] = "deleted";
                return;
            }
            if (sourceText.empty()) return;
            const auto sourcePath = fs::path(toWide(sourceText));
            const auto dimensions = inspectArtworkDimensions(sourcePath);
            const auto sizeRule = artworkSizeRule(type);
            const auto destination = copyArtworkToIsolation(sourcePath, cacheKey, type);
            artworkProtection[type] = "manual";
            item[type] = {
                {"source", "manual"},
                {"file", sourceText},
                {"portableFile", toUtf8(destination.wstring())},
                {"url", mappedDataUrl(destination)},
                {"width", dimensions.known ? dimensions.width : 0},
                {"height", dimensions.known ? dimensions.height : 0},
                {"sizeRule", sizeRule},
                {"updatedAt", unixTimeMsHost()}
            };
        };
        saveOne("cover", coverPath, clearCover);
        saveOne("long", longPath, clearLong);
        saveOne("wallpaper", wallpaperPath, clearWallpaper);
        // Native Steam artwork is intentionally written only from this
        // explicit editor command.  The automatic scraper never enters this
        // path, so a Steam game's local grid cannot be silently replaced.
        if (isNativeSteam) {
            if (!coverPath.empty() || clearCover) {
                writeNativeSteamArtwork(nativeTarget, "cover", coverPath, clearCover);
            }
            if (!longPath.empty() || clearLong) {
                writeNativeSteamArtwork(nativeTarget, "long", longPath, clearLong);
            }
            if (!wallpaperPath.empty() || clearWallpaper) {
                writeNativeSteamArtwork(nativeTarget, "wallpaper", wallpaperPath, clearWallpaper);
            }
        }
        if (artworkProtection.empty()) item.erase("artworkProtection");
        if (item.empty()) overrides["items"].erase(key);
        writeJsonWithLocalBackup(overridesPath, overrides);

        configTransaction.release();
        return workspaceSnapshot(false, json::object(), nullptr, isNativeSteam);
    }
    if (command == "wakeNetwork") {
        const auto result = runWorkerBounded({L"--wake-network-retries", L"--reason", L"network-restored"}, kWeakNetworkWorkerTimeoutMs);
        if (result.exitCode != 0 && result.output.find("NETWORK_RETRIES_SELECTED: 0") == std::string::npos) {
            requireSuccess(result);
        }
        return workspaceSnapshot(true);
    }
    if (command == "openFolder") {
        const auto path = toWide(jsonStringOr(arguments, "path"));
        if (path.empty()) throw std::runtime_error("Folder path is empty");
        ShellExecuteW(g_window, L"open", L"explorer.exe", quoteArgument(path).c_str(), nullptr, SW_SHOWNORMAL);
        return json{{"opened", true}};
    }
    if (command == "openSteamWebsite") {
        static constexpr wchar_t kSteamAboutUrl[] = L"https://store.steampowered.com/about/";
        const auto result = ShellExecuteW(g_window, L"open", kSteamAboutUrl, nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) throw std::runtime_error("无法打开 Steam 官方页面");
        return json{{"opened", true}, {"url", "https://store.steampowered.com/about/"}};
    }
    if (command == "activateWorkspace") {
        // Controller polling is intentionally limited to the foreground
        // workspace window. Restore/activate it when a WebView modal is
        // opened by mouse input so the next XInput action reaches that modal.
        focusWorkspaceWindow();
        return json{{"activated", true}};
    }
    if (command == "keyboard") {
        wchar_t commonFiles[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"CommonProgramFiles", commonFiles, MAX_PATH)) {
            const fs::path tabTip = fs::path(commonFiles) / L"microsoft shared" / L"ink" / L"TabTip.exe";
            // An explicit controller A is the only path that requests the
            // touch keyboard. Do not activate TabTip over the workspace:
            // stealing the foreground window here makes the next D-pad/A
            // event appear to be lost until the user clicks back.
            if (fs::is_regular_file(tabTip)) ShellExecuteW(g_window, L"open", tabTip.c_str(), nullptr, nullptr, SW_SHOWNOACTIVATE);
        }
        return json{{"requested", true}};
    }
    throw std::runtime_error("Unknown workspace command");
}

struct AsyncResult {
    json message;
    bool refreshWatchers = false;
};

static bool enqueueWorkspaceResult(std::unique_ptr<AsyncResult> result) noexcept {
    if (!result) return false;
    std::lock_guard<std::mutex> lock(g_resultPostMutex);
    const auto window = g_window.load();
    if (g_shuttingDown.load() || !window ||
        !PostMessageW(window, WM_WORKSPACE_RESULT, 0, reinterpret_cast<LPARAM>(result.get()))) return false;
    result.release();
    return true;
}

static void stopWorkspaceResultPosting(HWND window) noexcept {
    std::lock_guard<std::mutex> lock(g_resultPostMutex);
    g_shuttingDown = true;
    MSG message{};
    while (PeekMessageW(&message, window, WM_WORKSPACE_RESULT, WM_WORKSPACE_RESULT, PM_REMOVE))
        delete reinterpret_cast<AsyncResult*>(message.lParam);
}

struct HostBackgroundThread {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> finished;
};
static std::mutex g_hostThreadsMutex;
static std::vector<HostBackgroundThread> g_hostThreads;

static void launchHostBackgroundTask(std::function<void()> action) {
    std::lock_guard<std::mutex> lock(g_hostThreadsMutex);
    if (g_shuttingDown.load()) throw std::runtime_error("宿主正在关闭，未启动新后台任务");
    for (auto it = g_hostThreads.begin(); it != g_hostThreads.end();) {
        if (it->finished->load()) {
            if (it->thread.joinable()) it->thread.join();
            it = g_hostThreads.erase(it);
        } else ++it;
    }
    auto finished = std::make_shared<std::atomic<bool>>(false);
    // Allocate the bookkeeping entry before starting the thread: vector
    // allocation failure must not destroy a joinable temporary thread.
    g_hostThreads.push_back({std::thread{}, finished});
    try {
        g_hostThreads.back().thread = std::thread([action = std::move(action), finished] {
            try { action(); }
            catch (const std::exception& error) {
                try { appendUiError({{"kind", "background-task-unhandled"}, {"error", error.what()}}); } catch (...) {}
            }
            catch (...) { try { appendUiError({{"kind", "background-task-unhandled"}}); } catch (...) {} }
            finished->store(true);
        });
    } catch (...) { g_hostThreads.pop_back(); throw; }
}

static void joinHostBackgroundTasks() {
    std::vector<HostBackgroundThread> tasks;
    { std::lock_guard<std::mutex> lock(g_hostThreadsMutex); tasks.swap(g_hostThreads); }
    for (auto& task : tasks) if (task.thread.joinable()) task.thread.join();
}

static json scrapeStateSnapshot() {
    std::lock_guard<std::mutex> lock(g_scrapeStateMutex);
    if (!g_scrapeState.is_object()) g_scrapeState = json::object();
    return g_scrapeState;
}

static int releaseYearFromValueHost(const json& value) {
    auto yearFromText = [](const std::string& raw) {
        for (size_t index = 0; index + 4 <= raw.size(); ++index) {
            if (!std::isdigit(static_cast<unsigned char>(raw[index])) ||
                !std::isdigit(static_cast<unsigned char>(raw[index + 1])) ||
                !std::isdigit(static_cast<unsigned char>(raw[index + 2])) ||
                !std::isdigit(static_cast<unsigned char>(raw[index + 3]))) continue;
            const int year = (raw[index] - '0') * 1000 + (raw[index + 1] - '0') * 100 +
                (raw[index + 2] - '0') * 10 + (raw[index + 3] - '0');
            if (year >= 1900 && year <= 2100) return year;
        }
        return 0;
    };
    if (value.is_number_integer() || value.is_number_unsigned()) {
        try {
            const auto number = value.get<int64_t>();
            if (number >= 1900 && number <= 2100) return static_cast<int>(number);
            if (number >= 946684800 && number <= 4102444800) {
                const std::time_t timestamp = static_cast<std::time_t>(number);
                const auto* utc = std::gmtime(&timestamp);
                if (utc && utc->tm_year >= 0) return utc->tm_year + 1900;
            }
            return 0;
        } catch (...) { return 0; }
    }
    if (value.is_string()) return yearFromText(value.get<std::string>());
    if (value.is_array()) {
        for (const auto& item : value) {
            const int year = releaseYearFromValueHost(item);
            if (year > 0) return year;
        }
        return 0;
    }
    if (!value.is_object()) return 0;
    for (const char* key : {"year", "releaseYear", "date", "display", "release_date", "releaseDate"}) {
        if (!value.contains(key)) continue;
        const int year = releaseYearFromValueHost(value[key]);
        if (year > 0) return year;
    }
    for (const auto& [_, item] : value.items()) {
        const int year = releaseYearFromValueHost(item);
        if (year > 0) return year;
    }
    return 0;
}

static std::string releaseDisplayFromValueHost(const json& value) {
    if (value.is_string()) return trimHost(value.get<std::string>());
    if (!value.is_object()) return {};
    for (const char* key : {"display", "date"}) {
        if (value.contains(key) && value[key].is_string()) return trimHost(value[key].get<std::string>());
    }
    for (const char* key : {"release_date", "releaseDate"}) {
        if (value.contains(key)) {
            const auto display = releaseDisplayFromValueHost(value[key]);
            if (!display.empty()) return display;
        }
    }
    return {};
}

static void mergeIdentityCandidateMetadata(json& target, const json& source) {
    // The same AppID is present in several audit layers: preliminary ranked
    // search entries and the later appdetails-verified entry. Keep the better
    // score, but never let the earlier entry erase a year found by the later
    // one.
    const int targetYear = releaseYearFromValueHost(target.value("year", json(nullptr)));
    if (targetYear <= 0) {
        int sourceYear = releaseYearFromValueHost(source.value("year", json(nullptr)));
        if (sourceYear <= 0) sourceYear = releaseYearFromValueHost(source.value("releaseYear", json(nullptr)));
        if (sourceYear <= 0) sourceYear = releaseYearFromValueHost(source.value("releaseDate", json(nullptr)));
        if (sourceYear > 0) {
            target["year"] = sourceYear;
            target["releaseYear"] = sourceYear;
        }
    }
    if (releaseDisplayFromValueHost(target.value("releaseDate", json(nullptr))).empty()) {
        const auto sourceDisplay = releaseDisplayFromValueHost(source.value("releaseDate", json(nullptr)));
        if (!sourceDisplay.empty()) target["releaseDate"] = sourceDisplay;
    }
}

static void collectIdentitySearchCandidates(const json& value, std::vector<json>& output, const std::string& providerHint) {
    if (value.is_array()) { for (const auto& item : value) collectIdentitySearchCandidates(item, output, providerHint); return; }
    if (!value.is_object()) return;
    const auto provider = jsonStringOr(value, "provider", providerHint);
    std::string candidateName = jsonStringOr(value, "name");
    if (candidateName.empty()) candidateName = jsonStringOr(value, "formalName");
    if (candidateName.empty()) candidateName = jsonStringOr(value, "displayName");
    if (!candidateName.empty()) {
        int releaseYear = 0;
        for (const char* key : {"year", "releaseYear", "releaseDate", "release_date"}) {
            if (!value.contains(key)) continue;
            releaseYear = releaseYearFromValueHost(value[key]);
            if (releaseYear > 0) break;
        }
        std::string releaseDisplay;
        for (const char* key : {"releaseDate", "release_date"}) {
            if (!value.contains(key)) continue;
            releaseDisplay = releaseDisplayFromValueHost(value[key]);
            if (!releaseDisplay.empty()) break;
        }
        if (value.contains("metadata") && value["metadata"].is_object()) {
            const auto& metadata = value["metadata"];
            for (const char* key : {"releaseYear", "year", "releaseDate", "release_date"}) {
                if (!metadata.contains(key)) continue;
                if (releaseYear <= 0) releaseYear = releaseYearFromValueHost(metadata[key]);
                if (releaseDisplay.empty()) releaseDisplay = releaseDisplayFromValueHost(metadata[key]);
                if (releaseYear > 0 && !releaseDisplay.empty()) break;
            }
        }
        json candidate = {
            {"name", trimHost(candidateName)},
            {"provider", provider.empty() ? "steam" : provider},
            {"score", (value.contains("score") && value["score"].is_number()) ? value["score"].get<double>() : 0.0},
            {"year", releaseYear > 0 ? json(releaseYear) : json(0)},
            {"preview", value.value("preview", json(nullptr))}
        };
        if (!releaseDisplay.empty()) candidate["releaseDate"] = releaseDisplay;
        const auto query = jsonStringOr(value, "query");
        if (!query.empty()) candidate["query"] = trimHost(query);
        if (value.contains("appId") && value["appId"].is_number_integer()) candidate["steamAppId"] = value["appId"];
        else if (value.contains("steamAppId") && value["steamAppId"].is_number_integer()) candidate["steamAppId"] = value["steamAppId"];
        else if (value.contains("canonicalAppId") && value["canonicalAppId"].is_number_integer()) candidate["steamAppId"] = value["canonicalAppId"];
        else if (value.contains("storefrontAppId") && value["storefrontAppId"].is_number_integer()) candidate["steamAppId"] = value["storefrontAppId"];
        if (value.contains("igdbId") && (value["igdbId"].is_number_integer() || value["igdbId"].is_number_unsigned())) candidate["igdbId"] = value["igdbId"];
        if (candidate.contains("igdbId") && !candidate.contains("steamAppId")) {
            // IGDB is displayed as secondary evidence in the editor only. It
            // is never a Steam identity assertion or an import-ready ID.
            candidate["secondaryOnly"] = true;
        }
        output.push_back(std::move(candidate));
    }
    for (const auto& [key, item] : value.items()) collectIdentitySearchCandidates(item, output, key == "steam" ? "steam" : (key == "playniteIgdb" ? "playnite-igdb" : provider));
}
static json identitySearchCandidatesFromDirectory(const fs::path& outputDirectory) {
    std::vector<json> candidates;
    // Always merge both sources: a failed network round can contain the
    // provider candidates that completed before the worker reported failure.
    const auto failed = outputDirectory / L"rounds-failed.json";
    if (fs::is_regular_file(failed)) collectIdentitySearchCandidates(readJson(failed, json::object()), candidates);
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(outputDirectory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || entry.path().filename() != L"rounds.json") continue;
        collectIdentitySearchCandidates(readJson(entry.path(), json::object()), candidates);
    }
    std::map<std::string, json> unique;
    for (auto& candidate : candidates) {
        const auto key = identitySearchKey(jsonStringOr(candidate, "name")) + "|" + jsonStringOr(candidate, "provider");
        if (key.empty()) continue;
        const auto it = unique.find(key);
        if (it == unique.end()) {
            unique[key] = std::move(candidate);
        } else if (candidate.value("score", 0.0) > it->second.value("score", 0.0)) {
            auto replacement = std::move(candidate);
            mergeIdentityCandidateMetadata(replacement, it->second);
            unique[key] = std::move(replacement);
        } else {
            mergeIdentityCandidateMetadata(it->second, candidate);
        }
    }
    candidates.clear();
    for (auto& [_, candidate] : unique) candidates.push_back(std::move(candidate));
    std::sort(candidates.begin(), candidates.end(), [](const json& left, const json& right) {
        const bool leftSteam = jsonStringOr(left, "provider") == "steam" || left.contains("steamAppId");
        const bool rightSteam = jsonStringOr(right, "provider") == "steam" || right.contains("steamAppId");
        if (leftSteam != rightSteam) return leftSteam > rightSteam;
        return left.value("score", 0.0) > right.value("score", 0.0);
    });
    if (candidates.size() > 20) candidates.resize(20);
    return candidates;
}

static json identitySearchCachedCandidates(const fs::path& executable, const std::string& query) {
    std::vector<json> candidates;
    const std::vector<fs::path> roots = {
        g_dataRoot / L"cache" / L"identity",
        workspaceJobsRoot() / L"workspace-identification",
        workspaceJobsRoot() / L"workspace-identity-search",
        workspaceJobsRoot() / L"library-batch-identification",
        // Keep reading results produced by the pre-separation build.
        g_labRoot / L"output" / L"workspace-identification",
        g_labRoot / L"output" / L"workspace-identity-search",
        g_labRoot / L"output" / L"library-batch-identification"
    };
    std::error_code ec;
    for (const auto& root : roots) {
        if (!fs::is_directory(root, ec)) { ec.clear(); continue; }
        for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
            if (ec) { ec.clear(); break; }
            if (!entry.is_regular_file(ec) || entry.path().filename() != L"manifest.json") { ec.clear(); continue; }
            const auto manifest = readJson(entry.path(), json::object());
            const auto manifestExe = jsonStringOr(manifest, "exe", jsonStringOr(manifest, "executable"));
            if (manifestExe.empty() || !samePath(fs::path(toWide(manifestExe)), executable)) continue;
            collectIdentitySearchCandidates(manifest, candidates);
            const auto rounds = entry.path().parent_path() / L"rounds.json";
            if (fs::is_regular_file(rounds)) collectIdentitySearchCandidates(readJson(rounds, json::object()), candidates);
        }
    }
    const auto queryKey = identitySearchKey(query);
    if (!queryKey.empty()) {
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const json& candidate) {
            const auto candidateQueryKey = identitySearchKey(jsonStringOr(candidate, "query"));
            const auto candidateKey = identitySearchKey(jsonStringOr(candidate, "name"));
            if (!candidateQueryKey.empty() && candidateQueryKey != queryKey) {
                // A result with explicit source query belongs to another
                // search round.  Do not show it as if it were a cache hit for
                // the current Chinese/English query.
                return true;
            }
            if (candidateKey.empty()) return true;
            return candidateKey.find(queryKey) == std::string::npos && queryKey.find(candidateKey) == std::string::npos;
        }), candidates.end());
    }
    std::map<std::string, json> unique;
    for (auto& candidate : candidates) {
        const auto key = identitySearchKey(jsonStringOr(candidate, "name")) + "|" + jsonStringOr(candidate, "provider");
        if (key.empty()) continue;
        const auto it = unique.find(key);
        if (it == unique.end()) {
            unique[key] = std::move(candidate);
        } else if (candidate.value("score", 0.0) > it->second.value("score", 0.0)) {
            auto replacement = std::move(candidate);
            mergeIdentityCandidateMetadata(replacement, it->second);
            unique[key] = std::move(replacement);
        } else {
            mergeIdentityCandidateMetadata(it->second, candidate);
        }
    }
    candidates.clear();
    for (auto& [_, candidate] : unique) candidates.push_back(std::move(candidate));
    std::sort(candidates.begin(), candidates.end(), [](const json& left, const json& right) {
        const bool leftSteam = jsonStringOr(left, "provider") == "steam" || left.contains("steamAppId");
        const bool rightSteam = jsonStringOr(right, "provider") == "steam" || right.contains("steamAppId");
        if (leftSteam != rightSteam) return leftSteam > rightSteam;
        return left.value("score", 0.0) > right.value("score", 0.0);
    });
    if (candidates.size() > 20) candidates.resize(20);
    return candidates;
}

static std::string safeSteamAccountToken(std::string value) {
    for (auto& ch : value) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') ch = '_';
    }
    return value.empty() ? "account" : value;
}

static void requireSuccess(const ProcessResult& result);

static json commitToAllSteamAccounts(const json& requestedTarget) {
    const auto accounts = jsonArrayOr(readSteamAccounts(), "accounts");
    if (accounts.empty()) throw std::runtime_error("未发现可写入的 Steam 大屏用户");
    const auto selected = requestedTarget.value("selectedGameDirectories", json::array());
    json results = json::array();
    std::vector<std::pair<std::vector<std::wstring>, fs::path>> prepared;
    for (const auto& account : accounts) {
        const auto accountId = jsonStringOr(account, "accountId");
        const auto steamRoot = jsonStringOr(account, "steamRoot");
        if (accountId.empty()) continue;
        const auto token = safeSteamAccountToken(accountId);
        const auto planPath = g_dataRoot / L"state" / toWide("steam-add-plan-" + token + ".json");
        std::vector<std::wstring> common = {L"--account", toWide(accountId), L"--output", planPath.wstring()};
        if (!steamRoot.empty()) { common.insert(common.begin() + 2, {L"--steam-root", toWide(steamRoot)}); }
        const auto selectionMode = selected.empty() ? "all-ready" : "explicit-game-directories";
        common.insert(common.end() - 2, {L"--selection-mode", toWide(selectionMode)});
        for (const auto& directory : selected) {
            if (directory.is_string()) { common.insert(common.end() - 2, {L"--select-game", toWide(directory.get<std::string>())}); }
        }
        std::vector<std::wstring> planArgs = {L"--plan-add-library-to-steam"};
        planArgs.insert(planArgs.end(), common.begin(), common.end());
        requireSuccess(runWorker(planArgs));
        prepared.push_back({common, planPath});
    }
    if (prepared.empty()) throw std::runtime_error("Steam 账户列表没有可用的账户目标");
    for (const auto& [common, planPath] : prepared) {
        std::vector<std::wstring> commitArgs = {L"--commit-add-library-to-steam", L"--confirm-steam-closed"};
        commitArgs.insert(commitArgs.end(), common.begin(), common.end());
        requireSuccess(runWorker(commitArgs));
        results.push_back(readJson(planPath, json::object()));
    }
    json summary = results.back();
    summary["mode"] = "steam-library-add-commit-all-accounts";
    summary["allSteamAccounts"] = true;
    summary["accountResults"] = results;
    writeJsonWithLocalBackup(g_dataRoot / L"state" / L"last-steam-add-all-accounts.json", summary);
    return summary;
}

static json refreshArtworkForAllSteamAccounts(const json& requestedTarget) {
    const auto accounts = jsonArrayOr(readSteamAccounts(), "accounts");
    const auto selected = requestedTarget.value("selectedGameDirectories", json::array());
    if (accounts.empty()) throw std::runtime_error("未发现可更新的 Steam 大屏用户");
    if (selected.empty()) throw std::runtime_error("请先在“已加入 Steam”页面选择要更新图标的游戏");
    const auto library = readJson(g_dataRoot / L"state" / L"library-scan.json", json::object());
    const auto libraryGames = jsonArrayOr(library, "games");
    std::vector<std::string> eligibleDirectories;
    for (const auto& directory : selected) {
        if (!directory.is_string() || directory.get<std::string>().empty()) continue;
        const auto wanted = fs::path(toWide(directory.get<std::string>()));
        const bool native = std::any_of(libraryGames.begin(), libraryGames.end(), [&](const auto& game) {
            return isNativeSteamRecord(game) &&
                samePath(fs::path(toWide(jsonStringOr(game, "gameDirectory"))), wanted);
        });
        // Native Steam entries are read-only for all automatic and batch
        // refresh paths.  Ignore them even if an old UI sends their directory.
        if (!native) eligibleDirectories.push_back(directory.get<std::string>());
    }
    if (eligibleDirectories.empty()) {
        throw std::runtime_error("原生 Steam 游戏不能执行批量 Steam 图标刷新");
    }
    json results = json::array();
    for (const auto& account : accounts) {
        const auto accountId = jsonStringOr(account, "accountId");
        const auto steamRoot = jsonStringOr(account, "steamRoot");
        if (accountId.empty()) continue;
        const auto token = safeSteamAccountToken(accountId);
        const auto output = g_dataRoot / L"state" / toWide("steam-artwork-refresh-" + token + ".json");
        std::vector<std::wstring> args = {L"--commit-refresh-steam-artwork", L"--account", toWide(accountId), L"--confirm-steam-closed"};
        if (!steamRoot.empty()) { args.push_back(L"--steam-root"); args.push_back(toWide(steamRoot)); }
        for (const auto& directory : eligibleDirectories) {
            args.push_back(L"--select-game"); args.push_back(toWide(directory));
        }
        args.push_back(L"--output"); args.push_back(output.wstring());
        requireSuccess(runWorker(args));
        results.push_back(readJson(output, json::object()));
    }
    if (results.empty()) throw std::runtime_error("Steam 账户列表没有可用的账户目标");
    json summary = results.back();
    summary["mode"] = "steam-artwork-refresh-all-accounts";
    summary["allSteamAccounts"] = true;
    summary["accountResults"] = results;
    writeJsonWithLocalBackup(g_dataRoot / L"state" / L"last-steam-artwork-refresh-all-accounts.json", summary);
    return summary;
}

static json deleteFromAllSteamAccounts(const json& requestedTarget) {
    const auto accounts = jsonArrayOr(readSteamAccounts(), "accounts");
    if (accounts.empty()) throw std::runtime_error("未发现可删除的 Steam 大屏用户");
    const auto selected = requestedTarget.value("selectedGameDirectories", json::array());
    if (selected.empty()) throw std::runtime_error("删除 Steam 快捷方式必须明确选择游戏");
    json results = json::array();
    std::vector<std::pair<std::vector<std::wstring>, fs::path>> prepared;
    for (const auto& account : accounts) {
        const auto accountId = jsonStringOr(account, "accountId");
        const auto steamRoot = jsonStringOr(account, "steamRoot");
        if (accountId.empty()) continue;
        const auto token = safeSteamAccountToken(accountId);
        const auto planPath = g_dataRoot / L"state" / toWide("steam-delete-plan-" + token + ".json");
        std::vector<std::wstring> common = {L"--account", toWide(accountId)};
        if (!steamRoot.empty()) { common.push_back(L"--steam-root"); common.push_back(toWide(steamRoot)); }
        for (const auto& directory : selected) {
            if (directory.is_string()) { common.push_back(L"--select-game"); common.push_back(toWide(directory.get<std::string>())); }
        }
        common.push_back(L"--output"); common.push_back(planPath.wstring());
        std::vector<std::wstring> planArgs = {L"--plan-delete-library-from-steam"};
        planArgs.insert(planArgs.end(), common.begin(), common.end());
        requireSuccess(runWorker(planArgs));
        prepared.push_back({common, planPath});
    }
    if (prepared.empty()) throw std::runtime_error("Steam 账户列表没有可用的账户目标");
    for (const auto& [common, planPath] : prepared) {
        std::vector<std::wstring> commitArgs = {L"--commit-delete-library-from-steam", L"--confirm-steam-closed"};
        commitArgs.insert(commitArgs.end(), common.begin(), common.end());
        requireSuccess(runWorker(commitArgs));
        results.push_back(readJson(planPath, json::object()));
    }
    json summary = results.back();
    summary["mode"] = "steam-library-delete-commit-all-accounts";
    summary["allSteamAccounts"] = true;
    summary["accountResults"] = results;
    writeJsonWithLocalBackup(g_dataRoot / L"state" / L"last-steam-delete-all-accounts.json", summary);
    return summary;
}

static void updateScrapeState(const std::string& key, const json& value) {
    std::lock_guard<std::mutex> lock(g_scrapeStateMutex);
    if (!g_scrapeState.is_object()) g_scrapeState = json::object();
    g_scrapeState["items"][key] = value;
}

static void setScrapeQueueState(
    bool active, int current, int total, int limit, int concurrency = 1,
    int identityConcurrency = 1, int artworkConcurrency = 1) {
    std::lock_guard<std::mutex> lock(g_scrapeStateMutex);
    g_scrapeState["active"] = active;
    g_scrapeState["current"] = current;
    g_scrapeState["total"] = total;
    g_scrapeState["limit"] = limit;
    g_scrapeState["concurrency"] = concurrency;
    g_scrapeState["identityConcurrency"] = identityConcurrency;
    g_scrapeState["artworkConcurrency"] = artworkConcurrency;
    if (!g_scrapeState.contains("items") || !g_scrapeState["items"].is_object()) {
        g_scrapeState["items"] = json::object();
    }
}

static void postPipelineEvent(const std::string& event, json payload) noexcept {
    try {
        if (g_shuttingDown.load()) return;
        payload["kind"] = "event";
        payload["event"] = event;
        payload["scrape"] = scrapeStateSnapshot();
        auto result = std::make_unique<AsyncResult>();
        result->message = std::move(payload);
        enqueueWorkspaceResult(std::move(result));
    } catch (...) {
        // Progress is advisory. A WebView notification must not terminate a
        // recognition lane or the native host under memory/network pressure.
    }
}

static int scrapeQueueLimit() {
    // The queue itself is intentionally not capped at the old 50-item batch
    // size. Identity work is now scheduled in 50 lanes, so a larger library
    // must not silently leave the tail of the ready set unprocessed. Keep a
    // high hard ceiling only as a safety valve for corrupted/huge snapshots.
    constexpr int kDefaultScrapeQueueLimit = 10000;
    constexpr int kMaxScrapeQueueLimit = 10000;
    const auto config = readJson(g_dataRoot / L"config" / L"library-config.json", json::object());
    const auto it = config.find("scrapeQueueLimit");
    if (it != config.end() && it->is_number_integer()) {
        return (std::max)(1, (std::min)(kMaxScrapeQueueLimit, it->get<int>()));
    }
    return kDefaultScrapeQueueLimit;
}

// Keep each game's identity -> artwork chain serial, but let independent
// games resolve SteamIDs aggressively. Artwork is separately guarded below so
// a burst of fast identity matches cannot flood Steam CDN requests.
static int scrapeIdentityConcurrency() {
    return 50;
}

static int scrapeArtworkConcurrency() {
    return 4;
}

static bool startScrapeQueue(bool forceManual) {
    bool expected = false;
    if (!g_scrapeQueueRunning.compare_exchange_strong(expected, true)) {
        // A manual refresh is an explicit retry request.  Do not leave it
        // behind an automatic weak-network batch whose item timeout can be
        // several minutes. Cancel the current batch at its next safe
        // boundary, retain all completed manifests, and let its single queue
        // owner start exactly one forced manual pass after cleanup. This
        // preserves the no-parallel-queue invariant without making the UI
        // wait for the automatic batch's long network timeout.
        if (forceManual) {
            g_scrapeQueueForceRequested = true;
            cancelBackgroundJobs(g_scrapeQueueJobs);
        }
        return false;
    }
    BackgroundJob job;
    try {
        job = beginBackgroundJob(
            "scrape-queue", g_scrapeQueueJobs, g_scrapeQueueGenerations, "library");
        launchHostBackgroundTask([forceManual, job] {
            fs::path failureHistoryPath;
            try {
            // The queue is weak-network work.  It must not hold the task
            // boundary while each EXE spends up to its full network timeout in
            // the worker. Individual JSON mutations use only short locks.
            int limit = scrapeQueueLimit();
            const int identityConcurrency = scrapeIdentityConcurrency();
            const int artworkConcurrency = scrapeArtworkConcurrency();
            std::atomic<int> completed{0};
            size_t total = 0;
            std::string batchId;
            std::atomic<bool> cancelled{false};
            std::atomic<bool> failed{false};
            const int64_t batchStartedAt = unixTimeMsHost();
            size_t round1Total = 0;
            size_t round2Total = 0;
            std::string batchError;
            fs::path historyPath;
            std::mutex historyMutex;
            json history = {
                {"schemaVersion", 1},
                {"kind", "custom-steam-library-scrape-history"},
                {"status", "running"},
                {"startedAt", batchStartedAt},
                {"hostProcessId", GetCurrentProcessId()},
                {"mode", forceManual ? "manual-refresh" : "automatic-first-pass"},
                {"applicationRoot", toUtf8(g_labRoot.wstring())},
                {"dataRoot", toUtf8(g_dataRoot.wstring())},
                {"worker", toUtf8(g_worker.wstring())},
                {"workerRuns", json::array()}
            };
            std::atomic<bool> firstWorkerStartRecorded{false};
            auto persistHistory = [&]() {
                try {
                    if (historyPath.empty()) return false;
                    json snapshot;
                    {
                        std::lock_guard<std::mutex> lock(historyMutex);
                        snapshot = history;
                    }
                    return writeScrapeHistoryRecord(historyPath, snapshot);
                } catch (...) {
                    return false;
                }
            };
            auto recordWorkerRun = [&](const std::string& executable, const std::string& stage,
                                       int roundNumber, const std::vector<std::wstring>& arguments,
                                       const fs::path& outputDirectory, const ProcessResult& result,
                                       int64_t startedAt, int64_t finishedAt, bool launched) {
                try {
                    json run = {
                        {"executable", executable},
                        {"stage", stage},
                        {"round", roundNumber},
                        {"startedAt", startedAt},
                        {"finishedAt", finishedAt},
                        {"elapsedMs", (std::max)(int64_t{0}, finishedAt - startedAt)},
                        {"launched", launched},
                        {"exitCode", static_cast<uint32_t>(result.exitCode)},
                        {"outputDirectory", toUtf8(outputDirectory.wstring())},
                        {"arguments", json::array()},
                        {"workerOutput", retainedDiagnosticOutput(
                            result.output, result.exitCode == 0 ? 2048 : 16384)},
                        {"artifact", scrapeArtifactSummary(outputDirectory, executable)}
                    };
                    for (const auto& argument : arguments) run["arguments"].push_back(toUtf8(argument));
                    std::lock_guard<std::mutex> lock(historyMutex);
                    history["workerRuns"].push_back(std::move(run));
                } catch (...) {
                    // Per-worker diagnostics are optional. A logging failure
                    // must never discard a completed recognition result.
                }
            };
            auto recordFirstWorkerStart = [&](const std::string& executable, const std::string& stage,
                                              int roundNumber, const fs::path& outputDirectory) {
                try {
                    std::lock_guard<std::mutex> lock(historyMutex);
                    history["firstWorker"] = {
                        {"startedAt", unixTimeMsHost()},
                        {"executable", executable},
                        {"stage", stage},
                        {"round", roundNumber},
                        {"outputDirectory", toUtf8(outputDirectory.wstring())}
                    };
                } catch (...) {
                    // The batch itself remains independent of diagnostics.
                }
            };
            try {
                // A previous process can leave a status=running document only
                // when it exits early. Seal it before creating this batch.
                markInterruptedScrapeHistory();
                // The sequence is process-local. Include the process ID so a
                // restart never reuses scrape-1 directories still containing
                // output from the previous host.
                batchId = std::string("scrape-") + std::to_string(GetCurrentProcessId()) + "-" +
                    std::to_string(g_scrapeBatchSequence.fetch_add(1) + 1);
                historyPath = scrapeHistoryRoot() /
                    (L"round-" + std::to_wstring(batchStartedAt) + L"-" + toWide(batchId) + L".json");
                failureHistoryPath = historyPath;
                {
                    std::lock_guard<std::mutex> lock(historyMutex);
                    history["batchId"] = batchId;
                    history["historyPath"] = toUtf8(historyPath.wstring());
                }
                persistHistory();
                pruneScrapeHistory();
                // Start from the last local snapshot so queue assembly and the
                // first Worker launch are never gated by Steam/account/network
                // plan regeneration.  The identity stage refreshes the plan
                // after it has produced metadata, and the batch refreshes it
                // once more after artwork completes.
                auto current = workspaceSnapshot(false, json::object(), job);
                // A local fallback plan cannot tell whether an EXE is already
                // present in Steam's shortcuts.vdf. Before assembling the
                // automatic queue, obtain one authoritative plan when the
                // cached plan is unavailable. This prevents an already-added
                // shortcut from being mistaken for a new no-SteamID game.
                if (!job->cancelRequested.load() &&
                    jsonStringOr(current, "steamPlanSource") == "local-scrape-fallback") {
                    current = workspaceSnapshot(true, json::object(), job);
                }
                if (job->cancelRequested.load()) {
                    cancelled = true;
                } else {
                    struct ScrapeQueueTarget {
                        std::string executable;
                        bool hasVerifiedSteamId = false;
                        int steamAppId = 0;
                        uint64_t igdbId = 0;
                        bool artworkPending = false;
                        bool artworkComplete = false;
                        bool metadataGameSignal = false;
                        bool artworkFallbackEligible = false;
                        bool artworkFallbackPending = false;
                        int priority = 0;
                    };
                    std::vector<ScrapeQueueTarget> queue;
                    // Avoid chained value<json> temporaries here. A partially
                    // written snapshot/plan is valid weak-network state and
                    // must degrade to an empty queue.
                    const auto libraryNode = current.contains("library") && current["library"].is_object()
                        ? current["library"] : json::object();
                    const auto planNode = current.contains("steamPlan") && current["steamPlan"].is_object()
                        ? current["steamPlan"] : json::object();
                    const auto games = libraryNode.contains("games") && libraryNode["games"].is_array()
                        ? libraryNode["games"] : json::array();
                    const auto planItems = planNode.contains("items") && planNode["items"].is_array()
                        ? planNode["items"] : json::array();
                    const auto manualOverrides = current.contains("manualOverrides") && current["manualOverrides"].is_object()
                        ? current["manualOverrides"] : json::object();
                    for (const auto& game : games) {
                        if (static_cast<int>(queue.size()) >= limit || !game.is_object()) continue;
                        if (jsonStringOr(game, "status") != "ready") continue;
                        if (jsonBoolOr(game, "steamNative", false) ||
                            jsonStringOr(game, "contentType") == "steam-native") continue;
                        const auto executable = jsonStringOr(game, "primaryExecutable");
                        if (executable.empty()) continue;
                        bool pending = false;
                        bool hasVerifiedSteamId = false;
                        int steamAppId = 0;
                        uint64_t igdbId = 0;
                        bool artworkPending = false;
                        bool artworkComplete = false;
                        bool metadataGameSignal = false;
                        bool artworkFallbackEligible = false;
                        bool artworkFallbackPending = false;
                        bool planItemFound = false;
                        bool alreadyOwnedBySteam = false;
                        const auto executableKey = backgroundExecutableKey(executable);
                        for (const auto& item : planItems) {
                            if (!item.is_object()) continue;
                            const auto planExecutable = jsonStringOr(item, "primaryExecutable");
                            if (planExecutable.empty() || backgroundExecutableKey(planExecutable) != executableKey) continue;
                            planItemFound = true;
                            const auto status = jsonStringOr(item, "status");
                            // A shortcut already present in Steam is no longer
                            // a CustomSteamLibrary scrape target. Its SteamID
                            // is the shortcut's own stored identity, and an
                            // incomplete grid must never send the item back
                            // through identity/IGDB fallback.
                            alreadyOwnedBySteam = steamPlanItemAlreadyOwned(item);
                            if (alreadyOwnedBySteam) break;
                            const bool identityPending = status == "needs-identity-and-artwork" ||
                                status == "needs-identity-confirmation" || status == "needs-steam-verification" ||
                                status == "waiting-steam-verification";
                            metadataGameSignal = jsonBoolOr(item, "metadataGameSignal", false);
                            steamAppId = static_cast<int>(jsonIntegerOr(item, "steamStoreAppId",
                                jsonIntegerOr(item, "storefrontAppId", 0)));
                            hasVerifiedSteamId = steamAppId > 0 &&
                                jsonStringOr(item, "steamVerificationStatus") == "steam-verified";
                            igdbId = static_cast<uint64_t>((std::max)(int64_t{0},
                                jsonIntegerOr(item, "igdbId", 0)));
                            const auto artworkIt = item.find("artwork");
                            artworkComplete = artworkIt != item.end() && artworkIt->is_object() &&
                                jsonBoolOr(*artworkIt, "minimumComplete", false);
                            artworkComplete = artworkComplete || jsonBoolOr(item, "artworkFallbackComplete", false);
                            const bool longArtworkComplete = artworkIt != item.end() && artworkIt->is_object() &&
                                jsonBoolOr(*artworkIt, "long", false);
                            artworkPending = hasVerifiedSteamId &&
                                (status == "needs-minimum-artwork" || !artworkComplete || !longArtworkComplete);
                            // Every no-SteamID item with a missing image is a
                            // valid second-round fallback candidate. IGDB may
                            // enrich the artwork, but it never becomes a
                            // SteamID through this queue.
                            const bool automaticSlotsComplete = artworkIt != item.end() && artworkIt->is_object() && jsonBoolOr(*artworkIt, "automaticSlotsComplete", false);
                            artworkPending = artworkPending && !automaticSlotsComplete;
                            artworkFallbackEligible = !hasVerifiedSteamId && !automaticSlotsComplete && (!artworkComplete || !longArtworkComplete);
                            artworkFallbackPending = artworkFallbackEligible;
                            pending = identityPending || artworkPending || artworkFallbackPending;
                            break;
                        }
                        if (alreadyOwnedBySteam) continue;
                        // The local scan is authoritative for the existence
                        // of a ready primary EXE.  A missing/partial Steam
                        // plan must not turn manual refresh into a false
                        // instant completion, especially on a newly moved
                        // portable package before Steam accounts are visible.
                        // Force-manual means retry the whole ready set; the
                        // per-game manual-artwork guard above still protects
                        // user-owned artwork.
                        if (forceManual || !planItemFound) {
                            pending = true;
                            if (!planItemFound) {
                                hasVerifiedSteamId = false;
                                steamAppId = 0;
                                artworkComplete = false;
                                artworkPending = false;
                                artworkFallbackEligible = true;
                                artworkFallbackPending = true;
                            }
                        }
                        if (pending) {
                            const int priority = hasVerifiedSteamId && (artworkPending || artworkFallbackPending) ? 0 :
                                (hasVerifiedSteamId || artworkFallbackPending ? 1 : 2);
                            queue.push_back({executable, hasVerifiedSteamId, steamAppId, igdbId,
                                             artworkPending, artworkComplete, metadataGameSignal,
                                             artworkFallbackEligible, artworkFallbackPending, priority});
                        }
                    }
                    std::stable_sort(queue.begin(), queue.end(), [](const auto& left, const auto& right) {
                        return left.priority < right.priority;
                    });
                    total = queue.size();
                    round1Total = queue.size();
                    setScrapeQueueState(
                        true, 0, static_cast<int>(total), limit, artworkConcurrency,
                        identityConcurrency, artworkConcurrency);
                    {
                        std::lock_guard<std::mutex> lock(g_scrapeStateMutex);
                        g_scrapeState["batchId"] = batchId;
                        g_scrapeState["mode"] = forceManual ? "manual-refresh" : "automatic-first-pass";
                        g_scrapeState["round"] = 1;
                        g_scrapeState["roundLabel"] = "第一轮";
                        g_scrapeState["roundCurrent"] = 0;
                        g_scrapeState["roundTotal"] = round1Total;
                        g_scrapeState["items"] = json::object();
                        for (const auto& target : queue) {
                            g_scrapeState["items"][backgroundExecutableKey(target.executable)] = {
                                {"status", "queued"}, {"executable", target.executable},
                                {"priority", target.priority},
                                {"hasVerifiedSteamId", target.hasVerifiedSteamId},
                                {"steamAppId", target.steamAppId > 0 ? json(target.steamAppId) : json(nullptr)},
                                {"igdbId", target.igdbId > 0 ? json(target.igdbId) : json(nullptr)},
                                {"artworkPending", target.artworkPending},
                                {"artworkComplete", target.artworkComplete},
                                {"metadataGameSignal", target.metadataGameSignal},
                                {"artworkFallbackEligible", target.artworkFallbackEligible},
                                {"artworkFallbackPending", target.artworkFallbackPending}
                            };
                        }
                    }
                    postPipelineEvent("scrape-queue-started", {
                        {"batchId", batchId}, {"total", total}, {"limit", limit},
                        {"round", 1}, {"roundLabel", "第一轮"}, {"roundTotal", round1Total},
                        {"concurrency", artworkConcurrency},
                        {"identityConcurrency", identityConcurrency},
                        {"artworkConcurrency", artworkConcurrency}
                    });
                    const auto outputDirectory = workspaceJobsRoot() / L"library-batch-identification";
                    const auto batchOutputDirectory = outputDirectory / toWide(batchId);
                    fs::create_directories(batchOutputDirectory);
            {
                std::lock_guard<std::mutex> lock(historyMutex);
                history["queue"] = {
                    {"total", total}, {"limit", limit},
                    {"identityConcurrency", identityConcurrency},
                    {"artworkConcurrency", artworkConcurrency},
                    {"round1Total", round1Total},
                    {"perGameBudgetMs", kScrapePerGameBudgetMs},
                    {"identityWorkerTimeoutMs", kScrapeIdentityWorkerTimeoutMs},
                    {"artworkWorkerTimeoutMs", kScrapeArtworkWorkerTimeoutMs},
                    {"requeueRetryCount", kScrapeRetryCount},
                    {"requeueRetryDelaysMs", {kScrapeRetryDelayMs[0], kScrapeRetryDelayMs[1], kScrapeRetryDelayMs[2]}},
                    {"jobOutputDirectory", toUtf8(batchOutputDirectory.wstring())}
                };
            }
                    persistHistory();
                    std::mutex stageResultMutex;
                    std::set<std::string> identitySucceeded;
                    std::set<std::string> identityFailed;
                    std::set<std::string> deferredKeys;
                    std::set<std::string> completedKeys;
                    std::set<std::string> round1DoneKeys;
                    std::set<std::string> round2DoneKeys;
                    std::set<std::string> artworkFallbackPendingKeys;
                    std::set<std::string> artworkFallbackFailedKeys;
                    std::vector<ScrapeQueueTarget> artworkFallbackQueue;
                    std::unordered_map<std::string, int> identityRetryAttempts;
                    std::unordered_map<std::string, int> artworkRetryAttempts;
                    std::unordered_map<std::string, int> inlineFallbackRetryAttempts;
                    std::unordered_map<std::string, ScrapeGameTimeBudget> scrapeBudgets;
                    // Keep the verified ID in the current batch as soon as
                    // the identity worker writes it.  The persisted plan is
                    // intentionally refreshed less often than each worker;
                    // artwork must not wait for that refresh to discover the
                    // SteamID that this same batch just produced.
                    std::unordered_map<std::string, int> identityResolvedSteamIds;
                    std::unordered_map<std::string, uint64_t> identityResolvedIgdbIds;
                    std::mutex artworkPlanRefreshMutex;
                    std::mutex artworkFallbackQueueMutex;
                    // Identity resolution may run in 50 independent lanes,
                    // but artwork must remain at four active requests. This
                    // gate is global to the first-round per-game chains; the
                    // second-round artwork stage already uses the same
                    // four-worker stage limit.
                    std::mutex artworkLaneMutex;
                    std::condition_variable artworkLaneCv;
                    int activeArtworkLanes = 0;
                    auto pauseScrapeBudget = [&](const std::string& key) {
                        std::lock_guard<std::mutex> lock(stageResultMutex);
                        const auto found = scrapeBudgets.find(key);
                        if (found != scrapeBudgets.end()) found->second.suspend(std::chrono::steady_clock::now());
                    };
                    auto acquireArtworkLane = [&](const std::string& key) -> bool {
                        pauseScrapeBudget(key);
                        // runStage resumes the budget after the slot is acquired.
                        return acquireScrapeArtworkSlot(artworkLaneMutex, artworkLaneCv,
                            activeArtworkLanes, artworkConcurrency, cancelled, job->cancelRequested);
                    };
                    auto releaseArtworkLane = [&]() {
                        {
                            std::lock_guard<std::mutex> lock(artworkLaneMutex);
                            if (activeArtworkLanes > 0) --activeArtworkLanes;
                        }
                        artworkLaneCv.notify_one();
                    };

                    auto ensureScrapeDeadline = [&](const std::string& key) {
                        std::lock_guard<std::mutex> lock(stageResultMutex);
                        const auto now = std::chrono::steady_clock::now();
                        auto [found, inserted] = scrapeBudgets.try_emplace(key, now);
                        found->second.resume(now);
                    };
                    auto scrapeRemainingMs = [&](const std::string& key) -> DWORD {
                        std::lock_guard<std::mutex> lock(stageResultMutex);
                        const auto found = scrapeBudgets.find(key);
                        return found == scrapeBudgets.end() ? kScrapePerGameBudgetMs
                            : found->second.remaining(std::chrono::steady_clock::now());
                    };

                    struct PendingScrapeTarget {
                        ScrapeQueueTarget target;
                        size_t targetIndex = 0;
                        std::chrono::steady_clock::time_point due{};
                        uint64_t sequence = 0;
                    };
                    struct PendingScrapeTargetCompare {
                        bool operator()(const PendingScrapeTarget& left,
                                        const PendingScrapeTarget& right) const {
                            if (left.due != right.due) return left.due > right.due;
                            return left.sequence > right.sequence;
                        }
                    };
                    std::priority_queue<PendingScrapeTarget,
                        std::vector<PendingScrapeTarget>, PendingScrapeTargetCompare> pendingTargets;
                    std::mutex pendingTargetsMutex;
                    std::condition_variable pendingTargetsCv;
                    size_t activePendingTargets = 0;
                    uint64_t pendingTargetSequence = 0;

                    auto enqueuePendingTarget = [&](ScrapeQueueTarget target, size_t targetIndex,
                                                    std::chrono::steady_clock::time_point due) {
                        {
                            std::lock_guard<std::mutex> lock(pendingTargetsMutex);
                            pendingTargets.push({std::move(target), targetIndex, due, pendingTargetSequence++});
                        }
                        pendingTargetsCv.notify_one();
                    };

                    auto scheduleScrapeRetry = [&](const ScrapeQueueTarget& target, size_t targetIndex,
                                                   const std::string& stage, int retryAttempt) {
                        const auto delayIndex = (std::max)(0, (std::min)(retryAttempt - 1, kScrapeRetryCount - 1));
                        const auto delayMs = kScrapeRetryDelayMs[delayIndex];
                        updateScrapeState(backgroundExecutableKey(target.executable), {
                            {"status", "queued"}, {"stage", stage}, {"executable", target.executable},
                            {"retryAttempt", retryAttempt}, {"retryDelayMs", delayMs}
                        });
                        int roundCurrent = 0;
                        {
                            std::lock_guard<std::mutex> lock(stageResultMutex);
                            roundCurrent = static_cast<int>(round1DoneKeys.size());
                        }
                        postPipelineEvent("scrape-progress", {
                            {"batchId", batchId}, {"executable", target.executable},
                            {"status", "retry-queued"}, {"stage", stage},
                            {"retryAttempt", retryAttempt}, {"retryDelayMs", delayMs},
                            {"current", completed.load()}, {"total", total},
                            {"round", 1}, {"roundLabel", "第一轮"},
                            {"roundCurrent", roundCurrent},
                            {"roundTotal", round1Total}
                        });
                        enqueuePendingTarget(target, targetIndex,
                            std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs));
                    };

                    auto refreshPlanAfterArtwork = [&](const std::string& executable) {
                        std::lock_guard<std::mutex> refreshLock(artworkPlanRefreshMutex);
                        try {
                            // Do not rebuild Steam's complete add plan once per
                            // artwork item.  That path performs account
                            // discovery and a shortcuts.vdf plan worker (up to
                            // 2 minutes each), so 17 completed cards could
                            // serialize into a false "still scraping" state
                            // for tens of minutes.  The local snapshot already
                            // reads the newly written manifest and computes
                            // the waiting/ready status immediately.  The
                            // complete Steam plan is refreshed once after the
                            // stage/batch below.
                            auto refreshedSnapshot = workspaceSnapshot(false, json::object(), job);
                            // Update artwork readiness without replacing authoritative
                            // shortcut ownership with a non-Steam identity assessment.
                            refreshedSnapshot["steamPlan"] = buildLocalSteamPlanSnapshot(
                                refreshedSnapshot, jsonObjectOr(refreshedSnapshot, "steamPlan"));
                            refreshedSnapshot["steamPlanSource"] =
                                "local-scrape-fallback";
                            refreshedSnapshot["steamPlanError"] = nullptr;
                            attachWorkspaceArtwork(refreshedSnapshot["steamPlan"], refreshedSnapshot["steamAccounts"]);
                            if (!job->cancelRequested.load()) {
                                postPipelineEvent("library-plan-refreshed", {
                                    {"snapshot", refreshedSnapshot},
                                    {"source", "artwork-stage"},
                                    {"executable", executable}
                                });
                            }
                        } catch (...) {
                            // The final batch refresh is still available if
                            // this per-item advisory refresh is unavailable.
                        }
                    };

                    auto reportRoundDone = [&](const ScrapeQueueTarget& target, int roundNumber) {
                        const auto key = backgroundExecutableKey(target.executable);
                        int roundCurrent = 0;
                        {
                            std::lock_guard<std::mutex> lock(stageResultMutex);
                            auto& done = roundNumber == 2 ? round2DoneKeys : round1DoneKeys;
                            if (!done.insert(key).second) return;
                            roundCurrent = static_cast<int>(done.size());
                        }
                        const size_t roundTotal = roundNumber == 2 ? round2Total : round1Total;
                        {
                            std::lock_guard<std::mutex> lock(g_scrapeStateMutex);
                            g_scrapeState["round"] = roundNumber;
                            g_scrapeState["roundLabel"] = roundNumber == 2 ? "第二轮" : "第一轮";
                            g_scrapeState["roundCurrent"] = roundCurrent;
                            g_scrapeState["roundTotal"] = roundTotal;
                        }
                        postPipelineEvent("scrape-progress", {
                            {"batchId", batchId}, {"executable", target.executable},
                            {"status", "round-complete"},
                            {"stage", roundNumber == 2 ? "round-2" : "round-1"},
                            {"round", roundNumber},
                            {"roundLabel", roundNumber == 2 ? "第二轮" : "第一轮"},
                            {"roundCurrent", roundCurrent}, {"roundTotal", roundTotal},
                            {"current", completed.load()}, {"total", total}
                        });
                    };

                    auto postCompleted = [&](const ScrapeQueueTarget& target,
                                              const std::string& status,
                                              const std::string& stage,
                                              json extra,
                                              int roundNumber = 1) {
                         const auto key = backgroundExecutableKey(target.executable);
                         {
                             std::lock_guard<std::mutex> lock(stageResultMutex);
                             if (!completedKeys.insert(key).second) return;
                             if (stage == "artwork") artworkFallbackPendingKeys.erase(key);
                         }
                         const int current = completed.fetch_add(1) + 1;
                         reportRoundDone(target, roundNumber);
                         int roundCurrent = 0;
                         {
                             std::lock_guard<std::mutex> lock(stageResultMutex);
                             const auto& done = roundNumber == 2 ? round2DoneKeys : round1DoneKeys;
                             roundCurrent = static_cast<int>(done.size());
                         }
                         json itemState = extra.is_object() ? extra : json::object();
                        itemState["status"] = status;
                        itemState["stage"] = stage;
                        itemState["executable"] = target.executable;
                        updateScrapeState(key, itemState);
                        json payload = {
                            {"batchId", batchId}, {"executable", target.executable},
                            {"status", status}, {"stage", stage},
                            {"current", current}, {"total", total},
                            {"round", roundNumber},
                            {"roundLabel", roundNumber == 2 ? "第二轮" : "第一轮"},
                            {"roundCurrent", roundCurrent},
                            {"roundTotal", roundNumber == 2 ? round2Total : round1Total}
                        };
                        if (extra.is_object()) {
                            for (const auto& [name, value] : extra.items()) payload[name] = value;
                        }
                        {
                            std::lock_guard<std::mutex> lock(g_scrapeStateMutex);
                            const int previous = g_scrapeState.value("current", 0);
                            g_scrapeState["current"] = (std::max)(previous, current);
                        }
                        postPipelineEvent("scrape-progress", std::move(payload));
                    };

                    auto runStage = [&](const std::vector<ScrapeQueueTarget>& stageQueue,
                                        const std::string& stage,
                                        int stageConcurrency,
                                        bool metadataOnly,
                                        const std::string& workerMode,
                                        int roundNumber,
                                        size_t outputIndexOffset = 0) {
                        std::atomic<size_t> nextIndex{0};
                        auto processOne = [&](size_t queueIndex) {
                            const auto& target = stageQueue[queueIndex];
                            const auto& executable = target.executable;
                            const auto key = backgroundExecutableKey(executable);
                            BackgroundJob itemJob;
                            std::optional<GameTaskLease> gameLease;
                            bool gameLeaseHeld = false;
                            auto releaseGameLease = [&]() {
                                if (gameLeaseHeld && gameLease) {
                                    releaseGameTask(*gameLease);
                                    gameLeaseHeld = false;
                                }
                            };
                            auto finishItem = [&]() {
                                releaseGameLease();
                                if (itemJob) {
                                    finishBackgroundJob(g_scrapeQueueJobs, itemJob);
                                    itemJob.reset();
                                }
                            };
                            auto postStageResult = [&](const std::string& status, bool success,
                                                        json extra) {
                                json itemState = extra.is_object() ? extra : json::object();
                                itemState["status"] = status;
                                itemState["stage"] = stage;
                                itemState["executable"] = executable;
                                updateScrapeState(key, itemState);
                                if (metadataOnly) {
                                    {
                                        std::lock_guard<std::mutex> lock(stageResultMutex);
                                        if (success) identitySucceeded.insert(key);
                                        else if (status == "identity-failed") {
                                            identityFailed.insert(key);
                                        }
                                    }
                                }
                                int roundCurrent = 0;
                                {
                                    std::lock_guard<std::mutex> lock(stageResultMutex);
                                    const auto& done = roundNumber == 2 ? round2DoneKeys : round1DoneKeys;
                                    roundCurrent = static_cast<int>(done.size());
                                }
                                const size_t roundTotal = roundNumber == 2 ? round2Total : round1Total;
                                {
                                    std::lock_guard<std::mutex> lock(g_scrapeStateMutex);
                                    g_scrapeState["round"] = roundNumber;
                                    g_scrapeState["roundLabel"] = roundNumber == 2 ? "第二轮" : "第一轮";
                                    g_scrapeState["roundCurrent"] = roundCurrent;
                                    g_scrapeState["roundTotal"] = roundTotal;
                                }
                                json payload = {
                                    {"batchId", batchId}, {"executable", executable},
                                    {"status", status}, {"stage", stage},
                                    {"current", completed.load()}, {"total", total},
                                    {"round", roundNumber},
                                    {"roundLabel", roundNumber == 2 ? "第二轮" : "第一轮"},
                                    {"roundCurrent", roundCurrent},
                                    {"roundTotal", roundTotal}
                                };
                                if (extra.is_object()) {
                                    for (const auto& [name, value] : extra.items()) payload[name] = value;
                                }
                                postPipelineEvent("scrape-progress", std::move(payload));
                            };
                            auto postDeferredManual = [&]() {
                                {
                                    std::lock_guard<std::mutex> lock(stageResultMutex);
                                    deferredKeys.insert(key);
                                }
                                postCompleted(target, "deferred-manual", stage, {
                                    {"reason", "manual-identify-active"}
                                }, roundNumber);
                            };
                            try {
                                gameLease = tryAcquireScrapeGameTask(executable, stage);
                                if (!gameLease) {
                                    postDeferredManual();
                                    return;
                                }
                                gameLeaseHeld = true;
                                itemJob = beginBackgroundJob(
                                    "scrape-item", g_scrapeQueueJobs, g_scrapeQueueGenerations,
                                    "library-item|" + key);
                                if (!gameTaskLeaseIsCurrent(*gameLease)) {
                                    postDeferredManual();
                                    return;
                                }
                                ensureScrapeDeadline(key);
                                if (scrapeRemainingMs(key) < 1000u) {
                                    postCompleted(target, "failed", stage, {
                                        {"reason", "per-game-timeout"},
                                        {"timeoutMs", kScrapePerGameBudgetMs}
                                    }, roundNumber);
                                    finishItem();
                                    return;
                                }
                                updateScrapeState(key, {
                                    {"status", "loading"}, {"stage", stage}, {"executable", executable}
                                });
                                postPipelineEvent("scrape-progress", {
                                        {"batchId", batchId}, {"executable", executable},
                                        {"status", "loading"}, {"stage", stage},
                                        {"current", completed.load()}, {"total", total},
                                        {"round", roundNumber},
                                        {"roundLabel", roundNumber == 2 ? "第二轮" : "第一轮"},
                                        {"roundCurrent", static_cast<int>(queueIndex + 1)},
                                        {"roundTotal", stageQueue.size()}
                                    });

                                if (job->cancelRequested.load() || itemJob->cancelRequested.load()) {
                                    cancelled = true;
                                    updateScrapeState(key, {
                                        {"status", "paused"}, {"stage", stage}, {"executable", executable}
                                    });
                                    finishItem();
                                    return;
                                }
                                // This runs in up to 50 identity lanes. A full
                                // snapshot recursively reads historical jobs,
                                // which made the second cache-warm startup do
                                // the same expensive work once per game.
                                const auto latestOverrides = readJson(
                                    g_dataRoot / L"config" / L"manual-overrides.json",
                                    json{{"items", json::object()}});
                                bool retryingIdentityInCurrentBatch = false;
                                if (metadataOnly) {
                                    std::lock_guard<std::mutex> lock(stageResultMutex);
                                    retryingIdentityInCurrentBatch = identityRetryAttempts.count(key) != 0;
                                }
                                if (metadataOnly && !forceManual && !retryingIdentityInCurrentBatch &&
                                    automaticIdentityAttempted(latestOverrides, executable)) {
                                    postStageResult("identity-failed", false, {
                                        {"reason", "automatic-retry-cooldown"}
                                    });
                                    finishItem();
                                    return;
                                }
                                if (!metadataOnly && manualArtworkProtected(latestOverrides, executable)) {
                                    if (metadataOnly) postStageResult("skipped-manual", false, json::object());
                                    else postCompleted(target, "skipped-manual", stage, json::object(), roundNumber);
                                    finishItem();
                                    return;
                                }
                                if (metadataOnly && !forceManual) markAutomaticIdentityAttempt(executable);
                                const auto itemOutputDirectory = batchOutputDirectory / toWide(stage) /
                                    (L"item-" + std::to_wstring(outputIndexOffset + queueIndex + 1));
                                fs::create_directories(itemOutputDirectory);
                                std::vector<std::wstring> workerArguments = {
                                    toWide(executable), itemOutputDirectory.wstring(), L"--platform", L"pc"
                                };
                                if (metadataOnly) workerArguments.push_back(L"--metadata-only");
                                const bool explicitArtworkWorkerMode = !metadataOnly &&
                                    (workerMode == "steam-artwork-only" || workerMode == "igdb-artwork-only");
                                if (!workerMode.empty() && !explicitArtworkWorkerMode) {
                                    // workerMode is an internal label such as
                                    // "steam-comprehensive-identity-only"; pass it as the
                                    // actual CLI option expected by the worker.
                                    workerArguments.push_back(std::wstring(L"--") + toWide(workerMode));
                                }
                                if (!metadataOnly && workerMode == "steam-artwork-only" && target.steamAppId > 0) {
                                    workerArguments.push_back(L"--steam-artwork-only");
                                    workerArguments.push_back(L"--steam-id");
                                    workerArguments.push_back(toWide(std::to_string(target.steamAppId)));
                                } else if (!metadataOnly && workerMode == "igdb-artwork-only") {
                                    workerArguments.push_back(L"--igdb-artwork-only");
                                    if (target.igdbId > 0) {
                                        workerArguments.push_back(L"--igdb-id");
                                        workerArguments.push_back(toWide(std::to_string(target.igdbId)));
                                    }
                                    if (target.hasVerifiedSteamId && target.steamAppId > 0) {
                                        workerArguments.push_back(L"--artwork-steam-id");
                                        workerArguments.push_back(toWide(std::to_string(target.steamAppId)));
                                    }
                                } else if (!metadataOnly && workerMode.empty() && target.steamAppId > 0) {
                                    workerArguments.push_back(L"--steam-id");
                                    workerArguments.push_back(toWide(std::to_string(target.steamAppId)));
                                }
                                ProcessResult result{};
                                int workerAttempts = 1;
                                const int64_t workerStartedAt = unixTimeMsHost();
                                bool workerLaunched = false;
                                // Provider retries belong to the shared queue:
                                // a failed game is released immediately and
                                // re-enters after 1s, 3s, then 5s. This keeps
                                // the other game lanes productive.
                                if (job->cancelRequested.load() || itemJob->cancelRequested.load() ||
                                    !gameTaskLeaseIsCurrent(*gameLease)) {
                                    result.exitCode = ERROR_CANCELLED;
                                } else {
                                    workerLaunched = true;
                                    if (!firstWorkerStartRecorded.exchange(true)) {
                                        recordFirstWorkerStart(executable, stage, roundNumber, itemOutputDirectory);
                                        persistHistory();
                                    }
                                    const auto remainingBudgetMs = scrapeRemainingMs(key);
                                    if (remainingBudgetMs < 1000u) {
                                        result.exitCode = ERROR_TIMEOUT;
                                        result.output = "\n错误：单个游戏刮削总时限已到";
                                    } else {
                                        const auto workerTimeoutMs = (std::min)(
                                            metadataOnly ? kScrapeIdentityWorkerTimeoutMs : kScrapeArtworkWorkerTimeoutMs,
                                            remainingBudgetMs);
                                    result = runWorkerBounded(
                                        workerArguments,
                                        workerTimeoutMs,
                                        &itemJob->cancelRequested, &itemJob->activeProcess);
                                    }
                                }
                                const int64_t workerFinishedAt = unixTimeMsHost();
                                recordWorkerRun(
                                    executable, stage, roundNumber, workerArguments,
                                    itemOutputDirectory, result, workerStartedAt,
                                    workerFinishedAt, workerLaunched);
                                if (scrapeRemainingMs(key) == 0) {
                                    postCompleted(target, "failed", stage, {
                                        {"reason", "per-game-timeout"},
                                        {"timeoutMs", kScrapePerGameBudgetMs},
                                        {"workerAttempts", workerAttempts}
                                    }, roundNumber);
                                    finishItem();
                                    return;
                                }
                                const bool leaseLost = !gameTaskLeaseIsCurrent(*gameLease);
                                if (leaseLost && !job->cancelRequested.load()) {
                                    // A manual edit took ownership.  This is a
                                    // per-game deferral, not a cancellation of
                                    // the rest of the global refresh queue.
                                    postDeferredManual();
                                    finishItem();
                                    return;
                                }
                                if (job->cancelRequested.load() || itemJob->cancelRequested.load() ||
                                    result.output.find("TASK_CANCELLED") != std::string::npos) {
                                    cancelled = true;
                                    updateScrapeState(key, {
                                        {"status", "paused"}, {"stage", stage}, {"executable", executable}
                                    });
                                    postPipelineEvent("scrape-progress", {
                                        {"batchId", batchId}, {"executable", executable},
                                        {"status", "paused"}, {"stage", stage},
                                        {"current", completed.load()}, {"total", total},
                                        {"round", roundNumber},
                                        {"roundLabel", roundNumber == 2 ? "第二轮" : "第一轮"},
                                        {"roundCurrent", static_cast<int>(queueIndex + 1)},
                                        {"roundTotal", stageQueue.size()}
                                    });
                                    finishItem();
                                    return;
                                }
                                json retryInfo = {
                                    {"attempts", workerAttempts},
                                    {"retryLimit", 1},
                                    {"requeueRetryLimit", kScrapeRetryCount}
                                };
                                const int verifiedSteamAppId = metadataOnly
                                    ? outputVerifiedSteamAppId(itemOutputDirectory, executable) : 0;
                                const uint64_t resolvedIgdbId = metadataOnly
                                    ? outputResolvedIgdbId(itemOutputDirectory, executable) : 0;
                                const bool identityVerified = !metadataOnly ||
                                    (result.exitCode == 0 && verifiedSteamAppId > 0);
                                if (metadataOnly) {
                                        // Persist the manifest for diagnostics and
                                        // for the next round, but only a verified
                                        // Steam AppID counts as first-round success.
                                        persistResolvedIdentityFromOutput(itemOutputDirectory, executable);
                                        if (resolvedIgdbId > 0) {
                                            std::lock_guard<std::mutex> lock(stageResultMutex);
                                            identityResolvedIgdbIds[key] = resolvedIgdbId;
                                        }
                                }
                                if (result.exitCode == 0 && metadataOnly) {
                                    if (verifiedSteamAppId > 0) {
                                        std::lock_guard<std::mutex> lock(stageResultMutex);
                                        identityResolvedSteamIds[key] = verifiedSteamAppId;
                                    }
                                }
                                if (result.exitCode == 0 && identityVerified) {
                                    if (metadataOnly) {
                                        // The worker's manifest is the durable
                                        // identity result even when the
                                        // artwork stage has not started yet.
                                        // Promote it immediately so the next
                                        // plan sees the SteamID instead of
                                        // waiting for a later full refresh.
                                        if (!forceManual) recordAutomaticIdentityResult(executable, true);
                                        postStageResult("identity-ready", true, retryInfo);
                                } else {
                                        postCompleted(target,
                                            workerMode == "igdb-artwork-only" ? "artwork-fallback-ready" : "ready",
                                            stage, retryInfo, roundNumber);
                                    }
                                } else {
                                    std::string detail = result.output;
                                    if (detail.size() > 1200) detail = detail.substr(detail.size() - 1200);
                                    retryInfo["error"] = detail;
                                    if (metadataOnly) {
                                        if (result.exitCode == 0 && !identityVerified) {
                                            retryInfo["error"] = "worker returned metadata without a verified SteamID";
                                        }
                                        const auto identityError = retryInfo.contains("error") && retryInfo["error"].is_string()
                                            ? retryInfo["error"].get<std::string>() : detail;
                                        if (!forceManual) recordAutomaticIdentityResult(executable, false, identityError);
                                        postStageResult("identity-failed", false, retryInfo);
                                    } else {
                                        if (stage == "artwork" && workerMode == "steam-artwork-only") {
                                            // A verified SteamID with empty
                                            // CDN assets is still eligible for
                                            // the second-round IGDB artwork
                                            // fallback. Keep the item out of
                                            // completedKeys so the fallback
                                            // can publish the final status.
                                            {
                                                std::lock_guard<std::mutex> lock(stageResultMutex);
                                                artworkFallbackPendingKeys.insert(key);
                                            }
                                            postStageResult("artwork-failed-fallback-pending", false, retryInfo);
                                        } else if (stage == "artwork-fallback") {
                                            {
                                                std::lock_guard<std::mutex> lock(stageResultMutex);
                                                artworkFallbackFailedKeys.insert(key);
                                            }
                                            postStageResult("artwork-fallback-failed", false, retryInfo);
                                        } else {
                                            postCompleted(target, "failed", stage, retryInfo, roundNumber);
                                        }
                                    }
                                }
                                // Partial artwork is durable too. Show newly obtained
                                // backgrounds/long covers even if the portrait slot
                                // failed; do not hide them until the batch finishes.
                                if (!metadataOnly) refreshPlanAfterArtwork(executable);
                                finishItem();
                            } catch (const std::exception& error) {
                                if (metadataOnly) {
                                    if (!forceManual) recordAutomaticIdentityResult(executable, false, error.what());
                                    postStageResult("identity-failed", false, {{"error", error.what()}});
                                }
                                else {
                                    failed = true;
                                    postCompleted(target, "failed", stage, {{"error", error.what()}}, roundNumber);
                                }
                                finishItem();
                            } catch (...) {
                                if (metadataOnly) {
                                    if (!forceManual) recordAutomaticIdentityResult(executable, false, "身份识别发生未知异常");
                                    postStageResult("identity-failed", false, {{"error", "身份识别发生未知异常"}});
                                }
                                else {
                                    failed = true;
                                    postCompleted(target, "failed", stage, {{"error", "素材识别发生未知异常"}}, roundNumber);
                                }
                                finishItem();
                            }
                        };
                        const int workerCount = (std::min)(stageConcurrency, static_cast<int>(stageQueue.size()));
                        std::vector<std::thread> workers;
                        workers.reserve(workerCount);
                        std::exception_ptr poolError;
                        std::exception_ptr workerError;
                        std::mutex workerErrorMutex;
                        try {
                            for (int workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
                                workers.emplace_back([&] {
                                    try {
                                        while (!job->cancelRequested.load() && !cancelled.load()) {
                                            const auto queueIndex = nextIndex.fetch_add(1);
                                            if (queueIndex >= stageQueue.size()) break;
                                            processOne(queueIndex);
                                        }
                                    } catch (...) {
                                        // Never allow an unexpected exception
                                        // from an item or its error-reporting
                                        // path to escape a native thread.
                                        {
                                            std::lock_guard<std::mutex> errorLock(workerErrorMutex);
                                            if (!workerError) workerError = std::current_exception();
                                        }
                                        cancelled = true;
                                        job->cancelRequested = true;
                                    }
                                });
                            }
                        } catch (...) {
                            poolError = std::current_exception();
                            cancelled = true;
                            job->cancelRequested = true;
                        }
                        for (auto& worker : workers) {
                            if (worker.joinable()) worker.join();
                        }
                        if (poolError) std::rethrow_exception(poolError);
                        if (workerError) std::rethrow_exception(workerError);
                    };

                    // Do not put the whole identity queue behind a stage
                    // barrier.  A successful feature/community match must
                    // flow directly into Steam artwork for that same EXE;
                    // otherwise a later retry, plan refresh, or host exit can
                    // strand a valid SteamID with zero images.
                    auto processQueueTarget = [&](const ScrapeQueueTarget& target, size_t targetIndex) {
                        if (cancelled.load() || job->cancelRequested.load()) return;
                        const auto key = backgroundExecutableKey(target.executable);
                        bool retryingVisualFallback = false;
                        {
                            std::lock_guard<std::mutex> lock(stageResultMutex);
                            retryingVisualFallback = inlineFallbackRetryAttempts.count(key) != 0;
                        }
                        if (!target.hasVerifiedSteamId && !retryingVisualFallback) {
                            std::vector<ScrapeQueueTarget> oneTarget{target};
                            runStage(oneTarget, "identity", identityConcurrency, true,
                                "steam-comprehensive-identity-only", 1, targetIndex);
                        }
                        if (cancelled.load() || job->cancelRequested.load()) return;

                        int resolvedSteamAppId = target.steamAppId;
                        uint64_t resolvedIgdbId = target.igdbId;
                        bool identityReady = target.hasVerifiedSteamId && resolvedSteamAppId > 0;
                        bool identityDidFail = false;
                        {
                            std::lock_guard<std::mutex> lock(stageResultMutex);
                            const auto resolved = identityResolvedSteamIds.find(key);
                            if (resolved != identityResolvedSteamIds.end() && resolved->second > 0) {
                                resolvedSteamAppId = resolved->second;
                                identityReady = true;
                            }
                            identityReady = identityReady || identitySucceeded.count(key) != 0;
                            identityDidFail = identityFailed.count(key) != 0;
                            const auto resolvedIgdb = identityResolvedIgdbIds.find(key);
                            if (resolvedIgdb != identityResolvedIgdbIds.end() && resolvedIgdb->second > 0) {
                                resolvedIgdbId = resolvedIgdb->second;
                            }
                        }
                        if (identityReady && resolvedSteamAppId > 0) {
                            auto artworkTarget = target;
                            artworkTarget.hasVerifiedSteamId = true;
                            artworkTarget.steamAppId = resolvedSteamAppId;
                            // A newly resolved identity has no artwork in its
                            // metadata-only manifest. Existing artwork state
                            // is still respected for already verified items.
                            const bool needsArtwork = !target.hasVerifiedSteamId ||
                                target.artworkPending || !target.artworkComplete;
                            if (needsArtwork) {
                                artworkTarget.artworkPending = true;
                                std::vector<ScrapeQueueTarget> oneTarget{artworkTarget};
                                if (!acquireArtworkLane(key)) return;
                                try {
                                    runStage(oneTarget, "artwork", artworkConcurrency, false,
                                        "steam-artwork-only", 1, targetIndex);
                                } catch (...) {
                                    releaseArtworkLane();
                                    throw;
                                }
                                releaseArtworkLane();
                                bool artworkNeedsFallback = false;
                                {
                                    std::lock_guard<std::mutex> lock(stageResultMutex);
                                    artworkNeedsFallback = artworkFallbackPendingKeys.count(key) != 0;
                                }
                                 if (artworkNeedsFallback) {
                                    auto fallbackTarget = artworkTarget;
                                    fallbackTarget.artworkFallbackEligible = true;
                                    fallbackTarget.artworkFallbackPending = true;
                                    int retryAttempt = 0;
                                    {
                                        std::lock_guard<std::mutex> lock(stageResultMutex);
                                        retryAttempt = ++artworkRetryAttempts[key];
                                    }
                                    if (retryAttempt <= kScrapeRetryCount) {
                                        scheduleScrapeRetry(fallbackTarget, targetIndex, "artwork", retryAttempt);
                                    } else {
                                        reportRoundDone(target, 1);
                                        pauseScrapeBudget(key);
                                 std::lock_guard<std::mutex> queueLock(artworkFallbackQueueMutex);
                                        artworkFallbackQueue.push_back(std::move(fallbackTarget));
                                    }
                                }
                            } else {
                                postCompleted(artworkTarget, "ready", "artwork", json::object(), 1);
                            }
                            return;
                        }

                         if (identityDidFail) {
                             const bool needsVisualFallback = target.artworkFallbackEligible ||
                                 (!target.artworkComplete && !target.hasVerifiedSteamId);
                             if (needsVisualFallback) {
                                 // Lack of a SteamID (including a definitive no-match)
                                 // must not block pictures behind identity retries or
                                 // the slowest game in the batch. Keep this per-game
                                 // chain bounded and respect the same manual lease.
                                 auto fallbackTarget = target;
                                 fallbackTarget.igdbId = resolvedIgdbId;
                                 fallbackTarget.artworkFallbackEligible = true;
                                 fallbackTarget.artworkFallbackPending = true;
                                 std::vector<ScrapeQueueTarget> oneTarget{fallbackTarget};
                                 {
                                     std::lock_guard<std::mutex> lock(stageResultMutex);
                                     artworkFallbackFailedKeys.erase(key);
                                 }
                                 if (!acquireArtworkLane(key)) return;
                                 try {
                                     runStage(oneTarget, "artwork-fallback", artworkConcurrency,
                                         false, "igdb-artwork-only", 1, targetIndex);
                                 } catch (...) {
                                     releaseArtworkLane();
                                     throw;
                                 }
                                 releaseArtworkLane();
                                 if (cancelled.load() || job->cancelRequested.load()) return;
                                 int retryAttempt = 0;
                                 {
                                     std::lock_guard<std::mutex> lock(stageResultMutex);
                                     if (completedKeys.count(key) || deferredKeys.count(key) ||
                                         !artworkFallbackFailedKeys.count(key)) return;
                                     retryAttempt = ++inlineFallbackRetryAttempts[key];
                                 }
                                 if (retryAttempt <= kScrapeRetryCount) {
                                     scheduleScrapeRetry(fallbackTarget, targetIndex, "artwork-fallback", retryAttempt);
                                 } else {
                                     postCompleted(fallbackTarget, "failed", "artwork-fallback", {
                                         {"reason", "artwork-fallback-retries-exhausted"},
                                         {"retryLimit", kScrapeRetryCount}
                                     }, 1);
                                 }
                                 return;
                             }
                             // A metadata-verified IGDB match is enough to
                             // start the explicitly non-Steam artwork fallback.
                             // Retrying Steam identity first only delays the
                             // promised second round for games absent from Steam.
                             if (resolvedIgdbId > 0) {
                                 reportRoundDone(target, 1);
                                 auto fallbackTarget = target;
                                 fallbackTarget.igdbId = resolvedIgdbId;
                                 fallbackTarget.artworkFallbackEligible = true;
                                 fallbackTarget.artworkFallbackPending = true;
                                 pauseScrapeBudget(key);
                                 std::lock_guard<std::mutex> queueLock(artworkFallbackQueueMutex);
                                 artworkFallbackQueue.push_back(std::move(fallbackTarget));
                                 return;
                             }
                             int retryAttempt = 0;
                             {
                                 std::lock_guard<std::mutex> lock(stageResultMutex);
                                 retryAttempt = ++identityRetryAttempts[key];
                             }
                             if (retryAttempt <= kScrapeRetryCount) {
                                 scheduleScrapeRetry(target, targetIndex, "identity", retryAttempt);
                                 return;
                             }
                             reportRoundDone(target, 1);
                             // This is the second-round path only. IGDB (or a
                            // prior non-Steam match) may provide artwork, but
                            // it is deliberately never promoted to SteamID.
                            const bool artworkEligible = target.artworkFallbackEligible ||
                                (!target.artworkComplete && !target.hasVerifiedSteamId);
                            if (artworkEligible && (!target.artworkComplete || target.artworkFallbackPending)) {
                                auto fallbackTarget = target;
                                fallbackTarget.artworkFallbackEligible = true;
                                fallbackTarget.artworkFallbackPending = true;
                                pauseScrapeBudget(key);
                                std::lock_guard<std::mutex> queueLock(artworkFallbackQueueMutex);
                                artworkFallbackQueue.push_back(std::move(fallbackTarget));
                            } else {
                                reportRoundDone(target, 1);
                            }
                        }
                    };

                    const int laneCount = (std::min)(identityConcurrency, static_cast<int>(queue.size()));
                    std::vector<std::thread> lanes;
                    std::exception_ptr laneError;
                    std::mutex laneErrorMutex;
                    for (size_t targetIndex = 0; targetIndex < queue.size(); ++targetIndex) {
                        enqueuePendingTarget(queue[targetIndex], targetIndex,
                            std::chrono::steady_clock::now());
                    }
                    {
                        std::lock_guard<std::mutex> lock(historyMutex);
                        history["dispatch"] = {
                            {"startedAt", unixTimeMsHost()},
                            {"identityLanes", laneCount},
                            {"queueSize", queue.size()}
                        };
                    }
                    persistHistory();
                    try {
                        lanes.reserve(laneCount);
                        for (int lane = 0; lane < laneCount; ++lane) {
                            lanes.emplace_back([&] {
                                try {
                                    while (true) {
                                    std::optional<PendingScrapeTarget> pending;
                                    {
                                        std::unique_lock<std::mutex> lock(pendingTargetsMutex);
                                        while (!cancelled.load() && !job->cancelRequested.load()) {
                                            if (pendingTargets.empty()) {
                                                if (activePendingTargets == 0) return;
                                                pendingTargetsCv.wait(lock);
                                                continue;
                                            }
                                            const auto due = pendingTargets.top().due;
                                            const auto now = std::chrono::steady_clock::now();
                                            if (due > now) {
                                                pendingTargetsCv.wait_until(lock, due);
                                                continue;
                                            }
                                            pending = pendingTargets.top();
                                            pendingTargets.pop();
                                            ++activePendingTargets;
                                            break;
                                        }
                                    }
                                    if (!pending) break;
                                    try {
                                        processQueueTarget(pending->target, pending->targetIndex);
                                    } catch (...) {
                                        std::lock_guard<std::mutex> errorLock(laneErrorMutex);
                                        if (!laneError) laneError = std::current_exception();
                                        cancelled = true;
                                        job->cancelRequested = true;
                                    }
                                    {
                                        std::lock_guard<std::mutex> lock(pendingTargetsMutex);
                                        if (activePendingTargets > 0) --activePendingTargets;
                                    }
                                    pendingTargetsCv.notify_all();
                                    if (cancelled.load() || job->cancelRequested.load()) break;
                                }
                                } catch (...) {
                                    // Queue acquisition/copying itself also
                                    // runs on a native thread. Convert any
                                    // unexpected scheduler exception into the
                                    // joined-lane error path.
                                    std::lock_guard<std::mutex> errorLock(laneErrorMutex);
                                    if (!laneError) laneError = std::current_exception();
                                    cancelled = true;
                                    job->cancelRequested = true;
                                    pendingTargetsCv.notify_all();
                                }
                            });
                        }
                    } catch (...) {
                        laneError = std::current_exception();
                        cancelled = true;
                        pendingTargetsCv.notify_all();
                    }
                    for (auto& lane : lanes) {
                        if (lane.joinable()) lane.join();
                    }
                    if (laneError) std::rethrow_exception(laneError);

                    // Refresh once after the first-round serial chain.  This
                    // enriches second-round fallback IDs and updates the UI,
                    // but it is no longer on the critical path between a
                    // verified SteamID and its artwork request.
                    if (!cancelled.load() && !job->cancelRequested.load()) {
                        try {
                            auto plannedSnapshot = workspaceSnapshot(false, json::object(), job);
                            plannedSnapshot["steamPlan"] = buildLocalSteamPlanSnapshot(
                                plannedSnapshot, jsonObjectOr(plannedSnapshot, "steamPlan"));
                            attachWorkspaceArtwork(plannedSnapshot["steamPlan"], plannedSnapshot["steamAccounts"]);
                            if (!job->cancelRequested.load()) {
                                postPipelineEvent("library-plan-refreshed", {
                                    {"snapshot", plannedSnapshot}, {"source", "identity-stage"}
                                });
                            }
                            const auto refreshedPlan = plannedSnapshot.contains("steamPlan") &&
                                plannedSnapshot["steamPlan"].is_object()
                                ? plannedSnapshot["steamPlan"] : json::object();
                            const auto refreshedItems = refreshedPlan.contains("items") &&
                                refreshedPlan["items"].is_array()
                                ? refreshedPlan["items"] : json::array();
                            for (auto& fallbackTarget : artworkFallbackQueue) {
                                for (const auto& item : refreshedItems) {
                                    if (!item.is_object() ||
                                        jsonStringOr(item, "primaryExecutable") != fallbackTarget.executable) continue;
                                    const auto refreshedIgdbId = jsonIntegerOr(item, "igdbId", 0);
                                    if (refreshedIgdbId > 0) {
                                        fallbackTarget.igdbId = static_cast<uint64_t>(refreshedIgdbId);
                                    }
                                    break;
                                }
                            }
                        } catch (...) {
                            // A plan/account refresh is advisory.  The Steam
                            // artwork requests above already ran, and the
                            // original fallback ID remains usable here.
                        }
                    }
                    round2Total = artworkFallbackQueue.size();
                        if (!artworkFallbackQueue.empty() && !cancelled.load() && !job->cancelRequested.load()) {
                            {
                                std::lock_guard<std::mutex> lock(g_scrapeStateMutex);
                                g_scrapeState["round"] = 2;
                                g_scrapeState["roundLabel"] = "第二轮";
                                g_scrapeState["roundCurrent"] = 0;
                                g_scrapeState["roundTotal"] = round2Total;
                            }
                            postPipelineEvent("scrape-round-started", {
                                {"batchId", batchId}, {"round", 2}, {"roundLabel", "第二轮"},
                                {"roundCurrent", 0}, {"roundTotal", round2Total},
                                {"current", completed.load()}, {"total", total}
                            });
                            std::vector<ScrapeQueueTarget> pendingFallbackTargets = artworkFallbackQueue;
                            for (int retryAttempt = 0;
                                 retryAttempt <= kScrapeRetryCount && !pendingFallbackTargets.empty();
                                 ++retryAttempt) {
                                if (retryAttempt > 0) {
                                    const auto delayMs = kScrapeRetryDelayMs[retryAttempt - 1];
                                    for (const auto& fallbackTarget : pendingFallbackTargets) {
                                        updateScrapeState(backgroundExecutableKey(fallbackTarget.executable), {
                                            {"status", "queued"}, {"stage", "artwork-fallback"},
                                            {"executable", fallbackTarget.executable},
                                            {"retryAttempt", retryAttempt}, {"retryDelayMs", delayMs}
                                        });
                                    }
                                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                                }
                                {
                                    std::lock_guard<std::mutex> lock(stageResultMutex);
                                    artworkFallbackFailedKeys.clear();
                                }
                                runStage(pendingFallbackTargets, "artwork-fallback", artworkConcurrency,
                                    false, "igdb-artwork-only", 2);
                                if (cancelled.load() || job->cancelRequested.load()) break;
                                std::vector<ScrapeQueueTarget> failedFallbackTargets;
                                {
                                    std::lock_guard<std::mutex> lock(stageResultMutex);
                                    for (const auto& fallbackTarget : pendingFallbackTargets) {
                                        const auto fallbackKey = backgroundExecutableKey(fallbackTarget.executable);
                                        if (artworkFallbackFailedKeys.count(fallbackKey) &&
                                            !completedKeys.count(fallbackKey)) {
                                            failedFallbackTargets.push_back(fallbackTarget);
                                        }
                                    }
                                }
                                pendingFallbackTargets = std::move(failedFallbackTargets);
                            }
                            if (!cancelled.load() && !job->cancelRequested.load()) {
                                for (const auto& fallbackTarget : pendingFallbackTargets) {
                                    postCompleted(fallbackTarget, "failed", "artwork-fallback", {
                                        {"reason", "artwork-fallback-retries-exhausted"},
                                        {"retryLimit", kScrapeRetryCount}
                                    }, 2);
                                }
                            }
                        }
                        if (!cancelled.load() && !job->cancelRequested.load()) {
                            for (const auto& target : queue) {
                                const auto key = backgroundExecutableKey(target.executable);
                                {
                                    std::lock_guard<std::mutex> lock(stageResultMutex);
                                    if (deferredKeys.count(key) || completedKeys.count(key)) continue;
                                }
                                bool identityDidFail = false;
                                {
                                    std::lock_guard<std::mutex> lock(stageResultMutex);
                                    identityDidFail = identityFailed.count(key) != 0;
                                }
                                postCompleted(target, identityDidFail ? "identity-failed" : "not-ready", "final", {
                                    {"reason", "identity-or-artwork-still-pending"}
                                }, 1);
                            }
                        }
                }
            } catch (const std::exception& error) {
                if (job->cancelRequested.load()) cancelled = true;
                else {
                    failed = true;
                    batchError = error.what();
                    postPipelineEvent("scrape-error", {{"error", error.what()}});
                }
            } catch (...) {
                if (job->cancelRequested.load()) cancelled = true;
                else {
                    failed = true;
                    batchError = "自动刮削队列发生未知异常";
                    postPipelineEvent("scrape-error", {{"error", "自动刮削队列发生未知异常"}});
                }
            }
            setScrapeQueueState(
                false, completed.load(), static_cast<int>(total), limit, artworkConcurrency,
                identityConcurrency, artworkConcurrency);
            try {
                const auto state = scrapeStateSnapshot();
                const bool wasCancelled = cancelled.load() || job->cancelRequested.load();
                std::lock_guard<std::mutex> lock(historyMutex);
                history["status"] = wasCancelled ? "paused" : (failed.load() ? "failed" : "completed");
                history["finishedAt"] = unixTimeMsHost();
                history["summary"] = {
                    {"total", total},
                    {"completed", completed.load()},
                    {"round1Total", round1Total},
                    {"round2Total", round2Total},
                    {"cancelled", wasCancelled},
                    {"failed", failed.load()}
                };
                if (!batchError.empty()) history["error"] = batchError;
                if (state.contains("items")) history["items"] = state["items"];
            } catch (...) {
                // The queue state is already available in the UI. Do not let
                // a diagnostic snapshot alter the scrape completion path.
            }
            if (!historyPath.empty() && !persistHistory()) {
                appendUiError({
                    {"type", "scrape-history-write-failed"},
                    {"historyPath", toUtf8(historyPath.wstring())}
                });
            }
            pruneScrapeHistory();
            if (cancelled.load()) {
                postPipelineEvent("scrape-paused", {{"batchId", batchId}, {"current", completed.load()}, {"total", total}, {"cancelled", true}});
            } else if (!failed.load()) {
                postPipelineEvent("scrape-complete", {{"batchId", batchId}, {"current", completed.load()}, {"total", total}});
            }
            // The worker updates manifests and the library scan atomically,
            // but the cached Steam add plan is intentionally not rebuilt for
            // every progress tick. Rebuild it once after the batch and push
            // the snapshot from this detached lane. That makes a verified
            // SteamID + complete artwork move to ready-to-add immediately
            // without turning the UI command lane into a blocking gate.
            if (!job->cancelRequested.load()) {
                try {
                    const auto plannedSnapshot = workspaceSnapshot(true, json::object(), job);
                    if (!job->cancelRequested.load()) {
                        postPipelineEvent("library-plan-refreshed", {
                            {"snapshot", plannedSnapshot}, {"source", "scrape-queue"}
                        });
                    }
                } catch (...) {
                    // The newest cached plan remains visible. A weak or
                    // damaged Steam installation must not turn a completed
                    // scrape into a foreground error.
                }
            }
            finishBackgroundJob(g_scrapeQueueJobs, job);
            const bool rerunManual = g_scrapeQueueForceRequested.exchange(false);
            g_scrapeQueueRunning = false;
            if (rerunManual && !g_shuttingDown.load()) startScrapeQueue(true);
            } catch (const std::exception& error) {
                markScrapeHistoryFailed(failureHistoryPath, error.what());
                appendUiError({
                    {"type", "scrape-queue-thread-error"},
                    {"error", error.what()}
                });
                postPipelineEvent("scrape-error", {
                    {"error", "自动刮削调度异常，已安全停止本轮"}
                });
                finishBackgroundJob(g_scrapeQueueJobs, job);
                g_scrapeQueueForceRequested = false;
                g_scrapeQueueRunning = false;
            } catch (...) {
                markScrapeHistoryFailed(failureHistoryPath, "unknown exception");
                appendUiError({
                    {"type", "scrape-queue-thread-error"},
                    {"error", "unknown exception"}
                });
                postPipelineEvent("scrape-error", {
                    {"error", "自动刮削调度发生未知异常，已安全停止本轮"}
                });
                finishBackgroundJob(g_scrapeQueueJobs, job);
                g_scrapeQueueForceRequested = false;
                g_scrapeQueueRunning = false;
            }
        });
    } catch (...) {
        finishBackgroundJob(g_scrapeQueueJobs, job);
        g_scrapeQueueRunning = false;
        throw;
    }
    return true;
}

static bool startManualIdentifyTask(const json& arguments) {
    custom_steam_library::rejectBuiltinExcludedSteamTool(arguments);
    const auto executable = jsonStringOr(arguments, "executable");
    const auto manualName = trimHost(jsonStringOr(arguments, "manualName"));
    const auto steamId = trimHost(jsonStringOr(arguments, "steamId"));
    const auto igdbId = trimHost(jsonStringOr(arguments, "igdbId"));
    const auto key = backgroundExecutableKey(executable);
    const auto gameLease = reserveManualGameTask(executable);
    // A manual choice has priority over a batch item for this EXE.  Cancel
    // only that item's worker; other games in the batch remain available.
    cancelBackgroundJobs(g_scrapeQueueJobs, "library-item|" + key);
    BackgroundJob job;
    try {
        job = beginBackgroundJob(
            "manual-identify", g_manualIdentifyJobs, g_manualIdentifyGenerations, key);
    } catch (...) {
        releaseGameTask(gameLease);
        throw;
    }
    const auto outputDirectory = workspaceJobsRoot() / L"workspace-identification" /
        (std::to_wstring(unixTimeMsHost()) + L"-" + toWide(std::to_string(job->generation)));
    postPipelineEvent("manual-identify-started", {
        {"executable", executable}, {"jobId", job->jobId},
        {"generation", job->generation}, {"retryLimit", 3}
    });
    try {
        launchHostBackgroundTask([executable, manualName, steamId, igdbId, outputDirectory, key, job, gameLease] {
        bool requestArtworkFollowup = false;
        // Manual enrichment is also weak-network work.  Keep the boundary
        // free while the worker retries; only its local state commits lock it.
        try {
            fs::create_directories(outputDirectory);
            std::vector<std::wstring> args = {toWide(executable), outputDirectory.wstring(), L"--platform", L"pc"};
            if (!manualName.empty()) { args.push_back(L"--save-name"); args.push_back(toWide(manualName)); }
            if (!steamId.empty()) {
                args.push_back(L"--steam-id"); args.push_back(toWide(steamId));
                args.push_back(L"--save-id");
            } else if (!igdbId.empty()) {
                args.push_back(L"--igdb-id"); args.push_back(toWide(igdbId));
                args.push_back(L"--save-id");
            }
            // Match automatic scraping's protection rule: keep existing good
            // artwork and only fill missing/invalid fields during enrichment.
            args.push_back(L"--only-missing");
            ProcessResult result{};
            constexpr int maxAttempts = 3;
            constexpr DWORD manualAttemptTimeoutMs = 90u * 1000u;
            int attempts = 0;
            for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
                attempts = attempt;
                result = runProcess(g_worker, args, manualAttemptTimeoutMs,
                    &job->cancelRequested, &job->activeProcess);
                if (result.exitCode == 0) break;
                if (job->cancelRequested.load()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(250 * attempt));
            }
            const auto overridesPath = g_dataRoot / L"config" / L"manual-overrides.json";
            auto persistState = [&](const std::string& status, bool retryable, const std::string& error) {
                if (!jobIsCurrent(g_manualIdentifyGenerations, job)) return;
                std::lock_guard<std::mutex> boundaryLock(g_taskBoundaryMutex);
                if (!jobIsCurrent(g_manualIdentifyGenerations, job)) return;
                HostDataTransaction configTransaction;

                auto overrides = readJson(overridesPath, json{{"schemaVersion", 1}, {"items", json::object()}});
                if (!overrides.is_object()) overrides = json::object();
                overrides["schemaVersion"] = 1;
                if (!overrides.contains("items") || !overrides["items"].is_object()) overrides["items"] = json::object();
                auto& item = overrides["items"][toUtf8(normalizedPathKey(fs::path(toWide(executable))))];
                if (!item.is_object()) item = json::object();
                item["identityStatus"] = status;
                item["retryable"] = retryable;
                item["attempts"] = attempts;
                item["lastError"] = error.empty() ? json(nullptr) : json(error);
                item["updatedAt"] = unixTimeMsHost();
                writeJsonWithLocalBackup(overridesPath, overrides);

                configTransaction.release();
            };
            if (result.exitCode == 0 && jobIsCurrent(g_manualIdentifyGenerations, job)) {
                // Promote only a manifest belonging to this EXE.  This keeps a
                // previous game's partial output from becoming the new identity.
                std::error_code manifestError;
                std::string resolvedFormalName;
                int64_t resolvedSteamId = 0;
                int64_t resolvedIgdbId = 0;
                std::string resolvedSteamVerificationStatus;
                std::string resolvedMetadataVerificationStatus;
                int64_t resolvedSteamIdCandidate = 0;
                for (const auto& entry : fs::recursive_directory_iterator(outputDirectory, manifestError)) {
                    if (manifestError) { manifestError.clear(); break; }
                    if (!entry.is_regular_file(manifestError) || entry.path().filename() != L"manifest.json") { manifestError.clear(); continue; }
                    const auto document = readJson(entry.path(), json::object());
                    if (samePath(fs::path(toWide(jsonStringOr(document, "exe"))), fs::path(toWide(executable)))) {
                        if (jobIsCurrent(g_manualIdentifyGenerations, job)) persistResolvedIdentityCache(entry.path());
                        const auto match = jsonObjectOr(document, "match");
                        resolvedFormalName = jsonStringOr(match, "formalName", jsonStringOr(match, "displayName"));
                        resolvedSteamId = jsonIntegerOr(match, "storefrontAppId", jsonIntegerOr(match, "appId", 0));
                        resolvedIgdbId = jsonIntegerOr(match, "igdbId", 0);
                        resolvedSteamVerificationStatus = jsonStringOr(match, "steamVerificationStatus");
                        resolvedMetadataVerificationStatus = jsonStringOr(match, "metadataVerificationStatus");
                        resolvedSteamIdCandidate = jsonIntegerOr(match, "steamIdCandidate", 0);
                    }
                    manifestError.clear();
                }
                if (!resolvedFormalName.empty()) {
                    if (jobIsCurrent(g_manualIdentifyGenerations, job)) {
                        std::lock_guard<std::mutex> boundaryLock(g_taskBoundaryMutex);
                        if (jobIsCurrent(g_manualIdentifyGenerations, job)) {
                            HostDataTransaction configTransaction;

                            auto overrides = readJson(overridesPath, json{{"schemaVersion", 1}, {"items", json::object()}});
                            if (!overrides.is_object()) overrides = json::object();
                            if (!overrides.contains("items") || !overrides["items"].is_object()) overrides["items"] = json::object();
                            const auto currentKey = toUtf8(normalizedPathKey(fs::path(toWide(executable))));
                            const auto currentItem = overrides["items"].find(currentKey);
                            const bool clearWonRace = currentItem != overrides["items"].end() && currentItem->is_object()
                                && currentItem->value("idCleared", false);
                            overrides["schemaVersion"] = 1;
                            auto& item = overrides["items"][currentKey];
                            if (!item.is_object()) item = json::object();
                            // A name typed or selected by the user is authoritative.
                            // Network enrichment may add formalName/IDs, but must never
                            // silently replace that explicit evidence (including an
                            // intentionally unusual test value such as 123123).
                            if (manualName.empty()) item["name"] = resolvedFormalName;
                            item["formalName"] = resolvedFormalName;
                            const bool steamVerified = resolvedSteamId > 0 &&
                                resolvedSteamVerificationStatus == "steam-verified";
                            const bool metadataVerified = resolvedIgdbId > 0 &&
                                resolvedMetadataVerificationStatus == "metadata-verified";
                            if (!clearWonRace && steamVerified) item["steamId"] = resolvedSteamId;
                            if (!clearWonRace && metadataVerified) item["igdbId"] = resolvedIgdbId;
                            if (resolvedSteamVerificationStatus.empty()) {
                                resolvedSteamVerificationStatus = steamVerified
                                    ? "steam-verified" : "needs-confirmation";
                            }
                            if (resolvedMetadataVerificationStatus.empty()) {
                                resolvedMetadataVerificationStatus = metadataVerified
                                    ? "metadata-verified" : "not-available";
                            }
                            item["steamVerificationStatus"] = resolvedSteamVerificationStatus;
                            item["metadataVerificationStatus"] = resolvedMetadataVerificationStatus;
                            if (resolvedSteamIdCandidate > 0 && !steamVerified) {
                                item["steamIdCandidate"] = resolvedSteamIdCandidate;
                            }
                            const std::string finalIdentityStatus = steamVerified
                                ? "ready"
                                : (resolvedSteamVerificationStatus == "network-pending"
                                    ? "pending-network"
                                    : (metadataVerified ? "metadata-verified" : "needs-confirmation"));
                            item["identityStatus"] = finalIdentityStatus;
                            item["updatedAt"] = unixTimeMsHost();
                            writeJsonWithLocalBackup(overridesPath, overrides);

                            configTransaction.release();
                            resolvedSteamVerificationStatus = finalIdentityStatus;
                        }
                    }
                }
                const std::string reportedStatus = resolvedSteamVerificationStatus.empty()
                    ? "needs-confirmation" : resolvedSteamVerificationStatus;
                persistState(reportedStatus, reportedStatus != "ready", {});
                if (jobIsCurrent(g_manualIdentifyGenerations, job)) {
                    requestArtworkFollowup = true;
                    postPipelineEvent("manual-identify-complete", {
                        {"executable", executable}, {"jobId", job->jobId},
                        {"generation", job->generation}, {"attempts", attempts},
                        {"identityStatus", resolvedSteamVerificationStatus},
                        {"outputDirectory", toUtf8(outputDirectory.wstring())}
                    });
                }
            } else {
                std::string detail = result.output;
                if (detail.size() > 1600) detail = detail.substr(detail.size() - 1600);
                const bool cancelled = job->cancelRequested.load() || detail.find("TASK_CANCELLED") != std::string::npos;
                bool partialManifest = false;
                if (!cancelled) {
                    try {
                        // Artwork can return a non-zero code after writing a
                        // valid identity and a partial tall/hero set.  Keep
                        // that identity and let the normal artwork queue
                        // retry only the missing slots.
                        partialManifest = persistResolvedIdentityFromOutput(outputDirectory, executable);
                    } catch (...) {
                        partialManifest = false;
                    }
                }
                if (partialManifest && jobIsCurrent(g_manualIdentifyGenerations, job)) {
                    requestArtworkFollowup = true;
                }
                persistState("pending-network", true, detail);
                if (jobIsCurrent(g_manualIdentifyGenerations, job)) {
                    postPipelineEvent("manual-identify-failed", {
                        {"executable", executable}, {"jobId", job->jobId},
                        {"generation", job->generation}, {"error", detail},
                        {"retryable", true}, {"cancelled", cancelled}, {"attempts", attempts}
                    });
                }
            }
        } catch (const std::exception& error) {
            if (jobIsCurrent(g_manualIdentifyGenerations, job)) {
                postPipelineEvent("manual-identify-failed", {
                    {"executable", executable}, {"jobId", job->jobId},
                    {"generation", job->generation}, {"error", error.what()},
                    {"retryable", true}, {"attempts", 0}
                });
            }
        } catch (...) {
            if (jobIsCurrent(g_manualIdentifyGenerations, job)) {
                postPipelineEvent("manual-identify-failed", {
                    {"executable", executable}, {"jobId", job->jobId},
                    {"generation", job->generation}, {"error", "后台手动识别发生未知异常"},
                    {"retryable", true}
                });
            }
        }
            releaseGameTask(gameLease);
            // The identity worker may have completed before its plan was
            // regenerated.  A detached forced follow-up refreshes the plan
            // and fills any artwork slot that the manual worker could not
            // finish, without blocking the editor or creating a second
            // worker for the same EXE.
            if (requestArtworkFollowup && !g_shuttingDown.load()) {
                // The manual worker already bypassed automatic suppression.
                // The follow-up is only a detached plan/artwork continuation;
                // keep it out of the UI's manual-refresh progress label.
                startScrapeQueue(false);
            }
            finishBackgroundJob(g_manualIdentifyJobs, job);
        });
    } catch (...) {
        releaseGameTask(gameLease);
        finishBackgroundJob(g_manualIdentifyJobs, job);
        throw;
    }
    return true;
}

static json startIdentitySearchTask(const json& arguments) {
    custom_steam_library::rejectBuiltinExcludedSteamTool(arguments);
    const auto executable = jsonStringOr(arguments, "executable");
    const auto query = trimHost(jsonStringOr(arguments, "query"));
    const auto key = backgroundExecutableKey(executable);
    const auto job = beginBackgroundJob(
        "identity-search", g_identitySearchJobs, g_identitySearchGenerations, key);
    const auto outputDirectory = workspaceJobsRoot() / L"workspace-identity-search" /
        (std::to_wstring(unixTimeMsHost()) + L"-" + toWide(std::to_string(job->generation)));
    postPipelineEvent("identity-search-started", {
        {"executable", executable}, {"query", query},
        {"jobId", job->jobId}, {"generation", job->generation}
    });
    try {
    launchHostBackgroundTask([executable, query, outputDirectory, job] {
        try {
            fs::create_directories(outputDirectory);
            const std::vector<std::wstring> args = {
                toWide(executable), outputDirectory.wstring(), L"--platform", L"pc",
                L"--name", toWide(query), L"--metadata-only"
            };
            const auto result = runProcess(g_worker, args, 90u * 1000u,
                &job->cancelRequested, &job->activeProcess);
            const auto candidates = identitySearchCandidatesFromDirectory(outputDirectory);
            const bool cancelled = job->cancelRequested.load();
            if (jobIsCurrent(g_identitySearchGenerations, job) && (result.exitCode == 0 || !candidates.empty())) {
                postPipelineEvent("identity-search-complete", {
                    {"executable", executable}, {"query", query},
                    {"jobId", job->jobId}, {"generation", job->generation},
                    {"candidates", candidates}, {"workerExitCode", result.exitCode},
                    {"outputDirectory", toUtf8(outputDirectory.wstring())},
                    {"partial", result.exitCode != 0}
                });
            } else if (jobIsCurrent(g_identitySearchGenerations, job)) {
                std::string detail = result.output;
                if (detail.size() > 1600) detail = detail.substr(detail.size() - 1600);
                postPipelineEvent("identity-search-failed", {
                    {"executable", executable}, {"query", query},
                    {"jobId", job->jobId}, {"generation", job->generation},
                    {"error", detail}, {"retryable", !cancelled}, {"cancelled", cancelled}
                });
            }
        } catch (const std::exception& error) {
            if (jobIsCurrent(g_identitySearchGenerations, job)) postPipelineEvent("identity-search-failed", {
                {"executable", executable}, {"query", query},
                {"jobId", job->jobId}, {"generation", job->generation},
                {"error", error.what()}, {"retryable", true}
            });
        } catch (...) {
            if (jobIsCurrent(g_identitySearchGenerations, job)) postPipelineEvent("identity-search-failed", {
                {"executable", executable}, {"query", query},
                {"jobId", job->jobId}, {"generation", job->generation},
                {"error", "候选名称后台搜索发生未知异常"}, {"retryable", true}
            });
        }
        finishBackgroundJob(g_identitySearchJobs, job);
    });
    } catch (...) {
        finishBackgroundJob(g_identitySearchJobs, job);
        throw;
    }
    return {{"jobId", job->jobId}, {"generation", job->generation}};
}
static void startLibraryPipeline(bool allowFirstRunDefaults) {
    bool expected = false;
    if (!g_libraryPipelineRunning.compare_exchange_strong(expected, true)) return;
    const bool firstRunDiscovery = allowFirstRunDefaults && firstRunDefaultScanPending();
    BackgroundJob job;
    try {
        job = beginBackgroundJob(
            "library-pipeline", g_libraryPipelineJobs, g_libraryPipelineGenerations, "library");
    } catch (...) {
        g_libraryPipelineRunning = false;
        throw;
    }
    postPipelineEvent("library-open-started");
    try {
        if (firstRunDiscovery) {
            postPipelineEvent("library-first-run-started", {
                {"automatic", true}, {"candidateSearchDepth", 2},
                {"drivePolicy", "fixed-only"}, {"autoAddToSteam", false}
            });
        }
        launchHostBackgroundTask([job, firstRunDiscovery] {
        // Directory enumeration is local work and must not turn the later
        // weak-network scrape queue into a foreground input gate.
        bool cancelled = false;
        try {
            auto scanArguments = libraryScanWorkerArguments(firstRunDiscovery);
            const auto result = runWorkerBounded(
                scanArguments, kLocalScanWorkerTimeoutMs,
                &job->cancelRequested, &job->activeProcess);
            if (result.exitCode != 0) {
                if (job->cancelRequested.load()) cancelled = true;
                else throw std::runtime_error(result.output.empty() ? "目录扫描失败" : result.output);
            }
            // Publish the local scan immediately.  Plan generation remains in
            // this detached pipeline, so a slow Steam install cannot delay the
            // visible library or any foreground input.
            if (!cancelled && !job->cancelRequested.load()) {
                const auto scanSnapshot = workspaceSnapshot(false, json::object(), job);
                postPipelineEvent("library-scan-complete", {{"snapshot", scanSnapshot}, {"planPending", true}});
                if (firstRunDiscovery) {
                    const auto rootCount = jsonArrayOr(scanSnapshot.value("config", json::object()), "roots").size();
                    postPipelineEvent("library-first-run-scan-complete", {
                        {"snapshot", scanSnapshot}, {"rootCount", rootCount},
                        {"automatic", true}, {"autoAddToSteam", false}
                    });
                }
                if (job->cancelRequested.load()) cancelled = true;
            }
            if (!cancelled && !job->cancelRequested.load()) {
                const auto plannedSnapshot = workspaceSnapshot(true, json::object(), job);
                if (job->cancelRequested.load()) cancelled = true;
                else postPipelineEvent("library-plan-refreshed", {{"snapshot", plannedSnapshot}});
            }
            // Opening the library is the explicit first-pass trigger.  The
            // persisted scraping switch controls whether that first pass is
            // allowed; directory maintenance remains separately disabled by
            // kAutomaticLibraryMaintenanceEnabled.
            if (!cancelled && !job->cancelRequested.load() && automaticScrapingEnabled()) {
                startScrapeQueue(false);
            }
        } catch (const std::exception& error) {
            if (job->cancelRequested.load()) cancelled = true;
            else {
                if (firstRunDiscovery) postPipelineEvent("library-first-run-error", {{"error", error.what()}});
                postPipelineEvent("library-open-error", {{"error", error.what()}});
            }
        } catch (...) {
            if (job->cancelRequested.load()) cancelled = true;
            else {
                if (firstRunDiscovery) postPipelineEvent("library-first-run-error", {{"error", "首次自动扫描未完成，可手动点击扫描游戏库重试。"}});
                postPipelineEvent("library-open-error", {{"error", "打开游戏库时发生未知异常"}});
            }
        }
        if (cancelled || job->cancelRequested.load()) {
            postPipelineEvent("library-open-cancelled", {{"cancelled", true}});
        }
        finishBackgroundJob(g_libraryPipelineJobs, job);
        g_libraryPipelineRunning = false;
        });
    } catch (...) {
        finishBackgroundJob(g_libraryPipelineJobs, job);
        g_libraryPipelineRunning = false;
        throw;
    }
}

static void postWebMessage(const json& message) {
    if (!g_webview) return;
    const auto text = toWide(message.dump());
    g_webview->PostWebMessageAsJson(text.c_str());
}

// UI notifications are best-effort.  A malformed/oversized snapshot or a
// WebView that is already closing must never let an exception escape the
// native window procedure and become CRT terminate/abort (0xC0000409).
static void postWebMessageNoThrow(const json& message) noexcept {
    try { postWebMessage(message); } catch (...) {}
}

static void startNetworkWakeTask(uint64_t id) {
    bool expected = false;
    if (!g_networkWakeRunning.compare_exchange_strong(expected, true)) {
        g_networkWakePending = true;
        if (id != 0) {
            postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", true},
                {"result", {{"queued", true}, {"alreadyRunning", true}}}});
        }
        return;
    }
    g_networkWakeCancelRequested = false;
    BackgroundJob job;
    try {
        job = beginBackgroundJob(
            "network-wake", g_networkWakeJobs, g_networkWakeGenerations, "network");
    } catch (const std::exception& error) {
        g_networkWakeRunning = false;
        postPipelineEvent("network-wake-error", {{"error", error.what()}});
        if (id != 0) postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", false}, {"error", error.what()}});
        return;
    } catch (...) {
        g_networkWakeRunning = false;
        postPipelineEvent("network-wake-error", {{"error", "无法创建网络恢复后台任务"}});
        if (id != 0) postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", false}, {"error", "无法创建网络恢复后台任务"}});
        return;
    }
    postPipelineEvent("network-wake-started");
    try {
    launchHostBackgroundTask([id, job] {
        auto result = std::make_unique<AsyncResult>();
        bool cancelled = false;
        try {
            const auto worker = runWorkerBounded(
                {L"--wake-network-retries", L"--reason", L"network-restored"},
                kWeakNetworkWorkerTimeoutMs,
                &job->cancelRequested,
                &job->activeProcess);
            cancelled = job->cancelRequested.load() ||
                worker.output.find("TASK_CANCELLED") != std::string::npos;
            if (cancelled) {
                postPipelineEvent("network-wake-cancelled", {{"cancelled", true}});
                result->message = {{"kind", "response"}, {"id", id}, {"ok", true},
                    {"result", workspaceSnapshot(false, json::object(), job)}};
            } else {
            if (worker.exitCode != 0 &&
                worker.output.find("NETWORK_RETRIES_SELECTED: 0") == std::string::npos) {
                requireSuccess(worker);
            }
            // Return a local snapshot first.  Plan regeneration is a second
            // background phase, so a slow Steam install cannot delay the
            // user's ability to return to the library or close the window.
            const auto immediateSnapshot = workspaceSnapshot(false, json::object(), job);
            postPipelineEvent("network-wake-complete", {{"output", worker.output}, {"planPending", true}});
            result->message = {{"kind", "response"}, {"id", id}, {"ok", true},
                {"result", immediateSnapshot}};
            if (!job->cancelRequested.load()) {
                const auto refreshedSnapshot = workspaceSnapshot(true, json::object(), job);
                if (!job->cancelRequested.load()) {
                    postPipelineEvent("network-plan-refreshed", {{"snapshot", refreshedSnapshot}});
                }
            }
            }
        } catch (const std::exception& error) {
            if (job->cancelRequested.load()) {
                cancelled = true;
                postPipelineEvent("network-wake-cancelled", {{"cancelled", true}});
                result->message = {{"kind", "response"}, {"id", id}, {"ok", true},
                    {"result", workspaceSnapshot(false, json::object(), job)}};
            } else {
                postPipelineEvent("network-wake-error", {{"error", error.what()}});
                result->message = {{"kind", "response"}, {"id", id}, {"ok", false}, {"error", error.what()}};
            }
        } catch (...) {
            if (job->cancelRequested.load()) {
                cancelled = true;
                postPipelineEvent("network-wake-cancelled", {{"cancelled", true}});
                result->message = {{"kind", "response"}, {"id", id}, {"ok", true},
                    {"result", workspaceSnapshot(false, json::object(), job)}};
            } else {
                postPipelineEvent("network-wake-error", {{"error", "网络恢复任务发生未知异常"}});
                result->message = {{"kind", "response"}, {"id", id}, {"ok", false}, {"error", "网络恢复任务发生未知异常"}};
            }
        }
        finishBackgroundJob(g_networkWakeJobs, job);
        g_networkWakeRunning = false;
        g_networkWakeCancelRequested = false;
        // A network-restored event can arrive while the current wake batch is
        // still replaying.  Preserve that edge-triggered request and run one
        // follow-up batch instead of silently dropping it.
        const bool rerunPending = g_networkWakePending.exchange(false);
        enqueueWorkspaceResult(std::move(result));
        if (rerunPending && !g_shuttingDown.load() && g_window) startNetworkWakeTask(0);
    });
    } catch (...) {
        finishBackgroundJob(g_networkWakeJobs, job);
        g_networkWakeRunning = false;
        throw;
    }
}

static void startArtworkTask(uint64_t id, std::string command, json arguments) {
    auto executable = jsonStringOr(arguments, "executable");
    auto key = backgroundExecutableKey(executable);
    if (key.empty()) key = "query:" + jsonStringOr(arguments, "query");
    key += "|" + jsonStringOr(arguments, "type", "cover");
    const auto job = beginBackgroundJob("artwork", g_artworkJobs, g_artworkGenerations, key);
    try {
    launchHostBackgroundTask([id, command = std::move(command), arguments = std::move(arguments), job]() mutable {
        auto result = std::make_unique<AsyncResult>();
        try {
            result->message = {{"kind", "response"}, {"id", id}, {"ok", true},
                {"result", executeWorkspaceCommand(command, arguments, job)},
                {"jobId", job->jobId}, {"generation", job->generation}};
        } catch (const std::exception& error) {
            result->message = {{"kind", "response"}, {"id", id}, {"ok", false},
                {"error", error.what()}, {"jobId", job->jobId}, {"generation", job->generation}};
        } catch (...) {
            result->message = {{"kind", "response"}, {"id", id}, {"ok", false},
                {"error", "素材后台任务发生未知异常"}, {"jobId", job->jobId}, {"generation", job->generation}};
        }
        if (!jobIsCurrent(g_artworkGenerations, job)) {
            result->message = {{"kind", "response"}, {"id", id}, {"ok", false},
                {"cancelled", true}, {"error", "素材任务已取消或被新请求替代"},
                {"jobId", job->jobId}, {"generation", job->generation}};
        }
        finishBackgroundJob(g_artworkJobs, job);
        enqueueWorkspaceResult(std::move(result));
    });
    } catch (...) {
        finishBackgroundJob(g_artworkJobs, job);
        postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", false},
            {"error", "无法创建素材后台任务"}});
    }
}

static bool requestTaskCancellation() {
    bool requested = false;
    if (g_taskRunning.load()) {
        g_cancelRequested = true;
        requested = true;
    }
    if (g_networkWakeRunning.load()) {
        g_networkWakeCancelRequested = true;
        requested = true;
    }
    requested = cancelAllBackgroundJobs() || requested;
    {
        std::lock_guard<std::mutex> lock(g_activeProcessMutex);
        if (g_activeProcess) {
            TerminateProcess(g_activeProcess, ERROR_CANCELLED);
            requested = true;
        }
    }
    return requested;
}

static void beginCommand(uint64_t id, std::string command, json arguments = json::object()) {
    if (command == "uiError") {
        appendUiError(arguments);
        return;
    }
    if (command == "cancelArtwork") {
        const bool requested = cancelBackgroundJobs(g_artworkJobs);
        postWebMessageNoThrow({{"kind", "response"}, {{"id", id}}, {{"ok", true}}, {{"result", {{"cancelled", requested}}}}});
        return;
    }
    if (command == "cancelIdentitySearch") {
        const auto executable = jsonStringOr(arguments, "executable");
        const bool requested = cancelBackgroundJobs(
            g_identitySearchJobs, executable.empty() ? std::string{} : backgroundExecutableKey(executable));
        postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", true}, {"result", {{"cancelled", requested}}}});
        return;
    }
    if (command == "searchArtwork" || command == "downloadArtworkCandidate") {
        startArtworkTask(id, std::move(command), std::move(arguments));
        return;
    }
    if (command == "wakeNetwork") {
        // Network recovery is event-driven background work. It must never
        // occupy the foreground command lane or make edit/save commands
        // return "已有任务正在运行" while the provider is unavailable.
        startNetworkWakeTask(id);
        return;
    }
    if (command == "cancelTask") {
        const bool requested = requestTaskCancellation();
        postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", true}, {"result", {{"cancelled", requested}}}});
        return;
    }
    if (command == "searchIdentity") {
        // Do not put automatic identity search behind the generic foreground
        // command flag.  The request only validates local inputs and starts a
        // bounded worker; its result arrives later as an identity-search event.
        try {
            postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", true},
                {"result", startIdentitySearchRequest(arguments)}});
        } catch (const std::exception& error) {
            postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", false}, {"error", error.what()}});
        } catch (...) {
            postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", false}, {"error", "无法创建自动识别后台任务"}});
        }
        return;
    }
    // Background scan/recognition is deliberately not a gate for foreground
    // commands.  The command runs on its own detached lane and the stable
    // command lock, when needed, is held only around the local transaction.
    g_cancelRequested = false;
    bool expected = false;
    if (!g_taskRunning.compare_exchange_strong(expected, true)) {
        postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", false}, {"error", "已有任务正在运行"}});
        return;
    }
    try {
        launchHostBackgroundTask([id, command = std::move(command), arguments = std::move(arguments)]() mutable {
            auto result = std::make_unique<AsyncResult>();
            try {
                result->message = {
                    {"kind", "response"}, {"id", id}, {"ok", true},
                    {"result", executeWorkspaceCommand(command, arguments)}
                };
                // Directory watchers describe the configured roots, not the
                // scan job itself.  Restarting a synchronous
                // ReadDirectoryChangesW thread after every scan creates a
                // race with the watcher that just delivered the change (and
                // can terminate the GUI through std::thread's fail-fast
                // path). Keep the watcher alive across ordinary scans; only
                // configuration changes need a refresh.
                result->refreshWatchers = command == "startup" || command == "setEnabled" ||
                    command == "addRoot" || command == "removeRoot";
            } catch (const std::exception& error) {
                result->message = {{"kind", "response"}, {"id", id}, {"ok", false}, {"error", error.what()}};
            } catch (...) {
                result->message = {{"kind", "response"}, {"id", id}, {"ok", false}, {"error", "后台任务发生未知异常"}};
            }
            g_taskRunning = false;
            enqueueWorkspaceResult(std::move(result));
        });
    } catch (const std::exception& error) {
        g_taskRunning = false;
        postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", false}, {"error", error.what()}});
    } catch (...) {
        g_taskRunning = false;
        postWebMessageNoThrow({{"kind", "response"}, {"id", id}, {"ok", false}, {"error", "无法创建后台任务线程"}});
    }
}

struct DirectoryWatcher {
    HANDLE directory = INVALID_HANDLE_VALUE;
    std::atomic<bool> active{true};
    std::thread thread;
};
static std::vector<std::unique_ptr<DirectoryWatcher>> g_watchers;

static void stopDirectoryWatchers() {
    // Move ownership out first.  This keeps the backing DirectoryWatcher
    // object alive until its thread has definitely joined, even if a rare
    // system_error is raised by std::thread::join().
    auto watchers = std::move(g_watchers);
    g_watchers.clear();
    for (auto& watcher : watchers) {
        watcher->active = false;
        if (watcher->directory != INVALID_HANDLE_VALUE) CancelIoEx(watcher->directory, nullptr);
        if (watcher->thread.joinable()) CancelSynchronousIo(watcher->thread.native_handle());
    }
    for (auto& watcher : watchers) {
        if (watcher->thread.joinable()) {
            try {
                watcher->thread.join();
            } catch (...) {
                // Never allow a join failure to destroy a joinable thread and
                // invoke std::terminate from a window message handler.  Keep
                // the object alive until the orphaned thread exits.
                (void)watcher.release();
                continue;
            }
        }
        if (watcher->directory != INVALID_HANDLE_VALUE) CloseHandle(watcher->directory);
    }
}

static void refreshDirectoryWatchers() {
    stopDirectoryWatchers();
    if (!kAutomaticLibraryMaintenanceEnabled) return;
    const auto config = readJson(g_dataRoot / L"config" / L"library-config.json", json::object());
    if (!jsonBoolOr(config, "enabled", true)) return;
    for (const auto& item : jsonArrayOr(config, "roots")) {
        if (!item.is_string()) continue;
        const fs::path root = toWide(item.get<std::string>());
        HANDLE directory = CreateFileW(
            root.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (directory == INVALID_HANDLE_VALUE) continue;
        auto watcher = std::make_unique<DirectoryWatcher>();
        watcher->directory = directory;
        auto* raw = watcher.get();
        try {
            watcher->thread = std::thread([raw]() {
                try {
                    std::vector<unsigned char> buffer(32u << 10);
                    while (raw->active) {
                        DWORD bytes = 0;
                        const BOOL ok = ReadDirectoryChangesW(
                            raw->directory, buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
                            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
                            &bytes, nullptr, nullptr);
                        if (!ok || !raw->active) break;
                        if (g_window) PostMessageW(g_window, WM_WORKSPACE_DIRECTORY_DIRTY, 0, 0);
                    }
                } catch (...) {
                    // A watcher is advisory; a failed watcher must not bring
                    // down the host or call std::terminate on its thread.
                }
            });
            g_watchers.push_back(std::move(watcher));
        } catch (...) {
            watcher->active = false;
            if (watcher->directory != INVALID_HANDLE_VALUE) CancelIoEx(watcher->directory, nullptr);
            if (watcher->thread.joinable()) {
                try { watcher->thread.join(); } catch (...) { (void)watcher.release(); }
            }
            if (watcher && watcher->directory != INVALID_HANDLE_VALUE) CloseHandle(watcher->directory);
            throw;
        }
    }
}

static VOID CALLBACK networkChanged(PVOID, PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE) {
    if (row && row->Connected) PostMessageW(g_window, WM_WORKSPACE_NETWORK_RESTORED, 0, 0);
}

static void dispatchWorkspaceAction(const wchar_t* action) {
    if (!g_webview || !action || !*action) return;
    std::wstring script = L"window.dispatchEvent(new CustomEvent('workspace-action',{detail:'";
    script += action;
    script += L"'}))";
    g_webview->ExecuteScript(script.c_str(), nullptr);
}

static void pollGamepad() {
    if (parentInputModeEnabled()) {
        // Parent mode is semantic-action-only. The timer is not normally
        // created in this mode, but keeping this guard makes a late timer
        // message harmless and prevents a second XInput owner.
        g_previousGamepadButtons = 0;
        g_gamepadNavigationX = g_gamepadNavigationY = 0;
        g_gamepadShoulderPending = 0;
        g_gamepadShoulderComboHeld = false;
        return;
    }
    if (GetForegroundWindow() != g_window) {
        g_previousGamepadButtons = 0;
        g_gamepadNavigationX = g_gamepadNavigationY = 0;
        g_gamepadShoulderPending = 0;
        g_gamepadShoulderComboHeld = false;
        return;
    }
    XINPUT_STATE state{};
    if (XInputGetState(0, &state) != ERROR_SUCCESS) {
        g_previousGamepadButtons = 0;
        g_gamepadNavigationX = g_gamepadNavigationY = 0;
        g_gamepadShoulderPending = 0;
        g_gamepadShoulderComboHeld = false;
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const WORD pressed = static_cast<WORD>(state.Gamepad.wButtons & ~g_previousGamepadButtons);
    g_previousGamepadButtons = state.Gamepad.wButtons;
    // Match the main program's one-action-per-tick semantic ownership: a
    // directional press/repeat wins over face buttons, and a diagonal move
    // emits horizontal first. The child itself still consumes only one
    // semantic action per timer tick, so no double navigation can leak.
    const bool up = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0 ||
        state.Gamepad.sThumbLY >= kGamepadThumbThreshold;
    const bool down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0 ||
        state.Gamepad.sThumbLY <= -kGamepadThumbThreshold;
    const bool left = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0 ||
        state.Gamepad.sThumbLX <= -kGamepadThumbThreshold;
    const bool right = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0 ||
        state.Gamepad.sThumbLX >= kGamepadThumbThreshold;
    const int navX = right ? 1 : left ? -1 : 0;
    const int navY = down ? 1 : up ? -1 : 0;
    if (navX == 0 && navY == 0) {
        g_gamepadNavigationX = g_gamepadNavigationY = 0;
    } else if (navX != g_gamepadNavigationX || navY != g_gamepadNavigationY ||
               now - g_gamepadNavigationLastTick >= kGamepadNavigationRepeatMs) {
        if (navX != 0) dispatchWorkspaceAction(navX > 0 ? L"navigate-right" : L"navigate-left");
        else dispatchWorkspaceAction(navY > 0 ? L"navigate-down" : L"navigate-up");
        g_gamepadNavigationX = navX;
        g_gamepadNavigationY = navY;
        g_gamepadNavigationLastTick = now;
        return;
    }

    // LB+RB is reserved as a main-program summon-style combination. In the
    // standalone child it has no action, but it must not become two page
    // changes. A single shoulder is decided after the same 50 ms window as
    // the parent engine, then has a short post-combination lockout.
    const bool lb = (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    const bool rb = (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    if (lb && rb) {
        g_gamepadShoulderPending = 0;
        g_gamepadShoulderComboHeld = true;
    } else if (g_gamepadShoulderComboHeld) {
        if (!lb && !rb) {
            g_gamepadShoulderComboHeld = false;
            g_gamepadShoulderSuppressUntil = now + 450ULL;
        }
        g_gamepadShoulderPending = 0;
    } else if (now < g_gamepadShoulderSuppressUntil) {
        g_gamepadShoulderPending = 0;
    } else {
        if (g_gamepadShoulderPending != 0 &&
            now - g_gamepadShoulderPendingTick >= kGamepadShoulderDecisionMs) {
            dispatchWorkspaceAction(g_gamepadShoulderPending == XINPUT_GAMEPAD_LEFT_SHOULDER
                ? L"tab-previous" : L"tab-next");
            g_gamepadShoulderPending = 0;
        }
        if ((pressed & XINPUT_GAMEPAD_LEFT_SHOULDER) && !rb) {
            g_gamepadShoulderPending = XINPUT_GAMEPAD_LEFT_SHOULDER;
            g_gamepadShoulderPendingTick = now;
        }
        if ((pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) && !lb) {
            g_gamepadShoulderPending = XINPUT_GAMEPAD_RIGHT_SHOULDER;
            g_gamepadShoulderPendingTick = now;
        }
    }

    if (pressed & XINPUT_GAMEPAD_A) dispatchWorkspaceAction(L"accept");
    else if (pressed & XINPUT_GAMEPAD_B) dispatchWorkspaceAction(L"back");
    // X remains the standalone card-edit shortcut for compatibility with the
    // independent product UI. Parent mode translates the main program's X/Y
    // actions into semantic accept/edit before they reach this process.
    else if (pressed & (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y)) dispatchWorkspaceAction(L"edit");
}

static LRESULT CALLBACK titlebarButtonProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto action = static_cast<UINT_PTR>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT bounds{};
            GetClientRect(window, &bounds);
            // Match the restrained GPT-style chrome: neutral black buttons,
            // thin gray glyphs, and no permanent red close block.
            const COLORREF background = RGB(12, 12, 12);
            HBRUSH backgroundBrush = CreateSolidBrush(background);
            FillRect(dc, &bounds, backgroundBrush);
            DeleteObject(backgroundBrush);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(171, 171, 171));
            HFONT previous = g_titlebarButtonFont ? static_cast<HFONT>(SelectObject(dc, g_titlebarButtonFont)) : nullptr;
            const wchar_t* label = action == TITLEBAR_ACTION_MINIMIZE ? L"−" :
                action == TITLEBAR_ACTION_MAXIMIZE ? L"□" : L"×";
            DrawTextW(dc, label, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (previous) SelectObject(dc, previous);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_LBUTTONUP: {
            HWND titlebar = GetParent(window);
            HWND owner = titlebar ? GetParent(titlebar) : nullptr;
            if (owner) PostMessageW(owner, WM_WORKSPACE_TITLEBAR_ACTION, action, 0);
            return 0;
        }
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static LRESULT CALLBACK titlebarProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT bounds{};
            GetClientRect(window, &bounds);
            HBRUSH backgroundBrush = CreateSolidBrush(RGB(12, 12, 12));
            FillRect(dc, &bounds, backgroundBrush);
            DeleteObject(backgroundBrush);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(160, 160, 160));
            HFONT previous = g_titlebarFont ? static_cast<HFONT>(SelectObject(dc, g_titlebarFont)) : nullptr;
            RECT textBounds = bounds;
            textBounds.left += 18;
            textBounds.top += 4;
            textBounds.bottom = textBounds.top + 20;
            DrawTextW(dc, L"Steam自定义游戏库", -1, &textBounds, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            if (g_titlebarSubtitleFont) {
                SelectObject(dc, g_titlebarSubtitleFont);
                SetTextColor(dc, RGB(130, 138, 148));
                RECT subtitleBounds = bounds;
                subtitleBounds.left += 18;
                subtitleBounds.top += 24;
                subtitleBounds.bottom = subtitleBounds.top + 16;
                DrawTextW(dc, L"YeManCC附带程序", -1, &subtitleBounds, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            }
            if (previous) SelectObject(dc, previous);
            RECT divider = bounds;
            divider.top = divider.bottom - 1;
            HBRUSH dividerBrush = CreateSolidBrush(RGB(42, 42, 42));
            FillRect(dc, &divider, dividerBrush);
            DeleteObject(dividerBrush);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_LBUTTONDOWN:
            ReleaseCapture();
            SendMessageW(GetParent(window), WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        case WM_LBUTTONDBLCLK:
            SendMessageW(GetParent(window), WM_NCLBUTTONDBLCLK, HTCAPTION, 0);
            return 0;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static void registerTitlebarClasses(HINSTANCE instance) {
    WNDCLASSEXW titlebarClass{sizeof(titlebarClass)};
    titlebarClass.hInstance = instance;
    titlebarClass.lpfnWndProc = titlebarProcedure;
    titlebarClass.lpszClassName = L"YeManSteamLibraryTitlebar";
    titlebarClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    titlebarClass.style = CS_HREDRAW | CS_DBLCLKS;
    RegisterClassExW(&titlebarClass);

    WNDCLASSEXW buttonClass{sizeof(buttonClass)};
    buttonClass.hInstance = instance;
    buttonClass.lpfnWndProc = titlebarButtonProcedure;
    buttonClass.lpszClassName = L"YeManSteamLibraryTitlebarButton";
    buttonClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    buttonClass.style = CS_HREDRAW;
    RegisterClassExW(&buttonClass);
}

static void createTitlebar(HINSTANCE instance) {
    g_titlebarFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_titlebarSubtitleFont = CreateFontW(-10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_titlebarButtonFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");
    g_titlebar = CreateWindowExW(0, L"YeManSteamLibraryTitlebar", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, kWorkspaceTitlebarHeight,
        g_window, nullptr, instance, nullptr);
    auto createButton = [&](UINT_PTR action, const wchar_t* name) {
        HWND button = CreateWindowExW(0, L"YeManSteamLibraryTitlebarButton", name,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, kWorkspaceTitlebarHeight,
            g_titlebar, nullptr, instance, nullptr);
        SetWindowLongPtrW(button, GWLP_USERDATA, static_cast<LONG_PTR>(action));
        return button;
    };
    g_minimizeButton = createButton(TITLEBAR_ACTION_MINIMIZE, L"最小化");
    g_maximizeButton = createButton(TITLEBAR_ACTION_MAXIMIZE, L"全屏");
    g_closeButton = createButton(TITLEBAR_ACTION_CLOSE, L"关闭");
}

static void layoutTitlebar() {
    if (!g_window || !g_titlebar) return;
    RECT client{};
    GetClientRect(g_window, &client);
    const int width = client.right - client.left;
    const int buttonWidth = 46;
    MoveWindow(g_titlebar, 0, 0, width, kWorkspaceTitlebarHeight, TRUE);
    MoveWindow(g_closeButton, width - buttonWidth, 0, buttonWidth, kWorkspaceTitlebarHeight, TRUE);
    MoveWindow(g_maximizeButton, width - buttonWidth * 2, 0, buttonWidth, kWorkspaceTitlebarHeight, TRUE);
    MoveWindow(g_minimizeButton, width - buttonWidth * 3, 0, buttonWidth, kWorkspaceTitlebarHeight, TRUE);
}

static void applyWindowChrome() {
    if (!g_window) return;
    constexpr DWORD kImmersiveDarkMode = 20;
    constexpr DWORD kWindowCornerPreference = 33;
    constexpr DWORD kBorderColor = 34;
    constexpr DWORD kSystemBackdropType = 38;
    const BOOL dark = TRUE;
    const int corner = 2; // DWMWCP_ROUND
    const COLORREF border = RGB(44, 73, 91);
    const int backdrop = 2; // DWMSBT_MAINWINDOW / Mica
    DwmSetWindowAttribute(g_window, kImmersiveDarkMode, &dark, sizeof(dark));
    DwmSetWindowAttribute(g_window, kWindowCornerPreference, &corner, sizeof(corner));
    DwmSetWindowAttribute(g_window, kBorderColor, &border, sizeof(border));
    DwmSetWindowAttribute(g_window, kSystemBackdropType, &backdrop, sizeof(backdrop));
}

static void resizeWebView() {
    if (!g_window) return;
    layoutTitlebar();
    if (!g_controller) return;
    RECT bounds{};
    GetClientRect(g_window, &bounds);
    bounds.top = kWorkspaceTitlebarHeight;
    g_controller->put_Bounds(bounds);
}

// ================================================================
//  YeManCC-identical native startup spinner
// ================================================================

static void releaseSplashSurface() {
    if (g_splashDc) {
        if (g_splashBitmap) SelectObject(g_splashDc, g_splashBitmap);
        DeleteDC(g_splashDc);
    }
    if (g_splashBitmap) DeleteObject(g_splashBitmap);
    g_splashDc = nullptr;
    g_splashBitmap = nullptr;
    g_splashBits = nullptr;
    g_splashSurfaceW = 0;
    g_splashSurfaceH = 0;
}

static bool ensureSplashSurface(int width, int height) {
    if (g_splashDc && g_splashBitmap &&
        g_splashSurfaceW == width && g_splashSurfaceH == height) return true;
    releaseSplashSurface();

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    g_splashDc = CreateCompatibleDC(screen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_splashBitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS,
                                      &g_splashBits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!g_splashDc || !g_splashBitmap || !g_splashBits) {
        releaseSplashSurface();
        return false;
    }
    SelectObject(g_splashDc, g_splashBitmap);
    g_splashSurfaceW = width;
    g_splashSurfaceH = height;
    return true;
}

static void renderSplash() {
    if (!g_splash || !g_splashGdiplusToken) return;
    RECT client{};
    if (!GetClientRect(g_splash, &client)) return;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0 || !ensureSplashSurface(width, height)) return;

    using namespace Gdiplus;
    const int stride = width * 4;
    Bitmap bitmap(width, height, stride, PixelFormat32bppPARGB,
                  static_cast<BYTE*>(g_splashBits));
    Graphics graphics(&bitmap);
    graphics.SetCompositingMode(CompositingModeSourceCopy);
    graphics.Clear(Color(0, 0, 0, 0));
    graphics.SetCompositingMode(CompositingModeSourceOver);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    graphics.SetCompositingQuality(CompositingQualityHighQuality);

    const float scale = static_cast<float>(GetDpiForWindow(g_splash)) / 96.0f;
    const float radius = 24.0f * scale;
    const float stroke = 4.5f * scale;
    const float cx = static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(height) * 0.5f;
    RectF ring(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    const float angle = static_cast<float>(g_splashAngle) - 90.0f;
    const float arcWave = static_cast<float>(0.5 + 0.5 *
        std::sin(g_splashPhase * 0.82 + 0.7));
    const float sweep = 178.0f + 108.0f * arcWave;
    const BYTE alpha = static_cast<BYTE>(218.0f + 34.0f *
        static_cast<float>(0.5 + 0.5 * std::sin(g_splashPhase * 0.82 + 2.1)));
    Pen animatedPen(Color(alpha, 246, 248, 250), stroke);
    animatedPen.SetStartCap(LineCapRound);
    animatedPen.SetEndCap(LineCapRound);
    animatedPen.SetLineJoin(LineJoinRound);
    graphics.DrawArc(&animatedPen, ring, angle, sweep);

    POINT position{};
    RECT windowRect{};
    if (!GetWindowRect(g_splash, &windowRect)) return;
    position.x = windowRect.left;
    position.y = windowRect.top;
    SIZE size{width, height};
    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen = GetDC(nullptr);
    if (screen) {
        UpdateLayeredWindow(g_splash, screen, &position, &size, g_splashDc,
                            &source, 0, &blend, ULW_ALPHA);
        ReleaseDC(nullptr, screen);
    }
}

static void closeSplash();

static LRESULT CALLBACK splashProcedure(HWND window, UINT message,
                                        WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_splashAngle = 0.0;
        g_splashPhase = 0.0;
        QueryPerformanceFrequency(&g_splashQpcFrequency);
        QueryPerformanceCounter(&g_splashLastQpc);
        SetTimer(window, SPLASH_ANIMATION_TIMER_ID, SPLASH_TIMER_INTERVAL_MS, nullptr);
        SetTimer(window, SPLASH_FALLBACK_TIMER_ID, SPLASH_FALLBACK_TIMEOUT_MS, nullptr);
        return 0;
    case WM_TIMER:
        if (wParam == SPLASH_FALLBACK_TIMER_ID) {
            // The spinner is only a startup overlay. A missed WebView2
            // navigation callback must never keep the actual workspace hidden.
            if (g_window && IsWindow(g_window)) {
                ShowWindow(g_window, SW_SHOW);
                UpdateWindow(g_window);
            }
            closeSplash();
            return 0;
        }
        if (wParam == SPLASH_ANIMATION_TIMER_ID) {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            double elapsedSeconds = SPLASH_TIMER_INTERVAL_MS / 1000.0;
            if (g_splashLastQpc.QuadPart > 0 && g_splashQpcFrequency.QuadPart > 0 &&
                now.QuadPart >= g_splashLastQpc.QuadPart) {
                elapsedSeconds = static_cast<double>(now.QuadPart - g_splashLastQpc.QuadPart) /
                    static_cast<double>(g_splashQpcFrequency.QuadPart);
            }
            elapsedSeconds = (std::max)(0.001, (std::min)(elapsedSeconds, 0.1));
            g_splashLastQpc = now;
            g_splashPhase += elapsedSeconds * 2.0 * 3.14159265358979323846 * 0.62;
            const double speedFactor = 0.58 + 0.82 *
                (0.5 + 0.5 * std::sin(g_splashPhase));
            g_splashAngle = std::fmod(
                g_splashAngle + SPLASH_ROTATION_DEGREES_PER_SECOND * speedFactor * elapsedSeconds,
                360.0);
            renderSplash();
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        ValidateRect(window, nullptr);
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_DESTROY:
        KillTimer(window, SPLASH_ANIMATION_TIMER_ID);
        KillTimer(window, SPLASH_FALLBACK_TIMER_ID);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static void showSplash(HINSTANCE instance) {
    if (g_splash) return;
    WNDCLASSEXW splashClass{sizeof(splashClass)};
    splashClass.lpfnWndProc = splashProcedure;
    splashClass.hInstance = instance;
    splashClass.lpszClassName = L"YeManSteamLibrarySplash";
    splashClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&splashClass);

    HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(monitor, &monitorInfo);
    const UINT dpi = GetDpiForSystem();
    const int width = MulDiv(120, static_cast<int>(dpi), 96);
    const int height = MulDiv(120, static_cast<int>(dpi), 96);
    const int x = monitorInfo.rcWork.left +
        (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    const int y = monitorInfo.rcWork.top +
        (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;

    g_splash = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        splashClass.lpszClassName, L"", WS_POPUP, x, y, width, height,
        nullptr, nullptr, instance, nullptr);
    if (!g_splash) return;
    ShowWindow(g_splash, SW_SHOWNOACTIVATE);
    renderSplash();
}

static void closeSplash() {
    if (g_splash) {
        KillTimer(g_splash, SPLASH_ANIMATION_TIMER_ID);
        KillTimer(g_splash, SPLASH_FALLBACK_TIMER_ID);
        DestroyWindow(g_splash);
        g_splash = nullptr;
    }
    releaseSplashSurface();
}

static void shutdownSplashGraphics() {
    closeSplash();
    if (g_splashGdiplusToken) {
        Gdiplus::GdiplusShutdown(g_splashGdiplusToken);
        g_splashGdiplusToken = 0;
    }
}

static bool updateHealthHandshakeRequested() {
    return !g_launchOptions.updateHealthHandshakePath.empty() &&
        !g_launchOptions.updateHealthHandshakeToken.empty();
}

static bool probeUpdateHealthDataRoot(std::string& failure) {
    if (g_dataRoot.empty()) {
        failure = "data root is empty";
        return false;
    }
    std::error_code error;
    fs::create_directories(g_dataRoot / L"state", error);
    if (error) {
        failure = "data root cannot be created: " + error.message();
        return false;
    }
    const auto probe = g_dataRoot / L"state" /
        (L".update-health-" + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    {
        std::ofstream output(custom_steam_library::ioPath(probe), std::ios::binary | std::ios::trunc);
        if (!output) {
            failure = "data root is not writable";
            return false;
        }
        output << "yemancc-custom-steam-library-health\n";
        output.flush();
        if (!output.good()) {
            error.clear();
            fs::remove(probe, error);
            failure = "data root write probe failed";
            return false;
        }
    }
    error.clear();
    fs::remove(probe, error);
    if (error) {
        failure = "data root write probe cleanup failed: " + error.message();
        return false;
    }
    return true;
}

static bool writeUpdateHealthMarker(const char* phase, const std::string& failure = {}) noexcept {
    if (!updateHealthHandshakeRequested()) return false;
    try {
        const auto path = fs::absolute(fs::path(g_launchOptions.updateHealthHandshakePath));
        const auto parent = path.parent_path();
        if (parent.empty()) return false;
        std::error_code error;
        fs::create_directories(parent, error);
        if (error) return false;

        const auto manifest = readJson(g_labRoot / L"package-manifest.json", json::object());
        json marker = {
            {"schemaVersion", 1},
            {"phase", phase},
            {"packageId", jsonStringOr(manifest, "packageId")},
            {"packageType", jsonStringOr(manifest, "packageType")},
            {"packageVersion", jsonStringOr(manifest, "packageVersion")},
            {"expectedPackageVersion", toUtf8(g_launchOptions.updateHealthPackageVersion)},
            {"token", toUtf8(g_launchOptions.updateHealthHandshakeToken)},
            {"pid", GetCurrentProcessId()},
            {"inputOwner", toUtf8(g_launchOptions.inputOwner)},
            {"parentPid", g_launchOptions.parentPid},
            {"healthOnly", g_launchOptions.updateHealthOnly},
            {"protocol", 1},
            {"window", g_window != nullptr && IsWindow(g_window)},
            {"worker", fs::is_regular_file(g_worker)},
            {"workspaceUi", fs::is_directory(custom_steam_library::workspaceUiPath(g_labRoot))},
            {"webview2", g_controller && g_webview},
            {"dataRootWritable", g_updateHealthDataRootWritable},
            {"dataRoot", toUtf8(g_dataRoot.wstring())}
        };
        if (!failure.empty()) marker["error"] = failure;

        const auto temporary = parent /
            (path.filename().wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId()));
        {
            std::ofstream output(custom_steam_library::ioPath(temporary), std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output << marker.dump() << '\n';
            output.flush();
            if (!output.good()) return false;
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str());
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

static void reportUpdateHealthFailure(const wchar_t* message, const char* kind = "update-health-failed");
static void initializeWebView();
static void navigateWorkspaceDocument();
static void recoverWorkspaceWebView(COREWEBVIEW2_PROCESS_FAILED_KIND kind, int exitCode);

static void setHostExitReason(std::string_view kind, std::string_view detail = {}) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_hostExitReasonMutex);
        if (g_hostExitKind != "wm-close") return;
        g_hostExitKind.assign(kind.data(), kind.size());
        g_hostExitDetail.assign(detail.data(), detail.size());
    } catch (...) {
    }
}

struct WorkspaceWebViewFailure {
    COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
    int exitCode = 0;
};

static bool isWorkspaceUiSource(const std::wstring& source) {
    if (source.empty()) return false;
    if (_wcsicmp(source.c_str(), L"about:blank") == 0) return false;
    auto startsWith = [&](const wchar_t* prefix) {
        const size_t length = wcslen(prefix);
        return source.size() >= length && _wcsnicmp(source.c_str(), prefix, length) == 0;
    };
    return startsWith(L"https://steam-library.localhost/") ||
        startsWith(L"https://steam-library.local/");
}

static void navigateWorkspaceDocument() {
    if (!g_webview || g_shuttingDown.load()) return;
    g_webview->Navigate(kWorkspaceUiUrl);
}

static void handleWorkspaceNavigationCompleted(
    ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* arguments) {
    if (!arguments) return;
    UINT64 navigationId = 0;
    arguments->get_NavigationId(&navigationId);
    const UINT64 expected = g_webviewExpectedNavigationId.load();
    if (navigationId == 0 || navigationId != expected) return;

    BOOL success = FALSE;
    COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
    arguments->get_IsSuccess(&success);
    arguments->get_WebErrorStatus(&status);

    LPWSTR sourceRaw = nullptr;
    std::wstring source;
    if (sender && SUCCEEDED(sender->get_Source(&sourceRaw)) && sourceRaw) {
        source = sourceRaw;
        CoTaskMemFree(sourceRaw);
    }
    if (!isWorkspaceUiSource(source)) return;

    if (!success) {
        if (status == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) return;
        if (g_webviewMainDocumentReady.load()) return;
        const int retries = g_webviewNavigationRetries.fetch_add(1) + 1;
        if (retries <= kWebViewMaxNavigationRetries) {
            navigateWorkspaceDocument();
            return;
        }
        setHostExitReason("navigation-failed",
            std::string("webErrorStatus=") + std::to_string(static_cast<int>(status)));
        reportUpdateHealthFailure(L"Custom Steam Library workspace-ui 导航失败。", "navigation-failed");
        return;
    }

    g_webviewMainDocumentReady = true;
    g_webviewNavigationRetries = 0;
    g_webviewProcessRecoveries = 0;
    if (g_splash) {
        ShowWindow(g_window, SW_SHOW);
        UpdateWindow(g_window);
        closeSplash();
    }
    if (!g_updateHealthOnly) focusWorkspaceWindow();
    if (g_updateHealthOnly) {
        if (writeUpdateHealthMarker("ready")) {
            setHostExitReason("update-health-ready-exit");
            SetTimer(g_window, UPDATE_HEALTH_EXIT_TIMER, 5000, nullptr);
        }
    }
}

static bool bindWorkspaceWebView(ICoreWebView2Controller* controller) {
    if (!controller) return false;
    g_controller = controller;
    g_webview.Reset();
    controller->get_CoreWebView2(&g_webview);
    if (!g_webview) return false;
    if (parentInputModeEnabled()) {
        g_webview->AddScriptToExecuteOnDocumentCreated(
            L"window.__customSteamLibraryParentMode=true;", nullptr);
    }
    resizeWebView();
    ComPtr<ICoreWebView2Settings> settings;
    g_webview->get_Settings(&settings);
    if (settings) {
        settings->put_IsScriptEnabled(TRUE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDevToolsEnabled(FALSE);
    }

    ComPtr<ICoreWebView2_3> webview3;
    if (FAILED(g_webview.As(&webview3)) || !webview3) return false;
    const auto uiPath = custom_steam_library::workspaceUiPath(g_labRoot);
    webview3->SetVirtualHostNameToFolderMapping(
        kWorkspaceUiHost, uiPath.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    webview3->SetVirtualHostNameToFolderMapping(
        kWorkspaceUiHostLegacy, uiPath.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    webview3->SetVirtualHostNameToFolderMapping(
        kWorkspaceDataHost, g_dataRoot.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    webview3->SetVirtualHostNameToFolderMapping(
        kWorkspaceDataHostLegacy, g_dataRoot.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);

    EventRegistrationToken messageToken{};
    g_webview->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR raw = nullptr;
                if (FAILED(args->get_WebMessageAsJson(&raw)) || !raw) return S_OK;
                const std::wstring text(raw);
                CoTaskMemFree(raw);
                try {
                    const auto request = json::parse(toUtf8(text));
                    const uint64_t id = request.value("id", static_cast<uint64_t>(0));
                    const auto command = request.value("command", std::string{});
                    const auto arguments = request.value("arguments", json::object());
                    beginCommand(id, command, arguments);
                } catch (const std::exception& error) {
                    postWebMessageNoThrow({{"kind", "event"}, {"event", "host-error"}, {"error", error.what()}});
                }
                return S_OK;
            }).Get(), &messageToken);

    EventRegistrationToken startingToken{};
    g_webview->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                if (!args) return S_OK;
                LPWSTR uriRaw = nullptr;
                UINT64 navigationId = 0;
                args->get_Uri(&uriRaw);
                args->get_NavigationId(&navigationId);
                const std::wstring uri = uriRaw ? uriRaw : L"";
                if (uriRaw) CoTaskMemFree(uriRaw);
                if (!isWorkspaceUiSource(uri)) return S_OK;
                g_webviewExpectedNavigationId = navigationId;
                return S_OK;
            }).Get(), &startingToken);

    EventRegistrationToken completedToken{};
    g_webview->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* arguments) -> HRESULT {
                handleWorkspaceNavigationCompleted(sender, arguments);
                return S_OK;
            }).Get(), &completedToken);

    g_webview->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                if (!args || g_shuttingDown.load()) return S_OK;
                auto* failure = new (std::nothrow) WorkspaceWebViewFailure();
                if (!failure) return S_OK;
                args->get_ProcessFailedKind(&failure->kind);
                ComPtr<ICoreWebView2ProcessFailedEventArgs2> args2;
                if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&args2))) && args2) {
                    args2->get_ExitCode(&failure->exitCode);
                }
                if (!g_window || !PostMessageW(g_window, WM_WEBVIEW_PROCESS_FAILED, 0,
                        reinterpret_cast<LPARAM>(failure))) {
                    delete failure;
                }
                return S_OK;
            }).Get(), nullptr);
    return true;
}

static HRESULT onWorkspaceControllerCreated(HRESULT controllerResult, ICoreWebView2Controller* controller) {
    if (FAILED(controllerResult) || !controller) {
        g_webviewInitializeBusy = false;
        reportUpdateHealthFailure(L"WebView2 窗口初始化失败，无法打开 Custom Steam Library。", "webview-controller-failed");
        return controllerResult;
    }
    if (!bindWorkspaceWebView(controller)) {
        g_webviewInitializeBusy = false;
        reportUpdateHealthFailure(L"WebView2 窗口初始化失败，无法打开 Custom Steam Library。", "webview-controller-failed");
        return E_FAIL;
    }
    g_webviewInitializeBusy = false;
    navigateWorkspaceDocument();
    return S_OK;
}

static bool workspaceWebViewFailureNeedsRecovery(COREWEBVIEW2_PROCESS_FAILED_KIND kind) {
    // WebView2 recovers GPU/utility/frame subprocesses itself. Reloading the
    // whole editor for them loses draft state and can exhaust the retry limit.
    return kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED ||
        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE;
}

static void recoverWorkspaceWebView(COREWEBVIEW2_PROCESS_FAILED_KIND kind, int exitCode) {
    if (g_shuttingDown.load() || !g_window || !IsWindow(g_window)) return;
    const auto detail = std::string("kind=") + std::to_string(static_cast<int>(kind)) + " exitCode=" + std::to_string(exitCode);
    if (!workspaceWebViewFailureNeedsRecovery(kind)) {
        writeHostCrashRecord("webview-resource-process-warning", static_cast<DWORD>(exitCode), detail);
        return;
    }
    writeHostCrashRecord("webview-main-process-recovery", static_cast<DWORD>(exitCode), detail);
    const int attempt = g_webviewProcessRecoveries.fetch_add(1) + 1;
    if (attempt > kWebViewMaxProcessRecoveries) {
        setHostExitReason("webview-process-failed",
            std::string("kind=") + std::to_string(static_cast<int>(kind)) +
            " exitCode=" + std::to_string(exitCode) + " attempts=" + std::to_string(attempt));
        reportUpdateHealthFailure(L"WebView2 进程反复失败，无法打开 Custom Steam Library。", "webview-process-failed");
        return;
    }
    g_webviewMainDocumentReady = false;
    g_webviewExpectedNavigationId = 0;
    g_webviewNavigationRetries = 0;
    if (kind != COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED && g_webview) {
        if (SUCCEEDED(g_webview->Reload())) return;
    }
    if (g_controller) g_controller->Close();
    g_controller.Reset();
    g_webview.Reset();
    if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) g_webviewEnvironment.Reset();
    g_webviewInitializeBusy = false;
    initializeWebView();
}

static void reportUpdateHealthFailure(const wchar_t* message, const char* kind) {
    const auto failure = toUtf8(message ? std::wstring(message) : std::wstring(L"startup failed"));
    setHostExitReason(kind && *kind ? kind : "update-health-failed", failure);
    writeUpdateHealthMarker("failed", failure);
    if (!g_launchOptions.updateHealthOnly) {
        MessageBoxW(g_window, message, L"Steam自定义游戏库", MB_ICONERROR | MB_OK);
    }
    if (g_window && IsWindow(g_window)) PostMessageW(g_window, WM_CLOSE, 0, 0);
}

static void initializeWebView() {
    const auto userData = custom_steam_library::webviewUserDataRoot(g_dataRoot);
    std::error_code error;
    if (!fs::is_directory(custom_steam_library::workspaceUiPath(g_labRoot), error)) {
        reportUpdateHealthFailure(L"Custom Steam Library 界面文件夹缺失：workspace-ui", "workspace-ui-missing");
        return;
    }
    error.clear();
    fs::create_directories(userData, error);
    if (error) {
        reportUpdateHealthFailure(L"无法创建 WebView2 缓存目录，请检查数据目录权限。", "webview-userdata-failed");
        return;
    }
    if (g_webviewInitializeBusy.exchange(true)) return;

    if (g_webviewEnvironment) {
        const HRESULT controllerCreation = g_webviewEnvironment->CreateCoreWebView2Controller(
            g_window,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                    return onWorkspaceControllerCreated(controllerResult, controller);
                }).Get());
        if (FAILED(controllerCreation)) {
            g_webviewInitializeBusy = false;
            reportUpdateHealthFailure(L"WebView2 窗口初始化失败，无法打开 Custom Steam Library。", "webview-controller-failed");
        }
        return;
    }

    const HRESULT environmentCreation = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT environmentResult, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(environmentResult) || !environment) {
                    g_webviewInitializeBusy = false;
                    reportUpdateHealthFailure(L"未检测到可用的 WebView2 Runtime，无法打开 Custom Steam Library。", "webview-runtime-missing");
                    return environmentResult;
                }
                g_webviewEnvironment = environment;
                return environment->CreateCoreWebView2Controller(
                    g_window,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            return onWorkspaceControllerCreated(controllerResult, controller);
                        }).Get());
            }).Get());
    if (FAILED(environmentCreation)) {
        g_webviewInitializeBusy = false;
        reportUpdateHealthFailure(L"WebView2 环境初始化失败，无法打开 Custom Steam Library。", "webview-environment-failed");
    }
}

static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COPYDATA: {
            if (!parentInputModeEnabled()) return FALSE;
            DWORD senderPid = 0;
            GetWindowThreadProcessId(reinterpret_cast<HWND>(wParam), &senderPid);
            if (senderPid != g_launchOptions.parentPid) return FALSE;
            const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
            if (!data || data->dwData != 0x594D4343 || !data->lpData ||
                data->cbData < sizeof(wchar_t) || data->cbData % sizeof(wchar_t) != 0) return FALSE;
            const auto count = data->cbData / sizeof(wchar_t);
            if (count > 64) return FALSE;
            const auto* raw = static_cast<const wchar_t*>(data->lpData);
            if (raw[count - 1] != L'\0') return FALSE;
            const std::wstring action(raw, count - 1);
            if (!isParentSemanticAction(action)) return FALSE;
            dispatchWorkspaceAction(action.c_str());
            return TRUE;
        }
        case WM_WORKSPACE_TITLEBAR_ACTION:
            if (wParam == TITLEBAR_ACTION_MINIMIZE) ShowWindow(window, SW_MINIMIZE);
            else if (wParam == TITLEBAR_ACTION_MAXIMIZE) ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
            else if (wParam == TITLEBAR_ACTION_CLOSE) PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        case WM_SIZE:
            resizeWebView();
            return 0;
        case WM_TIMER:
            if (wParam == DIRECTORY_DEBOUNCE_TIMER) {
                KillTimer(window, DIRECTORY_DEBOUNCE_TIMER);
                if (!kAutomaticLibraryMaintenanceEnabled) {
                    g_directoryDirty = false;
                    return 0;
                }
                if (g_taskRunning) {
                    SetTimer(window, DIRECTORY_DEBOUNCE_TIMER, 2000, nullptr);
                } else if (g_directoryDirty.exchange(false)) {
                    beginCommand(0, "scan");
                }
                return 0;
            }
            if (wParam == GAMEPAD_TIMER) {
                pollGamepad();
                return 0;
            }
            if (wParam == UPDATE_HEALTH_EXIT_TIMER) {
                KillTimer(window, UPDATE_HEALTH_EXIT_TIMER);
                if (g_updateHealthOnly) PostMessageW(window, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        case WM_WORKSPACE_RESULT: {
            std::unique_ptr<AsyncResult> result(reinterpret_cast<AsyncResult*>(lParam));
            try {
                if (result && result->refreshWatchers) refreshDirectoryWatchers();
                if (result) postWebMessageNoThrow(result->message);
            } catch (const std::exception& error) {
                postWebMessageNoThrow({{"kind", "event"}, {"event", "host-error"}, {"error", error.what()}});
            } catch (...) {
                postWebMessageNoThrow({{"kind", "event"}, {"event", "host-error"}, {"error", "宿主刷新目录监视器时发生未知异常"}});
            }
            if (g_networkWakePending.exchange(false)) {
                // A second network-restored signal is coalesced while the
                // bounded retry worker is running.  Start the next attempt
                // directly so it never re-enters the foreground command lane.
                startNetworkWakeTask(0);
            } else if (g_directoryDirty.load()) {
                SetTimer(window, DIRECTORY_DEBOUNCE_TIMER, 2000, nullptr);
            }
            return 0;
        }
        case WM_WORKSPACE_DIRECTORY_DIRTY:
            if (!kAutomaticLibraryMaintenanceEnabled) return 0;
            g_directoryDirty = true;
            SetTimer(window, DIRECTORY_DEBOUNCE_TIMER, 2000, nullptr);
            postWebMessageNoThrow({{"kind", "event"}, {"event", "directory-change-detected"}});
            return 0;
        case WM_WORKSPACE_NETWORK_RESTORED:
            if (!kAutomaticLibraryMaintenanceEnabled) return 0;
            if (g_networkWakeRunning) g_networkWakePending = true;
            else startNetworkWakeTask(0);
            postWebMessageNoThrow({{"kind", "event"}, {"event", "network-restored"}});
            return 0;
        case WM_WEBVIEW_PROCESS_FAILED: {
            std::unique_ptr<WorkspaceWebViewFailure> failure(
                reinterpret_cast<WorkspaceWebViewFailure*>(lParam));
            if (failure) recoverWorkspaceWebView(failure->kind, failure->exitCode);
            return 0;
        }
        case WM_CLOSE:
            stopWorkspaceResultPosting(window);
            writeHostExitRecord();
            g_networkWakePending = false;
            requestTaskCancellation();
            restoreParentWindow();
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            stopWorkspaceResultPosting(window);
            requestTaskCancellation();
            g_window = nullptr;
            closeSplash();
            closeWorkerProcessJob();
            RemovePropW(window, kInputOwnerProperty);
            RemovePropW(window, kParentPidProperty);
            KillTimer(window, DIRECTORY_DEBOUNCE_TIMER);
            KillTimer(window, GAMEPAD_TIMER);
            KillTimer(window, UPDATE_HEALTH_EXIT_TIMER);
            stopDirectoryWatchers();
            if (g_networkNotification) {
                CancelMibChangeNotify2(g_networkNotification);
                g_networkNotification = nullptr;
            }
            if (g_controller) g_controller->Close();
            g_webview.Reset();
            g_controller.Reset();
            g_webviewEnvironment.Reset();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_launchOptions = parseLaunchOptions();
    g_updateHealthOnly = g_launchOptions.updateHealthOnly;
    if (g_launchOptions.updateHealthOnly && !updateHealthHandshakeRequested()) {
        reportUpdateHealthFailure(L"独立健康启动缺少握手路径或一次性令牌。", "update-health-failed");
        return 8;
    }
    if (g_launchOptions.inputOwner != L"host" && !parentInputModeEnabled()) {
        reportUpdateHealthFailure(L"手柄输入所有者参数无效。独立运行请使用 --input-owner=host；YeManCC 接入必须同时提供协议版本和父进程 PID。", "launch-options-invalid");
        return 6;
    }
    HANDLE singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\YeManSteamLibraryWorkspace");
    if (!singleInstance) {
        reportUpdateHealthFailure(L"无法创建 Custom Steam Library 单实例互斥体。", "single-instance-failed");
        return 2;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"YeManSteamLibraryWorkspace", nullptr);
        const bool compatible = existing && (parentInputModeEnabled()
            ? (windowUsesParentInput(existing) && windowParentPid(existing) == g_launchOptions.parentPid)
            : !windowUsesParentInput(existing));
        if (compatible && !g_updateHealthOnly) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(singleInstance);
        // A second invocation with a different input owner must not activate
        // or attach to the existing process.  The caller can report the
        // conflict and keep its own input ownership unchanged.
        if (g_updateHealthOnly) {
            reportUpdateHealthFailure(compatible
                ? L"Custom Steam Library 健康启动被现有实例占用。"
                : L"Custom Steam Library 已有不兼容的实例正在运行。",
                "single-instance-conflict");
        }
        return compatible ? 0 : 7;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t executablePath[32768]{};
    GetModuleFileNameW(nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
    g_labRoot = custom_steam_library::applicationRootFromExecutable(fs::path(executablePath));
    g_worker = custom_steam_library::workerPath(g_labRoot);
    g_dataRoot = custom_steam_library::defaultDataRoot(g_labRoot);
    // Install process-wide diagnostics only after the portable package root
    // is known, so remote-machine crashes leave a collectable record beside
    // the scrape history even when Windows cannot create a minidump.
    std::set_terminate(hostTerminateHandler);
    SetUnhandledExceptionFilter(hostUnhandledExceptionFilter);
    initializeWorkerProcessJob();
    const auto packageManifest = readJson(g_labRoot / L"package-manifest.json", json::object());
    const auto packageId = jsonStringOr(packageManifest, "packageId");
    const auto packageType = jsonStringOr(packageManifest, "packageType");
    const auto packageVersion = jsonStringOr(packageManifest, "packageVersion");
    if (g_updateHealthOnly && (packageId != "custom-steam-library" || packageType != "green-child" || packageVersion.empty())) {
        reportUpdateHealthFailure(L"Custom Steam Library package-manifest.json 身份或版本无效。", "package-manifest-invalid");
        CloseHandle(singleInstance);
        CoUninitialize();
        return 9;
    }
    if (g_updateHealthOnly && !g_launchOptions.updateHealthPackageVersion.empty() &&
        packageVersion != toUtf8(g_launchOptions.updateHealthPackageVersion)) {
        reportUpdateHealthFailure(L"Custom Steam Library 子包版本与升级清单不一致。", "package-version-mismatch");
        CloseHandle(singleInstance);
        CoUninitialize();
        return 10;
    }
    if (!fs::is_regular_file(g_worker)) {
        reportUpdateHealthFailure(L"Custom Steam Library Worker 缺失：SteamArtworkLab.exe", "worker-missing");
        CloseHandle(singleInstance);
        CoUninitialize();
        return 4;
    }
    if (!g_updateHealthOnly) {
        // A cancelled/timed-out worker may have been terminated before its
        // destructor could roll back.  Recover the durable Steam transaction
        // journal before opening the UI; failure is retained for a later pass
        // and must not prevent the workspace from starting.
        try {
            const auto cleanup = runWorkerBounded(
                {L"--cleanup-interrupted-transactions"}, 10000);
            if (cleanup.exitCode != 0) {
                writeHostCrashRecord("transaction-cleanup-warning", cleanup.exitCode, cleanup.output);
            }
        } catch (const std::exception& error) {
            writeHostCrashRecord("transaction-cleanup-warning", 0, error.what());
        } catch (...) {
            writeHostCrashRecord("transaction-cleanup-warning", 0, "事务清理启动失败");
        }
    }
    if (!fs::is_directory(custom_steam_library::workspaceUiPath(g_labRoot))) {
        reportUpdateHealthFailure(L"Custom Steam Library 界面文件夹缺失：workspace-ui", "workspace-ui-missing");
        CloseHandle(singleInstance);
        CoUninitialize();
        return 5;
    }
    if (g_updateHealthOnly) {
        std::string dataRootFailure;
        if (!probeUpdateHealthDataRoot(dataRootFailure)) {
            reportUpdateHealthFailure(toWide(dataRootFailure).c_str(), "data-root-failed");
            CloseHandle(singleInstance);
            CoUninitialize();
            return 11;
        }
        g_updateHealthDataRootWritable = true;
    }

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpszClassName = L"YeManSteamLibraryWorkspace";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = CreateSolidBrush(RGB(11, 14, 19));
    RegisterClassExW(&windowClass);

    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
    const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
    const int windowMargin = 50;
    const int width = (std::max)(1100, workWidth - windowMargin * 2);
    const int height = (std::max)(620, workHeight - windowMargin * 2);
    const int x = monitor.rcWork.left + (workWidth - width) / 2;
    const int y = monitor.rcWork.top + (workHeight - height) / 2;
    g_window = CreateWindowExW(
        WS_EX_APPWINDOW, windowClass.lpszClassName, L"Steam自定义游戏库 - YeManCC附带程序",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
        x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!g_window) {
        reportUpdateHealthFailure(L"无法创建 Custom Steam Library 主窗口。", "window-create-failed");
        CloseHandle(singleInstance);
        CoUninitialize();
        return 3;
    }
    SetPropW(g_window, kInputOwnerProperty,
        reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(parentInputModeEnabled()
            ? kParentInputOwnerMarker : kHostInputOwnerMarker)));
    SetPropW(g_window, kParentPidProperty,
        reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(parentInputModeEnabled()
            ? g_launchOptions.parentPid : 0)));
    registerTitlebarClasses(instance);
    createTitlebar(instance);
    applyWindowChrome();
    if (!g_updateHealthOnly) {
        Gdiplus::GdiplusStartupInput splashGdiplusInput;
        if (Gdiplus::GdiplusStartup(&g_splashGdiplusToken,
                                    &splashGdiplusInput, nullptr) != Gdiplus::Ok) {
            g_splashGdiplusToken = 0;
        }
        if (g_splashGdiplusToken) showSplash(instance);
    }
    // Show the real host immediately. The spinner is a non-activating overlay
    // above it, so a delayed/missed WebView2 event cannot leave an invisible
    // window or an empty desktop behind the startup animation.
    ShowWindow(g_window, g_updateHealthOnly ? SW_HIDE : SW_SHOW);
    if (!g_updateHealthOnly) UpdateWindow(g_window);
    if (!g_updateHealthOnly) focusWorkspaceWindow();
    if (g_splash) {
        SetWindowPos(g_splash, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    // Parent mode receives semantic WM_COPYDATA actions and must not start a
    // second XInput poller. Standalone mode keeps the existing host-owned path.
    if (g_launchOptions.inputOwner == L"host" && !g_updateHealthOnly) SetTimer(g_window, GAMEPAD_TIMER, 70, nullptr);
    if (!g_updateHealthOnly) NotifyIpInterfaceChange(AF_UNSPEC, networkChanged, nullptr, FALSE, &g_networkNotification);
    initializeWebView();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    g_shuttingDown = true;
    requestTaskCancellation();
    closeWorkerProcessJob();
    joinHostBackgroundTasks();
    shutdownSplashGraphics();
    CloseHandle(singleInstance);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}







