#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#include <bcrypt.h>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include "decky_sidebar_launch.h"
#if defined(_MSC_VER)
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#endif

namespace ymcc::physicalreader {
struct Context {
    bool desired = false;
    uint64_t power = 0, topology = 0, epoch = 0, parentBirth = 0;
    DWORD parentPid = 0;
    std::wstring helperExe, dll, devicePath;
    DWORD slot = 0;
    bool operator==(const Context&) const = default;
};
struct Sample {
    XINPUT_GAMEPAD pad{};
    bool guide = false;
    uint64_t tick = 0, sequence = 0;
    DWORD readerPid = 0;
    DWORD actualSlot = 4;
    DWORD packet = 0; // Opaque existing wire offset 68; evidence only.
};

namespace detail {
inline uint16_t u16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t u32(const uint8_t* p) noexcept {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
        (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint64_t u64(const uint8_t* p) noexcept {
    return uint64_t(u32(p)) | (uint64_t(u32(p + 4)) << 32);
}
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) noexcept : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    explicit operator bool() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
    void reset(HANDLE h = nullptr) noexcept {
        if (*this) CloseHandle(value);
        value = h;
    }
};
// Windows argv quoting, including trailing backslashes before the closing quote.
inline std::wstring quote(std::wstring_view text) {
    std::wstring result(1, L'"');
    size_t slashes = 0;
    for (const wchar_t c : text) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result.push_back(c);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}
inline bool noNul(const std::wstring& s) noexcept {
    return !s.empty() && s.find(L'\0') == std::wstring::npos;
}
inline uint64_t birth(HANDLE process) noexcept {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
    return (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
} // namespace detail

// Pure wire validator. Failure leaves output unchanged. Packet is an opaque
// XInput packet number; a zero gamepad (including a release) is a valid sample.
inline bool decode(const Context& context, DWORD expectedPid, uint64_t nonceTag,
                   uint64_t lastSeq, ULONGLONG now, std::span<const uint8_t> bytes,
                   Sample& output) noexcept {
    if (!context.desired || !expectedPid || context.slot >= XUSER_MAX_COUNT || bytes.size() != 104)
        return false;
    const auto* p = bytes.data();
    const uint32_t flags = detail::u32(p + 72);
    const uint64_t sequence = detail::u64(p + 16), tick = detail::u64(p + 24);
    if (detail::u32(p) != 0x31535256u || detail::u32(p + 4) != 1 ||
        detail::u32(p + 8) != 104 || detail::u32(p + 12) != ERROR_SUCCESS ||
        sequence <= lastSeq || tick > now || now - tick > 100 ||
        detail::u64(p + 32) != context.parentBirth || detail::u64(p + 40) != context.power ||
        detail::u64(p + 48) != context.topology || detail::u64(p + 56) != context.epoch ||
        detail::u32(p + 64) != context.slot || (flags & 3u) != 3u || (flags & ~7u) != 0 ||
        detail::u32(p + 76) != expectedPid || detail::u32(p + 92) >= XUSER_MAX_COUNT ||
        detail::u64(p + 96) != nonceTag)
        return false;
    Sample sample{};
    sample.sequence = sequence;
    sample.tick = tick;
    sample.readerPid = expectedPid;
    sample.actualSlot = detail::u32(p + 92);
    sample.packet = detail::u32(p + 68);
    sample.guide = (flags & 4u) != 0;
    // Offset 64 echoes the native slot; offset 92 is the reader process slot.
    // They may differ: exactPathBound is the device binding contract.
    sample.pad.wButtons = detail::u16(p + 80);
    sample.pad.bLeftTrigger = p[82];
    sample.pad.bRightTrigger = p[83];
    sample.pad.sThumbLX = std::bit_cast<int16_t>(detail::u16(p + 84));
    sample.pad.sThumbLY = std::bit_cast<int16_t>(detail::u16(p + 86));
    sample.pad.sThumbRX = std::bit_cast<int16_t>(detail::u16(p + 88));
    sample.pad.sThumbRY = std::bit_cast<int16_t>(detail::u16(p + 90));
    output = sample;
    return true;
}

class Bridge {
public:
    Bridge() {
        changed_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!changed_) { state_ = "failed-event"; return; }
        try {
            available_ = true;
            worker_ = std::thread([this] { workerMain(); });
        } catch (...) {
            available_ = false;
            state_ = "failed-worker";
        }
    }
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;

    // BUS-facing: no process, pipe, driver, discovery, or worker join here.
    void update(const Context& next) {
        std::lock_guard lock(mutex_);
        if (stopping_ || config_ == next) return;
        config_ = next;
        ++revision_;
        cached_ = false;
        delivered_ = 0;
        if (available_) state_ = next.desired ? "launching" : "inactive";
        if (changed_) SetEvent(changed_.value);
    }

    bool read(const Context& expected, ULONGLONG now, Sample& output) {
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock() || stopping_ || !cached_ || !expected.desired ||
            config_ != expected || cacheRevision_ != revision_)
            return false;
        // Receiving wire frames is strictly monotonic. Consumers may reuse an
        // equal-sequence cache until its ORIGINAL tick expires; this does not
        // prove new hardware freshness and must never refresh tick or sequence.
        Sample sample{};
        if (!decode(expected, readerPid_, nonceTag_, 0, now, cache_, sample) ||
            sample.sequence < delivered_) return false;
        delivered_ = sample.sequence;
        output = sample;
        return true;
    }

