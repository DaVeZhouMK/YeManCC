#pragma once
#include "steam_information_client.h"
#include <appmodel.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace ymcc::steaminfo {
namespace gaming_home_identity_detail {

inline constexpr std::wstring_view packageName = L"YeManCC.GamingHome";
inline constexpr std::wstring_view packagePublisher = L"CN=YeManCC Gaming Home";
inline constexpr std::wstring_view executableName = L"YeManGamingHome.exe";
inline constexpr UINT32 idByteLimit = 65536;
inline constexpr UINT32 fullNameCharLimit = 128; // Includes the terminating NUL.
inline constexpr UINT32 pathCharLimit = 32768;  // Includes the terminating NUL.

inline bool countedString(const wchar_t* text, UINT32 count, UINT32 capacity,
                          UINT32 limit) noexcept {
    if (!text || count < 2 || count > capacity || count > limit ||
        text[count - 1] != L'\0') return false;
    for (UINT32 i = 0; i + 1 < count; ++i)
        if (text[i] == L'\0') return false;
    return true;
}

// PACKAGE_ID stores pointers into the returned byte buffer, with no individual
// string counts. Bound and align every pointer before scanning for its first NUL.
inline bool idString(const std::vector<BYTE>& storage, UINT32 bytes,
                     const wchar_t* text, std::size_t limit,
                     std::wstring_view& result) noexcept {
    result = {};
    if (!text || bytes < sizeof(PACKAGE_ID) || bytes > storage.size()) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(storage.data());
    const auto address = reinterpret_cast<std::uintptr_t>(text);
    if (address < base || address % alignof(wchar_t) != 0) return false;
    const auto offset = address - base;
    if (offset < sizeof(PACKAGE_ID) || offset > bytes ||
        bytes - offset < sizeof(wchar_t)) return false;
    const std::size_t available = (bytes - offset) / sizeof(wchar_t);
    for (std::size_t i = 0; i < available && i <= limit; ++i) {
        if (text[i] == L'\0') {
            result = std::wstring_view(text, i);
            return true;
        }
    }
    return false;
}

// Accept ordinary absolute drive paths only. Do not collapse separators, strip
// namespaces, resolve '..', or treat a basename match as an executable identity.
inline bool absolutePath(std::wstring_view path) noexcept {
    if (path.size() < 3 || path.size() >= pathCharLimit ||
        !((path[0] >= L'A' && path[0] <= L'Z') ||
          (path[0] >= L'a' && path[0] <= L'z')) ||
        path[1] != L':' || path[2] != L'\\') return false;
    std::size_t component = 3;
    for (std::size_t i = 3; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == L'\\') {
            if (i == component || path[i - 1] == L'.' || path[i - 1] == L' ')
                return false;
            component = i + 1;
        } else if (path[i] < L' ' || path[i] == L'/' || path[i] == L':' ||
                   path[i] == L'"' || path[i] == L'<' || path[i] == L'>' ||
                   path[i] == L'|' || path[i] == L'?' || path[i] == L'*') {
            return false;
        }
    }
    return true;
}

inline bool samePath(std::wstring_view a, std::wstring_view b) noexcept {
    if (!absolutePath(a) || !absolutePath(b) || a.size() != b.size()) return false;
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
                                b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
} // namespace gaming_home_identity_detail

