#pragma once
#include "monitor_hwinfo_transaction.h"
#include "monitor_hwinfo_config_commit.h"
#include "monitor_freshness.h"
#include <tlhelp32.h>
#include <shellapi.h>
#include <objbase.h>
#include <wincrypt.h>
#include <filesystem>
#include <functional>
#include <chrono>
#include <memory>
#include <set>
#include <atomic>
#include <cstring>
#include <exception>
#include "../deps/json/json.hpp"

namespace ymcc::hwinfo {
using Json=nlohmann::json;
namespace fs=std::filesystem;
struct Handle {
    HANDLE h=nullptr;
    Handle()=default;explicit Handle(HANDLE v):h(v){}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    Handle(Handle&&v)noexcept:h(v.h){v.h=nullptr;}
    Handle&operator=(Handle&&v)noexcept{if(this!=&v){reset();h=v.h;v.h=nullptr;}return *this;}
    ~Handle(){reset();}void reset(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=nullptr;}
    explicit operator bool()const{return h&&h!=INVALID_HANDLE_VALUE;}
};
struct Key {HKEY h=nullptr;~Key(){if(h)RegCloseKey(h);}Key()=default;Key(const Key&)=delete;};
inline std::uint64_t clockMs(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
inline bool exists(const std::wstring&p){const auto v=GetFileAttributesW(p.c_str());return v!=INVALID_FILE_ATTRIBUTES&&!(v&FILE_ATTRIBUTE_DIRECTORY);}
inline void winFail(int code,const char*text,DWORD e=GetLastError()){throw Error(code,text,e==ERROR_ACCESS_DENIED||e==ERROR_PRIVILEGE_NOT_HELD||e==ERROR_ELEVATION_REQUIRED,e);}
inline std::wstring env(const wchar_t*name){auto size=GetEnvironmentVariableW(name,nullptr,0);if(!size||size>32768)return {};std::wstring s(size,L'\0');const auto got=GetEnvironmentVariableW(name,s.data(),size);if(!got||got>=size)return {};s.resize(got);return s;}
inline std::wstring imagePath(HANDLE process){std::wstring s(32768,L'\0');DWORD n=static_cast<DWORD>(s.size());if(!QueryFullProcessImageNameW(process,0,s.data(),&n))return {};s.resize(n);return s;}
inline std::wstring selfPath(){std::wstring s(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,s.data(),static_cast<DWORD>(s.size()));if(!n||n>=s.size())winFail(1,"YMCC image path unavailable");s.resize(n);return s;}
inline bool executablePathValid(const std::wstring&p){return !p.empty()&&p.find_first_of(std::wstring{L'\0',L'\r',L'\n',L'"'})==std::wstring::npos&&fs::path(p).is_absolute()&&lower(fs::path(p).filename().wstring())==L"hwinfo64.exe"&&exists(p);}
inline std::vector<DWORD> processes(){
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));if(!snapshot)winFail(4,"HWiNFO process enumeration failed");std::vector<DWORD> out;PROCESSENTRY32W row{sizeof(row)};
    if(!Process32FirstW(snapshot.h,&row)){if(GetLastError()!=ERROR_NO_MORE_FILES)winFail(4,"HWiNFO process enumeration could not start");return out;}
    do{if(_wcsicmp(row.szExeFile,L"HWiNFO64.exe")==0)out.push_back(row.th32ProcessID);}while(Process32NextW(snapshot.h,&row));if(GetLastError()!=ERROR_NO_MORE_FILES)winFail(4,"HWiNFO process enumeration did not finish");return out;
}
inline std::optional<std::string> readBytes(const std::wstring&p,std::size_t limit,bool optional=false){
    Handle f(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(!f){const auto e=GetLastError();if(optional&&(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND))return {};winFail(8,"HWiNFO configuration could not be opened",e);}
    LARGE_INTEGER size{};if(!GetFileSizeEx(f.h,&size))winFail(8,"HWiNFO configuration length unavailable");require(size.QuadPart>=0&&static_cast<unsigned long long>(size.QuadPart)<=limit,"HWiNFO configuration exceeds its byte budget");std::string s(static_cast<std::size_t>(size.QuadPart),'\0');DWORD got=0;
    if(!s.empty()&&(!ReadFile(f.h,s.data(),static_cast<DWORD>(s.size()),&got,nullptr)||got!=s.size()))winFail(8,"HWiNFO configuration changed during read");char extra=0;if(!ReadFile(f.h,&extra,1,&got,nullptr)||got!=0)throw Error(8,"HWiNFO configuration grew during read");return s;
}
inline std::string sha256(const std::wstring&p){
    HCRYPTPROV provider=0;if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))winFail(8,"HWiNFO hash provider unavailable");struct Provider{HCRYPTPROV h;~Provider(){CryptReleaseContext(h,0);}}a{provider};
    HCRYPTHASH hash=0;if(!CryptCreateHash(provider,CALG_SHA_256,0,0,&hash))winFail(8,"HWiNFO hash unavailable");struct Hash{HCRYPTHASH h;~Hash(){CryptDestroyHash(h);}}b{hash};
    Handle f(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(!f)winFail(8,"HWiNFO image could not be hashed");LARGE_INTEGER size{};if(!GetFileSizeEx(f.h,&size))winFail(8,"HWiNFO image length unavailable");require(size.QuadPart>0&&size.QuadPart<=67108864,"HWiNFO image exceeds verified sensor-policy budget");
    unsigned char buffer[65536];DWORD got=0;for(;;){if(!ReadFile(f.h,buffer,sizeof(buffer),&got,nullptr))winFail(8,"HWiNFO image hash read failed");if(!got)break;if(!CryptHashData(hash,buffer,got,0))winFail(8,"HWiNFO image hash failed");}unsigned char digest[32]{};DWORD length=32;if(!CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0)||length!=32)winFail(8,"HWiNFO hash result unavailable");constexpr char hex[]="0123456789abcdef";std::string out;out.reserve(64);for(auto c:digest){out+=hex[c>>4];out+=hex[c&15];}return out;
}
inline void writeAtomic(const std::wstring&path,const std::string&bytes,bool keepExisting=false){
    // One named recovery mutex serializes all callers, including legacy entries.
    if(keepExisting&&exists(path))return;static std::atomic<unsigned long long> sequence{0};const auto temp=path+L".ymcc-tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence);
    Handle f(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(!f)winFail(5,"HWiNFO staged configuration could not be created");
    struct Clean{std::wstring p;~Clean(){DeleteFileW(p.c_str());}}clean{temp};DWORD wrote=0;
    if((!bytes.empty()&&(!WriteFile(f.h,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)||wrote!=bytes.size()))||!FlushFileBuffers(f.h))winFail(5,"HWiNFO staged configuration write failed");f.reset();
    if(keepExisting){if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)){const auto e=GetLastError();if((e==ERROR_FILE_EXISTS||e==ERROR_ALREADY_EXISTS)&&exists(path))return;winFail(5,"HWiNFO original backup commit failed",e);}}
    else if(exists(path)){if(!ReplaceFileW(path.c_str(),temp.c_str(),nullptr,0,nullptr,nullptr))winFail(5,"HWiNFO configuration atomic replacement failed");}
    else if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH))winFail(5,"HWiNFO configuration commit failed");
    const auto verified=readBytes(path,bytes.size()+1);if(!verified||*verified!=bytes)throw Error(5,"HWiNFO committed bytes did not verify");
}
inline std::optional<bool> acquisition(const std::wstring&powerControl){
    const auto data=readBytes((fs::path(powerControl)/L"yeman-settings.json").wstring(),16777216,true);if(!data)return {};
    // Keep one bounded read buffer. Skipping a BOM must not clone or shift the
    // entire unified settings document on each startup/recovery transaction.
    auto first=data->cbegin();if(data->compare(0,3,"\xef\xbb\xbf")==0)first+=3;
    Json document;try{
        // Small policy-only documents retain the original parser. For larger
        // unified settings, validate every token but retain only this owner's
        // policy subtree; all other settings stay in their existing repository.
        if(data->size()<=4096)document=Json::parse(first,data->cend());
        else document=Json::parse(first,data->cend(),[](int depth,Json::parse_event_t event,Json& parsed){
            if(event!=Json::parse_event_t::key)return true;
            const auto& key=parsed.get_ref<const std::string&>();
            if(depth==1)return key=="rtss";
            if(depth==2)return key=="monitorAcquisition";
            return depth==3&&(key=="version"||key=="enabled"||key=="snapshotPolling");
        });
    }catch(...){throw Error(8,"Unified YMCC settings are malformed");}require(document.is_object(),"Unified YMCC settings must be an object");
    if(!document.contains("rtss")||document["rtss"].is_null())return {};const auto& rtss=document["rtss"];require(rtss.is_object(),"Unified rtss settings must be an object");if(!rtss.contains("monitorAcquisition")||rtss["monitorAcquisition"].is_null())return {};const auto&p=rtss["monitorAcquisition"];
    require(p.is_object()&&p.contains("version")&&p["version"].is_number_integer()&&p["version"]==1&&p.contains("enabled")&&p["enabled"].is_boolean(),"Unsupported HWiNFO acquisition settings");if(!p["enabled"].get<bool>())return {};require(p.contains("snapshotPolling")&&p["snapshotPolling"].is_boolean(),"HWiNFO acquisition choices must be booleans");return p["snapshotPolling"].get<bool>();
}
inline std::wstring registryString(HKEY parent,const wchar_t*value){DWORD type=0,size=0;auto r=RegQueryValueExW(parent,value,nullptr,&type,nullptr,&size);if(r==ERROR_FILE_NOT_FOUND)return {};if(r!=ERROR_SUCCESS||type!=REG_SZ||size>65536||size%2)return {};std::wstring s(size/2,L'\0');if(RegQueryValueExW(parent,value,nullptr,&type,reinterpret_cast<BYTE*>(s.data()),&size)!=ERROR_SUCCESS)return {};while(!s.empty()&&s.back()==L'\0')s.pop_back();if(s.find(L'\0')!=std::wstring::npos)return {};return s;}
inline std::wstring resolve(const std::wstring& explicitPath){
    if(!explicitPath.empty()){if(!executablePathValid(explicitPath))throw Error(2,"The explicit HWiNFO64.exe path is invalid or missing");return fs::absolute(explicitPath).lexically_normal().wstring();}
    for(auto pid:processes()){Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));if(p){auto path=imagePath(p.h);if(executablePathValid(path))return path;}}
    for(const auto* name:{L"ProgramFiles",L"ProgramFiles(x86)",L"LOCALAPPDATA"}){const auto base=env(name);if(base.empty())continue;auto candidate=(fs::path(base)/L"HWiNFO64"/L"HWiNFO64.exe").wstring();if(executablePathValid(candidate))return candidate;}
    for(auto hive:{HKEY_LOCAL_MACHINE,HKEY_CURRENT_USER})for(auto view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}){
        Key uninstall;if(RegOpenKeyExW(hive,L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,KEY_READ|view,&uninstall.h)!=ERROR_SUCCESS)continue;
        for(DWORD i=0;i<4096;++i){wchar_t name[512];DWORD length=512;const auto r=RegEnumKeyExW(uninstall.h,i,name,&length,nullptr,nullptr,nullptr,nullptr);if(r==ERROR_NO_MORE_ITEMS)break;if(r!=ERROR_SUCCESS)continue;Key app;if(RegOpenKeyExW(uninstall.h,name,0,KEY_READ|view,&app.h)!=ERROR_SUCCESS)continue;if(lower(registryString(app.h,L"DisplayName")).find(L"hwinfo")==std::wstring::npos)continue;
            const auto directory=registryString(app.h,L"InstallLocation");if(!directory.empty()){auto p=(fs::path(directory)/L"HWiNFO64.exe").wstring();if(executablePathValid(p))return p;}
            auto icon=registryString(app.h,L"DisplayIcon");const auto comma=icon.find(L',');if(comma!=std::wstring::npos)icon.erase(comma);icon=trim(icon);if(icon.size()>=2&&icon.front()==L'"'&&icon.back()==L'"')icon=icon.substr(1,icon.size()-2);if(executablePathValid(icon))return icon;
        }
    }return {};
}
inline std::optional<Proof> proofFromView(const unsigned char*view,std::size_t capacity,unsigned index,unsigned expected,std::uint64_t now){
    if(!view||capacity<44||!periodAllowed(expected))return {};
        const auto u32=[&](std::size_t offset){std::uint32_t v=0;std::memcpy(&v,view+offset,4);return v;};const auto stamp=[&](){MonitorHeaderStamp s;s.signature=u32(0);s.version=u32(4);s.revision=u32(8);std::memcpy(&s.pollTime,view+12,8);s.sensorOffset=u32(20);s.sensorSize=u32(24);s.sensorCount=u32(28);s.readingOffset=u32(32);s.readingSize=u32(36);s.readingCount=u32(40);return s;};
        const auto a=stamp();if(a.signature!=0x53695748||a.readingSize<316||a.readingSize>16384||!a.readingCount||a.readingCount>100000||a.sensorOffset<44||a.sensorOffset>capacity)return {};
        const auto total=a.readingSize*a.readingCount;if(total>33554432ULL||a.readingOffset>=capacity||a.readingOffset+total>capacity)return {};
        const bool known=a.version==2&&a.sensorOffset>=48&&capacity>=48;const auto period=known?u32(44):0;
        if(expected&&(!known||a.sensorOffset>a.readingOffset||period!=expected))return {};
        if(!a.pollTime||!monitorProducerTimeRepresentable(a.pollTime))return {};
        const auto source=a.pollTime*1000ULL;if(source>now+1000ULL||monitorProducerAgeMs(a.pollTime,now)>=monitorFreshnessThresholdMs(period))return {};
        volatile unsigned char readable=view[a.readingOffset];(void)readable;const auto b=stamp();if(!monitorHeaderStable(a,b)||(known&&u32(44)!=period))return {};
        return Proof{a.pollTime,{index,a.version,a.revision,a.sensorOffset,a.sensorSize,a.sensorCount,a.readingOffset,a.readingSize,a.readingCount,known?1ULL:0ULL,period}};
}
inline std::optional<Proof> sharedProof(unsigned expected=0){
    if(!periodAllowed(expected))return {};
    const wchar_t*names[]={L"Global\\HWiNFO_SENS_SM2",L"HWiNFO_SENS_SM2",L"Global\\HWiNFO_SENS_SM",L"HWiNFO_SENS_SM"};
    for(unsigned index=0;index<4;++index){
        Handle mapping(OpenFileMappingW(FILE_MAP_READ,FALSE,names[index]));if(!mapping)continue;
        const auto*view=static_cast<const unsigned char*>(MapViewOfFile(mapping.h,FILE_MAP_READ,0,0,0));if(!view)continue;
        struct Unmap{const void*p;~Unmap(){UnmapViewOfFile(p);}}unmap{view};MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(view,&info,sizeof(info)))continue;
        const auto proof=proofFromView(view,info.RegionSize,index,expected,clockMs());
        if(proof)return proof;
    }return {};
}
inline constexpr const char* verifiedSensorImage="e4ce0c85c4288fa7e8eb12ebeece99efff4d81e0b13de53f091daabc81a3c9d3";
struct SensorState {bool exists=false;DWORD value=0;bool operator==(const SensorState&v)const{return exists==v.exists&&(!exists||value==v.value);}};
inline bool sensorGroupAllowed(const std::wstring& group){if(group==L"F00F5000_0")return true;for(const auto* family:{L"F0000101_",L"F0000302_"}){const std::wstring prefix=family;if(group.compare(0,prefix.size(),prefix)!=0)continue;const auto suffix=group.substr(prefix.size());unsigned n=0;return number(suffix,n)&&n<=31&&suffix==std::to_wstring(n);}return false;}
inline std::vector<std::wstring> sensorGroups(){std::vector<std::wstring> groups;groups.reserve(65);for(const auto* family:{L"F0000101_",L"F0000302_"})for(unsigned i=0;i<32;++i)groups.push_back(std::wstring(family)+std::to_wstring(i));groups.push_back(L"F00F5000_0");return groups;}
inline std::string ascii(const std::wstring&s){std::string out;for(auto c:s){require(c>0&&c<128,"Non-ASCII fixed sensor identity");out+=static_cast<char>(c);}return out;}
inline SensorState readSensor(const std::wstring& group){require(sensorGroupAllowed(group),"Unsupported unused-sensor group identity");Key key;const auto path=L"Software\\HWiNFO64\\Sensors\\"+group;auto rc=RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key.h);if(rc==ERROR_FILE_NOT_FOUND)return {};if(rc!=ERROR_SUCCESS)winFail(8,"HWiNFO sensor preference read failed",rc);DWORD type=0,size=sizeof(DWORD),value=0;rc=RegQueryValueExW(key.h,L"Enabled",nullptr,&type,reinterpret_cast<BYTE*>(&value),&size);if(rc==ERROR_FILE_NOT_FOUND)return {};if(rc!=ERROR_SUCCESS||type!=REG_DWORD||size!=4)throw Error(8,"Unexpected sensor Enabled registry type");return {true,value};}
inline void writeSensor(const std::wstring&group,SensorState state){require(sensorGroupAllowed(group),"Unsupported unused-sensor group identity");Key key;const auto path=L"Software\\HWiNFO64\\Sensors\\"+group;LSTATUS rc;
    if(state.exists){rc=RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_SET_VALUE|KEY_WOW64_64KEY,nullptr,&key.h,nullptr);if(rc!=ERROR_SUCCESS)winFail(8,"HWiNFO sensor preference open failed",rc);rc=RegSetValueExW(key.h,L"Enabled",0,REG_DWORD,reinterpret_cast<const BYTE*>(&state.value),sizeof(DWORD));}
    else{rc=RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_SET_VALUE|KEY_WOW64_64KEY,&key.h);if(rc==ERROR_FILE_NOT_FOUND)return;if(rc!=ERROR_SUCCESS)winFail(8,"HWiNFO sensor preference open failed",rc);rc=RegDeleteValueW(key.h,L"Enabled");if(rc==ERROR_FILE_NOT_FOUND)return;}
    if(rc!=ERROR_SUCCESS)winFail(8,"HWiNFO sensor preference write failed",rc);
}
inline std::string sensorReceipt(const std::map<std::wstring,SensorState>& states,const std::wstring&prefix=L""){Json values=Json::object();for(const auto&[group,state]:states)if(prefix.empty()||group.compare(0,prefix.size(),prefix)==0)values[ascii(group)]={{"Exists",state.exists},{"Value",state.value}};const auto text=Json({{"schema",1},{"executableSha256",verifiedSensorImage},{"values",std::move(values)}}).dump();require(text.size()<=16384,"Unused-sensor backup exceeds fixed budget");return text;}
inline std::string applySensorBudget(const std::wstring&exe,const std::wstring&destination){
    const SensorState disabled{true,0};std::map<std::wstring,SensorState> before;std::vector<std::wstring> pending,changed;
    try{
        if(sha256(exe)!=verifiedSensorImage)return "unsupported-image";
        for(const auto&e:entries(decodeIni(*readBytes(destination,262144))))if(lower(e.section)==L"settings"&&lower(e.key)==L"rememberpreferences"){unsigned n=0;if(!number(e.value,n)||!n)return "preferences-disabled";}
        for(const auto&group:sensorGroups()){const auto state=readSensor(group);before[group]=state;if(!(state==disabled))pending.push_back(group);}if(pending.empty())return "already-configured";
        writeAtomic(destination+L".ymcc-sensor-groups-original.json",sensorReceipt(before,L"F0000101_"),true);
        writeAtomic(destination+L".ymcc-sensor-groups-last.json",sensorReceipt(before));
        if(std::any_of(pending.begin(),pending.end(),[](const auto&g){return g.compare(0,9,L"F0000302_")==0;}))writeAtomic(destination+L".ymcc-sensor-groups-cstate-original.json",sensorReceipt(before,L"F0000302_"),true);
        if(std::find(pending.begin(),pending.end(),L"F00F5000_0")!=pending.end())writeAtomic(destination+L".ymcc-sensor-groups-rtss-original.json",sensorReceipt(before,L"F00F5000_0"),true);
        for(const auto&group:pending){if(!processes().empty())throw Error(4,"Producer appeared during sensor configuration");if(!(readSensor(group)==before[group]))throw Error(8,"Concurrent sensor preference changed");changed.push_back(group);writeSensor(group,disabled);if(!(readSensor(group)==disabled))throw Error(8,"Sensor Enabled verification failed");}return "configured";
    }catch(...){bool rollback=true;for(auto i=changed.rbegin();i!=changed.rend();++i){try{if(!processes().empty())throw Error(4,"Producer running during sensor rollback");const auto current=readSensor(*i);if(current==before[*i])continue;if(!(current==disabled))throw Error(8,"Concurrent sensor choice preserved during rollback");writeSensor(*i,before[*i]);if(!(readSensor(*i)==before[*i]))throw Error(8,"Sensor rollback verification failed");}catch(...){rollback=false;}}
        // Optional producer savings must never block required data/FPS recovery.
        return rollback?"unconfirmed-rolled-back":"unconfirmed-rollback-deferred";
    }
}
struct Operations {
    Handle mutex,producer;bool owned=false;
    std::wstring powerControl,destination;
    std::function<bool()> cancelled;
    explicit Operations(std::wstring directory,std::function<bool()> cancel={}):powerControl(std::move(directory)),cancelled(std::move(cancel)){}
    ~Operations(){release();}
    bool acquire(DWORD ms){mutex=Handle(CreateMutexW(nullptr,FALSE,L"Global\\YeManCC_HWiNFO_Recovery"));if(!mutex)winFail(1,"HWiNFO recovery mutex unavailable");const auto rc=WaitForSingleObject(mutex.h,ms);owned=rc==WAIT_OBJECT_0||rc==WAIT_ABANDONED;if(rc==WAIT_FAILED)winFail(1,"HWiNFO recovery mutex wait failed");return owned;}
    void release(){if(owned&&mutex){ReleaseMutex(mutex.h);owned=false;}mutex.reset();}
    bool interrupted(){return cancelled&&cancelled();}
    bool healthy(){return !processes().empty()&&sharedProof().has_value();}
    void applyAffinity(){for(auto pid:processes()){Handle p(OpenProcess(PROCESS_SET_INFORMATION,FALSE,pid));if(p)SetProcessAffinityMask(p.h,0xA0);}}
    std::wstring resolve(const std::wstring& explicitPath){return hwinfo::resolve(explicitPath);}
    std::string planned(const Request&request,const std::optional<std::string>&existing){return mergeIni(managedIni(acquisition(powerControl)),existing?std::optional<Ini>{decodeIni(*existing)}:std::optional<Ini>{},request.period);}
    std::string planned(const Request&request){return planned(request,readBytes(destination,262144,true));}
    void prepare(const std::wstring&path,const Request&request){destination=(fs::path(path).parent_path()/L"HWiNFO64.INI").wstring();(void)planned(request);}
    void assertStopped(){if(!processes().empty())throw Error(4,"HWiNFO64.exe is running; configuration/launch refused");}
    void stop(){
        const auto list=processes();std::vector<Handle> running;running.reserve(list.size());
        for(auto pid:list){Handle p(OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));if(!p){const auto e=GetLastError();if(e==ERROR_INVALID_PARAMETER)continue;winFail(4,"HWiNFO64.exe could not be opened for confirmed stop",e);}const auto path=imagePath(p.h);if(path.empty())winFail(4,"HWiNFO64.exe process identity unavailable");if(lower(fs::path(path).filename().wstring())!=L"hwinfo64.exe")continue;running.push_back(std::move(p));}
        const auto started=GetTickCount64();for(auto&p:running){
            if(interrupted())throw Error(15,"Recovery invalidated before producer stop");
            if(WaitForSingleObject(p.h,0)==WAIT_OBJECT_0)continue;
            DWORD code=STILL_ACTIVE;if(!GetExitCodeProcess(p.h,&code))winFail(4,"HWiNFO64.exe termination state unavailable");
            // A prior stop can leave kernel I/O completing with a non-active
            // exit code. Wait for its exact handle; never kill it repeatedly.
            if(code==STILL_ACTIVE&&!TerminateProcess(p.h,0)&&WaitForSingleObject(p.h,0)!=WAIT_OBJECT_0)winFail(4,"HWiNFO64.exe could not be stopped");
        }
        for(auto&p:running){const auto elapsed=GetTickCount64()-started;const DWORD remaining=elapsed>=10000?0:static_cast<DWORD>(10000-elapsed);const auto rc=WaitForSingleObject(p.h,remaining);if(rc!=WAIT_OBJECT_0){DWORD code=STILL_ACTIVE;const bool known=GetExitCodeProcess(p.h,&code)!=FALSE;
            throw Error(4,known&&code!=STILL_ACTIVE?"HWiNFO64.exe termination is pending; no configuration or duplicate launch was attempted":"HWiNFO64.exe exit was not confirmed within 10 seconds");}}assertStopped();
    }
    void configure(const std::wstring&,const Request&request){
        assertStopped();
        const auto previous=readBytes(destination,262144,true);
        const auto plan=planned(request,previous);
        if(previous){
            writeAtomic(destination+L".ymcc-original.bak",*previous,true);
            writeAtomic(destination+L".ymcc-last.bak",*previous);
        }
        assertStopped();
        bool commitAttempted=false;
        try{
            commitConfiguration(previous,plan,[this]{assertStopped();},
                [this]{return readBytes(destination,262144,true);},
                [this,&commitAttempted](const std::string&bytes){
                    commitAttempted=true;writeAtomic(destination,bytes);
                },[this](unsigned delay){return wait(delay);});
        }catch(const ConfigConflict&){
            // The current file belongs to a concurrent edit. In particular,
            // do not roll it back just because its bytes equal our plan.
            throw;
        }catch(...){
            const auto error=std::current_exception();
            if(commitAttempted){
                try{
                    assertStopped();const auto current=readBytes(destination,262144,true);
                    if(current&&*current==plan){
                        if(previous)writeAtomic(destination,*previous);
                        else DeleteFileW(destination.c_str());
                    }
                }catch(...){ }
            }
            std::rethrow_exception(error);
        }
    }
    std::string sensorBudget(const std::wstring&exe){return applySensorBudget(exe,destination);}
    void launch(const std::wstring&exe){
        assertStopped();const auto directory=fs::path(exe).parent_path().wstring();
        // Preserve the old Start-Process shell launch semantics. Some HWiNFO
        // builds require AppInfo/UIAccess handling even from an elevated caller.
        SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
        info.lpVerb=L"open";info.lpFile=exe.c_str();info.lpDirectory=directory.c_str();info.nShow=SW_HIDE;
        if(!ShellExecuteExW(&info))winFail(6,"HWiNFO64.exe shell launch failed");
        producer=Handle(info.hProcess);
        if(!producer)throw Error(6,"Started HWiNFO64.exe process handle was not returned");
        SetProcessAffinityMask(producer.h,0xA0);
    }

    bool exited(){if(!producer)throw Error(6,"Started HWiNFO process handle unavailable");const auto rc=WaitForSingleObject(producer.h,0);if(rc==WAIT_FAILED)winFail(6,"Started HWiNFO process lifetime unavailable");return rc==WAIT_OBJECT_0;}
    std::optional<Proof> proof(unsigned expected){return sharedProof(expected);}
    bool wait(unsigned ms){const auto until=GetTickCount64()+ms;while(GetTickCount64()<until){if(cancelled&&cancelled())return false;const auto left=until-GetTickCount64();if(left>ms)break;Sleep(static_cast<DWORD>((std::min)(left,100ULL)));}return !(cancelled&&cancelled());}
};
inline std::wstring quoteArgument(const std::wstring&arg){std::wstring out=L"\"";unsigned slashes=0;for(auto c:arg){if(c==L'\\'){++slashes;continue;}if(c==L'"'){out.append(slashes*2+1,L'\\');out+=c;}else{out.append(slashes,L'\\');out+=c;}slashes=0;}out.append(slashes*2,L'\\');return out+L'"';}
inline std::wstring arguments(const Request&request){std::wstring args=L"--hwinfo-manage "+std::wstring(request.restart?L"restart":L"start")+L" --period "+std::to_wstring(request.period)+L" --power-control "+quoteArgument(request.powerControl);if(request.elevated)args+=L" --elevated";if(request.recoverOnly)args+=L" --recover-only";if(!request.executable.empty())args+=L" --executable "+quoteArgument(request.executable);return args;}
// The elevated child's namespace/access may differ from its parent. Its
// successful exit is not proof that this YMCC process can consume the data.
template<class Ops> Result confirmParentProducer(Ops& ops,unsigned period){
    std::optional<Proof> previous;bool sawProof=false;
    for(unsigned i=0;i<30;++i){
        if(ops.interrupted())return {15,false,ERROR_CANCELLED,"Parent producer confirmation cancelled",{}};
        const auto proof=ops.proof(period);
        if(ops.interrupted())return {15,false,ERROR_CANCELLED,"Parent producer confirmation cancelled",{}};
        if(proof){sawProof=true;if(previous&&proof->continues(*previous))return {0,false,0,"elevated-parent-producer-confirmed",{}};previous=proof;}
        else previous.reset();
        if(!ops.wait(1000))return {15,false,ERROR_CANCELLED,"Parent producer confirmation interrupted",{}};
    }
    return {sawProof?14:period?13:7,false,0,"Elevated producer was not confirmed readable and advancing from YMCC",{}};
}
// Pool threads and the private elevated command may start without a COM
// apartment. Scope it to this transaction; preserve a pre-existing apartment.
struct ShellApartment {
    HRESULT status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    ~ShellApartment(){if(SUCCEEDED(status))CoUninitialize();}
};
inline Result run(Request request,std::function<bool()> cancelled={},bool allowElevation=true){
    ShellApartment apartment;
    if(FAILED(apartment.status)&&apartment.status!=RPC_E_CHANGED_MODE)return {6,false,static_cast<DWORD>(apartment.status),"HWiNFO shell apartment unavailable",{}};
    Operations ops(request.powerControl,cancelled);auto result=transaction(ops,request);
    if(!result.needsElevation||request.elevated||!allowElevation)return result;
    if(cancelled&&cancelled())return {15,false,ERROR_CANCELLED,"Recovery invalidated before elevation",{}};
    const auto elevatedResult=[](Result value){value.elevationAttempted=true;return value;};
    // transaction has released the named mutex. Wait for the exact returned
    // YMCC child, never its deliberately long-lived HWiNFO descendant.
    request.elevated=true;request.restart=true;const auto self=selfPath(),args=arguments(request),directory=fs::path(self).parent_path().wstring();SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;info.lpVerb=L"runas";info.lpFile=self.c_str();info.lpParameters=args.c_str();info.lpDirectory=directory.c_str();info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info)){result.detail="HWiNFO recovery administrator permission was not granted";result.win32=GetLastError();return elevatedResult(result);}Handle child(info.hProcess);if(!child)return elevatedResult({15,false,0,"Elevated YMCC process handle unavailable",{}});
    const auto start=GetTickCount64();for(;;){const auto rc=WaitForSingleObject(child.h,100);if(rc==WAIT_OBJECT_0){DWORD code=0;if(!GetExitCodeProcess(child.h,&code)||code==STILL_ACTIVE)return elevatedResult({15,false,GetLastError(),"Elevated YMCC result unavailable",{}});if(code==0)return elevatedResult(confirmParentProducer(ops,request.period));return elevatedResult({static_cast<int>(code),false,0,"elevated-recovery-failed",{}});}if(rc==WAIT_FAILED)return elevatedResult({15,false,GetLastError(),"Elevated YMCC wait failed",{}});if(GetTickCount64()-start>=60000||(cancelled&&cancelled()))return elevatedResult({15,false,0,"Elevated YMCC result not confirmed; process was not terminated",{}});}
}
inline Json asJson(const Result&r){return {{"ok",r.code==0},{"code",r.code},{"detail",r.detail},{"win32Error",r.win32},{"needsElevation",r.needsElevation},{"elevationAttempted",r.elevationAttempted},{"sensorPolicy",r.sensorPolicy},{"owner","ymcc-native"}};}
// The exact private command is recognized before any UI/device/startup path.
// No implicit normal startup on malformed arguments or unsupported choices.
inline bool tryCommandLine(const wchar_t*commandLine,const std::wstring&defaultPowerControl,int&exitCode){
    int count=0;auto argv=CommandLineToArgvW(commandLine,&count);if(!argv)return false;struct Free{wchar_t**p;~Free(){LocalFree(p);}}free{argv};if(count<2||wcscmp(argv[1],L"--hwinfo-manage")!=0)return false;
    try{Request request;request.powerControl=defaultPowerControl;require(count>=3,"HWiNFO management mode missing");require(wcscmp(argv[2],L"start")==0||wcscmp(argv[2],L"restart")==0,"Invalid HWiNFO management mode");request.restart=wcscmp(argv[2],L"restart")==0;std::set<std::wstring> seen;
        for(int i=3;i<count;++i){const std::wstring flag=argv[i];require(seen.insert(flag).second,"Duplicate HWiNFO management argument");if(flag==L"--elevated"){request.elevated=true;continue;}if(flag==L"--recover-only"){request.recoverOnly=true;continue;}require(i+1<count,"HWiNFO management argument value missing");const std::wstring value=argv[++i];if(flag==L"--period"){unsigned p=0;require(number(value,p)&&std::to_wstring(p)==value&&periodAllowed(p),"Invalid HWiNFO period argument");request.period=p;}else if(flag==L"--executable"){require(executablePathValid(value),"Invalid explicit HWiNFO image path");request.executable=value;}else if(flag==L"--power-control"){require(fs::path(value).is_absolute()&&value.find_first_of(std::wstring{L'\0',L'\r',L'\n',L'"'})==std::wstring::npos,"Invalid YMCC settings directory");request.powerControl=value;}else throw Error(8,"Unknown HWiNFO management argument");}
        exitCode=run(request).code;
    }catch(const Error&e){exitCode=e.code;}catch(...){exitCode=1;}return true;
}
}
