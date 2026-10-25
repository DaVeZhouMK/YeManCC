#pragma once
#include "fxsound_recovery_policy.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <endpointvolume.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <atomic>
#include <thread>
#include <functional>
#include <cwctype>
#include <new>

namespace ymcc::fxsound {
using Microsoft::WRL::ComPtr;
// ABI also used by FxSound's audiopassthru/include/PolicyConfig.h.
struct __declspec(uuid("f8679f50-850a-41cf-9c72-430f290290c8")) PolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
struct __declspec(uuid("870af99c-171d-4f9e-af0d-e63df40c2bc9")) PolicyClient;
inline std::wstring lower(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(towlower(ch));
    return text;
}
inline std::wstring property(IPropertyStore* store, const PROPERTYKEY& key) {
    PROPVARIANT v; PropVariantInit(&v);
    std::wstring value;
    if (SUCCEEDED(store->GetValue(key, &v)) && v.vt == VT_LPWSTR && v.pwszVal) value = v.pwszVal;
    PropVariantClear(&v); return value;
}
inline ProcessState processState() {
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return ProcessState::unknown;
    HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (handle == INVALID_HANDLE_VALUE) return ProcessState::unknown;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    bool uncertain = false;
    if (!Process32FirstW(handle, &entry)) { CloseHandle(handle); return ProcessState::unknown; }
    do {
        if (_wcsicmp(entry.szExeFile, L"FxSound.exe") != 0) continue;
        DWORD owner = 0;
        if (!ProcessIdToSessionId(entry.th32ProcessID, &owner)) { uncertain = true; continue; }
        if (owner == session) { CloseHandle(handle); return ProcessState::running; }
    } while (Process32NextW(handle, &entry));
    const DWORD error = GetLastError(); CloseHandle(handle);
    return uncertain || error != ERROR_NO_MORE_FILES ? ProcessState::unknown : ProcessState::stopped;
}
inline std::vector<std::wstring> savedOutputs() {
    std::vector<std::wstring> ids;
    // FxSound writes these default REG_SZ values under Software\DFX\<version>\23\devices.
    for (const REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        HKEY root = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\DFX", 0, KEY_READ | view, &root) != ERROR_SUCCESS) continue;
        std::vector<std::pair<unsigned, std::wstring>> versions;
        for (DWORD i = 0; ; ++i) {
            wchar_t name[256]; DWORD size = 256;
            const auto error = RegEnumKeyExW(root, i, name, &size, nullptr, nullptr, nullptr, nullptr);
            if (error == ERROR_NO_MORE_ITEMS) break;
            if (error != ERROR_SUCCESS) break;
            const std::wstring text(name, size);
            if (text.empty() || text.size() > 5 || text.find_first_not_of(L"0123456789") != std::wstring::npos) continue;
            versions.push_back({static_cast<unsigned>(std::stoul(text)), text});
        }
        std::sort(versions.rbegin(), versions.rend());
        for (const auto& version : versions) {
            for (const auto* leaf : {L"most_recent_default", L"original_default", L"most_recent_playback", L"prior_default"}) {
                const auto path = version.second + L"\\23\\devices\\" + leaf;
                wchar_t value[1024]{}; DWORD bytes = sizeof(value), type = 0;
                if (RegGetValueW(root, path.c_str(), nullptr, RRF_RT_REG_SZ, &type, value, &bytes) == ERROR_SUCCESS && value[0]) {
                    std::wstring id(value);
                    if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(std::move(id));
                }
            }
        }
        RegCloseKey(root);
    }
    return ids;
}
class WindowsPort {
    IMMDeviceEnumerator* enumerator_;
    const std::atomic<bool>& enabled_;
public:
    WindowsPort(IMMDeviceEnumerator* enumerator, const std::atomic<bool>& enabled) : enumerator_(enumerator), enabled_(enabled) {}
    Snapshot read() const {
        Snapshot s;
        ComPtr<IMMDeviceCollection> list;
        if (FAILED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &list))) return s;
        UINT count = 0; if (FAILED(list->GetCount(&count))) return s;
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> endpoint; ComPtr<IPropertyStore> store;
            if (FAILED(list->Item(i, &endpoint)) || FAILED(endpoint->OpenPropertyStore(STGM_READ, &store))) return s;
            LPWSTR id = nullptr; if (FAILED(endpoint->GetId(&id))) return s;
            Device d; d.id = id; CoTaskMemFree(id);
            d.name = property(store.Get(), PKEY_Device_FriendlyName);
            const auto names = lower(d.name + L" " + property(store.Get(), PKEY_DeviceInterface_FriendlyName));
            d.fx = names.find(L"fxsound") != std::wstring::npos;
            PROPVARIANT form; PropVariantInit(&form);
            if (SUCCEEDED(store->GetValue(PKEY_AudioEndpoint_FormFactor, &form)) && form.vt == VT_UI4) d.form = form.ulVal;
            PropVariantClear(&form);
            bool virtualDevice = d.fx || names.empty();
            for (const auto* token : {L"virtual", L"voicemeeter", L"vb-audio", L"vb audio", L"cable", L"sonar", L"nahimic mirroring", L"steam streaming", L"nvidia broadcast", L"虚拟"})
                if (names.find(token) != std::wstring::npos) virtualDevice = true;
            // Include physical digital/monitor outputs, which may be the saved system default.
            d.usable = !virtualDevice && (d.form == Speakers || d.form == Headphones || d.form == Headset ||
                d.form == LineLevel || d.form == SPDIF || d.form == DigitalAudioDisplayDevice);
            s.devices.push_back(std::move(d));
        }
        for (unsigned role = 0; role < 3; ++role) {
            ComPtr<IMMDevice> endpoint;
            const auto hr = enumerator_->GetDefaultAudioEndpoint(eRender, static_cast<ERole>(role), &endpoint);
            if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) continue;
            if (FAILED(hr)) return s;
            LPWSTR id = nullptr; if (FAILED(endpoint->GetId(&id))) return s;
            s.defaults[role] = id; CoTaskMemFree(id);
        }
        s.preferred = savedOutputs();
        s.process = s.hasFxDefault() ? processState() : ProcessState::unknown;
        s.valid = true; return s;
    }
    SetResult setDefault(unsigned role, const std::wstring& source, const std::wstring& target) const {
        ComPtr<PolicyConfig> policy;
        const auto hr = CoCreateInstance(__uuidof(PolicyClient), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&policy));
        if (FAILED(hr)) return {false, hr};
        const auto current = read();
        const auto* output = current.find(target);
        if (!enabled_.load() || !current.valid || current.process != ProcessState::stopped ||
            !current.isFx(role) || current.defaults[role] != source || !output || !output->usable || output->fx)
            return {false, HRESULT_FROM_WIN32(ERROR_CANCELLED)};
        const auto result = policy->SetDefaultEndpoint(target.c_str(), static_cast<ERole>(role));
        if (FAILED(result)) return {false, result};
        ComPtr<IMMDevice> endpoint;
        auto verify = enumerator_->GetDefaultAudioEndpoint(eRender, static_cast<ERole>(role), &endpoint);
        LPWSTR id = nullptr;
        if (SUCCEEDED(verify)) verify = endpoint->GetId(&id);
        const bool ok = SUCCEEDED(verify) && id && target == id;
        if (id) CoTaskMemFree(id);
        return {ok, ok ? S_OK : FAILED(verify) ? verify : E_FAIL};
    }
};
class Service {
    std::atomic<bool> enabled_{false}, stopping_{false};
    bool started_ = false;
    HANDLE wake_ = nullptr;
    std::thread worker_;
    std::function<void(const Attempt&)> report_;
    void run() {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized)) { report_({0, L"", L"", false, initialized}); return; }
        {
            ComPtr<IMMDeviceEnumerator> enumerator;
            const auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator));
            if (FAILED(hr)) report_({0, L"", L"", false, hr});
            else {
                WindowsPort port(enumerator.Get(), enabled_); Recovery recovery;
                while (!stopping_.load() && !recovery.finished) {
                    for (const auto& result : recovery.step(port, GetTickCount64(), enabled_.load())) report_(result);
                    if (!recovery.finished) WaitForSingleObject(wake_, 500);
                }
            }
        }
        CoUninitialize();
    }
public:
    Service() = default;
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    ~Service() { shutdown(); }
    bool start(bool enabled, std::function<void(const Attempt&)> report) {
        if (started_) return true;
        started_ = true;
        // Enabling in Settings only affects the next YMCC launch.
        if (!enabled) return true;
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!wake_) return false;
        report_ = std::move(report); enabled_.store(true); stopping_.store(false);
        try { worker_ = std::thread([this] {
            try { run(); } catch (...) { try { report_({0,L"",L"",false,E_FAIL}); } catch (...) {} }
        }); }
        catch (...) { CloseHandle(wake_); wake_ = nullptr; return false; }
        return true;
    }
    void configure(bool enabled) {
        // OFF can cancel the pending startup check. ON never rearms this process.
        if (!enabled) enabled_.store(false);
    }
    void shutdown() {
        enabled_.store(false); stopping_.store(true);
        if (wake_) SetEvent(wake_);
        if (worker_.joinable()) worker_.join();
        if (wake_) { CloseHandle(wake_); wake_ = nullptr; }
    }
};
}
