#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "json.hpp"

// Standalone persistence only. No device, registry, service or product calls.
namespace ymcc::externalpad {
// IO failures can recover; invalid paths, file kinds and records stay fail-closed.
enum class DebtLoadFailure { None, RetryableIo, InvalidRecord };
inline constexpr std::uint32_t ledgerVersion = 1;
inline constexpr const char* debtOwner = "ymcc-external-controller-priority-v1";
inline constexpr std::size_t maxDebtBytes = 64 * 1024;
inline constexpr std::size_t maxHiddenPaths = 32;
inline constexpr std::size_t maxApplications = 32;
inline constexpr std::size_t maxInstanceChars = 199;
inline constexpr std::size_t maxApplicationChars = 1024;
inline constexpr std::size_t maxLedgerPathChars = 4096;
inline constexpr std::size_t maxLocationPathBytes = 4096;

struct VisibilityDebt {
    std::string owner = "ymcc-external-controller-priority-v1";
    std::vector<std::wstring> hiddenPaths;
    std::vector<std::wstring> applications;
    bool cloakChanged = false;
    std::string builtinRoot;
    bool refreshPending = false;
    std::string locationPath;
};

namespace detail {
inline bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}
inline bool winFail(std::string& error, const char* operation, DWORD code) {
    error = std::string(operation) + " (" + std::to_string(code) + ")";
    return false;
}
inline wchar_t foldAscii(wchar_t c) noexcept {
    return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
}
inline bool samePath(std::wstring_view a, std::wstring_view b) noexcept {
    return a.size() == b.size() && CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
        b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
inline bool toUtf8(std::wstring_view text, std::string& result) {
    if (text.empty()) { result.clear(); return true; }
    const auto size = static_cast<int>(text.size()); // All callers bound input first.
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size,
        nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return false;
    std::string converted(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size,
        converted.data(), bytes, nullptr, nullptr) != bytes) return false;
    result = std::move(converted);
    return true;
}
inline bool fromUtf8(std::string_view text, std::wstring& result) {
    if (text.empty()) { result.clear(); return true; }
    if (text.size() > maxDebtBytes) return false;
    const auto size = static_cast<int>(text.size());
    const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
    if (chars <= 0) return false;
    std::wstring converted(static_cast<std::size_t>(chars), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size,
        converted.data(), chars) != chars) return false;
    result = std::move(converted);
    return true;
}
inline bool alpha(wchar_t c) noexcept {
    c = foldAscii(c);
    return c >= L'a' && c <= L'z';
}
inline bool digit(wchar_t c) noexcept { return c >= L'0' && c <= L'9'; }
// Exact ENUMERATOR\DEVICE\INSTANCE, never a device interface, class, glob,
// traversal, or command. Syntax alone does not prove ancestry or ownership.
inline bool instance(std::wstring_view text) noexcept {
    if (text.empty() || text.size() > maxInstanceChars) return false;
    unsigned component = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\\') {
            if (i == start || text.substr(start, i - start) == L"." ||
                text.substr(start, i - start) == L"..") return false;
            if (i == text.size()) return component == 2;
            if (++component > 2) return false;
            start = i + 1;
            continue;
        }
        const wchar_t c = text[i];
        if (alpha(c) || digit(c) || c == L'_' || c == L'-') continue;
        if (component != 0 && (c == L'&' || c == L'{' || c == L'}' || c == L'.' || c == L'+')) continue;
        return false;
    }
    return false;
}
inline bool reservedComponent(std::wstring_view part) noexcept {
    const auto dot = part.find(L'.');
    auto base = part.substr(0, dot);
    while (!base.empty() && base.back() == L' ') base.remove_suffix(1);
    if (samePath(base, L"CON") || samePath(base, L"PRN") || samePath(base, L"AUX") ||
        samePath(base, L"NUL") || samePath(base, L"CONIN$") || samePath(base, L"CONOUT$")) return true;
    if (base.size() != 4) return false;
    const auto prefix = base.substr(0, 3);
    const auto suffix = base[3];
    return (samePath(prefix, L"COM") || samePath(prefix, L"LPT")) &&
        ((suffix >= L'0' && suffix <= L'9') || suffix == L'\u00b9' || suffix == L'\u00b2' || suffix == L'\u00b3');
}
// Fully qualified DOS or UNC file names only, with unambiguous components.
// Reject Win32/NT device namespaces and alternate streams before any file IO.
inline bool absoluteFile(std::wstring_view text, std::size_t limit) noexcept {
    if (text.size() < 4 || text.size() > limit) return false;
    std::size_t start = 0;
    unsigned required = 1;
    if (alpha(text[0]) && text[1] == L':' && text[2] == L'\\') start = 3;
    else if (text[0] == L'\\' && text[1] == L'\\') { start = 2; required = 3; }
    else return false;
    unsigned count = 0;
    std::size_t componentStart = start;
    for (std::size_t i = start; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\\') {
            const auto part = text.substr(componentStart, i - componentStart);
            if (part.empty() || part.size() > 255 || part.front() == L' ' || part == L"." || part == L".." ||
                part.back() == L'.' || part.back() == L' ' || reservedComponent(part)) return false;
            ++count;
            componentStart = i + 1;
        } else {
            const wchar_t c = text[i];
            if (c < 0x20 || c == 0x7f || c == L'/' || c == L':' || c == L'"' ||
                c == L'<' || c == L'>' || c == L'|' || c == L'?' || c == L'*') return false;
        }
    }
    return count >= required;
}
inline bool application(std::wstring_view text) noexcept {
    if (!absoluteFile(text, maxApplicationChars) || text.size() < 5) return false;
    const auto name = text.substr(text.find_last_of(L'\\') + 1);
    return name.size() > 4 && samePath(name.substr(name.size() - 4), L".exe");
}
inline bool hex(wchar_t c) noexcept {
    c = foldAscii(c);
    return digit(c) || (c >= L'a' && c <= L'f');
}
inline bool usbVidPidRoot(std::wstring_view root) noexcept {
    if (!instance(root) || root.size() < 23 || root[21] != L'\\' ||
        !samePath(root.substr(0, 8), L"USB\\VID_") || !samePath(root.substr(12, 5), L"&PID_")) return false;
    for (std::size_t i = 0; i < 4; ++i)
        if (!hex(root[8 + i]) || !hex(root[17 + i])) return false;
    return true;
}
// DEVPKEY_Device_LocationPaths spelling, e.g. PCIROOT(0)#USBROOT(0)#USB(4).
// Exact bytes stay intact. Syntax is not evidence of the current device tree.
inline bool safeLocationPath(std::string_view path) noexcept {
    if (path.empty() || path.size() > maxLocationPathBytes) return false;
    bool usb = false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const auto c = static_cast<wchar_t>(static_cast<unsigned char>(path[i]));
        if (!(alpha(c) || digit(c) || c == L'_' || c == L'-' || c == L'#' ||
            c == L'(' || c == L')' || c == L'\\')) return false;
        if (i + 2 < path.size() && foldAscii(c) == L'u' &&
            foldAscii(static_cast<wchar_t>(static_cast<unsigned char>(path[i + 1]))) == L's' &&
            foldAscii(static_cast<wchar_t>(static_cast<unsigned char>(path[i + 2]))) == L'b') usb = true;
    }
    return usb;
}
inline bool validate(const VisibilityDebt& debt, std::string& error) {
    if (debt.owner != debtOwner) return fail(error, "invalid owner");
    if (debt.hiddenPaths.size() > maxHiddenPaths || debt.applications.size() > maxApplications)
        return fail(error, "too many debt entries");
    if (debt.builtinRoot.size() > maxInstanceChars) return fail(error, "invalid builtin root");
    std::wstring root;
    if (!fromUtf8(debt.builtinRoot, root) || (!root.empty() && !instance(root)))
        return fail(error, "invalid builtin root");
    if (!debt.locationPath.empty() && !safeLocationPath(debt.locationPath))
        return fail(error, "invalid location path");
    if (debt.refreshPending && (!usbVidPidRoot(root) || debt.locationPath.empty()))
        return fail(error, "invalid pending refresh identity");
    for (std::size_t i = 0; i < debt.hiddenPaths.size(); ++i) {
        if (!instance(debt.hiddenPaths[i])) return fail(error, "invalid hidden target");
        for (std::size_t j = 0; j < i; ++j)
            if (samePath(debt.hiddenPaths[i], debt.hiddenPaths[j])) return fail(error, "duplicate hidden target");
    }
    for (std::size_t i = 0; i < debt.applications.size(); ++i) {
        const auto& path = debt.applications[i];
        if (!application(path)) return fail(error, "invalid application path");
        std::string encoded;
        if (!toUtf8(path, encoded)) return fail(error, "invalid application Unicode");
        for (std::size_t j = 0; j < i; ++j)
            if (samePath(path, debt.applications[j])) return fail(error, "duplicate application");
    }
    return true;
}
inline bool ledgerPath(const std::wstring& path, std::string& error) {
    if (!absoluteFile(path, maxLedgerPathChars)) return fail(error, "invalid ledger path");
    std::string encoded;
    return toUtf8(path, encoded) || fail(error, "invalid ledger path Unicode");
}
class DebtSax final : public nlohmann::json_sax<nlohmann::json> {
    enum Field : std::size_t { Version, Owner, Hidden, Applications, Cloak, Root, Refresh, Location, Count };
    std::array<bool, Count> seen_{};
    Field field_ = Version;
    bool started_ = false, completed_ = false, awaiting_ = false, array_ = false;
    bool scalar(Field f) const noexcept { return started_ && !completed_ && awaiting_ && !array_ && field_ == f; }
    bool reject(const char* message = "invalid ledger field type") { error = message; return false; }
public:
    VisibilityDebt debt;
    std::string error = "invalid ledger JSON";
    bool completed() const noexcept { return completed_; }
    bool null() override { return reject(); }
    bool boolean(bool value) override {
        if (scalar(Cloak)) debt.cloakChanged = value;
        else if (scalar(Refresh)) debt.refreshPending = value;
        else return reject();
        awaiting_ = false; return true;
    }
    bool number_integer(number_integer_t) override { return reject("invalid version or field type"); }
    bool number_unsigned(number_unsigned_t value) override {
        if (!scalar(Version) || value != ledgerVersion) return reject("invalid version or field type");
        awaiting_ = false; return true;
    }
    bool number_float(number_float_t, const string_t&) override { return reject("invalid version or field type"); }
    bool string(string_t& value) override {
        if (array_) {
            auto& entries = field_ == Hidden ? debt.hiddenPaths : debt.applications;
            const auto limit = field_ == Hidden ? maxHiddenPaths : maxApplications;
            if (entries.size() >= limit) return reject("too many debt entries");
            std::wstring converted;
            if (!fromUtf8(value, converted)) return reject("invalid ledger Unicode");
            if (field_ == Hidden ? !instance(converted) : !application(converted))
                return reject(field_ == Hidden ? "invalid hidden target" : "invalid application path");
            for (const auto& prior : entries)
                if (samePath(prior, converted)) return reject("duplicate debt entry");
            entries.push_back(std::move(converted));
            return true;
        }
        if (scalar(Owner)) {
            if (value != debtOwner) return reject("invalid owner");
            debt.owner = std::move(value);
        } else if (scalar(Root)) {
            if (value.size() > maxInstanceChars) return reject("invalid builtin root");
            std::wstring converted;
            if (!fromUtf8(value, converted) || (!converted.empty() && !instance(converted)))
                return reject("invalid builtin root");
            debt.builtinRoot = std::move(value);
        } else if (scalar(Location)) {
            if (!value.empty() && !safeLocationPath(value)) return reject("invalid location path");
            debt.locationPath = std::move(value);
        } else return reject();
        awaiting_ = false; return true;
    }
    bool binary(binary_t&) override { return reject(); }
    bool start_object(std::size_t) override {
        if (started_) return reject();
        started_ = true; return true;
    }
    bool key(string_t& value) override {
        if (!started_ || completed_ || awaiting_ || array_) return reject();
        constexpr std::array<std::string_view, Count> names{{
            "version", "owner", "hiddenPaths", "applications", "cloakChanged", "builtinRoot",
            "refreshPending", "locationPath"}};
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (value != names[i]) continue;
            if (seen_[i]) return reject("duplicate ledger field");
            seen_[i] = true; field_ = static_cast<Field>(i); awaiting_ = true; return true;
        }
        return reject("unknown ledger field");
    }
    bool end_object() override {
        if (!started_ || completed_ || awaiting_ || array_) return reject();
        // The two refresh fields were added to v1; old records default false/empty.
        for (std::size_t i = 0; i < Refresh; ++i)
            if (!seen_[i]) return reject("missing ledger field");
        completed_ = validate(debt, error); return completed_;
    }
    bool start_array(std::size_t) override {
        if (!scalar(Hidden) && !scalar(Applications)) return reject();
        array_ = true; return true;
    }
    bool end_array() override {
        if (!array_) return reject();
        array_ = false; awaiting_ = false; return true;
    }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override {
        return reject("invalid ledger JSON");
    }
};
inline bool decode(std::string_view bytes, VisibilityDebt& debt, std::string& error) {
    if (bytes.empty() || bytes.size() > maxDebtBytes || bytes.find('\0') != std::string_view::npos ||
        (bytes.size() >= 3 && bytes.substr(0, 3) == "\xef\xbb\xbf")) return fail(error, "invalid ledger bytes");
    DebtSax sax;
    if (!nlohmann::json::sax_parse(bytes.begin(), bytes.end(), &sax,
        nlohmann::json::input_format_t::json, true, false) || !sax.completed()) {
        error = sax.error; return false;
    }
    debt = std::move(sax.debt);
    return true;
}
inline bool encode(const VisibilityDebt& debt, std::string& bytes, std::string& error) {
    if (!validate(debt, error)) return false;
    auto hidden = nlohmann::json::array();
    auto apps = nlohmann::json::array();
    std::string encoded;
    for (const auto& path : debt.hiddenPaths) {
        if (!toUtf8(path, encoded)) return fail(error, "invalid hidden target Unicode");
        hidden.push_back(encoded);
    }
    for (const auto& path : debt.applications) {
        if (!toUtf8(path, encoded)) return fail(error, "invalid application Unicode");
        apps.push_back(encoded);
    }
    bytes = nlohmann::json{{"version", ledgerVersion}, {"owner", debt.owner},
        {"hiddenPaths", std::move(hidden)}, {"applications", std::move(apps)},
        {"cloakChanged", debt.cloakChanged}, {"builtinRoot", debt.builtinRoot},
        {"refreshPending", debt.refreshPending}, {"locationPath", debt.locationPath}}.dump();
    bytes += '\n';
    return bytes.size() <= maxDebtBytes || fail(error, "ledger too large");
}
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    bool close(std::string& error) {
        const HANDLE closing = std::exchange(handle, INVALID_HANDLE_VALUE);
        if (!CloseHandle(closing)) return winFail(error, "close ledger", GetLastError());
        return true;
    }
};
struct TempFile {
    std::wstring path;
    File file;
    bool owned = false;
    ~TempFile() {
        // Only a CREATE_NEW file owned by this call; never delete the ledger.
        if (file.handle != INVALID_HANDLE_VALUE) CloseHandle(std::exchange(file.handle, INVALID_HANDLE_VALUE));
        if (owned) DeleteFileW(path.c_str());
    }
};
inline bool ordinaryTarget(const std::wstring& path, std::string& error) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
        return winFail(error, "inspect ledger", code);
    }
    return !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        fail(error, "ledger is not a regular file");
}
} // namespace detail

