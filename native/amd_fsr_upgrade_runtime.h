#pragma once
#include "amd_fsr_upgrade.h"
#include "../deps/adlx-sdk/ADLX.h"
#include "../deps/adlx-sdk/I3DSettings3.h"
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"uuid.lib")
#pragma comment(lib,"version.lib")
#pragma comment(lib,"advapi32.lib")
namespace ymcc::amdfsr {
inline std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if(n<=0) return {}; std::string out(n,'\0');
    return WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr)?out:std::string{};
}
inline std::string registryDriverVersion(const char* path) {
    if(!path) return {}; std::string key=path;
    for(const auto prefix:{std::string("\\Registry\\Machine\\"),std::string("HKEY_LOCAL_MACHINE\\")})
        if(lower(key).rfind(lower(prefix),0)==0) key.erase(0,prefix.size());
    // Path originates in AMD's IADLXGPU, never in IPC arguments.
    if(lower(key).rfind("system\\currentcontrolset\\control\\",0)!=0) return {};
    char text[512]{};DWORD size=sizeof(text);
    return RegGetValueA(HKEY_LOCAL_MACHINE,key.c_str(),"DriverVersion",RRF_RT_REG_SZ,nullptr,text,&size)==ERROR_SUCCESS?std::string(text):std::string{};
}
// Metadata-only present display inventory. Do not create a DXGI/D3D device or
// load a graphics UMD just to discover PCI vendor/driver version.
inline std::vector<Adapter> presentAmd(const std::string& cpu) {
    std::vector<Adapter> result;
    HDEVINFO info=SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY,nullptr,nullptr,DIGCF_PRESENT);
    if(info==INVALID_HANDLE_VALUE) return result;
    struct Destroy {HDEVINFO value;~Destroy(){SetupDiDestroyDeviceInfoList(value);}} owner{info};
    SP_DEVINFO_DATA data{sizeof(data)};
    for(DWORD i=0;i<64 && SetupDiEnumDeviceInfo(info,i,&data);++i) {
        std::array<wchar_t,4096> hardware{},name{},instance{};DWORD type=0;
        if(!SetupDiGetDeviceRegistryPropertyW(info,&data,SPDRP_HARDWAREID,&type,reinterpret_cast<BYTE*>(hardware.data()),
            static_cast<DWORD>(sizeof(hardware)),nullptr) || type!=REG_MULTI_SZ) continue;
        if(lower(utf8(hardware.data())).rfind("pci\\ven_1002&",0)!=0) continue;
        if(!SetupDiGetDeviceInstanceIdW(info,&data,instance.data(),static_cast<DWORD>(instance.size()),nullptr)) continue;
        if(!SetupDiGetDeviceRegistryPropertyW(info,&data,SPDRP_FRIENDLYNAME,&type,reinterpret_cast<BYTE*>(name.data()),static_cast<DWORD>(sizeof(name)),nullptr))
            SetupDiGetDeviceRegistryPropertyW(info,&data,SPDRP_DEVICEDESC,&type,reinterpret_cast<BYTE*>(name.data()),static_cast<DWORD>(sizeof(name)),nullptr);
        Adapter a;a.amd=true;a.id=lower(utf8(instance.data()));a.name=utf8(name.data());a.candidate=integratedCandidate(a.name,cpu);a.reason="driver_missing";
        HKEY driver=SetupDiOpenDevRegKey(info,&data,DICS_FLAG_GLOBAL,0,DIREG_DRV,KEY_READ);
        if(driver!=INVALID_HANDLE_VALUE) {
            struct Close {HKEY value;~Close(){RegCloseKey(value);}} key{driver};
            std::array<wchar_t,512> version{};DWORD bytes=sizeof(version);
            if(RegQueryValueExW(driver,L"DriverVersion",nullptr,&type,reinterpret_cast<BYTE*>(version.data()),&bytes)==ERROR_SUCCESS && type==REG_SZ)
                a.driverVersion=utf8(version.data());
        }
        result.push_back(std::move(a));
    }
    return result;
}
inline bool knownOldFsrLibrary() {
    wchar_t system[MAX_PATH]{};const auto length=GetSystemDirectoryW(system,MAX_PATH);
    if(!length || length>=MAX_PATH) return false;
    const auto path=std::filesystem::path(system)/L"amdadlx64.dll";DWORD ignored=0;
    const auto size=GetFileVersionInfoSizeW(path.c_str(),&ignored);
    if(!size || size>1024*1024) return false;
    std::vector<BYTE> bytes(size);
    if(!GetFileVersionInfoW(path.c_str(),0,size,bytes.data())) return false;
    VS_FIXEDFILEINFO* info=nullptr;UINT returned=0;
    if(!VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&info),&returned) ||
        !info || returned<sizeof(*info) || info->dwSignature!=0xfeef04bd) return false;
    return !fsrApiVersion(HIWORD(info->dwFileVersionMS),LOWORD(info->dwFileVersionMS));
}
// All interface references are destroyed BEFORE ADLXTerminate/FreeLibrary.
// No AMD runtime DLL, service, callback subscription, or polling is kept resident.
class Session {
    HMODULE module_=nullptr;ADLXTerminate_Fn terminate_=nullptr;bool ownsInit_=false,attempted_=false;
public:
    adlx::IADLXSystem* system=nullptr;
    adlx::IADLX3DSettingsServices3Ptr services;
    std::string version;
    Session()=default;Session(const Session&)=delete;
    bool open() {
        if(attempted_) return system && services;
        attempted_=true;
        // Version-resource reads do not execute DLL entrypoints. Known ADLX
        // 1.4 and older cannot expose FSR, so avoid their threads/UMD startup.
        if(knownOldFsrLibrary()) return false;
        module_=LoadLibraryExW(L"amdadlx64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module_) return false;
        const auto full=reinterpret_cast<ADLXQueryFullVersion_Fn>(GetProcAddress(module_,ADLX_QUERY_FULL_VERSION_FUNCTION_NAME));
        const auto init=reinterpret_cast<ADLXInitialize_Fn>(GetProcAddress(module_,ADLX_INIT_FUNCTION_NAME));
        terminate_=reinterpret_cast<ADLXTerminate_Fn>(GetProcAddress(module_,ADLX_TERMINATE_FUNCTION_NAME));
        const auto query=reinterpret_cast<ADLXQueryVersion_Fn>(GetProcAddress(module_,ADLX_QUERY_VERSION_FUNCTION_NAME));
        const char* text=nullptr;adlx_uint64 packed=0;
        if(!full || ADLX_FAILED(full(&packed)) || !fsrApiVersion(static_cast<unsigned>(packed>>48),static_cast<unsigned>((packed>>32)&0xffff))) return false;
        if(!init || !terminate_ || !query || ADLX_FAILED(query(&text)) || !text) return false;version=text;
        const auto hr=init(ADLX_FULL_VERSION,&system);
        if(ADLX_FAILED(hr) || !system) {system=nullptr;return false;}
        ownsInit_=hr!=ADLX_ALREADY_INITIALIZED;
        adlx::IADLX3DSettingsServicesPtr base;
        if(ADLX_FAILED(system->Get3DSettingsServices(&base)) || !base) return false;
        services=adlx::IADLX3DSettingsServices3Ptr(base);
        return services!=nullptr;
    }
    ~Session() {services=nullptr;system=nullptr;if(ownsInit_ && terminate_) terminate_();if(module_) FreeLibrary(module_);}
};
class WindowsOps final:public Ops {
    std::filesystem::path root_;std::string cpu_;std::function<bool()> allowed_;Session session_;
    Adapter inspect(adlx::IADLXGPU* gpu,adlx::IADLX3DFidelityFXSuperResolutionPtr& feature) {
        Adapter a;const char *vendor=nullptr,*name=nullptr,*id=nullptr,*driver=nullptr;
        if(ADLX_FAILED(gpu->VendorId(&vendor)) || !vendor || (lower(vendor)!="1002" && lower(vendor)!="0x1002")) return a;
        a.amd=true;
        if(ADLX_SUCCEEDED(gpu->Name(&name)) && name) a.name=name;
        if(ADLX_SUCCEEDED(gpu->PNPString(&id)) && id) a.id=lower(id);
        if(ADLX_SUCCEEDED(gpu->DriverPath(&driver))) a.driverVersion=registryDriverVersion(driver);
        a.candidate=integratedCandidate(a.name,cpu_);a.reason="driver_interface_unavailable";
        ADLX_GPU_TYPE type=GPUTYPE_UNDEFINED;
        if(ADLX_FAILED(gpu->Type(&type)) || type==GPUTYPE_UNDEFINED) {a.reason="gpu_type_unreadable";return a;}
        a.typeRead=true;a.integrated=type==GPUTYPE_INTEGRATED;
        if(!a.integrated) {a.reason="discrete_gpu";return a;}
        if(a.id.empty() || a.driverVersion.empty()) {a.reason="identity_unreadable";return a;}
        a.fence=a.id+"|"+a.driverVersion+"|"+session_.version;
        if(!session_.services || ADLX_FAILED(session_.services->GetFidelityFXSuperResolution(gpu,&feature)) || !feature) return a;
        adlx_bool supported=false,enabled=false;
        if(ADLX_FAILED(feature->IsSupported(&supported))) {a.reason="support_unreadable";return a;}
        if(!supported) {a.reason="not_supported";return a;}
        a.supported=true;
        if(ADLX_FAILED(feature->IsEnabled(&enabled))) {a.reason="state_unreadable";return a;}
        a.enabled=static_cast<bool>(enabled);a.reason="ready";return a;
    }
public:
    WindowsOps(const std::wstring& root,const std::string& cpu,std::function<bool()> allowed={})
        :root_(root),cpu_(cpu),allowed_(std::move(allowed)){}
    bool active() const override {return !allowed_ || allowed_();}
    std::vector<Adapter> probe() override {
        if(!active()) return {};
        auto fallback=presentAmd(cpu_);
        if(fallback.empty() || !active()) return fallback;
        if(!session_.open()) {for(auto& a:fallback) a.reason="driver_interface_unavailable";return fallback;}
        adlx::IADLXGPUListPtr gpus;
        if(ADLX_FAILED(session_.system->GetGPUs(&gpus)) || !gpus || gpus->Size()>32) {
            for(auto& a:fallback) a.reason="support_unreadable";return fallback;
        }
        std::vector<Adapter> result;
        for(adlx_uint i=gpus->Begin();i<gpus->End() && active();++i) {
            adlx::IADLXGPUPtr gpu;if(ADLX_FAILED(gpus->At(i,&gpu)) || !gpu) continue;
            adlx::IADLX3DFidelityFXSuperResolutionPtr feature;auto a=inspect(gpu,feature);
            if(a.amd) result.push_back(std::move(a));
        }
        if(result.empty()) {for(auto& a:fallback) a.reason="support_unreadable";return fallback;}
        return result;
    }
    bool load(Json& policy) override {
        try {
            const auto file=root_/L"amd-fsr-upgrade.json";
            if(!std::filesystem::exists(file)) {policy=defaults();return true;}
            if(std::filesystem::file_size(file)>262144) return false;
            std::ifstream input(file,std::ios::binary);if(!input) return false;policy=Json::parse(input);return true;
        } catch(...) {return false;}
    }
    bool save(const Json& policy) override {
        try {
            if(!active()) return false;
            const auto file=root_/L"amd-fsr-upgrade.json",temp=root_/L"amd-fsr-upgrade.json.tmp";
            const auto text=policy.dump(2);HANDLE handle=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(handle==INVALID_HANDLE_VALUE) return false;
            DWORD written=0;const bool ok=WriteFile(handle,text.data(),static_cast<DWORD>(text.size()),&written,nullptr) && written==text.size() && FlushFileBuffers(handle);
            CloseHandle(handle);if(!ok) return false;
            return MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        } catch(...) {return false;}
    }
    bool write(const Adapter& expected,bool target) override {
        if(!active() || !admitted(expected) || !session_.services || presentAmd(cpu_).empty()) return false;
        adlx::IADLXGPUListPtr gpus;if(ADLX_FAILED(session_.system->GetGPUs(&gpus)) || !gpus || gpus->Size()>32) return false;
        for(adlx_uint i=gpus->Begin();i<gpus->End();++i) {
            adlx::IADLXGPUPtr gpu;if(ADLX_FAILED(gpus->At(i,&gpu)) || !gpu) continue;
            adlx::IADLX3DFidelityFXSuperResolutionPtr feature;const auto current=inspect(gpu,feature);
            if(current.id!=expected.id) continue;
            if(!admitted(current) || current.fence!=expected.fence || !feature) return false;
            if(!active()) return false;
            return ADLX_SUCCEEDED(feature->SetEnabled(target));
        }return false;
    }
};
inline Json runtime(const std::wstring& root,const std::string& cpu,const std::string& action,const Json& args=Json::object(),
                    std::function<bool()> allowed={}) {
    if(allowed && !allowed()) return inactive();
    static std::mutex serial;std::lock_guard<std::mutex> guard(serial);
    if(allowed && !allowed()) return inactive();
    // Cross-instance exclusion also protects the durable one-shot marker.
    const auto path=std::filesystem::path(root)/L"amd-fsr-upgrade.lock";
    HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(lock==INVALID_HANDLE_VALUE) return {{"schema",1},{"ok",false},{"reason","policy_busy"},{"defaultEnabled",false},{"versionConfirmed",false},{"adapters",Json::array()}};
    struct Close {HANDLE value;~Close(){CloseHandle(value);}} closer{lock};
    WindowsOps ops(root,cpu,std::move(allowed));return run(ops,action,args);
}
} // namespace ymcc::amdfsr
