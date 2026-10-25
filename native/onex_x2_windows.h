#pragma once
#include "onex_x2_session.h"
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <string>
#include <vector>

// Single-owner, process-lifetime transport for the three new OneX models only.
// The owner calls request/tick/popEvent on its existing input tick. No workers,
// blocking wait, WMI/EC writes, registration or physical gamepad HID opens.
namespace ymcc::onex_x2 {
class WindowsTransport final {
public:
    WindowsTransport() = default;
    WindowsTransport(const WindowsTransport&) = delete;
    WindowsTransport& operator=(const WindowsTransport&) = delete;
    WindowsTransport(WindowsTransport&&) = delete;
    WindowsTransport& operator=(WindowsTransport&&) = delete;
    void request(Model model, Context context, InputLayout layout, std::uint64_t now) noexcept {
        if (layout != layout_) session_.stop(StopReason::SelectionChanged, now);
        session_.request((layout == InputLayout::ReportIdThenFrame || layout == InputLayout::ZeroReportIdThenFrame) ? model : Model::Unsupported, context, now); layout_ = layout;
    }
    void stop(std::uint64_t now) noexcept { session_.request(Model::Unsupported, session_.snapshot().context, now); cancel(); finalize(); }
    // Retirement only: the existing owner serializes this with request/stop/tick.
    // Each call observes at most two operations without waiting or binding.
    void pollRetirement(std::uint64_t now) noexcept {
        if (session_.snapshot().phase == Phase::Live) return;
        cancel();
        service(read_, readOv_, readEvent_, true, now);
        service(write_, writeOv_, writeEvent_, false, now);
        session_.advance(now); cancel(); finalize();
    }
    // Pure resource-completion observation under the caller's owner lock.
    // Detached alone cannot certify failed closes or retained operations.
    bool stopped() const noexcept {
        return session_.snapshot().phase == Phase::Detached && !read_ && !write_ &&
            device_ == INVALID_HANDLE_VALUE && !readEvent_ && !writeEvent_;
    }
    void tick(std::uint64_t now, bool (*writePermission)() = nullptr, bool (*bindPermission)(const std::wstring&) = nullptr) {
        // Current selection/context retires old operations before their callbacks
        // can publish. CancelIoEx success is never treated as terminal completion.
        cancel();
        service(read_, readOv_, readEvent_, true, now);
        service(write_, writeOv_, writeEvent_, false, now);
        session_.advance(now); cancel(); finalize();
        const auto state = session_.snapshot();
        if (state.phase == Phase::Detached && stopped() && session_.canBind(now) &&
            (!bindAttempted_ || now >= lastBindAttempt_ + Session::bindRetryMs)) {
            bindAttempted_ = true; lastBindAttempt_ = now;
            bind(now, bindPermission);
        }
        if (session_.snapshot().phase != Phase::Live) return;
        if (!read_) startRead(now);
        if (session_.snapshot().phase == Phase::Live && !write_) startWrite(now, writePermission);
        cancel(); finalize();
    }
    Snapshot snapshot() const noexcept { return session_.snapshot(); }
    std::optional<Event> popEvent() noexcept { return session_.popEvent(); }
    DWORD lastError() const noexcept { return lastError_; }
    std::uint64_t bindAttempts() const noexcept { return bindAttempts_; }
    bool hasDevice() const noexcept { return device_ != INVALID_HANDLE_VALUE; }
    const std::wstring& devicePath() const noexcept { return devicePath_; }
private:
    void cancel() noexcept {
        const auto wanted = session_.takeCancellation();
        if (wanted.read && read_ && device_ != INVALID_HANDLE_VALUE) CancelIoEx(device_, &readOv_);
        if (wanted.write && write_ && device_ != INVALID_HANDLE_VALUE) CancelIoEx(device_, &writeOv_);
    }
    void service(std::optional<Operation>& operation, OVERLAPPED& overlap,
                 HANDLE event, bool reading, std::uint64_t now) noexcept {
        if (!operation) return;
        const DWORD waited = event ? WaitForSingleObject(event, 0) : WAIT_FAILED;
        if (waited != WAIT_OBJECT_0 && !HasOverlappedIoCompleted(&overlap)) {
            if (waited != WAIT_TIMEOUT) { lastError_ = ERROR_INVALID_HANDLE; session_.stop(StopReason::ObservationFailed, now); }
            return;
        }
        DWORD bytes = 0;
        const bool success = GetOverlappedResult(device_, &overlap, &bytes, FALSE) != FALSE;
        const DWORD error = success ? ERROR_SUCCESS : GetLastError();
        if (!success && (error == ERROR_IO_INCOMPLETE || !HasOverlappedIoCompleted(&overlap))) {
            if (error != ERROR_IO_INCOMPLETE) { lastError_ = error; session_.stop(StopReason::ObservationFailed, now); }
            return;
        }
        const auto completed = *operation; operation.reset();
        if (!success) lastError_ = error;
        if (reading) (void)session_.completeRead(completed, success, bytes, now);
        else (void)session_.completeWrite(completed, success, bytes, now);
    }
    void closeQuiescent() noexcept {
        if (read_ || write_) return;
        if (device_ != INVALID_HANDLE_VALUE) {
            if (CloseHandle(device_)) device_ = INVALID_HANDLE_VALUE;
            else lastError_ = GetLastError();
        }
        if (readEvent_) {
            if (CloseHandle(readEvent_)) readEvent_ = nullptr;
            else lastError_ = GetLastError();
        }
        if (writeEvent_) {
            if (CloseHandle(writeEvent_)) writeEvent_ = nullptr;
            else lastError_ = GetLastError();
        }
        if (device_ == INVALID_HANDLE_VALUE && !readEvent_ && !writeEvent_) {
            readOv_ = {}; writeOv_ = {}; devicePath_.clear();
        }
    }
    void finalize() noexcept {
        if (session_.snapshot().phase == Phase::Detached) closeQuiescent();
    }
    bool contractFor(HANDLE handle, Model model, TransportContract& contract) const noexcept {
        HIDD_ATTRIBUTES attributes{}; attributes.Size = sizeof(attributes);
        PHIDP_PREPARSED_DATA preparsed = nullptr; HIDP_CAPS caps{};
        const bool attributesOk = HidD_GetAttributes(handle, &attributes) != FALSE;
        const bool prepared = HidD_GetPreparsedData(handle, &preparsed) != FALSE;
        const bool capsOk = prepared && HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS;
        const bool matches = attributesOk && capsOk && matchesCollection(model, attributes.VendorID, attributes.ProductID, caps.UsagePage, caps.Usage) &&
            caps.InputReportByteLength >= 15 && caps.InputReportByteLength <= Session::maxInputBytes &&
            (caps.OutputReportByteLength == 64 || caps.OutputReportByteLength == 65);
        if (matches) contract = {layout_, caps.InputReportByteLength, caps.OutputReportByteLength};
        if (preparsed) HidD_FreePreparsedData(preparsed);
        return matches;
    }
    void bind(std::uint64_t now, bool (*bindPermission)(const std::wstring&)) {
        ++bindAttempts_;
        if ((layout_ != InputLayout::ReportIdThenFrame && layout_ != InputLayout::ZeroReportIdThenFrame) || device_ != INVALID_HANDLE_VALUE || read_ || write_) return;
        GUID guid{}; HidD_GetHidGuid(&guid);
        HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) { lastError_ = GetLastError(); return; }
        std::wstring selected;
        TransportContract contract{};
        const Model model = session_.snapshot().requested;
        for (DWORD index = 0; selected.empty(); ++index) {
            SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
            if (!SetupDiEnumDeviceInterfaces(set, nullptr, &guid, index, &iface)) break;
            DWORD bytes = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &bytes, nullptr);
            if (bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || bytes > 65536) continue;
            std::vector<BYTE> storage(bytes);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, bytes, nullptr, nullptr)) continue;
            HANDLE probe = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (probe == INVALID_HANDLE_VALUE) continue;
            if (contractFor(probe, model, contract)) selected = detail->DevicePath;
            if (!CloseHandle(probe)) {
                lastError_ = GetLastError();
                device_ = probe; // Retain the failed probe close in the same owner slot.
                break;
            }
        }
        SetupDiDestroyDeviceInfoList(set);
        if (device_ != INVALID_HANDLE_VALUE || selected.empty()) return;
        // Register the exact candidate before opening it. The application retains
        // relevant notifications observed while enumeration had no known path.
        if (bindPermission && !bindPermission(selected)) { lastError_ = ERROR_OPERATION_ABORTED; return; }
        device_ = CreateFileW(selected.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if (device_ == INVALID_HANDLE_VALUE) { lastError_ = GetLastError(); return; }
        // Probe identity can race removal/replacement. Check the actual owner
        // handle again before any input, initialization write or event allocation.
        TransportContract reopened{};
        if (!contractFor(device_, model, reopened) || reopened.inputBytes != contract.inputBytes || reopened.outputBytes != contract.outputBytes) {
            lastError_ = ERROR_INVALID_DATA; closeQuiescent(); return;
        }
        if (bindPermission && !bindPermission(selected)) { lastError_ = ERROR_OPERATION_ABORTED; closeQuiescent(); return; }
        readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        writeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!readEvent_ || !writeEvent_ || !session_.bind(contract, now)) { lastError_ = GetLastError(); closeQuiescent(); return; }
        devicePath_ = std::move(selected);
        lastError_ = ERROR_SUCCESS;
    }
    void startRead(std::uint64_t now) noexcept {
        const auto operation = session_.beginRead();
        if (!operation) return;
        readOv_ = {}; readOv_.hEvent = readEvent_; read_ = operation;
        if (!ResetEvent(readEvent_)) { lastError_ = GetLastError(); const auto failed = *read_; read_.reset(); (void)session_.completeRead(failed, false, 0, now); return; }
        DWORD bytes = 0;
        if (ReadFile(device_, session_.readBuffer(), static_cast<DWORD>(session_.readSize()), &bytes, &readOv_)) {
            const auto completed = *read_; read_.reset(); (void)session_.completeRead(completed, true, bytes, now);
        } else if (GetLastError() != ERROR_IO_PENDING) {
            lastError_ = GetLastError(); const auto completed = *read_; read_.reset();
            (void)session_.completeRead(completed, false, 0, now);
        }
    }
    void startWrite(std::uint64_t now, bool (*writePermission)()) noexcept {
        if (writePermission && !writePermission()) return;
        const auto operation = session_.beginWrite(now);
        if (!operation) return;
        writeOv_ = {}; writeOv_.hEvent = writeEvent_; write_ = operation;
        if (!ResetEvent(writeEvent_)) { lastError_ = GetLastError(); const auto failed = *write_; write_.reset(); (void)session_.completeWrite(failed, false, 0, now); return; }
        DWORD bytes = 0;
        if (writePermission && !writePermission()) {
            const auto withdrawn = *write_; write_.reset(); (void)session_.discardPreparedWrite(withdrawn); return;
        }
        if (WriteFile(device_, session_.writeBuffer(), static_cast<DWORD>(session_.writeSize()), &bytes, &writeOv_)) {
            const auto completed = *write_; write_.reset(); (void)session_.completeWrite(completed, true, bytes, now);
        } else if (GetLastError() != ERROR_IO_PENDING) {
            lastError_ = GetLastError(); const auto completed = *write_; write_.reset();
            (void)session_.completeWrite(completed, false, 0, now);
        }
    }
    Session session_{};
    std::wstring devicePath_;
    HANDLE device_ = INVALID_HANDLE_VALUE, readEvent_ = nullptr, writeEvent_ = nullptr;
    OVERLAPPED readOv_{}, writeOv_{};
    std::optional<Operation> read_, write_;
    InputLayout layout_ = InputLayout::Unverified;
    DWORD lastError_ = ERROR_SUCCESS;
    bool bindAttempted_ = false;
    std::uint64_t lastBindAttempt_ = 0, bindAttempts_ = 0;
};
} // namespace ymcc::onex_x2