// Parent supplies a fully qualified path in its trusted per-user settings directory,
// normally external-controller-priority-return-v1.json. No directory creation,
// location discovery, stale-temp adoption, or repair of malformed records.
// Failure leaves out unchanged. A missing file returns a clean default debt.
inline bool loadDebt(const std::wstring& path, VisibilityDebt& out, std::string& error,
                     DebtLoadFailure& failure) {
    error.clear();
    failure = DebtLoadFailure::InvalidRecord;
    const auto ioFailure = [&](const char* operation, DWORD code) {
        failure = DebtLoadFailure::RetryableIo;
        return detail::winFail(error, operation, code);
    };
    try {
        if (!detail::ledgerPath(path, error)) return false;
        detail::File file;
        file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file.handle == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
                out = VisibilityDebt{}; failure = DebtLoadFailure::None; return true;
            }
            // Opening a directory can itself fail with ACCESS_DENIED. Do not
            // mistake an observed invalid file kind for a transient access error.
            if (code == ERROR_ACCESS_DENIED) {
                const DWORD attributes = GetFileAttributesW(path.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES &&
                    (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                    return detail::fail(error, "ledger is not a regular file");
            }
            return ioFailure("open ledger", code);
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file.handle, &info)) return ioFailure("inspect ledger", GetLastError());
        SetLastError(ERROR_SUCCESS);
        const DWORD type = GetFileType(file.handle);
        const DWORD typeError = type == FILE_TYPE_UNKNOWN ? GetLastError() : ERROR_SUCCESS;
        if (typeError != ERROR_SUCCESS) return ioFailure("inspect ledger type", typeError);
        if (type != FILE_TYPE_DISK ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return detail::fail(error, "ledger is not a regular file");
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.handle, &size)) return ioFailure("size ledger", GetLastError());
        if (size.QuadPart <= 0 || size.QuadPart > static_cast<LONGLONG>(maxDebtBytes))
            return detail::fail(error, "invalid ledger size");
        std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD read = 0;
            if (!ReadFile(file.handle, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &read, nullptr))
                return ioFailure("read ledger", GetLastError());
            if (!read) return detail::fail(error, "truncated ledger");
            offset += read;
        }
        if (!file.close(error)) { failure = DebtLoadFailure::RetryableIo; return false; }
        VisibilityDebt candidate;
        if (!detail::decode(bytes, candidate, error)) return false;
        out = std::move(candidate);
        failure = DebtLoadFailure::None;
        return true;
    } catch (const std::bad_alloc&) {
        failure = DebtLoadFailure::RetryableIo;
        return detail::fail(error, "ledger load allocation failed");
    } catch (...) { return detail::fail(error, "ledger load failed"); }
}
// Keep the existing standalone three-argument interface.
inline bool loadDebt(const std::wstring& path, VisibilityDebt& out, std::string& error) {
    DebtLoadFailure failure;
    return loadDebt(path, out, error, failure);
}

