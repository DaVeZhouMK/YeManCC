#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <powrprof.h>
#include <powerbase.h>   // PowerRegisterSuspendResumeNotification / PowerUnregisterSuspendResumeNotification
#include <tlhelp32.h>    // CreateToolhelp32Snapshot / Process32*W（FanHost 进程枚举）
#include <iphlpapi.h>    // GetExtendedTcpTable（127.0.0.1:8765 所有权核对）
#pragma comment(lib, "iphlpapi.lib")
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "recovery_ui_progress.h"

// GP-XBOX-2（2026-09-28 裁决 §8）：--selftest 期间的事件名捕获；生产路径恒为 nullptr。
std::vector<std::string>* g_selftestEventCapture = nullptr;

namespace {
constexpr UINT WM_RECOVERY_WEBVIEW_PING = WM_USER + 20;
constexpr DWORD kStartupGraceMs = 30000;
constexpr DWORD kRoundIntervalMs = 3000;
// Two sequential channel checks must still fit the requested 1-second
// confirmation window: 3 * (100 + 100) ms + 2 * 250 ms = 1 second.
constexpr DWORD kSampleTimeoutMs = 100;
constexpr DWORD kSampleSpacingMs = 250;
// GP-XBOX-2（裁决 §5）：判死顺序常量。UI 探针失败不再单独构成判死条件。
constexpr DWORD kHeartbeatStaleMs = 8000;     // native 心跳失效阈值（发布 1 Hz）
constexpr DWORD kDeferWindowMs = 12000;       // lifecycleBusy 有界等待窗（4×3 s）
constexpr DWORD kTerminationWaitMs = 10000;   // 终止后确认 WAIT_OBJECT_0 的上限
constexpr DWORD kOwnerReleaseWaitMs = 20000;  // FanHost/8765 所有权释放上限
constexpr DWORD kOwnerReleasePollMs = 500;
constexpr unsigned int kSnapshotMagic = 0x534D4359u;   // 'YMCS'
constexpr unsigned int kSnapshotSchemaVersion = 1;

struct Options { DWORD pid = 0; HWND hwnd = nullptr; std::wstring eventName; std::wstring exitEventName; std::wstring targetPath; };

std::wstring dataDir() {
    PWSTR raw = nullptr;
    std::wstring root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw) {
        root = std::wstring(raw) + L"\\YeManCC";
        CoTaskMemFree(raw);
    }
    return root;
}

std::wstring logPath() {
    const std::wstring root = dataDir();
    if (root.empty()) return {};
    CreateDirectoryW(root.c_str(), nullptr);
    return root + L"\\recovery-service.log";
}

std::string timestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char value[40]{};
    snprintf(value, sizeof(value), "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return value;
}

void writeLog(const std::wstring& path, const char* event, const std::string& fields = {}) {
    if (g_selftestEventCapture) g_selftestEventCapture->push_back(event ? event : "unknown");
    if (path.empty()) return;
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) return;
    out << "{\"time\":\"" << timestamp() << "\",\"event\":\""
        << (event ? event : "unknown") << "\",\"pid\":" << GetCurrentProcessId();
    if (!fields.empty()) out << ',' << fields;
    out << "}\n";
}

std::wstring quoteArg(const std::wstring& value) {
    std::wstring out = L"\"";
    unsigned int slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') {
            out.append(slashes * 2 + 1, L'\\'); out.push_back(L'\"'); slashes = 0; continue;
        }
        out.append(slashes, L'\\'); slashes = 0; out.push_back(ch);
    }
    out.append(slashes * 2, L'\\'); out.push_back(L'\"');
    return out;
}

bool parseOptions(Options& options) {
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--pid") == 0 && i + 1 < argc) options.pid = wcstoul(argv[++i], nullptr, 10);
        else if (_wcsicmp(argv[i], L"--hwnd") == 0 && i + 1 < argc) options.hwnd = reinterpret_cast<HWND>(wcstoull(argv[++i], nullptr, 10));
        else if (_wcsicmp(argv[i], L"--event") == 0 && i + 1 < argc) options.eventName = argv[++i];
        else if (_wcsicmp(argv[i], L"--exit-event") == 0 && i + 1 < argc) options.exitEventName = argv[++i];
        else if (_wcsicmp(argv[i], L"--target") == 0 && i + 1 < argc) options.targetPath = argv[++i];
    }
    LocalFree(argv);
    return options.pid != 0 && !options.eventName.empty() && !options.targetPath.empty();
}

HWND findMainWindow(DWORD pid) {
    struct State { DWORD pid; HWND found; } state{pid, nullptr};
    EnumWindows([](HWND hwnd, LPARAM raw) -> BOOL {
        auto& state = *reinterpret_cast<State*>(raw); DWORD owner = 0;
        GetWindowThreadProcessId(hwnd, &owner);
        if (owner != state.pid || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
        state.found = hwnd; return FALSE;
    }, reinterpret_cast<LPARAM>(&state));
    return state.found;
}

bool processAlive(HANDLE process) {
    DWORD code = 0; return GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
}

bool hwndResponds(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false; DWORD_PTR result = 0;
    return SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                               kSampleTimeoutMs, &result) != 0;
}

bool webviewResponds(HWND hwnd, HANDLE pongEvent, DWORD serial) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    ResetEvent(pongEvent);
    if (!PostMessageW(hwnd, WM_RECOVERY_WEBVIEW_PING, serial, 0)) return false;
    return WaitForSingleObject(pongEvent, kSampleTimeoutMs) == WAIT_OBJECT_0;
}

bool firstProbe(const Options& options, HANDLE pongEvent, DWORD serial) {
    HWND hwnd = options.hwnd;
    DWORD owner = 0;
    if (!hwnd || !IsWindow(hwnd) ||
        (GetWindowThreadProcessId(hwnd, &owner), owner != options.pid))
        hwnd = findMainWindow(options.pid);
    return hwndResponds(hwnd) || webviewResponds(hwnd, pongEvent, serial);
}

bool confirmationProbe(const Options& options, HANDLE pongEvent, DWORD& serial) {
    for (int sample = 0; sample < 3; ++sample) {
        HWND hwnd = options.hwnd;
        DWORD owner = 0;
        if (!hwnd || !IsWindow(hwnd) ||
            (GetWindowThreadProcessId(hwnd, &owner), owner != options.pid))
            hwnd = findMainWindow(options.pid);
        const bool hwndOk = hwndResponds(hwnd);
        const bool webviewOk = !hwndOk && webviewResponds(hwnd, pongEvent, serial++);
        if (hwndOk || webviewOk) return true;
        if (sample < 2) Sleep(kSampleSpacingMs);
    }
    return false;
}

// GP-XBOX-2（§5.7）：终止与启动**拆开**——旧 PID 未经 WAIT_OBJECT_0 确认前不得
// CreateProcess；且必须在 FanHost/8765 所有权释放等待之后才允许启动（§5.8）。
bool terminateTargetProcess(HANDLE process, const std::wstring& log) {
    if (!TerminateProcess(process, 0xE001)) {
        writeLog(log, "restart-result", "\"ok\":false,\"reason\":\"target-terminate-failed\",\"error\":" +
            std::to_string(GetLastError()));
        return false;
    }
    return true;
}

bool targetTerminationSettled(HANDLE process) {
    return WaitForSingleObject(process, kTerminationWaitMs) == WAIT_OBJECT_0;
}

bool spawnTargetProcess(const Options& options, const std::wstring& log) {
    std::wstring commandLine = quoteArg(options.targetPath); STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessW(options.targetPath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!created) {
        writeLog(log, "restart-result", "\"ok\":false,\"error\":" + std::to_string(GetLastError()));
        return false;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}
}

// ================================================================
// R-A（§32/§33/§34，2026-09-15）：recovery 系统电源感知。
// S0 Modern Standby 睡眠期间 UI 泵冻结是正常现象，但 recovery 此前对此零感知，
// 把冻结误判为死机 → 判死重启（0915-0 链 1 根因）。注册系统电源通知：
// PBT_APMSUSPEND → sleeping=true；PBT_APMRESUME* → sleeping=false + 唤醒时刻。
// 判死前检查：睡眠中或唤醒 grace 内一律不判死（defer 继续探测）。
// DEVICE_NOTIFY_CALLBACK 由系统线程同步调用回调，recovery 无窗口/消息泵也可用。
// ================================================================
static std::atomic<bool> g_systemSleeping{false};
static std::atomic<ULONGLONG> g_lastWakeTick{0};
static HPOWERNOTIFY g_powerNotify = nullptr;
// 唤醒恢复 grace：主进程 UI 泵在唤醒后需要时间恢复（实测 5-6s），此窗口内不判死。
constexpr DWORD kWakeGraceMs = 15000;

