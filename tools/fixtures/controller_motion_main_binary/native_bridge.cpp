#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include "gamepad_motion_abi.h"
#include "native_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <type_traits>
#include <vector>

static_assert(sizeof(void*) == 8 && sizeof(int) == 4 && sizeof(float) == 4, "Fixed x64 ABI only");
namespace {
using namespace binary_test;
using Fields = std::vector<std::pair<std::string, std::string>>;
std::string quote(const std::string& s) {
    std::ostringstream out; out << '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 32) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
            else out << static_cast<char>(c);
        }
    }
    out << '"'; return out.str();
}
std::string number(double x) {
    if (!std::isfinite(x)) return quote(std::isnan(x) ? "NaN" : x > 0 ? "Infinity" : "-Infinity");
    std::ostringstream out; out << std::setprecision(12) << x; return out.str();
}
std::string integer(std::uint64_t x) { return std::to_string(x); }
std::string object(const Fields& fields) {
    std::string out = "{"; bool first = true;
    for (const auto& item : fields) { if (!first) out += ','; first = false; out += quote(item.first) + ':' + item.second; }
    return out + '}';
}
void emit(const std::string& type, Fields fields = {}) {
    fields.insert(fields.begin(), {"type", quote(type)});
    std::cout << object(fields) << '\n';
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("WideCharToMultiByte failed");
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring modulePath(HMODULE module) {
    std::vector<wchar_t> buffer(32768);
    const DWORD n = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!n || n >= buffer.size()) throw std::runtime_error("GetModuleFileNameW failed/truncated");
    return std::wstring(buffer.data(), n);
}
std::wstring normal(const std::wstring& path) { return std::filesystem::path(path).lexically_normal().wstring(); }
bool samePath(const std::wstring& a, const std::wstring& b) { return _wcsicmp(normal(a).c_str(), normal(b).c_str()) == 0; }
struct Module { std::wstring path; std::uintptr_t base; };
std::vector<Module> modules() {
    std::array<HMODULE, 512> handles{}; DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), handles.data(), static_cast<DWORD>(sizeof(handles)), &needed) || needed > sizeof(handles))
        throw std::runtime_error("K32EnumProcessModules failed/truncated");
    std::vector<Module> result;
    for (std::size_t i = 0; i < needed / sizeof(HMODULE); ++i)
        result.push_back({modulePath(handles[i]), reinterpret_cast<std::uintptr_t>(handles[i])});
    std::sort(result.begin(), result.end(), [](const Module& a, const Module& b) { return a.path < b.path; });
    return result;
}
void emitModules(const std::string& stage, const std::vector<Module>& list) {
    std::string values = "[";
    for (std::size_t i = 0; i < list.size(); ++i) { if (i) values += ','; values += object({{"path",quote(utf8(list[i].path))},{"base",quote(integer(list[i].base))}}); }
    emit("modules", {{"stage",quote(stage)},{"modules",values + ']'}});
}
struct PinnedFile {
    HANDLE file = INVALID_HANDLE_VALUE;
    explicit PinnedFile(const std::wstring& path) {
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot lock private DLL for read: " + std::to_string(GetLastError()));
    }
    ~PinnedFile() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
    PinnedFile(const PinnedFile&) = delete;
    PinnedFile& operator=(const PinnedFile&) = delete;
};
std::string hashFile(HANDLE file) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || static_cast<std::uint64_t>(size.QuadPart) != kBytes) throw std::runtime_error("Pinned DLL size mismatch");
    std::vector<UCHAR> state; // Outlives BCrypt hash destruction.
    struct HashResources {
        BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
        ~HashResources() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    } resources;
    auto nt = [](NTSTATUS status) { if (status < 0) throw std::runtime_error("BCrypt SHA256 operation failed"); };
    nt(BCryptOpenAlgorithmProvider(&resources.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    DWORD objectLength = 0, count = 0;
    nt(BCryptGetProperty(resources.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &count, 0));
    state.resize(objectLength);
    nt(BCryptCreateHash(resources.algorithm, &resources.hash, state.data(), objectLength, nullptr, 0, 0));
    std::array<UCHAR, 4096> bytes{}; DWORD received = 0;
    do {
        if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &received, nullptr)) throw std::runtime_error("ReadFile(private DLL) failed");
        if (received) nt(BCryptHashData(resources.hash, bytes.data(), received, 0));
    } while (received);
    std::array<UCHAR, 32> digest{};
    nt(BCryptFinishHash(resources.hash, digest.data(), static_cast<ULONG>(digest.size()), 0));
    std::ostringstream result; result << std::hex << std::setfill('0');
    for (UCHAR byte : digest) result << std::setw(2) << static_cast<unsigned>(byte);
    return result.str();
}
template<class Fn> Fn bind(HMODULE module, const char* name, std::uintptr_t expectedRva) {
    const FARPROC address = GetProcAddress(module, name);
    if (!address) throw std::runtime_error(std::string("ABI gap: missing export ") + name);
    Fn function{}; static_assert(sizeof(function) == sizeof(address)); std::memcpy(&function, &address, sizeof(function));
    const std::uintptr_t rva = reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module);
    emit("binding", {{"name",quote(name)},{"rva",integer(rva)},{"expectedRva",integer(expectedRva)}});
    if (rva != expectedRva) throw std::runtime_error(std::string("ABI gap: unexpected RVA ") + name);
    return function;
}
Api bindApi(HMODULE module) {
    Api api;
    api.create = bind<Create>(module, "CreateGamepadMotion", 0x35D0);
    api.destroy = bind<Delete>(module, "DeleteGamepadMotion", 0x3600);
    api.setMode = bind<SetMode>(module, "SetCalibrationMode", 0x3B70);
    api.getMode = bind<GetMode>(module, "GetCalibrationMode", 0x3660);
    api.setOffset = bind<SetOffset>(module, "SetCalibrationOffset", 0x3B80);
    api.getOffset = bind<Get3>(module, "GetCalibrationOffset", 0x3670);
    api.process = bind<Process>(module, "ProcessMotion", 0x3A80);
    api.getConfidence = bind<GetConfidence>(module, "GetAutoCalibrationConfidence", 0x3610);
    api.getCalibrated = bind<Get3>(module, "GetCalibratedGyro", 0x3640);
    api.getGravity = bind<Get3>(module, "GetGravity", 0x36D0);
    api.getPlayer = bind<GetSpace>(module, "GetPlayerSpaceGyro", 0x3720);
    api.getWorld = bind<GetSpace>(module, "GetWorldSpaceGyro", 0x3820);
    api.reset = bind<Reset>(module, "ResetContinuousCalibration", 0x3A90);
    return api;
}