// Caller serializes its ownership transactions. Record only exact additions it
// owns, and save successfully BEFORE hiding, application registration or cloak
// mutation. Persist pending debt until each return is confirmed by readback;
// a failed return/readback must never be cleared. After unhiding, keep the root,
// locationPath and refreshPending until the same exact-root/location helper cycle
// is acknowledged. Save VisibilityDebt{} (also clears refresh fields) only
// after confirmed return: it atomically replaces the record and keeps the file.
inline bool saveDebt(const std::wstring& path, const VisibilityDebt& debt, std::string& error) {
    error.clear();
    try {
        if (!detail::ledgerPath(path, error)) return false;
        std::string bytes;
        if (!detail::encode(debt, bytes, error) || !detail::ordinaryTarget(path, error)) return false;
        static std::atomic<std::uint64_t> sequence{0};
        detail::TempFile temp;
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            temp.path = path + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
                std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(sequence.fetch_add(1));
            temp.file.handle = CreateFileW(temp.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
            if (temp.file.handle != INVALID_HANDLE_VALUE) { temp.owned = true; break; }
            const DWORD code = GetLastError();
            if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS)
                return detail::winFail(error, "create ledger temp", code);
        }
        if (!temp.owned) return detail::fail(error, "ledger temp collision");
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD written = 0;
            if (!WriteFile(temp.file.handle, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &written, nullptr))
                return detail::winFail(error, "write ledger temp", GetLastError());
            if (!written) return detail::fail(error, "short ledger write");
            offset += written;
        }
        if (!FlushFileBuffers(temp.file.handle)) return detail::winFail(error, "flush ledger temp", GetLastError());
        if (!temp.file.close(error) || !detail::ordinaryTarget(path, error)) return false;
        // Same-directory replacement only: no copy fallback or reboot/registry operation.
        if (!MoveFileExW(temp.path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return detail::winFail(error, "replace ledger", GetLastError());
        temp.owned = false;
        return true;
    } catch (...) { return detail::fail(error, "ledger save failed"); }
}
} // namespace ymcc::externalpad
