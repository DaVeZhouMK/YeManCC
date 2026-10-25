#pragma once
#include "json.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// Explicit diagnostic export only. No startup work, cache, timer, subscription,
// Steam method, setting write, input/device operation or recovery admission.
namespace ymcc::steamtouchpadconsumer {
using json = nlohmann::json;
struct Context {
    DWORD pid = 0, windowsSession = 0;
    std::uint64_t birth = 0;
    std::wstring steamRoot;
    bool sessionBound = false;
};
using HashFile = std::function<std::string(const std::wstring&)>;

namespace detail {
inline constexpr char clientSha256[] = "e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e";
inline constexpr DWORD imageSize = 0x19b3000, timestamp = 0x6ace7cc5;
inline constexpr std::uint64_t diskBytes = 26544280, checkpointMs = 1000;
inline constexpr unsigned memoryApiLimit = 64, readCallLimit = 32, bindingCheckLimit = 3;
inline constexpr std::size_t readByteLimit = 1024, singleReadLimit = 128;
struct Slot { DWORD pointerRva, nameRva; const char* name; std::size_t nameBytes; };
inline constexpr std::array<Slot, 3> slots{{
    {0x180fe50, 0x12e55c0, "sc_kalman_repeated_sample", sizeof("sc_kalman_repeated_sample")},
    {0x1820d80, 0x136af60, "sc_kalman_slowq", sizeof("sc_kalman_slowq")},
    {0x1820e60, 0x136af70, "sc_kalman_fastq", sizeof("sc_kalman_fastq")}
}};
inline constexpr std::array<unsigned char, 59> holdCode{{
    0x48,0x8b,0x05,0xac,0xd2,0x19,0x01,0x83,0x78,0x7c,0x00,0x74,0x23,0x84,0xc9,0x74,
    0x1f,0x41,0x0f,0xb7,0x46,0x08,0x49,0x8d,0x56,0x0e,0x49,0x8d,0x4e,0x10,0x66,0x39,
    0x02,0x74,0x08,0x0f,0xb7,0x06,0x66,0x39,0x01,0x75,0x05,0x41,0xb7,0x01,0xeb,0x0b,
    0x45,0x32,0xff,0x49,0x8d,0x56,0x0e,0x49,0x8d,0x4e,0x10
}};
inline constexpr std::array<unsigned char, 13> overrideCode{{
    0x48,0x8b,0x05,0x18,0xe1,0x1a,0x01,0x83,0x78,0x7c,0x00,0x74,0x56
}};
struct LastErrorGuard {
    DWORD value = GetLastError();
    ~LastErrorGuard() noexcept { SetLastError(value); }
};
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) noexcept : value(h) {}
    ~Handle() noexcept { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool valid() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
};
struct Stop { const char* reason; DWORD error; bool unsupported; };
inline std::uint64_t fileTime(FILETIME t) noexcept {
    return (std::uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
inline std::string hex(std::uint64_t n) {
    constexpr char digits[] = "0123456789abcdef";
    char out[18]; out[0] = '0'; out[1] = 'x';
    for (unsigned i = 0; i < 16; ++i) out[17 - i] = digits[(n >> (i * 4)) & 15];
    return std::string(out, sizeof(out));
}
inline std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw Stop{"identity-path-encoding", GetLastError(), false};
    std::string out(static_cast<std::size_t>(n), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), out.data(), n, nullptr, nullptr) != n)
        throw Stop{"identity-path-encoding", GetLastError(), false};
    return out;
}
// Lexical absolute Win32 paths only; reject relative paths, ADS and dot/alias
// segments rather than silently broadening the caller's already bound root.
inline bool path(const std::wstring& input, std::wstring& out) {
    if (input.empty() || input.size() > 32760 || input.find(L'\0') != std::wstring::npos) return false;
    std::wstring s = input;
    std::replace(s.begin(), s.end(), L'/', L'\\');
    if (s.rfind(L"\\\\?\\UNC\\", 0) == 0) s = L"\\\\" + s.substr(8);
    else if (s.rfind(L"\\\\?\\", 0) == 0) s.erase(0, 4);
    const bool drive = s.size() >= 3 && ((s[0] >= L'A' && s[0] <= L'Z') ||
        (s[0] >= L'a' && s[0] <= L'z')) && s[1] == L':' && s[2] == L'\\';
    const bool unc = !drive && s.rfind(L"\\\\", 0) == 0;
    if (!drive && !unc) return false;
    std::wstring result = drive ? s.substr(0, 3) : L"\\\\";
    unsigned segments = 0;
    for (std::size_t pos = drive ? 3 : 2; pos < s.size();) {
        const auto end = s.find(L'\\', pos);
        const auto item = s.substr(pos, end == std::wstring::npos ? end : end - pos);
        pos = end == std::wstring::npos ? s.size() : end + 1;
        if (item.empty()) continue;
        if (item == L"." || item == L".." || item.back() == L'.' || item.back() == L' ' ||
            item.find_first_of(L":*?\"<>|") != std::wstring::npos) return false;
        if (result.back() != L'\\') result += L'\\';
        result += item; ++segments;
    }
    if (unc && segments < 2) return false;
    out = std::move(result); return true;
}
inline bool samePath(const std::wstring& a, const std::wstring& b) noexcept {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
        static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
inline std::wstring child(const std::wstring& root, const wchar_t* name) {
    return root + (root.back() == L'\\' ? L"" : L"\\") + name;
}
inline json receipt(bool enabled) {
    json globals = json::array();
    for (const auto& s : slots) globals.push_back({{"name",s.name},
        {"pointerSlotRva",s.pointerRva},{"fixedNameRva",s.nameRva},
        {"nameVerified",false},{"valueAvailable",false},
        {"floatAt78",nullptr},{"intAt7c",nullptr}});
    return {{"source","steam-touchpad-consumer-evidence"},{"schema",1},
        {"profile","steamclient64-e9045710"},{"expectedDiskSha256",clientSha256},
        {"scope","fixed-native-globals-and-two-code-fragments-only"},
        {"exportOnly",true},{"detailedEnabled",enabled},{"atomic",false},
        {"selectedControllerProven",false},{"flickerCauseProven",false},
        {"loadedWholeImageHashProven",false},{"defaultValuesProven",false},
        {"status",enabled ? "unavailable" : "disabled"},{"available",false},
        {"reason",enabled ? "not-observed" : "detailed-logging-disabled"},
        {"identityBefore",nullptr},{"identityAfter",nullptr},
        {"moduleBefore",nullptr},{"moduleAfter",nullptr},
        {"fileBefore",nullptr},{"fileAfterHash",nullptr},{"fileAfter",nullptr},
        {"identityBracketComplete",false},{"diskProfileVerified",false},
        {"loadedHeaderVerified",false},{"selectedCodeVerified",false},
        {"hashCalls",0},{"bindingCheckCalls",0},{"bindingCheckMax",bindingCheckLimit},
        {"readCalls",0},{"readBytes",0},{"requestedReadBytes",0},
        {"regionQueryCalls",0},{"memoryApiCalls",0},{"elapsedMs",0},
        {"checkpointBudgetMs",checkpointMs},{"hardIoTimeout",false},
        {"readCallLimit",readCallLimit},{"readByteLimit",readByteLimit},
        {"singleReadLimit",singleReadLimit},{"memoryApiCallLimit",memoryApiLimit},
        {"holdGateIntNonzero",nullptr},{"globals",std::move(globals)}};
}
struct Module { std::uintptr_t base = 0; DWORD size = 0; std::wstring path; };
struct FileStamp {
    std::uint64_t bytes = 0, modified = 0, index = 0;
    DWORD volume = 0;
    bool operator==(const FileStamp&) const = default;
};

class Observer {
    Context context_;
    const HashFile& hash_;
    const std::function<bool()>& bound_;
    json result_;
    const ULONGLONG started_ = GetTickCount64();
    unsigned reads_ = 0, queries_ = 0, hashes_ = 0, bindings_ = 0;
    std::size_t bytes_ = 0, requested_ = 0;
    std::wstring executable_, library_;
    Module module_;
    [[noreturn]] static void stop(const char* why, bool unsupported = false, DWORD error = 0) {
        throw Stop{why,error,unsupported};
    }
    [[noreturn]] static void win(const char* why) { throw Stop{why,GetLastError(),false}; }
    // Frequent checkpoints are elapsed-only: the production binding predicate
    // can perform registry/process work and must not run in read/module loops.
    void checkpoint() const {
        if (GetTickCount64() - started_ >= checkpointMs) stop("elapsed-checkpoint-budget");
    }
    void bindingCheckpoint() {
        checkpoint();
        if (bindings_ >= bindingCheckLimit) stop("binding-check-budget");
        ++bindings_;
        bool current = false;
        try { current = bound_ && bound_(); }
        catch (...) { stop("binding-check-exception"); }
        if (!current) stop("steam-session-binding-changed");
        checkpoint();
    }
    static std::uint64_t add(std::uint64_t a, std::size_t n) {
        if (n > (std::numeric_limits<std::uint64_t>::max)() - a) stop("address-overflow",true);
        return a + n;
    }
    std::uint64_t imageAddress(DWORD rva, std::size_t n) const {
        if (rva >= module_.size || n > module_.size - rva) stop("profile-range-outside-module",true);
        return add(module_.base, rva);
    }
    void read(HANDLE process, std::uint64_t address, void* dest, std::size_t n, bool image) {
        checkpoint();
        if (!n || n > singleReadLimit || reads_ >= readCallLimit ||
            n > readByteLimit - requested_ || reads_ + queries_ + 2 > memoryApiLimit)
            stop("memory-read-budget");
        const auto end = add(address,n);
        if (address < 0x10000 || end > (std::numeric_limits<std::uintptr_t>::max)())
            stop("unsupported-memory-address",true);
        MEMORY_BASIC_INFORMATION region{};
        ++queries_;
        if (VirtualQueryEx(process,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),
            &region,sizeof(region)) != sizeof(region)) win("memory-region-query");
        const auto begin = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        const auto regionEnd = add(begin,region.RegionSize);
        const DWORD access = region.Protect & 0xff;
        const bool readable = access == PAGE_READONLY || access == PAGE_READWRITE ||
            access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READ ||
            access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
        if (region.State != MEM_COMMIT || !readable || (region.Protect & (PAGE_GUARD|PAGE_NOACCESS)) ||
            address < begin || end > regionEnd) stop("unreadable-memory-region");
        if (image && (region.Type != MEM_IMAGE ||
            reinterpret_cast<std::uintptr_t>(region.AllocationBase) != module_.base))
            stop("module-image-region-mismatch",true);
        SIZE_T got = 0;
        ++reads_; requested_ += n;
        const BOOL ok = ReadProcessMemory(process,
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),dest,n,&got);
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        bytes_ += got <= n ? got : n;
        if (!ok || got != n) stop("partial-or-failed-memory-read",false,error);
        checkpoint();
    }
    template<class T> static T number(const unsigned char* data, std::size_t offset) noexcept {
        T value{}; std::memcpy(&value,data + offset,sizeof(value)); return value;
    }
    std::vector<unsigned char> sid(HANDLE process) const {
        checkpoint();
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(process,TOKEN_QUERY,&rawToken)) win("process-token-query");
        Handle token(rawToken);
        DWORD need = 0;
        const BOOL sized = GetTokenInformation(token.value,TokenUser,nullptr,0,&need);
        const DWORD error = GetLastError();
        if (sized || error != ERROR_INSUFFICIENT_BUFFER || need < sizeof(TOKEN_USER) || need > 4096)
            stop("token-user-size",false,error);
        std::vector<unsigned char> data(need);
        if (!GetTokenInformation(token.value,TokenUser,data.data(),need,&need)) win("token-user-read");
        if (need < sizeof(TOKEN_USER) || need > data.size()) stop("token-user-layout");
        const auto* user = reinterpret_cast<const TOKEN_USER*>(data.data());
        const auto first = reinterpret_cast<std::uintptr_t>(data.data());
        const auto value = reinterpret_cast<std::uintptr_t>(user->User.Sid);
        if (value < first || value - first > need || need - (value - first) < 8 ||
            !IsValidSid(user->User.Sid)) stop("token-user-sid-layout");
        const DWORD length = GetLengthSid(user->User.Sid);
        if (length < 8 || length > need - (value - first)) stop("token-user-sid-size");
        std::vector<unsigned char> result(length);
        if (!CopySid(length,result.data(),user->User.Sid)) win("token-user-sid-copy");
        checkpoint(); return result;
    }
    json identity(HANDLE process) const {
        checkpoint();
        if (GetProcessId(process) != context_.pid) stop("process-id-mismatch");
        FILETIME born{},exit{},kernel{},user{};
        if (!GetProcessTimes(process,&born,&exit,&kernel,&user)) win("process-times");
        if (fileTime(born) != context_.birth) stop("process-birth-mismatch");
        std::wstring observed(32768,L'\0'); DWORD count = static_cast<DWORD>(observed.size());
        if (!QueryFullProcessImageNameW(process,0,observed.data(),&count)) win("process-image-path");
        if (!count || count >= observed.size()) stop("process-image-path-size");
        observed.resize(count); std::wstring normalized;
        if (!path(observed,normalized) || !samePath(normalized,executable_)) stop("process-image-path-mismatch");
        DWORD targetSession = 0, callerSession = 0;
        if (!ProcessIdToSessionId(context_.pid,&targetSession) ||
            !ProcessIdToSessionId(GetCurrentProcessId(),&callerSession)) win("process-session-query");
        if (targetSession != context_.windowsSession || callerSession != context_.windowsSession)
            stop("windows-session-mismatch");
        auto targetSid = sid(process); auto callerSid = sid(GetCurrentProcess());
        if (!EqualSid(targetSid.data(),callerSid.data())) stop("process-user-sid-mismatch");
        DWORD code = 0;
        if (!GetExitCodeProcess(process,&code)) win("process-exit-code-query");
        // No WaitForSingleObject: this read/query handle has no SYNCHRONIZE right.
        if (code != STILL_ACTIVE) stop("process-not-active");
        checkpoint();
        return {{"pid",context_.pid},{"birthFileTime",std::to_string(context_.birth)},
            {"windowsSession",targetSession},{"callerWindowsSession",callerSession},
            {"path",utf8(executable_)},{"sameUserSID",true},{"sameWindowsSession",true},
            {"exitCode",code}};
    }
    template<std::size_t N> static std::wstring bounded(const wchar_t (&text)[N]) {
        const auto end = std::find(text,text + N,L'\0');
        if (end == text + N) stop("unterminated-module-identity");
        return std::wstring(text,end);
    }
    Module module() const {
        checkpoint();
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,context_.pid));
        if (!snapshot.valid()) win("module-snapshot");
        MODULEENTRY32W item{}; item.dwSize = sizeof(item);
        if (!Module32FirstW(snapshot.value,&item)) win("module-first");
        unsigned visited = 0, matches = 0; Module result;
        for (;;) {
            checkpoint();
            if (++visited > 512) stop("module-enumeration-budget");
            const auto name = bounded(item.szModule);
            const auto itemPath = bounded(item.szExePath);
            std::wstring normalized;
            const bool pathKnown = path(itemPath,normalized);
            const bool named = samePath(name,L"steamclient64.dll");
            const bool expected = pathKnown && samePath(normalized,library_);
            if (named || expected) {
                if (!named || !expected) stop("module-name-or-path-mismatch",true);
                ++matches;
                result = {reinterpret_cast<std::uintptr_t>(item.modBaseAddr),item.modBaseSize,normalized};
            }
            item.dwSize = sizeof(item);
            if (!Module32NextW(snapshot.value,&item)) {
                const DWORD error = GetLastError();
                if (error != ERROR_NO_MORE_FILES) stop("module-next",false,error);
                break;
            }
        }
        if (matches != 1) stop("module-missing-or-ambiguous");
        if (result.base < 0x10000 || result.size != imageSize) stop("unsupported-module-size",true);
        if (result.size > (std::numeric_limits<std::uintptr_t>::max)() - result.base)
            stop("module-range-overflow",true);
        checkpoint(); return result;
    }
    FileStamp stamp() const {
        checkpoint();
        Handle file(CreateFileW(library_.c_str(),FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        if (!file.valid()) win("module-file-open-attributes");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file.value,&info)) win("module-file-attributes");
        if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) stop("module-file-is-directory",true);
        FileStamp out{(std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow,
            fileTime(info.ftLastWriteTime),
            (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow,info.dwVolumeSerialNumber};
        if (out.bytes != diskBytes) stop("unsupported-module-file-size",true);
        checkpoint(); return out;
    }
    static json moduleJson(const Module& m) {
        return {{"base",hex(m.base)},{"imageSize",m.size},{"path",utf8(m.path)}};
    }
    static json stampJson(const FileStamp& s) {
        return {{"sizeBytes",s.bytes},{"modifiedFileTime",std::to_string(s.modified)},
            {"volumeSerial",s.volume},{"fileIndex",std::to_string(s.index)}};
    }
    void loadedProfile(HANDLE process) {
        std::array<unsigned char,64> dos{};
        read(process,imageAddress(0,dos.size()),dos.data(),dos.size(),true);
        if (dos[0] != 'M' || dos[1] != 'Z' || number<DWORD>(dos.data(),60) != 0x190)
            stop("unsupported-loaded-dos-header",true);
        std::array<unsigned char,120> peHeader{};
        read(process,imageAddress(0x190,peHeader.size()),peHeader.data(),peHeader.size(),true);
        if (number<DWORD>(peHeader.data(),0) != IMAGE_NT_SIGNATURE ||
            number<WORD>(peHeader.data(),4) != IMAGE_FILE_MACHINE_AMD64 ||
            number<DWORD>(peHeader.data(),8) != timestamp ||
            number<WORD>(peHeader.data(),20) != 0xf0 ||
            number<WORD>(peHeader.data(),24) != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            number<DWORD>(peHeader.data(),80) != imageSize)
            stop("unsupported-loaded-pe-header",true);
        result_["loadedHeaderVerified"] = true;
        result_["loadedHeader"] = {{"machine",IMAGE_FILE_MACHINE_AMD64},
            {"timestamp",timestamp},{"imageSize",imageSize},{"ntOffset",0x190}};
        std::array<unsigned char,holdCode.size()> hold{};
        read(process,imageAddress(0x672b9d,hold.size()),hold.data(),hold.size(),true);
        if (hold != holdCode) stop("unsupported-loaded-hold-code",true);
        std::array<unsigned char,overrideCode.size()> overrideRead{};
        read(process,imageAddress(0x672d41,overrideRead.size()),overrideRead.data(),overrideRead.size(),true);
        if (overrideRead != overrideCode) stop("unsupported-loaded-override-code",true);
        result_["selectedCodeVerified"] = true;
        result_["selectedCode"] = json::array({
            {{"rva",0x672b9d},{"bytes",holdCode.size()},{"matchesFixedProfile",true}},
            {{"rva",0x672d41},{"bytes",overrideCode.size()},{"matchesFixedProfile",true}}});
    }
    void globals(HANDLE process) {
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const auto& s = slots[i];
            if (s.nameBytes > 32) stop("internal-name-budget");
            std::uint64_t object = 0, namePointer = 0;
            read(process,imageAddress(s.pointerRva,sizeof(object)),&object,sizeof(object),true);
            if (!object) stop("global-object-unavailable");
            read(process,add(object,0x18),&namePointer,sizeof(namePointer),false);
            const auto expectedName = imageAddress(s.nameRva,s.nameBytes);
            if (namePointer != expectedName) stop("unsupported-global-name-pointer",true);
            std::array<char,32> name{};
            read(process,expectedName,name.data(),s.nameBytes,true);
            if (std::memcmp(name.data(),s.name,s.nameBytes)) stop("unsupported-loaded-global-name",true);
            result_["globals"][i]["nameVerified"] = true;
            std::array<unsigned char,8> values{};
            read(process,add(object,0x78),values.data(),values.size(),false);
            const float f = number<float>(values.data(),0);
            const std::int32_t n = number<std::int32_t>(values.data(),4);
            if (!std::isfinite(f)) stop("unsupported-nonfinite-global-value",true);
            // Provisional until the final identity/module/file/binding bracket.
            result_["globals"][i]["floatAt78"] = f;
            result_["globals"][i]["intAt7c"] = n;
        }
    }
    void redactValues() {
        result_["holdGateIntNonzero"] = nullptr;
        for (auto& row : result_["globals"]) {
            row["floatAt78"] = nullptr; row["intAt7c"] = nullptr;
            row["valueAvailable"] = false;
        }
    }