static ULONG CALLBACK powerNotifyCallback(PVOID /*context*/, ULONG type, PVOID /*setting*/) {
    switch (type) {
    case PBT_APMSUSPEND:
        // 系统进入睡眠：先于进程冻结置位，唤醒后判死闸即可拦截。
        g_systemSleeping.store(true, std::memory_order_release);
        break;
    case PBT_APMRESUMEAUTOMATIC:
    case PBT_APMRESUMESUSPEND:
    case PBT_APMRESUMECRITICAL:
        g_systemSleeping.store(false, std::memory_order_release);
        g_lastWakeTick.store(GetTickCount64(), std::memory_order_release);
        break;
    }
    return 0;
}

// DEVICE_NOTIFY_CALLBACK 模式的 Recipient 必须是 **DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS 结构体指针**
// （Microsoft Learn: PowerRegisterSuspendResumeNotification；传函数地址属 API 实参误用）。
// 结构体必须在注册存续期内保持有效 ⇒ 使用静态存储；回调签名与 DEVICE_NOTIFY_CALLBACK_ROUTINE 一致
// （ULONG CALLBACK(PVOID Context, ULONG Type, PVOID Setting)；Setting 对 suspend/resume 不使用）。
static DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS g_powerNotifyParams = { &powerNotifyCallback, nullptr };

static bool powerDeferKill(const std::wstring& log) {
    if (g_systemSleeping.load(std::memory_order_acquire)) {
        writeLog(log, "confirmation-deferred-sleep", "\"reason\":\"system-sleeping\"");
        return true;
    }
    const ULONGLONG lastWake = g_lastWakeTick.load(std::memory_order_acquire);
    if (lastWake != 0 && GetTickCount64() - lastWake < kWakeGraceMs) {
        writeLog(log, "confirmation-deferred-sleep",
            "\"reason\":\"wake-grace\",\"graceMs\":" + std::to_string(kWakeGraceMs));
        return true;
    }
    return false;
}

// RECOVERY-1（2026-09-20 实测修正）：句柄改由 wWinMain 在**启动时**打开并**持有**
// （retainedEvent），不再每次探测临时打开。命名事件对象在**最后一个句柄关闭**时被
// 销毁；父进程退出即关闭其句柄，所以"探测时才打开"在父进程已完全退出时**必然失败**
// （本机实测 LAZY=fail），主动退出的意图因此读不到 ⇒ 被当作非主动退出而重启。
// 持有句柄后：事件对象存活到本服务退出，并保留已置位状态 ⇒ "主动退出"与"硬崩溃"
// 可被精确区分（实测 RETAINED=1 / 0），且与父进程退出耗时无关。
// retainedEvent 为空时（启动期打开失败）回退旧的按名打开行为，并在日志标注 handle。
bool exitIntentSignalled(const std::wstring& exitEventName, HANDLE retainedEvent,
                         const std::wstring& log) {
    if (exitEventName.empty()) {
        writeLog(log, "exit-intent-probe", "\"open\":false,\"reason\":\"empty-name\"");
        return false;
    }
    HANDLE exitEvent = retainedEvent;
    bool transient = false;
    if (!exitEvent) {
        exitEvent = OpenEventW(SYNCHRONIZE, FALSE, exitEventName.c_str());
        transient = true;
        if (!exitEvent) {
            writeLog(log, "exit-intent-probe",
                "\"open\":false,\"reason\":\"open-failed\",\"handle\":\"lazy\"");
            return false;
        }
    }
    const bool signalled = WaitForSingleObject(exitEvent, 0) == WAIT_OBJECT_0;
    if (transient) CloseHandle(exitEvent);
    const std::string handle = transient ? "lazy" : "retained";
    writeLog(log, "exit-intent-probe", signalled
        ? "\"open\":true,\"signalled\":true,\"handle\":\"" + handle + "\""
        : "\"open\":true,\"signalled\":false,\"handle\":\"" + handle + "\"");
    if (signalled)
        writeLog(log, "restart-skipped-intentional-exit", "\"reason\":\"exit-intent-event\"");
    return signalled;
}

// ═══════════════════════════════════════════════════════════════════════════
// GP-XBOX-2（2026-09-28 裁决 §4/§5/§6）：生命周期感知判死协议（消费端）。
// 现场：17:44 RecoveryService 只凭 HWND/WebView 探针失败，就把仍在执行
// PREPARE_TARGET persona=elite 的旧 PID 终止（Pipe is broken + FanHost ~15 s 中断）。
// 新判死顺序（裁决 §5）：
//   ① 探针全失败 ⇒ 写 recovery-observation，读取 native 跨进程快照（Local\YeManCC.RecoveryState.<pid>）。
//   ② exitIntent ⇒ 禁止重启（事件通道 + 快照通道双判据）。
//   ③ 快照不可读/撕裂/代次不符 ⇒ fail-closed：recovery-blocked-state-unavailable，本轮不强杀。
//   ④ lifecycleBusy=true 且心跳新鲜 ⇒ recovery-deferred-lifecycle（不终止、不启第二实例）。
//   ⑤ settled + 新鲜后台心跳：旧协议不杀；新协议还须核对 UI posted-ping 推进。
//   ⑥ 心跳过期，或 idle UI 连续 30 s 无推进（系统唤醒 grace 外）⇒ 允许恢复。
//   ⑦ 终止旧 PID 后必须 WAIT_OBJECT_0 确认；旧 PID 结束后等待 FanHost/8765 所有权释放
//      （有界 20 s）——occupied 一律 recovery-restart-blocked-owner，超时停手，绝不并发第二 FanHost。
// 快照布局与 native/main.cpp 的 RecoveryStateSnapshot 严格同源（static_assert 双侧固定）。
// ═══════════════════════════════════════════════════════════════════════════
struct RecoveryStateSnapshot {
    unsigned int       magic;
    unsigned int       schemaVersion;
    unsigned int       sequence;         // seqlock：写中为奇、稳定为偶
    unsigned int       pid;
    unsigned long long processStartKey;  // FILETIME 创建时间（100ns）
    unsigned long long lastNativeHeartbeat; // 发布时刻 GetTickCount64
    unsigned long long lifecycleStartedAt;  // 忙段起点（0=空闲）
    char               lifecyclePhase[32];
    char               lastHostCommand[32];
    char               persona[32];
    unsigned int       lifecycleBusy;
    unsigned int       fanOwnerState;
    unsigned int       exitIntent;
    unsigned int       reserved0; // Optional posted-UI pulse: bit31=present, low31=counter.
};
static_assert(sizeof(RecoveryStateSnapshot) == 152, "GP-XBOX-2 cross-process snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, magic) == 0, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, schemaVersion) == 4, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, sequence) == 8, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, pid) == 12, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, processStartKey) == 16, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, lastNativeHeartbeat) == 24, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, lifecycleStartedAt) == 32, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, lifecyclePhase) == 40, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, lastHostCommand) == 72, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, persona) == 104, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, lifecycleBusy) == 136, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, fanOwnerState) == 140, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, exitIntent) == 144, "snapshot ABI");
static_assert(offsetof(RecoveryStateSnapshot, reserved0) == 148, "snapshot ABI");

enum class SnapshotRead { Ok = 0, Unavailable = 1, Torn = 2 };

struct SnapshotObservation {
    SnapshotRead state = SnapshotRead::Unavailable;
    std::string unavailableReason = "not-read";
    DWORD pid = 0;
    unsigned long long processStartKey = 0;
    unsigned int sequence = 0;
    unsigned long long heartbeatAgeMs = 0;
    bool heartbeatFresh = false;
    bool lifecycleBusy = false;
    unsigned long long lifecycleStartedAt = 0;
    std::string phase = "unknown";
    std::string lastHostCommand;
    std::string persona;
    unsigned int fanOwnerState = 0;
    bool exitIntent = false;
    bool uiPulseKnown = false;
    unsigned int uiPulse = 0;
};

struct SnapshotChannel {
    HANDLE mapping = nullptr;
    const RecoveryStateSnapshot* view = nullptr;
    DWORD pid = 0;
};

std::wstring snapshotName(DWORD pid) {
    return L"Local\\YeManCC.RecoveryState." + std::to_wstring(pid);
}

void snapshotChannelClose(SnapshotChannel& channel) {
    if (channel.view) { UnmapViewOfFile(channel.view); channel.view = nullptr; }
    if (channel.mapping) { CloseHandle(channel.mapping); channel.mapping = nullptr; }
    channel.pid = 0;
}

bool snapshotChannelEnsure(SnapshotChannel& channel, DWORD pid) {
    if (channel.view && channel.pid == pid) return true;
    snapshotChannelClose(channel);
    const std::wstring name = snapshotName(pid);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    if (!mapping) return false;
    const auto* view = static_cast<const RecoveryStateSnapshot*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(RecoveryStateSnapshot)));
    if (!view) { CloseHandle(mapping); return false; }
    channel.mapping = mapping;
    channel.view = view;
    channel.pid = pid;
    return true;
}

