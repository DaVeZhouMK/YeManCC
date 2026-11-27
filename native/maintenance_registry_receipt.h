#pragma once
#include "update_install_recovery.h"
#include "early_start_service.h"
#include <cstring>
#include <cwchar>
#include <mutex>
#include <vector>

namespace ymcc::maintenance {
using Json = nlohmann::json;
using namespace ymcc::updateconfig;

inline std::wstring receiptsDirectory() {
    PWSTR known = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &known))) return {};
    std::wstring state(known); CoTaskMemFree(known);
    state += L"\\YeManCC-Maintenance";
    if (!ordinaryParents(std::filesystem::path(state)) || !updaterecovery::secureDirectory(state, false)) return {};
    auto receipts = state + L"\\receipts-v1";
    return updaterecovery::secureDirectory(receipts, true) ? receipts : std::wstring{};
}
inline std::string registryTarget(const std::string& hive, const std::string& path, const std::string& name) {
    if (hive == "HKCU") {
        if (path == "Software\\Microsoft\\Windows\\CurrentVersion\\GamingConfiguration") {
            if (name == "GamingHomeApp") return "gaming-home";
            if (name == "StartupToGamingHome") return "gaming-start";
        }
        if (path == "Software\\Microsoft\\GameBar" && name == "UseNexusForGameBarEnabled") return "gamebar-controller";
    } else if (hive == "HKLM") {
        if (path == "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OEM" && name == "DeviceForm") return "gaming-device";
        if (path == "SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers" && name == "HwSchMode") return "gpu-scheduling";
    }
    return {};
}
inline std::wstring tokenSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    struct Close { HANDLE h; ~Close() { CloseHandle(h); } } close{token};
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    if (!bytes || bytes > 8192) return {};
    std::vector<unsigned char> data(bytes);
    if (!GetTokenInformation(token, TokenUser, data.data(), bytes, &bytes)) return {};
    auto sid = reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;
    if (IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinLocalServiceSid) || IsWellKnownSid(sid, WinNetworkServiceSid)) return {};
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(sid, &text)) return {};
    std::wstring result(text); LocalFree(text); return result;
}
template<class WideToUtf8>
Json registryFact(HKEY key, const std::wstring& name, WideToUtf8 utf8) {
    alignas(wchar_t) std::array<BYTE,8194> buffer{};
    DWORD type = 0, bytes = static_cast<DWORD>(buffer.size());
    const auto status = RegQueryValueExW(key, name.c_str(), nullptr, &type, buffer.data(), &bytes);
    if (status == ERROR_FILE_NOT_FOUND) return {{"type","none"}};
    if (status != ERROR_SUCCESS) return nullptr;
    if (type == REG_DWORD && bytes == sizeof(DWORD)) {
        DWORD data = 0; memcpy(&data, buffer.data(), sizeof(data));
        return {{"type","dword"},{"number",data}};
    }
    if (type == REG_SZ && bytes >= sizeof(wchar_t) && bytes % sizeof(wchar_t) == 0) {
        const auto val = reinterpret_cast<const wchar_t*>(buffer.data());
        const auto length = bytes / sizeof(wchar_t);
        if (val[length - 1] != L'\0' || std::wmemchr(val,L'\0',length-1) != nullptr) return nullptr;
        return {{"type","string"},{"text",utf8(std::wstring(val,length-1))}};
    }
    return nullptr;
}
inline bool protectedReceipt(const std::wstring& path,bool usersReadExecute=false) {
    HANDLE file = CreateFileW(path.c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    const auto ok = updaterecovery::secureHandle(file,usersReadExecute) && GetFileInformationByHandle(file,&info) && info.nNumberOfLinks == 1;
    CloseHandle(file);
    return ok;
}
inline std::wstring installedMaintenanceImage(const wchar_t* name) {
    PWSTR known = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles,0,nullptr,&known))) return {};
    std::wstring bin(known); CoTaskMemFree(known);
    bin += L"\\YeManCC Maintenance";
    const auto file=bin+L"\\"+name;
    const auto attr=GetFileAttributesW(file.c_str());
    if (attr==INVALID_FILE_ATTRIBUTES || (attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) return {};
    return ordinaryParents(std::filesystem::path(file)) && updaterecovery::secureDirectory(bin,false,true) && protectedReceipt(file,true) ? file : std::wstring{};
}
inline std::wstring installedUninstaller() { return installedMaintenanceImage(L"YeManCC-Uninstall.exe"); }
inline bool saveReceipt(const std::wstring& path, const Image& old, const Json& receipt) {
    const auto staged = path + L".tmp-" + suffix();
    const auto text = receipt.dump();
    if (text.size() > 65536) return false;
    if (!updaterecovery::writeProtectedFile(staged,text)) { DeleteFileW(staged.c_str()); return false; }
    const bool ok = sameImage(old,read(path,65536)) &&
        (old.kind == Kind::missing || protectedReceipt(path)) &&
        MoveFileExW(staged.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(staged.c_str());
    return ok;
}

inline std::mutex writers;
template<class Read, class Write>
auto writeTracked(const std::string& id,const std::wstring& sid,const Json& after,Read observe,Write write) {
    const std::lock_guard<std::mutex> lock(writers);
    std::wstring file;
    Json receipt;
    bool prepared = false;
    try {
        const auto directory = receiptsDirectory();
        if (!directory.empty() && !sid.empty() && !after.is_null()) {
            const auto before = observe();
            const std::string scope(sid.begin(),sid.end());
            file = directory + L"\\" + std::wstring(id.begin(),id.end()) + L"." + sid + L".json";
            const auto previous = read(file,65536);
            if (!before.is_null() && (before.value("type", "") == "none" || after.value("type", "") == "none" || before["type"] == after["type"]) && before != after) {
                if (previous.kind == Kind::missing) {
                    receipt = {{"schema",1},{"id",id},{"sid",scope},{"before",before},{"after",before}};
                } else if (previous.kind == Kind::ready && protectedReceipt(file) && previous.value.value("schema",0) == 1 &&
                           previous.value.value("id",std::string{}) == id && previous.value.value("sid",std::string{}) == scope &&
                           (previous.value.at("after") == before || previous.value.value("pending",Json(nullptr)) == before || previous.value.at("before") == before)) {
                    receipt = previous.value;
                    receipt["after"] = before;
                }
                if (receipt.is_object()) { receipt["pending"] = after; prepared = saveReceipt(file,previous,receipt); }
            }
        }
    } catch (...) { prepared = false; }
    const auto status = write();
    if (prepared) try {
        const auto readBack = observe();
        auto current = read(file,65536);
        if (current.kind == Kind::ready && current.value == receipt && readBack == receipt["pending"]) {
            receipt["after"] = readBack;
            receipt.erase("pending");
            (void)saveReceipt(file,current,receipt);
        }
    } catch (...) { /* A durable pending record is enough to reconcile after interruption. */ }
    return status;
}
template<class N, class W, class Write>
auto writeRegistry(const std::string& hive,const std::string& path,const std::string& name,
                   const Json& desired,HKEY key,N utf8,W wide,Write write) {
    const auto id = registryTarget(hive,path,name);
    if (id.empty()) return write();
    const auto sid = hive == "HKLM" ? L"machine" : tokenSid();
    const auto after = desired.is_string() ? Json{{"type","string"},{"text",desired}} :
        desired.is_number_integer() ? Json{{"type","dword"},{"number",static_cast<DWORD>(desired.get<int>())}} : Json(nullptr);
    return writeTracked(id,sid,after,[&]{return registryFact(key,wide(name),utf8);},write);
}
inline Json bytesFact(const std::string& bytes) {
    std::string hex;
    hex.reserve(bytes.size()*2);
    for (const unsigned char b : bytes) { hex += "0123456789abcdef"[b>>4]; hex += "0123456789abcdef"[b&15]; }
    return {{"type","bytes"},{"text",hex}};
}
inline Json observeEarlyStart(const std::wstring& path) {
    if (path.empty() || !ordinaryParents(std::filesystem::path(path))) return nullptr;
    HANDLE f = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Json{{"type","none"}} : Json(nullptr);
    }
    struct Close { HANDLE h; ~Close() { CloseHandle(h); } } close{f};
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(f,&info) || info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT) ||
        info.nNumberOfLinks != 1 || info.nFileSizeHigh || info.nFileSizeLow > 8192) return nullptr;
    std::string bytes(info.nFileSizeLow,'\0');
    DWORD count = 0;
    if (!ReadFile(f,bytes.data(),info.nFileSizeLow,&count,nullptr) || count != info.nFileSizeLow) return nullptr;
    return bytesFact(bytes);
}
template<class Write>
auto writeEarlyStart(const std::wstring& file,const std::string& command,bool enabled,Write write) {
    const auto expected = enabled ? bytesFact(command + "\r\n") : Json{{"type","none"}};
    return writeTracked("steam-earlystart",tokenSid(),expected,[&]{return observeEarlyStart(file);},write);
}

