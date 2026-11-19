#pragma once

// Private fixture API leaves only. Include after Windows/appmodel/string/vector
// headers and before the three API-name macros and steam_information_client.h.
// PackageFullNameFromId stays the real SDK API. This is not MSIX evidence.
namespace HomePackageAdapter {
inline std::wstring mode = L"normal", packageRoot;
inline DWORD admittedPid = 0;
inline unsigned idCalls = 0, fullCalls = 0, pathCalls = 0;

namespace detail {
struct Text {
    PCWSTR data;
    UINT32 chars; // Includes NUL; all identity inputs below are bounded literals.
};
template <std::size_t N>
inline constexpr Text literal(const wchar_t (&value)[N]) noexcept {
    return {value, static_cast<UINT32>(N)};
}
struct Identity {
    Text name = literal(L"YeManCC.GamingHome");
    Text publisher = literal(L"CN=YeManCC Gaming Home");
    Text resource = literal(L"");
    Text publisherId = literal(L"8wekyb3d8bbwe");
    UINT32 architecture = PROCESSOR_ARCHITECTURE_AMD64;

    PACKAGE_ID package() const noexcept {
        PACKAGE_ID result{};
        result.processorArchitecture = architecture;
        result.version.Major = 1;
        // The SDK reads these strings; the casts do not grant write access.
        result.name = const_cast<PWSTR>(name.data);
        result.publisher = const_cast<PWSTR>(publisher.data);
        result.resourceId = const_cast<PWSTR>(resource.data);
        result.publisherId = const_cast<PWSTR>(publisherId.data);
        return result;
    }
    UINT32 bytes() const noexcept {
        return static_cast<UINT32>(sizeof(PACKAGE_ID)) +
            (name.chars + publisher.chars + resource.chars + publisherId.chars) *
            static_cast<UINT32>(sizeof(wchar_t));
    }
};
inline Identity identity() noexcept {
    Identity value;
    if (mode == L"wrong-name") value.name = literal(L"YeManCC.OtherHome");
    if (mode == L"wrong-publisher") value.publisher = literal(L"CN=Other Publisher");
    if (mode == L"wrong-arch") value.architecture = PROCESSOR_ARCHITECTURE_INTEL;
    if (mode == L"resource-package") value.resource = literal(L"resources");
    return value;
}
inline LSTATUS processStatus(HANDLE process) noexcept {
    const DWORD actualPid = ::GetProcessId(process);
    if (!actualPid) {
        const DWORD error = ::GetLastError();
        return error ? static_cast<LSTATUS>(error) : ERROR_INVALID_HANDLE;
    }
    if (!admittedPid || actualPid != admittedPid) return ERROR_ACCESS_DENIED;
    return mode == L"unpackaged" ? APPMODEL_ERROR_NO_PACKAGE : ERROR_SUCCESS;
}
// A null output is a size query only with zero input capacity. Short buffers
// receive the required count, with no partial writes. Invalid inputs are untouched.
inline LSTATUS capacity(UINT32* count, const void* output, UINT32 required) noexcept {
    if (!count || (!output && *count != 0)) return ERROR_INVALID_PARAMETER;
    const UINT32 available = *count;
    *count = required;
    return available < required ? ERROR_INSUFFICIENT_BUFFER : ERROR_SUCCESS;
}
inline PWSTR append(BYTE* output, UINT32& offset, Text value) noexcept {
    auto* address = reinterpret_cast<PWSTR>(output + offset);
    const auto* source = reinterpret_cast<const BYTE*>(value.data);
    const UINT32 bytes = value.chars * static_cast<UINT32>(sizeof(wchar_t));
    for (UINT32 i = 0; i < bytes; ++i) output[offset + i] = source[i];
    offset += bytes;
    return address;
}
inline LSTATUS derivedFullName(wchar_t (&output)[128], UINT32& chars) noexcept {
    const Identity value = identity();
    PACKAGE_ID id = value.package();
    // Deliberately valid but different from the ID returned by packageId.
    if (mode == L"full-name-mismatch") id.version.Minor = 1;
    chars = 0;
    LSTATUS status = ::PackageFullNameFromId(&id, &chars, nullptr);
    if (status != ERROR_INSUFFICIENT_BUFFER)
        return status == ERROR_SUCCESS ? ERROR_INVALID_DATA : status;
    if (chars < 2 || chars > 128) return ERROR_INVALID_DATA;
    const UINT32 available = chars;
    status = ::PackageFullNameFromId(&id, &chars, output);
    if (status != ERROR_SUCCESS) return status;
    if (chars < 2 || chars > available || output[chars - 1] != L'\0')
        return ERROR_INVALID_DATA;
    for (UINT32 i = 0; i + 1 < chars; ++i)
        if (output[i] == L'\0') return ERROR_INVALID_DATA;
    return ERROR_SUCCESS;
}
} // namespace detail

inline LSTATUS WINAPI packageId(HANDLE process, UINT32* bytes, BYTE* output) noexcept {
    ++idCalls;
    if (!bytes || (!output && *bytes != 0)) return ERROR_INVALID_PARAMETER;
    const LSTATUS admitted = detail::processStatus(process);
    if (admitted != ERROR_SUCCESS) return admitted;
    const detail::Identity value = detail::identity();
    // The public helper must reject the size query before allocating storage.
    const UINT32 required = mode == L"oversize-id" ? 65538u : value.bytes();
    const LSTATUS status = detail::capacity(bytes, output, required);
    if (status != ERROR_SUCCESS) return status;
    for (UINT32 i = 0; i < required; ++i) output[i] = 0;
    PACKAGE_ID id = value.package();
    UINT32 offset = static_cast<UINT32>(sizeof(PACKAGE_ID));
    id.name = detail::append(output, offset, value.name);
    id.publisher = detail::append(output, offset, value.publisher);
    id.resourceId = detail::append(output, offset, value.resource);
    id.publisherId = detail::append(output, offset, value.publisherId);
    // One-past is never dereferenced here. All real strings still exist in-buffer;
    // the unchanged helper must reject this pointer before scanning it.
    if (mode == L"bad-id-pointer") id.name = reinterpret_cast<PWSTR>(output + required);
    if (mode == L"null-resource") id.resourceId = nullptr;
    if (mode == L"null-name") id.name = nullptr;
    if (mode == L"null-publisher") id.publisher = nullptr;
    if (mode == L"null-publisher-id") id.publisherId = nullptr;
    const auto* source = reinterpret_cast<const BYTE*>(&id);
    for (UINT32 i = 0; i < static_cast<UINT32>(sizeof(id)); ++i) output[i] = source[i];
    return ERROR_SUCCESS;
}

inline LSTATUS WINAPI fullName(HANDLE process, UINT32* chars, PWSTR output) noexcept {
    ++fullCalls;
    if (!chars || (!output && *chars != 0)) return ERROR_INVALID_PARAMETER;
    const LSTATUS admitted = detail::processStatus(process);
    if (admitted != ERROR_SUCCESS) return admitted;
    wchar_t derived[128]{};
    UINT32 required = 0;
    const LSTATUS derivedStatus = detail::derivedFullName(derived, required);
    if (derivedStatus != ERROR_SUCCESS) return derivedStatus;
    const LSTATUS status = detail::capacity(chars, output, required);
    if (status != ERROR_SUCCESS) return status;
    for (UINT32 i = 0; i < required; ++i) output[i] = derived[i];
    // Report the normal count while removing its final NUL, even for a caller
    // which supplied extra capacity. The public counted-string check must fail.
    if (mode == L"unterminated-full") output[required - 1] = L'X';
    return ERROR_SUCCESS;
}

inline LSTATUS WINAPI packagePath(PCWSTR name, UINT32* chars, PWSTR output) noexcept {
    ++pathCalls;
    if (!name || !chars || (!output && *chars != 0)) return ERROR_INVALID_PARAMETER;
    if (!admittedPid) return ERROR_ACCESS_DENIED;
    if (mode == L"unpackaged") return APPMODEL_ERROR_NO_PACKAGE;
    // This API has no process handle. Bind it to the exact declared full name;
    // PID authentication is performed by both handle-taking API leaves above.
    wchar_t expected[128]{};
    UINT32 expectedChars = 0;
    const LSTATUS derivedStatus = detail::derivedFullName(expected, expectedChars);
    if (derivedStatus != ERROR_SUCCESS) return derivedStatus;
    for (UINT32 i = 0; i < expectedChars; ++i)
        if (name[i] != expected[i]) return APPMODEL_ERROR_NO_PACKAGE;
    const detail::Text suffix = mode == L"wrong-root" ?
        detail::literal(L"\\Other") : detail::literal(L"");
    // Bounded before conversion/addition; the count is in WCHARs including NUL.
    if (packageRoot.size() >= 32768u ||
        packageRoot.size() + suffix.chars > 32768u) return ERROR_FILENAME_EXCED_RANGE;
    const UINT32 rootChars = static_cast<UINT32>(packageRoot.size());
    const UINT32 required = rootChars + suffix.chars;
    const LSTATUS status = detail::capacity(chars, output, required);
    if (status != ERROR_SUCCESS) return status;
    for (UINT32 i = 0; i < rootChars; ++i) output[i] = packageRoot[i];
    for (UINT32 i = 0; i < suffix.chars; ++i) output[rootChars + i] = suffix.data[i];
    return ERROR_SUCCESS;
}
} // namespace HomePackageAdapter