public:
    Observer(const Context& context, const HashFile& hash,
        const std::function<bool()>& bound, json result)
        : context_(context), hash_(hash), bound_(bound), result_(std::move(result)) {}
    json run() {
        try {
            if constexpr (sizeof(void*) != 8) stop("unsupported-host-pointer-width",true);
            std::wstring root;
            if (!context_.sessionBound || !context_.pid || !context_.birth || !bound_ || !hash_ ||
                !path(context_.steamRoot,root)) stop("steam-identity-context-unavailable");
            executable_ = child(root,L"steam.exe"); library_ = child(root,L"steamclient64.dll");
            bindingCheckpoint(); // 1: before opening the bound Steam process.
            Handle process(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,context_.pid));
            if (!process.valid()) win("process-open-read-query");
            result_["identityBefore"] = identity(process.value);
            module_ = module(); result_["moduleBefore"] = moduleJson(module_);
            const auto before = stamp(); result_["fileBefore"] = stampJson(before);
            checkpoint(); ++hashes_;
            std::string digest;
            try { digest = hash_(library_); }
            catch (...) { stop("module-file-hash-failed"); }
            bindingCheckpoint(); // 2: after the single supplied hash call.
            const auto afterHash = stamp(); result_["fileAfterHash"] = stampJson(afterHash);
            if (!(before == afterHash)) stop("module-file-changed-during-hash");
            for (auto& c : digest) if (c >= 'A' && c <= 'F') c += 'a' - 'A';
            if (digest != clientSha256) stop("unsupported-module-disk-sha256",true);
            result_["diskProfileVerified"] = true;
            loadedProfile(process.value);
            globals(process.value);
            const auto afterModule = module(); result_["moduleAfter"] = moduleJson(afterModule);
            if (module_.base != afterModule.base || module_.size != afterModule.size ||
                !samePath(module_.path,afterModule.path)) stop("module-identity-changed");
            const auto after = stamp(); result_["fileAfter"] = stampJson(after);
            if (!(before == after)) stop("module-file-changed-during-observation");
            result_["identityAfter"] = identity(process.value);
            bindingCheckpoint(); // 3: after identity/module/file checks, before accepting values.
            result_["identityBracketComplete"] = true;
            for (auto& row : result_["globals"]) row["valueAvailable"] = true;
            result_["holdGateIntNonzero"] = result_["globals"][0]["intAt7c"].get<std::int32_t>() != 0;
            result_["status"] = "observed"; result_["reason"] = "fixed-globals-observed";
            result_["available"] = true;
        } catch (const Stop& failure) {
            redactValues();
            result_["status"] = failure.unsupported ? "unsupported" : (reads_ ? "partial" : "unavailable");
            result_["reason"] = failure.reason;
            result_["win32Error"] = failure.error;
        } catch (...) {
            redactValues(); result_["status"] = reads_ ? "partial" : "unavailable";
            result_["reason"] = "capture-exception";
        }
        result_["hashCalls"] = hashes_;
        result_["bindingCheckCalls"] = bindings_;
        result_["readCalls"] = reads_; result_["readBytes"] = bytes_;
        result_["requestedReadBytes"] = requested_;
        result_["regionQueryCalls"] = queries_; result_["memoryApiCalls"] = reads_ + queries_;
        result_["elapsedMs"] = GetTickCount64() - started_;
        return std::move(result_);
    }
};
} // namespace detail

inline json capture(bool detailedEnabled, const Context& context, const HashFile& hash,
    const std::function<bool()>& stillBound) noexcept {
    detail::LastErrorGuard error;
    try {
        auto out = detail::receipt(detailedEnabled);
        // Disabled does not inspect context/binding, open a process/module/file,
        // hash anything, start a clock or construct an observer.
        if (!detailedEnabled) return out;
        return detail::Observer(context,hash,stillBound,std::move(out)).run();
    } catch (...) {
        try {
            auto out = detail::receipt(detailedEnabled);
            out["status"] = detailedEnabled ? "unavailable" : "disabled";
            out["reason"] = "capture-allocation-or-serialization-failed";
            return out;
        } catch (...) { return nullptr; }
    }
}
} // namespace ymcc::steamtouchpadconsumer