// Private source candidate: no real MSIX identity cases have been executed.
// Compilation, fixtures, and any mainline import are the parent task's work.
// This verifies Windows' current process package identity and installation path;
// it makes no stronger claim about certificate-chain policy than that OS identity.
// It is not action permission. The caller must separately enforce sameUser,
// request freshness, and its existing operation/admission rules.
// Primary API contracts: appmodel.h, GetPackageId, GetPackageFullName,
// GetPackagePathByFullName, and PackageFullNameFromId (Windows SDK / Microsoft Learn).
inline bool gamingHomePackagePeer(const Peer& peer) noexcept {
    using namespace gaming_home_identity_detail;
    try {
        const HANDLE process = peer.process.value;
        if (!process || process == INVALID_HANDLE_VALUE || !peer.live() ||
            !absolutePath(peer.path)) return false;

        UINT32 idBytes = 0;
        if (GetPackageId(process, &idBytes, nullptr) != ERROR_INSUFFICIENT_BUFFER ||
            idBytes < sizeof(PACKAGE_ID) || idBytes > idByteLimit) return false;
        const UINT32 idCapacity = idBytes;
        std::vector<BYTE> idStorage(idCapacity, static_cast<BYTE>(0xff));
        if (GetPackageId(process, &idBytes, idStorage.data()) != ERROR_SUCCESS ||
            idBytes < sizeof(PACKAGE_ID) || idBytes > idCapacity ||
            idBytes > idByteLimit) return false;

        PACKAGE_ID id{};
        std::memcpy(&id, idStorage.data(), sizeof(id));
        std::wstring_view name, publisher, resource, publisherId;
        if (id.reserved != 0 || id.processorArchitecture != PROCESSOR_ARCHITECTURE_AMD64 ||
            !idString(idStorage, idBytes, id.name, 50, name) ||
            !idString(idStorage, idBytes, id.publisher, 8192, publisher) ||
            (id.resourceId && !idString(idStorage, idBytes, id.resourceId, 30, resource)) ||
            !idString(idStorage, idBytes, id.publisherId, 13, publisherId) ||
            name != packageName || publisher != packagePublisher ||
            !resource.empty() || publisherId.size() != 13) return false;

        UINT32 fullChars = 0;
        if (GetPackageFullName(process, &fullChars, nullptr) != ERROR_INSUFFICIENT_BUFFER ||
            fullChars < 2 || fullChars > fullNameCharLimit) return false;
        const UINT32 fullCapacity = fullChars;
        std::vector<wchar_t> fullName(fullCapacity, static_cast<wchar_t>(0xffff));
        if (GetPackageFullName(process, &fullChars, fullName.data()) != ERROR_SUCCESS ||
            !countedString(fullName.data(), fullChars, fullCapacity, fullNameCharLimit))
            return false;

        // Reconstruct the full name from the bounded ID, including version,
        // architecture, resource ID and publisher ID, then compare exactly.
        // Thus the path lookup cannot silently use a different package name.
        UINT32 derivedChars = 0;
        if (PackageFullNameFromId(&id, &derivedChars, nullptr) != ERROR_INSUFFICIENT_BUFFER ||
            derivedChars < 2 || derivedChars > fullNameCharLimit) return false;
        const UINT32 derivedCapacity = derivedChars;
        std::vector<wchar_t> derived(derivedCapacity, static_cast<wchar_t>(0xffff));
        if (PackageFullNameFromId(&id, &derivedChars, derived.data()) != ERROR_SUCCESS ||
            !countedString(derived.data(), derivedChars, derivedCapacity, fullNameCharLimit) ||
            std::wstring_view(derived.data(), derivedChars - 1) !=
                std::wstring_view(fullName.data(), fullChars - 1)) return false;

        UINT32 rootChars = 0;
        if (GetPackagePathByFullName(fullName.data(), &rootChars, nullptr) !=
                ERROR_INSUFFICIENT_BUFFER || rootChars < 2 || rootChars > pathCharLimit)
            return false;
        const UINT32 rootCapacity = rootChars;
        std::vector<wchar_t> root(rootCapacity, static_cast<wchar_t>(0xffff));
        if (GetPackagePathByFullName(fullName.data(), &rootChars, root.data()) != ERROR_SUCCESS ||
            !countedString(root.data(), rootChars, rootCapacity, pathCharLimit)) return false;
        const std::wstring_view packageRoot(root.data(), rootChars - 1);
        if (!absolutePath(packageRoot) ||
            packageRoot.size() + 1 + executableName.size() >= pathCharLimit) return false;
        std::wstring expected(packageRoot);
        expected += L'\\';
        expected += executableName;
        return samePath(peer.path, expected) && peer.process.value == process && peer.live();
    } catch (...) {
        return false;
    }
}
} // namespace ymcc::steaminfo
