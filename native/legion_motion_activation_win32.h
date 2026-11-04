#pragma once
#include "legion_motion_activation.h"
#include <algorithm>

namespace ymcc::legion_activation {
// The capture owner duplicates its already admitted overlapped vendor handle.
// Read and write use separate OVERLAPPED/event/buffer storage. A cancelled write
// retains this process-lifetime slot until GetOverlappedResult is terminal.
class Win32Port {
public:
    bool open(HANDLE source, unsigned reportBytes) {
        error = ERROR_SUCCESS;
        if (source == INVALID_HANDLE_VALUE || source == nullptr || reportBytes == 0 || reportBytes > buffer_.size()) {
            error = ERROR_INVALID_PARAMETER; return false;
        }
        if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &device_, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            error = GetLastError(); return false;
        }
        event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event_) { error = GetLastError(); release(); return false; }
        bytes_ = reportBytes; return true;
    }
    template<class Command> Completion submit(const Command& command) {
        if (!command.length || command.length > command.bytes.size() || command.length > bytes_)
            return fail(ERROR_INVALID_PARAMETER);
        buffer_.fill(0); std::copy_n(command.bytes.begin(), command.length, buffer_.begin());
        ZeroMemory(&operation_, sizeof(operation_)); operation_.hEvent = event_;
        ResetEvent(event_);
        // Overlapped completion owns the byte count, including immediate success.
        if (WriteFile(device_, buffer_.data(), bytes_, nullptr, &operation_)) return poll();
        error = GetLastError();
        return error == ERROR_IO_PENDING ? Completion::Pending : Completion::Failed;
    }
    Completion poll() {
        DWORD written = 0;
        if (GetOverlappedResult(device_, &operation_, &written, FALSE)) {
            error = ERROR_SUCCESS;
            return written == bytes_ ? Completion::Success : fail(ERROR_WRITE_FAULT);
        }
        error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE || !HasOverlappedIoCompleted(&operation_)) return Completion::Pending;
        return Completion::Failed;
    }
    void cancel() { (void)CancelIoEx(device_, &operation_); }
    bool release() {
        if (device_ != INVALID_HANDLE_VALUE) {
            if (!CloseHandle(device_)) { error = GetLastError(); return false; }
            device_ = INVALID_HANDLE_VALUE;
        }
        if (event_) {
            if (!CloseHandle(event_)) { error = GetLastError(); return false; }
            event_ = nullptr;
        }
        bytes_ = 0; return true;
    }
    DWORD error = ERROR_SUCCESS;
private:
    Completion fail(DWORD value) { error = value; return Completion::Failed; }
    HANDLE device_ = INVALID_HANDLE_VALUE, event_ = nullptr;
    OVERLAPPED operation_{};
    std::array<std::uint8_t, 65> buffer_{};
    DWORD bytes_ = 0;
};
} // namespace ymcc::legion_activation
