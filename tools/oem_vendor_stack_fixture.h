#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <functional>
#include <algorithm>
#include <iostream>
#include <set>
#include <cstring>
#include "json.hpp"
using json=nlohmann::json;
/* FAMILY_ENUM */
static constexpr uint64_t kOemBitCc=1ull<<22,kOemBitAc=1ull<<23,kOemBitLib=1ull<<24,kOemBitClaw=1ull<<25,kOemBitQs=1ull<<26,kOemBitZotac=1ull<<47,kOemBitDots=1ull<<48,kOemBitLegionFrontL=1ull<<61,kOemBitLegionFrontR=1ull<<62;
static struct {YmccFamily family=YmccFamily::AsusRogAlly;std::string deviceClass="HandheldCompanion.Devices.ROGAlly";} g_machineIdentity;
static bool msiClawKnownBoardAuthority(){return g_machineIdentity.family==YmccFamily::MsiClaw;}
static std::atomic<bool> g_exitRequested{false};
enum class PowerLifecycle { Ready, Suspending, Suspended, Resuming };
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_powerGeneration{1};
static unsigned long long currentPowerGeneration() { return g_powerGeneration.load(); }
static unsigned wakeInputResumes = 0;
static void inputCaptureResumeSensorsForPower(bool) { ++wakeInputResumes; }
static void sgRecordFact(const char*, const json&) {}
static std::string W2U(const std::wstring& s){return std::string(s.begin(),s.end());}
static std::wstring U2W(const std::string& s){return std::wstring(s.begin(),s.end());}
static std::wstring POWER_CONTROL_DIR=L"memory";
static std::vector<json> logs;
static void appendNativeLifecycleLog(const char*,json x){logs.push_back(x);}
struct Service{DWORD type=SERVICE_AUTO_START,state=SERVICE_RUNNING;bool deny=false,stopFail=false,startFail=false,stuck=false,configFail=false,queryFail=false;ULONGLONG settleAt=0;DWORD settleState=SERVICE_RUNNING;DWORD stopDelay=0;};
struct Task {bool enabled=true,running=true,deny=false,setFail=false,stuck=false;unsigned setCalls=0,failSetCall=0;bool runFail=false;};
static std::map<std::wstring,Service> services;
static std::map<std::wstring,Task> tasks;
// Faults survive definition removal, just as scheduler/template access does.
struct TaskXmlPort {
    bool templateMissing=false,readyDenied=false,removeDenied=false,removeFail=false,removeStillPresent=false;
    bool importDenied=false,importFail=false,importStillAbsent=false,queryDeniedAfterRemove=false,queryDeniedAfterImport=false;
    bool removed=false,imported=false;
    unsigned removeDelay=0;
    Task importedTask{true,false}; // XML registration enables policy but does not start an instance.
};
static std::map<std::wstring,TaskXmlPort> taskXmlPorts;
static unsigned taskReadyCalls=0,taskRemovalCalls=0,taskRemovals=0,taskImportCalls=0,taskImports=0;
static std::vector<json> taskXmlTrace;
static std::function<void()> onTaskRemoved;
static std::vector<std::wstring> processes,stoppedProcesses;
static bool processFail=false,scmFail=false,queueFail=false,journalFail=false,journalExists=false;
static std::string journal;
static bool journalReadFails=false,journalShortRead=false,journalDirectory=false,journalReparse=false;
static DWORD journalAttributeError=0;
static unsigned journalWrites=0,journalReads=0;