HMODULE nativeModule=nullptr;std::unique_ptr<PinnedFile> nativeFile;Api nativeApi;std::string lastError;
unsigned created=0,deleted=0,processed=0,modes=0,offsets=0,resets=0,confidenceReads=0,offsetReads=0;
std::wstring wide(const char* value){const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value,-1,nullptr,0);if(n<=0)throw std::runtime_error("Invalid UTF8 path");std::vector<wchar_t> b(static_cast<std::size_t>(n));MultiByteToWideChar(CP_UTF8,0,value,-1,b.data(),n);return b.data();}
}
extern "C" int bridge_open(const char* dllText,const char* rootText){
 try{SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);const std::filesystem::path dll(wide(dllText)),root(wide(rootText));
  if(!dll.is_absolute()||!root.is_absolute()||!samePath(dll.wstring(),(root/L"binary"/L"GamepadMotion.dll").wstring()))throw std::runtime_error("Only private absolute DLL allowed");
  const auto relative=std::filesystem::path(modulePath(nullptr)).lexically_relative(root);if(relative.empty()||relative.is_absolute()||*relative.begin()==L"..")throw std::runtime_error("Only evidence helper allowed");
  emitModules("before-target-load",modules());nativeFile=std::make_unique<PinnedFile>(dll.wstring());const auto hash=hashFile(nativeFile->file);if(hash!=kSha256)throw std::runtime_error("Pinned SHA mismatch");emit("hash-verified",{{"sha256",quote(hash)},{"lockedAgainstWriteAndDelete","true"}});
  nativeModule=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!nativeModule)throw std::runtime_error("LoadLibraryExW failed: "+std::to_string(GetLastError()));const auto actual=modulePath(nativeModule);emit("loaded",{{"path",quote(utf8(actual))},{"flags",quote("DLL_LOAD_DIR|SYSTEM32")}});if(!samePath(actual,dll.wstring()))throw std::runtime_error("Wrong module");emitModules("after-target-load",modules());nativeApi=bindApi(nativeModule);return 1;
 }catch(const std::exception& e){lastError=e.what();emit("bridge-fatal",{{"error",quote(lastError)}});if(nativeModule){FreeLibrary(nativeModule);nativeModule=nullptr;}nativeFile.reset();return 0;}}
