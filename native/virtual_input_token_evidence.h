#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(_MSC_VER)
#pragma comment(lib, "advapi32.lib")
#endif

namespace ymcc::virtualInputEvidence {

// value is meaningful only when known=true, in which case error=ERROR_SUCCESS.
// UNKNOWN uses known=false, a Win32 error, and value=0 as an ignored placeholder.
// Errors are from the failed API, or ERROR_INVALID_DATA/ERROR_INVALID_SID for
// malformed returned data; a failed API without an error uses ERROR_GEN_FAILURE.
struct TokenValue {
    bool known = false;
    DWORD error = ERROR_NOT_READY;
    DWORD value = 0;
};

struct ProcessTokenEvidence {
    DWORD processId = 0;        // GetCurrentProcessId(); always available.
    DWORD openError = ERROR_NOT_READY; // OpenProcessToken error, or ERROR_SUCCESS.
    TokenValue elevation;      // TokenElevation.TokenIsElevated: raw DWORD, 0/nonzero.
    TokenValue elevationType;  // TOKEN_ELEVATION_TYPE: Default=1, Full=2, Limited=3.
    TokenValue integrityRid;   // Last subauthority of the validated integrity SID.
    TokenValue uiAccess;       // TokenUIAccess: raw DWORD, 0/nonzero.
    TokenValue isAppContainer; // TokenIsAppContainer: raw DWORD, 0/nonzero.
    TokenValue sessionId;      // TokenSessionId: raw DWORD; zero can be a known value.
};

namespace detail {
struct LastErrorScope {
    DWORD saved = ::GetLastError();
    ~LastErrorScope() noexcept { ::SetLastError(saved); }
};

struct TokenHandle {
    HANDLE value = nullptr;
    TokenHandle() noexcept = default;
    ~TokenHandle() noexcept { if (value) ::CloseHandle(value); }
    TokenHandle(const TokenHandle&) = delete;
    TokenHandle& operator=(const TokenHandle&) = delete;
};

inline DWORD queryError() noexcept {
    const DWORD error = ::GetLastError();
    return error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
}

template <typename T>
inline bool queryFixed(HANDLE token, TOKEN_INFORMATION_CLASS informationClass,
                       T& value, TokenValue& evidence) noexcept {
    T observed{};
    DWORD returned = 0;
    ::SetLastError(ERROR_SUCCESS);
    if (!::GetTokenInformation(token, informationClass, &observed,
                               static_cast<DWORD>(sizeof(observed)), &returned)) {
        evidence.error = queryError();
        return false;
    }
    if (returned != sizeof(observed)) {
        evidence.error = ERROR_INVALID_DATA;
        return false;
    }
    value = observed;
    evidence.known = true;
    evidence.error = ERROR_SUCCESS;
    return true;
}

inline TokenValue queryIntegrityRid(HANDLE token) noexcept {
    // One fixed buffer holds the label and the largest SID supported by the SDK.
    // No size probe, allocation, retry, SID string conversion, or account lookup.
    alignas(TOKEN_MANDATORY_LABEL)
        std::array<unsigned char, sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE> buffer{};
    DWORD returned = 0;
    ::SetLastError(ERROR_SUCCESS);
    if (!::GetTokenInformation(token, TokenIntegrityLevel, buffer.data(),
                               static_cast<DWORD>(buffer.size()), &returned)) {
        return {false, queryError(), 0};
    }
    if (returned < sizeof(TOKEN_MANDATORY_LABEL) || returned > buffer.size()) {
        return {false, ERROR_INVALID_DATA, 0};
    }

    TOKEN_MANDATORY_LABEL label{};
    std::memcpy(&label, buffer.data(), sizeof(label));
    const auto base = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto address = reinterpret_cast<std::uintptr_t>(label.Label.Sid);
    if (!label.Label.Sid || address < base) return {false, ERROR_INVALID_SID, 0};
    const auto offset = address - base;
    constexpr std::size_t sidHeaderBytes = offsetof(SID, SubAuthority);
    // Check the pointer, alignment, and readable header before dereferencing a SID.
    if (offset < sizeof(label) || offset > returned ||
        returned - offset < sidHeaderBytes || address % alignof(SID) != 0) {
        return {false, ERROR_INVALID_SID, 0};
    }
    SID header{};
    std::memcpy(&header, buffer.data() + offset, sidHeaderBytes);
    if (header.SubAuthorityCount == 0 || header.SubAuthorityCount > SID_MAX_SUB_AUTHORITIES) {
        return {false, ERROR_INVALID_SID, 0};
    }
    const std::size_t sidBytes = sidHeaderBytes +
        static_cast<std::size_t>(header.SubAuthorityCount) * sizeof(DWORD);
    if (sidBytes > SECURITY_MAX_SID_SIZE || sidBytes > returned - offset) {
        return {false, ERROR_INVALID_SID, 0};
    }
    // The complete SID is now inside the returned buffer, including its last RID.
    PSID sid = label.Label.Sid;
    if (!::IsValidSid(sid)) return {false, ERROR_INVALID_SID, 0};
    const PUCHAR count = ::GetSidSubAuthorityCount(sid);
    if (!count || *count != header.SubAuthorityCount) return {false, ERROR_INVALID_SID, 0};
    const PDWORD rid = ::GetSidSubAuthority(sid, static_cast<DWORD>(*count) - 1);
    if (!rid) return {false, ERROR_INVALID_SID, 0};
    return {true, ERROR_SUCCESS, *rid};
}
} // namespace detail

// Evidence only: this does not establish why an input collector was filtered.
// Reads this process's primary token, not an impersonating thread or a target's
// token. It changes no token, privilege, process state, or device state.
// Work is bounded: one TOKEN_QUERY open, at most six GetTokenInformation calls,
// fixed stack storage, no retries, no heap, no background state. The sole owned
// handle is closed on every path; GetCurrentProcess() is a borrowed pseudo handle.
// Each invocation is a fresh snapshot, not an atomic view across all queries.
// The main-thread integrator should call once and keep:
//   static const auto token = ymcc::virtualInputEvidence::processTokenEvidence();
// A cached snapshot describes that first call, not live state at each log sample.
inline ProcessTokenEvidence processTokenEvidence() noexcept {
    const detail::LastErrorScope preserveLastError;
    ProcessTokenEvidence out{};
    out.processId = ::GetCurrentProcessId();
    detail::TokenHandle token;
    ::SetLastError(ERROR_SUCCESS);
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token.value)) {
        const DWORD error = detail::queryError();
        out.openError = error;
        out.elevation.error = error;
        out.elevationType.error = error;
        out.integrityRid.error = error;
        out.uiAccess.error = error;
        out.isAppContainer.error = error;
        out.sessionId.error = error;
        return out;
    }

    out.openError = ERROR_SUCCESS;
    TOKEN_ELEVATION elevation{};
    if (detail::queryFixed(token.value, TokenElevation, elevation, out.elevation)) {
        out.elevation.value = elevation.TokenIsElevated;
    }
    TOKEN_ELEVATION_TYPE elevationType{};
    if (detail::queryFixed(token.value, TokenElevationType, elevationType, out.elevationType)) {
        out.elevationType.value = static_cast<DWORD>(elevationType);
    }
    out.integrityRid = detail::queryIntegrityRid(token.value);
    detail::queryFixed(token.value, TokenUIAccess, out.uiAccess.value, out.uiAccess);
    detail::queryFixed(token.value, TokenIsAppContainer, out.isAppContainer.value, out.isAppContainer);
    detail::queryFixed(token.value, TokenSessionId, out.sessionId.value, out.sessionId);
    return out;
}

} // namespace ymcc::virtualInputEvidence