    void stop() noexcept {
        std::lock_guard stopLock(stopMutex_);
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            cached_ = false;
            ++revision_;
            state_ = "inactive";
            if (changed_) SetEvent(changed_.value);
        }
        if (worker_.joinable()) worker_.join();
        // update/read never use this handle after observing stopping_.
        changed_.reset();
    }
    ~Bridge() { stop(); }

    std::string state() {
        std::lock_guard lock(mutex_);
        if (state_ == "streaming") {
            const auto now = GetTickCount64();
            const auto tick = detail::u64(cache_.data() + 24);
            if (!cached_ || tick > now || now - tick > 100) return "stale";
        }
        return state_;
    }

private:
    struct ReadOperation {
        OVERLAPPED ov{};
        std::array<uint8_t, 104> bytes{};
        detail::Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        bool pending = false;
        ReadOperation() { ov.hEvent = event.value; }
    };
    struct Session {
        detail::Handle child, thread, job, pipe, parent;
        std::unique_ptr<ReadOperation> io;
        DWORD pid = 0;
        uint64_t nonce = 0;
        bool resumed = false;
        bool cleanupDone = false;
        bool clean = true;

        // All waits have explicit budgets. Only this Session's child can be
        // terminated. The job is assigned while the child is still suspended.
        bool cleanup() noexcept {
            if (cleanupDone) return clean;
            cleanupDone = true;
            if (pipe && io && io->pending) CancelIoEx(pipe.value, &io->ov);
            // Closing the client also lets a cooperative server see EOF.
            pipe.reset();
            if (child && WaitForSingleObject(child.value, resumed ? 100 : 0) != WAIT_OBJECT_0) {
                if (!resumed) TerminateProcess(child.value, ERROR_PROCESS_ABORTED);
                job.reset(); // KILL_ON_JOB_CLOSE is the running-child fallback.
                if (WaitForSingleObject(child.value, 1000) != WAIT_OBJECT_0) {
                    TerminateProcess(child.value, ERROR_PROCESS_ABORTED);
                    if (WaitForSingleObject(child.value, 1000) != WAIT_OBJECT_0) clean = false;
                }
            }
            job.reset();
            if (io && io->pending) {
                if (WaitForSingleObject(io->event.value, 1000) == WAIT_OBJECT_0) {
                    io->pending = false; // completion, including abort, is now observed
                } else {
                    // Windows cannot promise cancellation completion within any
                    // deadline. Never free a kernel-referenced OVERLAPPED/buffer.
                    // Release every HANDLE, retain only this allocation until
                    // process teardown, and permanently disable this Bridge.
                    io->event.reset();
                    (void)io.release();
                    clean = false;
                }
            }
            io.reset();
            thread.reset();
            child.reset();
            parent.reset();
            return clean;
        }
        ~Session() { cleanup(); }
    };