extern "C" const char* bridge_error(){return lastError.c_str();}
extern "C" int bridge_close(){
 bool released=false,absent=false;if(nativeModule&&created==deleted){const auto target=modulePath(nativeModule);released=FreeLibrary(nativeModule)!=FALSE;nativeModule=nullptr;const auto after=modules();emitModules("after-target-unload",after);absent=std::none_of(after.begin(),after.end(),[&](const Module& m){return samePath(m.path,target);});}
 emit("bridge-summary",{{"created",integer(created)},{"deleted",integer(deleted)},{"processCalls",integer(processed)},{"modeCalls",integer(modes)},{"offsetWrites",integer(offsets)},{"resets",integer(resets)},{"confidenceReads",integer(confidenceReads)},{"offsetReads",integer(offsetReads)},{"moduleUnloaded",released&&absent?"true":"false"}});if(released&&absent)nativeFile.reset();return released&&absent&&created==deleted?1:0;
}
extern "C" void* bridge_create(){void* p=nativeApi.create();if(p)++created;return p;}
extern "C" void bridge_delete(void* p){nativeApi.destroy(p);++deleted;}
extern "C" void bridge_mode(void* p,int m){nativeApi.setMode(p,m);++modes;}
extern "C" int bridge_mode_read(void* p){return nativeApi.getMode(p);}
extern "C" void bridge_offset(void* p,float x,float y,float z,int w){nativeApi.setOffset(p,x,y,z,w);++offsets;}
extern "C" void bridge_process(void* p,float gx,float gy,float gz,float ax,float ay,float az,float dt){nativeApi.process(p,gx,gy,gz,ax,ay,az,dt);++processed;}
extern "C" void bridge_cal(void* p,float* x,float* y,float* z){nativeApi.getCalibrated(p,x,y,z);}
extern "C" void bridge_gravity(void* p,float* x,float* y,float* z){nativeApi.getGravity(p,x,y,z);}
extern "C" void bridge_player(void* p,float* x,float* y,float f){nativeApi.getPlayer(p,x,y,f);}
extern "C" void bridge_world(void* p,float* x,float* y,float f){nativeApi.getWorld(p,x,y,f);}
extern "C" void bridge_reset(void* p){nativeApi.reset(p);++resets;}
extern "C" unsigned bridge_reset_calls(){return resets;}
extern "C" unsigned bridge_process_calls(){return processed;}
extern "C" float bridge_confidence(void* p){++confidenceReads;return nativeApi.getConfidence(p);}
extern "C" void bridge_get_offset(void* p,float* x,float* y,float* z){nativeApi.getOffset(p,x,y,z);++offsetReads;}
