#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <string>
#include <vector>
#include "json.hpp"
#include "hidhide_cli_job_guard.h"

namespace ymcc::input {
// Optional, one-shot evidence transport. A delete-on-close spool cannot block
// the helper on pipe capacity; it is never a persistent receipt or a poller.
// Only the two exact standard handles and explicit caller-owned tracking
// leases may be inherited by a tracked launch.
class OneShotHelperDiagnostic {
public:
    OneShotHelperDiagnostic() = default;
    OneShotHelperDiagnostic(const OneShotHelperDiagnostic&) = delete;
    OneShotHelperDiagnostic& operator=(const OneShotHelperDiagnostic&) = delete;
    ~OneShotHelperDiagnostic() { cleanup(); }
    bool prepare(const std::wstring& testDirectory = {}) noexcept {
        try {
            std::wstring directory = testDirectory;
            if (directory.empty()) {
                wchar_t buffer[MAX_PATH + 1]{};
                const DWORD size = GetTempPathW(MAX_PATH, buffer);
                if (!size || size >= MAX_PATH) return false;
                directory.assign(buffer, size);
            }
            if (directory.back() != L'\\' && directory.back() != L'/') directory += L'\\';
            static std::atomic<unsigned long long> sequence{0};
            const auto path = directory + L"ymcc-input-one-shot-" + std::to_wstring(GetCurrentProcessId()) +
                L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(sequence++) + L".stdout.tmp";
            file_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (file_ == INVALID_HANDLE_VALUE) { file_ = nullptr; return false; }
            if (!DuplicateHandle(GetCurrentProcess(), file_, GetCurrentProcess(), &output_, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
                cleanup(); return false;
            }
            SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
            null_ = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (null_ == INVALID_HANDLE_VALUE) { null_ = nullptr; cleanup(); return false; }
            SIZE_T size = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
            if (!size) { cleanup(); return false; }
            attributes_.resize(size);
            auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes_.data());
            if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) { cleanup(); return false; }
            initialized_ = true;
            inherited_ = {output_, null_};
            if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited_.data(),
                    sizeof(inherited_), nullptr, nullptr)) { cleanup(); return false; }
            startup_ = {};
            startup_.StartupInfo.cb = sizeof(startup_);
            startup_.StartupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            startup_.StartupInfo.wShowWindow = SW_HIDE;
            startup_.StartupInfo.hStdOutput = output_;
            startup_.StartupInfo.hStdInput = startup_.StartupInfo.hStdError = null_;
            startup_.lpAttributeList = list;
            return true;
        } catch (...) { cleanup(); return false; }
    }
    LPSTARTUPINFOW startup() noexcept { return &startup_.StartupInfo; }
    // Compose stdout HANDLE_LIST and exact JOB_LIST in one owner. The
    // diagnostic spool still owns its handles until the real launch ends.
    bool prepareTrackedLaunch(ymcc::hidHideCli::JobGuard& job,const std::wstring& name,
                              const std::vector<HANDLE>& trackingLeases={}) noexcept {
        if(!initialized_ || !output_ || !null_)return false;
        try {
            auto ordinary=startup_.StartupInfo;ordinary.cb=sizeof(STARTUPINFOW);
            std::vector<HANDLE> handles(inherited_.begin(),inherited_.end());
            handles.insert(handles.end(),trackingLeases.begin(),trackingLeases.end());
            return job.prepareTracked(ordinary,name,handles);
        } catch(...) { return false; }
    }
    void started() noexcept { closeLaunchHandles(); }
    // Caller proves process termination using its existing handle/wait. Never
    // read while the child runs, add a wait, or change the helper's exit result.
    nlohmann::json terminal(DWORD exitCode) noexcept {
        try {
            using Json = nlohmann::json;
            Json result = {{"transport", "delete-on-close-stdout"}, {"state", "record-missing"},
                {"scope", "helper-self-reported-existing-call-facts"}, {"hardwareSuccessProven", false}};
            LARGE_INTEGER size{};
            if (!file_ || !GetFileSizeEx(file_, &size) || size.QuadPart < 0) {
                result["state"] = "capture-read-unavailable"; return result;
            }
            constexpr DWORD cap = 2048;
            result["stdoutBytes"] = size.QuadPart;
            if (size.QuadPart > cap) { result["state"] = "record-oversize"; return result; }
            const auto count = static_cast<DWORD>(size.QuadPart);
            std::string text(count, '\0');
            LARGE_INTEGER start{};
            DWORD read = 0;
            if (!SetFilePointerEx(file_, start, nullptr, FILE_BEGIN) ||
                (count && (!ReadFile(file_, text.data(), count, &read, nullptr) || read != count))) {
                result["state"] = "capture-read-unavailable"; return result;
            }
            if (text.empty()) return result;
            constexpr char prefix[] = "YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ";
            constexpr size_t prefixSize = sizeof(prefix) - 1;
            if (text.compare(0, prefixSize, prefix) != 0) { result["state"] = "record-prefix-invalid"; return result; }
            if (text.find('\0') != std::string::npos) { result["state"] = "record-schema-invalid"; return result; }
            bool duplicateKey = false;
            std::vector<std::vector<std::string>> objectKeys;
            auto observeKeys = [&](int, Json::parse_event_t event, Json& value) {
                if (event == Json::parse_event_t::object_start) objectKeys.emplace_back();
                else if (event == Json::parse_event_t::object_end) {
                    if (!objectKeys.empty()) objectKeys.pop_back();
                } else if (event == Json::parse_event_t::key && !objectKeys.empty()) {
                    const auto& key = value.get_ref<const std::string&>();
                    for (const auto& prior : objectKeys.back()) if (prior == key) duplicateKey = true;
                    objectKeys.back().push_back(key);
                }
                return true;
            };
            auto record = Json::parse(text.begin() + prefixSize, text.end(), observeKeys, false);
            auto exactInteger = [&](const char* key, unsigned expected) {
                const auto it = record.find(key);
                if (it == record.end() || !it->is_number_integer()) return false;
                return it->is_number_unsigned() ? it->get<unsigned long long>() == expected :
                    it->get<long long>() == static_cast<long long>(expected);
            };
            if (!record.is_object() || duplicateKey || !exactInteger("Schema", 1) ||
                !record.contains("Kind") || !record["Kind"].is_string() ||
                !record.contains("Stage") || !record["Stage"].is_string()) {
                result["state"] = "record-schema-invalid"; return result;
            }
            const bool authorityFailure = record["Kind"] == "ymcc.gameAuthority.failure";
            const bool refreshFailure = record["Kind"] == "ymcc.gameRefresh.failure";
            DWORD expectedExit = 0;
            if (authorityFailure) {
                // Preserve the existing Result=8 protocol and its original facts.
                if (!exactInteger("Result", 8) ||
                    (record["Stage"] != "first" && record["Stage"] != "after-prepare")) {
                    result["state"] = "record-schema-invalid"; return result;
                }
                expectedExit = 8;
            } else if (refreshFailure) {
                const auto& stage = record["Stage"];
                const auto optionalBool = [&](const char* key, bool nullable = false) {
                    const auto it = record.find(key);
                    return it == record.end() || it->is_boolean() || (nullable && it->is_null());
                };
                const auto optionalUint32 = [&](const char* key) {
                    const auto it = record.find(key);
                    if (it == record.end() || it->is_null()) return true;
                    if (!it->is_number_integer()) return false;
                    return it->is_number_unsigned() ? it->get<unsigned long long>() <= 0xffffffffULL :
                        (it->get<long long>() >= 0 && it->get<long long>() <= 0xffffffffLL);
                };
                const auto optionalInt32 = [&](const char* key) {
                    const auto it = record.find(key);
                    if (it == record.end() || it->is_null()) return true;
                    if (!it->is_number_integer()) return false;
                    return it->is_number_unsigned() ? it->get<unsigned long long>() <= 0x7fffffffULL :
                        (it->get<long long>() >= (-2147483647LL - 1) && it->get<long long>() <= 2147483647LL);
                };
                const auto optionalExceptionType = [&](const char* key) {
                    const auto it = record.find(key);
                    return it == record.end() || it->is_null() ||
                        (it->is_string() && it->get_ref<const std::string&>().find('\0') == std::string::npos);
                };
                const auto hardware = record.find("HardwareSuccessProven");
                if (!exactInteger("Result", 6) ||
                    (stage != "admission" && stage != "prepare" && stage != "prepare-locate" && stage != "revalidate" &&
                     stage != "current-locate" && stage != "current-authority" && stage != "disable" && stage != "enable") ||
                    !optionalBool("Xusb") || !optionalBool("SameNode", true) ||
                    !optionalBool("DisableCallEntered") || !optionalBool("EnableCallEntered") ||
                    !optionalUint32("PrepareLocateResult") || !optionalUint32("CurrentLocateResult") ||
                    !optionalUint32("DisableResult") || !optionalUint32("EnableResult") ||
                    !optionalInt32("ExceptionHResult") || !optionalExceptionType("ExceptionType") ||
                    !optionalBool("CompensationChecked") || !optionalBool("CompensationAttempted") ||
                    !optionalBool("CompensationIdentityMatched", true) || !optionalBool("CompensationSameNode", true) ||
                    !optionalUint32("CompensationLocateResult") || !optionalUint32("CompensationEnableResult") ||
                    !optionalExceptionType("CompensationExceptionType") || !optionalInt32("CompensationExceptionHResult") ||
                    (hardware != record.end() && (!hardware->is_boolean() || hardware->get<bool>()))) {
                    result["state"] = "record-schema-invalid"; return result;
                }
                expectedExit = 6;
            } else {
                result["state"] = "record-schema-invalid"; return result;
            }
            if (exitCode != expectedExit) { result["state"] = "record-exit-mismatch"; return result; }
            result["state"] = "record-accepted";
            result["reportedFacts"] = std::move(record);
            return result;
        } catch (...) { return nullptr; }
    }
private:
    void closeLaunchHandles() noexcept {
        if (output_) { CloseHandle(output_); output_ = nullptr; }
        if (null_) { CloseHandle(null_); null_ = nullptr; }
        if (initialized_) {
            DeleteProcThreadAttributeList(reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes_.data()));
            initialized_ = false;
        }
    }
    void cleanup() noexcept {
        closeLaunchHandles();
        if (file_) { CloseHandle(file_); file_ = nullptr; }
    }
    HANDLE file_ = nullptr, output_ = nullptr, null_ = nullptr;
    bool initialized_ = false;
    std::array<HANDLE, 2> inherited_{};
    std::vector<unsigned char> attributes_;
    STARTUPINFOEXW startup_{};
};
}