unsigned long long processStartKeyOf(HANDLE process) {
    FILETIME creation{}, exitTime{}, kernel{}, user{};
    if (!process || !GetProcessTimes(process, &creation, &exitTime, &kernel, &user)) return 0;
    ULARGE_INTEGER value{};
    value.LowPart = creation.dwLowDateTime;
    value.HighPart = creation.dwHighDateTime;
    return value.QuadPart;
}

std::string boundedText(const char* text, size_t cap) {
    size_t length = 0;
    while (length < cap && text[length]) ++length;
    return std::string(text, length);
}

SnapshotRead readRecoverySnapshot(SnapshotChannel& channel, HANDLE process, DWORD pid,
                                  SnapshotObservation& obs) {
    obs = SnapshotObservation{};
    obs.pid = pid;
    if (!snapshotChannelEnsure(channel, pid)) {
        obs.unavailableReason = "mapping-absent";
        return SnapshotRead::Unavailable;
    }
    const RecoveryStateSnapshot* view = channel.view;
    for (int attempt = 0; attempt < 6; ++attempt) {
        const unsigned int s1 = view->sequence;
        if (s1 & 1u) { Sleep(2); continue; }
        std::atomic_thread_fence(std::memory_order_acquire);
        RecoveryStateSnapshot copy = *view;
        std::atomic_thread_fence(std::memory_order_acquire);
        const unsigned int s2 = view->sequence;
        if (s1 != s2 || (s2 & 1u)) { Sleep(2); continue; }
        if (copy.magic != kSnapshotMagic || copy.schemaVersion != kSnapshotSchemaVersion) {
            obs.unavailableReason = "magic-or-schema-mismatch";
            return SnapshotRead::Unavailable;
        }
        if (copy.pid != pid) {
            obs.unavailableReason = "pid-mismatch";
            return SnapshotRead::Unavailable;
        }
        const unsigned long long targetKey = processStartKeyOf(process);
        if (targetKey != 0 && copy.processStartKey != targetKey) {
            obs.unavailableReason = "process-start-key-mismatch";
            return SnapshotRead::Unavailable;
        }
        obs.state = SnapshotRead::Ok;
        obs.processStartKey = copy.processStartKey;
        obs.sequence = s2;
        const ULONGLONG now = GetTickCount64();
        obs.heartbeatAgeMs = now >= copy.lastNativeHeartbeat ? now - copy.lastNativeHeartbeat : 0;
        obs.heartbeatFresh = obs.heartbeatAgeMs <= kHeartbeatStaleMs;
        obs.lifecycleBusy = copy.lifecycleBusy != 0;
        obs.lifecycleStartedAt = copy.lifecycleStartedAt;
        obs.phase = boundedText(copy.lifecyclePhase, sizeof(copy.lifecyclePhase));
        obs.lastHostCommand = boundedText(copy.lastHostCommand, sizeof(copy.lastHostCommand));
        obs.persona = boundedText(copy.persona, sizeof(copy.persona));
        obs.fanOwnerState = copy.fanOwnerState;
        obs.exitIntent = copy.exitIntent != 0;
        obs.uiPulseKnown = (copy.reserved0 & ymcc::kRecoveryUiPulsePresent) != 0;
        obs.uiPulse = copy.reserved0 & ymcc::kRecoveryUiPulseMask;
        return SnapshotRead::Ok;
    }
    obs.unavailableReason = "torn-read";
    return SnapshotRead::Torn;
}

// ---- 外部 FanHost 所有权核对（§5.8：旧 PID 结束后的释放等待判据）----
struct OwnerProbe {
    bool fanHostProcessFound = false;
    unsigned int fanHostProcessCount = 0;
    bool port8765Found = false;
};

unsigned short decodeNetworkU16(DWORD value) {
    return static_cast<unsigned short>(((value & 0xffu) << 8) | ((value >> 8) & 0xffu));
}

OwnerProbe probeFanHostOwner() {
    OwnerProbe probe;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, L"YeManFanHost.exe") == 0) {
                    probe.fanHostProcessFound = true;
                    ++probe.fanHostProcessCount;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    // 保守判据：127.0.0.1:8765 只要仍有 LISTEN（任意 owner，含 HTTP.sys），即视为占用。
    ULONG bytes = 0;
    constexpr ULONG kIpv4Family = 2; // AF_INET
    if (GetExtendedTcpTable(nullptr, &bytes, FALSE, kIpv4Family,
                            TCP_TABLE_OWNER_PID_LISTENER, 0) == ERROR_INSUFFICIENT_BUFFER && bytes != 0) {
        std::vector<unsigned char> buffer(bytes);
        auto* table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
        if (GetExtendedTcpTable(table, &bytes, FALSE, kIpv4Family,
                                TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR) {
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto& row = table->table[i];
                if (row.dwLocalAddr == 0x0100007Fu && decodeNetworkU16(row.dwLocalPort) == 8765) {
                    probe.port8765Found = true;
                    break;
                }
            }
        }
    }
    return probe;
}

// ---- 日志字段拼装（§6：pid/processStartKey/sequence/lifecyclePhase/lastHostCommand/fanOwnerState/ageMs）----
std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(ch);
        }
    }
    return out;
}

std::string ownerStateName(unsigned int state) {
    switch (state) {
    case 1: return "owned-by-current";
    case 2: return "handoff-pending";
    case 3: return "released";
    default: return "unknown";
    }
}

std::string observationFields(const SnapshotObservation& obs) {
    std::string fields = "\"targetPid\":" + std::to_string(obs.pid) +
        ",\"processStartKey\":" + std::to_string(obs.processStartKey) +
        ",\"sequence\":" + std::to_string(obs.sequence) +
        ",\"lifecycleBusy\":" + std::string(obs.lifecycleBusy ? "true" : "false") +
        ",\"lifecyclePhase\":\"" + jsonEscape(obs.phase) + "\"" +
        ",\"lifecycleStartedAt\":" + std::to_string(obs.lifecycleStartedAt) +
        ",\"lastHostCommand\":\"" + jsonEscape(obs.lastHostCommand) + "\"" +
        ",\"persona\":\"" + jsonEscape(obs.persona) + "\"" +
        ",\"fanOwnerState\":\"" + ownerStateName(obs.fanOwnerState) + "\"" +
        ",\"ageMs\":" + std::to_string(obs.heartbeatAgeMs) +
        ",\"heartbeatFresh\":" + std::string(obs.heartbeatFresh ? "true" : "false") +
        ",\"exitIntent\":" + std::string(obs.exitIntent ? "true" : "false") +
        ",\"uiPulseKnown\":" + std::string(obs.uiPulseKnown ? "true" : "false") +
        ",\"uiPulse\":" + std::to_string(obs.uiPulse);
    fields += ",\"snapshot\":\"" + jsonEscape(obs.state == SnapshotRead::Ok ? std::string("ok") : obs.unavailableReason) + "\"";
    return fields;
}

std::string ownerProbeFields(const OwnerProbe& probe) {
    return "\"externalOwnerState\":\"" +
        std::string((!probe.fanHostProcessFound && !probe.port8765Found) ? "released" : "blocked") +
        "\",\"fanHostProcess\":" + std::string(probe.fanHostProcessFound ? "true" : "false") +
        ",\"fanHostProcessCount\":" + std::to_string(probe.fanHostProcessCount) +
        ",\"port8765\":" + std::string(probe.port8765Found ? "true" : "false");
}

// ---- 判死决策核（生产与离线自测共用同一实现，裁决 §7「同源模拟」）----
enum class RoundResult { Continue, Stop };

struct RecoveryEpisodeState {
    ymcc::RecoveryUiProgress uiProgress;
    bool deferActive = false;
    ULONGLONG deferStartedTick = 0;
    bool deferWindowLogged = false;
    int decisionRounds = 0;
    ULONGLONG lastUnavailableLogTick = 0;
    ULONGLONG lastOwnerLogTick = 0;
    ULONGLONG lastDeferLogTick = 0;
};

