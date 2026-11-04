#pragma once
#include "process_inventory.h"
#include "steam_session_observer.h"
#include "decky_sidebar_launch.h"
#include <sddl.h>
#include <wincrypt.h>

// The existing CSS-use worker services these requests. No thread, periodic
// discovery, standalone inventory or cross-user endpoint is created here.
namespace ymcc::steamcssinventory {
class Bridge {
    static constexpr DWORD capacity = 128, headerWords = 16;
    static constexpr DWORD bytes = (headerWords + capacity * 5) * sizeof(DWORD);
    HANDLE map_ = nullptr, query_ = nullptr, reply_ = nullptr, mutex_ = nullptr;
    DWORD* view_ = nullptr;
    std::wstring name_;
public:
    ~Bridge() { close(); }
    void close() {
        if (view_) UnmapViewOfFile(view_); view_ = nullptr;
        for (auto value : {map_, query_, reply_, mutex_}) if (value) CloseHandle(value);
        map_ = query_ = reply_ = mutex_ = nullptr; name_.clear();
    }
    bool initialize() {
        if (query_) return true;
        deckylaunch::Handle token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return false;
        const auto user = deckylaunch::tokenData(token.value, TokenUser);
        LPWSTR sid = nullptr;
        if (user.empty() || !ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid, &sid)) return false;
        const auto acl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")S:(ML;;NW;;;ME)"; LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return false;
        HCRYPTPROV provider = 0; BYTE random[16]{};
        const bool acquired = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT) != FALSE;
        const bool generated = acquired && CryptGenRandom(provider, sizeof(random), random) != FALSE;
        if (provider) CryptReleaseContext(provider, 0);
        if (!generated) { LocalFree(descriptor); return false; }
        name_ = L"Local\\YMCC.CssInventory.";
        for (const auto byte : random) { name_ += L"0123456789abcdef"[byte >> 4]; name_ += L"0123456789abcdef"[byte & 15]; }
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
        bool unique = true;
        auto created = [&](HANDLE value) { unique = unique && value && GetLastError() != ERROR_ALREADY_EXISTS; return value; };
        map_ = created(CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, bytes, name_.c_str()));
        query_ = created(CreateEventW(&attributes, FALSE, FALSE, (name_ + L".query").c_str()));
        reply_ = created(CreateEventW(&attributes, FALSE, FALSE, (name_ + L".reply").c_str()));
        mutex_ = created(CreateMutexW(&attributes, FALSE, (name_ + L".lock").c_str()));
        LocalFree(descriptor);
        if (unique) view_ = static_cast<DWORD*>(MapViewOfFile(map_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, bytes));
        if (!view_) { close(); return false; }
        ZeroMemory(view_, bytes); view_[0] = 0x59435049; view_[1] = 1;
        return true;
    }
    const std::wstring& name() const { return name_; }
    HANDLE event() const { return query_; }
    void serve(const steamsession::Session& steam, bool allowed) noexcept {
        if (!view_) return;
        const auto serial = InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(view_ + 2), 0, 0);
        DWORD rows[capacity * 5]{}; DWORD count = 0, error = ERROR_NOT_READY;
        try {
            if (allowed) {
                const auto topology = processInventory::read(); // Sole YMCC producer; 250ms shared coalescing.
                error = !topology ? ERROR_NOT_READY : topology->status != processInventory::Status::Complete ? (topology->error ? topology->error : ERROR_NOT_READY) : ERROR_SUCCESS;
                if (topology && topology->status == processInventory::Status::Complete) {
                    DWORD ourSession = 0;
                    if (!ProcessIdToSessionId(GetCurrentProcessId(), &ourSession)) error = GetLastError();
                    for (const auto& row : topology->rows) {
                        const DWORD kind = !_wcsicmp(row.name.c_str(), L"CssLoader-Standalone-Headless.exe") ? 1 :
                            !_wcsicmp(row.name.c_str(), L"CSSLoader Desktop.exe") || !_wcsicmp(row.name.c_str(), L"CSSLoader-Desktop.exe") ? 2 : 0;
                        if (!kind || error) continue;
                        DWORD session = 0;
                        if (!ProcessIdToSessionId(row.pid, &session)) { error = GetLastError(); break; }
                        if (session != ourSession) continue;
                        deckylaunch::Handle process; process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.pid);
                        if (!process.value) { if (GetLastError() == ERROR_INVALID_PARAMETER) continue; error = GetLastError(); break; }
                        if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) continue;
                        FILETIME birth{}, exit{}, kernel{}, user{};
                        if (!GetProcessTimes(process.value, &birth, &exit, &kernel, &user)) { error = GetLastError(); break; }
                        if (count == capacity) { error = ERROR_MORE_DATA; break; }
                        auto* target = rows + count++ * 5;
                        target[0] = row.pid; target[1] = row.parentPid; target[2] = birth.dwLowDateTime; target[3] = birth.dwHighDateTime; target[4] = kind;
                    }
                }
            }
        } catch (...) { error = ERROR_GEN_FAILURE; }
        // A timed-out old client cannot commit a reply for its replacement.
        if (InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(view_ + 2), 0, 0) != serial) return;
        view_[4] = error ? 0 : 1; view_[5] = error ? 0 : count;
        view_[6] = allowed ? steam.pid : 0; view_[7] = DWORD(steam.created); view_[8] = DWORD(steam.created >> 32); view_[9] = error;
        CopyMemory(view_ + headerWords, rows, sizeof(rows)); MemoryBarrier();
        InterlockedExchange(reinterpret_cast<volatile LONG*>(view_ + 3), serial); SetEvent(reply_);
    }
};
} // namespace ymcc::steamcssinventory
