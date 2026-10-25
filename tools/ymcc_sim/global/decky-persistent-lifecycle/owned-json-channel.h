#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>

// Candidate for FIXTURE-OWNED files only. All readers and writers of one private
// file must use the same canonical absolute spelling and this lease. It cannot
// make an unrelated/uncooperative reader safe. No product/shared-file authority.
// Acquire before opening a reader or temporary writer. Close every file handle
// before destroying the lease. Hold it across the single original commit call.
// There is one finite wait and no retry, fallback or writer API substitution.
namespace owned_json_candidate {
inline constexpr DWORD maximumWaitMs = 1000;
struct Failure : std::runtime_error {
    DWORD error = 0, waitResult = 0;
    explicit Failure(const char* stage, DWORD code, DWORD wait = 0)
        : std::runtime_error(stage), error(code), waitResult(wait) {}
};
inline std::wstring mutexName(const std::filesystem::path& file) {
    auto normalized = std::filesystem::absolute(file).lexically_normal().wstring();
    // These are newly created private fixture paths, not aliases/reparse targets.
    // Invariant case mapping makes both processes derive the same per-file key.
    const int length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        normalized.data(), static_cast<int>(normalized.size()), nullptr, 0, nullptr, nullptr, 0);
    if (!length) throw Failure("owned-json-key-case-size", GetLastError());
    std::wstring canonical(static_cast<std::size_t>(length), L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, normalized.data(),
        static_cast<int>(normalized.size()), canonical.data(), length, nullptr, nullptr, 0))
        throw Failure("owned-json-key-case", GetLastError());
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) throw Failure("owned-json-key-algorithm-ntstatus", static_cast<DWORD>(status));
    unsigned char digest[32]{};
    status = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(canonical.data()), static_cast<ULONG>(canonical.size()*sizeof(wchar_t)),
        digest, sizeof(digest));
    const auto closed = BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw Failure("owned-json-key-hash-ntstatus", static_cast<DWORD>(status));
    if (closed < 0) throw Failure("owned-json-key-close-ntstatus", static_cast<DWORD>(closed));
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring result = L"Local\\YMCC-OwnedJsonObservation-";
    for (const auto byte : digest) { result += hex[byte>>4]; result += hex[byte&15]; }
    return result;
}
class FileLease {
    HANDLE mutex_ = nullptr;
    bool owned_ = false;
public:
    FileLease(const FileLease&) = delete;
    FileLease& operator=(const FileLease&) = delete;
    explicit FileLease(const std::filesystem::path& file, DWORD waitMs) {
        if (!waitMs || waitMs > maximumWaitMs)
            throw Failure("owned-json-lock-budget", ERROR_INVALID_PARAMETER);
        const auto name = mutexName(file);
        mutex_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!mutex_) throw Failure("owned-json-lock-open", GetLastError());
        const auto waited = WaitForSingleObject(mutex_, waitMs);
        const DWORD error = waited == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
        if (waited == WAIT_OBJECT_0) { owned_ = true; return; }
        // Abandonment is visible failure. Never continue into read/commit with
        // potentially partial state merely because ownership was granted.
        if (waited == WAIT_ABANDONED) ReleaseMutex(mutex_);
        CloseHandle(mutex_); mutex_ = nullptr;
        throw Failure(waited == WAIT_TIMEOUT ? "owned-json-lock-timeout" :
            waited == WAIT_ABANDONED ? "owned-json-lock-abandoned" : "owned-json-lock-wait",
            error, waited);
    }
    ~FileLease() noexcept {
        if (owned_) ReleaseMutex(mutex_);
        if (mutex_) CloseHandle(mutex_);
    }
};
}