struct RecoveryOps {
    std::function<bool()> processAlive;
    std::function<bool()> terminateTarget;
    std::function<bool()> waitTargetExit;
    std::function<OwnerProbe()> ownerProbe;
    std::function<bool()> spawnTarget;
    std::function<ULONGLONG()> now;
    std::function<ULONGLONG()> uiNow; // Working-time clock excludes system sleep.
    std::function<void(DWORD)> sleep;
};

// §5.8/§5.9：旧 PID 结束后等待 FanHost/8765 所有权释放（有界 20 s）。
// 释放 ⇒ true（可 CreateProcess）；占用写 recovery-restart-blocked-owner；
// 超时写 recovery-restart-owner-timeout 并返回 false（停手，绝不并发第二 FanHost）。
bool waitForOwnerRelease(RecoveryOps& ops, const SnapshotObservation& obs,
                         RecoveryEpisodeState& episode, const std::wstring& log) {
    const ULONGLONG deadline = ops.now() + kOwnerReleaseWaitMs;
    bool blockedLogged = false;
    for (;;) {
        const OwnerProbe probe = ops.ownerProbe();
        if (!probe.fanHostProcessFound && !probe.port8765Found) {
            writeLog(log, "recovery-fan-owner-state", ownerProbeFields(probe) + "," + observationFields(obs));
            return true;
        }
        const ULONGLONG now = ops.now();
        if (!blockedLogged) {
            writeLog(log, "recovery-restart-blocked-owner",
                "\"waitMs\":" + std::to_string(kOwnerReleaseWaitMs) + "," + ownerProbeFields(probe) +
                "," + observationFields(obs));
            blockedLogged = true;
        } else if (now - episode.lastOwnerLogTick >= 4000) {
            writeLog(log, "recovery-fan-owner-state", ownerProbeFields(probe) + "," + observationFields(obs));
            episode.lastOwnerLogTick = now;
        }
        if (now >= deadline) {
            writeLog(log, "recovery-restart-owner-timeout",
                "\"waitMs\":" + std::to_string(kOwnerReleaseWaitMs) + "," + ownerProbeFields(probe) +
                "," + observationFields(obs));
            return false;
        }
        ops.sleep(kOwnerReleasePollMs);
    }
}

// §5.7-§5.9：真实死锁恢复序列。
RoundResult recoveryPerformRestart(const SnapshotObservation& obs, RecoveryEpisodeState& episode,
                                   RecoveryOps& ops, const std::wstring& log,
                                   const char* reason = "ui-unresponsive-lifecycle-idle-heartbeat-stale") {
    writeLog(log, "recovery-restart-allowed",
        "\"reason\":\"" + jsonEscape(reason) + "\"," + observationFields(obs));
    if (ops.processAlive()) {
        writeLog(log, "restart-attempt", "\"reason\":\"unresponsive\"," + observationFields(obs));
        if (!ops.terminateTarget()) return RoundResult::Continue;
        if (!ops.waitTargetExit()) {
            // 旧 PID 未确认退出 ⇒ 绝不 CreateProcess（§5.7）。
            writeLog(log, "restart-result", "\"ok\":false,\"reason\":\"target-termination-timeout\"");
            writeLog(log, "recovery-restart-blocked-process-alive",
                "\"waitMs\":" + std::to_string(kTerminationWaitMs) + "," + observationFields(obs));
            return RoundResult::Continue;
        }
    }
    if (!waitForOwnerRelease(ops, obs, episode, log)) return RoundResult::Stop;
    if (!ops.spawnTarget()) return RoundResult::Stop;
    writeLog(log, "recovery-restart-started", observationFields(obs));
    writeLog(log, "restart-result", "\"ok\":true");
    return RoundResult::Stop;
}

// §5.2-§5.6：单轮决策。UI 全失败已经由调用方确认；本函数只处理"接下来做什么"。
RoundResult recoveryDecisionRound(const SnapshotObservation& obs, RecoveryEpisodeState& episode,
                                  RecoveryOps& ops, const std::wstring& log) {
    ++episode.decisionRounds;
    const ULONGLONG now = ops.now();
    // §5.2 退出意图（快照通道；事件通道由调用方检查）。
    if (obs.exitIntent) {
        writeLog(log, "restart-skipped-intentional-exit",
            "\"reason\":\"snapshot-exit-intent\"," + observationFields(obs));
        return RoundResult::Stop;
    }
    // §5.6 fail-closed：状态快照缺失/损坏/代次不符 ⇒ 本轮不强杀，也不把"读不到"当死锁。
    if (obs.state != SnapshotRead::Ok) {
        episode.uiProgress.reset();
        if (episode.lastUnavailableLogTick == 0 || now - episode.lastUnavailableLogTick >= 30000) {
            episode.lastUnavailableLogTick = now;
            writeLog(log, "recovery-blocked-state-unavailable",
                "\"policy\":\"fail-closed-no-terminate\"," + observationFields(obs));
        }
        return RoundResult::Continue;
    }
    // §5.3 lifecycleBusy + 心跳新鲜 ⇒ 延期（有界窗 12 s；窗后仍新鲜则继续观察，绝不判死）。
    if (obs.heartbeatFresh && obs.lifecycleBusy) {
        episode.uiProgress.reset(); // Never accumulate UI-stall time during protected lifecycle work.
        if (!episode.deferActive) {
            episode.deferActive = true;
            episode.deferStartedTick = now;
            episode.deferWindowLogged = false;
            episode.lastDeferLogTick = now;
            writeLog(log, "recovery-deferred-lifecycle",
                "\"beat\":\"start\",\"waitWindowMs\":" + std::to_string(kDeferWindowMs) + "," + observationFields(obs));
        } else if (now - episode.lastDeferLogTick >= 12000) {
            writeLog(log, "recovery-deferred-lifecycle",
                "\"beat\":\"continue\",\"waitWindowMs\":" + std::to_string(kDeferWindowMs) + "," + observationFields(obs));
            episode.lastDeferLogTick = now;
        }
        if (!episode.deferWindowLogged && now - episode.deferStartedTick >= kDeferWindowMs) {
            episode.deferWindowLogged = true;
            writeLog(log, "recovery-deferred-lifecycle",
                "\"beat\":\"window-exceeded\",\"waitWindowMs\":" + std::to_string(kDeferWindowMs) +
                ",\"policy\":\"heartbeat-fresh-never-kill\"," + observationFields(obs));
        }
        // Fan 所有权观察（事件级：episode 起点 + 每 4 轮）。
        if ((episode.decisionRounds - 1) % 4 == 0) {
            const OwnerProbe probe = ops.ownerProbe();
            writeLog(log, "recovery-fan-owner-state",
                "\"fanOwnerState\":\"" + ownerStateName(obs.fanOwnerState) + "\"," + ownerProbeFields(probe) +
                "," + observationFields(obs));
        }
        return RoundResult::Continue;
    }
    // §5.4 settled 取消忙段延期；后台心跳不再掩盖已确认的 UI 消息泵停滞。
    if (obs.heartbeatFresh) {
        if (episode.deferActive) {
            episode.deferActive = false;
            writeLog(log, "recovery-cancelled-lifecycle-settled",
                "\"reason\":\"heartbeat-fresh-idle\"," + observationFields(obs));
        }
        // A live independent publisher only proves that publisher is alive.
        // With explicit UI progress support, 30 seconds of confirmed failed
        // probes AND no posted-message progress identifies an idle UI stall.
        const ULONGLONG uiNow = ops.uiNow ? ops.uiNow() : now;
        const bool wasActive = episode.uiProgress.active;
        const auto ui = episode.uiProgress.observe(obs.uiPulseKnown, obs.uiPulse, uiNow);
        if (!wasActive && episode.uiProgress.active)
            writeLog(log, "recovery-ui-stall-observed",
                "\"windowMs\":" + std::to_string(ymcc::kRecoveryUiStallMs) + "," + observationFields(obs));
        if (ui == ymcc::RecoveryUiProgressResult::Stalled) {
            writeLog(log, "recovery-ui-stall-confirmed",
                "\"noProgressMs\":" + std::to_string(uiNow - episode.uiProgress.lastProgressTick) +
                "," + observationFields(obs));
            return recoveryPerformRestart(obs, episode, ops, log,
                "ui-pump-stalled-native-heartbeat-fresh-lifecycle-idle");
        }
        // Legacy snapshot (reserved0=0) remains fail-closed for this new path.
        return RoundResult::Continue;
    }
    // §5.5 真实死锁（UI 失败 + 快照已过期 + 无退出意图 + 进程存活）。
    episode.deferActive = false;
    return recoveryPerformRestart(obs, episode, ops, log);
}

