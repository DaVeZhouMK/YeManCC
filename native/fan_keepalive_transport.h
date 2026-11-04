#pragma once
#include <windows.h>
#include <winhttp.h>
#include <cstdint>
#include <utility>
namespace ymcc::fan_http {
// Existing single keepalive worker owns only transport resources. Credentials,
// process/port, lease and response admission remain fresh for every request.
class KeepaliveTransport {
    HINTERNET session_ = nullptr, connection_ = nullptr;
    INTERNET_PORT port_ = 0;
    std::uint64_t owner_ = 0;
public:
    ~KeepaliveTransport() { reset(); }
    KeepaliveTransport() = default;
    KeepaliveTransport(const KeepaliveTransport&) = delete;
    KeepaliveTransport& operator=(const KeepaliveTransport&) = delete;
    void reset() noexcept {
        if (connection_) WinHttpCloseHandle(std::exchange(connection_, nullptr));
        if (session_) WinHttpCloseHandle(std::exchange(session_, nullptr));
        port_ = 0; owner_ = 0;
    }
    HINTERNET connect(INTERNET_PORT port, std::uint64_t owner) noexcept {
        // Zero means no retained live process-object binding, including FI.
        if (!owner || owner != owner_ || port != port_) reset();
        if (connection_) return connection_;
        session_ = WinHttpOpen(L"YeManCC-FanKeepalive/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        if (!session_) return nullptr;
        if (!WinHttpSetTimeouts(session_, 650, 650, 650, 650)) { reset(); return nullptr; }
        connection_ = WinHttpConnect(session_, L"127.0.0.1", port, 0);
        if (!connection_) { reset(); return nullptr; }
        port_ = port; owner_ = owner;
        return connection_;
    }
};
inline KeepaliveTransport& currentThreadTransport() {
    static thread_local KeepaliveTransport transport;
    return transport;
}
// Failure/exception retires the connection even if a later diagnostic throws.
struct RetireFailedTransport {
    KeepaliveTransport& transport;
    bool accepted = false;
    ~RetireFailedTransport() { if (!accepted) transport.reset(); }
};
} // namespace ymcc::fan_http