    bool current(uint64_t revision) {
        std::lock_guard lock(mutex_);
        return !stopping_ && revision_ == revision;
    }
    void status(uint64_t revision, const std::string& value, bool invalidate = false) {
        std::lock_guard lock(mutex_);
        if (stopping_ || revision_ != revision) return;
        state_ = value;
        if (invalidate) cached_ = false;
    }
    bool interrupted(DWORD milliseconds) noexcept {
        return WaitForSingleObject(changed_.value, milliseconds) != WAIT_TIMEOUT;
    }
    static std::string error(const char* stage, DWORD code) {
        return std::string("failed-") + stage + "-" + std::to_string(code);
    }
    bool launch(const Context& context, uint64_t revision, Session& session,
                std::wstring& pipePath, std::string& failure) {
        auto fail = [&](const char* stage, DWORD code) {
            failure = error(stage, code);
            return false;
        };
        if (context.parentPid != GetCurrentProcessId() || !context.parentBirth ||
            context.slot >= XUSER_MAX_COUNT || !detail::noNul(context.helperExe) ||
            !detail::noNul(context.dll) || !detail::noNul(context.devicePath) ||
            !std::filesystem::path(context.helperExe).is_absolute() ||
            !std::filesystem::path(context.dll).is_absolute())
            return fail("context", ERROR_INVALID_PARAMETER);
        session.parent.reset(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, context.parentPid));
        if (!session.parent || detail::birth(session.parent.value) != context.parentBirth ||
            WaitForSingleObject(session.parent.value, 0) != WAIT_TIMEOUT)
            return fail("parent-identity", ERROR_ACCESS_DENIED);
        std::array<uint8_t, 16> nonce{};
        const auto randomStatus = BCryptGenRandom(nullptr, nonce.data(),
            static_cast<ULONG>(nonce.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (randomStatus < 0) return fail("nonce", static_cast<DWORD>(randomStatus));
        constexpr wchar_t hex[] = L"0123456789abcdef";
        std::wstring nonceHex;
        for (auto byte : nonce) {
            nonceHex.push_back(hex[byte >> 4]);
            nonceHex.push_back(hex[byte & 15]);
        }
        session.nonce = detail::u64(nonce.data());
        const std::wstring name = L"ymcc-source-" + std::to_wstring(context.parentPid) + L"-" + nonceHex;
        pipePath = L"\\\\.\\pipe\\" + name;
        std::wstring command = detail::quote(context.helperExe);
        const std::array<std::wstring, 11> args{
            L"--physical-source-reader-v1", name, nonceHex,
            std::to_wstring(context.parentPid), std::to_wstring(context.parentBirth),
            context.dll, context.devicePath, std::to_wstring(context.power),
            std::to_wstring(context.topology), std::to_wstring(context.epoch), std::to_wstring(context.slot)
        };
        for (const auto& arg : args) { command.push_back(L' '); command += detail::quote(arg); }
        if (command.size() >= 32767) return fail("command-length", ERROR_BAD_LENGTH);
        if (!current(revision)) return false;
        status(revision, "launching", true);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION info{};
        const auto result = deckylaunch::create(context.helperExe, command, nullptr,
            std::filesystem::path(context.helperExe).parent_path().wstring(), startup, info);
        // Adopt immediately, including an exceptional creator failure that left
        // its own suspended child in PROCESS_INFORMATION.
        session.child.reset(info.hProcess);
        session.thread.reset(info.hThread);
        session.pid = info.dwProcessId;
        if (!result.created) {
            failure = "failed-launch-" + result.stage + "-" + std::to_string(result.error);
            return false;
        }
        if (!session.child || !session.thread || !session.pid)
            return fail("child-handles", ERROR_INVALID_HANDLE);
        if (!current(revision)) return false;
        session.job.reset(CreateJobObjectW(nullptr, nullptr));
        if (!session.job) return fail("job-create", GetLastError());
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(session.job.value, JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits)))
            return fail("job-limits", GetLastError());
        if (!AssignProcessToJobObject(session.job.value, session.child.value))
            return fail("job-assign", GetLastError());
        session.io = std::make_unique<ReadOperation>();
        if (!session.io->event) return fail("read-event", GetLastError());
        // Serialize ResumeThread against OFF/identity changes. No wait or
        // process creation occurs while holding the BUS-facing mutex.
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || revision_ != revision) return false;
            const DWORD previous = ResumeThread(session.thread.value);
            if (previous != 1) return fail("resume", previous == DWORD(-1) ? GetLastError() : ERROR_INVALID_STATE);
            session.resumed = true;
            state_ = "connecting";
        }
        session.thread.reset();
        return true;
    }

    bool connect(uint64_t revision, const std::wstring& path, Session& session,
                 std::string& failure) {
        const ULONGLONG deadline = GetTickCount64() + 5000;
        const HANDLE waits[]{changed_.value, session.child.value, session.parent.value};
        while (current(revision)) {
            if (WaitForMultipleObjects(3, waits, FALSE, 0) != WAIT_TIMEOUT) return false;
            session.pipe.reset(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
            if (session.pipe) {
                ULONG serverPid = 0;
                if (!GetNamedPipeServerProcessId(session.pipe.value, &serverPid) || serverPid != session.pid) {
                    failure = error("pipe-server-pid", ERROR_ACCESS_DENIED);
                    return false;
                }
                return current(revision);
            }
            const auto code = GetLastError();
            if (code != ERROR_PIPE_BUSY && code != ERROR_FILE_NOT_FOUND) {
                failure = error("pipe-open", code);
                return false;
            }
            const auto now = GetTickCount64();
            if (now >= deadline) { failure = "failed-connect-timeout"; return false; }
            const DWORD delay = static_cast<DWORD>((deadline - now) < 25 ? deadline - now : 25);
            const DWORD waited = WaitForMultipleObjects(3, waits, FALSE, delay);
            if (waited != WAIT_TIMEOUT) { failure = "failed-connect-exit"; return false; }
        }
        return false;
    }

    void stream(const Context& context, uint64_t revision, Session& session, std::string& failure) {
        auto& io = *session.io;
        uint64_t lastSequence = 0;
        DWORD used = 0;
        ULONGLONG frameDeadline = GetTickCount64() + 1000;
        const HANDLE waits[]{changed_.value, io.event.value, session.child.value, session.parent.value};
        while (current(revision)) {
            if (GetTickCount64() >= frameDeadline) {
                failure = used ? "failed-partial-timeout" : "failed-read-timeout";
                return;
            }
            ResetEvent(io.event.value);
            io.ov = {};
            io.ov.hEvent = io.event.value;
            DWORD count = 0;
            const BOOL done = ReadFile(session.pipe.value, io.bytes.data() + used,
                static_cast<DWORD>(io.bytes.size()) - used, &count, &io.ov);
            if (!done) {
                const auto code = GetLastError();
                if (code != ERROR_IO_PENDING) { failure = error("read", code); return; }
                io.pending = true;
                for (;;) {
                    const auto now = GetTickCount64();
                    if (now >= frameDeadline) {
                        failure = used ? "failed-partial-timeout" : "failed-read-timeout";
                        return;
                    }
                    const DWORD timeout = static_cast<DWORD>(frameDeadline - now);
                    const DWORD waited = WaitForMultipleObjects(4, waits, FALSE, timeout);
                    if (waited == WAIT_TIMEOUT) continue;
                    if (waited != WAIT_OBJECT_0 + 1) { failure = "failed-read-cancel-or-exit"; return; }
                    const BOOL completed = GetOverlappedResult(session.pipe.value, &io.ov, &count, FALSE);
                    const DWORD completionError = completed ? ERROR_SUCCESS : GetLastError();
                    if (!completed && completionError == ERROR_IO_INCOMPLETE) continue;
                    io.pending = false;
                    if (!completed) { failure = error("read-completion", completionError); return; }
                    break;
                }
            }
            if (!count || count > io.bytes.size() - used) { failure = "failed-frame-length"; return; }
            used += count;
            if (used != io.bytes.size()) continue;
            Sample sample{};
            if (!decode(context, session.pid, session.nonce, lastSequence, GetTickCount64(), io.bytes, sample)) {
                failure = "failed-frame-validation";
                return;
            }
            {
                std::lock_guard lock(mutex_);
                if (stopping_ || revision_ != revision || config_ != context) return;
                cache_ = io.bytes;
                cacheRevision_ = revision;
                readerPid_ = session.pid;
                nonceTag_ = session.nonce;
                cached_ = true;
                state_ = "streaming";
            }
            lastSequence = sample.sequence;
            used = 0;
            frameDeadline = GetTickCount64() + 1000;
        }
    }

    void workerMain() noexcept {
        Context budgetContext;
        bool haveBudget = false;
        unsigned attempts = 0;
        for (;;) {
            uint64_t revision = 0;
            try {
                Context context;
                {
                    std::lock_guard lock(mutex_);
                    if (stopping_) return;
                    context = config_;
                    revision = revision_;
                    ResetEvent(changed_.value);
                }
                if (!haveBudget || context != budgetContext) {
                    budgetContext = context;
                    haveBudget = true;
                    attempts = 0;
                }
                if (!context.desired) {
                    status(revision, "inactive", true);
                } else {
                    while (attempts < 2 && current(revision)) {
                        if (attempts && interrupted(250)) break;
                        if (!current(revision)) break;
                        ++attempts;
                        Session session;
                        std::wstring pipePath;
                        std::string failure = "failed-child-exit";
                        try {
                            if (launch(context, revision, session, pipePath, failure) &&
                                connect(revision, pipePath, session, failure))
                                stream(context, revision, session, failure);
                        } catch (...) {
                            failure = "failed-exception";
                        }
                        // Capture an already-exited reader's original status
                        // BEFORE job close/TerminateProcess can supply our own
                        // cleanup code. Never report STILL_ACTIVE as an exit.
                        if (session.resumed && session.child &&
                            WaitForSingleObject(session.child.value, 0) == WAIT_OBJECT_0) {
                            DWORD exitCode = STILL_ACTIVE;
                            if (GetExitCodeProcess(session.child.value, &exitCode) && exitCode != STILL_ACTIVE)
                                failure += "-child-exit-" + std::to_string(exitCode);
                        }
                        // Invalidate before any cleanup wait, including a stale
                        // cached frame from a failed/closed reader.
                        status(revision, failure, true);
                        if (!session.cleanup()) {
                            std::lock_guard lock(mutex_);
                            cached_ = false;
                            available_ = false;
                            state_ = "failed-cleanup-unconfirmed";
                            return; // no retries after unconfirmed kernel completion
                        }
                    }
                }
                if (!current(revision)) continue;
            } catch (...) {
                // An allocation failure must not turn identical updates into
                // an unbounded retry loop either.
                attempts = 2;
                try { status(revision, "failed-worker-exception", true); } catch (...) {}
            }
            if (WaitForSingleObject(changed_.value, INFINITE) != WAIT_OBJECT_0) return;
        }
    }

    std::mutex mutex_, stopMutex_;
    Context config_;
    uint64_t revision_ = 0, cacheRevision_ = 0, delivered_ = 0, nonceTag_ = 0;
    DWORD readerPid_ = 0;
    bool stopping_ = false, available_ = false, cached_ = false;
    std::string state_ = "inactive";
    std::array<uint8_t, 104> cache_{};
    detail::Handle changed_;
    std::thread worker_;
};
} // namespace ymcc::physicalreader