template<class N> Json repairEarlyStartService(N utf8) {
    struct Ports : earlystart::WindowsPorts {
        N encode;
        explicit Ports(N encoding):encode(encoding){}
        Json fact(const earlystart::State& state,DWORD startType) {
            if(!state.known || !state.installed || state.binaryPath.empty() || startType < SERVICE_AUTO_START || startType > SERVICE_DISABLED)
                return nullptr;
            return {{"type","string"},{"text",std::to_string(startType)+"\n"+std::to_string(state.serviceType)+"\n"+encode(state.binaryPath)}};
        }
        bool enableAutomatic() {
            const auto previous=this->read();
            return writeTracked("earlystart-service",L"machine",fact(previous,SERVICE_AUTO_START),[this,&previous]{
                const auto current=this->read();
                if(current.binaryPath!=previous.binaryPath || current.serviceType!=previous.serviceType) return Json(nullptr);
                return fact(current,current.startType);
            },[this,&previous]{
                const auto current=this->read();
                if(!current.known || !current.installed || current.binaryPath!=previous.binaryPath || current.serviceType!=previous.serviceType || current.startType!=previous.startType)
                    return false;
                return earlystart::WindowsPorts::enableAutomatic();
            });
        }
    } ports(utf8);
    return earlystart::repairWith(ports);
}
}