// ═══════════════════════════════════════════════════════════════════════════
// 离线同源自测：原九门 + UI 停滞/睡眠时钟/回退兼容 + schema 读写自测。
// 同源：直接驱动上面的生产决策核 recoveryDecisionRound（仅替换 OS 原语与时钟）。
// 无安装、无硬件、不睡眠、不启停常驻服务。
// ═══════════════════════════════════════════════════════════════════════════
struct SimStep {
    bool snapshotAvailable = true;
    bool busy = false;
    unsigned long long heartbeatAgeMs = 1000;
    bool exitIntent = false;
    const char* phase = "lifecycle-work";
    bool uiPulseKnown = false;
    unsigned int uiPulse = 0;
    bool uiRecovered = false;
    bool powerDeferred = false;
};

struct SimWorld {
    ULONGLONG now = 1000000;
    ULONGLONG uiWorkingTime = 1000000;
    ULONGLONG uiStepMs = kRoundIntervalMs;
    ULONGLONG wallStepMs = kRoundIntervalMs;
    bool targetAlive = true;
    int kills = 0;
    int spawns = 0;
    bool ownerBlocked = false;             // probe 队列耗尽后的默认占用状态
    std::vector<bool> ownerBlockedQueue;   // 每个 probe 的脚本状态
    size_t ownerProbeIndex = 0;
    bool ownerBlockedNow() {
        const bool blocked = ownerProbeIndex < ownerBlockedQueue.size()
            ? ownerBlockedQueue[ownerProbeIndex] : ownerBlocked;
        ++ownerProbeIndex;
        return blocked;
    }
};

struct ScenarioOutcome {
    int kills = 0;
    int spawns = 0;
    std::vector<std::string> events;
    size_t stopRound = 0;
};

void runSimScenario(const std::vector<SimStep>& steps, SimWorld& world, ScenarioOutcome& outcome,
                    const std::wstring& tracePath) {
    RecoveryEpisodeState episode;
    RecoveryOps ops;
    ops.processAlive = [&world]() { return world.targetAlive; };
    ops.terminateTarget = [&world]() {
        ++world.kills;
        world.targetAlive = false;
        return true;
    };
    ops.waitTargetExit = [&world]() { return !world.targetAlive; };
    ops.ownerProbe = [&world]() {
        OwnerProbe probe;
        const bool blocked = world.ownerBlockedNow();
        probe.fanHostProcessFound = blocked;
        probe.port8765Found = blocked;
        probe.fanHostProcessCount = blocked ? 1u : 0u;
        return probe;
    };
    ops.spawnTarget = [&world]() { ++world.spawns; return true; };
    ops.now = [&world]() { return world.now; };
    ops.uiNow = [&world]() { return world.uiWorkingTime; };
    ops.sleep = [&world](DWORD ms) { world.now += ms; };
    g_selftestEventCapture = &outcome.events;
    size_t round = 0;
    for (const SimStep& step : steps) {
        ++round;
        world.now += world.wallStepMs;
        world.uiWorkingTime += world.uiStepMs;
        if (step.uiRecovered || step.powerDeferred) { episode.uiProgress.reset(); continue; }
        // Stall observation uses working-time; owner/defer gates keep their original clock.
        SnapshotObservation obs;
        obs.pid = 4242;
        obs.processStartKey = 0x1122334455ull;
        obs.state = step.snapshotAvailable ? SnapshotRead::Ok : SnapshotRead::Unavailable;
        if (!step.snapshotAvailable) obs.unavailableReason = "mapping-absent";
        obs.heartbeatAgeMs = step.heartbeatAgeMs;
        obs.heartbeatFresh = step.heartbeatAgeMs <= kHeartbeatStaleMs;
        obs.lifecycleBusy = step.busy;
        obs.phase = step.phase;
        obs.lastHostCommand = "PREPARE_TARGET";
        obs.persona = "xbox-elite-v2";
        obs.fanOwnerState = 1;
        obs.exitIntent = step.exitIntent;
        obs.uiPulseKnown = step.uiPulseKnown;
        obs.uiPulse = step.uiPulse;
        const RoundResult result = recoveryDecisionRound(obs, episode, ops, tracePath);
        if (result == RoundResult::Stop) { outcome.stopRound = round; break; }
    }
    if (outcome.stopRound == 0) outcome.stopRound = round;
    g_selftestEventCapture = nullptr;
    outcome.kills = world.kills;
    outcome.spawns = world.spawns;
}

bool eventsContain(const std::vector<std::string>& events, const char* name) {
    for (const auto& item : events) if (item == name) return true;
    return false;
}

int eventIndex(const std::vector<std::string>& events, const char* name) {
    for (size_t i = 0; i < events.size(); ++i) if (events[i] == name) return static_cast<int>(i);
    return -1;
}

std::string eventsText(const std::vector<std::string>& events) {
    std::string text;
    for (const auto& item : events) { if (!text.empty()) text += ">"; text += item; }
    return text;
}

std::string layoutText() {
    std::ostringstream out;
    out << "layout sizeof=" << sizeof(RecoveryStateSnapshot)
        << " magic=" << offsetof(RecoveryStateSnapshot, magic)
        << " schemaVersion=" << offsetof(RecoveryStateSnapshot, schemaVersion)
        << " sequence=" << offsetof(RecoveryStateSnapshot, sequence)
        << " pid=" << offsetof(RecoveryStateSnapshot, pid)
        << " processStartKey=" << offsetof(RecoveryStateSnapshot, processStartKey)
        << " lastNativeHeartbeat=" << offsetof(RecoveryStateSnapshot, lastNativeHeartbeat)
        << " lifecycleStartedAt=" << offsetof(RecoveryStateSnapshot, lifecycleStartedAt)
        << " lifecyclePhase=" << offsetof(RecoveryStateSnapshot, lifecyclePhase)
        << " lastHostCommand=" << offsetof(RecoveryStateSnapshot, lastHostCommand)
        << " persona=" << offsetof(RecoveryStateSnapshot, persona)
        << " lifecycleBusy=" << offsetof(RecoveryStateSnapshot, lifecycleBusy)
        << " fanOwnerState=" << offsetof(RecoveryStateSnapshot, fanOwnerState)
        << " exitIntent=" << offsetof(RecoveryStateSnapshot, exitIntent)
        << " reserved0=" << offsetof(RecoveryStateSnapshot, reserved0)
        << " magicValue=0x" << std::hex << kSnapshotMagic << std::dec
        << " schema=" << kSnapshotSchemaVersion;
    return out.str();
}

