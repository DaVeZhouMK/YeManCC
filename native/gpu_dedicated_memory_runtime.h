#pragma once
// Windows implementation. Construction and registration have no device writes.
// References: G-Helper bf64c632 Intel GMM and ASUS APU/VRAM protocol; GPL-3.0.
#include "gpu_dedicated_memory_contract.h"
#include <cstddef>
#include <windows.h>
#include <dxgi1_2.h>
#include <dxcore.h>
#include <d3d12.h>
#include <d3dkmthk.h>
#include <setupapi.h>
#include <wrl/client.h>
#include "json.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#pragma comment(lib, "setupapi.lib")
namespace ymcc::dedicatedmemory {
using Json = nlohmann::json;
inline constexpr const char* readerRevision = "ymcc-gpu-memory-r8-20261014";
using Microsoft::WRL::ComPtr;
inline std::mutex& runtimeMutex() { static std::mutex m; return m; }
inline std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    const auto n = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if (n <= 0) return {};
    std::string result(static_cast<std::size_t>(n),'\0');
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),result.data(),n,nullptr,nullptr)) return {};
    return result;
}
inline std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const auto n = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if (n <= 0) return {};
    std::wstring result(static_cast<std::size_t>(n),L'\0');
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),result.data(),n)) return {};
    return result;
}
struct Module {
    HMODULE value = nullptr;
    explicit Module(const wchar_t* name) : value(LoadLibraryExW(name,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)) {}
    ~Module() { if (value) FreeLibrary(value); }
    Module(const Module&) = delete;
};
struct RegKey { HKEY value = nullptr; ~RegKey() { if (value) RegCloseKey(value); } };
struct Handle { HANDLE value = INVALID_HANDLE_VALUE; ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
// Prefix through BootId from the published Windows KUSER_SHARED_DATA layout.
// x64 Windows 10/11 map this read-only page at 0x7ffe0000. Copy it through the
// checked process-memory API; no unchecked dereference or registry boot heuristic.
struct SharedLoaderBootData {
    DWORD tickCountLow, tickCountMultiplier;
    LONG interruptTime[3], systemTime[3], timeZoneBias[3];
    WORD imageNumberLow, imageNumberHigh;
    WCHAR ntSystemRoot[260];
    DWORD maxStackTraceDepth, cryptoExponent, timeZoneId, largePageMinimum, aitSamplingValue, appCompatFlag;
    ULONGLONG rngSeedVersion;
    DWORD globalValidationRunlevel;
    LONG timeZoneBiasStamp;
    DWORD ntBuildNumber, ntProductType;
    BYTE productTypeIsValid, reserved0;
    WORD nativeProcessorArchitecture;
    DWORD ntMajorVersion, ntMinorVersion;
    BYTE processorFeatures[64];
    DWORD reserved1, reserved3, timeSlip, alternativeArchitecture, bootId;
};
static_assert(offsetof(SharedLoaderBootData,ntMajorVersion) == 0x26c);
static_assert(offsetof(SharedLoaderBootData,bootId) == 0x2c4);
inline std::optional<DWORD> loaderBootSequence() {
#if defined(_M_X64)
    SharedLoaderBootData info{}; SIZE_T readBytes = 0;
    constexpr SIZE_T bytes = offsetof(SharedLoaderBootData,bootId) + sizeof(DWORD);
    if (!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(std::uintptr_t(0x7ffe0000)),
        &info,bytes,&readBytes) || readBytes != bytes || info.ntMajorVersion != 10 || info.ntMinorVersion != 0 ||
        info.nativeProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64) return std::nullopt;
    return info.bootId;
#else
    return std::nullopt;
#endif
}
inline std::string bootIdentity() {
    struct BootInfo { GUID id; DWORD firmware; ULONGLONG flags; } info{};
    using Query = LONG (WINAPI*)(ULONG,void*,ULONG,ULONG*);
    const auto fn = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQuerySystemInformation"));
    ULONG returned = 0;
    if (!fn || fn(90,&info,sizeof(info),&returned) < 0 || returned < sizeof(GUID)) return {};
    if (IsEqualGUID(info.id,GUID{})) return {};
    wchar_t text[40]{};
    if (!StringFromGUID2(info.id,text,40)) return {};
    const auto sequence = loaderBootSequence();
    if (!sequence) return {};
    return "windows-loader-v1|" + utf8(text) + "|" + std::to_string(*sequence);
}
inline std::wstring regString(HKEY key, const wchar_t* name) {
    DWORD type = 0, bytes = 0;
    if (RegQueryValueExW(key,name,nullptr,&type,nullptr,&bytes) != ERROR_SUCCESS || type != REG_SZ ||
        bytes < sizeof(wchar_t) || bytes > 8192 || bytes % sizeof(wchar_t)) return {};
    std::vector<wchar_t> text(bytes / sizeof(wchar_t) + 1,0);
    if (RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(text.data()),&bytes) != ERROR_SUCCESS || type != REG_SZ) return {};
    return text.data();
}
inline std::wstring devString(HDEVINFO set, SP_DEVINFO_DATA& node, DWORD property) {
    std::array<wchar_t,4096> text{}; DWORD type = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set,&node,property,&type,reinterpret_cast<BYTE*>(text.data()),
                                          static_cast<DWORD>(sizeof(text)),nullptr) || type != REG_SZ) return {};
    text.back() = 0; return text.data();
}
inline bool devNumber(HDEVINFO set, SP_DEVINFO_DATA& node, DWORD property, DWORD& number) {
    DWORD type = 0, size = 0;
    return SetupDiGetDeviceRegistryPropertyW(set,&node,property,&type,reinterpret_cast<BYTE*>(&number),sizeof(number),&size) &&
        type == REG_DWORD && size == sizeof(number);
}
inline bool driverReference(const std::wstring& value) {
    constexpr auto prefix = L"{4d36e968-e325-11ce-bfc1-08002be10318}\\";
    const auto n = wcslen(prefix);
    return value.size() == n + 4 && _wcsnicmp(value.c_str(),prefix,n) == 0 &&
        std::all_of(value.begin()+static_cast<std::ptrdiff_t>(n),value.end(),[](wchar_t c){return c >= L'0' && c <= L'9';});
}
struct Binding { std::wstring pnpId, registryKey, driverVersion; std::string source; };
inline std::optional<std::wstring> kernelDriverReference(const std::wstring& path) {
    const std::wstring prefix = L"\\Registry\\Machine\\SYSTEM\\";
    if (path.size() <= prefix.size() || _wcsnicmp(path.c_str(),prefix.c_str(),prefix.size()) != 0) return std::nullopt;
    const auto rest = path.substr(prefix.size()); const auto split = rest.find(L'\\');
    if (split == std::wstring::npos) return std::nullopt;
    const auto controlSet = rest.substr(0,split);
    if (_wcsicmp(controlSet.c_str(),L"CurrentControlSet") != 0) {
        if (controlSet.size() != 13 || _wcsnicmp(controlSet.c_str(),L"ControlSet",10) != 0 ||
            !std::all_of(controlSet.begin()+10,controlSet.end(),[](wchar_t c){return c >= L'0' && c <= L'9';})) return std::nullopt;
        RegKey select; DWORD type = 0, current = 0, bytes = sizeof(current);
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\Select",0,KEY_READ|KEY_WOW64_64KEY,&select.value) != ERROR_SUCCESS ||
            RegQueryValueExW(select.value,L"Current",nullptr,&type,reinterpret_cast<BYTE*>(&current),&bytes) != ERROR_SUCCESS ||
            type != REG_DWORD || bytes != sizeof(current) || current == 0 || current > 999) return std::nullopt;
        const auto selected = static_cast<DWORD>((controlSet[10]-L'0')*100 + (controlSet[11]-L'0')*10 + controlSet[12]-L'0');
        if (selected != current) return std::nullopt;
    }
    const std::wstring classPath = L"\\Control\\Class\\";
    if (_wcsnicmp(rest.c_str()+split,classPath.c_str(),classPath.size()) != 0) return std::nullopt;
    const auto reference = rest.substr(split+classPath.size());
    return driverReference(reference) ? std::optional<std::wstring>(reference) : std::nullopt;
}
inline std::optional<Binding> bindingFor(const LUID& luid, std::string* reason = nullptr, Json* diagnostics = nullptr) {
    const auto fail = [&](const char* stage) -> std::optional<Binding> {
        if (reason) *reason = std::string("[adapter.binding.") + stage + "] 无法确认当前适配器的驱动节点";
        return std::nullopt;
    };
    D3DKMT_OPENADAPTERFROMLUID open{}; open.AdapterLuid = luid;
    if (D3DKMTOpenAdapterFromLuid(&open) < 0) return fail("open");
    struct AdapterHandle {
        D3DKMT_HANDLE value;
        ~AdapterHandle() { D3DKMT_CLOSEADAPTER close{}; close.hAdapter=value; D3DKMTCloseAdapter(&close); }
    } handle{open.hAdapter};
    const auto queryInfo = [&](KMTQUERYADAPTERINFOTYPE type, void* data, UINT size) {
        D3DKMT_QUERYADAPTERINFO query{}; query.hAdapter=handle.value; query.Type=type;
        query.pPrivateDriverData=data; query.PrivateDriverDataSize=size;
        const auto status=D3DKMTQueryAdapterInfo(&query);
        if (diagnostics) (*diagnostics)["kmtStatus"][std::to_string(static_cast<int>(type))]=static_cast<std::uint32_t>(status);
        return status >= 0;
    };
    // Windows supplies this key for the same LUID, so present devices do not need optional PCI properties.
    std::optional<std::wstring> direct;
    D3DKMT_PHYSICAL_ADAPTER_COUNT count{};
    if (queryInfo(KMTQAITYPE_PHYSICALADAPTERCOUNT,&count,sizeof(count))) {
        if (diagnostics) (*diagnostics)["physicalAdapterCount"]=count.Count;
        if (count.Count != 1) return fail("physical-count");
        std::array<wchar_t,4096> path{}; UINT chars=static_cast<UINT>(path.size());
        D3DKMT_QUERY_PHYSICAL_ADAPTER_PNP_KEY key{}; key.PnPKeyType=D3DKMT_PNP_KEY_SOFTWARE;
        key.pDest=path.data(); key.pCchDest=&chars;
        if (queryInfo(KMTQAITYPE_PHYSICALADAPTERPNPKEY,&key,sizeof(key))) {
            if (chars < 2 || chars > path.size() || path[chars-1] != 0 ||
                wcsnlen(path.data(),path.size()) != chars-1) return fail("pnp-key-buffer");
            if (diagnostics) (*diagnostics)["kernelSoftwareKey"]=utf8(path.data());
            direct=kernelDriverReference(path.data());
            if (!direct) return fail("pnp-key-path");
        }
    }
    D3DKMT_ADAPTERADDRESS address{};
    const bool hasAddress=queryInfo(KMTQAITYPE_ADAPTERADDRESS,&address,sizeof(address));
    if (diagnostics && hasAddress) (*diagnostics)["pciAddress"]={{"bus",address.BusNumber},{"device",address.DeviceNumber},{"function",address.FunctionNumber}};
    if (!direct && !hasAddress) return fail("routes-unavailable");
    constexpr GUID display{0x4d36e968,0xe325,0x11ce,{0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18}};
    const auto devices=SetupDiGetClassDevsW(&display,nullptr,nullptr,DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return fail("present-devices");
    struct DeviceSet { HDEVINFO value; ~DeviceSet(){SetupDiDestroyDeviceInfoList(value);} } set{devices};
    std::vector<Binding> found; bool complete=false;
    for (DWORD index=0;index<128;++index) {
        SP_DEVINFO_DATA node{}; node.cbSize=sizeof(node);
        if (!SetupDiEnumDeviceInfo(devices,index,&node)) {complete=GetLastError()==ERROR_NO_MORE_ITEMS;break;}
        const auto reference=devString(devices,node,SPDRP_DRIVER);
        if (!driverReference(reference)) continue;
        DWORD bus=0,slot=0;
        const bool hasPci=devNumber(devices,node,SPDRP_BUSNUMBER,bus) && devNumber(devices,node,SPDRP_ADDRESS,slot);
        const bool pciMatch=hasAddress && hasPci && bus==address.BusNumber &&
            (slot>>16)==address.DeviceNumber && (slot&0xffff)==address.FunctionNumber;
        if (direct) {
            if (_wcsicmp(reference.c_str(),direct->c_str()) != 0) continue;
            if (hasAddress && hasPci && !pciMatch) return fail("identity-conflict");
        } else if (!pciMatch) continue;
        std::array<wchar_t,4096> instance{};
        if (!SetupDiGetDeviceInstanceIdW(devices,&node,instance.data(),static_cast<DWORD>(instance.size()),nullptr) ||
            _wcsnicmp(instance.data(),L"PCI\\",4) != 0) return fail("instance-id");
        Binding binding{instance.data(),L"SYSTEM\\CurrentControlSet\\Control\\Class\\"+reference,{},direct ? "kernel-pnp" : "pci-address"};
        RegKey key;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,binding.registryKey.c_str(),0,KEY_READ|KEY_WOW64_64KEY,&key.value) != ERROR_SUCCESS) return fail("driver-open");
        binding.driverVersion=regString(key.value,L"DriverVersion");
        if (binding.driverVersion.empty()) return fail("driver-version");
        found.push_back(std::move(binding));
    }
    if (!complete) return fail("enumeration");
    if (found.size()!=1) return fail(found.empty() ? "no-match" : "ambiguous");
    return found.front();
}
inline bool openIntelParent(const Probe& p, REGSAM access, RegKey& parent, std::string* reason = nullptr,
                            Json* dispatch = nullptr) {
    const auto fail = [&](const std::string& stage, const std::string& text, std::optional<LSTATUS> rc = std::nullopt) {
        const auto message = "[" + stage + "] " + text;
        if (reason) *reason = message;
        if (dispatch) { (*dispatch)["stage"] = stage; (*dispatch)["reason"] = message;
            (*dispatch)["win32Error"] = rc ? Json(*rc) : Json(nullptr); }
        return false;
    };
    const auto path = wide(p.registryKey);
    const std::wstring prefix = L"SYSTEM\\CurrentControlSet\\Control\\Class\\";
    if (p.provider != "intel-gmm" || path.rfind(prefix,0) != 0 || !driverReference(path.substr(prefix.size())))
        return fail("intel.parent.binding","显示驱动节点绑定无效");
    if (dispatch) (*dispatch)["api"] = "RegOpenKeyExW";
    const auto rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,access|KEY_QUERY_VALUE|KEY_WOW64_64KEY,&parent.value);
    if (rc != ERROR_SUCCESS) return fail("intel.parent.open","无法打开当前驱动节点，Win32=" + std::to_string(rc),rc);
    if (dispatch) (*dispatch)["api"] = "RegQueryValueExW";
    const auto driver = regString(parent.value,L"DriverVersion");
    if (driver.empty()) return fail("intel.parent.driver-read","无法读取当前驱动节点的版本");
    if (p.driverVersion.rfind(utf8(driver) + ":",0) != 0) return fail("intel.parent.driver-changed","当前驱动版本与适配器绑定不一致");
    return true;
}
inline bool readIntelSetting(Probe& p) {
    RegKey parent, key;
    if (!openIntelParent(p,0,parent,&p.reason)) return false;
    const auto rc = RegOpenKeyExW(parent.value,L"GMM",0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key.value);
    if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND) { p.configuredRaw.reset(); return true; }
    if (rc != ERROR_SUCCESS) { p.reason = "[intel.config.open] 无法读取 GMM 节点，Win32=" + std::to_string(rc); return false; }
    DWORD type = 0, value = 0, size = sizeof(value);
    const auto readRc = RegQueryValueExW(key.value,L"DedicatedSegmentSize",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size);
    if (readRc == ERROR_FILE_NOT_FOUND) { p.configuredRaw.reset(); return true; }
    if (readRc != ERROR_SUCCESS) { p.reason = "[intel.config.read] 无法读取 DedicatedSegmentSize，Win32=" + std::to_string(readRc); return false; }
    if (type != REG_DWORD || size != sizeof(value)) { p.reason = "[intel.config.type] DedicatedSegmentSize 不是完整 DWORD"; return false; }
    // Unknown or out-of-range values are not converted to Auto/default.
    if (std::none_of(p.options.begin(),p.options.end(),[&](const Option& o){return !o.removeOverride && o.raw == value;})) {
        p.reason = "[intel.config.unknown] DedicatedSegmentSize=" + std::to_string(value) + " MiB 不属于当前可确认档位"; return false;
    }
    p.configuredRaw = value; return true;
}
inline bool writeIntelSetting(const Probe& p, const Option& option, Json* dispatch = nullptr) {
    if (dispatch) *dispatch = Json{{"mechanism","intel-gmm-registry"},{"acknowledgementScope","registry-only"},
        {"driverAcceptance","not_reported"},{"stage","intel.write.validate"},{"api",nullptr},
        {"win32Error",nullptr},{"valueWriteAttempted",false},{"acknowledged",false}};
    if (!admitted(p) || !offered(p,option)) return false;
    RegKey parent, key;
    if (!openIntelParent(p,option.removeOverride ? 0 : KEY_CREATE_SUB_KEY,parent,nullptr,dispatch)) return false;
    const auto status = [&](const char* stage, const char* api, LSTATUS rc, bool accepted) {
        if (dispatch) {
            (*dispatch)["stage"] = stage; (*dispatch)["api"] = api;
            (*dispatch)["win32Error"] = rc; (*dispatch)["acknowledged"] = accepted;
            if (!accepted) (*dispatch)["reason"] = std::string("[") + stage + "] 专用显存配置操作失败，Win32=" + std::to_string(rc);
        }
        return accepted;
    };
    if (option.removeOverride) {
        const auto rc = RegOpenKeyExW(parent.value,L"GMM",0,KEY_SET_VALUE|KEY_WOW64_64KEY,&key.value);
        if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND)
            return status("intel.config.absent","RegOpenKeyExW",rc,true);
        if (rc != ERROR_SUCCESS) return status("intel.config.open-write","RegOpenKeyExW",rc,false);
        if (dispatch) (*dispatch)["valueWriteAttempted"] = true;
        const auto erased = RegDeleteValueW(key.value,L"DedicatedSegmentSize");
        return status("intel.config.delete","RegDeleteValueW",erased,erased == ERROR_SUCCESS || erased == ERROR_FILE_NOT_FOUND);
    }
    const auto created = RegCreateKeyExW(parent.value,L"GMM",0,nullptr,0,KEY_SET_VALUE|KEY_WOW64_64KEY,nullptr,&key.value,nullptr);
    if (created != ERROR_SUCCESS) return status("intel.config.create","RegCreateKeyExW",created,false);
    const DWORD raw = option.raw;
    if (dispatch) (*dispatch)["valueWriteAttempted"] = true;
    const auto written = RegSetValueExW(key.value,L"DedicatedSegmentSize",0,REG_DWORD,reinterpret_cast<const BYTE*>(&raw),sizeof(raw));
    return status("intel.config.set","RegSetValueExW",written,written == ERROR_SUCCESS);
}
inline bool rogClass(const std::string& name) {
    return name == "HandheldCompanion.Devices.ROGAlly" || name == "HandheldCompanion.Devices.ROGAllyX" ||
        name == "HandheldCompanion.Devices.XboxROGAlly" || name == "HandheldCompanion.Devices.XboxROGAllyX";
}
inline bool legacyRogClass(const std::string& name) {
    return name == "HandheldCompanion.Devices.ROGAlly" || name == "HandheldCompanion.Devices.ROGAllyX";
}
inline bool asusCall(std::uint32_t method, std::uint32_t device, std::optional<std::uint32_t> value,
                     bool large, std::vector<std::uint8_t>& result, Json* diagnostics = nullptr) {
    if (diagnostics) *diagnostics = Json{{"method",method},{"deviceId",device},{"ioctl",0x0022240c},
        {"requestedRaw",value ? Json(*value) : Json(nullptr)},{"transportOk",false},
        {"returnedBytes",nullptr},{"win32Error",nullptr},{"stage","asus.device.open"},{"api","CreateFileW"}};
    const auto failure = [&](const char* reason) {
        if (diagnostics) (*diagnostics)["reason"] = reason;
        return false;
    };
    Handle handle;
    handle.value = CreateFileW(L"\\\\.\\ATKACPI",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                               nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (handle.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (diagnostics) (*diagnostics)["win32Error"] = error;
        return failure("无法打开 ASUS 固件接口");
    }
    std::array<std::uint32_t,5> input{method,large ? 12u : 8u,device,value.value_or(0),0};
    result.assign(large ? 64 : 16,0); DWORD returned = 0;
    if (diagnostics) { (*diagnostics)["stage"] = "asus.ioctl"; (*diagnostics)["api"] = "DeviceIoControl"; }
    if (!DeviceIoControl(handle.value,0x0022240c,input.data(),large ? 20u : 16u,result.data(),
                         static_cast<DWORD>(result.size()),&returned,nullptr)) {
        const auto error = GetLastError();
        if (diagnostics) (*diagnostics)["win32Error"] = error;
        return failure("ASUS 固件请求失败");
    }
    if (diagnostics) {
        (*diagnostics)["transportOk"] = true; (*diagnostics)["win32Error"] = ERROR_SUCCESS;
        (*diagnostics)["returnedBytes"] = returned; (*diagnostics)["stage"] = "asus.reply";
    }
    if (returned < 4 || returned > result.size()) return failure("ASUS 固件返回长度无效");
    result.resize(returned); return true;
}
inline std::uint32_t head(const std::vector<std::uint8_t>& v) {
    if (v.size() < 4) return 0;
    return std::uint32_t(v[0]) | (std::uint32_t(v[1]) << 8) | (std::uint32_t(v[2]) << 16) | (std::uint32_t(v[3]) << 24);
}
inline bool readRogSetting(Probe& p, Json* diagnostics = nullptr) {
    Json replies = Json::array();
    const auto publish = [&] {
        if (diagnostics) (*diagnostics)["asusMemory"] = Json{{"reads",replies},{"reason",p.reason}};
    };
    const auto failure = [&](const char* stage,const char* reason) {
        p.reason = std::string("[") + stage + "] " + reason; publish(); return false;
    };
    if (!rogClass(p.deviceClass)) return failure("rog.model","当前机型没有匹配的 ASUS 显存协议");
    const auto request = [&](std::uint32_t device,bool large,std::vector<std::uint8_t>& buffer) {
        Json reply;
        const auto ok = asusCall(0x53545344,device,std::nullopt,large,buffer,&reply);
        if (ok) reply["firmwareReturn"] = head(buffer);
        replies.push_back(std::move(reply)); return ok;
    };
    std::vector<std::uint8_t> buffer;
    if (!request(0x000600c4,true,buffer)) return failure("rog.vram.read","无法完整读取 ASUS 显存配置");
    if (head(buffer) & 0x10000u) {
        std::uint32_t current = 0;
        const auto choices = rogVramOptions(buffer,p.installedBytes,current);
        if (!choices) return failure("rog.vram.decode","ASUS 显存配置或可选档位无法确认");
        p.provider = "rog-vram"; p.options = *choices; p.configuredRaw = current; p.reason.clear(); publish();
        if (diagnostics) (*diagnostics)["asusMemory"]["optionsSource"] = "firmware-reply";
        return true;
    }
    if (head(buffer) != 0 || !legacyRogClass(p.deviceClass))
        return failure("rog.vram.unsupported","当前机型未提供可确认的 ASUS 显存配置");
    if (!request(0x000600c1,false,buffer)) return failure("rog.apu.read","无法完整读取 ASUS APU 显存配置");
    const auto current = decodeRogApu(head(buffer));
    if (!current) return failure("rog.apu.decode","ASUS APU 显存配置返回值无法确认");
    p.provider = "rog-apu"; p.options = rogApuOptions(p.installedBytes); p.configuredRaw = *current;
    if (!std::any_of(p.options.begin(),p.options.end(),[&](const Option& o){return o.raw == *current;}))
        return failure("rog.apu.option","ASUS APU 显存配置不属于当前可确认档位");
    p.reason.clear(); publish();
    if (diagnostics) (*diagnostics)["asusMemory"]["optionsSource"] = "model-specific-protocol-codes";
    return true;
}
inline bool writeRogSetting(const Probe& p, const Option& option, Json* dispatch = nullptr) {
    if (dispatch) *dispatch = Json{{"mechanism","asus-acpi-memory"},{"acknowledgementScope","firmware-method-return"},
        {"driverAcceptance","not_reported"},{"acknowledged",false},{"valueWriteAttempted",false},
        {"operation",p.provider == "rog-apu" ? "rog.apu.write" : "rog.vram.write"},{"stage","rog.write.validate"}};
    if (!admitted(p) || !rogClass(p.deviceClass) || !offered(p,option) ||
        (p.provider != "rog-apu" && p.provider != "rog-vram")) {
        if (dispatch) (*dispatch)["reason"] = "当前机型、适配器或显存档位未通过写入校验";
        return false;
    }
    std::vector<std::uint8_t> result; Json reply;
    const auto device = p.provider == "rog-apu" ? 0x000600c1u : 0x000600c4u;
    const bool transported = asusCall(0x53564544,device,option.raw,false,result,&reply);
    if (dispatch) {
        dispatch->update(reply);
        // Opening a device is not a mutation. The DEVS request begins at DeviceIoControl.
        (*dispatch)["valueWriteAttempted"] = reply.value("api",std::string{}) == "DeviceIoControl";
    }
    if (!transported) return false;
    const auto response = head(result); const bool acknowledged = response == 1;
    if (dispatch) {
        (*dispatch)["firmwareReturn"] = response; (*dispatch)["acknowledged"] = acknowledged;
        if (!acknowledged) (*dispatch)["reason"] = "ASUS 固件未确认显存设置请求";
    }
    return acknowledged;
}
inline std::string luidText(const LUID& value) {
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(8) << static_cast<std::uint32_t>(value.HighPart)
         << ':' << std::setw(8) << value.LowPart;
    return text.str();
}
inline HRESULT intelUmaIdentity(IDXGIAdapter1* adapter, const LUID& expected,
                                std::uint64_t& driverVersion, bool& excludedByKernelType, Json* detail = nullptr) {
    excludedByKernelType = false;
    // The DXGI adapter and its capacity were read successfully. Confirm the same
    // Intel hardware's CPU/GPU memory architecture when DXCore cannot expose it.
    D3DKMT_OPENADAPTERFROMLUID open{}; open.AdapterLuid = expected;
    if (D3DKMTOpenAdapterFromLuid(&open) < 0) return E_FAIL;
    struct KernelHandle {
        D3DKMT_HANDLE value;
        ~KernelHandle() { D3DKMT_CLOSEADAPTER close{}; close.hAdapter=value; D3DKMTCloseAdapter(&close); }
    } handle{open.hAdapter};
    D3DKMT_ADAPTERTYPE type{};
    D3DKMT_QUERYADAPTERINFO query{}; query.hAdapter=handle.value; query.Type=KMTQAITYPE_ADAPTERTYPE;
    query.pPrivateDriverData=&type; query.PrivateDriverDataSize=sizeof(type);
    if (D3DKMTQueryAdapterInfo(&query) < 0) return E_FAIL;
    if (detail) (*detail)["kernelAdapterType"]={{"renderSupported",type.RenderSupported != 0},
        {"softwareDevice",type.SoftwareDevice != 0},{"hybridDiscrete",type.HybridDiscrete != 0},
        {"indirectDisplay",type.IndirectDisplayDevice != 0},{"paravirtualized",type.Paravirtualized != 0}};
    if (!type.RenderSupported || type.SoftwareDevice || type.HybridDiscrete ||
        type.IndirectDisplayDevice || type.Paravirtualized) {
        // A successful kernel classification is positive exclusion evidence.
        // Other DXGI_ERROR_UNSUPPORTED returns (device creation or architecture)
        // remain read failures and must not be mistaken for this result.
        excludedByKernelType = true;
        return DXGI_ERROR_UNSUPPORTED;
    }
    Module module(L"d3d12.dll");
    if (!module.value) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    using CreateDevice = HRESULT (WINAPI*)(IUnknown*,D3D_FEATURE_LEVEL,REFIID,void**);
    const auto create = reinterpret_cast<CreateDevice>(GetProcAddress(module.value,"D3D12CreateDevice"));
    if (!create) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    ComPtr<ID3D12Device> device;
    auto hr = create(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device));
    if (FAILED(hr)) return hr;
    if (!device) return E_POINTER;
    const auto actual = device->GetAdapterLuid();
    if (detail) (*detail)["d3d12Luid"]=luidText(actual);
    if (actual.LowPart != expected.LowPart || actual.HighPart != expected.HighPart) return E_INVALIDARG;
    if (device->GetNodeCount() != 1) return E_INVALIDARG;
    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
    hr = device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1,&architecture,sizeof(architecture));
    if (FAILED(hr)) return hr;
    if (detail) (*detail)["d3d12Architecture"]={{"uma",architecture.UMA != FALSE},
        {"cacheCoherentUma",architecture.CacheCoherentUMA != FALSE},{"nodeIndex",architecture.NodeIndex}};
    // Cache coherence is required as additional positive evidence. Never assume
    // an Intel vendor/name alone proves an integrated memory architecture.
    if (architecture.NodeIndex != 0 || !architecture.UMA || !architecture.CacheCoherentUMA) return DXGI_ERROR_UNSUPPORTED;
    LARGE_INTEGER version{};
    hr = adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&version);
    if (FAILED(hr)) return hr;
    hr = device->GetDeviceRemovedReason();
    if (FAILED(hr)) return hr;
    driverVersion = static_cast<std::uint64_t>(version.QuadPart);
    return driverVersion ? S_OK : E_INVALIDARG;
}
inline Probe probeWindows(const std::string& deviceClass, Json* diagnostics = nullptr) {
    if (diagnostics) *diagnostics=Json{{"adapters",Json::array()}};
    const auto fail = [](const std::string& stage, const std::string& detail) {
        Probe p; p.reason = "[" + stage + "] " + detail; return p;
    };
    const auto failedHr = [&](const std::string& stage, HRESULT hr) {
        std::ostringstream code; code << std::hex << static_cast<std::uint32_t>(hr);
        return fail(stage,"读取失败，HRESULT=0x" + code.str());
    };
    Module dxgi(L"dxgi.dll"), dxcore(L"dxcore.dll");
    if (!dxgi.value || !dxcore.value) return fail("runtime.module",!dxgi.value ? "无法加载系统 DXGI" : "无法加载系统 DXCore");
    using Create = HRESULT (WINAPI*)(REFIID,void**);
    const auto createDxgi = reinterpret_cast<Create>(GetProcAddress(dxgi.value,"CreateDXGIFactory1"));
    const auto createCore = reinterpret_cast<Create>(GetProcAddress(dxcore.value,"DXCoreCreateAdapterFactory"));
    if (!createDxgi || !createCore) return fail("runtime.entry","系统图形读取入口不可用");
    ComPtr<IDXGIFactory1> factory; ComPtr<IDXCoreAdapterFactory> core;
    auto hr = createDxgi(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return failedHr("dxgi.factory",hr);
    hr = createCore(IID_PPV_ARGS(&core));
    if (FAILED(hr)) return failedHr("dxcore.factory",hr);
    ULONGLONG ramKb = 0;
    if (!GetPhysicallyInstalledSystemMemory(&ramKb) || ramKb > UINT64_MAX / 1024) return fail("system.ram","无法确认已安装内存总量");
    const auto boot = bootIdentity();
    if (boot.empty()) return fail("system.boot","无法读取启动身份");
    std::vector<Probe> candidates; bool complete = false;
    std::string rejected = "没有可确认的 Intel 或受支持 ROG 核显";
    for (UINT i = 0; i < 32; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(i,&adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) { complete = true; break; }
        if (FAILED(hr)) return failedHr("dxgi.enumeration",hr);
        DXGI_ADAPTER_DESC1 d{};
        hr = adapter->GetDesc1(&d);
        if (FAILED(hr)) return failedHr("dxgi.capacity",hr);
        Json* detail=nullptr;
        if (diagnostics) {
            (*diagnostics)["adapters"].push_back(Json{{"adapterName",utf8(d.Description)},{"vendorId",d.VendorId},
                {"deviceId",d.DeviceId},{"flags",d.Flags},{"luid",luidText(d.AdapterLuid)},{"capacityRead",true},{"dedicatedBytes",d.DedicatedVideoMemory},
                {"dedicatedSystemBytes",d.DedicatedSystemMemory},{"sharedSystemBytes",d.SharedSystemMemory}});
            detail=&(*diagnostics)["adapters"].back();
        }
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (d.VendorId != 0x8086 && !(d.VendorId == 0x1002 && rogClass(deviceClass))) continue;
        ComPtr<IDXCoreAdapter> ca;
        bool hardware = false, integrated = false;
        std::uint64_t liveVersion = 0;
        std::string classificationSource = "dxcore";
        hr = core->GetAdapterByLuid(d.AdapterLuid,IID_PPV_ARGS(&ca));
        if (detail) (*detail)["dxcoreLuidHr"]=static_cast<std::uint32_t>(hr);
        if (FAILED(hr)) {
            // E_INVALIDARG specifically means the adapter is unavailable through DXCore.
            // A removed/failed adapter never uses another route to turn that error into success.
            if (hr != E_INVALIDARG || d.VendorId != 0x8086) return failedHr("dxcore.luid",hr);
            bool excludedByKernelType = false;
            const auto umaHr = intelUmaIdentity(adapter.Get(),d.AdapterLuid,liveVersion,excludedByKernelType,detail);
            if (detail) (*detail)["intelUmaHr"]=static_cast<std::uint32_t>(umaHr);
            if (excludedByKernelType) {
                if (detail) { (*detail)["disposition"]="excluded"; (*detail)["exclusionReason"]="kernel-adapter-type"; }
                rejected = utf8(d.Description) + ": kernel type excludes a physical integrated rendering adapter";
                continue;
            }
            if (FAILED(umaHr)) return failedHr("intel.uma-identity",umaHr);
            hardware = true; integrated = true; classificationSource = "d3d12-cache-coherent-uma";
        } else {
            if (!ca) return failedHr("dxcore.luid",E_POINTER);
            hr = ca->GetProperty(DXCoreAdapterProperty::IsHardware,sizeof(hardware),&hardware);
            if (FAILED(hr)) return failedHr("dxcore.is-hardware",hr);
            if (detail) (*detail)["isHardware"]=hardware;
            // A confirmed nonhardware adapter is ineligible. Its optional integrated property
            // must not veto a later physical iGPU in the same complete enumeration.
            if (!hardware) { rejected = utf8(d.Description) + ": IsHardware=0"; continue; }
            hr = ca->GetProperty(DXCoreAdapterProperty::IsIntegrated,sizeof(integrated),&integrated);
            if (FAILED(hr)) return failedHr("dxcore.is-integrated",hr);
            if (detail) {(*detail)["isHardware"]=hardware;(*detail)["isIntegrated"]=integrated;}
            if (!hardware || !integrated) { rejected = utf8(d.Description) + ": IsHardware=" + std::to_string(hardware) + ", IsIntegrated=" + std::to_string(integrated); continue; }
        }
        if (detail) (*detail)["classificationSource"]=classificationSource;
        Probe p; p.hardware = hardware; p.integrated = integrated; p.deviceClass = deviceClass;
        p.bootId = boot; p.adapterName = utf8(d.Description); p.dedicatedBytes = d.DedicatedVideoMemory;
        p.capacityRead = true; // GetDesc1 succeeded: a reported zero is data, never a fallback.
        p.classificationSource = classificationSource;
        p.installedBytes = ramKb * 1024;
        if (p.installedBytes == 0 || p.dedicatedBytes > p.installedBytes || (d.VendorId != 0x8086 && p.dedicatedBytes == 0)) return fail("dxgi.capacity-range",p.adapterName + ": DedicatedVideoMemory=" + std::to_string(p.dedicatedBytes) + ", installedBytes=" + std::to_string(p.installedBytes));
        p.luid = luidText(d.AdapterLuid);
        std::string bindingReason; const auto binding = bindingFor(d.AdapterLuid,&bindingReason,detail);
        if (!binding) return fail("adapter.binding",p.adapterName + ": LUID=" + p.luid + " " + bindingReason);
        p.pnpId = utf8(binding->pnpId); p.registryKey = utf8(binding->registryKey); p.bindingSource = binding->source;
        if (ca) {
            hr = ca->GetProperty(DXCoreAdapterProperty::DriverVersion,sizeof(liveVersion),&liveVersion);
            if (FAILED(hr)) return failedHr("dxcore.driver-version",hr);
            if (liveVersion == 0) return failedHr("dxcore.driver-version",E_INVALIDARG);
        }
        p.driverVersion = utf8(binding->driverVersion) + ":" + std::to_string(liveVersion);
        p.identityConfirmed = true;
        if (d.VendorId == 0x8086) {
            p.provider = "intel-gmm"; p.options = intelOptions(p.installedBytes);
            if (!readIntelSetting(p)) { Probe failed; failed.reason = p.reason.empty() ? "[intel.config] 当前配置读取失败" : p.reason; return failed; }
        } else if (!readRogSetting(p,detail)) { Probe failed; failed.reason = p.reason; return failed; }
        if (!admitted(p)) return fail("adapter.admission",p.adapterName + ": 读取数据未通过完整性门槛");
        candidates.push_back(std::move(p));
    }
    if (!complete) return fail("dxgi.enumeration-limit","适配器枚举未完整结束");
    if (candidates.empty()) return fail("adapter.none",rejected);
    const auto selected = uniquePhysicalProbe(candidates);
    return selected ? *selected : fail("adapter.ambiguous","多个适配器或别名的身份、容量、配置、档位不一致");
}
inline Json optionJson(const Option& o) {
    return Json{{"id",o.id},{"raw",o.raw},{"capacityMiB",o.capacityMiB ? Json(*o.capacityMiB) : Json(nullptr)},
                {"removeOverride",o.removeOverride}};
}
inline Option parseOption(const Json& j) {
    Option o; o.id = j.at("id").get<std::string>(); o.raw = j.at("raw").get<std::uint32_t>();
    if (!j.at("capacityMiB").is_null()) o.capacityMiB = j.at("capacityMiB").get<std::uint32_t>();
    o.removeOverride = j.at("removeOverride").get<bool>(); return o;
}
class WindowsOps final : public Ops {
    std::wstring root_; std::string deviceClass_; Json* diagnostics_ = nullptr;
    Json* dispatch_ = nullptr; std::string writeBinding_;
public:
    WindowsOps(std::wstring root,std::string deviceClass,Json* diagnostics = nullptr,Json* dispatch = nullptr)
        : root_(std::move(root)),deviceClass_(std::move(deviceClass)),diagnostics_(diagnostics),dispatch_(dispatch) {}
    bool matchesWriteBinding(const Probe& p) const { return !writeBinding_.empty() && writeBinding_ == bindingFence(p); }
    Probe probe() override { return probeWindows(deviceClass_,diagnostics_); }
    JournalRead journal() override {
        try {
            const auto path = std::filesystem::path(root_) / L"gpu-dedicated-memory.json";
            if (!std::filesystem::exists(path)) return {};
            if (std::filesystem::file_size(path) > 65536) return {false,std::nullopt};
            std::ifstream input(path,std::ios::binary); if (!input) return {false,std::nullopt};
            const auto json = Json::parse(input);
            if (json.at("schema") != 1) return {false,std::nullopt};
            Journal j; j.pnpId = json.at("pnpId").get<std::string>(); j.driverVersion = json.at("driverVersion").get<std::string>();
            j.provider = json.at("provider").get<std::string>(); j.bootId = json.at("bootId").get<std::string>();
            j.stage = json.at("stage").get<std::string>(); j.beforeBytes = json.at("beforeBytes").get<std::uint64_t>();
            if (!json.at("originalRaw").is_null()) j.originalRaw = json.at("originalRaw").get<std::uint32_t>();
            j.target = parseOption(json.at("target"));
            if (j.pnpId.empty() || j.bootId.empty() || (j.stage != "saved" && j.stage != "prepared")) return {false,std::nullopt};
            return {true,j};
        } catch (...) { return {false,std::nullopt}; }
    }
    bool save(const Journal& j) override {
        try {
            const auto path = std::filesystem::path(root_) / L"gpu-dedicated-memory.json";
            const auto temp = path.wstring() + L".tmp";
            const Json json{{"schema",1},{"pnpId",j.pnpId},{"driverVersion",j.driverVersion},{"provider",j.provider},
                {"bootId",j.bootId},{"stage",j.stage},{"originalRaw",j.originalRaw ? Json(*j.originalRaw) : Json(nullptr)},
                {"beforeBytes",j.beforeBytes},{"target",optionJson(j.target)}};
            const auto text = json.dump(2);
            Handle file; file.value = CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if (file.value == INVALID_HANDLE_VALUE) return false;
            DWORD written = 0;
            if (!WriteFile(file.value,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) || written != text.size() ||
                !FlushFileBuffers(file.value)) return false;
            CloseHandle(file.value); file.value = INVALID_HANDLE_VALUE;
            return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH) != 0;
        } catch (...) { return false; }
    }
    bool write(const Probe& expected, const Option& option) override {
        // Re-resolve immediately before any write; no renderer-supplied registry path or device index.
        const auto current = probe();
        if (!admitted(current) || fence(current) != fence(expected)) return false;
        if (current.provider == "intel-gmm") { writeBinding_ = bindingFence(current); return writeIntelSetting(current,option,dispatch_); }
        writeBinding_ = bindingFence(current); return writeRogSetting(current,option,dispatch_);
    }
};
inline Json resultJson(const Result& r) {
    const auto& p = r.snapshot;
    const bool ready = admitted(p);
    Json out{{"schema",1},{"readerRevision",readerRevision},{"ok",r.ok},{"available",ready},{"reason",r.reason}};
    // An invalid journal is a failed read and must not expose the controls.
    if (!ready || (!r.ok && r.reason == "显存设置记录无法读取")) { out["available"] = false; return out; }
    Json choices = Json::array(); std::string selected;
    for (const auto& o : p.options) { choices.push_back(optionJson(o)); if (configuredAs(p,o)) selected = o.id; }
    out["provider"] = p.provider; out["adapterName"] = p.adapterName; out["driverVersion"] = p.driverVersion;
    out["pnpId"] = p.pnpId; out["luid"] = p.luid; out["dedicatedBytes"] = p.dedicatedBytes;
    out["capacityRead"] = p.capacityRead; out["bindingSource"] = p.bindingSource;
    out["classificationSource"] = p.classificationSource;
    out["selected"] = selected; out["options"] = choices; out["fence"] = fence(p);
    out["verification"] = verification(p,r.journal);
    out["bootId"] = p.bootId;
    if (r.journal && r.journal->pnpId == p.pnpId && r.journal->provider == p.provider) {
        out["pendingOption"] = r.journal->target.id; out["savedBootId"] = r.journal->bootId;
        out["bootComparison"] = bootComparison(p,*r.journal);
    }
    return out;
}
// The public setting route admits known OEM protocols only. A registry value and
// a readable Intel capacity establish diagnostics, not an accepted capacity setter.
inline bool knownSettingRoute(const Probe& p) {
    return admitted(p) && rogClass(p.deviceClass) && (p.provider == "rog-apu" || p.provider == "rog-vram");
}
inline Json knownSettingResult(Json out, const Probe& p) {
    const bool known = knownSettingRoute(p);
    out["settingRoute"] = known ? "known-model" : "read-only";
    if (!known && out.value("available",false)) {
        out["available"] = false;
        out["reason"] = "[setting.route.unconfirmed] 当前机型没有已确认的显存设置通道";
    }
    return out;
}
inline Json writeResultJson(const Result& result, const WindowsOps& ops, Json dispatch) {
    auto out = resultJson(result);
    if (dispatch.is_object()) {
        // This is the independent post-write probe, never the setter's reply.
        dispatch["configurationReadback"] = admitted(result.snapshot) && ops.matchesWriteBinding(result.snapshot) &&
            result.journal && offered(result.snapshot,result.journal->target) && configuredAs(result.snapshot,result.journal->target);
        dispatch["capacityRead"] = result.snapshot.capacityRead;
        dispatch["dedicatedBytes"] = result.snapshot.capacityRead ? Json(result.snapshot.dedicatedBytes) : Json(nullptr);
        if (!result.ok && dispatch.contains("reason")) out["reason"] = result.reason + "；" + dispatch["reason"].get<std::string>();
        out["writeDiagnostics"] = std::move(dispatch);
    }
    return out;
}
inline Json runtimeRead(const std::wstring& root,const std::string& deviceClass) {
    std::lock_guard<std::mutex> guard(runtimeMutex());
    Json diagnostics;
    WindowsOps ops(root,deviceClass,&diagnostics); const auto readResult = read(ops);
    auto result = knownSettingResult(resultJson(readResult),readResult.snapshot);
    diagnostics["settingRoute"] = Json{{"policy","known-models-v1"},{"allowed",knownSettingRoute(readResult.snapshot)},
        {"deviceClass",deviceClass},{"provider",readResult.snapshot.provider}};
    result["readDiagnostics"] = std::move(diagnostics); return result;
}
inline Json runtimeApply(const std::wstring& root,const std::string& deviceClass,const Json& args) {
    std::lock_guard<std::mutex> guard(runtimeMutex());
    if (!args.is_object() || !args.contains("fence") || !args["fence"].is_string() || !args.contains("option") ||
        !args["option"].is_string() || !args.contains("confirmed") || !args["confirmed"].is_boolean())
        return Json{{"schema",1},{"readerRevision",readerRevision},{"ok",false},{"available",false},{"reason","显存设置参数无效"}};
    Json dispatch;
    WindowsOps ops(root,deviceClass,nullptr,&dispatch);
    auto before = read(ops);
    if (!before.ok || !knownSettingRoute(before.snapshot)) {
        auto out = knownSettingResult(resultJson(before),before.snapshot); out["ok"] = false;
        out["writeDiagnostics"] = Json{{"stage","setting.route.validate"},{"valueWriteAttempted",false},
            {"acknowledged",false},{"driverAcceptance","not_reported"}};
        return out;
    }
    const auto result = applyFromRead(ops,std::move(before),args["fence"].get<std::string>(),args["option"].get<std::string>(),args["confirmed"].get<bool>());
    return knownSettingResult(writeResultJson(result,ops,std::move(dispatch)),result.snapshot);
}
} // namespace ymcc::dedicatedmemory
