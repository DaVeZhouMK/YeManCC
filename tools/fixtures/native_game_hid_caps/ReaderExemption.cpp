#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include <json.hpp>

using DWORD=std::uint32_t;
constexpr DWORD INVALID_FILE_ATTRIBUTES=0xffffffff;
constexpr int MAX_PATH=260;
static const std::wstring POWER_CONTROL_DIR=L"C:\\YMCC\\PowerControl";
static const std::wstring modulePath=L"C:\\YMCC\\YeManCC\\YeManCC.exe";
static const std::wstring lightPath=POWER_CONTROL_DIR+L"\\light-setter\\YeManLightSetter.exe";
static const std::wstring canonicalPath=POWER_CONTROL_DIR+L"\\feature-assets\\virtual-gamepad\\YeManInputHost.exe";
static const std::wstring legacyPath=L"C:\\YMCC\\YeManCC\\InputHost\\YeManInputHost.exe";
static std::set<std::wstring> presentPaths;
static DWORD GetFileAttributesW(const wchar_t* value){return presentPaths.contains(value)?0:INVALID_FILE_ATTRIBUTES;}
static DWORD GetModuleFileNameW(void*,wchar_t* buffer,DWORD count){const auto length=std::min<std::size_t>(count-1,modulePath.size());std::copy_n(modulePath.begin(),length,buffer);buffer[length]=0;return static_cast<DWORD>(length);}
static std::wstring hidHideCurrentModulePath(){return modulePath;}
@@PRODUCTION_READERS@@

static std::set<std::wstring> registeredPaths;
static std::wstring callerPath;
static bool activeCloak,hiddenDevice;
static bool GetActive(){return activeCloak;}
struct InertDevicePath{unsigned short Length=0;};
static bool Blacklisted(InertDevicePath*,unsigned long){return hiddenDevice;}
static bool Whitelisted(std::uintptr_t,bool* cacheHit){*cacheHit=true;return registeredPaths.contains(callerPath);}
template<class... Arguments>static void LogEvent(Arguments...){ }
template<class... Arguments>static long WdfMemoryCreate(Arguments...){return -1;}
template<class... Arguments>static long RtlStringCchCopyUnicodeStringEx(Arguments...){return -1;}
static void WdfObjectDelete(void*){ }
#define FALSE false
#define TRUE true
#define SYSTEM_PID 4
#define PROCESS_HANDLE_TO_PROCESS_ID(value) (value)
#define ETW(value) 0
#define TRACE_ALWAYS(...) ((void)0)
#define WDF_NO_OBJECT_ATTRIBUTES nullptr
#define NonPagedPool 0
#define LOGIC_TAG 0
#define WCHAR wchar_t
#define NT_SUCCESS(value) ((value)>=0)
#define STRSAFE_NO_TRUNCATION 0
static bool driverPermits(const std::wstring& processPath,bool cloak,bool hidden,bool system,DWORD desiredAccess){
    callerPath=processPath;activeCloak=cloak;hiddenDevice=hidden;
    const std::uintptr_t processId=system?SYSTEM_PID:1000;
    const unsigned long sessionId=1;
    InertDevicePath deviceInstancePath;
    bool accessDenied=false,cacheHit=false;
    void* wdfMemory=nullptr;
    wchar_t* buffer=nullptr;
    long ntstatus=0;
    (void)desiredAccess;
@@DRIVER_AUTHORIZATION@@
    return !accessDenied;
}

int main(int argumentCount,char** arguments){
    if(argumentCount!=2)return 2;
    using Json=nlohmann::json;
    std::vector<std::string> failures;
    Json rows=Json::array();
    int assertions=0;
    auto check=[&](bool value,const std::string& label){++assertions;if(!value)failures.push_back(label);};
    for(const std::string family: {"MSI","ASUS","GPD","Lenovo","AYANEO","AOKZOE","OneXPlayer","Valve","Ayn","Zotac","Minisforum","SuiPlay","Generic"}){
        for(bool canonical: {false,true})for(bool legacy: {false,true})for(bool light: {false,true}){
            presentPaths.clear();
            if(canonical)presentPaths.insert(canonicalPath);
            if(legacy)presentPaths.insert(legacyPath);
            if(light)presentPaths.insert(lightPath);
            const auto selected=inputHostExecutablePath();
            const auto readers=hidHideExemptApplicationPaths();
            registeredPaths={readers.begin(),readers.end()};
            const bool helperPresent=canonical||legacy;
            const auto label=family+"/canonical="+std::to_string(canonical)+"/legacy="+std::to_string(legacy)+"/light="+std::to_string(light);
            check(registeredPaths.contains(modulePath),label+" main-reader");
            check(registeredPaths.contains(lightPath)==light,label+" light-reader");
            check(selected==(canonical?canonicalPath:legacyPath),label+" actual-helper-selection");
            check(registeredPaths.contains(selected)==helperPresent,label+" exact-helper-registration");
            check(registeredPaths.size()==std::size_t(1+light+helperPresent),label+" no-extra-reader");
            check(!registeredPaths.contains(canonical?legacyPath:canonicalPath),label+" unused-helper-not-granted");
            check(driverPermits(modulePath,true,true,false,0),label+" main-reader-still-allowed");
            check(!driverPermits(L"C:\\Games\\Game.exe",true,true,false,0),label+" external-zero-access-denied");
            check(!driverPermits(L"C:\\Games\\Game.exe",true,true,false,0x80000000),label+" external-read-denied");
            check(driverPermits(L"C:\\Games\\Game.exe",false,true,false,0),label+" cloak-off");
            check(driverPermits(L"C:\\Games\\Game.exe",true,false,false,0),label+" device-returned");
            check(driverPermits(L"C:\\System",true,true,true,0),label+" driver-system-rule");
            if(helperPresent){
                check(driverPermits(selected,true,true,false,0),label+" helper-read-zero-access-denied");
                check(driverPermits(selected,true,true,false,0x80000000),label+" helper-read-access-denied");
            }
            rows.push_back({{"family",family},{"canonicalPresent",canonical},{"legacyPresent",legacy},{"lightPresent",light},{"helperRegistered",registeredPaths.contains(selected)},{"helperPresent",helperPresent}});
        }
    }
    Json result={{"passed",failures.empty()},{"cases",rows.size()},{"assertions",assertions},{"failures",failures},{"rows",rows},{"realHardwareAccess",false},{"driverLoaded",false},{"productMainExecuted",false}};
    std::ofstream(arguments[1])<<result.dump(2)<<'\n';
    std::cout<<Json{{"passed",failures.empty()},{"cases",rows.size()},{"assertions",assertions},{"failures",failures.size()}}.dump()<<'\n';
    return failures.empty()?0:1;
}