// schema/读写自测：用生产读取器（readRecoverySnapshot）对真实命名映射做
// 正例（roundtrip）/ 反例（撕裂序列 / 代次不符 / 映射缺失）。
void runSnapshotChannelSelfTest(std::ostringstream& report, bool& pass,
                                const std::function<void(const char*, bool, const std::string&)>& check) {
    const DWORD pid = GetCurrentProcessId();
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        sizeof(RecoveryStateSnapshot), snapshotName(pid).c_str());
    auto* view = mapping
        ? static_cast<RecoveryStateSnapshot*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RecoveryStateSnapshot)))
        : nullptr;
    const bool mappingOk = mapping != nullptr && view != nullptr;
    check("schema-mapping-created", mappingOk, mappingOk ? "live" : "create-failed");

    auto writeFields = [&](ULONGLONG heartbeat, unsigned int busy, unsigned int sequence) {
        memset(view, 0, sizeof(RecoveryStateSnapshot));
        view->magic = kSnapshotMagic;
        view->schemaVersion = kSnapshotSchemaVersion;
        view->sequence = sequence;
        view->pid = pid;
        view->processStartKey = processStartKeyOf(GetCurrentProcess());
        view->lastNativeHeartbeat = heartbeat;
        view->lifecycleStartedAt = 0;
        strncpy_s(view->lifecyclePhase, "roundtrip-phase", _TRUNCATE);
        strncpy_s(view->lastHostCommand, "PREPARE_TARGET", _TRUNCATE);
        strncpy_s(view->persona, "xbox-elite-v2", _TRUNCATE);
        view->lifecycleBusy = busy;
        view->fanOwnerState = 1;
        view->exitIntent = 1;
        view->reserved0 = ymcc::encodeRecoveryUiPulse(123);
    };

    if (mappingOk) {
        writeFields(GetTickCount64(), 1, 2);
        SnapshotChannel channel;
        SnapshotObservation obs;
        const SnapshotRead read = readRecoverySnapshot(channel, GetCurrentProcess(), pid, obs);
        check("schema-roundtrip", read == SnapshotRead::Ok && obs.lifecycleBusy && obs.exitIntent &&
              obs.heartbeatFresh && obs.phase == "roundtrip-phase" &&
              obs.lastHostCommand == "PREPARE_TARGET" && obs.persona == "xbox-elite-v2" &&
              obs.fanOwnerState == 1 && obs.processStartKey != 0 && obs.uiPulseKnown && obs.uiPulse == 123,
              "read=" + std::to_string(static_cast<int>(read)) + " phase=" + obs.phase +
              " command=" + obs.lastHostCommand + " persona=" + obs.persona +
              " startKey=" + std::to_string(obs.processStartKey));
        snapshotChannelClose(channel);

        view->sequence = 3;   // 撕裂（奇数 = 写中）序列必须判不可读
        SnapshotChannel tornChannel;
        SnapshotObservation tornObs;
        const SnapshotRead torn = readRecoverySnapshot(tornChannel, GetCurrentProcess(), pid, tornObs);
        check("schema-torn-read-detected", torn != SnapshotRead::Ok && tornObs.unavailableReason == "torn-read",
              "state=" + std::to_string(static_cast<int>(torn)) + " reason=" + tornObs.unavailableReason);
        snapshotChannelClose(tornChannel);

        view->sequence = 2;
        view->processStartKey ^= 1ull;   // 模拟 PID 复用（代次不符）
        SnapshotChannel keyChannel;
        SnapshotObservation keyObs;
        const SnapshotRead keyRead = readRecoverySnapshot(keyChannel, GetCurrentProcess(), pid, keyObs);
        check("schema-startkey-rejects-stale-generation",
              keyRead != SnapshotRead::Ok && keyObs.unavailableReason == "process-start-key-mismatch",
              "state=" + std::to_string(static_cast<int>(keyRead)) + " reason=" + keyObs.unavailableReason);
        snapshotChannelClose(keyChannel);

        UnmapViewOfFile(view);
        CloseHandle(mapping);
        SnapshotChannel missingChannel;
        SnapshotObservation missingObs;
        const SnapshotRead missing = readRecoverySnapshot(missingChannel, GetCurrentProcess(), pid + 1, missingObs);
        check("schema-missing-mapping-fail-closed",
              missing == SnapshotRead::Unavailable && missingObs.unavailableReason == "mapping-absent",
              "state=" + std::to_string(static_cast<int>(missing)) + " reason=" + missingObs.unavailableReason);
        snapshotChannelClose(missingChannel);
    } else {
        if (view) UnmapViewOfFile(view);
        if (mapping) CloseHandle(mapping);
    }
}