#ifdef YMCC_VENDOR_NATIVE_IO
#ifndef YMCC_VENDOR_STACK_FIXTURE
#error Native journal IO requires all original OEM fake boundaries.
#endif
#include <filesystem>
#include <fstream>
#include <cwctype>
#include <aclapi.h>
#pragma comment(lib,"advapi32.lib")
namespace journal_native {
namespace fs=std::filesystem;
static bool active=false;
static fs::path root,caseRoot;
static std::map<HANDLE,fs::path> handles;
static std::set<fs::path> stages;
static std::string fault;
static json events=json::array();
static std::function<void()> afterPublish;
static bool aclRestoreFailed=false;
static unsigned seq=0;
static fs::path target(){return caseRoot/L"oem-vendor-stack-lease.json";}
static bool same(const fs::path& a,const fs::path& b){return _wcsicmp(a.c_str(),b.c_str())==0;}
static bool ancestry(const fs::path& p){
    for(auto q=p;!q.empty();){const auto attr=::GetFileAttributesW(q.c_str());if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT))return false;auto parent=q.parent_path();if(parent==q)break;q=parent;}
    return true;
}
static bool leaf(const fs::path& p){
    if(!active||!p.is_absolute()||p!=p.lexically_normal()||!same(p.parent_path(),caseRoot))return false;
    return same(caseRoot.parent_path(),root)&&ancestry(caseRoot);
}
static bool staged(const fs::path& p){
    if(!leaf(p))return false;
    const auto name=p.filename().native(),prefix=std::wstring(L"oem-vendor-stack-lease.json.ymcc.")+std::to_wstring(GetCurrentProcessId())+L".";
    if(!name.starts_with(prefix)||!name.ends_with(L".tmp"))return false;
    const auto number=name.substr(prefix.size(),name.size()-prefix.size()-4);
    return !number.empty()&&number.size()<=20&&number.find_first_not_of(L"0123456789")==std::wstring::npos;
}
static void event(const char* api,const fs::path& p,bool nativeCalled,bool nativeOk,bool returnedOk,DWORD error=0,bool injected=false,DWORD requested=0,DWORD transferred=0){
    events.push_back({{"api",api},{"leaf",W2U(p.filename().native())},{"nativeCalled",nativeCalled},{"nativeOk",nativeOk},{"returnedOk",returnedOk},{"win32Error",error},{"injected",injected},{"requested",requested},{"transferred",transferred}});
}
static bool bound(HANDLE h){
    auto i=handles.find(h);if(i==handles.end()||!leaf(i->second))return false;
    wchar_t name[32768]{};auto used=::GetFinalPathNameByHandleW(h,name,32768,FILE_NAME_NORMALIZED);
    if(!used||used>=32768)return false;std::wstring actual(name,used);if(actual.starts_with(L"\\\\?\\"))actual.erase(0,4);
    return same(fs::path(actual),i->second);
}
static HANDLE open(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE templateFile){
    const fs::path p(name);const bool journal=leaf(p)&&same(p,target());
    const bool temp=staged(p)&&disposition==CREATE_NEW&&access==GENERIC_WRITE;
    if(!journal&&!temp){SetLastError(ERROR_ACCESS_DENIED);event("CreateFileW",p,false,false,false,ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
    HANDLE h=::CreateFileW(name,access,share,security,disposition,flags,templateFile);const DWORD error=h==INVALID_HANDLE_VALUE?GetLastError():0;
    event("CreateFileW",p,true,h!=INVALID_HANDLE_VALUE,h!=INVALID_HANDLE_VALUE,error);
    if(h!=INVALID_HANDLE_VALUE){handles[h]=p;if(temp)stages.insert(p);}return h;
}
static BOOL size(HANDLE h,PLARGE_INTEGER result){if(!bound(h)){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}const auto ok=::GetFileSizeEx(h,result);event("GetFileSizeEx",handles.at(h),true,ok,ok,ok?0:GetLastError());return ok;}
static BOOL info(HANDLE h,LPBY_HANDLE_FILE_INFORMATION result){if(!bound(h)){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}const auto ok=::GetFileInformationByHandle(h,result);event("GetFileInformationByHandle",handles.at(h),true,ok,ok,ok?0:GetLastError());return ok;}
static BOOL read(HANDLE h,LPVOID bytes,DWORD length,LPDWORD used,LPOVERLAPPED overlap){if(!bound(h)){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}const auto ok=::ReadFile(h,bytes,length,used,overlap);event("ReadFile",handles.at(h),true,ok,ok,ok?0:GetLastError(),false,length,used?*used:0);return ok;}
static BOOL close(HANDLE h){auto i=handles.find(h);if(i==handles.end()){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}const auto path=i->second;const auto ok=::CloseHandle(h);event("CloseHandle",path,true,ok,ok,ok?0:GetLastError());if(ok)handles.erase(i);return ok;}
static BOOL write(HANDLE h,LPCVOID bytes,DWORD length,LPDWORD used,LPOVERLAPPED overlap){
    if(!bound(h)||!stages.contains(handles.at(h))){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    const bool partial=fault=="short-write";const DWORD request=partial?length/2:length;const auto ok=::WriteFile(h,bytes,request,used,overlap);
    event("WriteFile",handles.at(h),true,ok,ok,ok?0:GetLastError(),partial,length,used?*used:0);return ok;
}
static BOOL flush(HANDLE h){
    if(!bound(h)||!stages.contains(handles.at(h))){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    const auto nativeOk=::FlushFileBuffers(h);const bool inject=nativeOk&&fault=="flush-ack";const DWORD error=inject?ERROR_WRITE_FAULT:nativeOk?0:GetLastError();
    event("FlushFileBuffers",handles.at(h),true,nativeOk,nativeOk&&!inject,error,inject);if(error)SetLastError(error);return nativeOk&&!inject;
}
static BOOL remove(LPCWSTR name){
    const fs::path p(name);if(!staged(p)||!stages.contains(p)){SetLastError(ERROR_ACCESS_DENIED);event("DeleteFileW",p,false,false,false,ERROR_ACCESS_DENIED);return FALSE;}
    const auto ok=::DeleteFileW(name);event("DeleteFileW",p,true,ok,ok,ok?0:GetLastError());if(ok)stages.erase(p);return ok;
}
static BOOL move(LPCWSTR from,LPCWSTR to,DWORD flags){
    const fs::path source(from),destination(to);if(!staged(source)||!stages.contains(source)||!leaf(destination)||!same(destination,target())){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    if(fault=="force-replace"){SetLastError(ERROR_ACCESS_DENIED);event("MoveFileExW",destination,false,false,false,ERROR_ACCESS_DENIED,true);return FALSE;}
    const auto ok=::MoveFileExW(from,to,flags);event("MoveFileExW",destination,true,ok,ok,ok?0:GetLastError());if(ok){stages.erase(source);if(afterPublish){auto callback=std::move(afterPublish);afterPublish={};callback();}}return ok;
}
static BOOL replace(LPCWSTR replaced,LPCWSTR replacement,LPCWSTR backup,DWORD flags,LPVOID exclude,LPVOID reserved){
    const fs::path destination(replaced),source(replacement);if(backup||!staged(source)||!stages.contains(source)||!leaf(destination)||!same(destination,target())){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    const auto ok=::ReplaceFileW(replaced,replacement,backup,flags,exclude,reserved);event("ReplaceFileW",destination,true,ok,ok,ok?0:GetLastError());if(ok){stages.erase(source);if(afterPublish){auto callback=std::move(afterPublish);afterPublish={};callback();}}return ok;
}
static DWORD attrs(LPCWSTR name){const fs::path p(name);if(!leaf(p)||!same(p,target())){SetLastError(ERROR_ACCESS_DENIED);return INVALID_FILE_ATTRIBUTES;}const DWORD value=::GetFileAttributesW(name);event("GetFileAttributesW",p,true,value!=INVALID_FILE_ATTRIBUTES,value!=INVALID_FILE_ATTRIBUTES,value==INVALID_FILE_ATTRIBUTES?GetLastError():0);return value;}
// Independent fixture setup/probes are never substituted for consumer IO.
static void put(const std::string& bytes,const fs::path& path={}){
    const auto p=path.empty()?target():path;if(!leaf(p))throw std::runtime_error("fixture setup escaped current owned case");
    HANDLE h=::CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("fixture setup open failed");
    DWORD count=0;const bool ok=::WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size()&&::FlushFileBuffers(h);::CloseHandle(h);if(!ok)throw std::runtime_error("fixture setup write failed");
}
static std::string get(const fs::path& path={}){
    const auto p=path.empty()?target():path;if(!leaf(p))throw std::runtime_error("fixture probe escaped current owned case");
    HANDLE h=::CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("fixture probe open failed");
    std::string bytes;char data[4096];DWORD count=0;BOOL ok=TRUE;while((ok=::ReadFile(h,data,sizeof(data),&count,nullptr))&&count)bytes.append(data,count);::CloseHandle(h);if(!ok)throw std::runtime_error("fixture probe read failed");return bytes;
}
struct Hold {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Hold(DWORD share){if(!leaf(target()))throw std::runtime_error("fixture lock escaped owned case");value=::CreateFileW(target().c_str(),GENERIC_READ,share,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(value==INVALID_HANDLE_VALUE)throw std::runtime_error("fixture lock failed");}
    ~Hold(){if(value!=INVALID_HANDLE_VALUE)::CloseHandle(value);}
};
struct Deny {
    fs::path path;PSECURITY_DESCRIPTOR original=nullptr;PACL prior=nullptr,changed=nullptr;SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision=0;bool applied=false;
    Deny(const fs::path& selected,DWORD rights):path(selected){
        if(!(leaf(path)||same(path,caseRoot))||!ancestry(path))throw std::runtime_error("ACL scope escaped owned case");
        DWORD error=::GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&prior,nullptr,&original);
        if(error==ERROR_SUCCESS&&!::GetSecurityDescriptorControl(original,&control,&revision))error=GetLastError();
        BYTE sid[SECURITY_MAX_SID_SIZE]{};DWORD used=sizeof(sid);EXPLICIT_ACCESSW entry{};
        if(error==ERROR_SUCCESS&&!::CreateWellKnownSid(WinWorldSid,nullptr,sid,&used))error=GetLastError();
        entry.grfAccessPermissions=rights;entry.grfAccessMode=DENY_ACCESS;entry.grfInheritance=NO_INHERITANCE;entry.Trustee.TrusteeForm=TRUSTEE_IS_SID;entry.Trustee.ptstrName=reinterpret_cast<LPWSTR>(sid);
        if(error==ERROR_SUCCESS)error=::SetEntriesInAclW(1,&entry,prior,&changed);
        if(error==ERROR_SUCCESS)error=::SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,changed,nullptr);
        if(error!=ERROR_SUCCESS){if(changed)::LocalFree(changed);if(original)::LocalFree(original);throw std::runtime_error("owned ACL deny setup failed");}applied=true;
    }
    ~Deny(){if(applied){const DWORD flags=DACL_SECURITY_INFORMATION|((control&SE_DACL_PROTECTED)?PROTECTED_DACL_SECURITY_INFORMATION:UNPROTECTED_DACL_SECURITY_INFORMATION);if(::SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()),SE_FILE_OBJECT,flags,nullptr,nullptr,prior,nullptr)!=ERROR_SUCCESS)aclRestoreFailed=true;}if(changed)::LocalFree(changed);if(original)::LocalFree(original);}
};
static void beginRoot(){
    wchar_t image[32768]{};auto length=GetModuleFileNameW(nullptr,image,32768);if(!length||length>=32768)throw std::runtime_error("fixture image path missing");
    const fs::path exe(std::wstring(image,length));if(exe.filename()!=L"oem-vendor-stack.fixture.exe"||exe.root_name().native().size()!=2||::GetDriveTypeW(exe.root_path().c_str())!=DRIVE_FIXED||!ancestry(exe.parent_path()))throw std::runtime_error("fixture must be exact owned local executable");
    root=exe.parent_path()/L"journal-io-owned";if(!::CreateDirectoryW(root.c_str(),nullptr))throw std::runtime_error("native IO root must be fresh");
}
// Defined before the fake API macros; failure cleanup never mutates an iterated map.
static void closeOwnedHandles(){for(const auto& entry:handles)::CloseHandle(entry.first);handles.clear();}
static void beginCase(){
    if(!handles.empty())throw std::runtime_error("native reader/writer leaked owned handle");
    caseRoot=root/(L"case-"+std::to_wstring(++seq));if(!::CreateDirectoryW(caseRoot.c_str(),nullptr))throw std::runtime_error("native IO case must be fresh");
    active=true;POWER_CONTROL_DIR=caseRoot.native();events=json::array();stages.clear();fault.clear();afterPublish={};aclRestoreFailed=false;
}
static size_t count(const char* api,bool successOnly=false){size_t n=0;for(const auto& e:events)if(e["api"]==api&&e["nativeCalled"]==true&&(!successOnly||e["nativeOk"]==true))++n;return n;}
static bool error(const char* api,DWORD code){for(const auto& e:events)if(e["api"]==api&&e["nativeCalled"]==true&&e["returnedOk"]==false&&e["win32Error"]==code)return true;return false;}
}
static bool oemFixtureProductionWrite(const std::wstring&,const std::string&);
#endif

struct JournalReadLease {std::string bytes;size_t cursor=0;};
static HANDLE fakeJournalOpen(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE templateFile){
#ifdef YMCC_VENDOR_NATIVE_IO
    if(journal_native::active)return journal_native::open(name,access,share,security,disposition,flags,templateFile);
#endif
    ++journalReads;if(journalReadFails||!journalExists){SetLastError(journalReadFails?ERROR_SHARING_VIOLATION:ERROR_FILE_NOT_FOUND);return INVALID_HANDLE_VALUE;}
    return reinterpret_cast<HANDLE>(new JournalReadLease{journal});
}
static BOOL fakeJournalSize(HANDLE file,PLARGE_INTEGER size){
#ifdef YMCC_VENDOR_NATIVE_IO
    if(journal_native::active)return journal_native::size(file,size);
#endif
size->QuadPart=static_cast<LONGLONG>(reinterpret_cast<JournalReadLease*>(file)->bytes.size());return TRUE;}
static BOOL fakeJournalInfo(HANDLE file,LPBY_HANDLE_FILE_INFORMATION info){
#ifdef YMCC_VENDOR_NATIVE_IO
    if(journal_native::active)return journal_native::info(file,info);
#endif
*info={};info->dwFileAttributes=journalDirectory?FILE_ATTRIBUTE_DIRECTORY:journalReparse?FILE_ATTRIBUTE_REPARSE_POINT:FILE_ATTRIBUTE_NORMAL;return TRUE;}
static BOOL fakeJournalRead(HANDLE file,LPVOID bytes,DWORD length,LPDWORD read,LPOVERLAPPED overlap){
#ifdef YMCC_VENDOR_NATIVE_IO
    if(journal_native::active)return journal_native::read(file,bytes,length,read,overlap);
#endif
    if(journalReadFails){*read=0;SetLastError(ERROR_READ_FAULT);return FALSE;}
    auto& lease=*reinterpret_cast<JournalReadLease*>(file);*read=journalShortRead?0:static_cast<DWORD>(std::min<size_t>(length,lease.bytes.size()-lease.cursor));
    std::memcpy(bytes,lease.bytes.data()+lease.cursor,*read);lease.cursor+=*read;return TRUE;
}
static BOOL fakeJournalClose(HANDLE file){
#ifdef YMCC_VENDOR_NATIVE_IO
    if(journal_native::active)return journal_native::close(file);
#endif
delete reinterpret_cast<JournalReadLease*>(file);return TRUE;}

static std::vector<std::function<void()>> jobs;
static std::function<void()> onPersist;
static int taskQueries=0;
static int mutationCount=0,stops=0,starts=0,taskMutations=0,taskStarts=0,processCalls=0;
static ULONGLONG tick=1;
static SC_HANDLE fakeScm(LPCWSTR,LPCWSTR,DWORD){if(scmFail){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}return reinterpret_cast<SC_HANDLE>(1);}
static SC_HANDLE fakeOpen(SC_HANDLE,LPCWSTR name,DWORD){auto i=services.find(name);if(i==services.end()){SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);return nullptr;}if(i->second.deny){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}return reinterpret_cast<SC_HANDLE>(&i->second);}
static BOOL fakeClose(SC_HANDLE){return TRUE;}
static BOOL fakeConfigQuery(SC_HANDLE h,LPQUERY_SERVICE_CONFIGW p,DWORD,DWORD* n){auto& v=*reinterpret_cast<Service*>(h);*n=sizeof(QUERY_SERVICE_CONFIGW);if(v.queryFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}if(!p){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}p->dwStartType=v.type;return TRUE;}
static BOOL fakeStatus(SC_HANDLE h,SC_STATUS_TYPE,LPBYTE p,DWORD,DWORD*){auto& v=*reinterpret_cast<Service*>(h);if(v.queryFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}if(v.settleAt&&tick>=v.settleAt){v.state=v.settleState;v.settleAt=0;}reinterpret_cast<SERVICE_STATUS_PROCESS*>(p)->dwCurrentState=v.state;return TRUE;}
static BOOL fakeChange(SC_HANDLE h,DWORD,DWORD t,DWORD,LPCWSTR,LPCWSTR,LPDWORD,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR){auto& v=*reinterpret_cast<Service*>(h);if(v.configFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}v.type=t;++mutationCount;return TRUE;}
static BOOL fakeControl(SC_HANDLE h,DWORD control,LPSERVICE_STATUS){auto& v=*reinterpret_cast<Service*>(h);++stops;if(v.stopFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}if(!v.stuck){if(v.stopDelay){v.state=SERVICE_STOP_PENDING;v.settleAt=tick+v.stopDelay;v.settleState=SERVICE_STOPPED;}else v.state=control==SERVICE_CONTROL_PAUSE?SERVICE_PAUSED:SERVICE_STOPPED;}return TRUE;}
static BOOL fakeStart(SC_HANDLE h,DWORD,LPCWSTR*){auto& v=*reinterpret_cast<Service*>(h);++starts;if(v.startFail){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}v.state=SERVICE_RUNNING;return TRUE;}
static DWORD fakeAttrs(LPCWSTR path){
#ifdef YMCC_VENDOR_NATIVE_IO
    if(journal_native::active)return journal_native::attrs(path);
#endif
if(journalAttributeError){SetLastError(journalAttributeError);return INVALID_FILE_ATTRIBUTES;}if(journalExists)return journalDirectory?FILE_ATTRIBUTE_DIRECTORY:journalReparse?FILE_ATTRIBUTE_REPARSE_POINT:FILE_ATTRIBUTE_NORMAL;SetLastError(ERROR_FILE_NOT_FOUND);return INVALID_FILE_ATTRIBUTES;}
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& s){
#ifdef YMCC_VENDOR_NATIVE_IO
    if(journal_native::active){++journalWrites;return oemFixtureProductionWrite(path,s);}
#endif
++journalWrites;if(journalFail)return false;journal=s;journalExists=true;if(onPersist){auto f=std::move(onPersist);onPersist={};f();}return true;}
static std::string sgReadFile(const std::wstring&){++journalReads;return journalReadFails?std::string{}:journal;}
static bool gamepadSerialSubmit(std::function<void()> f){if(queueFail || g_exitRequested)return false;jobs.push_back(f);return true;}
#define OpenSCManagerW fakeScm
#define OpenServiceW fakeOpen
#define CloseServiceHandle fakeClose
#define QueryServiceConfigW fakeConfigQuery
#define QueryServiceStatusEx fakeStatus
#define ChangeServiceConfigW fakeChange
#define ControlService fakeControl
#define StartServiceW fakeStart
#define GetFileAttributesW fakeAttrs
#define CreateFileW fakeJournalOpen
#define GetFileSizeEx fakeJournalSize
#define GetFileInformationByHandle fakeJournalInfo
#define ReadFile fakeJournalRead
#define CloseHandle fakeJournalClose
#define GetTickCount64() tick
#define Sleep(ms) (tick += (ms))
#ifdef YMCC_VENDOR_NATIVE_IO
#define WriteFile journal_native::write
#define FlushFileBuffers journal_native::flush
#define DeleteFileW journal_native::remove
#define MoveFileExW journal_native::move
#define ReplaceFileW journal_native::replace
#endif
static void oemVendorStackRestore(const char*);
