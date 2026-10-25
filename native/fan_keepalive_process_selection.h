#pragma once
#include <windows.h>
#include <utility>

namespace ymcc::fan_process {
struct ProcessApi {
    using Handle = HANDLE;
    static Handle open(DWORD pid) noexcept {
        return OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    }
    static bool alive(Handle process) noexcept {
        return WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }
    static void close(Handle process) noexcept { CloseHandle(process); }
};

// Thread-confined selection only, never a cached authorization decision.
// The keepalive worker owns this object until idle/exit. Every use rechecks
// the same process object's lifetime and invokes the current image/port gates.
// A failed open/validation falls back to the original complete inventory.
template<class Api = ProcessApi>
class KeepaliveSelection {
    typename Api::Handle process_{};
    DWORD pid_ = 0;
public:
    KeepaliveSelection() = default;
    ~KeepaliveSelection() { reset(); }
    KeepaliveSelection(const KeepaliveSelection&) = delete;
    KeepaliveSelection& operator=(const KeepaliveSelection&) = delete;
    void reset() noexcept {
        if (process_) Api::close(std::exchange(process_, {}));
        pid_ = 0;
    }
    void remember(DWORD pid) noexcept {
        reset();
        if (!pid) return;
        auto process = Api::open(pid);
        if (!process) return;
        if (!Api::alive(process)) { Api::close(process); return; }
        process_ = process;
        pid_ = pid;
    }
    template<class Validate>
    bool matches(Validate&& validate) {
        if (!process_) return false;
        try {
            if (Api::alive(process_) && validate(pid_, process_) && Api::alive(process_))
                return true;
        } catch (...) {
            reset();
            throw;
        }
        reset();
        return false;
    }
};
} // namespace ymcc::fan_process