int runRecoveryModelSelfTest() {
    const std::wstring dir = dataDir();
    const std::wstring tracePath = dir + L"\\recovery-selftest-trace.log";
    DeleteFileW(tracePath.c_str());
    std::ostringstream report;
    bool pass = true;
    auto check = [&](const char* name, bool ok, const std::string& detail) {
        report << (ok ? "PASS " : "FAIL ") << name << " " << detail << "\n";
        if (!ok) pass = false;
    };
    report << "recovery-selftest (GP-XBOX-2 offline gates; same-source decision core; no install/hardware/sleep)\n";
    report << layoutText() << "\n";

    // 门 1（§7-1）：UI 全失败 + PREPARE_TARGET 进行中 + 心跳新鲜 ⇒ 0 杀 0 启 + deferred。
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps(6, SimStep{true, true, 1000, false, "lifecycle:initial-target-admission"});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-1-deferred-no-kill", world.kills == 0 && world.spawns == 0 &&
              eventsContain(outcome.events, "recovery-deferred-lifecycle") &&
              !eventsContain(outcome.events, "restart-attempt") &&
              !eventsContain(outcome.events, "recovery-restart-started"),
              "kills=" + std::to_string(world.kills) + " spawns=" + std::to_string(world.spawns) +
              " events=" + eventsText(outcome.events));
    }
    // 门 2（§7-2）：生命周期在窗内 settled ⇒ 0 重启 + cancelled-lifecycle-settled。
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps;
        steps.push_back(SimStep{true, true, 1000, false, "lifecycle:initial-target-admission"});
        steps.push_back(SimStep{true, true, 1000, false, "lifecycle:initial-target-admission"});
        for (int i = 0; i < 4; ++i) steps.push_back(SimStep{true, false, 1000, false, "idle"});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-2-settled-cancelled", world.kills == 0 && world.spawns == 0 &&
              eventsContain(outcome.events, "recovery-cancelled-lifecycle-settled") &&
              !eventsContain(outcome.events, "restart-attempt"),
              "kills=" + std::to_string(world.kills) + " spawns=" + std::to_string(world.spawns) +
              " events=" + eventsText(outcome.events));
    }
    // 门 3（§7-3）：心跳过期 + 生命周期空闲 + 无退出意图 ⇒ 恰好 1 杀 1 启。
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps;
        steps.push_back(SimStep{true, false, 30000, false, "idle"});
        steps.push_back(SimStep{true, false, 30000, false, "idle"});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-3-true-deadlock-restarts-once", world.kills == 1 && world.spawns == 1 &&
              eventsContain(outcome.events, "recovery-restart-allowed") &&
              eventsContain(outcome.events, "restart-attempt") &&
              eventsContain(outcome.events, "recovery-restart-started") &&
              eventsContain(outcome.events, "recovery-fan-owner-state"),
              "kills=" + std::to_string(world.kills) + " spawns=" + std::to_string(world.spawns) +
              " events=" + eventsText(outcome.events));
    }
    // 门 4（§7-4）：旧进程已结束但 FanHost/8765 仍占用 ⇒ 0 启 + blocked-owner + 超时停手。
    {
        SimWorld world;
        ScenarioOutcome outcome;
        world.ownerBlocked = true;
        std::vector<SimStep> steps;
        steps.push_back(SimStep{true, false, 30000, false, "idle"});
        steps.push_back(SimStep{true, false, 30000, false, "idle"});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-4-owner-blocked-no-spawn", world.kills == 1 && world.spawns == 0 &&
              eventsContain(outcome.events, "recovery-restart-blocked-owner") &&
              eventsContain(outcome.events, "recovery-restart-owner-timeout") &&
              !eventsContain(outcome.events, "recovery-restart-started"),
              "kills=" + std::to_string(world.kills) + " spawns=" + std::to_string(world.spawns) +
              " events=" + eventsText(outcome.events));
    }
    // 门 5（§7-5）：owner 在窗口内释放 ⇒ 只启动一次新进程。
    {
        SimWorld world;
        ScenarioOutcome outcome;
        world.ownerBlockedQueue = {true, true, true, false};
        world.ownerBlocked = false;
        std::vector<SimStep> steps;
        steps.push_back(SimStep{true, false, 30000, false, "idle"});
        steps.push_back(SimStep{true, false, 30000, false, "idle"});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-5-owner-released-single-spawn", world.kills == 1 && world.spawns == 1 &&
              eventsContain(outcome.events, "recovery-restart-blocked-owner") &&
              eventsContain(outcome.events, "recovery-restart-started") &&
              !eventsContain(outcome.events, "recovery-restart-owner-timeout"),
              "kills=" + std::to_string(world.kills) + " spawns=" + std::to_string(world.spawns) +
              " events=" + eventsText(outcome.events));
    }
    // 门 6（§7-6）：exit intent=true ⇒ 0 杀 0 启。
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps;
        steps.push_back(SimStep{true, true, 1000, true, "lifecycle:persona-transition"});
        steps.push_back(SimStep{true, true, 1000, true, "lifecycle:persona-transition"});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-6-exit-intent-no-restart", world.kills == 0 && world.spawns == 0 &&
              eventsContain(outcome.events, "restart-skipped-intentional-exit") &&
              !eventsContain(outcome.events, "restart-attempt"),
              "kills=" + std::to_string(world.kills) + " spawns=" + std::to_string(world.spawns) +
              " events=" + eventsText(outcome.events));
    }
    // 门 7（§7-7）：取证输入（如 Xbox 双槽 mask）不是恢复判据——决策核不读任何槽位掩码；
    // 本门断言：即便处于"双槽世界"，busy+新鲜心跳依然只 defer，不触发重启、不阻断生命周期。
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps(4, SimStep{true, true, 1000, false, "lifecycle:initial-target-admission"});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-7-dual-slot-evidence-not-a-gate", world.kills == 0 && world.spawns == 0 &&
              eventsContain(outcome.events, "recovery-deferred-lifecycle") &&
              !eventsContain(outcome.events, "recovery-restart-allowed"),
              "kills=" + std::to_string(world.kills) + " spawns=" + std::to_string(world.spawns) +
              " events=" + eventsText(outcome.events));
    }
    // 门 8（§7-8）：17:44 同源回放——PREPARE_TARGET 完成前不得出现 restart-attempt；
    // 无 restart-attempt ⇒ 不会 TerminateProcess ⇒ 旧 InputHost 不会出现 Pipe is broken，
    // 也不会出现新旧 YMCC/新旧 FanHost 并行（本服务是唯一启动者）。
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps;
        steps.push_back(SimStep{true, true, 1000, false, "lifecycle:persona-transition"});
        steps.push_back(SimStep{true, true, 1000, false, "lifecycle:initial-target-admission"});
        steps.push_back(SimStep{true, true, 800, false, "lifecycle:initial-target-admission"});
        steps.push_back(SimStep{true, false, 600, false, "idle"});
        steps.push_back(SimStep{true, false, 700, false, "idle"});
        runSimScenario(steps, world, outcome, tracePath);
        const int deferIndex = eventIndex(outcome.events, "recovery-deferred-lifecycle");
        const int cancelIndex = eventIndex(outcome.events, "recovery-cancelled-lifecycle-settled");
        check("gate-8-replay-1744-no-kill-no-broken-pipe", world.kills == 0 && world.spawns == 0 &&
              deferIndex >= 0 && cancelIndex > deferIndex &&
              !eventsContain(outcome.events, "restart-attempt") &&
              !eventsContain(outcome.events, "recovery-restart-allowed") &&
              !eventsContain(outcome.events, "restart-result"),
              "kills=" + std::to_string(world.kills) + " spawns=" + std::to_string(world.spawns) +
              " deferIndex=" + std::to_string(deferIndex) + " cancelIndex=" + std::to_string(cancelIndex) +
              " events=" + eventsText(outcome.events));
    }
    // 门 9（§5.6 附加）：快照缺失 ⇒ fail-closed（0 杀 0 启 + blocked-state-unavailable）。
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps(3, SimStep{false, false, 30000, false, "unknown"});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-9-state-unavailable-fail-closed", world.kills == 0 && world.spawns == 0 &&
              eventsContain(outcome.events, "recovery-blocked-state-unavailable") &&
              !eventsContain(outcome.events, "restart-attempt"),
              "kills=" + std::to_string(world.kills) + " spawns=" + std::to_string(world.spawns) +
              " events=" + eventsText(outcome.events));
    }
    // 2026-10-01 wake hang: the independent publisher can outlive the UI pump.
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps(11, SimStep{true, false, 500, false, "idle", true, 42});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-10-idle-ui-stall-fresh-publisher-restarts-once",
              world.kills == 1 && world.spawns == 1 && outcome.stopRound == 11 &&
              eventsContain(outcome.events, "recovery-ui-stall-confirmed"), "30s / one restart");
    }
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps(10, SimStep{true, false, 500, false, "idle", true, 42});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-11-ui-stall-before-30s-no-kill", world.kills == 0 && world.spawns == 0, "27s observed");
    }
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps;
        for (unsigned int pulse = 0; pulse < 40; ++pulse)
            steps.push_back(SimStep{true, false, 500, false, "idle", true, pulse});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-12-posted-ui-progress-no-kill", world.kills == 0 && world.spawns == 0,
              "probe timeout with advancing UI is not a dead pump");
    }
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps(40, SimStep{true, false, 500, false, "idle"});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-13-legacy-reserved-zero-no-kill", world.kills == 0 && world.spawns == 0,
              "old publisher cannot be interpreted as UI stall");
    }
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps(40, SimStep{true, true, 500, false, "PREPARE_TARGET", true, 42});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-14-busy-ui-stall-no-kill", world.kills == 0 && world.spawns == 0,
              "17:44 lifecycle protection still takes priority");
    }
    for (int resetKind = 0; resetKind < 4; ++resetKind) {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps(10, SimStep{true, false, 500, false, "idle", true, 42});
        SimStep reset{true, false, 500, false, "idle", true, 42};
        if (resetKind == 0) reset.uiRecovered = true;
        if (resetKind == 1) reset.powerDeferred = true;
        if (resetKind == 2) reset.busy = true;
        if (resetKind == 3) reset.snapshotAvailable = false;
        steps.push_back(reset);
        steps.insert(steps.end(), 10, SimStep{true, false, 500, false, "idle", true, 42});
        runSimScenario(steps, world, outcome, tracePath);
        const std::string name = "gate-15-ui-stall-reset-" + std::to_string(resetKind);
        check(name.c_str(), world.kills == 0 && world.spawns == 0,
              "UI success / sleep grace / lifecycle work / missing snapshot resets deadline");
    }
    {
        SimWorld world;
        ScenarioOutcome outcome;
        world.ownerBlocked = true;
        std::vector<SimStep> steps(11, SimStep{true, false, 500, false, "idle", true, 42});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-16-ui-stall-owner-blocked-no-second-instance",
              world.kills == 1 && world.spawns == 0 && eventsContain(outcome.events, "recovery-restart-owner-timeout"),
              "same owner-release gate");
    }
    {
        SimWorld world;
        ScenarioOutcome outcome;
        std::vector<SimStep> steps(10, SimStep{true, false, 500, false, "idle", true, 42});
        steps.push_back(SimStep{true, false, 500, true, "idle", true, 42});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-17-ui-stall-exit-intent-no-kill", world.kills == 0 && world.spawns == 0,
              "intentional exit takes priority at deadline");
    }
    {
        ymcc::RecoveryUiProgress tracker;
        const auto first = tracker.observe(true, 42, 1000);
        const auto second = tracker.observe(true, 42, 30999);
        const auto boundary = tracker.observe(true, 42, 31000);
        check("gate-18-ui-stall-boundary-and-samples",
              first != ymcc::RecoveryUiProgressResult::Stalled && second != ymcc::RecoveryUiProgressResult::Stalled &&
              boundary == ymcc::RecoveryUiProgressResult::Stalled, "29999 / 30000 ms boundary");
        tracker.reset();
        tracker.observe(true, 42, 1000);
        check("gate-19-ui-stall-one-long-gap-not-sufficient",
              tracker.observe(true, 42, 90000) != ymcc::RecoveryUiProgressResult::Stalled,
              "at least three failed rounds");
        tracker.reset();
        tracker.observe(true, ymcc::kRecoveryUiPulseMask, 1000);
        check("gate-20-ui-pulse-wrap-is-progress",
              tracker.observe(true, 0, 100000) == ymcc::RecoveryUiProgressResult::Advanced, "pulse wrap");
        check("gate-21-ui-clock-reset-no-underflow",
              tracker.observe(true, 0, 999) == ymcc::RecoveryUiProgressResult::Observing, "backwards clock");
    }
    {
        SimWorld world;
        ScenarioOutcome outcome;
        world.uiStepMs = 0;
        world.wallStepMs = 24ULL * 60 * 60 * 1000;
        std::vector<SimStep> steps(40, SimStep{true, false, 500, false, "idle", true, 42});
        runSimScenario(steps, world, outcome, tracePath);
        check("gate-22-sleep-time-not-ui-stall-time", world.kills == 0 && world.spawns == 0,
              "wall time advances; UI working-time does not");
    }
    // schema/读写自测（生产读取器 + 真实命名映射）。
    runSnapshotChannelSelfTest(report, pass, check);

    std::string trace;
    {
        std::ifstream in(tracePath, std::ios::binary);
        if (in) trace.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    report << "----- selftest trace (recovery-selftest-trace.log) -----\n" << trace << "----- end trace -----\n";
    report << (pass ? "RESULT PASS\n" : "RESULT FAIL\n");
    {
        std::ofstream out(dir + L"\\recovery-selftest.txt", std::ios::trunc);
        if (out) out << report.str();
    }
    fputs(report.str().c_str(), stdout);
    return pass ? 0 : 3;
}

// `--snapshot-read-once --pid N --out F`：供 native 自测跨进程验证生产读取器。
int runSnapshotReadOnce() {
    DWORD pid = 0;
    std::wstring outPath;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--pid") == 0 && i + 1 < argc) pid = wcstoul(argv[++i], nullptr, 10);
        else if (_wcsicmp(argv[i], L"--out") == 0 && i + 1 < argc) outPath = argv[++i];
    }
    LocalFree(argv);
    if (pid == 0 || outPath.empty()) return 2;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    SnapshotChannel channel;
    SnapshotObservation obs;
    const SnapshotRead read = readRecoverySnapshot(channel, process, pid, obs);
    if (process) CloseHandle(process);
    snapshotChannelClose(channel);
    std::ostringstream json;
    json << "{\"mode\":\"snapshot-read-once\",\"available\":" << (read == SnapshotRead::Ok ? "true" : "false")
         << ",\"state\":" << static_cast<int>(read)
         << ",\"reason\":\"" << jsonEscape(obs.unavailableReason) << "\""
         << ",\"pid\":" << obs.pid
         << ",\"processStartKey\":" << obs.processStartKey
         << ",\"sequence\":" << obs.sequence
         << ",\"heartbeatAgeMs\":" << obs.heartbeatAgeMs
         << ",\"heartbeatFresh\":" << (obs.heartbeatFresh ? "true" : "false")
         << ",\"lifecycleBusy\":" << (obs.lifecycleBusy ? "true" : "false")
         << ",\"lifecyclePhase\":\"" << jsonEscape(obs.phase) << "\""
         << ",\"lastHostCommand\":\"" << jsonEscape(obs.lastHostCommand) << "\""
         << ",\"persona\":\"" << jsonEscape(obs.persona) << "\""
         << ",\"fanOwnerState\":\"" << ownerStateName(obs.fanOwnerState) << "\""
         << ",\"exitIntent\":" << (obs.exitIntent ? "true" : "false")
         << ",\"uiPulseKnown\":" << (obs.uiPulseKnown ? "true" : "false")
         << ",\"uiPulse\":" << obs.uiPulse
         << ",\"sizeof\":" << sizeof(RecoveryStateSnapshot) << "}\n";
    std::ofstream out(outPath, std::ios::trunc);
    if (!out) return 2;
    out << json.str();
    return read == SnapshotRead::Ok ? 0 : 3;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // GP-XBOX-2 离线入口（不依赖 --pid/--event/--target；无安装、无常驻、无睡眠动作）。
    if (wcsstr(GetCommandLineW(), L"--selftest")) return runRecoveryModelSelfTest();
    if (wcsstr(GetCommandLineW(), L"--snapshot-read-once")) return runSnapshotReadOnce();
    Options options;
    if (!parseOptions(options)) return 2;
    const std::wstring mutexName = L"Local\\YeManCC.RecoveryService." + std::to_wstring(options.pid);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) { if (mutex) CloseHandle(mutex); return 0; }
    HANDLE pongEvent = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, options.eventName.c_str());
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, options.pid);
    if (!pongEvent || !process) { if (pongEvent) CloseHandle(pongEvent); if (process) CloseHandle(process); CloseHandle(mutex); return 3; }
    // RECOVERY-1：启动时就打开 exit-intent 事件并**持有句柄**，使事件对象活过父进程
    // 退出并保留置位状态（父进程一旦完全退出，其句柄被系统关闭；若本服务未持有句柄，
    // 事件对象即被销毁，探测只能得到 open:false，主动退出意图不可读）。打开失败不致命：
    // exitIntentSignalled 会回退按名打开（旧行为）并记 handle:"lazy"。
    HANDLE exitEventRetained = options.exitEventName.empty()
        ? nullptr
        : OpenEventW(SYNCHRONIZE, FALSE, options.exitEventName.c_str());

    const std::wstring log = logPath();
    writeLog(log, "service-started", "\"intervalMs\":3000,\"confirmationSamples\":3,\"confirmationWindowMs\":1000,\"rule\":\"hwnd-or-webview\",\"exitIntentHandle\":\"" +
        std::string(exitEventRetained ? "retained" : "unavailable") +
        "\",\"deathGate\":\"lifecycle-snapshot\"");
    // R-A（§32 A1）：注册系统电源通知（回调模式，无需消息泵）。**实参修正（2026-09-20）**：
    // Recipient 传的是 DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS 结构体指针（原实现传函数地址，属
    // API 契约违背）。注册失败不致命，退回旧行为（无电源感知），记日志供诊断；失败时显式
    // 复位句柄，保证注销段只在"确实注册成功"时执行。签名=Win8+
    // PowerRegisterSuspendResumeNotification(DWORD Flags, HANDLE Recipient, PHPOWERNOTIFY)。
    DWORD powerRc = PowerRegisterSuspendResumeNotification(
        DEVICE_NOTIFY_CALLBACK,
        reinterpret_cast<HANDLE>(&g_powerNotifyParams),
        &g_powerNotify);
    if (powerRc != ERROR_SUCCESS) g_powerNotify = nullptr;
    writeLog(log, "power-notify-registered",
        "\"ok\":" + std::string(powerRc == ERROR_SUCCESS ? "true" : "false") +
        ",\"error\":" + std::to_string(powerRc) +
        ",\"recipient\":\"subscribe-parameters\"");
    Sleep(kStartupGraceMs);
    DWORD serial = 1;
    RecoveryEpisodeState episode;
    SnapshotChannel channel;
    RecoveryOps ops;
    ops.processAlive = [&]() { return processAlive(process); };
    ops.terminateTarget = [&]() { return terminateTargetProcess(process, log); };
    ops.waitTargetExit = [&]() { return targetTerminationSettled(process); };
    ops.ownerProbe = []() { return probeFanHostOwner(); };
    ops.spawnTarget = [&]() { return spawnTargetProcess(options, log); };
    ops.now = []() { return GetTickCount64(); };
    ops.uiNow = []() -> ULONGLONG {
        ULONGLONG workingTime = 0;
        // Failure stays at zero, which cannot accumulate a stall deadline.
        return QueryUnbiasedInterruptTime(&workingTime) ? workingTime / 10000ULL : 0;
    };
    ops.sleep = [](DWORD ms) { Sleep(ms); };
    for (;;) {
        if (!processAlive(process)) { writeLog(log, "target-exited"); break; }
        // First inquiry is a cheap single sample. Only an all-fail result
        // escalates to the 1-second / 3-sample confirmation inquiry.
        if (!firstProbe(options, pongEvent, serial++)) {
            writeLog(log, "first-probe-failed", "\"channels\":\"hwnd,webview\"");
            writeLog(log, "confirmation-start", "\"samples\":3,\"windowMs\":1000");
            if (!confirmationProbe(options, pongEvent, serial)) {
                writeLog(log, "unresponsive-confirmed", "\"failedSamples\":3,\"channels\":\"hwnd,webview\"");
                // R-A（§32 A2）：睡眠中或唤醒 grace 内不判死——S0 Modern Standby
                // 睡眠期 UI 泵冻结是正常现象（0915-0 链 1 根因）。defer 继续循环
                // 探测，唤醒后恢复判死（真死锁仍会被救，最多延后 grace）。
                if (powerDeferKill(log)) {
                    episode.uiProgress.reset();
                    Sleep(kRoundIntervalMs);
                    continue;
                }
                // GP-XBOX-2（§5.1）：探针失败后先写观测行并读取生命周期快照——
                // UI 通道失败不再单独构成判死条件。
                SnapshotObservation observation;
                (void)readRecoverySnapshot(channel, process, options.pid, observation);
                writeLog(log, "recovery-observation",
                    "\"source\":\"lifecycle-snapshot\"," + observationFields(observation));
                // §5.2 退出意图双通道（事件 + 快照，快照通道在决策核内再判一次）。
                if (exitIntentSignalled(options.exitEventName, exitEventRetained, log)) break;
                if (recoveryDecisionRound(observation, episode, ops, log) == RoundResult::Stop) break;
            } else {
                writeLog(log, "confirmation-passed", "\"rule\":\"any-channel-success\"");
                episode.uiProgress.reset();
                if (episode.deferActive) {
                    episode.deferActive = false;
                    writeLog(log, "recovery-cancelled-lifecycle-settled",
                        "\"reason\":\"ui-recovered\",\"targetPid\":" + std::to_string(options.pid));
                }
            }
        } else {
            episode.uiProgress.reset();
            if (episode.deferActive) {
                episode.deferActive = false;
                writeLog(log, "recovery-cancelled-lifecycle-settled",
                    "\"reason\":\"ui-recovered\",\"targetPid\":" + std::to_string(options.pid));
            }
        }
        Sleep(kRoundIntervalMs);
    }
    // R-A（§32 A4）：退出清理电源通知注册。**配对 API 修正（2026-09-20）**：注册走
    // PowerRegisterSuspendResumeNotification，注销必须用其配对函数
    // PowerUnregisterSuspendResumeNotification（原用的 UnregisterSuspendResumeNotification
    // 是另一族 API），并使用注册返回的句柄。
    if (g_powerNotify) {
        PowerUnregisterSuspendResumeNotification(g_powerNotify);
        g_powerNotify = nullptr;
    }
    snapshotChannelClose(channel);
    writeLog(log, "service-exited");
    if (exitEventRetained) CloseHandle(exitEventRetained);
    CloseHandle(process); CloseHandle(pongEvent); CloseHandle(mutex); return 0;
}