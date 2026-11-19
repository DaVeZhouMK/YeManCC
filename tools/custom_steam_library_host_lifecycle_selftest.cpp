#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <aclapi.h>
#include <functional>
#include <atomic>
#include <string>
static std::function<DWORD(const std::wstring&, const std::wstring&)> hostMoveFault;
static std::function<void(const std::wstring&,const std::wstring&)> identityAfterMove;
static std::function<void(HANDLE)> identityAfterFlush;
static BOOL WINAPI fixtureMove(LPCWSTR from, LPCWSTR to, DWORD flags) {
    if (hostMoveFault) { const auto error = hostMoveFault(from, to); if (error) { SetLastError(error); return FALSE; } }
    const auto result=MoveFileExW(from,to,flags);if(result&&identityAfterMove)identityAfterMove(from,to);return result;
}

// Faults apply only to this consumer's explicitly selected staging file.
static std::wstring configIoTarget;
static int configIoMode=0;
static bool configIoPartial=false;
static bool configIoSelected(HANDLE handle) {
    if(configIoTarget.empty())return false;
    wchar_t name[32768]{};const auto size=GetFinalPathNameByHandleW(handle,name,32768,FILE_NAME_NORMALIZED);
    return size && size<32768 && std::wstring(name).find(configIoTarget)!=std::wstring::npos;
}
static BOOL WINAPI configFixtureWrite(HANDLE h,LPCVOID data,DWORD count,LPDWORD written,LPOVERLAPPED overlapped) {
    if(configIoMode==1 && configIoSelected(h)) {
        if(configIoPartial){SetLastError(ERROR_WRITE_FAULT);return FALSE;}
        configIoPartial=true;return WriteFile(h,data,(std::min)(count,DWORD{7}),written,overlapped);
    }
    return WriteFile(h,data,count,written,overlapped);
}
static BOOL WINAPI configFixtureFlush(HANDLE h) {
    if(configIoMode==2 && configIoSelected(h)){SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    const auto result=FlushFileBuffers(h);if(result&&identityAfterFlush)identityAfterFlush(h);return result;
}
#define WriteFile configFixtureWrite
#define FlushFileBuffers configFixtureFlush

#define MoveFileExW fixtureMove
// Count only a separate diagnostic read. CPU samples keep observation disabled.
static bool snapshotReadObservation=false;
static uint64_t snapshotReadCalls=0,snapshotReadBytes=0,snapshotManifestReads=0;
static BOOL WINAPI snapshotObservedRead(HANDLE handle,LPVOID data,DWORD count,LPDWORD read,LPOVERLAPPED over){
    if(configIoMode==3 && configIoSelected(handle)){if(read)*read=0;SetLastError(ERROR_READ_FAULT);return FALSE;}
    const auto ok=ReadFile(handle,data,count,read,over);
    if(snapshotReadObservation&&ok){
        ++snapshotReadCalls;if(read)snapshotReadBytes+=*read;
        wchar_t name[1024]{};const auto size=GetFinalPathNameByHandleW(handle,name,1024,FILE_NAME_NORMALIZED);
        if(size&&size<1024&&std::wstring(name,size).ends_with(L"\\manifest.json"))++snapshotManifestReads;
    }return ok;
}
#define ReadFile snapshotObservedRead
#include "custom_steam_library_storage_isolation.h"
// Selected Host admission tests never launch a process, shell or HTTP session.
#include <winhttp.h>
#include <shellapi.h>
static bool admissionFixtureOnly=false;
static std::wstring manualEnrichmentFixtureWorker;
static std::atomic<int> admissionProcesses{0},admissionNetwork{0},admissionShell{0};
static std::atomic<uint64_t> admissionOwnedProcesses{0};
static BOOL WINAPI admissionCreateProcess(LPCWSTR app,LPWSTR cmd,LPSECURITY_ATTRIBUTES pa,LPSECURITY_ATTRIBUTES ta,BOOL inherit,DWORD flags,LPVOID env,LPCWSTR dir,LPSTARTUPINFOW start,LPPROCESS_INFORMATION info) {
    if(admissionFixtureOnly&&!(app&&!manualEnrichmentFixtureWorker.empty()&&_wcsicmp(app,manualEnrichmentFixtureWorker.c_str())==0)){++admissionProcesses;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    if(admissionFixtureOnly&&app&&!manualEnrichmentFixtureWorker.empty()&&_wcsicmp(app,manualEnrichmentFixtureWorker.c_str())==0)++admissionOwnedProcesses;
    return CreateProcessW(app,cmd,pa,ta,inherit,flags,env,dir,start,info);
}
static HINTERNET WINAPI admissionHttpOpen(LPCWSTR agent,DWORD access,LPCWSTR proxy,LPCWSTR bypass,DWORD flags) {
    if(admissionFixtureOnly){++admissionNetwork;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
    return WinHttpOpen(agent,access,proxy,bypass,flags);
}
static HINSTANCE WINAPI admissionShellExecute(HWND w,LPCWSTR operation,LPCWSTR file,LPCWSTR parameters,LPCWSTR dir,INT show) {
    if(admissionFixtureOnly){++admissionShell;return reinterpret_cast<HINSTANCE>(INT_PTR{SE_ERR_ACCESSDENIED});}
    return ShellExecuteW(w,operation,file,parameters,dir,show);
}
#define CreateProcessW admissionCreateProcess
#define WinHttpOpen admissionHttpOpen
#define ShellExecuteW admissionShellExecute

// Activity cases use a hidden owned Win32 window and real window timers.
// Focus, visibility and XInput are local ports; no real window is activated.
#include <xinput.h>
static bool activityOnly=false,activityVisible=true,activityIconic=false,activityDisconnected=false;
static HWND activityWindow=nullptr,activityForeground=nullptr;
static unsigned activityStarts=0,activityStops=0,activityReads=0,activityForegroundReads=0,activityDefocusOnRead=0;
static XINPUT_STATE activityState{};
static HWND WINAPI activityGetForeground(){if(!activityOnly)return GetForegroundWindow();++activityForegroundReads;return activityDefocusOnRead&&activityForegroundReads>=activityDefocusOnRead?nullptr:activityForeground;}
static BOOL WINAPI activityIsVisible(HWND window){return activityOnly&&window==activityWindow?activityVisible:IsWindowVisible(window);}
static BOOL WINAPI activityIsIconic(HWND window){return activityOnly&&window==activityWindow?activityIconic:IsIconic(window);}
static UINT_PTR WINAPI activitySetTimer(HWND window,UINT_PTR id,UINT interval,TIMERPROC callback){if(activityOnly&&window==activityWindow&&id==0x52)++activityStarts;return SetTimer(window,id,interval,callback);}
static BOOL WINAPI activityKillTimer(HWND window,UINT_PTR id){if(activityOnly&&window==activityWindow&&id==0x52)++activityStops;return KillTimer(window,id);}
static DWORD WINAPI activityXInput(DWORD user,XINPUT_STATE* state){if(!activityOnly)return XInputGetState(user,state);++activityReads;if(activityDisconnected)return ERROR_DEVICE_NOT_CONNECTED;*state=activityState;return ERROR_SUCCESS;}
#define GetForegroundWindow activityGetForeground
#define IsWindowVisible activityIsVisible
#define IsIconic activityIsIconic
#define SetTimer activitySetTimer
#define KillTimer activityKillTimer
#define XInputGetState activityXInput
static BOOL WINAPI manualReplyPostMessage(HWND,UINT,WPARAM,LPARAM);
#define PostMessageW manualReplyPostMessage
// Isolated current-account registry port; never writes the user's registry.
static bool activeAccountFixture=false,activeAccountWrongType=false;
static DWORD activeAccountUser=0;
static uint64_t activeAccountReads=0;
static std::wstring activeAccountRoot;
static LSTATUS WINAPI activeAccountRegGetValue(HKEY root,LPCWSTR key,LPCWSTR name,DWORD flags,LPDWORD type,PVOID data,LPDWORD size){
    if(!activeAccountFixture)return RegGetValueW(root,key,name,flags,type,data,size);
    ++activeAccountReads;
    if(root!=HKEY_CURRENT_USER||!key||!name||!size)return ERROR_FILE_NOT_FOUND;
    if(_wcsicmp(key,L"Software\\Valve\\Steam\\ActiveProcess")==0&&_wcsicmp(name,L"ActiveUser")==0){
        if(activeAccountWrongType)return ERROR_UNSUPPORTED_TYPE;
        if(flags!=RRF_RT_REG_DWORD||!data||*size<sizeof(DWORD))return ERROR_MORE_DATA;
        memcpy(data,&activeAccountUser,sizeof(DWORD));*size=sizeof(DWORD);if(type)*type=REG_DWORD;return ERROR_SUCCESS;
    }
    if(_wcsicmp(key,L"Software\\Valve\\Steam")==0&&_wcsicmp(name,L"SteamPath")==0&&!activeAccountRoot.empty()){
        const auto bytes=static_cast<DWORD>((activeAccountRoot.size()+1)*sizeof(wchar_t));
        if(flags!=RRF_RT_REG_SZ||!data||*size<bytes)return ERROR_MORE_DATA;
        memcpy(data,activeAccountRoot.c_str(),bytes);*size=bytes;if(type)*type=REG_SZ;return ERROR_SUCCESS;
    }
    return ERROR_FILE_NOT_FOUND;
}
#define RegGetValueW activeAccountRegGetValue
#include <tlhelp32.h>
// Win32/NtQuery ports only: product detection, shared Store and writer stay real.
#include <winternl.h>
#include <cstring>
#include <cstdint>
#include <map>
#include <mutex>
enum class NativeGridProcessMode { Absent, Present, QueryFailure, SnapshotFailure, FirstFailure, PartialFailure };
static bool nativeGridFixture=false;
static NativeGridProcessMode nativeGridProcessMode=NativeGridProcessMode::Absent;
struct NativeGridProcessCalls {
    uint64_t query=0,snapshot=0,first=0,next=0,opened=0,closed=0,unexpected=0,openProcess=0,terminate=0;
    ULONGLONG lastQueryAt=0;
};
static NativeGridProcessCalls nativeGridCalls;
struct NativeGridSnapshot { NativeGridProcessMode mode; bool first=false; };
static std::map<HANDLE,NativeGridSnapshot> nativeGridSnapshots;
static std::mutex nativeGridSnapshotMutex;
using NativeGridQuery=NTSTATUS (NTAPI*)(SYSTEM_INFORMATION_CLASS,PVOID,ULONG,PULONG);
static NTSTATUS NTAPI nativeGridQuery(SYSTEM_INFORMATION_CLASS kind,PVOID data,ULONG size,PULONG returned) {
    if(!nativeGridFixture) {
        static const auto actual=reinterpret_cast<NativeGridQuery>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQuerySystemInformation"));
        return actual?actual(kind,data,size,returned):static_cast<NTSTATUS>(0xC0000002L);
    }
    ++nativeGridCalls.query;nativeGridCalls.lastQueryAt=GetTickCount64();if(returned)*returned=0;
    if(static_cast<ULONG>(kind)!=252){++nativeGridCalls.unexpected;return static_cast<NTSTATUS>(0xC0000003L);}
    // Only unsupported class enters the real Store's Toolhelp fallback.
    return static_cast<NTSTATUS>(nativeGridProcessMode==NativeGridProcessMode::QueryFailure?0xC0000022L:0xC0000003L);
}
static FARPROC WINAPI nativeGridGetProcAddress(HMODULE module,LPCSTR name) {
    // Return the dispatcher even while inactive: resolveQuery caches its first result.
    if(reinterpret_cast<ULONG_PTR>(name)>0xffff&&std::strcmp(name,"NtQuerySystemInformation")==0&&module==GetModuleHandleW(L"ntdll.dll"))
        return reinterpret_cast<FARPROC>(nativeGridQuery);
    return GetProcAddress(module,name);
}
static HANDLE WINAPI nativeGridCreateSnapshot(DWORD flags,DWORD pid) {
    if(!nativeGridFixture)return CreateToolhelp32Snapshot(flags,pid);
    ++nativeGridCalls.snapshot;
    if(flags!=TH32CS_SNAPPROCESS||pid){++nativeGridCalls.unexpected;SetLastError(ERROR_INVALID_PARAMETER);return INVALID_HANDLE_VALUE;}
    if(nativeGridProcessMode==NativeGridProcessMode::SnapshotFailure){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
    HANDLE handle=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!handle)return INVALID_HANDLE_VALUE;
    try{std::lock_guard<std::mutex> lock(nativeGridSnapshotMutex);nativeGridSnapshots.emplace(handle,NativeGridSnapshot{nativeGridProcessMode});}
    catch(...){CloseHandle(handle);throw;}
    ++nativeGridCalls.opened;SetLastError(ERROR_SUCCESS);return handle;
}
static BOOL WINAPI nativeGridProcessFirst(HANDLE snapshot,LPPROCESSENTRY32W entry) {
    std::lock_guard<std::mutex> lock(nativeGridSnapshotMutex);
    const auto found=nativeGridSnapshots.find(snapshot);
    if(found==nativeGridSnapshots.end()) {
        if(!nativeGridFixture)return Process32FirstW(snapshot,entry);
        ++nativeGridCalls.unexpected;SetLastError(ERROR_INVALID_HANDLE);return FALSE;
    }
    ++nativeGridCalls.first;
    if(!entry||entry->dwSize!=sizeof(PROCESSENTRY32W)){++nativeGridCalls.unexpected;SetLastError(ERROR_BAD_LENGTH);return FALSE;}
    auto& state=found->second;state.first=true;
    if(state.mode==NativeGridProcessMode::FirstFailure){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    if(state.mode==NativeGridProcessMode::Absent){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
    // Inert PID, never opened. Both complete and partial prefixes see Steam.
    *entry=PROCESSENTRY32W{};entry->dwSize=sizeof(*entry);entry->th32ProcessID=0x7ffffffe;
    wcscpy_s(entry->szExeFile,L"steam.exe");SetLastError(ERROR_SUCCESS);return TRUE;
}
static BOOL WINAPI nativeGridProcessNext(HANDLE snapshot,LPPROCESSENTRY32W entry) {
    std::lock_guard<std::mutex> lock(nativeGridSnapshotMutex);
    const auto found=nativeGridSnapshots.find(snapshot);
    if(found==nativeGridSnapshots.end()) {
        if(!nativeGridFixture)return Process32NextW(snapshot,entry);
        ++nativeGridCalls.unexpected;SetLastError(ERROR_INVALID_HANDLE);return FALSE;
    }
    ++nativeGridCalls.next;
    if(!found->second.first||!entry||entry->dwSize!=sizeof(PROCESSENTRY32W)){++nativeGridCalls.unexpected;SetLastError(ERROR_BAD_LENGTH);return FALSE;}
    SetLastError(found->second.mode==NativeGridProcessMode::PartialFailure?ERROR_ACCESS_DENIED:ERROR_NO_MORE_FILES);return FALSE;
}
static BOOL WINAPI nativeGridCloseHandle(HANDLE handle) {
    const auto error=GetLastError();bool owned=false;
    {std::lock_guard<std::mutex> lock(nativeGridSnapshotMutex);owned=nativeGridSnapshots.contains(handle);}
    const auto result=CloseHandle(handle);
    if(owned&&result){std::lock_guard<std::mutex> lock(nativeGridSnapshotMutex);nativeGridSnapshots.erase(handle);++nativeGridCalls.closed;}
    if(result)SetLastError(error);return result;
}
static HANDLE WINAPI nativeGridOpenProcess(DWORD access,BOOL inherit,DWORD pid) {
    if(nativeGridFixture){++nativeGridCalls.openProcess;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
    return OpenProcess(access,inherit,pid);
}
static BOOL WINAPI nativeGridTerminateProcess(HANDLE process,UINT code) {
    if(nativeGridFixture){++nativeGridCalls.terminate;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return TerminateProcess(process,code);
}
#define GetProcAddress nativeGridGetProcAddress
#define CreateToolhelp32Snapshot nativeGridCreateSnapshot
#define Process32FirstW nativeGridProcessFirst
#define Process32NextW nativeGridProcessNext
#define CloseHandle nativeGridCloseHandle
#define OpenProcess nativeGridOpenProcess
#define TerminateProcess nativeGridTerminateProcess
static bool presentationProbe=false;
static unsigned presentationShow=0,presentationForeground=0,presentationActive=0,presentationKeyboard=0;
static BOOL WINAPI presentationShowWindow(HWND window,int command){if(presentationProbe&&window==activityWindow){++presentationShow;activityVisible=command!=SW_HIDE;activityIconic=command==SW_MINIMIZE;return TRUE;}return ShowWindow(window,command);}
static BOOL WINAPI presentationForegroundWindow(HWND window){if(presentationProbe){++presentationForeground;activityForeground=window;return TRUE;}return SetForegroundWindow(window);}
static HWND WINAPI presentationActiveWindow(HWND window){if(presentationProbe){++presentationActive;return window;}return SetActiveWindow(window);}
static HWND WINAPI presentationFocusWindow(HWND window){if(presentationProbe){++presentationKeyboard;return window;}return SetFocus(window);}
#define ShowWindow presentationShowWindow
#define SetForegroundWindow presentationForegroundWindow
#define SetActiveWindow presentationActiveWindow
#define SetFocus presentationFocusWindow
#define wWinMain unusedHostMain
#include "../native/custom-steam-library/custom_steam_library_plan_receipt.h"
#include "../native/steam_process_discovery.h"
#define read originalLifecycleSteamInformationRead
#include "../native/steam_information_client.h"
#undef read
static bool ownedAddPartialSteamFacts=false;
namespace ymcc::steaminfo {
inline Facts read(std::uint32_t mask) noexcept {
    if(!ownedAddPartialSteamFacts)return originalLifecycleSteamInformationRead(mask);
    Facts facts;facts.available=true;facts.presence=0;
    facts.root=utf8(activeAccountRoot);facts.rootError=ERROR_SUCCESS;
    facts.uiAccountKnown=true;facts.uiAccount=activeAccountUser;
    return facts;
}
}
#include "../native/custom-steam-library/workspace_host.cpp"
#undef wWinMain
#undef ShowWindow
#undef SetForegroundWindow
#undef SetActiveWindow
#undef SetFocus
#undef GetProcAddress
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW
#undef CloseHandle
#undef OpenProcess
#undef TerminateProcess
#undef RegGetValueW
#undef PostMessageW
static std::atomic<bool> manualReplyCapture{false};
static std::mutex manualReplyEventMutex;
static json manualReplyEvents=json::array();
static std::function<void(const json&)> pipelineFixtureNotice;
static BOOL WINAPI manualReplyPostMessage(HWND window,UINT message,WPARAM word,LPARAM value) {
    if(manualReplyCapture.load()&&message==WM_WORKSPACE_RESULT){
        std::unique_ptr<AsyncResult> owned(reinterpret_cast<AsyncResult*>(value));
        if(pipelineFixtureNotice)pipelineFixtureNotice(owned->message);
        std::lock_guard<std::mutex> lock(manualReplyEventMutex);manualReplyEvents.push_back(owned->message);return TRUE;
    }
    return PostMessageW(window,message,word,value);
}
#undef GetForegroundWindow
#undef IsWindowVisible
#undef IsIconic
#undef SetTimer
#undef KillTimer
#undef XInputGetState
#undef CreateProcessW
#undef WinHttpOpen
#undef ShellExecuteW
#undef MoveFileExW
#undef WriteFile
#undef FlushFileBuffers
#undef ReadFile
#include <iostream>

// This ACL is applied only to an isolated case file and restored before return.
static void withConfigReadDenied(const fs::path& file,const std::function<void()>& action) {
    auto name=custom_steam_library::ioPath(file).wstring();
    PSECURITY_DESCRIPTOR descriptor=nullptr;PACL previous=nullptr,denied=nullptr;
    if(GetNamedSecurityInfoW(name.data(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,nullptr,nullptr,&previous,nullptr,&descriptor)!=ERROR_SUCCESS)
        throw std::runtime_error("Cannot inspect isolated file ACL");
    SECURITY_DESCRIPTOR_CONTROL control{};DWORD revision=0;GetSecurityDescriptorControl(descriptor,&control,&revision);
    std::array<unsigned char,SECURITY_MAX_SID_SIZE> everyone{};DWORD sidSize=static_cast<DWORD>(everyone.size());
    if(!CreateWellKnownSid(WinWorldSid,nullptr,everyone.data(),&sidSize)){LocalFree(descriptor);throw std::runtime_error("Cannot create isolated ACL SID");}
    EXPLICIT_ACCESSW entry{};entry.grfAccessPermissions=FILE_READ_DATA;entry.grfAccessMode=DENY_ACCESS;entry.grfInheritance=NO_INHERITANCE;
    entry.Trustee.TrusteeForm=TRUSTEE_IS_SID;entry.Trustee.TrusteeType=TRUSTEE_IS_WELL_KNOWN_GROUP;entry.Trustee.ptstrName=reinterpret_cast<LPWSTR>(everyone.data());
    if(SetEntriesInAclW(1,&entry,previous,&denied)!=ERROR_SUCCESS){LocalFree(descriptor);throw std::runtime_error("Cannot build isolated deny ACL");}
    const auto applied=SetNamedSecurityInfoW(name.data(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,nullptr,nullptr,denied,nullptr);
    if(applied!=ERROR_SUCCESS){LocalFree(denied);LocalFree(descriptor);throw std::runtime_error("Cannot apply isolated deny ACL");}
    auto restore=[&]{const auto result=SetNamedSecurityInfoW(name.data(),SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|((control&SE_DACL_PROTECTED)?PROTECTED_DACL_SECURITY_INFORMATION:UNPROTECTED_DACL_SECURITY_INFORMATION),nullptr,nullptr,previous,nullptr);LocalFree(denied);LocalFree(descriptor);return result;};
    try{action();}catch(...){restore();throw;}
    if(restore()!=ERROR_SUCCESS)throw std::runtime_error("Cannot restore isolated file ACL");
}

static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
// Both existing grid cases enter this scope before their first direct writer.
static void nativeGridSelect(NativeGridProcessMode mode,bool invalidate=true) {
    nativeGridProcessMode=mode;if(invalidate)ymcc::processInventory::invalidate();
}
struct NativeGridProcessScope {
    bool active=nativeGridFixture,admission=admissionFixtureOnly;
    NativeGridProcessMode mode=nativeGridProcessMode;
    std::wstring worker=manualEnrichmentFixtureWorker;
    NativeGridProcessScope() {
        check(nativeGridSnapshots.empty(),"Previous controlled snapshot leaked");
        nativeGridCalls={};nativeGridFixture=true;admissionFixtureOnly=true;manualEnrichmentFixtureWorker.clear();
        nativeGridSelect(NativeGridProcessMode::Absent);
    }
    ~NativeGridProcessScope() noexcept {
        ymcc::processInventory::invalidate();
        {std::lock_guard<std::mutex> lock(nativeGridSnapshotMutex);for(const auto& [h,state]:nativeGridSnapshots){(void)state;CloseHandle(h);}nativeGridSnapshots.clear();}
        nativeGridFixture=active;nativeGridProcessMode=mode;admissionFixtureOnly=admission;manualEnrichmentFixtureWorker.swap(worker);
    }
};
static void mustFail(const std::function<void()>& action) { bool failed=false; try { action(); } catch (const std::exception&) { failed=true; } check(failed,"Expected a controlled error"); }
static std::string bytes(const fs::path& path) { std::ifstream in(custom_steam_library::ioPath(path),std::ios::binary); return {std::istreambuf_iterator<char>(in),{}}; }
static void text(const fs::path& path, const std::string& value) { fs::create_directories(custom_steam_library::ioPath(path.parent_path())); std::ofstream output(custom_steam_library::ioPath(path),std::ios::binary);output<<value;output.flush();if(!output)throw std::runtime_error("Cannot write owned fixture bytes"); }
static void trustedAdmissionFixtureRoot(const fs::path& root) {
    text(root/L"data-root.json",json{{"schemaVersion",2},{"dataRoot",toUtf8(root.wstring())},{"createdAt",uint64_t{1}}}.dump());
}
static size_t temporaries(const fs::path& root) { size_t count=0; for(const auto& e:fs::recursive_directory_iterator(custom_steam_library::ioPath(root,true))) if(e.path().filename().wstring().find(L".tmp-")!=std::wstring::npos) ++count; return count; }
static fs::path selfExecutable() { std::wstring p(32768,L'\0'); p.resize(GetModuleFileNameW(nullptr,p.data(),static_cast<DWORD>(p.size()))); return p; }
template<class Path> static fs::path admissionBrowserPath(const Path& path) {
    if constexpr(requires {hostBrowserDataRoot(path);})return hostBrowserDataRoot(path);
    else return custom_steam_library::webviewUserDataRoot(path);
}
static int helper(const std::wstring& mode) {
    if(mode==L"empty") return 0;
    if(mode==L"nonzero") { std::cout<<"fixture error"; return 23; }
    if(mode==L"invalid-output") { const char bad[]={char(0xff),char(0xfe),0,1}; std::cout.write(bad,sizeof(bad)); return 0; }
    if(mode==L"sleep" || mode==L"pipe-child") { Sleep(mode==L"sleep"?30000:2000); return 0; }
    if(mode==L"flood" || mode==L"continuous-flood") {
        const std::string block(8192,'X');DWORD wrote=0;
        for(int i=0;mode==L"continuous-flood"||i<800;++i)
            if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),block.data(),static_cast<DWORD>(block.size()),&wrote,nullptr))return 9;
        return 0;
    }
    if(mode==L"hold-pipe") {
        const auto exe=selfExecutable(); const auto cmd=quoteArgument(exe.wstring())+L" --fixture-worker pipe-child";
        std::vector<wchar_t> c(cmd.begin(),cmd.end());c.push_back(0);STARTUPINFOW si{sizeof(si)};
        si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION pi{};check(CreateProcessW(exe.c_str(),c.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi),"Could not start fixture descendant");
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);std::cout<<"parent done";return 0;
    }
    return 99;
}
struct SnapshotCacheFixture{fs::path root,png;json cache;size_t count=0;};
static SnapshotCacheFixture snapshotCacheFixture(const fs::path& root,size_t count){
    g_labRoot=root;g_dataRoot=root/L"data";trustedAdmissionFixtureRoot(g_dataRoot);
    const auto steam=root/L"Steam",shortcuts=steam/L"userdata"/L"1001"/L"config"/L"shortcuts.vdf";
    g_steamTarget={{"accountId","1001"},{"steamRoot",toUtf8(steam.wstring())},{"shortcutsVdf",toUtf8(shortcuts.wstring())}};
    const auto png=g_dataRoot/L"cached.png";const unsigned char pixels[]={0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,0x0d,0x49,0x44,0x41,0x54,0x08,0x1d,0x63,0xf8,0xcf,0xc0,0xf0,0x1f,0x00,0x05,0x80,0x02,0x3f,0x49,0xc2,0xdb,0xb4,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};text(png,std::string(reinterpret_cast<const char*>(pixels),sizeof(pixels)));
    json games=json::array(),items=json::array(),overrides={{"schemaVersion",1},{"items",json::object()},{"customNote","keep original"}};
    for(size_t i=0;i<count;++i){
        const auto folder=root/L"Games"/std::to_wstring(i),exe=folder/L"game.exe",manifest=g_dataRoot/L"artwork"/std::to_wstring(i)/L"manifest.json";text(exe,"owned fixture, never executed");
        games.push_back({{"gameDirectory",toUtf8(folder.wstring())},{"primaryExecutable",toUtf8(exe.wstring())},{"directoryName","Cached "+std::to_string(i)},{"status","ready"}});
        json assets=json::array();for(const auto* type:{"tall","long","hero"})assets.push_back({{"type",type},{"ok",true},{"portableFile",toUtf8(png.wstring())},{"provider","local-fixture"}});
        writeJsonWithLocalBackup(manifest,{{"exe",toUtf8(exe.wstring())},{"match",json::object()},{"artwork",assets}},false);
        items.push_back({{"gameDirectory",toUtf8(folder.wstring())},{"primaryExecutable",toUtf8(exe.wstring())},{"status","added-to-steam"},{"formalName","Cached "+std::to_string(i)},{"artworkManifest",toUtf8(manifest.wstring())}});
        overrides["items"][toUtf8(normalizedPathKey(exe))]={{"name","Saved "+std::to_string(i)},{"wallpaper",{{"file",toUtf8(png.wstring())}}}};
    }
    writeJsonWithLocalBackup(g_dataRoot/L"config"/L"library-config.json",{{"schemaVersion",1},{"roots",json::array()},{"manualBuckets",json::object()}},false);
    writeJsonWithLocalBackup(g_dataRoot/L"config"/L"manual-overrides.json",overrides,false);
    writeJsonWithLocalBackup(g_dataRoot/L"state"/L"library-scan.json",{{"schemaVersion",1},{"games",games},{"summary",{{"ready",count}}}},false);
    writeJsonWithLocalBackup(g_dataRoot/L"state"/L"steam-accounts.json",{{"schemaVersion",1},{"accounts",json::array({{{"accountId","1001"},{"steamRoot",toUtf8(steam.wstring())},{"shortcutsVdf",toUtf8(shortcuts.wstring())}}})}},false);
    json cache={{"schemaVersion",1},{"mode","steam-library-add-plan"},{"libraryState",toUtf8((g_dataRoot/L"state"/L"library-scan.json").wstring())},{"accountId","1001"},{"shortcutsVdf",toUtf8(shortcuts.wstring())},{"summary",{{"alreadyInSteam",count}}},{"items",items},{"customNote","cached display only"}};
    writeJsonWithLocalBackup(workspacePlanPath(),cache,false);return {root,png,cache,count};
}
static void checkCachedSnapshot(const json& value,const SnapshotCacheFixture& f,bool cached,const std::string& source={}){
    check(value["library"]["games"].size()==f.count&&value["steamPlan"]["items"].size()==f.count,"Snapshot lost current inventory or plan rows");
    check(value["manualOverrides"].value("customNote",std::string{})=="keep original","Snapshot lost unknown user fields");
    for(const auto& [key,item]:value["manualOverrides"]["items"].items())check(item["wallpaper"].value("available",false),"Cached manual wallpaper disappeared");
    for(const auto& item:value["steamPlan"]["items"]){
        check(item["artworkPreview"]["hero"].value("portableFile",std::string{})==toUtf8(f.png.wstring()),"Cached static artwork preview changed");
        const auto status=jsonStringOr(item,"status");check(cached?status=="added-to-steam":status!="added-to-steam"&&status!="already-in-steam","Cache ownership or fallback identity changed");
    }
    check(jsonStringOr(value,"steamPlanSource")== (source.empty()?(cached?toUtf8(workspacePlanPath().wstring()):"local-scrape-fallback"):source),"Snapshot chose a wrong cache/fallback source");
}
static json observedSnapshotRead(const SnapshotCacheFixture& f,bool cached=true){
    snapshotReadCalls=snapshotReadBytes=snapshotManifestReads=0;snapshotReadObservation=true;json value;
    try{value=workspaceSnapshot(false);}catch(...){snapshotReadObservation=false;throw;}snapshotReadObservation=false;checkCachedSnapshot(value,f,cached);
    return {{"readCalls",snapshotReadCalls},{"readBytes",snapshotReadBytes},{"manifestReads",snapshotManifestReads},{"games",f.count}};
}
static uint64_t snapshotFileTime(FILETIME value){ULARGE_INTEGER n{};n.LowPart=value.dwLowDateTime;n.HighPart=value.dwHighDateTime;return n.QuadPart;}
static json measuredNativeWork(unsigned iterations,const std::function<size_t()>& action){
    FILETIME created{},exited{},kernelA{},userA{},kernelB{},userB{};ULONG64 cyclesA=0,cyclesB=0;LARGE_INTEGER a{},b{},frequency{};IO_COUNTERS ioA{},ioB{};
    check(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernelA,&userA)&&QueryProcessCycleTime(GetCurrentProcess(),&cyclesA)&&GetProcessIoCounters(GetCurrentProcess(),&ioA)&&QueryPerformanceFrequency(&frequency)&&QueryPerformanceCounter(&a),"Cannot sample owned native process");
    size_t rows=0;for(unsigned i=0;i<iterations;++i)rows+=action();
    check(QueryPerformanceCounter(&b)&&GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernelB,&userB)&&QueryProcessCycleTime(GetCurrentProcess(),&cyclesB)&&GetProcessIoCounters(GetCurrentProcess(),&ioB),"Cannot finish owned native sample");
    check(!snapshotReadObservation,"CPU sample ran with read path observer");
    return {{"iterations",iterations},{"rows",rows},{"cpuMs",double(snapshotFileTime(kernelB)+snapshotFileTime(userB)-snapshotFileTime(kernelA)-snapshotFileTime(userA))/10000.0},{"cycles",cyclesB-cyclesA},{"wallMs",double(b.QuadPart-a.QuadPart)*1000.0/double(frequency.QuadPart)},{"readOperations",ioB.ReadOperationCount-ioA.ReadOperationCount},{"readTransferBytes",ioB.ReadTransferCount-ioA.ReadTransferCount},{"observerEnabledDuringCpu",false}};
}
static json measuredSnapshotWork(const SnapshotCacheFixture& f,unsigned iterations,bool cached=true){
    checkCachedSnapshot(workspaceSnapshot(false),f,cached); // Warm identical fixture files before measuring.
    const auto work=measuredNativeWork(iterations,[&]{const auto value=workspaceSnapshot(false);return value["steamPlan"]["items"].size();});
    check(work["rows"].get<size_t>()==f.count*iterations&&!snapshotReadObservation,"Native benchmark did not consume its fixed cached workload");checkCachedSnapshot(workspaceSnapshot(false),f,cached);
    return work;
}

static void historyFixtureStamp(const fs::path& path,unsigned generation){
    HANDLE file=CreateFileW(custom_steam_library::ioPath(path).c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);check(file!=INVALID_HANDLE_VALUE,"Cannot stamp owned history input");ULARGE_INTEGER ticks{};ticks.QuadPart=133000000000000000ull+uint64_t(generation)*10000000ull;const FILETIME value{ticks.LowPart,ticks.HighPart};const auto ok=SetFileTime(file,nullptr,nullptr,&value);CloseHandle(file);check(ok,"Cannot set fixed history file order");
}
static void seedSnapshotHistory(const SnapshotCacheFixture& f,unsigned rounds){
    for(unsigned round=0;round<rounds;++round)for(size_t i=0;i<f.count;++i){const auto exe=jsonStringOr(f.cache["items"][i],"primaryExecutable");const auto failure=round+1==rounds?g_dataRoot/L"state"/L"failures"/(std::to_wstring(i)+L".json"):workspaceJobsRoot()/L"library-batch-identification"/L"failures"/(std::to_wstring(round)+L"-"+std::to_wstring(i)+L".json");json candidates=json::array();for(int n=0;n<3;++n)candidates.push_back({{"name",(round+1==rounds?"Newest ":"Older ")+std::to_string(i)+"-"+std::to_string(n)},{"steamAppId",1000+int(i)},{"provider","steam"},{"score",90-n}});text(failure,json{{"executable",exe},{"manualReviewCandidates",candidates},{"customNote","keep original failure"}}.dump());historyFixtureStamp(failure,round);
        const auto manifest=workspaceJobsRoot()/L"library-batch-identification"/(L"round-"+std::to_wstring(round))/(L"game-"+std::to_wstring(i))/L"manifest.json";text(manifest,json{{"exe",exe},{"match",{{"formalName","Historical "+std::to_string(i)},{"igdbId",9000+int(i)},{"primaryProvider","playnite-igdb"}}}}.dump());historyFixtureStamp(manifest,round);
    }
}

struct RefreshNoticeWindow {
    HWND window=nullptr;
    RefreshNoticeWindow(){window=CreateWindowExW(0,L"STATIC",L"owned refresh notice",WS_POPUP,0,0,10,10,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(window!=nullptr,"Cannot create owned notice window");g_window=window;retireWorkspaceRefreshProtocol();}
    ~RefreshNoticeWindow(){retireWorkspaceRefreshProtocol();stopWorkspaceResultPosting(window);g_window=nullptr;DestroyWindow(window);g_shuttingDown=false;}
    json take(){MSG m{};check(PeekMessageW(&m,window,WM_WORKSPACE_RESULT,WM_WORKSPACE_RESULT,PM_REMOVE),"No native refresh notice queued");std::unique_ptr<AsyncResult> result(reinterpret_cast<AsyncResult*>(m.lParam));check(bool(result),"Null queued refresh notice");return std::move(result->message);}
    bool empty(){MSG m{};return !PeekMessageW(&m,window,WM_WORKSPACE_RESULT,WM_WORKSPACE_RESULT,PM_NOREMOVE);}
};

template<class Manual> static bool requestOwnedScrapeFollowup(Manual manual){
    if constexpr(requires{startScrapeQueue(manual,true);})return startScrapeQueue(manual,true);
    else return startScrapeQueue(manual);
}
// End-to-end queue worker: no network, Steam discovery or user-data access.
// Every subprocess is this fixture EXE; paths stay in owned Build/Evidence.
static int queueFixtureWorker(int argc, wchar_t** argv) {
    wchar_t value[32768]{};
    const DWORD length=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",value,32768);
    if(!length || length>=32768) return 98;
    const auto data=fs::absolute(value);
    const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
    const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
    check(normalizedPathKey(data).starts_with(normalizedPathKey(build)+L"\\")||
        normalizedPathKey(data).starts_with(normalizedPathKey(evidence)+L"\\"),"Queue fixture escaped owned Build/Evidence");
    g_dataRoot=data;admissionFixtureOnly=true;
    auto has=[&](const wchar_t* flag){for(int i=1;i<argc;++i)if(std::wstring(argv[i])==flag)return true;return false;};
    auto argument=[&](const wchar_t* flag){for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==flag)return fs::path(argv[i+1]);return fs::path{};};
    auto output=argument(L"--output");
    const auto removal=readJson(data/L"state"/L"removal-scenario.json",json::object());
    if(has(L"--remove-library-game")){
        check(!removal.empty()&&samePath(argument(L"--game-dir"),fs::path(toWide(removal.at("directory").get<std::string>()))),"Unexpected queue removal request");
        if(removal.value("reject",false)){std::cerr<<"owned queue removal rejected";return 35;}
        const auto file=data/L"config"/L"manual-overrides.json";auto overrides=readJson(file,json::object());
        overrides["items"].erase(removal.at("key").get<std::string>());writeJsonWithLocalBackup(file,overrides);
        const auto inventory=data/L"state"/L"library-scan.json";auto library=readJson(inventory,json::object());json games=json::array();
        for(const auto& row:jsonArrayOr(library,"games"))if(!samePath(fs::path(toWide(jsonStringOr(row,"gameDirectory"))),argument(L"--game-dir")))games.push_back(row);
        library["games"]=games;writeJsonWithLocalBackup(inventory,library);std::cout<<"owned queue removal confirmed";return 0;
    }
    if(has(L"--scan-library")){check(!removal.empty(),"Unowned scan port");std::cerr<<"owned queue rescan failure";return 36;}
    if(has(L"--list-steam-accounts")) {
        check(normalizedPathKey(output).starts_with(normalizedPathKey(data)+L"\\"),"Accounts output escaped fixture");
        const json report={{"schemaVersion",1},{"accounts",json::array()},{"steamStatus","installed-no-account"}};
        writeJsonWithLocalBackup(output,report,false);
        std::cout<<"ACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_STATUS: current\nSTEAM_ACCOUNTS_JSON: "<<report.dump()<<"\n";return 0;
    }
    if(has(L"--plan-add-library-to-steam")) {
        check(normalizedPathKey(output).starts_with(normalizedPathKey(data)+L"\\"),"Plan output escaped fixture");
        json snapshot={{"library",readJson(data/L"state"/L"library-scan.json",json::object())},
            {"manualOverrides",readJson(data/L"config"/L"manual-overrides.json",json::object())}};
        if(!removal.empty())attachResolvedIdentityCache(snapshot);
        auto plan=buildLocalSteamPlanSnapshot(snapshot,readJson(data/L"state"/L"fixture-plan.json",json::object()));
        writeJsonWithLocalBackup(output,plan,false);
        std::cout<<"STEAM_PLAN_RECEIPT: sha256-v1:"<<custom_steam_library::plan_receipt::digest(plan)<<"\n";return 0;
    }
    check(argc>=3,"Missing fixture EXE/output");const auto exe=fs::absolute(argv[1]);const auto out=fs::absolute(argv[2]);
    check(normalizedPathKey(exe).starts_with(normalizedPathKey(data.parent_path())+L"\\")&&
        normalizedPathKey(out).starts_with(normalizedPathKey(data)+L"\\"),"Worker path escaped fixture");
    if(!removal.empty()){
        wchar_t gateName[256]{},readyName[256]{};check(GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_GATE",gateName,256)&&GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_READY",readyName,256),"Missing queue removal gates");
        HANDLE gate=OpenEventW(SYNCHRONIZE,FALSE,gateName),ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,readyName);check(gate&&ready,"Cannot open queue removal gates");
        text(data/L"state"/L"removal-entered"/(exe.filename().wstring()+L".json"),json{{"pid",GetCurrentProcessId()},{"metadata",has(L"--metadata-only")}}.dump());SetEvent(ready);
        const auto released=WaitForSingleObject(gate,10000);CloseHandle(gate);CloseHandle(ready);check(released==WAIT_OBJECT_0,"Queue removal gate not released");
        if(has(L"--metadata-only")){
            text(out/L"result"/L"manifest.json",json{{"exe",toUtf8(exe.wstring())},{"match",{{"appId",620},{"storefrontAppId",620},{"formalName","Owned queue identity"},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"},{"source","steam-official"}}},{"metadata",json::object()}}.dump());return 0;
        }
    }
    if(has(L"--metadata-only")) {
        const auto retryControl=bytes(data/L"state"/L"network-retry-fixture.txt");
        if(!retryControl.empty()) {
            const auto id=std::string(64,'a');
            if(retryControl=="unconfirmed") std::cout<<"NETWORK_RETRY_QUEUE_UNAVAILABLE: "<<id<<'\n';
            else std::cout<<"NETWORK_RETRY_JOB: "<<id<<'\n';
            if(retryControl=="duplicate")std::cout<<"NETWORK_RETRY_JOB: "<<id<<'\n';
            std::cout<<"NETWORK_RETRY_WAKE_MODE: event-driven-no-polling\nFAILURE_KIND: network-unavailable\n";
            return 1;
        }
        wchar_t gateName[256]{},readyName[256]{};
        const auto gateSize=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_GATE",gateName,256);
        if(gateSize){
            const auto readySize=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_READY",readyName,256);
            check(gateSize<256&&readySize&&readySize<256,"Invalid owned queue gates");
            HANDLE gate=OpenEventW(SYNCHRONIZE,FALSE,gateName),ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,readyName);
            check(gate&&ready,"Cannot open owned queue gates");
            text(data/L"state"/(L"queue-fixture-worker-"+std::to_wstring(GetCurrentProcessId())+L".json"),
                json{{"pid",GetCurrentProcessId()},{"executable",toUtf8(exe.wstring())},{"output",toUtf8(out.wstring())},{"stage","metadata-only"}}.dump());
            SetEvent(ready);const auto released=WaitForSingleObject(gate,10000);CloseHandle(ready);CloseHandle(gate);
            check(released==WAIT_OBJECT_0,"Owned queue gate was not released");
        }
        writeJsonWithLocalBackup(out/L"rounds-failed.json",{{"executable",toUtf8(exe.wstring())},
            {"failure",{{"kind","unrecognized"},{"retryWhenNetworkReturns",false}}}},false);
        std::cout<<"FAILURE_KIND: unrecognized\n";return 1;
    }
    check(has(L"--igdb-artwork-only")||(!removal.empty()&&has(L"--steam-artwork-only")),"No-ID queue used a Steam artwork/identity worker");
    const auto artwork=data/L"artwork"/(removal.empty()?L"old-game":exe.filename().wstring());json slots=json::array();
    for(const auto* type:{"tall","long","hero"}) {
        const auto image=artwork/(toWide(type)+L".png");text(image,"isolated picture fixture");
        slots.push_back({{"type",type},{"ok",true},{"portableFile",toUtf8(image.wstring())},{"provider","baidu-image"}});
    }
    const json manifest={{"exe",toUtf8(exe.wstring())},{"gameDirectory",toUtf8(exe.parent_path().wstring())},
        {"match",{{"appId",0},{"igdbId",0},{"primaryProvider","artwork-fallback"},{"identityStatus","unverified"}}},
        {"artwork",slots},{"successCount",3},{"ok",true},{"required",{{"tall",true},{"hero",true}}}};
    writeJsonWithLocalBackup(artwork/L"manifest.json",manifest,false);
    writeJsonWithLocalBackup(out/L"result"/L"manifest.json",manifest,false);return 0;
}
static void runQueueFixture(const fs::path& root,bool manual,bool cooldown,bool cachedPrograms=false,const std::string& retryControl={}) {
    wchar_t layoutValue[32]{};const auto layoutSize=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_KEY_LAYOUT",layoutValue,32);
    check(layoutSize<32,"Queue fixture key layout too long");const auto keyLayout=layoutSize?toUtf8(layoutValue):std::string{"legacy"};
    check(keyLayout=="legacy"||keyLayout=="canonical","Unknown queue fixture key layout");
    wchar_t variantValue[256]{},oldGate[256]{},oldReady[256]{};
    const auto variantSize=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_VARIANT",variantValue,256);
    check(variantSize<256,"Queue fixture variant too long");const auto variant=variantSize?toUtf8(variantValue):std::string{};
    check(variant.empty()||(!manual&&!cooldown&&(variant=="busy-coalesces"||variant=="normal-open"||variant=="manual-force"||
        variant=="manual-force-with-followup"||variant=="cancelled"||variant=="shutdown"||variant=="missing-worker")),"Unsupported queue fixture variant");
    const auto oldGateSize=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_GATE",oldGate,256),oldReadySize=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_READY",oldReady,256);
    const auto oldSequence=g_scrapeBatchSequence.load();HANDLE entered=nullptr,release=nullptr,heldWorker=nullptr;
    bool cancelledBeforeRelease=false;DWORD heldWorkerPid=0,heldWorkerExit=STILL_ACTIVE;
    const auto savedData=g_dataRoot,savedLab=g_labRoot,savedWorker=g_worker;
    const auto savedTarget=steamTargetSnapshot();
    wchar_t prior[32768]{};const auto priorLength=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",prior,32768);
    auto restore=[&]{
        if(release)SetEvent(release);
        g_shuttingDown=true;requestTaskCancellation();closeWorkerProcessJob();
        for(;;){joinHostBackgroundTasks();std::lock_guard<std::mutex> lock(g_hostThreadsMutex);if(g_hostThreads.empty())break;}
        if(heldWorker)CloseHandle(heldWorker);if(entered)CloseHandle(entered);if(release)CloseHandle(release);
        heldWorker=entered=release=nullptr;
        SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_GATE",oldGateSize&&oldGateSize<256?oldGate:nullptr);
        SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_READY",oldReadySize&&oldReadySize<256?oldReady:nullptr);
        g_dataRoot=savedData;g_labRoot=savedLab;g_worker=savedWorker;g_steamTarget=savedTarget;
        SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",priorLength&&priorLength<32768?prior:nullptr);
        g_shuttingDown=false;g_cancelRequested=false;
    };
    try {
        g_dataRoot=root/L"data";trustedAdmissionFixtureRoot(g_dataRoot);g_labRoot=root;g_worker=selfExecutable();g_steamTarget=json::object();
        SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",g_dataRoot.c_str());
        const auto joined=root/L"Games"/L"Joined"/L"Joined.exe",old=cachedPrograms&&manual?root/L"Games"/L"PSP"/L"PPSSPPWindows64.exe":root/L"Games"/L"Ishin2"/L"ISHIN2.EXE";
        text(joined,"already-added fixture");text(old,"old no-ID fixture");json games=json::array();
        for(const auto& exe:{joined,old}) games.push_back({{"primaryExecutable",toUtf8(exe.wstring())},
            {"gameDirectory",toUtf8(exe.parent_path().wstring())},{"directoryName",toUtf8(exe.parent_path().filename().wstring())},
            {"status","ready"},{"contentType","game"}});
        std::vector<fs::path> localPrograms;
        if(cachedPrograms&&!manual)for(const auto* name:{L"PPSSPPWindows64.exe",L"snes9x.exe",L"NsEmuTools.exe"}){
            const auto exe=root/L"Games"/L"RetainedApplications"/fs::path(name).stem()/name;text(exe,"owned inert application, never executed");localPrograms.push_back(exe);
            games.push_back({{"primaryExecutable",toUtf8(exe.wstring())},{"gameDirectory",toUtf8(exe.parent_path().wstring())},{"directoryName",toUtf8(exe.parent_path().filename().wstring())},{"status","ready"},{"contentType","game"}});
        }
        writeJsonWithLocalBackup(g_dataRoot/L"state"/L"library-scan.json",{{"games",games}},false);
        writeJsonWithLocalBackup(g_dataRoot/L"config"/L"library-config.json",{{"roots",json::array({toUtf8((root/L"Games").wstring())})}},false);
        json overrides={{"name","維新の嵐 幕末志士伝"}};
        if(cooldown) overrides["automaticIdentityRetryAfterAt"]=unixTimeMsHost()+600000;
        const auto overrideKey=keyLayout=="canonical"?toUtf8(normalizedPathKey(old)):toUtf8(old.wstring());
        writeJsonWithLocalBackup(g_dataRoot/L"config"/L"manual-overrides.json",{{"items",{{overrideKey,overrides}}}},false);
        json plan={{"libraryState",toUtf8((g_dataRoot/L"state"/L"library-scan.json").wstring())},
            {"summary",{{"alreadyInSteam",1}}},{"items",json::array({
                {{"primaryExecutable",toUtf8(joined.wstring())},{"gameDirectory",toUtf8(joined.parent_path().wstring())},
                 {"status","already-in-steam"},{"shortAppId","2388972698"}},
                {{"primaryExecutable",toUtf8(old.wstring())},{"gameDirectory",toUtf8(old.parent_path().wstring())},
                 {"status","needs-identity-and-artwork"},{"artwork",{{"minimumComplete",false},{"long",false},{"automaticSlotsComplete",false}}}}
            })}};
        for(const auto& exe:localPrograms)plan["items"].push_back({{"primaryExecutable",toUtf8(exe.wstring())},{"gameDirectory",toUtf8(exe.parent_path().wstring())},{"status","waiting-steam-verification"},{"artwork",{{"minimumComplete",true},{"long",true},{"automaticSlotsComplete",true}}}});
        if(!retryControl.empty()){
            text(g_dataRoot/L"state"/L"network-retry-fixture.txt",retryControl);
            if(retryControl!="artwork")plan["items"][1]["artwork"]={{"minimumComplete",true},{"long",true},{"automaticSlotsComplete",true}};
        }
        writeJsonWithLocalBackup(workspacePlanPath(),plan,false);writeJsonWithLocalBackup(g_dataRoot/L"state"/L"fixture-plan.json",plan,false);
        const auto deadline=GetTickCount64()+15000;
        if(!variant.empty()){
            const auto name=L"Local\\YMCC-OwnedQueueGate-"+std::to_wstring(GetCurrentProcessId());
            entered=CreateEventW(nullptr,TRUE,FALSE,(name+L"-ready").c_str());release=CreateEventW(nullptr,TRUE,FALSE,(name+L"-release").c_str());
            check(entered&&release,"Cannot create owned queue gates");
            SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_GATE",(name+L"-release").c_str());
            SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_READY",(name+L"-ready").c_str());
        }
        check(startScrapeQueue(manual),"Queue fixture did not start");
        if(!variant.empty()){
            check(WaitForSingleObject(entered,3000)==WAIT_OBJECT_0&&g_scrapeQueueRunning.load(),"Actual data-bearing Worker did not enter owned gate");
            BackgroundJob owner,item;
            {std::lock_guard<std::mutex> lock(g_backgroundJobsMutex);for(const auto& candidate:g_scrapeQueueJobs){
                if(candidate&&candidate->key=="library")owner=candidate;
                if(candidate&&candidate->key=="library-item|"+backgroundExecutableKey(toUtf8(old.wstring())))item=candidate;
            }}
            check(owner&&item&&!owner->cancelRequested.load()&&!item->cancelRequested.load(),"Missing live owned data-bearing queue jobs");
            {std::lock_guard<std::mutex> lock(g_activeProcessMutex);check(item->activeProcess&&DuplicateHandle(GetCurrentProcess(),item->activeProcess,GetCurrentProcess(),&heldWorker,0,FALSE,DUPLICATE_SAME_ACCESS),"Cannot hold actual gated Worker");}
            heldWorkerPid=GetProcessId(heldWorker);BOOL assigned=FALSE;
            check(heldWorkerPid&&IsProcessInJob(heldWorker,g_workerProcessJob,&assigned)&&assigned,"Gated Worker is not owned by original Host Job");
            if(variant!="manual-force")for(int n=0;n<20;++n)check(!(variant=="normal-open"?startScrapeQueue(false):requestOwnedScrapeFollowup(false)),"Busy data-bearing queue started parallel work");
            check(!owner->cancelRequested.load()&&WaitForSingleObject(heldWorker,0)==WAIT_TIMEOUT,"Automatic followup cancelled actual data-bearing Worker");
            if(variant=="missing-worker")g_worker=root/L"missing-owned-worker.exe";
            if(variant=="manual-force"||variant=="manual-force-with-followup")for(int n=0;n<20;++n)check(!startScrapeQueue(true),"Manual force started parallel owner");
            if(variant=="cancelled")requestTaskCancellation();
            if(variant=="shutdown")g_shuttingDown=true;
            cancelledBeforeRelease=owner->cancelRequested.load();
            if(variant=="manual-force"||variant=="manual-force-with-followup"||variant=="cancelled"||variant=="shutdown")
                check(WaitForSingleObject(heldWorker,2000)==WAIT_OBJECT_0,"Cancelled actual Worker remained live until gate release");
            SetEvent(release);
        }
        while(g_scrapeQueueRunning.load()&&GetTickCount64()<deadline)Sleep(10);
        check(!g_scrapeQueueRunning.load(),"Queue waited for identity retries/batch barrier");
        for(;;){joinHostBackgroundTasks();bool pending=false;{std::lock_guard<std::mutex> lock(g_hostThreadsMutex);pending=!g_hostThreads.empty();}
            if(!pending&&!g_scrapeQueueRunning.load())break;check(GetTickCount64()<deadline,"Data-bearing successor queue did not drain");Sleep(1);}
        json history;std::vector<json> histories;
        for(const auto& entry:fs::directory_iterator(scrapeHistoryRoot())) {
            if(entry.path().extension()==L".json"&&entry.path().filename().wstring().starts_with(L"round-"))histories.push_back(readJson(entry.path(),json::object()));
        }
        std::sort(histories.begin(),histories.end(),[](const auto& a,const auto& b){return jsonStringOr(a,"batchId")<jsonStringOr(b,"batchId");});
        check(!histories.empty(),"Data-bearing queue produced no history");history=histories.front();
        if(!variant.empty()){
            check(WaitForSingleObject(heldWorker,0)==WAIT_OBJECT_0&&GetExitCodeProcess(heldWorker,&heldWorkerExit),"Completed queue retained its gated Worker");
            const auto batches=g_scrapeBatchSequence.load()-oldSequence;size_t workerRuns=0;
            for(const auto& h:histories)for(const auto& run:jsonArrayOr(h,"workerRuns")){
                ++workerRuns;check(jsonStringOr(run,"executable")==toUtf8(old.wstring()),"Joined game entered a busy queue variant");
            }
            writeJsonWithLocalBackup(root/L"queue-variant-observed.json",{{"variant",variant},{"batches",batches},{"histories",histories},
                {"workerRuns",workerRuns},{"heldWorkerPid",heldWorkerPid},{"heldWorkerExit",heldWorkerExit},{"cancelledBeforeRelease",cancelledBeforeRelease},
                {"running",g_scrapeQueueRunning.load()},{"jobs",g_scrapeQueueJobs.size()}},false);
            check(!g_scrapeQueueRunning.load()&&g_scrapeQueueJobs.empty(),"Busy data-bearing queue retained ownership/jobs");
            if(variant=="missing-worker"){
                check(batches==1&&histories.size()==1,"Failed data-bearing queue automatically resumed a follow-up");
                check(history["status"]=="failed"&&history["summary"]["failed"]==true&&workerRuns==1,"Missing Worker failure was not preserved");
                restore();return;
            }
            if(variant=="cancelled"||variant=="shutdown"){
                check(batches==1&&histories.size()==1&&history["status"]=="paused"&&workerRuns==1,"Cancelled data-bearing queue restarted or lost terminal history");
                restore();return;
            }
            if(variant=="manual-force"||variant=="manual-force-with-followup"){
                check(batches==2&&histories.size()==2&&histories[0]["status"]=="paused"&&histories[1]["status"]=="completed"&&
                    histories[1]["mode"]=="manual-refresh"&&cancelledBeforeRelease&&heldWorkerExit==ERROR_CANCELLED&&workerRuns==3,
                    "Manual force did not cancel old child and complete exactly one serial retry");history=histories.back();
            }else{
                check(batches==(variant=="busy-coalesces"?2:1)&&histories.size()==batches&&workerRuns==2,"Busy followups were lost, duplicated or repeated completed media work");
                for(const auto& h:histories)check(h["mode"]=="automatic-first-pass"&&h["status"]=="completed","Automatic followup borrowed manual force or failed");
            }
        }
        if(cachedPrograms&&!manual){
            size_t applicationRuns=0;for(const auto& h:histories)for(const auto& run:jsonArrayOr(h,"workerRuns"))for(const auto& exe:localPrograms)if(backgroundExecutableKey(jsonStringOr(run,"executable"))==backgroundExecutableKey(toUtf8(exe.wstring())))++applicationRuns;
            writeJsonWithLocalBackup(root/L"local-program-queue-observations.json",{{"applicationWorkerRuns",applicationRuns},{"total",history["summary"]["total"]},{"running",g_scrapeQueueRunning.load()},{"pendingJobs",g_scrapeQueueJobs.size()}},false);
            check(applicationRuns==0,"Cached emulator applications entered automatic identity workers");
        }
        check(history["status"]=="completed"&&history["summary"]["total"]==1&&history["summary"]["completed"]==1,"No-ID queue did not complete or scraped joined game");
        const auto runs=jsonArrayOr(history,"workerRuns");
        if(!retryControl.empty()&&retryControl!="artwork"){
            auto stages=json::array();
            for(const auto& run:runs){
                check(jsonStringOr(run,"executable")==toUtf8(old.wstring()),"Network retry fixture touched joined game");
                stages.push_back(jsonStringOr(run,"stage"));
            }
            writeJsonWithLocalBackup(root/L"network-retry-observed.json",{{"mode",retryControl},{"stages",stages},{"status",history["status"]},{"running",g_scrapeQueueRunning.load()}},false);
            check(runs.size()==(retryControl=="queued"?1:4),"Incorrect number of identity attempts after network retry reply");
            check(std::all_of(stages.begin(),stages.end(),[](const auto& stage){return stage=="identity";}),"Already complete artwork ran another stage");
            check(g_scrapeQueueJobs.empty(),"Network retry fixture left Host jobs owned");
            restore();return;
        }
        check(runs.size()==(cooldown&&!manual?1:2),"Identity failure blocked fallback or caused redundant identity retries");
        for(const auto& run:runs)check(jsonStringOr(run,"executable")==toUtf8(old.wstring()),"Joined shortcut underwent automatic ID verification");
        check(jsonStringOr(runs.back(),"stage")=="artwork-fallback","No-ID game did not directly enter visual fallback");
        const auto snapshot=workspaceSnapshot(false);const auto items=snapshot["steamPlan"]["items"];
        check(items[0]["status"]=="already-in-steam","Artwork refresh moved joined game");
        check(items[1]["status"]=="waiting-steam-verification"&&items[1]["artworkPreview"].size()==3,"No-ID pictures not applied or invented import identity");
        restore();
    } catch(...) {restore();throw;}
}
static void runQueueRemovalFixture(const fs::path& root,bool artworkOnly,bool reject){
    const auto savedData=g_dataRoot,savedLab=g_labRoot,savedWorker=g_worker;const auto savedTarget=steamTargetSnapshot();const auto savedAllowed=manualEnrichmentFixtureWorker;
    std::vector<std::pair<std::wstring,std::wstring>> environment;for(const auto* name:{L"YMCC_QUEUE_FIXTURE_DATA_ROOT",L"YMCC_QUEUE_FIXTURE_GATE",L"YMCC_QUEUE_FIXTURE_READY"}){wchar_t value[32768]{};const auto n=GetEnvironmentVariableW(name,value,32768);environment.emplace_back(name,n&&n<32768?value:L"");}
    HANDLE entered=nullptr,release=nullptr,held=nullptr;
    const auto cleanup=[&]{if(release)SetEvent(release);g_shuttingDown=true;requestTaskCancellation();closeWorkerProcessJob();joinHostBackgroundTasks();if(held)CloseHandle(held);if(entered)CloseHandle(entered);if(release)CloseHandle(release);g_dataRoot=savedData;g_labRoot=savedLab;g_worker=savedWorker;g_steamTarget=savedTarget;manualEnrichmentFixtureWorker=savedAllowed;for(const auto& [name,value]:environment)SetEnvironmentVariableW(name.c_str(),value.empty()?nullptr:value.c_str());held=entered=release=nullptr;g_shuttingDown=false;g_cancelRequested=false;};
    try{
        g_dataRoot=root/L"data";trustedAdmissionFixtureRoot(g_dataRoot);g_labRoot=root;g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();g_steamTarget=json::object();initializeWorkerProcessJob();
        const auto removed=root/L"Games"/L"Parent"/L"removed.exe",child=removed.parent_path()/L"Independent"/L"child.exe",peer=root/L"Games"/L"Parent-peer"/L"peer.exe";
        json games=json::array(),items=json::object();for(const auto& exe:{removed,child,peer}){text(exe,"owned inert queue executable");games.push_back({{"primaryExecutable",toUtf8(exe.wstring())},{"gameDirectory",toUtf8(exe.parent_path().wstring())},{"directoryName",toUtf8(exe.parent_path().filename().wstring())},{"status","ready"},{"contentType","game"}});if(exe!=removed||!artworkOnly)items[toUtf8(normalizedPathKey(exe))]={{"name","accepted name"},{"futureField",42}};}
        writeJsonWithLocalBackup(g_dataRoot/L"state"/L"library-scan.json",{{"games",games}},false);writeJsonWithLocalBackup(g_dataRoot/L"config"/L"library-config.json",{{"roots",json::array({toUtf8((root/L"Games").wstring())})}},false);
        const auto overridesPath=g_dataRoot/L"config"/L"manual-overrides.json";writeJsonWithLocalBackup(overridesPath,{{"items",items}},false);
        const auto key=toUtf8(normalizedPathKey(removed));const auto cache=g_dataRoot/L"cache"/L"identity"/toWide(safeArtworkSegment(key)+".json");
        if(artworkOnly){const auto seed=g_dataRoot/L"cache"/L"jobs"/L"seed"/L"manifest.json";text(seed,json{{"exe",toUtf8(removed.wstring())},{"match",{{"appId",620},{"storefrontAppId",620},{"formalName","Owned cached identity"},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"},{"source","steam-official"}}},{"metadata",json::object()}}.dump());check(persistResolvedIdentityCache(seed),"Cannot seed verified artwork-only identity");}
        const auto cacheBefore=bytes(cache);text(g_dataRoot/L"state"/L"removal-scenario.json",json{{"directory",toUtf8(removed.parent_path().wstring())},{"key",key},{"reject",reject}}.dump());
        const auto name=L"Local\\YMCC-QueueRemoval-"+std::to_wstring(GetCurrentProcessId())+(artworkOnly?L"-art":L"-identity")+(reject?L"-failed":L"-confirmed");
        entered=CreateEventW(nullptr,TRUE,FALSE,(name+L"-ready").c_str());release=CreateEventW(nullptr,TRUE,FALSE,(name+L"-release").c_str());check(entered&&release,"Cannot allocate queue removal gates");
        SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",g_dataRoot.c_str());SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_GATE",(name+L"-release").c_str());SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_READY",(name+L"-ready").c_str());
        check(startScrapeQueue(false),"Removal queue did not start");const auto deadline=GetTickCount64()+12000;bool allEntered=false;
        do{allEntered=true;for(const auto& exe:{removed,child,peer})allEntered=allEntered&&fs::exists(custom_steam_library::ioPath(g_dataRoot/L"state"/L"removal-entered"/(exe.filename().wstring()+L".json")));if(!allEntered)Sleep(5);}while(!allEntered&&GetTickCount64()<deadline);
        check(allEntered,"Three removal queue workers did not enter controlled gates");BackgroundJob owner,item;
        {std::lock_guard<std::mutex> lock(g_backgroundJobsMutex);for(const auto& job:g_scrapeQueueJobs){if(job->key=="library")owner=job;if(job->key=="library-item|"+key)item=job;}}
        check(owner&&item,"Missing owned queue removal tasks");{std::lock_guard<std::mutex> lock(g_activeProcessMutex);check(item->activeProcess&&DuplicateHandle(GetCurrentProcess(),item->activeProcess,GetCurrentProcess(),&held,0,FALSE,DUPLICATE_SAME_ACCESS),"Cannot hold removed game's actual Worker");}
        const auto entry=readJson(g_dataRoot/L"state"/L"removal-entered"/(removed.filename().wstring()+L".json"),json::object());check(entry.value("metadata",false)!=artworkOnly,"Artwork-only control incorrectly performed identity work");
        bool commandRejected=false;std::string detail;try{executeWorkspaceCommand("removeGame",{{"gameDirectory",toUtf8(removed.parent_path().wstring())}});}catch(const std::exception& error){commandRejected=true;detail=error.what();}
        const bool itemCancelled=item->cancelRequested.load(),parentCancelled=owner->cancelRequested.load();SetEvent(release);
        while(g_scrapeQueueRunning.load()&&GetTickCount64()<deadline)Sleep(5);check(!g_scrapeQueueRunning.load(),"Removed queue did not drain");joinHostBackgroundTasks();
        DWORD workerExit=STILL_ACTIVE;check(WaitForSingleObject(held,0)==WAIT_OBJECT_0&&GetExitCodeProcess(held,&workerExit),"Removed queue Worker is still live");
        auto actual=readJson(overridesPath,json::object());size_t removedRuns=0,peerRuns=0;json histories=json::array();
        for(const auto& file:fs::directory_iterator(custom_steam_library::ioPath(scrapeHistoryRoot())))if(file.path().filename().wstring().starts_with(L"round-")&&file.path().extension()==L".json"){auto history=readJson(file.path(),json::object());histories.push_back(history);for(const auto& run:jsonArrayOr(history,"workerRuns")){if(jsonStringOr(run,"executable")==toUtf8(removed.wstring()))++removedRuns;else ++peerRuns;}}
        bool passed=commandRejected&&detail.find(reject?"owned queue removal rejected":"owned queue rescan failure")!=std::string::npos&&!parentCancelled&&itemCancelled!=reject&&g_scrapeQueueJobs.empty()&&g_hostThreads.empty();
        for(const auto& exe:{child,peer}){const auto k=toUtf8(normalizedPathKey(exe));passed=passed&&actual["items"][k]["futureField"]==42&&jsonStringOr(actual["items"][k],"automaticIdentityLastStatus")=="completed"&&fs::exists(custom_steam_library::ioPath(g_dataRoot/L"artwork"/exe.filename()/L"manifest.json"));}
        if(reject)passed=passed&&workerExit==0&&removedRuns==(artworkOnly?1:2);
        else passed=passed&&workerExit==ERROR_CANCELLED&&removedRuns==1&&!actual["items"].contains(key)&&bytes(cache)==cacheBefore;
        passed=passed&&peerRuns==4;
        writeJsonWithLocalBackup(root/L"removal-queue-observed.json",{{"artworkOnly",artworkOnly},{"reject",reject},{"passed",passed},{"commandRejected",commandRejected},{"error",detail},{"itemCancelled",itemCancelled},{"parentCancelled",parentCancelled},{"removedRuns",removedRuns},{"peerRuns",peerRuns},{"workerExit",workerExit},{"entries",actual},{"histories",histories}},false);
        cleanup();check(passed,"Removed game continued automatic writes/stages or interrupted retained games");
    }catch(...){if(held||entered||release)cleanup();throw;}
}
template<class Action> static bool identityCallerPersist(Action&& action){if constexpr(std::is_void_v<decltype(action())>){action();return true;}else return action();}
static int libraryMediaDemandProbe(const fs::path& root){
 try{
  check(!fs::exists(root),"fresh isolated library demand root required");fs::create_directories(root);admissionFixtureOnly=true;json cases=json::array();unsigned checks=0;
  const auto verify=[&](bool ok,const char* why){++checks;check(ok,why);};
  const auto test=[&](const char* name,auto fn){g_dataRoot=root/toWide(name)/L"data";g_labRoot=root;trustedAdmissionFixtureRoot(g_dataRoot);g_shuttingDown=false;g_cancelRequested=false;admissionProcesses=0;admissionNetwork=0;admissionShell=0;const auto start=checks;try{fn();verify(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"media command escaped original host isolation");cases.push_back({{"name",name},{"passed",true},{"checks",checks-start}});}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"checks",checks-start},{"error",e.what()}});}ymcc::media::steamDemand::setForegroundReader({});};
  const auto steamEntry=[&](int id){ymcc::media::steamDemand::setForegroundReader([]{return json{{"source","native-steam-bigpicture-foreground"},{"known",true},{"foreground",true},{"current",true},{"mode","bigpicture"},{"pid",123},{"birth","456"},{"window","789"},{"ownerEpoch","owner-A"},{"revision",1}};});ymcc::media::Store store(g_dataRoot);auto lease=store.begin({{"appId",id},{"verified",true},{"demandConsumer","steam"},{"demandEpoch","owner-A"},{"demandRevision",1}});verify(lease.value("acquired",false),"owned Steam lease prepared");const auto file=g_dataRoot/L"owned.mp4";text(file,std::string("\0\0\0\x18" "ftypisom",12));auto entry=store.publish(lease,file,false);store.finish(entry);ymcc::media::steamDemand::setForegroundReader({});return entry;};
  test("library-edit-uses-command-owner-not-stored-steam-ticket",[&]{auto entry=steamEntry(501);entry["action"]="mode";entry["mode"]="static";const auto result=executeWorkspaceCommand("mediaEdit",entry);verify(result.value("wallpaperMode",std::string{})=="static"&&result.value("modeExplicit",false),"independent library edit inherited revoked Steam consumer");verify(ymcc::media::Store(g_dataRoot).resolve({{"appId",501}}).value("wallpaperMode",std::string{})=="static","library edit was not durable");});
  test("library-import-keeps-manual-slot-and-video",[&]{auto entry=steamEntry(502);const auto image=g_dataRoot/L"owned.jpg";text(image,std::string("\xff\xd8\xff\xe0",4));entry["source"]=toUtf8(image.wstring());entry["slot"]="static";const auto result=executeWorkspaceCommand("mediaImport",entry);verify(result.value("staticReady",false)&&result.value("dynamicReady",false),"independent library import inherited revoked Steam consumer or lost video");verify(result["manualSlots"].value("static",false),"manual slot precedence was lost");});
  test("library-create-reuses-shared-entry-without-steam-proof",[&]{const auto entry=steamEntry(503);const auto result=executeWorkspaceCommand("mediaCreate",entry);verify(result.value("key",std::string{})=="steam-503"&&result.value("status",std::string{})!="consumer-inactive","library registration inherited a completed Steam ticket");});
  test("library-command-retains-binding-conflict",[&]{auto entry=steamEntry(504);entry["demandConsumer"]="library";entry.erase("demandEpoch");entry.erase("demandRevision");auto change=entry;change["action"]="rename";change["name"]="New name";const auto accepted=executeWorkspaceCommand("mediaEdit",change);verify(accepted.value("name",std::string{})=="New name","independent explicit library control failed");change["name"]="stale name";bool refused=false;try{executeWorkspaceCommand("mediaEdit",change);}catch(const std::exception&){refused=true;}verify(refused,"consumer normalization bypassed bindingRevision conflict");verify(ymcc::media::Store(g_dataRoot).resolve({{"appId",504}}).value("name",std::string{})=="New name","stale library request overwrote accepted metadata");});
  test("direct-steam-callback-remains-inactive",[&]{auto entry=steamEntry(505);entry["action"]="mode";entry["mode"]="static";const auto result=ymcc::media::Store(g_dataRoot).edit(entry);verify(result.value("status",std::string{})=="consumer-inactive","library boundary relaxed direct Steam gate");verify(ymcc::media::Store(g_dataRoot).resolve({{"appId",505}}).value("wallpaperMode",std::string{})=="dynamic","inactive Steam callback changed shared mode");});
  test("plain-library-request-remains-independent",[&]{auto entry=executeWorkspaceCommand("mediaCreate",{{"appId",506},{"verified",true}});entry["action"]="rename";entry["name"]="Library choice";const auto result=executeWorkspaceCommand("mediaEdit",entry);verify(result.value("name",std::string{})=="Library choice","ordinary library request now requires Steam foreground");});
  const bool ok=std::all_of(cases.begin(),cases.end(),[](const json& row){return row.value("passed",false);});const json result={{"ok",ok},{"cases",cases},{"caseCount",cases.size()},{"checks",checks},{"scope","Original executeWorkspaceCommand mediaCreate/mediaEdit/mediaImport with owned files and injected Steam proof; external process/network/shell denied"}};writeJsonWithLocalBackup(root/L"library-demand-result.json",result,false);std::cout<<result.dump()<<std::endl;return ok?0:1;
 }catch(const std::exception& e){ymcc::media::steamDemand::setForegroundReader({});std::cerr<<e.what()<<std::endl;return 1;}
}
template<class Value> static uint64_t fixtureReplyRequest(Value id){if constexpr(requires{registerWorkspaceReply(id);})return registerWorkspaceReply(id);else return id;}
template<class Value> static std::optional<uint64_t> fixtureReplyResult(Value id){if constexpr(requires{consumeWorkspaceReply(id);})return consumeWorkspaceReply(id);else return id;}
static int replyRoutingProbe(const fs::path& root){
 json checks=json::array();HWND window=nullptr;
 const auto verify=[&](bool ok,const char* name){checks.push_back({{"name",name},{"passed",ok}});if(!ok)throw std::runtime_error(name);};
 try{
  const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
  check(!fs::exists(root)&&normalizedPathKey(root).starts_with(normalizedPathKey(evidence)+L"\\"),"fresh isolated reply root required");fs::create_directories(root);g_dataRoot=root/L"data";g_labRoot=root;trustedAdmissionFixtureRoot(g_dataRoot);g_shuttingDown=false;g_cancelRequested=false;admissionFixtureOnly=true;admissionProcesses=admissionNetwork=admissionShell=0;
  const auto old=fixtureReplyRequest(uint64_t{1});retireWorkspaceRefreshProtocol();const auto fresh=fixtureReplyRequest(uint64_t{1});
  verify(!fixtureReplyResult(old),"retired response cannot fulfill the new page's reused request1");
  verify(old!=fresh,"native correlation token is never reused after page retirement");
  verify(fixtureReplyResult(fresh)==std::optional<uint64_t>{1},"current page still receives its original browser request id");
  verify(!fixtureReplyResult(fresh),"duplicate response cannot be delivered twice");
  verify(!fixtureReplyResult(UINT64_MAX),"unknown response token cannot borrow a current request");
  const auto a=fixtureReplyRequest(uint64_t{42}),b=fixtureReplyRequest(uint64_t{43});verify(fixtureReplyResult(b)==std::optional<uint64_t>{43}&&fixtureReplyResult(a)==std::optional<uint64_t>{42},"out-of-order current responses retain exact request identities");
  const auto prior=fixtureReplyRequest(uint64_t{8});retireWorkspaceRefreshProtocol();const auto next=fixtureReplyRequest(uint64_t{8});verify(!fixtureReplyResult(prior)&&fixtureReplyResult(next)==std::optional<uint64_t>{8},"a late failure also cannot settle a replacement request");
  window=CreateWindowExW(0,L"STATIC",L"owned reply fixture",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);verify(window!=nullptr,"owned native result window exists");g_window=window;
  ymcc::media::Store store(g_dataRoot);auto entry=store.registerEntry({{"appId",570},{"verified",true}});entry["action"]="rename";entry["name"]="accepted before recovery";uint64_t write=0,current=0;
  {ymcc::media::Lock hold(g_dataRoot);write=fixtureReplyRequest(uint64_t{1});beginCommand(write,"mediaEdit",entry);verify(g_acceptedWrites.load()==1,"original FIFO accepts the retained write before recovery");retireWorkspaceRefreshProtocol();current=fixtureReplyRequest(uint64_t{1});verify(g_acceptedWrites.load()==1&&!g_cancelRequested.load(),"page retirement neither drops nor cancels the accepted write");}
  const auto until=GetTickCount64()+5000;while(g_taskRunning.load()&&GetTickCount64()<until)Sleep(1);verify(!g_taskRunning.load(),"original retained FIFO finishes within fixture bound");joinHostBackgroundTasks();
  verify(store.resolve({{"appId",570}}).value("name",std::string{})=="accepted before recovery","the actual accepted media edit is durable after document retirement");
  bool found=false;MSG m{};while(PeekMessageW(&m,window,WM_WORKSPACE_RESULT,WM_WORKSPACE_RESULT,PM_REMOVE)){std::unique_ptr<AsyncResult> result(reinterpret_cast<AsyncResult*>(m.lParam));if(result&&result->message.value("id",uint64_t{0})==write){found=true;verify(result->message.value("ok",false),"the original FIFO produced its real successful receipt");verify(!fixtureReplyResult(write),"late successful queued receipt is not delivered into the new document");}}
  verify(found,"actual native result queue contains the retired write receipt");verify(fixtureReplyResult(current)==std::optional<uint64_t>{1},"new page request survives completion of the old write");verify(g_acceptedWrites.load()==0,"accepted write accounting returns to zero");
  verify(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"reply and retained-write test performs no external work");
  stopWorkspaceResultPosting(window);DestroyWindow(window);window=nullptr;g_window=nullptr;g_shuttingDown=false;
 }catch(const std::exception& error){if(window){stopWorkspaceResultPosting(window);requestTaskCancellation();joinHostBackgroundTasks();DestroyWindow(window);g_window=nullptr;}g_shuttingDown=false;checks.push_back({{"error",error.what()},{"passed",false}});}
 const bool ok=std::all_of(checks.begin(),checks.end(),[](const json& c){return c.value("passed",false);});const auto result=json{{"ok",ok},{"checks",checks},{"scope","actual native reply router and original retained FIFO/Store with owned HWND/files; no real WebView crash or installed app"}};writeJsonWithLocalBackup(root/L"reply-result.json",result,false);std::cout<<result.dump()<<std::endl;return ok?0:1;
}
class FixtureWebMessageSource final : public ICoreWebView2WebMessageReceivedEventArgs {
public:
 std::wstring source;HRESULT result=S_OK;bool returnNull=false;int bodyReads=0;
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void** value) override {if(value)*value=nullptr;return E_NOINTERFACE;}
 ULONG STDMETHODCALLTYPE AddRef() override {return 1;}
 ULONG STDMETHODCALLTYPE Release() override {return 1;}
 HRESULT STDMETHODCALLTYPE get_Source(LPWSTR* value) override {
  if(!value)return E_POINTER;*value=nullptr;if(FAILED(result)||returnNull)return result;
  const auto count=(source.size()+1)*sizeof(wchar_t);*value=static_cast<LPWSTR>(CoTaskMemAlloc(count));if(!*value)return E_OUTOFMEMORY;memcpy(*value,source.c_str(),count);return result;
 }
 HRESULT STDMETHODCALLTYPE get_WebMessageAsJson(LPWSTR* value) override {++bodyReads;if(value)*value=nullptr;return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE TryGetWebMessageAsString(LPWSTR* value) override {++bodyReads;if(value)*value=nullptr;return E_NOTIMPL;}
};
static int webviewSourceProbe(const fs::path& root){
 const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
 check(!fs::exists(root)&&normalizedPathKey(root).starts_with(normalizedPathKey(evidence)+L"\\"),"fresh isolated source-admission root required");fs::create_directories(root);
 json checks=json::array();FixtureWebMessageSource args;
 const auto verify=[&](bool ok,const char* name){checks.push_back({{"name",name},{"passed",ok}});};
 const auto source=[&](const wchar_t* value,bool expected,const char* name){args.source=value;verify(isWorkspaceWebMessageSource(&args)==expected,name);};
 source(L"https://steam-library.localhost/index.html",true,"packaged current UI origin is admitted");
 source(L"https://steam-library.local/index.html",true,"original packaged UI alias remains admitted");
 source(L"https://steam-library.localhost/index.html?phase=2#editor",true,"trusted query and fragment retain command ownership");
 source(L"https://steam-library-data.localhost/foreign.html",false,"mapped media data origin cannot dispatch native commands");
 source(L"https://steam-library.localhost.invalid/index.html",false,"hostname suffix cannot borrow packaged UI origin");
 source(L"https://steam-library.localhost@invalid.local/index.html",false,"userinfo lookalike cannot borrow packaged UI origin");
 source(L"http://steam-library.localhost/index.html",false,"different scheme is not the packaged UI");
 source(L"about:blank",false,"blank document is not a native command UI");
 source(L"",false,"empty source is refused");
 args.result=E_FAIL;verify(!isWorkspaceWebMessageSource(&args),"failed native source query is refused");
 args.result=S_OK;args.returnNull=true;verify(!isWorkspaceWebMessageSource(&args),"successful source query with no value is refused");
 verify(!isWorkspaceWebMessageSource(nullptr),"missing native message arguments are refused");
 verify(args.bodyReads==0,"source admission never reads or parses an unadmitted command body");
 const bool ok=std::all_of(checks.begin(),checks.end(),[](const json& item){return item.value("passed",false);});
 const auto report=json{{"ok",ok},{"checks",checks},{"scope","actual production COM source admission helper and local interface arguments; no real navigation or external action"}};
 writeJsonWithLocalBackup(root/L"webview-source-result.json",report,false);std::cout<<report.dump()<<std::endl;return ok?0:1;
}

// The dependent fallback is the exact pre-optimization download Watcher callback.
// It permits a before-source run without introducing a second queue model.
template<class Root> static void mediaProgressCallback(const Root& root){
    if constexpr(requires{postWorkspaceMediaProgress(root);})postWorkspaceMediaProgress(root);
    else postPipelineEvent("media-download-progress",{{"mediaDownloads",ymcc::media::downloads::Queue(root).snapshot()}});
}
static int mediaProgressNoticeProbe(const fs::path& root){
    json checks=json::array();auto verify=[&](bool ok,const char* label){checks.push_back({{"passed",ok},{"label",label}});if(!ok)throw std::runtime_error(label);};
    const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
    check(normalizedPathKey(root).starts_with(normalizedPathKey(evidence)+L"\\")&&!fs::exists(root),"Unsafe/reused media notice root");fs::create_directories(root);
    try{
        admissionFixtureOnly=true;g_dataRoot=root/L"data";trustedAdmissionFixtureRoot(g_dataRoot);g_labRoot=root;
        RefreshNoticeWindow window;activityOnly=true;activityWindow=window.window;activityVisible=true;activityIconic=false;activityForeground=nullptr;
        struct Restore{~Restore(){activityOnly=false;activityWindow=nullptr;activityForeground=nullptr;g_shuttingDown=false;}} restore;
        using ymcc::media::downloads::Queue;Queue queue(g_dataRoot);json inventory=json::array();for(int id=1;id<=256;++id)inventory.push_back({{"appId",id},{"verified",true}});queue.enqueue(inventory);queue.configure(false,"");
        const auto expected=queue.snapshot();verify(expected.value("total",0)==256,"real queue seeded with 256 jobs");
        const auto queueBytes=bytes(g_dataRoot/L"config"/L"game-media-downloads.json");
        {std::lock_guard lock(g_scrapeStateMutex);g_scrapeState={{"items",json::object()}};for(int id=0;id<128;++id)g_scrapeState["items"][std::to_string(id)]={{"status","running"},{"label",std::string(128,'s')}};}
        json observations=json::array();
        const auto measure=[&](const char* name,const json& capability){
            acceptWorkspaceRefreshProtocol(capability);uint64_t full=0,compact=0,payloadBytes=0;
            const auto work=measuredNativeWork(32,[&]{mediaProgressCallback(g_dataRoot);const auto msg=window.take();full+=msg.contains("mediaDownloads");compact+=jsonBoolOr(msg,"refreshRequested",false);payloadBytes+=msg.dump().size();return size_t{1};});
            observations.push_back({{"name",name},{"work",work},{"fullMessages",full},{"compactMessages",compact},{"payloadBytes",payloadBytes}});
        };
        measure("legacy-background",{{"advisoryRefreshProtocol",1}});
        measure("negotiated-background",{{"advisoryRefreshProtocol",1},{"advisoryMediaProgressProtocol",1}});
        writeJsonWithLocalBackup(root/L"callback-cost.json",{{"observations",observations},{"queueBytes",queueBytes.size()},{"scope","32 exact production callback projections each, actual GetProcessIoCounters and owned hidden window; no whole Watcher or installed CPU claim"}},false);
        verify(observations[0]["work"]["readOperations"].get<uint64_t>()>0,"legacy callback actually reads queue files through ifstream");
        mediaProgressCallback(g_dataRoot);auto compact=window.take();
        verify(jsonBoolOr(compact,"refreshRequested",false)&&jsonIntegerOr(compact,"mediaProgressProtocol")==1&&!compact.contains("mediaDownloads"),"negotiated background callback emits only advisory");
        verify(!compact.contains("scrape"),"compact media advisory omits unrelated scrape projection");
        verify(workspaceRefreshMessageCurrent(compact),"current media advisory is accepted");
        verify(observations[1]["work"]["readOperations"]==0&&observations[1]["work"]["readTransferBytes"]==0,"negotiated callback performs no file reads");
        auto wrong=compact;wrong["mediaProgressProtocol"]=2;verify(!workspaceRefreshMessageCurrent(wrong),"unknown media advisory version is rejected");
        wrong=compact;wrong["refreshDocument"]=0;verify(!workspaceRefreshMessageCurrent(wrong),"missing document media advisory is rejected");
        retireWorkspaceRefreshProtocol();verify(!workspaceRefreshMessageCurrent(compact),"retired document media advisory is rejected");
        acceptWorkspaceRefreshProtocol({{"advisoryMediaProgressProtocol",1}});verify(!workspaceRefreshMessageCurrent(compact),"new document capability cannot revive old advisory");
        const auto full=[&](const char* label){mediaProgressCallback(g_dataRoot);auto message=window.take();verify(message.contains("mediaDownloads")&&message["mediaDownloads"]==expected&&!message.contains("refreshRequested")&&message.contains("scrape")&&workspaceRefreshMessageCurrent(message),label);};
        activityForeground=activityWindow;full("foreground keeps real full progress and scrape payload");
        activityVisible=false;mediaProgressCallback(g_dataRoot);verify(jsonBoolOr(window.take(),"refreshRequested",false),"hidden foreground window uses advisory");activityVisible=true;
        activityIconic=true;mediaProgressCallback(g_dataRoot);verify(jsonBoolOr(window.take(),"refreshRequested",false),"minimized foreground window uses advisory");activityIconic=false;activityForeground=nullptr;
        for(const auto& capability:std::vector<json>{json::object(),json{{"advisoryRefreshProtocol",1}},json{{"advisoryMediaProgressProtocol",2}},json{{"advisoryMediaProgressProtocol","1"}},json{{"advisoryMediaProgressProtocol",true}}}){acceptWorkspaceRefreshProtocol(capability);full("old or unsupported client retains full progress in background");}
        acceptWorkspaceRefreshProtocol({{"advisoryMediaProgressProtocol",1}});mediaProgressCallback(g_dataRoot);const auto pending=window.take();retireWorkspaceRefreshProtocol();verify(!workspaceRefreshMessageCurrent(pending),"queued old advisory is rejected at dispatch");
        g_shuttingDown=true;mediaProgressCallback(g_dataRoot);verify(window.empty(),"shutdown produces no media notification");g_shuttingDown=false;
        verify(bytes(g_dataRoot/L"config"/L"game-media-downloads.json")==queueBytes,"notification projection does not mutate durable queue");
        verify(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"no external process network or shell escaped fixture");
        writeJsonWithLocalBackup(root/L"media-progress-result.json",{{"ok",true},{"checks",checks},{"scope","real Host helper, real queue, local activity ports and owned native message queue; no WebView COM delivery or full Watcher execution"}},false);return 0;
    }catch(const std::exception& e){writeJsonWithLocalBackup(root/L"media-progress-result.json",{{"ok",false},{"checks",checks},{"error",e.what()}},false);std::cerr<<e.what()<<std::endl;return 1;}
}

static void nativeArtworkObservationFixture(const fs::path& parent) {
    const auto savedRoot=g_dataRoot;
    struct Restore {fs::path root;~Restore(){identityAfterMove={};hostMoveFault={};g_dataRoot=root;}} restore{savedRoot};
    const auto bitmap=[](){BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+900*3*420;BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=900;info.biHeight=420;info.biPlanes=1;info.biBitCount=24;info.biSizeImage=900*3*420;std::string value(file.bfSize,0x55);memcpy(value.data(),&file,sizeof(file));memcpy(value.data()+sizeof(file),&info,sizeof(info));return value;};
    const auto tree=[](const fs::path& root){std::map<std::wstring,std::string> result;for(const auto& file:fs::directory_iterator(custom_steam_library::ioPath(root)))if(file.is_regular_file())result[file.path().filename().wstring()]=bytes(file.path());return result;};
    const auto incoming=bitmap();json rows=json::array();bool safe=true;
    for(const auto* name:{"new-destination-before","changed-existing-before","deleted-existing-before","new-extension-before","empty-clear-before","later-content-during","later-extension-during","replacement-before-commit","unchanged-control","unrelated-game-control","untouched-slot-control"}){
        const std::string mode=name;const auto root=parent/L"observation"/toWide(mode);g_dataRoot=root/L"data";trustedAdmissionFixtureRoot(g_dataRoot);identityAfterMove={};hostMoveFault={};
        const auto grid=root/L"Steam"/L"userdata"/L"1001"/L"config"/L"grid",source=root/L"incoming.bmp",config=g_dataRoot/L"config"/L"commit.json";
        if(mode!="empty-clear-before"){text(grid/L"620p.jpg","old-cover-jpg");text(grid/L"620p.png","old-cover-png");}
        text(grid/L"620.jpg","old-long-jpg");text(grid/L"620.png","old-long-png");text(grid/L"620_hero.png","old-hero");text(grid/L"999p.png","old-other-game");text(source,incoming);text(config,R"({"old":true})");
        const auto initial=tree(grid);const auto oldConfig=bytes(config);auto expected=initial;const bool success=mode.ends_with("-control"),before=mode.ends_with("-before");
        unsigned commits=0,nativeActivations=0;bool activated=false,injected=false;std::string error;
        try {
            NativeSteamArtworkTarget target;target.appId=620;target.grids={{"1001",grid}};
            std::vector<NativeSteamArtworkEdit> edits={{"cover",mode=="empty-clear-before"?fs::path{}:source,mode=="empty-clear-before"}};
            if(mode.starts_with("later-"))edits.push_back({"long",source,false});
            NativeSteamArtworkWrite write(target,edits,&activated);
            if(mode=="new-destination-before"){text(grid/L"620p.bmp","external-new-destination");expected[L"620p.bmp"]="external-new-destination";}
            if(mode=="changed-existing-before"){text(grid/L"620p.png","external-existing");expected[L"620p.png"]="external-existing";}
            if(mode=="deleted-existing-before"){fs::remove(custom_steam_library::ioPath(grid/L"620p.png"));expected.erase(L"620p.png");}
            if(mode=="new-extension-before"){text(grid/L"620p.webp","external-extension");expected[L"620p.webp"]="external-extension";}
            if(mode=="empty-clear-before"){text(grid/L"620p.png","external-after-empty-clear");expected[L"620p.png"]="external-after-empty-clear";}
            if(mode=="unrelated-game-control"){text(grid/L"999p.png","external-other-game");expected[L"999p.png"]="external-other-game";}
            if(mode=="untouched-slot-control"){text(grid/L"620_hero.png","external-hero");expected[L"620_hero.png"]="external-hero";}
            if(mode=="later-content-during")expected[L"620.png"]="external-later-content";
            if(mode=="later-extension-during")expected[L"620.webp"]="external-later-extension";
            if(mode=="replacement-before-commit")expected[L"620p.bmp"]="external-after-activation";
            identityAfterMove=[&](const auto&,const auto& to){
                if(to.find(grid.wstring())==std::wstring::npos)return;
                ++nativeActivations;
                if(injected||!to.ends_with((grid/L"620p.bmp").wstring()))return;
                injected=true;
                if(mode=="later-content-during")text(grid/L"620.png","external-later-content");
                if(mode=="later-extension-during")text(grid/L"620.webp","external-later-extension");
                if(mode=="replacement-before-commit")text(grid/L"620p.bmp","external-after-activation");
            };
            write.write([&]{++commits;writeJsonWithLocalBackup(config,{{"new",true}},false,&activated);});
        } catch(const std::exception& failure) { error=failure.what(); }
        identityAfterMove={};
        if(success){expected.erase(L"620p.jpg");expected.erase(L"620p.png");expected[L"620p.bmp"]=incoming;}
        const auto actual=tree(grid);const bool filesPreserved=actual==expected;
        const bool commitCorrect=success?(commits==1&&activated&&bytes(config)!=oldConfig):(commits==0&&!activated&&bytes(config)==oldConfig);
        const bool conflictCorrect=success?error.empty():!error.empty()&&error.find("发生变化")!=std::string::npos;
        const bool zeroWrites=!before||nativeActivations==0;
        const bool preservedExternalFinal=mode!="replacement-before-commit"||error.find("恢复未完成")!=std::string::npos;
        const bool passed=filesPreserved&&commitCorrect&&conflictCorrect&&zeroWrites&&preservedExternalFinal&&!temporaries(g_dataRoot);
        rows.push_back({{"mode",mode},{"passed",passed},{"error",error},{"filesPreserved",filesPreserved},{"commitCorrect",commitCorrect},{"commits",commits},{"nativeActivations",nativeActivations},{"expectedSuccess",success}});safe=safe&&passed;
    }
    writeJsonWithLocalBackup(parent/L"native-artwork-observation-observed.json",{{"rows",rows},{"scope","Actual NativeSteamArtworkWrite with owned files and constructor/apply/commit boundary modifications; actual owned config writer. No installed Steam or process/device/sleep operations."}},false);
    check(rows.size()==11&&safe,"Native artwork write ignored changed preimages or committed stale state");
}

int wmain(int argc,wchar_t** argv) {
    // Controlled publication receipt at the original account-worker boundary.
    // The Host still owns the real process, pipe, deadline and account reader.
    wchar_t accountFixtureRoot[32768]{};
    const auto accountFixtureSize=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",accountFixtureRoot,32768);

    if(accountFixtureSize&&accountFixtureSize<32768&&argc>=2&&std::wstring(argv[1])==L"--scan-library"){
        try{const auto data=fs::absolute(accountFixtureRoot);const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();check(normalizedPathKey(data).starts_with(normalizedPathKey(evidence)+L"\\"),"Pipeline scan port escaped evidence");const auto response=json::parse(bytes(data/L"account-receipt-worker.json"));check(response.value("pipelineScanPort",false)&&(argc==2||(argc==3&&std::wstring(argv[2])==L"--first-run-defaults")),"Unexpected pipeline scan arguments");text(data/(L"pipeline-scan-call-"+std::to_wstring(GetCurrentProcessId())+L".json"),json{{"argc",argc},{"stateUnchanged",true}}.dump());const auto code=response.value("scanExit",23);if(code)std::cerr<<"owned directory scan failed";return code;}catch(const std::exception& error){std::cerr<<error.what();return 89;}
    }
    if(accountFixtureSize&&accountFixtureSize<32768&&argc==4&&std::wstring(argv[1])==L"--list-steam-accounts"&&std::wstring(argv[2])==L"--output"){
        try{
            const auto data=fs::absolute(accountFixtureRoot),output=fs::absolute(argv[3]);
            const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
            check(normalizedPathKey(data).starts_with(normalizedPathKey(evidence)+L"\\")&&samePath(output,data/L"state"/L"steam-accounts.json"),"Account receipt port escaped owned evidence");
            const auto response=json::parse(bytes(data/L"account-receipt-worker.json"));
            if(response.contains("rawFile"))text(output,response.at("rawFile").get<std::string>());
            std::cout<<response.value("stdout",std::string{});std::cerr<<response.value("stderr",std::string{});return response.value("exitCode",0);
        }catch(const std::exception& error){std::cerr<<error.what();return 94;}
    }

    // Same owned account port, extended to observe actual snapshot planning.
    if(accountFixtureSize&&accountFixtureSize<32768&&argc>2&&std::wstring(argv[1])==L"--plan-add-library-to-steam"){
        try{
            const auto data=fs::absolute(accountFixtureRoot);
            const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
            check(normalizedPathKey(data).starts_with(normalizedPathKey(evidence)+L"\\"),"Account plan port escaped owned evidence");
            fs::path output;json arguments=json::array();for(int i=1;i<argc;++i){arguments.push_back(toUtf8(argv[i]));if(std::wstring(argv[i])==L"--output"&&i+1<argc)output=fs::absolute(argv[i+1]);}
            auto response=json::parse(bytes(data/L"account-receipt-worker.json"));
            std::string selectedAccount="1001";for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==L"--account")selectedAccount=toUtf8(argv[i+1]);
            if(response.contains("plansByAccount")){
                std::string selected="22202";for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==L"--account")selected=toUtf8(argv[i+1]);
                response["planResult"]=response["plansByAccount"].at(selected);
            }
            check(samePath(output,data/L"state"/L"steam-add-plan.json")||(response.contains("commitResult")&&samePath(output,data/L"state"/L"steam-add-plan-1001.json"))||
                ((selectedAccount=="1001"||selectedAccount=="1002")&&response.contains("completionsByAccount")&&response["completionsByAccount"].contains(selectedAccount)&&samePath(output,data/L"state"/toWide("steam-add-plan-"+selectedAccount+".json"))),"Account plan output escaped owned evidence");
            text(data/(L"account-plan-call-"+std::to_wstring(GetCurrentProcessId())+L".json"),json{{"arguments",arguments}}.dump());
            if(!response.value("skipPlanWrite",false))writeJsonWithLocalBackup(output,response.at("planResult"),false);
            if(response.contains("laterPlan"))writeJsonWithLocalBackup(output,response.at("laterPlan"),false);
            const auto normal="STEAM_PLAN_RECEIPT: sha256-v1:"+custom_steam_library::plan_receipt::digest(response.at("planResult"))+"\n";
            std::cout<<response.value("planReceiptText",normal);
            if(response.value("rejectPlanAccount",std::string{})==selectedAccount)return 23;
            return response.value("planExit",0);
        }catch(const std::exception& error){std::cerr<<error.what();return 93;}
    }

    // Same owned boundary for a completed add; never reaches an installed Steam.
    if(accountFixtureSize&&accountFixtureSize<32768&&argc>2&&std::wstring(argv[1])==L"--commit-add-library-to-steam"){
        try{
            const auto data=fs::absolute(accountFixtureRoot);
            const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
            fs::path output;for(int i=2;i+1<argc;++i)if(std::wstring(argv[i])==L"--output")output=fs::absolute(argv[i+1]);
            const auto response=json::parse(bytes(data/L"account-receipt-worker.json"));
            std::string selectedAccount="1001";for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==L"--account")selectedAccount=toUtf8(argv[i+1]);
            const bool multi=response.contains("completionsByAccount");
            check(normalizedPathKey(data).starts_with(normalizedPathKey(evidence)+L"\\")&&
                ((!multi&&samePath(output,data/L"state"/L"steam-add-plan-1001.json"))||(multi&&(selectedAccount=="1001"||selectedAccount=="1002")&&response["completionsByAccount"].contains(selectedAccount)&&samePath(output,data/L"state"/toWide("steam-add-plan-"+selectedAccount+".json")))),"Account commit port escaped owned evidence");
            const auto result=multi?response["completionsByAccount"].at(selectedAccount):response.at("commitResult");
            text(data/(L"account-commit-call-"+std::to_wstring(GetCurrentProcessId())+L".json"),result.dump());
            const auto fault=response.value("faultAccount",std::string{})==selectedAccount?response.value("faultKind",std::string{}):std::string{};
            if(fault=="commit-failed"){std::cerr<<"owned add commit rejected";return 24;}
            if(response.value("publishOutput",false))writeJsonWithLocalBackup(output,result,false);
            if(response.value("publishAccount",true)){
                const auto saved=data/L"state"/toWide("last-steam-add-"+selectedAccount+".json");
                if(response.value("addBindingPort",false)&&response.contains("rawAccountResult")){check(!multi&&selectedAccount=="1001"&&response["rawAccountResult"].is_string(),"Raw add binding port escaped exact owned account");text(saved,response["rawAccountResult"].get<std::string>());}
                else writeJsonWithLocalBackup(saved,response.value("accountResult",result),false);
            }
            if(response.value("publishGlobal",false))writeJsonWithLocalBackup(data/L"state"/L"last-steam-add.json",response.value("globalResult",result),false);
            auto content=result;content.erase("resultPublicationWarnings");
            const auto receipt="STEAM_ADD_RESULT_RECEIPT: sha256-v1:"+custom_steam_library::plan_receipt::digest(content)+"\n";
            if(fault=="missing-receipt")return 0;
            std::cout<<response.value("commitReceiptText",receipt);return response.value("commitExit",0);
        }catch(const std::exception& error){std::cerr<<error.what();return 92;}
    }

    if(accountFixtureSize&&accountFixtureSize<32768&&argc>2&&(std::wstring(argv[1])==L"--commit-refresh-steam-artwork"||std::wstring(argv[1])==L"--plan-delete-library-from-steam"||std::wstring(argv[1])==L"--commit-delete-library-from-steam")){
        try{const auto data=fs::absolute(accountFixtureRoot);const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
            check(normalizedPathKey(data).starts_with(normalizedPathKey(evidence)+L"\\"),"Partial operation escaped evidence");
            fs::path output;std::string account;json args=json::array();for(int i=1;i<argc;++i){args.push_back(toUtf8(argv[i]));if(i+1<argc&&std::wstring(argv[i])==L"--output")output=fs::absolute(argv[i+1]);if(i+1<argc&&std::wstring(argv[i])==L"--account")account=toUtf8(argv[i+1]);}

            const auto response=json::parse(bytes(data/L"account-receipt-worker.json"));
            if(response.value("deleteResumePort",false)){
                const bool planOnly=std::wstring(argv[1])==L"--plan-delete-library-from-steam";
                check((account=="1001"||account=="1002")&&samePath(output,data/L"state"/toWide("steam-delete-plan-"+account+".json"))&&std::wstring(argv[1])!=L"--commit-refresh-steam-artwork","Resume delete port escaped owned account");
                const auto statePath=data/L"delete-resume-state.json";auto state=readJson(statePath,json::object());if(!state.contains("absent"))state["absent"]=json::array();
                bool absent=false,allow=false;for(const auto& id:state["absent"])if(id==account)absent=true;for(int i=1;i<argc;++i)if(std::wstring(argv[i])==L"--allow-already-absent")allow=true;
                auto calls=jsonArrayOr(state,"calls");calls.push_back({{"account",account},{"plan",planOnly},{"allowAbsent",allow},{"wasAbsent",absent}});state["calls"]=calls;writeJsonWithLocalBackup(statePath,state,false);
                if(!planOnly&&response.value("failAccount",std::string{})==account&&!state.value("failureConsumed",false)){state["failureConsumed"]=true;writeJsonWithLocalBackup(statePath,state,false);std::cerr<<"owned second-account commit failure";return 37;}
                if(!planOnly&&absent&&!allow){std::cerr<<"owned strict not-found refusal";return 38;}
                json result={{"schemaVersion",1},{"accountId",account},{"shortcutsVdf",toUtf8((data.parent_path()/L"Steam"/L"userdata"/toWide(account)/L"config"/L"shortcuts.vdf").wstring())},{"libraryState",toUtf8((data/L"state"/L"library-scan.json").wstring())},{"items",json::array()},{"summary",json::object()},{"committed",!planOnly},{"mode",planOnly?"steam-library-delete-plan":"steam-library-delete-commit"},{"alreadyAbsentCount",absent?1:0},{"deletedCount",absent?0:1}};
                if(!planOnly&&!absent){state["absent"].push_back(account);writeJsonWithLocalBackup(statePath,state,false);}
                writeJsonWithLocalBackup(output,result,false);if(!planOnly)std::cout<<"STEAM_DELETE_RESULT_RECEIPT: sha256-v1:"<<custom_steam_library::plan_receipt::addCompletionDigest(result)<<"\n";return 0;
            }
            check(account=="1001"&&(samePath(output,data/L"state"/L"steam-artwork-refresh-1001.json")||samePath(output,data/L"state"/L"steam-delete-plan-1001.json")),"Partial operation targeted unreadable account");
            const bool refresh=std::wstring(argv[1])==L"--commit-refresh-steam-artwork",planOnly=std::wstring(argv[1])==L"--plan-delete-library-from-steam";
            json base={{"schemaVersion",1},{"accountId",account},{"shortcutsVdf",toUtf8((data.parent_path()/L"Steam"/L"userdata"/L"1001"/L"config"/L"shortcuts.vdf").wstring())},{"libraryState",toUtf8((data/L"state"/L"library-scan.json").wstring())},{"items",json::array()},{"summary",json::object()},{"committed",!planOnly},{"mode",refresh?"steam-artwork-refresh":planOnly?"steam-library-delete-plan":"steam-library-delete-commit"}};
            const auto result=planOnly?base:response.value("actionResult",base);text(data/(L"account-extra-call-"+std::to_wstring(GetCurrentProcessId())+L".json"),args.dump());
            if(planOnly){writeJsonWithLocalBackup(output,result,false);return 0;}
            text(data/(L"account-action-commit-"+std::to_wstring(GetCurrentProcessId())+L".json"),result.dump());
            if(response.value("publishActionOutput",true))writeJsonWithLocalBackup(output,response.value("actionOutputResult",result),false);
            if(response.value("publishActionGlobal",false))writeJsonWithLocalBackup(data/L"state"/(refresh?L"last-steam-artwork-refresh.json":L"last-steam-delete.json"),response.value("actionGlobalResult",result),false);
            const auto receipt=std::string(refresh?"STEAM_REFRESH_RESULT_RECEIPT: sha256-v1:":"STEAM_DELETE_RESULT_RECEIPT: sha256-v1:")+custom_steam_library::plan_receipt::addCompletionDigest(result)+"\n";
            std::cout<<response.value("actionReceiptText",receipt);return response.value("actionExit",0);
        }catch(const std::exception& error){std::cerr<<error.what();return 91;}
    }

    // Same-EXE removal port for the real Host removal/task race. The actual
    // Worker removal CLI is covered by the datachain consumer, not this port.
    wchar_t removalRoot[32768]{};
    const auto removalSize=GetEnvironmentVariableW(L"YMCC_MANUAL_REMOVAL_FIXTURE_ROOT",removalRoot,32768);
    if(removalSize&&removalSize<32768&&argc>1&&
        (std::wstring(argv[1])==L"--remove-library-game"||std::wstring(argv[1])==L"--scan-library")){
        try{
            const auto data=custom_steam_library::configuredDataRoot(),control=fs::absolute(removalRoot);
            const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
            check(normalizedPathKey(control).starts_with(normalizedPathKey(evidence)+L"\\"),"Removal port escaped owned evidence");
            g_dataRoot=data;const auto request=readJson(control/L"remove-request.json",json::object());
            if(std::wstring(argv[1])==L"--scan-library"){std::cerr<<"owned rescan failure after confirmed removal";return 36;}
            check(argc==4&&std::wstring(argv[2])==L"--game-dir"&&samePath(fs::path(argv[3]),fs::path(toWide(request.at("directory").get<std::string>()))),"Unexpected removal request");
            if(request.value("reject",false)){std::cerr<<"owned removal rejected before write";return 35;}
            const auto file=data/L"config"/L"manual-overrides.json";auto overrides=readJson(file,json::object());
            for(const auto& key:request.at("removedKeys"))overrides["items"].erase(key.get<std::string>());
            writeJsonWithLocalBackup(file,overrides);std::cout<<"owned confirmed removal";return 0;
        }catch(const std::exception& error){std::cerr<<error.what();return 94;}
    }

    // Controlled manifest/process boundary for the real detached Host task.
    wchar_t fixtureRoot[32768]{},fixtureGate[256]{};
    const auto fixtureSize=GetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_ROOT",fixtureRoot,32768);
    if(fixtureSize&&fixtureSize<32768&&argc>2&&fs::path(argv[1]).is_absolute()){
        try{
            const auto data=custom_steam_library::configuredDataRoot(),control=fs::absolute(fixtureRoot),output=fs::absolute(argv[2]);
            const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
            check(normalizedPathKey(control).starts_with(normalizedPathKey(evidence)+L"\\")&&normalizedPathKey(output).starts_with(normalizedPathKey(data/L"cache"/L"jobs")+L"\\"),"Manual enrichment port escaped owned evidence");
            g_dataRoot=data;admissionFixtureOnly=true;
            json arguments=json::array();for(int i=1;i<argc;++i)arguments.push_back(toUtf8(argv[i]));
            text(control/L"started.json",json{{"arguments",arguments},{"scope","controlled Worker port, not actual recognition/network"}}.dump());
            text(control/toWide(safeArtworkSegment(toUtf8(normalizedPathKey(fs::path(argv[1]))))+".started.json"),arguments.dump());
            const auto gateSize=GetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_GATE",fixtureGate,256);check(gateSize&&gateSize<256,"Missing owned enrichment gate");
            HANDLE gate=OpenEventW(SYNCHRONIZE,FALSE,fixtureGate);check(gate!=nullptr,"Cannot open owned enrichment gate");const auto released=WaitForSingleObject(gate,5000);CloseHandle(gate);check(released==WAIT_OBJECT_0,"Owned enrichment gate not released");
            const auto option=[&](const wchar_t* name){for(int i=3;i<argc;++i)if(std::wstring(argv[i])==name)return i;return 0;};
            const int nameFlag=option(L"--save-name"),idFlag=option(L"--save-id");
            // Model only the documented persistence directives. Final Host
            // writes and its real task lifetime are exercised without stubs.
            if(nameFlag||idFlag){const auto file=data/L"config"/L"manual-overrides.json";auto value=readJson(file,json::object());auto& item=value["items"][toUtf8(normalizedPathKey(fs::path(argv[1])))];if(nameFlag&&nameFlag+1<argc)item["name"]=toUtf8(argv[nameFlag+1]);if(idFlag){item["steamId"]=620;item.erase("idCleared");}writeJsonWithLocalBackup(file,value);}
            json manifest={{"exe",toUtf8(argv[1])},{"match",{{"appId",620},{"storefrontAppId",620},{"formalName","Official resolved title"},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"},{"source","steam-official"}}},{"metadata",json::object()}};
            const auto authority=control.filename().wstring();auto& match=manifest["match"];
            if(authority==L"id-only-old-resolver")match["resolverVersion"]="legacy-name-hint-v1";
            if(authority==L"id-only-offline-label")match["source"]="local-install-metadata-offline";
            if(authority==L"id-only-conflicting-app")match["storefrontAppId"]=777;
            if(authority==L"id-only-missing-provider")match.erase("primaryProvider");
            if(authority==L"id-only-supported-v2")match["resolverVersion"]="steam-official-fuzzy-global-v2";
            if(authority.starts_with(L"requested-")){
                match["igdbId"]=72;match["metadataVerificationStatus"]="metadata-verified";
                if(authority.find(L"steam-mismatch")!=std::wstring::npos){match["appId"]=777;match["storefrontAppId"]=777;}
                if(authority==L"requested-igdb-mismatch")match["igdbId"]=99;
                if(authority==L"requested-explicit-content"){match["matchedContentAppId"]=700;match["baseGameAppId"]=620;match["identityKind"]="steam-base-game-with-explicit-content";match["contentRelation"]="explicit-dlc";}
            }
            text(output/L"owned"/L"manifest.json",manifest.dump());
            std::cout<<"owned enrichment manifest";return authority.starts_with(L"requested-")&&authority.ends_with(L"-partial")?23:0;
        }catch(const std::exception& error){std::cerr<<error.what();return 95;}
    }
    // Controlled Worker port; the real initializer is covered by datachain.
    if (argc == 4 && std::wstring(argv[1]) == L"--cleanup-interrupted-transactions" &&
        std::wstring(argv[2]) == L"--expected-data-root") {
        const auto selected = custom_steam_library::configuredDataRoot();
        if (selected.empty() || !fs::path(argv[3]).is_absolute() || !samePath(selected, argv[3])) return 96;
        if (!fs::exists(selected / L"data-root.json")) trustedAdmissionFixtureRoot(selected);
        std::cout << "owned startup Worker port";
        return 0;
    }
    if(argc==3 && std::wstring(argv[2])==L"--media-progress-notice") return mediaProgressNoticeProbe(fs::absolute(argv[1]));
    if(argc==3 && std::wstring(argv[2])==L"--webview-source") return webviewSourceProbe(fs::absolute(argv[1]));
    if(argc==3 && std::wstring(argv[2])==L"--reply-routing") return replyRoutingProbe(fs::absolute(argv[1]));
    if(argc==3 && std::wstring(argv[2])==L"--library-media-demand") return libraryMediaDemandProbe(fs::absolute(argv[1]));
    if(argc==3 && std::wstring(argv[1])==L"--fixture-worker") return helper(argv[2]);
    if(argc==2&&std::wstring(argv[1])==L"--scan-library"){try{return queueFixtureWorker(argc,argv);}catch(const std::exception& e){std::cerr<<e.what();return 97;}}
    if(argc>2 && std::wstring(argv[2])!=L"--config-storm" && std::wstring(argv[2])!=L"--data-root-admission" && std::wstring(argv[2])!=L"--identity-caller" && std::wstring(argv[2])!=L"--discovery-state" && std::wstring(argv[2])!=L"--editor-queue" && std::wstring(argv[2])!=L"--activity") { try{return queueFixtureWorker(argc,argv);}catch(const std::exception& e){std::cerr<<e.what();return 97;} }
    try {
        check(argc==2 || (argc==3 && (std::wstring(argv[2])==L"--config-storm" || std::wstring(argv[2])==L"--data-root-admission" || std::wstring(argv[2])==L"--identity-caller" || std::wstring(argv[2])==L"--discovery-state" || std::wstring(argv[2])==L"--editor-queue" || std::wstring(argv[2])==L"--activity")),"Pass a fresh isolated root");const auto root=fs::absolute(argv[1]);
        const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
        const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
        check((normalizedPathKey(root).starts_with(normalizedPathKey(build)+L"\\")||normalizedPathKey(root).starts_with(normalizedPathKey(evidence)+L"\\"))&&!fs::exists(root),"Unsafe/reused fixture root");
        wchar_t queueChoice[256]{};SetLastError(ERROR_SUCCESS);
        const auto queueChoiceSize=GetEnvironmentVariableW(L"YMCC_HOST_LIFECYCLE_CASE",queueChoice,256);
        const bool queueChoicePresent=queueChoiceSize!=0||GetLastError()!=ERROR_ENVVAR_NOT_FOUND;
        std::string queueCase;
        if(queueChoicePresent){
            check(argc==2&&queueChoiceSize>0&&queueChoiceSize<256,"Unsupported/empty default queue case selection");
            queueCase=toUtf8(queueChoice);
            check(queueCase=="automatic-no-id-queue-applies-pictures-without-retrying-identity"||
                queueCase=="identity-cooldown-does-not-block-no-id-artwork"||
                queueCase=="manual-refresh-still-excludes-already-added-shortcuts"||
                queueCase=="manual-image-activation-fault-keeps-old-image"||
                queueCase=="manual-image-transient-replace-retries"||
                queueCase=="native-grid-activation-failure-keeps-all-old-extensions"||
                queueCase=="native-grid-success-replaces-only-own-slot"||
                queueCase=="native-modern-horizontal-is-copied-persisted-and-reused-without-steam-read","Unknown lifecycle case selection");
            manualEnrichmentFixtureWorker=selfExecutable().wstring();
        }
        fs::create_directories(root);g_dataRoot=root/L"data";trustedAdmissionFixtureRoot(g_dataRoot);g_labRoot=root;SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",g_dataRoot.c_str());const bool admission=argc==3&&std::wstring(argv[2])==L"--data-root-admission";const bool identityCaller=argc==3&&std::wstring(argv[2])==L"--identity-caller";const bool discoveryState=argc==3&&std::wstring(argv[2])==L"--discovery-state";admissionFixtureOnly=admission||identityCaller||discoveryState||queueChoicePresent;json cases=json::array();
        auto test=[&](const char* name,const std::function<void(const fs::path&)>& action){if(!queueCase.empty()&&queueCase!=name)return;if(discoveryState){if(!std::string(name).starts_with("discovercfg-"))return;wchar_t selected[256]{};const auto n=GetEnvironmentVariableW(L"YMCC_DISCOVERY_STATE_CASE",selected,256);if(n&&n<256&&std::string(name)!=toUtf8(selected))return;}if(identityCaller&&!std::string(name).starts_with("identity-caller-"))return;if(identityCaller){wchar_t choice[256]{};const auto n=GetEnvironmentVariableW(L"YMCC_HOST_IDENTITY_SELFTEST_CASE",choice,256);if(n&&n<256&&std::string(name)!=toUtf8(choice))return;}if(admission&&!std::string(name).starts_with("root-admission-"))return;if(admission){wchar_t selected[256]{};const auto count=GetEnvironmentVariableW(L"YMCC_HOST_ROOT_SELFTEST_CASE",selected,256);if(count&&count<256&&std::string(name)!=toUtf8(selected))return;}if(argc==3 && !admission && !identityCaller && !discoveryState && !std::string(name).starts_with("storm-") && !std::string(name).starts_with("json-") &&
                std::string(name)!="empty-root-config-always-enters-first-run-retry-path" && std::string(name)!="explicitly-cleared-roots-do-not-enter-first-run-discovery")return;hostMoveFault={};identityAfterMove={};identityAfterFlush={};configIoMode=0;configIoTarget.clear();configIoPartial=false;g_shuttingDown=false;g_cancelRequested=false;const auto p=root/std::to_wstring(cases.size());fs::create_directories(p);if(!admission){g_dataRoot=p/L"data";g_labRoot=p;trustedAdmissionFixtureRoot(g_dataRoot);}if(discoveryState){g_steamTarget=json::object();g_worker=p/L"worker-never-launched.exe";admissionProcesses=0;admissionNetwork=0;admissionShell=0;}
            try { action(p);if(!queueCase.empty())check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Selected queue attempted an external process/network/shell");cases.push_back({{"name",name},{"passed",true}}); } catch(const std::exception& e) { cases.push_back({{"name",name},{"passed",false},{"error",toUtf8(toWide(e.what()))}}); }
            hostMoveFault={};identityAfterMove={};identityAfterFlush={};configIoMode=0;configIoTarget.clear();configIoPartial=false;g_shuttingDown=true;requestTaskCancellation();closeWorkerProcessJob();joinHostBackgroundTasks();g_shuttingDown=false;g_cancelRequested=false;g_window=nullptr;};

        if(argc==3&&std::wstring(argv[2])==L"--activity"){
            admissionFixtureOnly=true;activityOnly=true;
            activityWindow=CreateWindowExW(0,L"STATIC",L"owned activity fixture",WS_POPUP,0,0,10,10,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            check(activityWindow!=nullptr,"Owned hidden activity window creation failed");g_window=activityWindow;
            auto pump=[&](DWORD ms){unsigned ticks=0;const auto until=GetTickCount64()+ms;do{MSG message{};while(PeekMessageW(&message,activityWindow,0,0,PM_REMOVE)){if(message.message==WM_TIMER&&message.wParam==GAMEPAD_TIMER){++ticks;windowProcedure(activityWindow,WM_TIMER,message.wParam,message.lParam);}}if(ms)Sleep(1);}while(GetTickCount64()<until);return ticks;};
            auto activityCase=[&](const char* name,const std::function<void()>& fn){
                KillTimer(activityWindow,GAMEPAD_TIMER);pump(0);g_launchOptions=LaunchOptions{};g_updateHealthOnly=false;activityVisible=true;activityIconic=false;activityDisconnected=false;activityForeground=nullptr;activityState={};activityStarts=activityStops=activityReads=activityForegroundReads=activityDefocusOnRead=0;
                windowProcedure(activityWindow,WM_ACTIVATEAPP,FALSE,0);
                try{fn();cases.push_back({{"name",name},{"passed",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"error",e.what()}});}
                windowProcedure(activityWindow,WM_ACTIVATEAPP,FALSE,0);KillTimer(activityWindow,GAMEPAD_TIMER);pump(0);
            };
            activityCase("activity-background-removes-real-70ms-timer",[&]{
                SetTimer(activityWindow,GAMEPAD_TIMER,70,nullptr);windowProcedure(activityWindow,WM_ACTIVATEAPP,FALSE,0);const auto ticks=pump(180);
                check(ticks==0&&activityReads==0,"Background still wakes on the gamepad timer");
            });
            activityCase("activity-foreground-starts-once-and-duplicate-events-do-not-reset",[&]{
                activityForeground=activityWindow;windowProcedure(activityWindow,WM_ACTIVATEAPP,TRUE,0);windowProcedure(activityWindow,WM_ACTIVATEAPP,TRUE,0);windowProcedure(activityWindow,WM_SIZE,0,0);
                check(activityStarts==1,"Foreground did not create exactly one owned input timer");check(pump(100)>=1&&activityReads>=1,"Foreground input timer did not actually fire/read its local port");
            });
            activityCase("activity-minimized-hidden-and-late-timer-cannot-poll",[&]{
                activityForeground=activityWindow;windowProcedure(activityWindow,WM_ACTIVATEAPP,TRUE,0);activityIconic=true;windowProcedure(activityWindow,WM_SIZE,SIZE_MINIMIZED,0);pump(0);
                const auto reads=activityReads;windowProcedure(activityWindow,WM_TIMER,GAMEPAD_TIMER,0);check(pump(150)==0&&activityReads==reads,"Minimized or late input timer still polls");
                activityIconic=false;activityVisible=false;windowProcedure(activityWindow,WM_SHOWWINDOW,FALSE,0);windowProcedure(activityWindow,WM_ACTIVATEAPP,TRUE,0);check(pump(100)==0,"Hidden foreground claim restarted a timer");
                json presentationRows=json::array();bool presentationSafe=true;
                const auto priorClosing=g_closeAfterWrites.load(),priorShutdown=g_shuttingDown.load();
                struct PresentationRestore{bool closing,shutdown;~PresentationRestore(){presentationProbe=false;g_closeAfterWrites=closing;g_shuttingDown=shutdown;closeSplash();}} presentationRestore{priorClosing,priorShutdown};
                for(const auto* route:{"navigation","splash-fallback"})for(const auto* scenario:{"hidden","minimized","other-app","pending-close","closing-visible","shutdown","health","foreground"}){
                    const std::string mode=scenario,path=route;activityVisible=mode!="hidden"&&mode!="pending-close"&&mode!="health";activityIconic=mode=="minimized";activityForeground=mode=="foreground"||mode=="closing-visible"||mode=="shutdown"?activityWindow:nullptr;
                    g_closeAfterWrites=mode=="pending-close"||mode=="closing-visible";g_shuttingDown=mode=="shutdown";g_updateHealthOnly=mode=="health";
                    const bool beforeVisible=activityVisible,beforeIconic=activityIconic;const auto beforeForeground=activityForeground;
                    const auto splash=CreateWindowExW(0,L"STATIC",L"owned inert splash",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(splash!=nullptr,"Owned splash allocation failed");g_splash=splash;
                    presentationShow=presentationForeground=presentationActive=presentationKeyboard=0;presentationProbe=true;
                    if(path=="navigation")finishWorkspaceNavigationPresentation();else splashProcedure(splash,WM_TIMER,SPLASH_FALLBACK_TIMER_ID,0);
                    presentationProbe=false;const bool cleaned=g_splash==nullptr&&!IsWindow(splash);const bool foregroundControl=mode=="foreground";
                    const bool ownershipPreserved=activityVisible==beforeVisible&&activityIconic==beforeIconic&&activityForeground==beforeForeground;
                    const bool allowedFocus=path=="navigation"&&foregroundControl;
                    const bool actionsSafe=allowedFocus?presentationForeground==1&&presentationActive==1&&presentationKeyboard==1:presentationForeground==0&&presentationActive==0&&presentationKeyboard==0&&presentationShow==0;
                    const bool passed=cleaned&&ownershipPreserved&&actionsSafe;presentationSafe=presentationSafe&&passed;
                    presentationRows.push_back({{"route",path},{"scenario",mode},{"splashDestroyed",cleaned},{"ownershipPreserved",ownershipPreserved},{"showCalls",presentationShow},{"foregroundCalls",presentationForeground},{"activeCalls",presentationActive},{"keyboardFocusCalls",presentationKeyboard},{"passed",passed}});
                }
                g_closeAfterWrites=priorClosing;g_shuttingDown=priorShutdown;g_updateHealthOnly=false;activityVisible=true;activityIconic=false;activityForeground=nullptr;
                writeJsonWithLocalBackup(root/L"navigation-presentation.json",presentationRows,false);
                check(presentationSafe,"Delayed workspace presentation reclaimed background input ownership");
            });
            activityCase("activity-focus-return-requires-complete-neutral-before-input",[&]{
                activityForeground=activityWindow;windowProcedure(activityWindow,WM_ACTIVATEAPP,TRUE,0);activityState.Gamepad.wButtons=XINPUT_GAMEPAD_A;activityState.Gamepad.sThumbLX=25000;pollGamepad();
                check(g_previousGamepadButtons==0&&g_gamepadNavigationX==0,"Held buttons or stick navigated on focus return");
                activityState.Gamepad.wButtons=0;pollGamepad();check(g_gamepadNavigationX==0,"Held stick passed neutral admission");
                activityState.Gamepad.sThumbLX=0;activityState.Gamepad.sThumbRX=25000;activityState.Gamepad.bRightTrigger=255;pollGamepad();activityState.Gamepad.wButtons=XINPUT_GAMEPAD_A;pollGamepad();check(g_previousGamepadButtons==0,"Right stick/trigger did not hold the release barrier");
                activityState={};pollGamepad();activityState.Gamepad.wButtons=XINPUT_GAMEPAD_A;pollGamepad();check(g_previousGamepadButtons==XINPUT_GAMEPAD_A,"Fresh input was not restored after neutral");
                windowProcedure(activityWindow,WM_ACTIVATEAPP,FALSE,0);windowProcedure(activityWindow,WM_ACTIVATEAPP,TRUE,0);activityState={};activityState.Gamepad.wButtons=0x0c00;pollGamepad();activityState.Gamepad.wButtons|=XINPUT_GAMEPAD_A;pollGamepad();check((g_previousGamepadButtons&XINPUT_GAMEPAD_A)!=0,"Reserved non-action bits permanently held the release barrier");
            });
            activityCase("activity-disconnect-reconnect-cannot-replay-a-held-button",[&]{
                activityForeground=activityWindow;windowProcedure(activityWindow,WM_ACTIVATEAPP,TRUE,0);activityState={};pollGamepad();activityState.Gamepad.wButtons=XINPUT_GAMEPAD_A;pollGamepad();activityDisconnected=true;pollGamepad();activityDisconnected=false;pollGamepad();
                check(g_previousGamepadButtons==0,"Reconnect replayed a held button");activityState={};pollGamepad();activityState.Gamepad.wButtons=XINPUT_GAMEPAD_A;pollGamepad();check(g_previousGamepadButtons==XINPUT_GAMEPAD_A,"Reconnect did not recover after release");
            });
            activityCase("activity-parent-mode-and-health-check-never-own-a-poller",[&]{
                activityForeground=activityWindow;g_launchOptions.inputOwner=L"parent";g_launchOptions.integration=L"YeManCC";g_launchOptions.protocol=L"1";g_launchOptions.parentPid=GetCurrentProcessId();windowProcedure(activityWindow,WM_ACTIVATEAPP,TRUE,0);windowProcedure(activityWindow,WM_TIMER,GAMEPAD_TIMER,0);
                check(activityStarts==0&&activityReads==0,"Parent semantic mode started an XInput owner");g_launchOptions=LaunchOptions{};g_updateHealthOnly=true;windowProcedure(activityWindow,WM_ACTIVATEAPP,TRUE,0);windowProcedure(activityWindow,WM_TIMER,GAMEPAD_TIMER,0);check(activityStarts==0&&activityReads==0,"Health-only child started an XInput owner");
            });
            auto parentSemanticMode=[&]{g_launchOptions.inputOwner=L"parent";g_launchOptions.integration=L"YeManCC";g_launchOptions.protocol=L"1";g_launchOptions.parentPid=GetCurrentProcessId();activityForeground=activityWindow;};
            auto semantic=[&](const wchar_t* action){COPYDATASTRUCT data{0x594D4343,static_cast<DWORD>((wcslen(action)+1)*sizeof(wchar_t)),const_cast<wchar_t*>(action)};return windowProcedure(activityWindow,WM_COPYDATA,reinterpret_cast<WPARAM>(activityWindow),reinterpret_cast<LPARAM>(&data));};
            activityCase("activity-parent-semantic-only-enters-the-actual-foreground-library",[&]{
                parentSemanticMode();check(semantic(L"accept")==TRUE,"Foreground parent semantic action was lost");activityForeground=nullptr;check(semantic(L"accept")==FALSE,"Background library accepted a late semantic confirmation");
                activityForeground=activityWindow;activityVisible=false;check(semantic(L"back")==FALSE,"Hidden library consumed a parent action");activityVisible=true;activityIconic=true;check(semantic(L"edit")==FALSE,"Minimized library consumed a parent action");activityIconic=false;g_updateHealthOnly=true;check(semantic(L"accept")==FALSE,"Health-only library accepted semantic input");g_updateHealthOnly=false;g_shuttingDown=true;const auto closing=semantic(L"accept");g_shuttingDown=false;check(closing==FALSE,"Closing library accepted semantic input");
            });
            activityCase("activity-parent-semantic-keeps-authentication-and-payload-fences",[&]{
                parentSemanticMode();++g_launchOptions.parentPid;check(semantic(L"accept")==FALSE,"Other parent PID entered");--g_launchOptions.parentPid;check(semantic(L"launch-unlisted")==FALSE,"Unknown semantic action entered");
                wchar_t embedded[]={L'a',L'c',L'c',L'e',L'p',L't',0,L'x',0};COPYDATASTRUCT data{0x594D4343,sizeof embedded,embedded};check(windowProcedure(activityWindow,WM_COPYDATA,reinterpret_cast<WPARAM>(activityWindow),reinterpret_cast<LPARAM>(&data))==FALSE,"Embedded NUL broadened a semantic action");
                data.dwData=0;check(windowProcedure(activityWindow,WM_COPYDATA,reinterpret_cast<WPARAM>(activityWindow),reinterpret_cast<LPARAM>(&data))==FALSE,"Wrong protocol tag entered");data.dwData=0x594D4343;data.cbData=256;check(windowProcedure(activityWindow,WM_COPYDATA,reinterpret_cast<WPARAM>(activityWindow),reinterpret_cast<LPARAM>(&data))==FALSE,"Oversized semantic action entered");
                check(semantic(L"navigate-right")==TRUE&&activityReads==0&&activityStarts==0,"Valid parent route did not remain semantic-only");
            });
            activityCase("activity-parent-semantic-rechecks-focus-and-rejects-retired-window",[&]{
                parentSemanticMode();activityForegroundReads=0;activityDefocusOnRead=2;check(semantic(L"accept")==FALSE,"Focus lost during admission still dispatched an action");activityDefocusOnRead=0;g_window=nullptr;const auto retired=semantic(L"accept");g_window=activityWindow;check(retired==FALSE,"Retired host window accepted an action");
            });
            activityCase("activity-focus-wakes-only-download-owner",[&]{
                using namespace ymcc::media::downloads;struct Events{HANDLE main,library,owner;~Events(){CloseHandle(main);CloseHandle(library);CloseHandle(owner);}void reset(){ResetEvent(main);ResetEvent(library);ResetEvent(owner);}} events{CreateEventW(nullptr,TRUE,FALSE,eventName(g_dataRoot,L"main").c_str()),CreateEventW(nullptr,TRUE,FALSE,eventName(g_dataRoot,L"library").c_str()),CreateEventW(nullptr,TRUE,FALSE,eventName(g_dataRoot,L"owner").c_str())};check(events.main&&events.library&&events.owner,"create isolated named route events");
                Queue queue(g_dataRoot);queue.enqueue(json::array({{{"appId",570},{"verified",true}}}));queue.configure(false,"");queue.resource("");events.reset();windowProcedure(activityWindow,WM_ACTIVATEAPP,TRUE,0);
                const json observed={{"owner",WaitForSingleObject(events.owner,0)==WAIT_OBJECT_0},{"main",WaitForSingleObject(events.main,0)==WAIT_OBJECT_0},{"library",WaitForSingleObject(events.library,0)==WAIT_OBJECT_0}};writeJsonWithLocalBackup(root/L"focus-route.json",observed,false);
                check(observed["owner"]==true&&observed["main"]==false&&observed["library"]==false,"focus-only hint woke unchanged UI status watchers");events.reset();queue.resource("cpu-busy",95);
                check(WaitForSingleObject(events.main,0)==WAIT_OBJECT_0&&WaitForSingleObject(events.library,0)==WAIT_OBJECT_0&&WaitForSingleObject(events.owner,0)==WAIT_TIMEOUT,"real resource publication lost either UI or woke its owner again");
            });
            activityCase("activity-close-with-accepted-write-wakes-only-download-owner",[&]{
                using namespace ymcc::media::downloads;struct Events{HANDLE main,library,owner;~Events(){CloseHandle(main);CloseHandle(library);CloseHandle(owner);}} events{CreateEventW(nullptr,TRUE,FALSE,eventName(g_dataRoot,L"main").c_str()),CreateEventW(nullptr,TRUE,FALSE,eventName(g_dataRoot,L"library").c_str()),CreateEventW(nullptr,TRUE,FALSE,eventName(g_dataRoot,L"owner").c_str())};check(events.main&&events.library&&events.owner,"create isolated close route events");
                const auto prior=g_acceptedWrites.load();const bool closing=g_closeAfterWrites.load();struct Restore{unsigned count;bool closing;~Restore(){g_acceptedWrites=count;g_closeAfterWrites=closing;}} restore{prior,closing};g_launchOptions.inputOwner=L"host";g_launchOptions.parentPid=0;g_acceptedWrites=1;windowProcedure(activityWindow,WM_CLOSE,0,0);
                const json observed={{"owner",WaitForSingleObject(events.owner,0)==WAIT_OBJECT_0},{"main",WaitForSingleObject(events.main,0)==WAIT_OBJECT_0},{"library",WaitForSingleObject(events.library,0)==WAIT_OBJECT_0},{"acceptedWrites",g_acceptedWrites.load()},{"closingAfterWrites",g_closeAfterWrites.load()},{"ownedWindowStillExists",IsWindow(activityWindow)!=FALSE}};writeJsonWithLocalBackup(root/L"close-route.json",observed,false);
                check(observed["owner"]==true&&observed["main"]==false&&observed["library"]==false,"closing an accepted write woke unchanged UI watchers");check(observed["acceptedWrites"]==1&&observed["closingAfterWrites"]==true&&observed["ownedWindowStillExists"]==true,"close route discarded accepted work or destroyed its owned window early");
            });
            activityCase("activity-focus-no-longer-causes-native-status-read-bursts",[&]{
                const auto originalRoot=g_dataRoot;struct RestoreRoot{fs::path prior;~RestoreRoot(){g_dataRoot=prior;}} restoreRoot{originalRoot};g_dataRoot=root/L"focus-observer-data";
                using namespace ymcc::media::downloads;Queue queue(g_dataRoot);json inventory=json::array();for(int id=1;id<=256;++id)inventory.push_back({{"appId",id},{"verified",true}});queue.enqueue(inventory);queue.configure(false,"");std::atomic<unsigned> mainReads{0},libraryReads{0};json metrics;
                {Watcher main(g_dataRoot,L"main",[&]{(void)Queue(g_dataRoot).snapshot();++mainReads;});Watcher library(g_dataRoot,L"library",[&]{(void)Queue(g_dataRoot).snapshot();++libraryReads;});IO_COUNTERS before{},after{};check(GetProcessIoCounters(GetCurrentProcess(),&before)!=FALSE,"focus observer IO start");const auto started=GetTickCount64();for(int n=0;n<6;++n){windowProcedure(activityWindow,WM_ACTIVATEAPP,n%2?TRUE:FALSE,0);Sleep(100);}check(GetProcessIoCounters(GetCurrentProcess(),&after)!=FALSE,"focus observer IO end");metrics={{"inventory",256},{"elapsedMs",GetTickCount64()-started},{"focusMessages",6},{"mainSnapshotReads",mainReads.load()},{"librarySnapshotReads",libraryReads.load()},{"readOperations",after.ReadOperationCount-before.ReadOperationCount},{"readBytes",after.ReadTransferCount-before.ReadTransferCount},{"writeOperations",after.WriteOperationCount-before.WriteOperationCount}};}
                writeJsonWithLocalBackup(root/L"focus-observer-cost.json",metrics,false);check(mainReads==0&&libraryReads==0&&metrics["readOperations"]==0&&metrics["writeOperations"]==0,"focus burst caused real unchanged queue reads/publications");
            });
            activityCase("activity-does-not-start-external-processes-network-or-shell",[&]{check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Activity consumer escaped local ports");});
            KillTimer(activityWindow,GAMEPAD_TIMER);DestroyWindow(activityWindow);activityWindow=nullptr;g_window=nullptr;activityOnly=false;
            bool ok=true;for(const auto& row:cases)ok=ok&&row.value("passed",false);const json report={{"ok",ok},{"cases",cases},{"scope","actual production window messages/poller and real timers on a hidden owned window; focus/visibility/XInput ports only"}};
            writeJsonWithLocalBackup(root/L"activity-results.json",report,false);std::cout<<report.dump()<<std::endl;return ok?0:1;
        }
        if(argc==3&&std::wstring(argv[2])==L"--editor-queue"){
            admissionFixtureOnly=true;
            auto editorCase=[&](const char* name,const std::function<void()>& fn){wchar_t selected[256]{};const auto count=GetEnvironmentVariableW(L"YMCC_HOST_EDITOR_QUEUE_CASE",selected,256);if(count&&count<256&&std::string(name)!=toUtf8(selected))return;try{fn();cases.push_back({{"name",name},{"passed",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"error",e.what()}});}};
            const auto game=g_dataRoot/L"games"/L"test.exe";text(game,"local fixture");
            const auto key=toUtf8(normalizedPathKey(game));
            auto submission=[&](const std::string& name){return json{{"operations",json::array({{{"command","saveManualName"},{"arguments",{{"executable",toUtf8(game.wstring())},{"name",name}}}}})}};};
            auto waitQueue=[&]{const auto until=GetTickCount64()+5000;while(g_taskRunning.load()&&GetTickCount64()<until)Sleep(5);check(!g_taskRunning.load(),"Native command FIFO did not drain");};
            editorCase("editor-queue-submission-validates-all-commands-before-write",[&]{
                const auto before=bytes(g_dataRoot/L"config"/L"manual-overrides.json");auto args=submission("must-not-write");args["operations"].push_back({{"command","commit"},{"arguments",json::object()}});
                bool rejected=false;try{executeWorkspaceCommand("saveEditor",args);}catch(...){rejected=true;}
                check(rejected&&bytes(g_dataRoot/L"config"/L"manual-overrides.json")==before,"Invalid batch partially wrote user choices");
                const auto file=g_dataRoot/L"config"/L"manual-overrides.json";
                writeJsonWithLocalBackup(g_dataRoot/L"state"/L"library-scan.json",{{"schemaVersion",1},{"games",json::array({{{"gameDirectory",toUtf8(game.parent_path().wstring())},{"primaryExecutable",toUtf8(game.wstring())},{"status","ready"}}})}},false);
                const auto gateName=L"Local\\YMCC-IdBoundary-"+std::to_wstring(GetCurrentProcessId());HANDLE gate=CreateEventW(nullptr,TRUE,TRUE,gateName.c_str());check(gate,"Cannot create owned ID validation gate");
                const auto previousWorker=g_worker;const bool previousQueue=g_scrapeQueueRunning.load();
                struct Restore{HANDLE gate;fs::path worker;bool queue;~Restore(){joinHostBackgroundTasks();CloseHandle(gate);g_worker=worker;g_scrapeQueueRunning=queue;manualEnrichmentFixtureWorker.clear();SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_ROOT",nullptr);SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_GATE",nullptr);}} restore{gate,previousWorker,previousQueue};
                g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();g_scrapeQueueRunning=true;initializeWorkerProcessJob();SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_GATE",gateName.c_str());
                json boundaryRows=json::array();bool allRejected=true;
                const json invalid=json::array({{{"field","steamId"},{"id","00"}},{{"field","steamId"},{"id","2147483648"}},{{"field","steamId"},{"id","9223372036854775807"}},{{"field","steamId"},{"id","9223372036854775808"}},{{"field","steamId"},{"id","1e3"}},{{"field","steamId"},{"id","0x26c"}},{{"field","igdbId"},{"id","000"}},{{"field","igdbId"},{"id","9223372036854775808"}},{{"field","igdbId"},{"id","18446744073709551615"}}});
                for(const auto& test:invalid)for(const auto* route:{"direct","editor"}){
                    const auto field=test["field"].get<std::string>(),id=test["id"].get<std::string>();const auto control=g_dataRoot/L"enrichment-control"/toWide(std::to_string(boundaryRows.size()));fs::create_directories(control);SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_ROOT",control.c_str());
                    const json retained={{"name","Original boundary name"},{"steamId",620},{"cover",{{"path","keep-cover"}}},{"wallpaper",{{"path","keep-wallpaper"}}},{"futureField",{{"keep",true}}}};writeJsonWithLocalBackup(file,{{"schemaVersion",1},{"items",{{key,retained}}}});const auto original=bytes(file);
                    json identify={{"executable",toUtf8(game.wstring())},{"manualName","Must not replace name"},{"deferSnapshot",true}};identify[field]=id;
                    bool refused=false;std::string error;try{if(std::string(route)=="direct")executeWorkspaceCommand("identifyOne",identify);else{auto batch=submission("Must not partially save name");batch["operations"].push_back({{"command","identifyOne"},{"arguments",identify}});executeWorkspaceCommand("saveEditor",batch);}}catch(const std::exception& e){refused=true;error=e.what();}
                    joinHostBackgroundTasks();const bool preserved=bytes(file)==original,noWorker=!fs::exists(control/L"started.json"),released=g_manualIdentifyJobs.empty()&&g_hostThreads.empty()&&g_acceptedWrites.load()==0;
                    const bool good=refused&&preserved&&noWorker&&released;allRejected=allRejected&&good;boundaryRows.push_back({{"field",field},{"id",id},{"route",route},{"rejected",refused},{"error",error},{"originalPreserved",preserved},{"workerNotStarted",noWorker},{"ownersReleased",released},{"passed",good}});
                }
                json validRows=json::array();bool validAccepted=true;
                for(const auto& test:json::array({{{"field","steamId"},{"id","1"}},{{"field","steamId"},{"id","000620"}},{{"field","steamId"},{"id","2147483647"}},{{"field","igdbId"},{"id","1"}},{{"field","igdbId"},{"id","00072"}},{{"field","igdbId"},{"id","9223372036854775807"}}})){
                    const auto field=test["field"].get<std::string>(),id=test["id"].get<std::string>();const auto control=g_dataRoot/L"enrichment-control"/toWide("valid-"+std::to_string(validRows.size()));fs::create_directories(control);SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_ROOT",control.c_str());
                    writeJsonWithLocalBackup(file,{{"schemaVersion",1},{"items",{{key,{{"name","Valid control"},{"cover",{{"path","keep-cover"}}}}}}}});json arguments={{"executable",toUtf8(game.wstring())},{"deferSnapshot",true}};arguments[field]=id;bool accepted=true;try{executeWorkspaceCommand("identifyOne",arguments);}catch(...){accepted=false;}joinHostBackgroundTasks();const auto actual=readJson(file,json::object())["items"][key];
                    const bool good=accepted&&fs::exists(control/L"started.json")&&jsonIntegerOr(actual,field.c_str(),0)==std::stoll(id)&&actual["cover"]["path"]=="keep-cover"&&g_manualIdentifyJobs.empty()&&g_hostThreads.empty();validAccepted=validAccepted&&good;validRows.push_back({{"field",field},{"id",id},{"accepted",accepted},{"actual",actual},{"passed",good}});
                }
                writeJsonWithLocalBackup(g_dataRoot/L"id-input-boundary-observed.json",{{"invalid",boundaryRows},{"valid",validRows}},false);
                check(boundaryRows.size()==18&&allRejected&&validRows.size()==6&&validAccepted,"Manual identity invalid input mutated data or valid bounds were rejected");

            });
            editorCase("editor-queue-two-submissions-use-FIFO-without-busy-rejection",[&]{
                std::unique_lock barrier(g_taskBoundaryMutex);beginCommand(8101,"saveEditor",submission("first"));beginCommand(8102,"saveEditor",submission("second"));
                check(g_taskRunning.load()&&g_acceptedWrites.load()==2,"Accepted submissions were rejected or lost");
                barrier.unlock();waitQueue();check(readJson(g_dataRoot/L"config"/L"manual-overrides.json",json::object())["items"][key]["name"]=="second","Native saves executed out of order");
                check(g_acceptedWrites.load()==0,"Completed submissions retained a write owner");
            });
            editorCase("editor-queue-a-failed-request-does-not-starve-the-next",[&]{
                std::unique_lock barrier(g_taskBoundaryMutex);beginCommand(8103,"saveEditor",submission(""));beginCommand(8104,"saveEditor",submission("after-failure"));barrier.unlock();waitQueue();
                check(readJson(g_dataRoot/L"config"/L"manual-overrides.json",json::object())["items"][key]["name"]=="after-failure"&&g_acceptedWrites.load()==0,"Failure stopped the queue or leaked accepted writes");
            });
            editorCase("editor-queue-close-request-drains-already-accepted-writes",[&]{
                std::unique_lock barrier(g_taskBoundaryMutex);beginCommand(8105,"saveEditor",submission("saved-after-close"));g_closeAfterWrites=true;
                beginCommand(8106,"saveEditor",submission("rejected-after-close"));check(g_acceptedWrites.load()==1,"Closing accepted new work");barrier.unlock();waitQueue();g_closeAfterWrites=false;
                check(readJson(g_dataRoot/L"config"/L"manual-overrides.json",json::object())["items"][key]["name"]=="saved-after-close"&&g_acceptedWrites.load()==0,"Closing discarded an accepted submission");
                RefreshNoticeWindow reopened;g_launchOptions=LaunchOptions{};g_launchOptions.parentPid=0;
                std::unique_lock reopenBarrier(g_taskBoundaryMutex);beginCommand(8111,"saveEditor",submission("first-retained-save"));g_closeAfterWrites=true;
                windowProcedure(reopened.window,WM_SHOWWINDOW,TRUE,SW_PARENTOPENING);const bool parentOpeningKeepsClose=g_closeAfterWrites.load();
                windowProcedure(reopened.window,WM_SHOWWINDOW,TRUE,0);const bool reopenedSession=!g_closeAfterWrites.load()&&g_acceptedWrites.load()==1;
                beginCommand(8112,"saveEditor",submission("new-save-after-reopen"));const bool acceptedNewWrite=g_acceptedWrites.load()==2;
                reopenBarrier.unlock();waitQueue();joinHostBackgroundTasks();
                const auto saved=readJson(g_dataRoot/L"config"/L"manual-overrides.json",json::object())["items"][key]["name"];
                MSG stale{};unsigned unguardedClose=0;while(PeekMessageW(&stale,reopened.window,WM_CLOSE,WM_CLOSE,PM_REMOVE))++unguardedClose;
                const bool queueDrained=g_acceptedWrites.load()==0&&!g_taskRunning.load();
                g_closeAfterWrites=false;windowProcedure(reopened.window,WM_APP+0x46,0,0);windowProcedure(reopened.window,WM_APP+0x46,0,0);const bool staleIgnored=IsWindow(reopened.window)!=FALSE;
                std::unique_lock finalBarrier(g_taskBoundaryMutex);beginCommand(8113,"saveEditor",submission("saved-after-final-close"));windowProcedure(reopened.window,WM_CLOSE,0,0);const bool finalCloseRetains=g_closeAfterWrites.load()&&g_acceptedWrites.load()==1&&IsWindow(reopened.window);
                finalBarrier.unlock();waitQueue();joinHostBackgroundTasks();const auto finalSaved=readJson(g_dataRoot/L"config"/L"manual-overrides.json",json::object())["items"][key]["name"];
                MSG drained{};const bool guardedCloseQueued=PeekMessageW(&drained,reopened.window,WM_APP+0x46,WM_APP+0x46,PM_REMOVE)!=FALSE;
                if(guardedCloseQueued)windowProcedure(reopened.window,drained.message,drained.wParam,drained.lParam);
                const bool finalClosed=guardedCloseQueued&&!IsWindow(reopened.window);
                json observation={{"parentOpeningKeepsClose",parentOpeningKeepsClose},{"reopenedSession",reopenedSession},{"acceptedNewWrite",acceptedNewWrite},{"savedName",saved},{"queueDrained",queueDrained},{"unguardedCloseMessages",unguardedClose},{"staleDrainedIgnored",staleIgnored},{"finalCloseRetainsAccepted",finalCloseRetains},{"finalSavedName",finalSaved},{"guardedCloseQueued",guardedCloseQueued},{"finalWindowDestroyed",finalClosed}};
                writeJsonWithLocalBackup(root/L"reopen-retained-write.json",observation,false);g_closeAfterWrites=false;
                check(parentOpeningKeepsClose&&reopenedSession&&acceptedNewWrite&&saved=="new-save-after-reopen"&&queueDrained&&unguardedClose==0&&staleIgnored&&finalCloseRetains&&finalSaved=="saved-after-final-close"&&finalClosed,"Reopened retained-write host kept a stale close request");
            });
            editorCase("editor-queue-duplicate-Steam-requests-share-one-native-slot",[&]{
                std::unique_lock barrier(g_taskBoundaryMutex);beginCommand(8107,"saveEditor",submission("dedupe-barrier"));
                json args={{"selectionMode","all-steam-accounts"},{"selectedGameDirectories",json::array({"fixture-b","fixture-a"})}};beginCommand(8108,"commit",args);std::reverse(args["selectedGameDirectories"].begin(),args["selectedGameDirectories"].end());beginCommand(8109,"commit",args);
                {std::lock_guard<std::mutex> queueLock(g_commandQueueMutex);
                    check(g_commitWaiters.size()==1&&g_commitWaiters.begin()->second.size()==2&&g_acceptedWrites.load()==2,"Duplicate Steam request got a second write slot");
                    // Inspect admission only: no Steam transaction is executed.
                    std::queue<WorkspaceQueuedCommand> retained;while(!g_commandQueue.empty()){auto item=std::move(g_commandQueue.front());g_commandQueue.pop();if(item.command=="commit")--g_acceptedWrites;else retained.push(std::move(item));}g_commandQueue=std::move(retained);g_commitWaiters.clear();
                }
                barrier.unlock();waitQueue();check(g_acceptedWrites.load()==0,"Duplicate admission leaked a write owner");
            });
            editorCase("editor-queue-reuses-committed-all-account-receipt-without-replanning",[&]{
                g_steamTarget={{"selectionMode","all-steam-accounts"}};
                json receipt={{"schemaVersion",1},{"libraryState",toUtf8((g_dataRoot/L"state"/L"library-scan.json").wstring())},{"mode","steam-library-add-commit-all-accounts"},{"allSteamAccounts",true},{"summary",json::object()},{"items",json::array({{{"gameDirectory",toUtf8(game.parent_path().wstring())},{"primaryExecutable",toUtf8(game.wstring())},{"status","added-to-steam"}}})}};
                const auto file=g_dataRoot/L"state"/L"last-steam-add-all-accounts.json";writeJsonWithLocalBackup(file,receipt,false);
                const auto cached=newestCachedSteamPlan(false);check(cached.source==file&&cached.document["items"][0]["status"]=="added-to-steam","Completed card fell back to an old plan");
                receipt["allSteamAccounts"]="true";writeJsonWithLocalBackup(file,receipt,false);check(newestCachedSteamPlan(false).source.empty(),"Malformed all-account proof was accepted");receipt["allSteamAccounts"]=true;writeJsonWithLocalBackup(file,receipt,false);
            });
            editorCase("editor-queue-all-account-receipt-cannot-borrow-a-selected-account",[&]{
                g_steamTarget={{"selectionMode","explicit-game-directories"},{"accountId","999"}};check(newestCachedSteamPlan(false).source.empty(),"Single account consumed all-account receipt");g_steamTarget=json::object();
            });
            editorCase("editor-queue-primary-choice-must-precede-dependent-writes",[&]{
                const auto file=g_dataRoot/L"config"/L"manual-overrides.json";const auto prior=bytes(file);auto args=submission("must-not-partially-save");args["operations"].push_back({{"command","setPrimary"},{"arguments",{{"gameDirectory",toUtf8(game.parent_path().wstring())},{"executable",toUtf8(game.wstring())}}}});
                bool rejected=false;try{executeWorkspaceCommand("saveEditor",args);}catch(const std::exception& error){rejected=std::string(error.what()).find("主程序选择")!=std::string::npos;}
                check(rejected&&bytes(file)==prior,"Primary operation order was not rejected before dependent writes");
            });
            editorCase("editor-queue-primary-invalid-target-is-rejected-without-a-worker",[&]{
                bool rejected=false;try{executeWorkspaceCommand("saveEditor",{{"operations",json::array({{{"command","setPrimary"},{"arguments",{{"gameDirectory",toUtf8(game.parent_path().wstring())},{"executable",toUtf8((game.parent_path()/L"missing.exe").wstring())}}}}})}});}catch(const std::exception& error){rejected=std::string(error.what()).find("存在的 EXE")!=std::string::npos;}
                check(rejected&&admissionProcesses==0,"Invalid primary was not validated before worker admission");
            });
            editorCase("editor-queue-repeated-name-does-not-rewrite-config-or-backup",[&]{
                const auto file=g_dataRoot/L"config"/L"manual-overrides.json";auto overrides=readJson(file,json::object());overrides["items"][key]["name"]="Already saved";overrides["items"][key]["updatedAt"]=1;writeJsonWithLocalBackup(file,overrides);const auto prior=bytes(file),backup=bytes(fs::path(file.wstring()+L".bak"));
                const auto result=executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(game.wstring())},{"name","Already saved"},{"deferSnapshot",true}});
                check(result.value("saved",false)&&bytes(file)==prior&&bytes(fs::path(file.wstring()+L".bak"))==backup,"An already saved name rewrote config/backup or changed its timestamp");
            });
            editorCase("editor-queue-explicit-name-clear-preserves-data-and-retries",[&]{
                const auto file=g_dataRoot/L"config"/L"manual-overrides.json",rootRecord=g_dataRoot/L"data-root.json";
                auto overrides=readJson(file,json::object());const json owned={{"name","Owned custom name"},{"steamId",620},{"wallpaper",{{"path","keep local wallpaper"}}},{"futureAnnotation",{{"keep",true}}}};
                overrides["items"][key]=owned;overrides["items"]["other-owned-exe"]={{"name","Other game"},{"future",7}};writeJsonWithLocalBackup(file,overrides);
                const json args={{"executable",toUtf8(game.wstring())},{"name",""},{"clearName",true},{"deferSnapshot",true}};
                const auto result=executeWorkspaceCommand("saveManualName",args);auto actual=readJson(file,json::object());const auto item=actual["items"][key];
                check(result.value("saved",false)&&!item.contains("name"),"Explicit name clear was not committed");
                check(item["steamId"]==owned["steamId"]&&item["wallpaper"]==owned["wallpaper"]&&item["futureAnnotation"]==owned["futureAnnotation"]&&actual["items"]["other-owned-exe"]==overrides["items"]["other-owned-exe"],"Name clear changed another override, identity, wallpaper or annotation");
                const auto prior=bytes(file),backup=bytes(fs::path(file.wstring()+L".bak"));const auto repeated=executeWorkspaceCommand("saveManualName",args);
                check(repeated.value("unchanged",false)&&bytes(file)==prior&&bytes(fs::path(file.wstring()+L".bak"))==backup,"Repeated name clear rewrote config/backup");
                for(const auto& invalid:std::vector<json>{{{"executable",toUtf8(game.wstring())},{"name",""},{"deferSnapshot",true}},{{"executable",toUtf8(game.wstring())},{"name","conflicting name"},{"clearName",true},{"deferSnapshot",true}}}){
                    bool rejected=false;try{executeWorkspaceCommand("saveManualName",invalid);}catch(...){rejected=true;}check(rejected&&bytes(file)==prior,"Ambiguous name clear changed configuration");
                }
                const auto record=bytes(rootRecord);text(rootRecord,"{");bool rejected=false;try{executeWorkspaceCommand("saveManualName",args);}catch(...){rejected=true;}text(rootRecord,record);
                check(rejected&&bytes(file)==prior,"Name clear bypassed current root admission");
                actual["items"][key]=owned;writeJsonWithLocalBackup(file,actual);const auto retrySource=bytes(file);hostMoveFault=[&](const auto&,const auto& to)->DWORD{return to==file.wstring()?ERROR_ACCESS_DENIED:0;};
                rejected=false;try{executeWorkspaceCommand("saveManualName",args);}catch(...){rejected=true;}hostMoveFault={};
                check(rejected&&bytes(file)==retrySource,"Failed name clear reported success or changed original data");
                const auto retried=executeWorkspaceCommand("saveManualName",args);check(retried.value("saved",false)&&!readJson(file,json::object())["items"][key].contains("name"),"Explicit clear retry did not commit");
            });
            for(const auto* variant:{"id-only-later-name","manual-later-clear","id-only-automatic-name","manual-later-name","id-only-later-id-clear","id-only-failed-id-clear","id-only-old-resolver","id-only-offline-label","id-only-conflicting-app","id-only-missing-provider","id-only-cache-unacknowledged","id-only-supported-v2"})editorCase((std::string("editor-queue-manual-enrichment-")+variant).c_str(),[&,variant]{
                const auto originalCase=[&]{
                const std::string kind=variant;const auto file=g_dataRoot/L"config"/L"manual-overrides.json",control=g_dataRoot/L"enrichment-control"/toWide(kind);fs::create_directories(control);
                const auto gateName=L"Local\\YMCC-ManualEnrichment-"+std::to_wstring(GetCurrentProcessId())+L"-"+toWide(kind);HANDLE gate=CreateEventW(nullptr,TRUE,FALSE,gateName.c_str());check(gate!=nullptr,"Cannot create owned enrichment gate");
                const auto previousWorker=g_worker;const bool previousQueue=g_scrapeQueueRunning.load();
                struct Restore {HANDLE gate;fs::path worker;bool queue;~Restore(){SetEvent(gate);joinHostBackgroundTasks();CloseHandle(gate);g_worker=worker;g_scrapeQueueRunning=queue;manualEnrichmentFixtureWorker.clear();SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_ROOT",nullptr);SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_GATE",nullptr);}} restore{gate,previousWorker,previousQueue};
                g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();g_scrapeQueueRunning=true;initializeWorkerProcessJob();
                SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_ROOT",control.c_str());SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_GATE",gateName.c_str());
                auto overrides=readJson(file,json::object());const bool named=kind.starts_with("manual-");overrides["items"][key]={{"steamId",620},{"wallpaper",{{"path","preserve owned wallpaper"}}},{"futureAnnotation",{{"keep",true}}}};if(named)overrides["items"][key]["name"]="Original accepted name";writeJsonWithLocalBackup(file,overrides);
                const bool authorityCase=kind=="id-only-old-resolver"||kind=="id-only-offline-label"||kind=="id-only-conflicting-app"||kind=="id-only-missing-provider"||kind=="id-only-cache-unacknowledged"||kind=="id-only-supported-v2";
                const auto authorityCache=g_dataRoot/L"cache"/L"identity"/toWide(safeArtworkSegment(toUtf8(normalizedPathKey(game)))+".json");
                if(kind=="id-only-cache-unacknowledged")text(authorityCache,json{{"schemaVersion",99},{"executable",toUtf8(game.wstring())},{"futureNote","preserve unrecognized authority"}}.dump());
                const auto authorityCacheBefore=bytes(authorityCache);
                const bool started=startManualIdentifyTask({{"executable",toUtf8(game.wstring())},{"manualName",named?"Original accepted name":""},{"steamId","620"}});
                const auto deadline=GetTickCount64()+3000;while(!fs::exists(control/L"started.json")&&GetTickCount64()<deadline)Sleep(5);
                const bool workerStarted=fs::exists(control/L"started.json");
                json clearReceipt=json::object(),clearIntent=json::object();const auto identityCache=g_dataRoot/L"cache"/L"identity"/toWide(safeArtworkSegment(toUtf8(normalizedPathKey(game)))+".json");const auto originalCache=bytes(identityCache);bool clearRejected=false;
                if(workerStarted&&(kind=="id-only-later-id-clear"||kind=="id-only-failed-id-clear")){
                    if(kind=="id-only-failed-id-clear")hostMoveFault=[&](const auto&,const auto& to)->DWORD{return to==file.wstring()?ERROR_ACCESS_DENIED:0;};
                    try{clearReceipt=executeWorkspaceCommand("clearManualSteamId",{{"executable",toUtf8(game.wstring())},{"deferSnapshot",true}});}catch(...){clearRejected=true;}hostMoveFault={};clearIntent=readJson(file,json::object())["items"][key];
                }

                if(workerStarted&&!authorityCase&&kind!="id-only-later-id-clear"&&kind!="id-only-failed-id-clear"){if(kind=="manual-later-clear")executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(game.wstring())},{"name",""},{"clearName",true},{"deferSnapshot",true}});else if(kind!="id-only-automatic-name")executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(game.wstring())},{"name","Later accepted name"},{"deferSnapshot",true}});}
                SetEvent(gate);joinHostBackgroundTasks();const auto actual=readJson(file,json::object())["items"][key];const auto receipt=readJson(control/L"started.json",json::object());
                writeJsonWithLocalBackup(control/L"observed.json",{{"variant",kind},{"started",started},{"workerStarted",workerStarted},{"entry",actual},{"worker",receipt}},false);
                check(started&&workerStarted,"Original detached manual task did not reach its controlled Worker");
                if(authorityCase){
                    if(kind=="id-only-supported-v2")check(jsonStringOr(actual,"identityStatus")=="ready"&&actual.value("steamId",0)==620&&custom_steam_library::identity_cache::recordValid(custom_steam_library::identity_cache::readSnapshot(authorityCache),toUtf8(game.wstring()),unixTimeMsHost()),"Supported original identity proof was not acknowledged");
                    else{check(jsonStringOr(actual,"identityStatus")!="ready"&&actual.value("steamId",0)==620,"Unacknowledged identity result became ready or replaced the accepted ID");check(bytes(authorityCache)==authorityCacheBefore,"Refused manual identity altered retained cache");}
                    check(actual["wallpaper"]==overrides["items"][key]["wallpaper"]&&actual["futureAnnotation"]==overrides["items"][key]["futureAnnotation"]&&g_manualIdentifyJobs.empty()&&g_hostThreads.empty(),"Manual identity receipt lost retained data or task ownership");return;
                }
                if(kind=="id-only-later-id-clear"){
                    check(clearReceipt.value("saved",false)&&!clearRejected&&clearIntent.value("idCleared",false),"Explicit ID clear was not accepted before old completion");
                    check(actual==clearIntent,"Late identity enrichment rewrote an accepted ID clear");check(bytes(identityCache)==originalCache,"Late identity enrichment published cache after accepted clear");
                    check(g_manualIdentifyJobs.empty()&&g_hostThreads.empty(),"Cleared enrichment retained task resources");return;
                }
                if(kind=="id-only-failed-id-clear"){
                    check(clearRejected&&!clearIntent.value("idCleared",false),"Failed ID clear changed current identity");check(actual.value("steamId",0)==620&&jsonStringOr(actual,"formalName")=="Official resolved title","Failed clear retired valid accepted enrichment");check(g_manualIdentifyJobs.empty()&&g_hostThreads.empty(),"Failed clear retained task resources");return;
                }
                const bool cleared=kind=="manual-later-clear"||kind=="id-only-automatic-name";
                check(cleared?!actual.contains("name"):jsonStringOr(actual,"name")=="Later accepted name","Late manual enrichment replaced the current name intent");
                check(jsonStringOr(actual,"formalName")=="Official resolved title"&&actual["wallpaper"]==overrides["items"][key]["wallpaper"]&&actual["futureAnnotation"]==overrides["items"][key]["futureAnnotation"],"Enrichment lost formal metadata or changed unrelated fields");
                for(const auto& arg:jsonArrayOr(receipt,"arguments"))check(arg!="--save-name"&&arg!="--save-id","Enrichment Worker still receives stale persistence directives");
                check(g_manualIdentifyJobs.empty()&&g_hostThreads.empty(),"Completed enrichment retained its task resources");
                };originalCase();
                if(std::string(variant)=="id-only-later-id-clear"){
                    json observed=json::array();bool safe=true;
                    for(const bool reject:{false,true}){
                        const auto control=g_dataRoot/L"removal-control"/(reject?L"failed":L"confirmed");fs::create_directories(control);
                        const auto directory=g_dataRoot/L"removal-games"/L"parent";
                        const std::vector<fs::path> executables={directory/L"current.exe",directory/L"bin"/L"old-primary.exe",directory/L"independent"/L"child.exe",directory.parent_path()/L"parent-sibling"/L"peer.exe"};
                        const auto gateName=L"Local\\YMCC-ManualRemoval-"+std::to_wstring(GetCurrentProcessId())+(reject?L"-failed":L"-confirmed");
                        HANDLE gate=CreateEventW(nullptr,TRUE,FALSE,gateName.c_str());check(gate,"Cannot create owned removal gate");
                        const auto previousWorker=g_worker;const bool previousQueue=g_scrapeQueueRunning.load();
                        struct Restore{HANDLE gate;fs::path worker;bool queue;~Restore(){SetEvent(gate);joinHostBackgroundTasks();CloseHandle(gate);g_worker=worker;g_scrapeQueueRunning=queue;manualEnrichmentFixtureWorker.clear();for(const auto* n:{L"YMCC_MANUAL_ENRICHMENT_FIXTURE_ROOT",L"YMCC_MANUAL_ENRICHMENT_FIXTURE_GATE",L"YMCC_MANUAL_REMOVAL_FIXTURE_ROOT"})SetEnvironmentVariableW(n,nullptr);}} restore{gate,previousWorker,previousQueue};
                        g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();g_scrapeQueueRunning=true;initializeWorkerProcessJob();
                        SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_ROOT",control.c_str());SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_GATE",gateName.c_str());SetEnvironmentVariableW(L"YMCC_MANUAL_REMOVAL_FIXTURE_ROOT",control.c_str());
                        const auto file=g_dataRoot/L"config"/L"manual-overrides.json";auto overrides=readJson(file,json::object());json keys=json::array(),cacheBefore=json::array();
                        for(const auto& exe:executables){text(exe,"owned inert executable");const auto k=toUtf8(normalizedPathKey(exe));keys.push_back(k);overrides["items"][k]={{"steamId",620},{"name","accepted choice"},{"wallpaper",{{"path","keep"}}},{"futureField",42}};
                            const auto cache=g_dataRoot/L"cache"/L"identity"/toWide(safeArtworkSegment(k)+".json");cacheBefore.push_back(bytes(cache));}
                        writeJsonWithLocalBackup(file,overrides);
                        for(const auto& exe:executables){check(startManualIdentifyTask({{"executable",toUtf8(exe.wstring())},{"steamId","620"}}),"Removal control task not accepted");
                            const auto marker=control/toWide(safeArtworkSegment(toUtf8(normalizedPathKey(exe)))+".started.json");const auto deadline=GetTickCount64()+3000;while(!fs::exists(custom_steam_library::ioPath(marker))&&GetTickCount64()<deadline)Sleep(5);check(fs::exists(custom_steam_library::ioPath(marker)),"Removal control Worker did not start");}
                        std::vector<BackgroundJob> jobs;{std::lock_guard<std::mutex> lock(g_backgroundJobsMutex);for(const auto& k:keys)for(const auto& job:g_manualIdentifyJobs)if(job->key==k.get<std::string>())jobs.push_back(job);}
                        check(jobs.size()==4,"Removal fixture did not retain all real task owners");
                        text(control/L"remove-request.json",json{{"directory",toUtf8(directory.wstring())},{"reject",reject},{"removedKeys",json::array({keys[0],keys[1]})}}.dump());
                        bool removalRejected=false;std::string error;try{executeWorkspaceCommand("removeGame",{{"gameDirectory",toUtf8(directory.wstring())}});}catch(const std::exception& e){removalRejected=true;error=e.what();}
                        const auto acceptedState=readJson(file,json::object());SetEvent(gate);joinHostBackgroundTasks();const auto actual=readJson(file,json::object());
                        json row={{"reject",reject},{"commandRejected",removalRejected},{"error",error},{"entries",actual["items"]},{"cancelled",json::array()}};bool passed=removalRejected&&error.find(reject?"owned removal rejected":"owned rescan failure")!=std::string::npos;
                        for(size_t i=0;i<keys.size();++i){const auto k=keys[i].get<std::string>();const bool removed=!reject&&i<2;row["cancelled"].push_back(jobs[i]->cancelRequested.load());
                            const auto cache=g_dataRoot/L"cache"/L"identity"/toWide(safeArtworkSegment(k)+".json");
                            if(removed)passed=passed&&jobs[i]->cancelRequested.load()&&!actual["items"].contains(k)&&bytes(cache)==cacheBefore[i].get<std::string>();
                            else passed=passed&&!jobs[i]->cancelRequested.load()&&jsonStringOr(actual["items"][k],"formalName")=="Official resolved title"&&actual["items"][k]["wallpaper"]==overrides["items"][k]["wallpaper"]&&actual["items"][k]["futureField"]==42;}
                        passed=passed&&g_manualIdentifyJobs.empty()&&g_hostThreads.empty();row["passed"]=passed;row["acceptedState"]=acceptedState;observed.push_back(row);safe=safe&&passed;
                    }
                    writeJsonWithLocalBackup(g_dataRoot/L"removal-manual-observed.json",observed,false);
                    check(safe,"Confirmed removal allowed late manual identity writes or retired another game");return;
                }
                if(std::string(variant)!="id-only-conflicting-app")return;
                json requestObserved=json::array();bool requestSafe=true,feedbackSafe=true;
                for(const auto* mode:{"requested-steam-mismatch","requested-igdb-mismatch","requested-steam-match","requested-igdb-match","requested-explicit-content","requested-steam-mismatch-partial","requested-steam-match-partial"}){
                    const std::string kind=mode;const bool igdb=kind.find("igdb-")!=std::string::npos,partial=kind.ends_with("-partial"),mismatch=kind.find("mismatch")!=std::string::npos;
                    const auto control=g_dataRoot/L"enrichment-control"/toWide(kind),file=g_dataRoot/L"config"/L"manual-overrides.json";fs::create_directories(control);
                    const auto gateName=L"Local\\YMCC-ManualReply-"+std::to_wstring(GetCurrentProcessId())+L"-"+toWide(kind);HANDLE gate=CreateEventW(nullptr,TRUE,FALSE,gateName.c_str());check(gate,"Cannot create owned manual reply gate");
                    const auto previousWorker=g_worker;const bool previousQueue=g_scrapeQueueRunning.load();
                    struct Restore {HANDLE gate;fs::path worker;bool queue;~Restore(){SetEvent(gate);joinHostBackgroundTasks();CloseHandle(gate);g_worker=worker;g_scrapeQueueRunning=queue;manualEnrichmentFixtureWorker.clear();SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_ROOT",nullptr);SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_GATE",nullptr);}} restore{gate,previousWorker,previousQueue};
                    g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();g_scrapeQueueRunning=true;initializeWorkerProcessJob();SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_ROOT",control.c_str());SetEnvironmentVariableW(L"YMCC_MANUAL_ENRICHMENT_FIXTURE_GATE",gateName.c_str());
                    writeJsonWithLocalBackup(g_dataRoot/L"state"/L"library-scan.json",{{"schemaVersion",1},{"games",json::array({{{"gameDirectory",toUtf8(game.parent_path().wstring())},{"primaryExecutable",toUtf8(game.wstring())},{"status","ready"}}})}},false);
                    const auto seed=workspaceJobsRoot()/L"manual-reply-seed"/L"manifest.json";
                    writeJsonWithLocalBackup(seed,{{"exe",toUtf8(game.wstring())},{"match",{{"appId",620},{"storefrontAppId",620},{"igdbId",72},{"formalName","Current verified identity"},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"metadataVerificationStatus","metadata-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"},{"source","steam-official"}}},{"metadata",json::object()}},false);
                    check(persistResolvedIdentityCache(seed),"Cannot seed original verified cache");
                    const auto cachePath=g_dataRoot/L"cache"/L"identity"/toWide(safeArtworkSegment(toUtf8(normalizedPathKey(game)))+".json");const auto cacheBefore=custom_steam_library::identity_cache::readSnapshot(cachePath);
                    auto value=readJson(file,json::object());const json retained={{"name","User chosen label"},{"steamId",883710},{"igdbId",99},{"storefrontSteamId",883710},{"formalName","Old metadata"},{"steamVerificationStatus","steam-verified"},{"metadataVerificationStatus","metadata-verified"},{"steamIdCandidate",777},{"wallpaper",{{"path","preserve user wallpaper"}}},{"futureAnnotation",{{"keep",true}}}};value["items"][key]=retained;writeJsonWithLocalBackup(file,value,false);
                    json arguments={{"executable",toUtf8(game.wstring())},{"deferSnapshot",true}};arguments[igdb?"igdbId":"steamId"]=igdb?"72":kind=="requested-explicit-content"?"700":"620";
                    const auto previousWindow=g_window.load();
                    struct EventRestore{HWND window;~EventRestore(){joinHostBackgroundTasks();manualReplyCapture=false;g_window=window;}} eventRestore{previousWindow};
                    {std::lock_guard<std::mutex> lock(manualReplyEventMutex);manualReplyEvents=json::array();}
                    g_window=reinterpret_cast<HWND>(INT_PTR{1});manualReplyCapture=true;
                    const auto accepted=executeWorkspaceCommand("identifyOne",arguments);const auto until=GetTickCount64()+3000;while(!fs::exists(control/L"started.json")&&GetTickCount64()<until)Sleep(5);const bool reached=fs::exists(control/L"started.json");const auto pending=readJson(file,json::object())["items"][key];
                    const bool pendingClean=jsonStringOr(pending,"identityStatus")=="pending-network"&&!pending.contains("formalName")&&!pending.contains("storefrontSteamId")&&!pending.contains("steamIdCandidate")&&jsonStringOr(pending,"steamVerificationStatus")!="steam-verified"&&jsonStringOr(pending,"metadataVerificationStatus")!="metadata-verified";
                    SetEvent(gate);joinHostBackgroundTasks();const auto actual=readJson(file,json::object())["items"][key],receipt=readJson(control/L"started.json",json::object());const auto cacheAfter=custom_steam_library::identity_cache::readSnapshot(cachePath);
                    const bool cacheValid=hostIdentityRecordValid(cacheAfter,toUtf8(game.wstring()),unixTimeMsHost());const bool originalCache=custom_steam_library::identity_cache::sameSnapshot(cacheBefore,cacheAfter);
                    const auto expectedManifest=fs::path(toWide(receipt["arguments"][1].get<std::string>()))/L"owned"/L"manifest.json";
                    const bool outputBound=cacheValid&&samePath(fs::path(toWide(jsonStringOr(cacheAfter.value,"manifestPath"))),expectedManifest)&&jsonIntegerOr(jsonObjectOr(cacheAfter.value,"match"),"appId",0)==620;
                    const bool chosen=mismatch?(igdb?(jsonIntegerOr(actual,"igdbId",0)==72&&jsonIntegerOr(actual,"steamId",0)==0):jsonIntegerOr(actual,"steamId",0)==620):jsonIntegerOr(actual,"steamId",0)==620;
                    const bool outcome=mismatch?(jsonStringOr(actual,"identityStatus")!="ready"&&originalCache):(partial?(jsonStringOr(actual,"identityStatus")!="ready"&&outputBound):(jsonStringOr(actual,"identityStatus")=="ready"&&outputBound));
                    json events;{std::lock_guard<std::mutex> lock(manualReplyEventMutex);events=manualReplyEvents;}
                    json terminal=json::object();for(const auto& event:events)if(jsonStringOr(event,"event")=="manual-identify-complete"||jsonStringOr(event,"event")=="manual-identify-failed")terminal=event;
                    const bool feedback=partial?jsonStringOr(terminal,"event")=="manual-identify-failed":
                        jsonStringOr(terminal,"event")=="manual-identify-complete"&&jsonStringOr(terminal,"identityStatus")==jsonStringOr(actual,"identityStatus")&&
                        (!mismatch||(!jsonStringOr(terminal,"error").empty()&&jsonStringOr(terminal,"error")==jsonStringOr(actual,"lastError")));
                    feedbackSafe=feedbackSafe&&feedback;
                    const bool preserved=actual["name"]==retained["name"]&&actual["wallpaper"]==retained["wallpaper"]&&actual["futureAnnotation"]==retained["futureAnnotation"]&&g_manualIdentifyJobs.empty()&&g_hostThreads.empty();
                    requestObserved.push_back({{"variant",kind},{"events",events},{"feedbackCorrect",feedback},{"accepted",accepted},{"workerReached",reached},{"pending",pending},{"pendingFlagsReset",pendingClean},{"actual",actual},{"chosenIdPreserved",chosen},{"outcomeCorrect",outcome},{"cacheValid",cacheValid},{"originalCachePreserved",originalCache},{"matchingOutputPublished",outputBound},{"retainedDataAndOwners",preserved}});requestSafe=requestSafe&&reached&&pendingClean&&chosen&&outcome&&preserved;
                }
                writeJsonWithLocalBackup(g_dataRoot/L"manual-request-reply-observed.json",requestObserved,false);
                // Bind the actual cache writer to the same request, including
                // its own fresh manifest read at publication time.
                const auto directManifest=workspaceJobsRoot()/L"manual-reply-probe"/L"manifest.json";
                writeJsonWithLocalBackup(directManifest,{{"exe",toUtf8(game.wstring())},{"match",{{"appId",777},{"storefrontAppId",777},{"formalName","Unrelated official identity"},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"}}},{"metadata",json::object()}},false);
                const auto directCache=g_dataRoot/L"cache"/L"identity"/toWide(safeArtworkSegment(toUtf8(normalizedPathKey(game)))+".json");const auto directBefore=custom_steam_library::identity_cache::readSnapshot(directCache);
                const auto publishForRequest=[&](auto writer){if constexpr(std::is_invocable_r_v<bool,decltype(writer),const fs::path&,const json&>)return writer(directManifest,json{{"steamId","620"}});else return writer(directManifest);};
                const bool directAccepted=publishForRequest(&persistResolvedIdentityCache);const bool directPreserved=custom_steam_library::identity_cache::sameSnapshot(directBefore,custom_steam_library::identity_cache::readSnapshot(directCache));
                writeJsonWithLocalBackup(g_dataRoot/L"direct-cache-request-observed.json",{{"accepted",directAccepted},{"originalCachePreserved",directPreserved},{"requestedSteamId",620},{"manifestSteamId",777}},false);
                check(requestObserved.size()==7&&requestSafe&&!directAccepted&&directPreserved,"Manual identity request/result mismatch changed accepted choice or cache");
                check(feedbackSafe,"Manual identity completion receipt contradicts persisted outcome");
            });
            for(const auto* mode:{"busy-coalesces","normal-open","cancelled","shutdown"})editorCase((std::string("editor-queue-followup-")+mode).c_str(),[&,mode]{
                const std::string kind=mode;const auto owned=root/toWide(std::string("followup-")+mode);const auto oldData=g_dataRoot,oldLab=g_labRoot,oldWorker=g_worker;const auto oldSequence=g_scrapeBatchSequence.load();
                HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(entered&&release,"Cannot allocate owned followup gates");
                struct Restore {HANDLE entered,release;fs::path data,lab,worker;~Restore(){SetEvent(release);joinHostBackgroundTasks();hostMoveFault={};g_dataRoot=data;g_labRoot=lab;g_worker=worker;g_shuttingDown=false;g_cancelRequested=false;CloseHandle(entered);CloseHandle(release);}} restore{entered,release,oldData,oldLab,oldWorker};
                g_dataRoot=owned/L"data";g_labRoot=owned;g_worker=owned/L"never-run.exe";trustedAdmissionFixtureRoot(g_dataRoot);const auto cached=snapshotCacheFixture(owned,0);g_steamTarget=json::object();
                std::atomic<bool> gated{false};hostMoveFault=[&](const auto&,const auto& to)->DWORD{if(to.find(L"scrape-history")!=std::wstring::npos&&!gated.exchange(true)){SetEvent(entered);if(WaitForSingleObject(release,5000)!=WAIT_OBJECT_0)return ERROR_TIMEOUT;}return 0;};
                check(startScrapeQueue(false),"Original queue did not start");check(WaitForSingleObject(entered,3000)==WAIT_OBJECT_0&&g_scrapeQueueRunning.load(),"Actual queue did not reach owned publication gate");
                BackgroundJob current;{std::lock_guard<std::mutex> lock(g_backgroundJobsMutex);for(const auto& candidate:g_scrapeQueueJobs)if(candidate&&candidate->key=="library")current=candidate;}
                check(current&&!current->cancelRequested.load(),"No live owned queue job at followup request");
                for(int n=0;n<20;++n)check(!(kind=="normal-open"?startScrapeQueue(false):requestOwnedScrapeFollowup(false)),"Busy queue started a parallel pass");
                check(!current->cancelRequested.load(),"Automatic followup cancelled the running queue");
                if(kind=="cancelled")requestTaskCancellation();if(kind=="shutdown")g_shuttingDown=true;
                SetEvent(release);const auto drainUntil=GetTickCount64()+10000;
                for(;;){joinHostBackgroundTasks();bool pendingThreads=false;{std::lock_guard<std::mutex> lock(g_hostThreadsMutex);pendingThreads=!g_hostThreads.empty();}if(!pendingThreads&&!g_scrapeQueueRunning.load())break;check(GetTickCount64()<drainUntil,"Actual queued followup did not drain");Sleep(1);}hostMoveFault={};
                const auto batches=g_scrapeBatchSequence.load()-oldSequence;json histories=json::array();std::error_code ec;
                for(const auto& entry:fs::directory_iterator(scrapeHistoryRoot(),ec))if(entry.path().extension()==L".json")histories.push_back(readJson(entry.path(),json::object()));
                writeJsonWithLocalBackup(owned/L"observed.json",{{"mode",kind},{"batches",batches},{"histories",histories},{"running",g_scrapeQueueRunning.load()},{"jobs",g_scrapeQueueJobs.size()}},false);
                check(batches==(kind=="busy-coalesces"?2:1),"Busy automatic queue lost explicit follow-up or repeated an unrequested pass");
                check(!g_scrapeQueueRunning.load()&&g_scrapeQueueJobs.empty(),"Followup queue did not release its owner");
                if(kind=="busy-coalesces")for(const auto& h:histories)check(jsonStringOr(h,"mode")=="automatic-first-pass","Followup borrowed manual force or reset policy");
            });
            editorCase("editor-queue-no-external-Steam-process-network-or-shell",[&]{check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Queue consumer escaped isolation");});
            joinHostBackgroundTasks();
            bool ok=true;for(const auto& row:cases)ok=ok&&row.value("passed",false);
            const json report={{"ok",ok},{"cases",cases},{"scope","actual production native FIFO and editor submission; local isolated files, external process/network/shell denied"}};
            writeJsonWithLocalBackup(root/L"editor-queue-results.json",report,false);std::cout<<report.dump()<<std::endl;return ok?0:1;
        }
        if(discoveryState){
        const auto plan=[](){return json{{"schemaVersion",1},{"mode","steam-library-add-plan"},{"libraryState",toUtf8((g_dataRoot/L"state"/L"library-scan.json").wstring())},{"summary",{{"readyToAdd",0}}},{"items",json::array()},{"unknown",{{"note","keep"}}}};};
        test("discovercfg-root-record-exact-byte-reuse",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,4);const auto file=g_dataRoot/L"data-root.json";const auto original=bytes(file);const auto parsed=json::parse(original);const auto fixed=fs::last_write_time(file);json stages=json::array();
            const auto consume=[&](const char* name,bool ready){const auto observed=hostDataRootReadiness();check(observed.ready==ready,(std::string("Root reuse stage ")+name+": "+observed.reason).c_str());stages.push_back({{"name",name},{"ready",ready}});};
            const auto replace=[&](const std::string& raw){text(file,raw);fs::last_write_time(file,fixed);};
            consume("warm-valid-root",true);auto detached=*readHostRootRecord(file,1u<<20);detached["schemaVersion"]=99;check((*readHostRootRecord(file,1u<<20))["schemaVersion"]==parsed["schemaVersion"],"Caller modified retained parsed root");stages.push_back({{"name","returned-object-is-detached"}});
            auto unknown=parsed;unknown["schemaVersion"]=9;const auto changed=unknown.dump();check(changed.size()==original.size(),"Same-size root mutation changed workload size");replace(changed);consume("same-size-same-mtime-future-schema",false);check(bytes(file)==changed,"Future root was rewritten");replace(original);consume("repaired-root-same-mtime",true);
            auto wrong=parsed;auto rootText=wrong["dataRoot"].template get<std::string>();rootText.back()=rootText.back()=='z'?'y':'z';wrong["dataRoot"]=rootText;replace(wrong.dump());consume("same-size-same-mtime-foreign-root",false);replace(original);consume("foreign-root-repaired",true);
            HANDLE held=CreateFileW(custom_steam_library::ioPath(file).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Cannot hold warm root record");const auto heldReady=hostDataRootReadiness().ready;CloseHandle(held);check(!heldReady,"Warm parsed record bypassed exclusive root lock");consume("lock-release-rereads",true);
            fs::remove(file);consume("removed-warm-record",false);replace(original);consume("recreated-record",true);
            replace("{\"schemaVersion\":1,\"schemaVersion\":1}");consume("duplicate-key-after-warm",false);replace(original);consume("duplicate-key-repaired",true);
            auto deep=original;deep.pop_back();deep+=",\"nested\":"+std::string(65,'[')+"0"+std::string(65,']')+"}";replace(deep);consume("too-deep-after-warm",false);replace(original);
            bool smallerLimitRejected=false;try{readHostRootRecord(file,16);}catch(const std::exception&){smallerLimitRejected=true;}check(smallerLimitRejected,"Parsed root bypassed current byte limit");consume("small-limit-restored",true);
            auto large=parsed;large["padding"]=std::string(5000,'x');replace(large.dump());consume("large-valid-record-not-cached",true);large["schemaVersion"]=9;replace(large.dump());consume("large-changed-record",false);replace(original);consume("large-record-restored",true);
            const auto debt=g_dataRoot/L"state"/L"data-migration.json";json committed={{"schemaVersion",2},{"targetDataRoot",toUtf8(g_dataRoot.wstring())},{"sources",json::array()},{"strategy","copy-missing-files-target-wins"},{"filesCopied",0},{"filesSkippedBecauseTargetWins",0},{"bytesCopied",0},{"symlinksSkipped",0},{"startedAt",1},{"completedAt",1},{"phase","committed"},{"pathNormalization","legacy-data-root-and-artwork-overrides-rewritten"},{"unknownDebt",{{"keep",true}}}};text(debt,committed.dump());consume("warm-two-root-debt-records",true);auto pending=committed;pending["completedAt"]=0;const auto debtStamp=fs::last_write_time(debt);check(pending.dump().size()==committed.dump().size(),"Debt mutation changed byte size");text(debt,pending.dump());fs::last_write_time(debt,debtStamp);consume("same-size-same-mtime-pending-debt",false);text(debt,committed.dump());consume("committed-debt-repaired",true);check(readHostRootRecord(debt,4u<<20)->at("unknownDebt")["keep"]==true,"Decoded debt lost extension fields");fs::remove(debt);
            const auto savedRoot=g_dataRoot;g_dataRoot=p/L"foreign-data";const auto foreignFile=g_dataRoot/L"data-root.json";text(foreignFile,original);consume("different-selected-root-same-bytes",false);auto foreign=parsed;foreign["dataRoot"]=toUtf8(g_dataRoot.wstring());text(foreignFile,foreign.dump());consume("different-selected-root-valid-document",true);g_dataRoot=savedRoot;consume("original-selected-root-restored",true);
            const auto overrideFile=g_dataRoot/L"config"/L"manual-overrides.json";const bool overrideExisted=fs::exists(overrideFile);const auto before=bytes(overrideFile);replace(changed);bool denied=false;try{executeWorkspaceCommand("saveManualName",{{"executable",jsonStringOr(f.cache["items"][0],"primaryExecutable")},{"name","must not save"}});}catch(const std::exception& e){denied=std::string(e.what()).find("library-data-root-read-only")!=std::string::npos;}check(denied&&fs::exists(overrideFile)==overrideExisted&&bytes(overrideFile)==before,"Warm parsed root granted write after revocation");replace(original);consume("write-revocation-and-repair",true);
            const auto work=measuredNativeWork(2000,[&]{return hostDataRootReadiness().ready?size_t{1}:size_t{0};});check(work["rows"]==2000&&work["iterations"]==2000,"Root read workload skipped current validation");check(bytes(file)==original&&admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Root decoding probe changed original or invoked external work");writeJsonWithLocalBackup(p/L"root-record-reuse-work.json",{{"variants",stages},{"rootReadWork",work},{"originalRecordBytes",original.size()},{"scope","2000 actual current path/read/schema decisions; exact-byte decode is the only reusable work"},{"rootByteLimitUnchanged",1u<<20},{"debtByteLimitUnchanged",4u<<20}},false);
        });
        test("discovercfg-refresh-protocol-envelope",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,4);RefreshNoticeWindow window;const auto value=workspaceSnapshot(false);unsigned calls=0;json stages=json::array();
            const auto factory=[&]{++calls;return value;};
            postWorkspaceRefreshNotice("library-plan-refreshed",factory,{{"source","protocol-test"}});auto legacy=window.take();check(calls==1&&legacy.contains("snapshot")&&!legacy.contains("refreshRequested")&&workspaceRefreshMessageCurrent(legacy),"Legacy UI stopped receiving full snapshot");checkCachedSnapshot(legacy["snapshot"],f,true);stages.push_back({{"name","legacy-full-payload"}});
            for(const auto& unsupported:std::vector<json>{json{{"advisoryRefreshProtocol",2}},json{{"advisoryRefreshProtocol","1"}},json{{"advisoryRefreshProtocol",true}},json::object()}){acceptWorkspaceRefreshProtocol(unsupported);calls=0;postWorkspaceRefreshNotice("library-plan-refreshed",factory);const auto message=window.take();check(calls==1&&message.contains("snapshot")&&!message.contains("refreshRequested"),"Unsupported UI protocol received compact notice");}
            stages.push_back({{"name","unsupported-and-old-ui-full-fallback"}});
            const auto rootRecord=g_dataRoot/L"data-root.json";const auto rootBytes=bytes(rootRecord);text(rootRecord,json{{"schemaVersion",99}}.dump());beginCommand(123,"startup",{{"advisoryRefreshProtocol",1}});text(rootRecord,rootBytes);check(g_refreshProtocolEpoch.load()==g_refreshDocumentEpoch.load(),"Startup protocol declaration not accepted before read-only diagnostic");calls=0;postWorkspaceRefreshNotice("library-plan-refreshed",factory,{{"source","protocol-test"}});auto compact=window.take();writeJsonWithLocalBackup(p/L"negotiated-notice.json",{{"factoryCalls",calls},{"message",compact}},false);check(calls==0&&!compact.contains("snapshot")&&jsonBoolOr(compact,"refreshRequested",false)&&jsonIntegerOr(compact,"refreshProtocol")==1,"Negotiated refresh still built legacy snapshot");check(workspaceRefreshMessageCurrent(compact),"Current negotiated notice rejected");stages.push_back({{"name","startup-capability-and-compact-envelope"}});
            auto wrong=compact;wrong["refreshProtocol"]=2;check(!workspaceRefreshMessageCurrent(wrong),"Unknown compact protocol accepted");wrong=compact;wrong["refreshDocument"]=0;check(!workspaceRefreshMessageCurrent(wrong),"Missing compact owner accepted");retireWorkspaceRefreshProtocol();check(!workspaceRefreshMessageCurrent(compact)&&workspaceRefreshMessageCurrent(legacy),"Retired document notice crossed into another UI");acceptWorkspaceRefreshProtocol({{"advisoryRefreshProtocol",1}});postWorkspaceRefreshNotice("library-plan-refreshed",factory);const auto current=window.take();check(workspaceRefreshMessageCurrent(current)&&!workspaceRefreshMessageCurrent(compact),"Reused declaration revived old notice");stages.push_back({{"name","navigation-unknown-version-and-old-document"}});
            retireWorkspaceRefreshProtocol();acceptWorkspaceRefreshProtocol({{"advisoryRefreshProtocol",1}});postWorkspaceRefreshNotice("library-plan-refreshed",factory);retireWorkspaceRefreshProtocol();const auto queued=window.take();check(!workspaceRefreshMessageCurrent(queued),"Queued compact notice survived retirement");check(workspaceRefreshMessageCurrent({{"kind","response"},{"refreshRequested",true},{"refreshProtocol",1},{"refreshDocument",1}}),"Response acknowledgement was filtered as an advisory");stages.push_back({{"name","dispatch-owner-check-and-response-control"}});
            auto job=std::make_shared<BackgroundJobControl>();job->cancelRequested=true;calls=0;postWorkspaceRefreshNotice("library-plan-refreshed",factory,json::object(),job);check(calls==0&&window.empty(),"Cancelled notice built or published");job->cancelRequested=false;acceptWorkspaceRefreshProtocol(json::object());postWorkspaceRefreshNotice("library-plan-refreshed",[&]{++calls;job->cancelRequested=true;return value;},json::object(),job);check(calls==1&&window.empty(),"Cancellation during legacy build published stale notice");g_shuttingDown=true;calls=0;postWorkspaceRefreshNotice("library-plan-refreshed",factory);g_shuttingDown=false;check(calls==0&&window.empty(),"Shutdown performed notice work");stages.push_back({{"name","cancel-before-during-and-shutdown"}});
            acceptWorkspaceRefreshProtocol({{"advisoryRefreshProtocol",1}});calls=0;postWorkspaceRefreshNotice("steam-commit-progress",factory);const auto other=window.take();check(calls==1&&other.contains("snapshot")&&!other.contains("refreshRequested"),"Nonadvisory event lost its payload");stages.push_back({{"name","nonadvisory-payload-control"}});
            check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Protocol fixture invoked external work");writeJsonWithLocalBackup(p/L"refresh-protocol-boundaries.json",{{"variants",stages},{"windowQueueReal",true},{"dispatchPredicateReal",true},{"webviewComDeliveryExercised",false}},false);
        });
        test("discovercfg-advisory-producer-work",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,80);seedSnapshotHistory(f,8);const auto initial=workspaceSnapshot(false);checkCachedSnapshot(initial,f,true);RefreshNoticeWindow window;acceptWorkspaceRefreshProtocol({{"advisoryRefreshProtocol",1}});const auto exe=jsonStringOr(f.cache["items"][0],"primaryExecutable");auto job=std::make_shared<BackgroundJobControl>();unsigned compactCount=0,legacyCount=0;json captured=json::array();
            const auto validate=[&](const json& message){check(jsonStringOr(message,"event")=="library-plan-refreshed"&&jsonStringOr(message,"source")=="artwork-stage"&&jsonStringOr(message,"executable")==exe&&workspaceRefreshMessageCurrent(message),"Native artwork notice envelope/owner changed");if(message.contains("snapshot")){++legacyCount;checkCachedSnapshot(message["snapshot"],f,true,"local-scrape-fallback");check(message["snapshot"]["identityCandidates"].size()==80&&message["snapshot"]["resolvedIdentities"].size()==80,"Legacy notification lost history projection");}else{++compactCount;check(jsonBoolOr(message,"refreshRequested",false)&&jsonIntegerOr(message,"refreshProtocol")==1,"Compact producer omitted refresh trigger");}};
            publishArtworkStageRefresh(job,exe);validate(window.take());compactCount=legacyCount=0;const auto noticeWork=measuredNativeWork(2,[&]{publishArtworkStageRefresh(job,exe);return size_t{1};});for(int i=0;i<2;++i){auto message=window.take();validate(message);captured.push_back(std::move(message));}check(window.empty(),"Native notice count changed");
            json latest;const auto stateReadWork=measuredNativeWork(1,[&]{latest=workspaceSnapshot(false);return latest["steamPlan"]["items"].size();});checkCachedSnapshot(latest,f,true);const auto combinedWork=measuredNativeWork(1,[&]{publishArtworkStageRefresh(job,exe);publishArtworkStageRefresh(job,exe);latest=workspaceSnapshot(false);return latest["steamPlan"]["items"].size();});validate(window.take());validate(window.take());check(window.empty(),"Combined refresh lost or duplicated notices");checkCachedSnapshot(latest,f,true);check(latest["identityCandidates"].size()==80&&latest["resolvedIdentities"].size()==80,"Fresh state read omitted required data");
            size_t payloadBytes=0;for(const auto& message:captured)payloadBytes+=message.dump().size();check(noticeWork["rows"]==2&&stateReadWork["rows"]==80&&combinedWork["rows"]==80&&compactCount+legacyCount==4,"Advisory workload did not consume its declared work");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Advisory producer invoked external work");writeJsonWithLocalBackup(p/L"native-advisory-events.json",captured,false);writeJsonWithLocalBackup(p/L"advisory-producer-work.json",{{"games",80},{"failureRecords",640},{"legacyManifests",640},{"noticeWork",noticeWork},{"stateReadWork",stateReadWork},{"combinedWork",combinedWork},{"compactNotices",compactCount},{"legacyNotices",legacyCount},{"timedNoticePayloadBytes",payloadBytes},{"fixtureRoot",toUtf8(f.root.wstring())},{"scope","Two real producer notices; separately one real state read; separately two notices plus one state read. Win32 result queue captured before WebView serialization."},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);
        });
        test("discovercfg-optional-history-lock-recovery",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,4);const auto exe0=jsonStringOr(f.cache["items"][0],"primaryExecutable"),exe1=jsonStringOr(f.cache["items"][1],"primaryExecutable");const auto key0=toUtf8(normalizedPathKey(fs::path(toWide(exe0)))),key1=toUtf8(normalizedPathKey(fs::path(toWide(exe1))));const auto failure=g_dataRoot/L"state"/L"failures"/L"pending.json",jobFailure=workspaceJobsRoot()/L"other-job"/L"failures"/L"pending.json",legacy=workspaceJobsRoot()/L"library-batch-identification"/L"old-job"/L"manifest.json";
            const auto record=[&](const std::string& exe,const char* name){return json{{"executable",exe},{"manualReviewCandidates",json::array({{{"name",name},{"steamAppId",620},{"provider","steam"},{"score",88}}})}}.dump();};const auto original=record(exe0,"State review"),jobBytes=record(exe1,"Job review");text(failure,original);text(jobFailure,jobBytes);text(legacy,json{{"exe",exe0},{"match",{{"formalName","Legacy candidate"},{"igdbId",77},{"primaryProvider","playnite-igdb"}}}}.dump());json stages=json::array();
            const auto consume=[&](const char* name,const json& value,bool state,bool job,bool identity){checkCachedSnapshot(value,f,true);check(value["identityCandidates"].contains(key0)==state&&value["identityCandidates"].contains(key1)==job,"Optional history damaged another candidate");check(value["resolvedIdentities"].contains(key0)==identity,"Optional history changed legacy/cache gate");if(identity)check(!custom_steam_library::verifiedSteamIdentityMatch(value["resolvedIdentities"][key0]),"Legacy candidate gained verified identity");stages.push_back({{"name",name},{"stateCandidate",state},{"jobCandidate",job},{"legacyIdentity",identity}});};
            consume("valid-history",workspaceSnapshot(false),true,true,true);
            const auto locked=[&](const fs::path& path,const char* label){HANDLE held=CreateFileW(custom_steam_library::ioPath(path).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Cannot exclusively hold owned optional history");json value;std::string error;try{value=workspaceSnapshot(false);}catch(const std::exception& e){error=e.what();}CloseHandle(held);writeJsonWithLocalBackup(p/(toWide(label)+L"-observation.json"),{{"error",error},{"snapshotReturned",error.empty()},{"file",toUtf8(path.wstring())}},false);check(error.empty(),"Locked optional failure record blocked workspace snapshot");return value;};
            consume("state-file-held",locked(failure,"state-held"),false,true,true);check(bytes(failure)==original,"Held failure original changed");consume("state-file-released",workspaceSnapshot(false),true,true,true);
            consume("job-file-held",locked(jobFailure,"job-held"),true,false,true);check(bytes(jobFailure)==jobBytes,"Held job failure changed");consume("job-file-released",workspaceSnapshot(false),true,true,true);
            consume("legacy-file-held",locked(legacy,"legacy-held"),true,true,false);consume("legacy-file-released",workspaceSnapshot(false),true,true,true);
            text(failure,"{");consume("malformed-failure",workspaceSnapshot(false),false,true,true);check(bytes(failure)=="{","Malformed optional failure auto-repaired");text(failure,original);consume("failure-repaired",workspaceSnapshot(false),true,true,true);fs::remove(failure);consume("failure-removed",workspaceSnapshot(false),false,true,true);text(failure,original);consume("failure-restored",workspaceSnapshot(false),true,true,true);
            const auto current=g_dataRoot/L"cache"/L"identity"/L"future.json";const auto future=json{{"schemaVersion",99},{"executable",exe0},{"match",{{"formalName","Future identity"}}}}.dump();text(current,future);consume("current-cache-stops-legacy-fallback",workspaceSnapshot(false),true,true,false);check(bytes(current)==future,"Future identity bytes changed");fs::remove(current);consume("legacy-resumes-after-current-cache-removal",workspaceSnapshot(false),true,true,true);
            fs::rename(custom_steam_library::ioPath(workspacePlanPath()),custom_steam_library::ioPath(p/L"retained-plan.json"));const auto fallback=workspaceSnapshot(false);for(const auto& item:fallback["steamPlan"]["items"])check(!steamPlanItemAlreadyOwned(item),"Failure/legacy candidate invented shortcut ownership");fs::rename(custom_steam_library::ioPath(p/L"retained-plan.json"),custom_steam_library::ioPath(workspacePlanPath()));stages.push_back({{"name","candidates-do-not-invent-membership"}});
            check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Optional history invoked external work");writeJsonWithLocalBackup(p/L"optional-history-boundaries.json",{{"variants",stages},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);
        });
        test("discovercfg-large-history-process-work",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,80);seedSnapshotHistory(f,8);const auto validate=[&](const json& value){checkCachedSnapshot(value,f,true);check(value["identityCandidates"].size()==80&&value["resolvedIdentities"].size()==80,"History scan lost current candidates or legacy identities");for(const auto& [key,item]:value["identityCandidates"].items())check(jsonStringOr(item["candidates"][0],"name").starts_with("Newest "),"Older history displaced newest candidate");for(const auto& [key,item]:value["resolvedIdentities"].items())check(!custom_steam_library::verifiedSteamIdentityMatch(item),"History scan promoted candidate authority");};
            validate(workspaceSnapshot(false));const auto failureWork=measuredNativeWork(2,[&]{json value=json::object();attachFailureCandidates(value);return value["identityCandidates"].size();});const auto identityWork=measuredNativeWork(2,[&]{json value=json::object();attachResolvedIdentityCache(value);return value["resolvedIdentities"].size();});const auto snapshotWork=measuredSnapshotWork(f,2,true);validate(workspaceSnapshot(false));check(failureWork["rows"]==160&&identityWork["rows"]==160&&snapshotWork["rows"]==160,"History probe did not consume fixed workload");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Large history invoked external work");writeJsonWithLocalBackup(p/L"history-scan-work.json",{{"games",80},{"rounds",8},{"failureRecords",640},{"legacyManifests",640},{"failureWork",failureWork},{"identityWork",identityWork},{"snapshotWork",snapshotWork},{"fixtureRoot",toUtf8(f.root.wstring())},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);
        });
        test("discovercfg-owned-plan-process-work",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,80);auto cache=f.cache;for(size_t i=0;i<80;++i){cache["items"][i]["shortAppId"]=std::to_string(2147483648ull+i);cache["items"][i]["gridDirectory"]="";cache["items"][i]["customOwnedToken"]="preserve-"+std::to_string(i);}text(workspacePlanPath(),cache.dump());
            const auto snapshot=workspaceSnapshot(false);checkCachedSnapshot(snapshot,f,true);const auto ownership=snapshot["steamPlan"];const auto bytesBefore=bytes(workspacePlanPath());
            const auto validate=[&](const json& plan){check(plan["items"]==ownership["items"]&&plan["summary"].value("alreadyInSteam",0)==80,"Owned plan lost shortcut metadata, order or membership");};
            validate(buildLocalSteamPlanSnapshot(snapshot,ownership));const auto planWork=measuredNativeWork(2,[&]{return buildLocalSteamPlanSnapshot(snapshot,ownership)["items"].size();});validate(buildLocalSteamPlanSnapshot(snapshot,ownership));
            const auto refresh=[&]{auto value=workspaceSnapshot(false);value["steamPlan"]=buildLocalSteamPlanSnapshot(value,jsonObjectOr(value,"steamPlan"));attachWorkspaceArtwork(value["steamPlan"],value["steamAccounts"]);return value;};
            checkCachedSnapshot(refresh(),f,true);const auto refreshWork=measuredNativeWork(2,[&]{return refresh()["steamPlan"]["items"].size();});const auto after=refresh();checkCachedSnapshot(after,f,true);validate(after["steamPlan"]);
            check(planWork["rows"]==160&&refreshWork["rows"]==160&&bytes(workspacePlanPath())==bytesBefore,"Owned benchmark changed workload or persisted authority");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Owned refresh invoked external work");writeJsonWithLocalBackup(p/L"owned-plan-work.json",{{"games",80},{"planWork",planWork},{"refreshWork",refreshWork},{"fixtureRoot",toUtf8(f.root.wstring())},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);
        });


        test("discovercfg-static-source-priority",[&](const auto& p){
            json stages=json::array();
            using Body=std::function<void(ymcc::media::Store&,json,const fs::path&,const fs::path&,const fs::path&)>;
            const auto scenario=[&](const char* name,const Body& body){
                const auto root=p/std::to_wstring(stages.size()),oldImage=root/L"artwork"/L"manual"/L"old.jpg",incoming=root/L"incoming.jpg",exe=root/L"Game"/L"game.exe";
                text(oldImage,std::string("\xff\xd8\xff",3)+"old legacy bytes");text(incoming,std::string("\xff\xd8\xff",3)+"new shared bytes");text(exe,"owned fixture never executed");
                ymcc::media::atomicWrite(root/L"config"/L"manual-overrides.json",{{"items",{{ymcc::media::normalized(toUtf8(exe.wstring())),{{"wallpaper",{{"portableFile",toUtf8(oldImage.wstring())}}},{"artworkProtection",{{"wallpaper","manual"}}}}}}}});
                ymcc::media::Store store(root);auto e=store.registerEntry({{"appId",620},{"verified",true},{"executable",toUtf8(exe.wstring())},{"name","Static source fixture"}});
                try{body(store,e,root,oldImage,incoming);stages.push_back({{"name",name},{"passed",true}});}catch(const std::exception& error){stages.push_back({{"name",name},{"passed",false},{"error",error.what()}});}
            };
            scenario("manual-import-survives-unchanged-legacy-reopen",[&](auto& store,auto e,const auto& root,const auto&,const auto& incoming){
                e=store.publish(e,incoming,true);const auto before=store.snapshot();const auto index=root/L"config"/L"game-media.json";const auto raw=bytes(index);const auto stamp=fs::last_write_time(index);const auto accepted=e["assets"]["static"];
                for(int i=0;i<6;++i){const auto current=store.registerEntry(e);check(current["assets"]["static"]==accepted&&current["bindingRevision"]==e["bindingRevision"],"Unchanged legacy source replaced accepted static import");}
                check(store.snapshot()==before&&bytes(index)==raw&&fs::last_write_time(index)==stamp,"Stable reopen rewrote media state or bumped revision");
            });
            scenario("shared-delete-is-not-resurrected-by-unchanged-legacy",[&](auto& store,auto e,const auto&,const auto&,const auto&){e["action"]="delete";e["slot"]="static";e=store.edit(e);auto current=store.registerEntry(e);check(current["deletedSlots"].value("static",false)&&!current["assets"].contains("static"),"Unchanged legacy source resurrected shared deletion");auto pending=store.begin(e);check(std::find(pending["taskSlots"].begin(),pending["taskSlots"].end(),"static")==pending["taskSlots"].end(),"Deleted static slot re-entered automatic preparation");store.finish(pending);});
            scenario("new-import-survives-old-legacy-delete",[&](auto& store,auto e,const auto& root,const auto&,const auto& incoming){auto doc=ymcc::media::read(root/L"config"/L"manual-overrides.json");auto& row=doc["items"][ymcc::media::normalized(e["executable"].template get<std::string>())];row.erase("wallpaper");row["artworkProtection"]["wallpaper"]="deleted";ymcc::media::atomicWrite(root/L"config"/L"manual-overrides.json",doc);e=store.registerEntry(e);e=store.publish(e,incoming,true);const auto current=store.registerEntry(e);check(current["assets"].contains("static")&&current["assets"]["static"]==e["assets"]["static"]&&!current["deletedSlots"].value("static",false),"Old legacy delete erased a later explicit import");});
            scenario("later-manual-legacy-edit-still-applies",[&](auto& store,auto e,const auto& root,const auto&,const auto& incoming){e=store.publish(e,incoming,true);const auto later=root/L"artwork"/L"manual"/L"later.jpg";text(later,std::string("\xff\xd8\xff",3)+"later explicit legacy choice");auto doc=ymcc::media::read(root/L"config"/L"manual-overrides.json");doc["items"][ymcc::media::normalized(e["executable"].template get<std::string>())]["wallpaper"]["portableFile"]=toUtf8(later.wstring());ymcc::media::atomicWrite(root/L"config"/L"manual-overrides.json",doc);const auto current=store.registerEntry(e);check(current["assets"].contains("static")&&store.assetPath(current["assets"]["static"])==fs::weakly_canonical(later),"A later explicit legacy choice was ignored");});
            scenario("later-legacy-delete-and-reselection-still-apply",[&](auto& store,auto e,const auto& root,const auto& oldImage,const auto& incoming){e=store.publish(e,incoming,true);const auto config=root/L"config"/L"manual-overrides.json";auto doc=ymcc::media::read(config);const auto key=ymcc::media::normalized(e["executable"].template get<std::string>());doc["items"][key].erase("wallpaper");doc["items"][key]["artworkProtection"]["wallpaper"]="deleted";ymcc::media::atomicWrite(config,doc);e=store.registerEntry(e);check(e["deletedSlots"].value("static",false),"Later legacy delete was ignored");doc["items"][key]["wallpaper"]={{"portableFile",toUtf8(oldImage.wstring())}};doc["items"][key]["artworkProtection"]["wallpaper"]="manual";ymcc::media::atomicWrite(config,doc);e=store.registerEntry(e);check(e["assets"].contains("static")&&!e["deletedSlots"].value("static",false),"Reselecting the original file after a later delete was ignored");});
            scenario("automatic-legacy-cache-cannot-replace-manual-import",[&](auto& store,auto e,const auto& root,const auto&,const auto& incoming){e=store.publish(e,incoming,true);const auto accepted=e["assets"]["static"];ymcc::media::atomicWrite(root/L"config"/L"manual-overrides.json",{{"items",json::object()}});const auto current=store.registerEntry(e);check(current["assets"]["static"]==accepted&&current["manualSlots"].value("static",false),"Removing a legacy override erased a manual shared import");});
            scenario("identity-alias-transfer-retains-static-source-boundary",[&](auto& store,auto e,const auto& root,const auto&,const auto& incoming){const auto exe=e["executable"].template get<std::string>();auto state=store.snapshot();state["entries"]=json::object();state["aliases"]=json::object();ymcc::media::atomicWrite(root/L"config"/L"game-media.json",state);auto local=store.registerEntry({{"executable",exe}});local=store.publish(local,incoming,true);const auto accepted=local["assets"]["static"];store.registerEntry({{"appId",620}});auto migrated=store.registerEntry({{"appId",620},{"verified",true},{"executable",exe}});check(migrated["assets"]["static"]==accepted,"Alias transfer lost the imported static source boundary");});
            scenario("source-generation-allows-explicit-same-file-reselection",[&](auto& store,auto e,const auto& root,const auto& oldImage,const auto& incoming){
                e=store.publish(e,incoming,true);auto doc=ymcc::media::read(root/L"config"/L"manual-overrides.json");
                doc["items"][ymcc::media::normalized(e["executable"].template get<std::string>())]["wallpaperSourceRevision"]="later-explicit-selection";
                ymcc::media::atomicWrite(root/L"config"/L"manual-overrides.json",doc);const auto chosen=store.registerEntry(e);
                check(store.assetPath(chosen["assets"]["static"])==fs::weakly_canonical(oldImage),"Explicit same-file selection lost to shared import");
            });
            scenario("pre-baseline-manual-entry-retains-its-existing-explicit-choice",[&](auto& store,auto e,const auto& root,const auto&,const auto& incoming){
                e=store.publish(e,incoming,true);const auto accepted=e["assets"]["static"];auto doc=store.snapshot();doc["entries"][e["key"].template get<std::string>()].erase("libraryStaticSource");ymcc::media::atomicWrite(root/L"config"/L"game-media.json",doc);
                const auto chosen=store.registerEntry(e);check(chosen["assets"]["static"]==accepted,"Old entry without a baseline lost its manual choice");
                const auto first=bytes(root/L"config"/L"game-media.json");store.registerEntry(chosen);check(bytes(root/L"config"/L"game-media.json")==first,"Migrated baseline never stabilized");
            });
            scenario("unreadable-legacy-source-cannot-acknowledge-or-change-the-shared-choice",[&](auto& store,auto e,const auto& root,const auto&,const auto& incoming){
                e=store.publish(e,incoming,true);const auto config=root/L"config"/L"manual-overrides.json",index=root/L"config"/L"game-media.json";const auto original=bytes(config),before=bytes(index);text(config,"{");bool rejected=false;try{store.registerEntry(e);}catch(...){rejected=true;}
                check(rejected&&bytes(index)==before,"Unreadable source changed shared state or baseline");text(config,original);check(store.registerEntry(e)["assets"]["static"]==e["assets"]["static"],"Source repair resurrected an older choice");
            });
            scenario("actual-editor-stamps-only-explicit-wallpaper-decisions",[&](auto& store,auto e,const auto& root,const auto& oldImage,const auto& incoming){
                const auto savedRoot=g_dataRoot;struct RestoreRoot{fs::path value;~RestoreRoot(){g_dataRoot=value;}} restore{savedRoot};g_dataRoot=root;trustedAdmissionFixtureRoot(root);
                const auto exe=e["executable"].template get<std::string>();const auto key=ymcc::media::normalized(exe);const auto config=root/L"config"/L"manual-overrides.json";
                executeWorkspaceCommand("saveArtworkOverride",{{"executable",exe},{"clearWallpaper",true}});
                const auto first=ymcc::media::read(config)["items"][key].value("wallpaperSourceRevision",std::string{});check(!first.empty(),"Explicit wallpaper delete omitted its source generation");
                e=store.resolve(e);e=store.publish(e,incoming,true);
                executeWorkspaceCommand("saveArtworkOverride",{{"executable",exe},{"clearWallpaper",true}});
                const auto second=ymcc::media::read(config)["items"][key].value("wallpaperSourceRevision",std::string{});
                check(second!=first&&store.resolve(e)["deletedSlots"].value("static",false),"Repeating a later explicit delete did not supersede an intervening import");
                executeWorkspaceCommand("saveArtworkOverride",{{"executable",exe},{"clearCover",true}});
                check(ymcc::media::read(config)["items"][key].value("wallpaperSourceRevision",std::string{})==second,"Unrelated artwork edit changed wallpaper provenance");
            });
            writeJsonWithLocalBackup(p/L"static-source-stages.json",stages,false);check(std::all_of(stages.begin(),stages.end(),[](const auto& row){return row.value("passed",false);}),"Static source priority stage failed; inspect original stages");
        });

        test("discovercfg-import-receipt-preview-and-cas",[&](const auto& p){
            ymcc::media::Store store(g_dataRoot);const auto exe=p/L"Game"/L"game.exe";text(exe,"owned fixture never executed");
            auto entry=executeWorkspaceCommand("mediaCreate",{{"appId",620},{"verified",true},{"executable",toUtf8(exe.wstring())},{"name","Receipt fixture"}});json stages=json::array();bool allUrls=true;
            for(const auto* slot:{"static","dynamic"}){
                const auto source=p/(std::string(slot)=="static"?L"owned.jpg":L"owned.mp4");text(source,std::string(slot)=="static"?std::string("\xFF\xD8\xFF",3)+"owned storage fixture, no decoder":std::string("\0\0\0\x18" "ftypisom",12)+"owned storage fixture, no decoder");auto args=entry;args["source"]=toUtf8(source.wstring());args["slot"]=slot;const auto original=bytes(source);
                const auto receipt=executeWorkspaceCommand("mediaImport",args);const auto persisted=store.resolve(receipt);check(receipt["key"]==entry["key"]&&receipt["bindingRevision"].get<uint64_t>()==entry["bindingRevision"].get<uint64_t>()+1,"Import receipt lost accepted key/revision");check(persisted["assets"][slot]==receipt["assets"][slot],"Receipt asset differs from durable import");check(bytes(store.assetPath(receipt["assets"][slot]))==original&&bytes(source)==original,"Import bytes or selected source changed");
                const auto url=jsonStringOr(receipt,(std::string(slot)+"PreviewUrl").c_str());allUrls=allUrls&&!url.empty()&&url==mappedDataUrl(store.assetPath(receipt["assets"][slot]));stages.push_back({{"slot",slot},{"receipt",receipt},{"mappedSlotUrl",url}});
                const auto accepted=store.snapshot();bool rejected=false;try{executeWorkspaceCommand("mediaImport",args);}catch(const std::exception& e){rejected=std::string(e.what()).find("media-stale-publication")!=std::string::npos;}check(rejected&&store.snapshot()==accepted,"Stale import bypassed revision check or changed accepted bytes");
                auto absent=receipt;absent["source"]=toUtf8((p/L"missing.mp4").wstring());absent["slot"]="dynamic";rejected=false;try{executeWorkspaceCommand("mediaImport",absent);}catch(const std::exception&){rejected=true;}check(rejected&&store.snapshot()==accepted,"Missing import source mutated accepted entry");
                auto playback=receipt;playback["action"]="playback";playback["start"]=5;playback["end"]=35;playback["fit"]="cover";entry=executeWorkspaceCommand("mediaEdit",playback);check(entry["playback"]["start"]==5,"Accepted import revision cannot feed next edit without full snapshot");
            }
            writeJsonWithLocalBackup(p/L"import-receipt-stages.json",stages,false);check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Import test invoked external work");check(allUrls,"Accepted import lacks mapped slot preview URLs");
        });

        test("discovercfg-owned-plan-authority-and-alias",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,4);const auto snapshot=workspaceSnapshot(false);auto ownership=snapshot["steamPlan"];for(size_t i=0;i<4;++i){ownership["items"][i]["customOwnedToken"]="proof-"+std::to_string(i);ownership["items"][i]["shortAppId"]=std::to_string(2147483648ull+i);}json observations=json::array();
            const auto itemFor=[&](const json& result,const std::string& exe){for(const auto& row:result["items"])if(jsonStringOr(row,"primaryExecutable")==exe)return row;throw std::runtime_error("Owned boundary lost selected EXE");};
            const auto exe=jsonStringOr(snapshot["library"]["games"][0],"primaryExecutable");const auto directory=jsonStringOr(snapshot["library"]["games"][0],"gameDirectory");
            const auto consumed=[&](const char* name,const json& input,const json& old){auto plan=buildLocalSteamPlanSnapshot(input,old);observations.push_back({{"name",name},{"items",plan["items"].size()}});return plan;};
            const auto rejected=[&](const char* name,const json& input,const json& old,const std::string& target){const auto plan=consumed(name,input,old);const auto row=itemFor(plan,target);check(!steamPlanItemAlreadyOwned(row)&&!row.contains("customOwnedToken")&&!row.contains("shortAppId"),name);};
            check(consumed("matching-full-proof",snapshot,ownership)["items"]==ownership["items"],"Matching ownership metadata changed");
            auto changed=snapshot;const auto replacement=p/L"Games"/L"0"/L"replacement.exe";text(replacement,"owned fixture never executed");changed["library"]["games"][0]["primaryExecutable"]=toUtf8(replacement.wstring());rejected("Changed primary EXE retained old ownership",changed,ownership,toUtf8(replacement.wstring()));
            changed=snapshot;changed["library"]["games"][0]["gameDirectory"]=toUtf8((p/L"other-directory").wstring());rejected("Wrong directory retained old ownership",changed,ownership,exe);
            changed["library"]["games"][0]["alternateGameDirectories"]=json::array({false,directory});check(itemFor(consumed("matching-alternate-directory",changed,ownership),exe)==ownership["items"][0],"Alternate directory lost original shortcut metadata");
            auto ambiguous=ownership;auto later=ownership["items"][0];later["gameDirectory"]=changed["library"]["games"][0]["gameDirectory"];later["customOwnedToken"]="later-primary-directory";ambiguous["items"].insert(ambiguous["items"].begin()+1,later);check(itemFor(consumed("earlier-alias-before-later-primary",changed,ambiguous),exe)["customOwnedToken"]=="proof-0","Directory preference overrode earlier matching source row");
            auto reorder=changed;std::swap(reorder["library"]["games"][0],reorder["library"]["games"][1]);check(itemFor(consumed("cached-candidates-retain-source-order",reorder,ambiguous),exe)["customOwnedToken"]=="proof-0","Indexed ownership candidates changed first matching row");
            changed["library"]["games"][0]["gameDirectory"]="";rejected("Empty main directory borrowed alias ownership",changed,ownership,exe);
            changed=snapshot;changed["library"]["games"][0]["primaryExecutable"]="";const auto empty=consumed("empty-exe-cannot-own",changed,ownership);check(!steamPlanItemAlreadyOwned(empty["items"][0])&&!empty["items"][0].contains("customOwnedToken"),"Empty EXE retained shortcut authority");
            auto foreign=ownership;foreign["libraryState"]=toUtf8((p/L"foreign-library.json").wstring());rejected("Foreign library plan retained ownership",snapshot,foreign,exe);
            auto unowned=ownership;unowned["items"][0]["status"]="ready-to-add";rejected("Uncommitted source item retained ownership",snapshot,unowned,exe);
            auto malformed=ownership;malformed["items"][0]["gameDirectory"]="";rejected("Empty source directory retained ownership",snapshot,malformed,exe);malformed["items"][0]=false;rejected("Nonobject source row retained ownership",snapshot,malformed,exe);
            auto normalized=ownership;auto upper=toWide(exe);CharUpperBuffW(upper.data(),static_cast<DWORD>(upper.size()));normalized["items"][0]["primaryExecutable"]=toUtf8(upper);normalized["items"][0]["gameDirectory"]=toUtf8(fs::relative(fs::path(toWide(directory)),fs::current_path()).wstring());const auto normalizedPlan=consumed("case-relative-source-paths",snapshot,normalized);check(normalizedPlan["items"][0]==normalized["items"][0],"Equivalent source paths lost or rewrote metadata");
            auto native=ownership;native["items"][0]["status"]="steam-native";native["items"][0]["steamNative"]=true;check(consumed("native-source-status-compatible",snapshot,native)["items"][0]==native["items"][0],"Native owned source status changed");
            const auto oldExeBytes=bytes(fs::path(toWide(exe)));fs::remove(fs::path(toWide(exe)));check(consumed("missing-file-does-not-remove-recorded-shortcut",snapshot,ownership)["items"][0]==ownership["items"][0],"Filesystem presence incorrectly replaced recorded shortcut ownership");text(fs::path(toWide(exe)),oldExeBytes);
            auto removed=ownership;removed["items"].erase(removed["items"].begin());rejected("Next invocation retained removed ownership",snapshot,removed,exe);
            // Pending/manual IDs must not borrow a previous game's proof.
            auto selectedSnapshot=snapshot;selectedSnapshot["library"]["games"]=json::array({snapshot["library"]["games"][0]});
            auto selectedOwned=ownership;selectedOwned["items"]=json::array({ownership["items"][0]});
            selectedOwned["items"][0]["steamStoreAppId"]=883710;selectedOwned["items"][0]["storefrontAppId"]=883710;selectedOwned["items"][0]["steamVerificationStatus"]="steam-verified";selectedOwned["items"][0]["steamShortcutName"]="Stored shortcut label";
            const auto manifestPath=fs::path(toWide(jsonStringOr(ownership["items"][0],"artworkManifest")));json selectedObserved=json::array();bool selectedSafe=true;
            auto official=[](int app){return json{{"appId",app},{"storefrontAppId",app},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"},{"formalName","Verified identity"}};};
            for(const auto* variant:{"both-conflict","cache-current","manifest-current","numeric-string","explicit-content","name-only"}){
                const std::string kind=variant;auto oldMatch=official(883710),newMatch=official(620);auto manifestMatch=(kind=="manifest-current"||kind=="numeric-string"||kind=="explicit-content")?newMatch:oldMatch;
                json manual={{"steamId",620},{"name","Custom user label"}};if(kind=="numeric-string")manual["steamId"]="620";
                if(kind=="explicit-content"){manual["steamId"]=700;manifestMatch["matchedContentAppId"]=700;manifestMatch["baseGameAppId"]=620;manifestMatch["contentRelation"]="explicit-dlc";manifestMatch["identityKind"]="steam-base-game-with-explicit-content";}
                if(kind=="name-only")manual.erase("steamId");
                selectedSnapshot["manualOverrides"]={{"items",{{toUtf8(normalizedPathKey(fs::path(toWide(exe)))),manual}}}};
                selectedSnapshot["resolvedIdentities"]={{toUtf8(normalizedPathKey(fs::path(toWide(exe)))),kind=="cache-current"?newMatch:oldMatch}};
                const auto selectedOverridesPath=g_dataRoot/L"config"/L"manual-overrides.json";writeJsonWithLocalBackup(selectedOverridesPath,selectedSnapshot["manualOverrides"],false);const auto selectedOverrideBytes=bytes(selectedOverridesPath);
                auto source=readJson(manifestPath,json::object());source["match"]=manifestMatch;writeJsonWithLocalBackup(manifestPath,source,false);const auto sourceBytes=bytes(manifestPath);
                const auto cacheManifest=workspaceJobsRoot()/L"workspace-identification"/L"manual-selection"/L"manifest.json";
                writeJsonWithLocalBackup(cacheManifest,{{"exe",exe},{"match",kind=="cache-current"?newMatch:oldMatch}},false);const auto cacheBytes=bytes(cacheManifest);
                auto exported=selectedSnapshot;attachResolvedIdentityCache(exported);const auto selectedKey=toUtf8(normalizedPathKey(fs::path(toWide(exe))));
                const auto resolved=jsonObjectOr(exported,"resolvedIdentities");const auto actualResolved=resolved.contains(selectedKey)?resolved[selectedKey]:json::object();
                const int expectedResolved=kind=="cache-current"?620:kind=="name-only"?883710:0;
                const bool cacheCorrect=jsonIntegerOr(actualResolved,"appId",0)==expectedResolved&&bytes(cacheManifest)==cacheBytes;
                for(bool owned:{true,false}){
                    auto result=buildLocalSteamPlanSnapshot(selectedSnapshot,owned?selectedOwned:json::object());const auto row=itemFor(result,exe);
                    const int expected=owned?(kind=="name-only"?883710:0):(kind=="both-conflict"?0:kind=="name-only"?883710:620);
                    const bool correct=jsonIntegerOr(row,"steamStoreAppId",0)==expected;
                    attachArtworkPreviews(result);const auto preview=itemFor(result,exe);const bool hasHero=jsonObjectOr(preview,"artworkPreview").contains("hero");
                    const bool previewCorrect=hasHero!=(kind=="both-conflict"||kind=="cache-current");
                    const bool preserved=bytes(manifestPath)==sourceBytes&&bytes(selectedOverridesPath)==selectedOverrideBytes&&(!owned||(steamPlanItemAlreadyOwned(row)&&row["customOwnedToken"]=="proof-0"&&row["shortAppId"]=="2147483648"));
                    selectedObserved.push_back({{"variant",kind},{"owned",owned},{"expectedAppId",expected},{"item",row},{"identityCorrect",correct},{"previewCorrect",previewCorrect},{"cacheCorrect",cacheCorrect},{"exportedIdentity",actualResolved},{"inputsAndMembershipPreserved",preserved}});selectedSafe=selectedSafe&&correct&&previewCorrect&&cacheCorrect&&preserved;
                }
            }
            writeJsonWithLocalBackup(p/L"manual-selection-host-observed.json",selectedObserved,false);
            check(selectedObserved.size()==12&&selectedSafe,"Local plan reused identity conflicting with selected Steam ID");
            json igdbObserved=json::array();bool igdbSafe=true;
            const auto selectedKey=toUtf8(normalizedPathKey(fs::path(toWide(exe))));
            const auto selectedOverridesPath=g_dataRoot/L"config"/L"manual-overrides.json";
            const auto cacheManifest=workspaceJobsRoot()/L"workspace-identification"/L"manual-selection"/L"manifest.json";
            for(const auto* variant:{"conflicting-steam","matching-igdb-manifest","matching-steam-igdb","mismatching-igdb"}){
                const std::string kind=variant;auto old=official(883710),chosen=official(883710);chosen["igdbId"]=kind=="mismatching-igdb"?73:72;chosen["formalName"]="Selected metadata title";
                if(kind!="matching-steam-igdb"){chosen["appId"]=0;chosen["storefrontAppId"]=nullptr;chosen["primaryProvider"]="playnite-igdb";chosen["identityStatus"]="metadata-verified";chosen["metadataVerificationStatus"]="metadata-verified";chosen["steamVerificationStatus"]="not-available";}
                const auto observed=kind=="conflicting-steam"?old:chosen;
                selectedSnapshot["manualOverrides"]={{"items",{{selectedKey,{{"igdbId",72},{"name","Selected metadata title"}}}}}};
                selectedSnapshot["resolvedIdentities"]={{selectedKey,old}};writeJsonWithLocalBackup(selectedOverridesPath,selectedSnapshot["manualOverrides"],false);const auto manualBefore=bytes(selectedOverridesPath);
                auto doc=readJson(manifestPath,json::object());doc["match"]=observed;writeJsonWithLocalBackup(manifestPath,doc,false);const auto manifestBefore=bytes(manifestPath);
                writeJsonWithLocalBackup(cacheManifest,{{"exe",exe},{"match",old}},false);const auto cacheBefore=bytes(cacheManifest);
                auto exported=selectedSnapshot;attachResolvedIdentityCache(exported);const auto exportedRows=jsonObjectOr(exported,"resolvedIdentities");const bool cacheCorrect=!exportedRows.contains(selectedKey);
                for(bool owned:{true,false}){
                    const auto value=buildLocalSteamPlanSnapshot(selectedSnapshot,owned?selectedOwned:json::object());const auto item=itemFor(value,exe);
                    const int expected=!owned&&kind=="matching-steam-igdb"?883710:0;
                    const bool identity=jsonIntegerOr(item,"steamStoreAppId",0)==expected;
                    const bool metadata=owned||kind!="matching-igdb-manifest"||(jsonIntegerOr(item,"igdbId",0)==72&&jsonStringOr(item,"formalName")=="Selected metadata title");
                    const bool preserved=bytes(selectedOverridesPath)==manualBefore&&bytes(manifestPath)==manifestBefore&&bytes(cacheManifest)==cacheBefore;
                    const bool membership=!owned||steamPlanItemAlreadyOwned(item)&&item["customOwnedToken"]=="proof-0";
                    igdbObserved.push_back({{"variant",kind},{"owned",owned},{"item",item},{"identityCorrect",identity},{"metadataCorrect",metadata},{"cacheFiltered",cacheCorrect},{"inputsPreserved",preserved},{"membershipPreserved",membership},{"passed",identity&&metadata&&cacheCorrect&&preserved&&membership}});igdbSafe=igdbSafe&&identity&&metadata&&cacheCorrect&&preserved&&membership;
                }
            }
            const auto cacheFile=g_dataRoot/L"cache"/L"identity"/(toWide(safeArtworkSegment(selectedKey))+L".json");
            const auto seed=p/L"igdb-seed"/L"manifest.json",incoming=p/L"igdb-incoming"/L"manifest.json";json writerRows=json::array();
            for(const auto* variant:{"explicit-igdb","automatic","wrong-request","unverified-metadata","malformed-request","zero-request"}){
                const std::string kind=variant;fs::remove(cacheFile);writeJsonWithLocalBackup(seed,{{"exe",exe},{"match",official(883710)}},false);check(persistResolvedIdentityCache(seed),"Cannot seed original official identity");
                auto previous=readJson(cacheFile,json::object());previous["userAnnotation"]="preserve";writeJsonWithLocalBackup(cacheFile,previous,false);const auto before=bytes(cacheFile);
                json metadata={{"igdbId",72},{"formalName","Selected metadata title"},{"primaryProvider","playnite-igdb"},{"identityStatus","metadata-verified"},{"metadataVerificationStatus",kind=="unverified-metadata"?"pending":"metadata-verified"},{"steamVerificationStatus","not-available"}};
                writeJsonWithLocalBackup(incoming,{{"exe",exe},{"match",metadata},{"metadata",json::object()}},false);
                json request=kind=="automatic"?json::object():json{{"igdbId",kind=="wrong-request"?"73":kind=="malformed-request"?"+72":kind=="zero-request"?"0":"72"}};
                const auto manualBefore=bytes(selectedOverridesPath);const bool accepted=persistResolvedIdentityCache(incoming,request);const auto result=readJson(cacheFile,json::object());const bool expected=kind=="explicit-igdb";
                const bool correct=accepted==expected&&(expected?(jsonIntegerOr(result,"igdbId",0)==72&&!result.contains("steamAppId")&&!custom_steam_library::verifiedSteamIdentityMatch(jsonObjectOr(result,"match"))&&result["userAnnotation"]=="preserve"):bytes(cacheFile)==before)&&bytes(selectedOverridesPath)==manualBefore;
                writerRows.push_back({{"variant",kind},{"accepted",accepted},{"expected",expected},{"record",result},{"passed",correct}});igdbSafe=igdbSafe&&correct;
            }
            writeJsonWithLocalBackup(p/L"igdb-selection-host-observed.json",{{"rows",igdbObserved},{"writers",writerRows}},false);
            check(igdbSafe,"Host plan or cache reused identity conflicting with selected IGDB ID");


            // The normal cached fast path must obey the same manual-choice
            // fence as buildLocalSteamPlanSnapshot, without rewriting its source.
            {
                const auto cached=snapshotCacheFixture(p/L"cp",1);json cachedObserved=json::array();bool cachedSafe=true;
                const auto original=cached.cache["items"][0];const auto path=jsonStringOr(original,"primaryExecutable"),key=toUtf8(normalizedPathKey(fs::path(toWide(path))));
                for(const auto* variant:{"igdb-conflict-owned","igdb-no-binding-owned","igdb-match-owned","igdb-numeric-owned","igdb-conflict-ready","igdb-conflict-not-selected","igdb-conflict-artwork","steam-conflict-artwork","native-ignore-choice","name-only","steam-overrides-igdb","idless-manual-import","cleared-id"}){
                    const std::string kind=variant;auto projected=cached.cache;auto& row=projected["items"][0];row["steamStoreAppId"]=883710;row["storefrontAppId"]=883710;row["igdbId"]=73;row["steamVerificationStatus"]="steam-verified";row["identityStatus"]="verified";row["formalName"]="Old official title";row["steamShortcutName"]="Retained shortcut title";row["customOwnedToken"]="keep-membership";row["shortAppId"]="2147483648";
                    const bool native=kind=="native-ignore-choice",idless=kind=="idless-manual-import",matching=kind=="igdb-match-owned",nameOnly=kind=="name-only",steamPriority=kind=="steam-overrides-igdb";
                    std::string status="added-to-steam";if(kind=="igdb-conflict-ready"||idless)status="ready-to-add";else if(kind=="igdb-conflict-not-selected")status="ready-not-selected";else if(kind.ends_with("artwork"))status="needs-minimum-artwork";if(native){status="already-in-steam";row["steamNative"]=true;row["nativeSteamAppId"]=883710;}row["status"]=status;
                    if(matching)row["igdbId"]=72;if(kind=="igdb-no-binding-owned")row.erase("igdbId");if(idless){row.erase("steamStoreAppId");row.erase("storefrontAppId");row.erase("igdbId");}
                    json manual={{"name","Accepted display name"},{"igdbId",72}};if(kind=="igdb-numeric-owned")manual["igdbId"]="72";if(kind=="steam-conflict-artwork"){manual.erase("igdbId");manual["steamId"]=620;}if(nameOnly)manual.erase("igdbId");if(steamPriority)manual["steamId"]=883710;if(kind=="cleared-id"){manual.erase("igdbId");manual["idCleared"]=true;}
                    const bool owned=status=="added-to-steam"||status=="already-in-steam";const bool invalidated=!native&&!idless&&!matching&&!nameOnly&&!steamPriority;
                    projected["summary"]={{"scannedGames",1},{"alreadyInSteam",owned?1:0},{"readyToAdd",status=="ready-to-add"?1:0},{"readyNotSelected",status=="ready-not-selected"?1:0},{"needsMinimumArtwork",status=="needs-minimum-artwork"?1:0},{"needsIdentity",0},{"needsSteamVerification",0}};
                    const auto config=g_dataRoot/L"config"/L"manual-overrides.json",source=workspacePlanPath(),scan=g_dataRoot/L"state"/L"library-scan.json";
                    writeJsonWithLocalBackup(config,{{"schemaVersion",1},{"items",{{key,manual}}},{"customNote","keep explicit selection"}},false);writeJsonWithLocalBackup(source,projected,false);const auto configBytes=bytes(config),planBytes=bytes(source),scanBytes=bytes(scan);
                    const auto actual=workspaceSnapshot(false);const auto& value=actual["steamPlan"];const auto item=value["items"][0];const auto& totals=value["summary"];
                    const int expectedApp=invalidated||idless?0:883710;const bool identity=jsonIntegerOr(item,"steamStoreAppId",0)==expectedApp;
                    const bool pending=invalidated&&!owned;const auto expectedStatus=pending?std::string("waiting-steam-verification"):status;
                    const bool counts=jsonIntegerOr(totals,"readyToAdd",0)==(status=="ready-to-add"&&!pending?1:0)&&jsonIntegerOr(totals,"readyNotSelected",0)==(status=="ready-not-selected"&&!pending?1:0)&&jsonIntegerOr(totals,"needsMinimumArtwork",0)==(status=="needs-minimum-artwork"&&!pending?1:0)&&jsonIntegerOr(totals,"needsIdentity",0)==(pending?1:0)&&jsonIntegerOr(totals,"needsSteamVerification",0)==(pending?1:0)&&!totals.contains("needsArtwork");
                    const bool membership=!owned||(steamPlanItemAlreadyOwned(item)&&jsonStringOr(item,"customOwnedToken")=="keep-membership"&&jsonStringOr(item,"shortAppId")=="2147483648");
                    const bool sourceUnchanged=bytes(config)==configBytes&&bytes(source)==planBytes&&bytes(scan)==scanBytes&&jsonStringOr(actual,"steamPlanSource")==toUtf8(source.wstring())&&jsonStringOr(actual["manualOverrides"],"customNote")=="keep explicit selection";
                    const bool passed=identity&&counts&&membership&&sourceUnchanged&&jsonStringOr(item,"status")==expectedStatus;cachedSafe=cachedSafe&&passed;
                    cachedObserved.push_back({{"variant",kind},{"identityCorrect",identity},{"countsCorrect",counts},{"membershipPreserved",membership},{"inputsAndCachedSourcePreserved",sourceUnchanged},{"expectedStatus",expectedStatus},{"item",item},{"summary",totals},{"passed",passed}});
                }
                writeJsonWithLocalBackup(p/L"cached-choice-projection-observed.json",cachedObserved,false);
                check(cachedSafe,"Cached workspace plan ignored explicit identity choice or retained stale minimum-artwork count");
            }
            check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Ownership boundary invoked external work");writeJsonWithLocalBackup(p/L"owned-plan-boundaries.json",{{"variants",observations},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);
        });
        test("discovercfg-native-metadata-stage-cost",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,80);fs::remove(workspacePlanPath());const auto snapshot=workspaceSnapshot(false);checkCachedSnapshot(snapshot,f,false);const auto& overrides=snapshot["manualOverrides"];const auto& games=snapshot["library"]["games"];json stages=json::object();
            stages["threeManualLookupsPerGame"]=measuredNativeWork(2,[&]{size_t count=0;for(const auto& game:games)for(int i=0;i<3;++i){const auto value=manualArtworkOverrideForExecutable(overrides,jsonStringOr(game,"primaryExecutable"));count+=value.contains("wallpaper");}return count;});
            stages["localPlan"]=measuredNativeWork(2,[&]{return buildLocalSteamPlanSnapshot(snapshot)["items"].size();});
            auto plan=buildLocalSteamPlanSnapshot(snapshot);stages["attachPreviews"]=measuredNativeWork(2,[&]{auto copy=plan;attachArtworkPreviews(copy);return copy["items"].size();});
            stages["identityCache"]=measuredNativeWork(2,[&]{auto copy=json::object();attachResolvedIdentityCache(copy);return copy["resolvedIdentities"].size();});
            stages["rootReadinessPerGame"]=measuredNativeWork(2,[&]{size_t ready=0;for(size_t i=0;i<f.count;++i)ready+=hostDataRootReadiness().ready;return ready;});
            stages["fullFallback"]=measuredSnapshotWork(f,2,false);check(stages["threeManualLookupsPerGame"]["rows"]==480&&stages["localPlan"]["rows"]==160&&stages["attachPreviews"]["rows"]==160&&stages["rootReadinessPerGame"]["rows"]==160,"Stage probe did not consume its declared workload");
            check(!fs::exists(workspacePlanPath())&&admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Stage probe published authority or external work");writeJsonWithLocalBackup(p/L"stage-work.json",{{"games",80},{"stages",stages},{"interpretation","Isolated function workloads, not additive or sampled full-pipeline attribution"},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);
        });
        test("discovercfg-manual-override-alias-and-lifecycle",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,4);fs::remove(workspacePlanPath());const auto alt=g_dataRoot/L"manual.png";text(alt,bytes(f.png));json overrides={{"schemaVersion",1},{"items",json::object()},{"customNote","preserve manual aliases"}};auto& items=overrides["items"];std::vector<std::string> exes,upper;
            for(const auto& row:f.cache["items"]){const auto exe=jsonStringOr(row,"primaryExecutable");exes.push_back(exe);auto key=toWide(exe);CharUpperBuffW(key.data(),static_cast<DWORD>(key.size()));upper.push_back(toUtf8(key));}
            items[upper[0]]={{"wallpaper",{{"file",toUtf8(alt.wstring())}}},{"idCleared",true},{"customNote","first valid alias"}};
            items[toUtf8(normalizedPathKey(fs::path(toWide(exes[0]))))]={{"wallpaper",{{"file",toUtf8(f.png.wstring())}}},{"customNote","later canonical"}};
            items[upper[1]]=false;items[toUtf8(normalizedPathKey(fs::path(toWide(exes[1]))))]={{"artworkProtection",{{"cover","deleted"}}},{"wallpaper",{{"file",toUtf8(alt.wstring())}}},{"customNote","skip malformed earlier alias"}};
            const auto relative=fs::relative(fs::path(toWide(exes[2])),fs::current_path());items[toUtf8(relative.wstring())]={{"artworkProtection",{{"wallpaper","deleted"}}},{"cover",{{"file",toUtf8(alt.wstring())}}},{"customNote","relative alias"}};
            for(int i=0;i<4;++i){const auto path=g_dataRoot/L"artwork"/std::to_wstring(i)/L"manifest.json";auto doc=readJson(path,json::object());doc["match"]={{"formalName","Observed "+std::to_string(i)}};text(path,doc.dump());}
            check(jsonStringOr(manualArtworkOverrideForExecutable(overrides,exes[1]),"customNote")=="skip malformed earlier alias","Raw helper stopped skipping nonobject alias");
            json observations=json::array();const auto verify=[&](const char* name){const auto raw=overrides.dump();text(g_dataRoot/L"config"/L"manual-overrides.json",raw);const auto snapshot=workspaceSnapshot(false);check(snapshot["steamPlan"]["items"].size()==4&&snapshot["manualOverrides"]["customNote"]=="preserve manual aliases","Manual lookup lost inventory or unknown user fields");for(size_t i=0;i<4;++i){const auto& row=snapshot["steamPlan"]["items"][i];const auto manual=manualArtworkOverrideForExecutable(snapshot["manualOverrides"],exes[i]);const auto doc=readJson(fs::path(toWide(jsonStringOr(row,"artworkManifest"))),json::object());check(row["artwork"]==hostArtworkReadiness(doc,manual),"Bulk manual lookup changed readiness/alias order");check(jsonStringOr(row,"formalName")== (jsonBoolOr(manual,"idCleared",false)?"Cached "+std::to_string(i):"Observed "+std::to_string(i)),"Bulk manual lookup changed clear-identity policy");const auto previews=jsonObjectOr(row,"artworkPreview"),protection=jsonObjectOr(manual,"artworkProtection");for(const auto& [slot,type]:{std::pair<const char*,const char*>{"cover","tall"},{"wallpaper","hero"}}){if(jsonStringOr(protection,slot)=="deleted")check(!previews.contains(type),"Deleted manual slot reappeared");else{const auto file=hostManifestArtworkSource(jsonObjectOr(manual,slot));const auto expected=file.empty()?f.png:file;check(jsonStringOr(jsonObjectOr(previews,type),"portableFile")==toUtf8(expected.wstring()),"Bulk manual lookup chose wrong alias artwork");}}}check(bytes(g_dataRoot/L"config"/L"manual-overrides.json")==raw&&!fs::exists(workspacePlanPath()),"Manual read rewrote config or cached authority");observations.push_back({{"name",name},{"items",4}});};
            verify("mixed-case-relative-and-first-valid-alias");items[upper[0]]["wallpaper"]["file"]=toUtf8(f.png.wstring());items[toUtf8(relative.wstring())].erase("artworkProtection");items[toUtf8(relative.wstring())]["wallpaper"]={{"file",toUtf8(alt.wstring())}};verify("next-snapshot-observes-manual-change");
            items[upper[0]]["artworkProtection"]={{"cover","deleted"}};const auto raw=overrides.dump();text(g_dataRoot/L"config"/L"manual-overrides.json",raw);auto cache=f.cache;const auto grid=p/L"Grid";text(grid/L"123p.png",bytes(f.png));cache["items"][0]["gridDirectory"]=toUtf8(grid.wstring());cache["items"][0]["shortAppId"]="123";text(workspacePlanPath(),cache.dump());const auto joined=workspaceSnapshot(false);check(!jsonObjectOr(joined["steamPlan"]["items"][0],"artworkPreview").contains("tall")&&bytes(g_dataRoot/L"config"/L"manual-overrides.json")==raw,"Joined Steam grid ignored first-alias deletion protection");observations.push_back({{"name","joined-grid-keeps-manual-deletion"},{"items",4}});
            for(const auto& bad:std::vector<json>{json(nullptr),json::array(),json(true),json{{"items",false}}})check(manualArtworkOverrideForExecutable(bad,exes[0]).empty(),"Malformed manual container supplied override");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Manual boundary invoked external work");writeJsonWithLocalBackup(p/L"manual-alias-boundaries.json",{{"variants",observations},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);
        });
        test("discovercfg-sparse-cached-manifest-lookup",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,80);const auto complete=observedSnapshotRead(f);std::string firstExe;
            for(const auto& entry:fs::recursive_directory_iterator(g_dataRoot/L"artwork"))if(entry.is_regular_file()&&entry.path().filename()==L"manifest.json"){firstExe=jsonStringOr(readJson(entry.path(),json::object()),"exe");break;}
            check(!firstExe.empty(),"No first owned manifest");auto cache=f.cache;bool changed=false;for(auto& item:cache["items"])if(samePath(fs::path(toWide(jsonStringOr(item,"primaryExecutable"))),fs::path(toWide(firstExe)))){item.erase("artworkManifest");changed=true;break;}check(changed,"No sparse cached item selected");text(workspacePlanPath(),cache.dump());const auto sparse=observedSnapshotRead(f);
            writeJsonWithLocalBackup(p/L"snapshot-sparse-work.json",{{"complete",complete},{"sparse",sparse},{"firstExecutable",firstExe},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);
            check(sparse["manifestReads"].get<uint64_t>()<=complete["manifestReads"].get<uint64_t>()+1,"Single missing cached artwork path scanned unrelated later manifests");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Sparse lookup invoked external work");
        });
        test("discovercfg-required-fallback-missing-artwork-cost",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,40);fs::remove(workspacePlanPath());for(int i=1;i<40;++i)text(g_dataRoot/L"artwork"/std::to_wstring(i)/L"manifest.json",i%2?"{":json{{"exe",""}}.dump());const auto observed=observedSnapshotRead(f,false);
            writeJsonWithLocalBackup(p/L"snapshot-missing-work.json",{{"observed",observed},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);check(!fs::exists(workspacePlanPath())&&admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Missing artwork gained authority or external work");check(observed["manifestReads"].get<uint64_t>()<=40*4,"Missing artwork restarted whole-tree lookup for each game");
        });
        test("discovercfg-required-fallback-process-work",[&](const auto& p){
            const auto one=snapshotCacheFixture(p/L"one",1);fs::remove(workspacePlanPath());const auto single=observedSnapshotRead(one,false);
            const auto many=snapshotCacheFixture(p/L"many",80);fs::remove(workspacePlanPath());const auto observed=observedSnapshotRead(many,false);const auto work=measuredSnapshotWork(many,2,false);
            const auto manualBytes=bytes(g_dataRoot/L"config"/L"manual-overrides.json"),libraryBytes=bytes(g_dataRoot/L"state"/L"library-scan.json");checkCachedSnapshot(workspaceSnapshot(false),many,false);
            check(!fs::exists(workspacePlanPath())&&bytes(g_dataRoot/L"config"/L"manual-overrides.json")==manualBytes&&bytes(g_dataRoot/L"state"/L"library-scan.json")==libraryBytes,"Required fallback rewrote local files or published cached authority");
            writeJsonWithLocalBackup(p/L"snapshot-fallback-work.json",{{"single",single},{"many",observed},{"work",work},{"fixtureRoot",toUtf8(many.root.wstring())},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);
            check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Required fallback invoked external work");
            check(single["manifestReads"].get<uint64_t>()>0&&observed["manifestReads"].get<uint64_t>()<=single["manifestReads"].get<uint64_t>()*80*2,"Required fallback repeated whole-tree artwork reads per game");
        });
        test("discovercfg-required-fallback-manifest-boundaries",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,4);fs::remove(workspacePlanPath());json observations=json::array();
            const auto manifest=[&](int i){return g_dataRoot/L"artwork"/std::to_wstring(i)/L"manifest.json";};
            const auto original0=bytes(manifest(0)),original1=bytes(manifest(1)),original2=bytes(manifest(2)),original3=bytes(manifest(3));
            const auto verify=[&](const char* name){const auto snapshot=workspaceSnapshot(false);checkCachedSnapshot(snapshot,f,false);for(const auto& item:snapshot["steamPlan"]["items"]){const auto exe=jsonStringOr(item,"primaryExecutable");const auto expected=findPortableArtworkManifestForHost(exe);check(jsonStringOr(item,"artworkManifest")== (expected?toUtf8(expected->second.wstring()):std::string{}),"Bulk fallback changed first matching manifest ownership");const auto previews=jsonObjectOr(item,"artworkPreview");check(previews.contains("tall")==bool(expected),"Fallback retained missing or foreign automatic cover");}observations.push_back({{"name",name},{"items",snapshot["steamPlan"]["items"].size()}});};
            verify("complete");text(manifest(1),"{");fs::remove(manifest(2));auto foreign=json::parse(original3);foreign["exe"]=toUtf8((p/L"Foreign"/L"game.exe").wstring());text(manifest(3),foreign.dump());
            text(g_dataRoot/L"artwork"/L"duplicate"/L"manifest.json",original0);text(g_dataRoot/L"artwork"/L"empty-exe"/L"manifest.json",json{{"exe",""}}.dump());text(g_dataRoot/L"artwork"/L"invalid-row"/L"manifest.json","[]");verify("damaged-missing-foreign-duplicate-empty");
            text(manifest(1),original1);text(manifest(2),original2);text(manifest(3),original3);verify("next-snapshot-recovers-repaired-files");
            fs::remove(manifest(0));fs::remove(g_dataRoot/L"artwork"/L"duplicate"/L"manifest.json");verify("next-snapshot-observes-deletion");
            auto library=readJson(g_dataRoot/L"state"/L"library-scan.json",json::object());const auto replacement=p/L"Games"/L"2"/L"replacement.exe";text(replacement,"owned replacement, never executed");library["games"][2]["primaryExecutable"]=toUtf8(replacement.wstring());text(g_dataRoot/L"state"/L"library-scan.json",library.dump());
            const auto changed=workspaceSnapshot(false);const auto& replaced=changed["steamPlan"]["items"][2];check(jsonStringOr(replaced,"primaryExecutable")==toUtf8(replacement.wstring())&&jsonStringOr(replaced,"artworkManifest").empty()&&!jsonObjectOr(replaced,"artworkPreview").contains("tall"),"Changed primary EXE borrowed old artwork identity");observations.push_back({{"name","changed-primary-does-not-borrow"},{"items",4}});
            check(!findPortableArtworkManifestForHost(""),"Empty EXE gained manifest ownership");g_shuttingDown=true;const auto stopped=findPortableArtworkManifestForHost(jsonStringOr(f.cache["items"][1],"primaryExecutable"));g_shuttingDown=false;check(!stopped,"Shutdown continued artwork lookup");
            check(!fs::exists(workspacePlanPath())&&admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Manifest boundary test published authority or external work");writeJsonWithLocalBackup(p/L"snapshot-manifest-boundaries.json",{{"variants",observations},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);
        });
        test("discovercfg-cached-snapshot-process-work",[&](const auto& p){
            const auto one=snapshotCacheFixture(p/L"one",1);const auto single=observedSnapshotRead(one);
            const auto many=snapshotCacheFixture(p/L"many",80);const auto observed=observedSnapshotRead(many);const auto work=measuredSnapshotWork(many,2);
            const auto planBytes=bytes(workspacePlanPath()),manualBytes=bytes(g_dataRoot/L"config"/L"manual-overrides.json");checkCachedSnapshot(workspaceSnapshot(false),many,true);
            check(bytes(workspacePlanPath())==planBytes&&bytes(g_dataRoot/L"config"/L"manual-overrides.json")==manualBytes,"Read-only cached snapshot rewrote user/cache files");
            const json report={{"single",single},{"many",observed},{"work",work},{"fixtureRoot",toUtf8(many.root.wstring())},{"sourceOnlyArtworkDecoding",true},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}};writeJsonWithLocalBackup(p/L"snapshot-work.json",report,false);
            check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Cached snapshot invoked an external port");
            check(single["manifestReads"].get<uint64_t>()>0&&observed["manifestReads"].get<uint64_t>()<=single["manifestReads"].get<uint64_t>()*80*2,"Complete cached snapshot repeated whole-tree artwork reads per game");
            // Exercise the complete cached snapshot path, including cancelled
            // refresh fallback, without replacing cache or user artwork bytes.
            const auto chosen=snapshotCacheFixture(p/L"manual-selection",4);
            const auto overridesPath=g_dataRoot/L"config"/L"manual-overrides.json";const auto originalOverrides=readJson(overridesPath,json::object());
            const auto originalPlan=chosen.cache;const auto sourceImage=bytes(chosen.png);text(g_worker,"owned cancelled-refresh marker; never launched");
            const std::array<const char*,4> statuses={"added-to-steam","ready-to-add","ready-not-selected","needs-minimum-artwork"};
            json observations=json::array();bool safe=true;
            for(const auto* name:{"changed","matching","numeric-string","explicit-content","cleared","name-only","unrestricted","native"}){
                const std::string kind=name;auto current=originalPlan,overrides=originalOverrides;current["summary"]={{"alreadyInSteam",1},{"readyToAdd",1},{"readyNotSelected",1},{"needsArtwork",1},{"needsIdentity",0},{"needsSteamVerification",0},{"steamNativeGames",0}};
                for(size_t i=0;i<4;++i){
                    auto& row=current["items"][i];const auto exe=jsonStringOr(row,"primaryExecutable");auto& manual=overrides["items"][toUtf8(normalizedPathKey(fs::path(toWide(exe))))];
                    manual["cover"]={{"file",toUtf8(chosen.png.wstring())}};manual["customUserNote"]="keep";
                    if(kind!="name-only")manual["steamId"]=kind=="explicit-content"?700:620;
                    if(kind=="numeric-string")manual["steamId"]="620";
                    if(kind=="cleared"){manual.erase("steamId");manual["idCleared"]=true;}
                    if(kind=="changed")manual.erase("name");
                    const int app=kind=="matching"||kind=="explicit-content"?620:kind=="unrestricted"?0:883710;
                    row["steamStoreAppId"]=app;row["storefrontAppId"]=app;row["identityStatus"]="verified";row["steamVerificationStatus"]="steam-verified";row["primaryProvider"]="steam";row["resolverVersion"]="steam-official-fuzzy-global-v4";row["identityTrust"]="current-v4-steam-verified";
                    row["status"]=statuses[i];row["directoryName"]="Local game "+std::to_string(i);row["formalName"]="Old verified title";row["releaseYear"]=1999;row["artwork"]={{"minimumComplete",true}};row["customCacheNote"]="keep";
                    if(i==0){row["steamShortcutName"]="Stored shortcut label";row["shortAppId"]="2147483648";row["storedAppId"]=-2147483648;}
                    if(kind=="explicit-content"){row["matchedContentAppId"]=700;row["baseGameAppId"]=620;row["identityKind"]="steam-base-game-with-explicit-content";}
                    if(kind=="native"&&i==0){row["status"]="steam-native";row["steamNative"]=true;row["nativeSteamAppId"]=883710;current["summary"]["alreadyInSteam"]=0;current["summary"]["steamNativeGames"]=1;}
                    if(kind=="unrestricted"){row.erase("identityStatus");row.erase("steamVerificationStatus");}
                }
                text(workspacePlanPath(),current.dump());text(overridesPath,overrides.dump());const auto planBefore=bytes(workspacePlanPath()),overridesBefore=bytes(overridesPath);
                for(bool cancelledRefresh:{false,true}){
                    auto cancelled=std::make_shared<BackgroundJobControl>();cancelled->cancelRequested=true;
                    const auto value=cancelledRefresh?workspaceSnapshot(true,g_steamTarget,cancelled):workspaceSnapshot(false);bool rowsCorrect=true;
                    const bool invalid=kind=="changed"||kind=="numeric-string"||kind=="cleared"||kind=="native";
                    for(size_t i=0;i<4;++i){
                        const auto& row=value["steamPlan"]["items"][i];const bool native=kind=="native"&&i==0;const bool conflict=invalid&&!native;
                        const int expected=conflict?0:jsonIntegerOr(current["items"][i],"steamStoreAppId",0);
                        const auto expectedStatus=conflict&&i>0?"waiting-steam-verification":jsonStringOr(current["items"][i],"status");
                        const bool identity=jsonIntegerOr(row,"steamStoreAppId",0)==expected&&jsonStringOr(row,"status")==expectedStatus;
                        const bool metadata=!conflict||(!row.contains("identityTrust")&&!row.contains("releaseYear")&&!row.contains("artwork")&&row["steamVerificationStatus"]=="needs-confirmation");
                        const bool preserved=row["customCacheNote"]=="keep"&&(i!=0||(row["shortAppId"]=="2147483648"&&row["storedAppId"]==-2147483648));
                        const auto previews=jsonObjectOr(row,"artworkPreview");const bool artwork=native||(previews.contains("tall")&&previews.contains("hero"));rowsCorrect=rowsCorrect&&identity&&metadata&&preserved&&artwork;
                    }
                    const auto summary=value["steamPlan"]["summary"];const bool summaryCorrect=jsonIntegerOr(summary,"readyToAdd",0)==(invalid?0:1)&&jsonIntegerOr(summary,"readyNotSelected",0)==(invalid?0:1)&&jsonIntegerOr(summary,"needsArtwork",0)==(invalid?0:1)&&jsonIntegerOr(summary,"needsIdentity",0)==(invalid?3:0)&&jsonIntegerOr(summary,"needsSteamVerification",0)==(invalid?3:0)&&jsonIntegerOr(summary,"alreadyInSteam",0)==(kind=="native"?0:1);
                    const bool untouched=bytes(workspacePlanPath())==planBefore&&bytes(overridesPath)==overridesBefore&&bytes(chosen.png)==sourceImage;
                    observations.push_back({{"variant",kind},{"cancelledRefresh",cancelledRefresh},{"rowsCorrect",rowsCorrect},{"summaryCorrect",summaryCorrect},{"filesUntouched",untouched},{"snapshot",value}});safe=safe&&rowsCorrect&&summaryCorrect&&untouched;
                }
            }
            writeJsonWithLocalBackup(p/L"cached-manual-selection-observed.json",observations,false);
            check(observations.size()==16&&safe,"Cached snapshot revived identity inconsistent with manual selection");
            check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Manual cached snapshot invoked an external port");
        });
        test("discovercfg-account-reply-payload-ignores-later-cache",[&](const fs::path& p){
            const auto output=g_dataRoot/L"state"/L"steam-accounts.json";
            const json prior={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",json::array({{{"accountId","1001"},{"steamRoot","owned-old"}}})}};
            const json current={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",json::array({{{"accountId","2002"},{"steamRoot","owned-current"}}})},{"unknownAnnotation","preserved"}};
            const std::string ack="ACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_STATUS: current\n";
            const auto wire=[&](const json& report){return ack+"STEAM_ACCOUNTS_JSON: "+report.dump()+"\n";};
            wchar_t saved[32768]{};const auto savedSize=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",saved,32768);
            SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();
            struct Restore {std::wstring saved;~Restore(){manualEnrichmentFixtureWorker.clear();SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",saved.empty()?nullptr:saved.c_str());}} restore{savedSize&&savedSize<32768?saved:L""};
            const auto call=[&](const std::string& replyText,const std::string& cache,int workerExit=0){text(g_dataRoot/L"account-receipt-worker.json",json{{"stdout",replyText},{"rawFile",cache},{"exitCode",workerExit}}.dump());return readSteamAccounts();};
            json observations=json::array();
            for(const auto& cache:std::vector<std::string>{prior.dump(),"{","{\"schemaVersion\":99}"}){
                check(call(wire(current),cache)==current,"Account reply was replaced by the later shared cache");
                check(bytes(output)==cache,"Host rewrote the later shared account cache");
                observations.push_back({{"kind","later-cache-ignored"},{"cache",cache}});
            }
            auto bad=current;bad["accounts"][0]["readError"]="locked";
            const std::vector<std::string> rejected={ack,ack+"STEAM_ACCOUNTS_JSON: {\n",wire(current)+"STEAM_ACCOUNTS_JSON: "+current.dump()+"\n",wire(current)+"STEAM_ACCOUNTS_STATUS: unknown\n",wire(current)+"ACCOUNTS_WRITE_CONFIRMED: false\n",wire(current)+"WORKER_OUTPUT_TRUNCATED\n",wire(json{{"steamStatus","ready"},{"accounts",json::array()}}),wire(bad),"STEAM_ACCOUNTS_STATUS: unknown\nACCOUNTS_WRITE_CONFIRMED: false\n"};
            for(const auto& response:rejected){
                const auto result=call(response,prior.dump());check(jsonStringOr(result,"steamStatus")=="error"&&jsonArrayOr(result,"accounts").empty(),"Unconfirmed payload reused shared account authority");
                check(bytes(output)==prior.dump(),"Rejected reply changed retained cache");
                check(call(wire(current),prior.dump())==current,"Explicit payload recheck did not recover");
                observations.push_back({{"kind","rejected-and-recovered"},{"reply",response}});
            }
            const auto nonzero=call(wire(current),prior.dump(),1);check(jsonStringOr(nonzero,"steamStatus")=="error","Nonzero account worker was accepted");
            const std::string privateMarker="owned-account-receipt-not-for-display";
            const std::string envelope="STEAM_ACCOUNTS: 1\nMULTIPLE_STEAM_ACCOUNTS: false\nEXPLICIT_ACCOUNT_SELECTION_REQUIRED: false\nOUTPUT: "+privateMarker+"\n";
            std::string longDetail="可读错误：";for(int i=0;i<800;++i)longDetail+="短";
            struct DisplayCase{std::string label,wire,detail;int exit;};
            const std::vector<DisplayCase> displayCases={
                {"valid-body-nonzero",envelope+wire(current),"",1},
                {"malformed-with-detail",envelope+ack+"STEAM_ACCOUNTS_JSON: {\"fixture\":\""+privateMarker+"\"\nfixture-account-file-locked\n","fixture-account-file-locked",0},
                {"blank"," \t\r\n","",23},
                {"oversized-unicode",longDetail,longDetail.substr(0,15),23}
            };
            json displayObservations=json::array();
            for(const auto& row:displayCases){
                const auto result=call(row.wire,prior.dump(),row.exit);
                const auto error=jsonStringOr(result,"error");
                const bool leaked=error.find("STEAM_ACCOUNTS")!=std::string::npos||error.find("ACCOUNTS_WRITE_CONFIRMED")!=std::string::npos||error.find(privateMarker)!=std::string::npos||error.find("EXPLICIT_ACCOUNT_SELECTION_REQUIRED")!=std::string::npos;
                const json observed={{"label",row.label},{"errorBytes",error.size()},{"leakedMachineFields",leaked}};
                writeJsonWithLocalBackup(p/L"account-display-last-observed.json",observed,false);
                check(!leaked,"Machine account receipt leaked into the display error");
                check(jsonStringOr(result,"steamStatus")=="error"&&jsonArrayOr(result,"accounts").empty()&&bytes(output)==prior.dump(),"Display formatting changed account authority or its cache");
                check(!trimHost(error).empty()&&error.size()<=1600,"Account display error was blank or exceeded its text budget");
                if(!row.detail.empty())check(error.starts_with(row.detail),"Account display error discarded useful text");
                check(json::parse(json(error).dump())==error,"Capped display error is not valid UTF-8");
                displayObservations.push_back(observed);
            }
            writeJsonWithLocalBackup(p/L"account-display-observations.json",displayObservations,false);
            for(const auto* status:{"not-installed","installed-no-account"}){
                const json empty={{"schemaVersion",1},{"steamStatus",status},{"accounts",json::array()}};check(call(wire(empty),prior.dump())==empty,"Current empty account reply lost");
            }
            check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Account payload consumer escaped controlled owned port");
            writeJsonWithLocalBackup(p/L"account-payload-observations.json",{{"observations",observations},{"scope","production Host/owned subprocess/pipe/Job; controlled Worker reply; no real Steam/network"}},false);
        });
        test("discovercfg-account-write-errors-retain-discovery-cause",[&](const fs::path& p){
            wchar_t variant[64]{};const auto size=GetEnvironmentVariableW(L"YMCC_HOST_SNAPSHOT_ACCOUNT_VARIANT",variant,64);

            if(size&&size<64&&std::wstring(variant)==L"add-partial-completion"){
                const auto savedTarget=steamTargetSnapshot();const auto savedData=g_dataRoot,savedLab=g_labRoot,savedWorker=g_worker;
                const auto allowed=manualEnrichmentFixtureWorker;wchar_t env[32768]{};const auto n=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env,32768);
                struct Restore{json target;fs::path data,lab,worker;std::wstring allowed,env;~Restore(){
                    ownedAddPartialSteamFacts=false;activeAccountFixture=false;activeAccountRoot.clear();setSteamTarget(target);g_dataRoot=data;g_labRoot=lab;
                    g_worker=worker;manualEnrichmentFixtureWorker=allowed;SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env.empty()?nullptr:env.c_str());
                }} restore{savedTarget,savedData,savedLab,savedWorker,allowed,n&&n<32768?env:L""};
                g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();ownedAddPartialSteamFacts=true;
                json rows=json::array();bool safe=true;size_t index=0;
                for(const auto* label:{"single-current-confirmed","batch-current-unconfirmed","readback-unconfirmed","first-commit-failed","preflight-failed","all-confirmed"}){
                    const std::string kind=label;const auto f=snapshotCacheFixture(p/std::to_wstring(index++),2);const auto steam=f.root/L"Steam";
                    SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());
                    activeAccountFixture=true;activeAccountUser=kind=="batch-current-unconfirmed"?1002:1001;activeAccountRoot=steam.wstring();
                    const size_t selectedCount=kind=="single-current-confirmed"?1:2;
                    json selected=json::array(),accounts=json::array(),plans=json::object(),completed=json::object();
                    for(size_t i=0;i<selectedCount;++i)selected.push_back(f.cache["items"][i]["gameDirectory"]);
                    for(const auto* id:{"1001","1002"}){
                        auto plan=f.cache;plan["accountId"]=id;plan["shortcutsVdf"]=toUtf8((steam/L"userdata"/toWide(id)/L"config"/L"shortcuts.vdf").wstring());
                        accounts.push_back({{"accountId",id},{"shortcutsVdf",plan["shortcutsVdf"]},{"steamRoot",toUtf8(steam.wstring())},{"readError",nullptr}});
                        for(auto& item:plan["items"])item["status"]="ready-to-add";
                        plans[id]=plan;plan["mode"]="steam-library-add-commit";plan["committed"]=true;
                        for(size_t i=0;i<selectedCount;++i)plan["items"][i]["status"]="added-to-steam";
                        completed[id]=plan;
                    }
                    const auto all=g_dataRoot/L"state"/L"last-steam-add-all-accounts.json";
                    if(kind=="batch-current-unconfirmed"){
                        auto old=completed["1001"];old["mode"]="steam-library-add-commit-all-accounts";old["allSteamAccounts"]=true;
                        text(all,old.dump());
                    }
                    const auto report=json{{"schemaVersion",1},{"steamStatus","ready"},{"accounts",accounts}};
                    json port={{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+report.dump()+"\n"},
                        {"plansByAccount",plans},{"completionsByAccount",completed}};
                    if(kind=="preflight-failed")port["rejectPlanAccount"]="1002";
                    else if(kind!="all-confirmed"){
                        port["faultAccount"]=kind=="first-commit-failed"?"1001":"1002";
                        port["faultKind"]=kind=="readback-unconfirmed"?"missing-receipt":"commit-failed";
                    }
                    text(g_dataRoot/L"account-receipt-worker.json",port.dump());
                    const json request={{"selectionMode","all-steam-accounts"},{"selectedGameDirectories",selected}};setSteamTarget(request);
                    json result;std::string error;try{result=commitToAllSteamAccounts(request);}catch(const std::exception& e){error=e.what();}
                    const auto snap=workspaceSnapshot(false,request),disk=readJson(all,json::object());std::set<std::string> commits;size_t planCalls=0;
                    for(const auto& file:fs::directory_iterator(g_dataRoot)){
                        const auto name=file.path().filename().wstring();
                        if(name.starts_with(L"account-commit-call-"))commits.insert(jsonStringOr(readJson(file.path(),json::object()),"accountId"));
                        else if(name.starts_with(L"account-plan-call-"))++planCalls;
                    }
                    const bool partial=kind!="first-commit-failed"&&kind!="preflight-failed"&&kind!="all-confirmed";
                    bool passed=false;
                    if(partial){
                        const bool currentConfirmed=kind!="batch-current-unconfirmed";
                        const auto results=jsonArrayOr(result,"accountResults");
                        const bool projection=currentConfirmed?jsonStringOr(result,"accountId")=="1001"&&disk==result:
                            jsonStringOr(result,"accountId").empty()&&jsonStringOr(snap["steamPlan"],"accountId")!="1001"&&jsonStringOr(disk,"accountId")=="1001";
                        passed=error.empty()&&results.size()==1&&results.front()==completed["1001"]&&
                            result.value("displayAccountCompletionConfirmed",!currentConfirmed)==currentConfirmed&&projection&&
                            jsonStringOr(result["unconfirmedAccount"],"accountId")=="1002"&&result.value("skippedAccountCount",-1)==0&&
                            !jsonStringOr(result,"accountCommitWarning").empty()&&planCalls==2&&commits==std::set<std::string>{"1001","1002"};
                    }else if(kind=="all-confirmed"){
                        passed=error.empty()&&jsonArrayOr(result,"accountResults").size()==2&&jsonStringOr(result,"accountId")=="1001"&&
                            jsonStringOr(result,"accountCommitWarning").empty()&&disk==result&&planCalls==2&&commits.size()==2;
                    }else{
                        passed=!error.empty()&&!fs::exists(all)&&planCalls==2&&(kind=="preflight-failed"?commits.empty():commits==std::set<std::string>{"1001"});
                    }
                    rows.push_back({{"variant",kind},{"passed",passed},{"result",result},{"currentPlanAccount",jsonStringOr(snap["steamPlan"],"accountId")},
                        {"error",error},{"commitAccounts",commits},{"planCalls",planCalls}});safe=safe&&passed;
                }
                writeJsonWithLocalBackup(p/L"add-partial-completion-observed.json",{{"rows",rows}},false);
                check(safe,"All-account add lost a verified completion or misreported the affected accounts");
                check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Partial add escaped owned ports");return;
            }


            if(size&&size<64&&std::wstring(variant)==L"add-result-binding"){
                const auto f=snapshotCacheFixture(p,1);const auto savedTarget=steamTargetSnapshot();const auto savedWorker=g_worker;const auto savedAllowed=manualEnrichmentFixtureWorker;wchar_t env[32768]{};const auto n=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env,32768);
                struct Restore{json target;fs::path worker;std::wstring allowed,env;~Restore(){setSteamTarget(target);g_worker=worker;manualEnrichmentFixtureWorker=allowed;SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env.empty()?nullptr:env.c_str());}}restore{savedTarget,savedWorker,savedAllowed,n&&n<32768?env:L""};
                SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();
                const auto target=steamTargetSnapshot();const json accounts={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",json::array({target})}};const auto control=g_dataRoot/L"account-receipt-worker.json",account=g_dataRoot/L"state"/L"last-steam-add-1001.json",global=g_dataRoot/L"state"/L"last-steam-add.json",all=g_dataRoot/L"state"/L"last-steam-add-all-accounts.json";
                const auto count=[&](){size_t total=0;for(const auto& e:fs::directory_iterator(g_dataRoot))if(e.path().filename().wstring().starts_with(L"account-commit-call-"))++total;return total;};json rows=json::array();bool safe=true;
                for(const auto* label:{"normal","known-noop","global-selection-changed","missing-schema","future-schema","schema-string","duplicate-root","duplicate-nested","unknown-noop","items-object","foreign-account","foreign-installation","wrong-library","missing-receipt","wrong-digest"}){
                    const std::string kind=label;setSteamTarget(target);std::error_code ignored;fs::remove(account,ignored);fs::remove(global,ignored);fs::remove(all,ignored);auto completed=f.cache;completed["mode"]="steam-library-add-commit";completed["committed"]=true;completed["customNote"]="current add completion";
                    if(kind=="known-noop"||kind=="unknown-noop"){completed["committed"]=false;if(kind=="known-noop")completed["reason"]="nothing-ready-to-add";}
                    if(kind=="missing-schema")completed.erase("schemaVersion");if(kind=="future-schema")completed["schemaVersion"]=99;if(kind=="schema-string")completed["schemaVersion"]="1";if(kind=="items-object")completed["items"]=json::object();if(kind=="foreign-account")completed["accountId"]="1002";if(kind=="foreign-installation")completed["shortcutsVdf"]=toUtf8((p/L"OtherSteam"/L"userdata"/L"1001"/L"config"/L"shortcuts.vdf").wstring());if(kind=="wrong-library")completed["libraryState"]=toUtf8((p/L"OtherLibrary"/L"state"/L"library-scan.json").wstring());
                    if(kind=="duplicate-nested")completed["probe"]={{"key","second"}};
                    json response={{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+accounts.dump()+"\n"},{"planResult",f.cache},{"commitResult",completed},{"addBindingPort",true}};
                    if(kind=="duplicate-root"){auto raw=completed.dump();raw.insert(1,"\"committed\":false,");response["rawAccountResult"]=raw;}
                    if(kind=="duplicate-nested"){auto raw=completed.dump();const std::string marker="\"key\":\"second\"";const auto at=raw.find(marker);check(at!=std::string::npos,"Owned duplicate marker absent");raw.replace(at,marker.size(),"\"key\":\"first\",\"key\":\"second\"");response["rawAccountResult"]=raw;}
                    if(kind=="missing-receipt")response["commitReceiptText"]="";if(kind=="wrong-digest")response["commitReceiptText"]="STEAM_ADD_RESULT_RECEIPT: sha256-v1:"+std::string(64,'0')+"\n";
                    if(kind=="global-selection-changed"){auto changed=target;changed["accountId"]="1002";changed["shortcutsVdf"]=toUtf8((p/L"Steam"/L"userdata"/L"1002"/L"config"/L"shortcuts.vdf").wstring());setSteamTarget(changed);}
                    text(control,response.dump());const auto before=count();json got;std::string error;try{got=commitToAllSteamAccounts({{"selectedGameDirectories",json::array({f.cache["items"][0]["gameDirectory"]})}});}catch(const std::exception& e){error=e.what();}
                    const bool reject=kind!="normal"&&kind!="known-noop"&&kind!="global-selection-changed";const bool one=count()==before+1;const bool correct=reject?!error.empty()&&!fs::exists(all):error.empty()&&jsonArrayOr(got,"accountResults").size()==1&&jsonStringOr(got["accountResults"][0],"accountId")=="1001"&&got["accountResults"][0]["committed"]==json(kind!="known-noop");const bool selection=kind!="global-selection-changed"||jsonStringOr(steamTargetSnapshot(),"accountId")=="1002";const bool passed=correct&&one&&selection;
                    rows.push_back({{"variant",kind},{"passed",passed},{"expectedReject",reject},{"singleCommit",one},{"selectionPreserved",selection},{"error",error},{"result",got}});safe=safe&&passed;
                }
                writeJsonWithLocalBackup(p/L"add-result-binding-observed.json",{{"rows",rows}},false);check(rows.size()==15&&safe,"Completed add accepted malformed/foreign result or used mutable global account");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Add result binding escaped owned ports");return;
            }
            if(size&&size<64&&std::wstring(variant)==L"delete-resume"){
                const auto f=snapshotCacheFixture(p,1);const auto target=steamTargetSnapshot();const auto worker=g_worker;const auto allowed=manualEnrichmentFixtureWorker;wchar_t env[32768]{};const auto n=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env,32768);
                struct Restore{json target;fs::path worker;std::wstring allowed,env;~Restore(){setSteamTarget(target);g_worker=worker;manualEnrichmentFixtureWorker=allowed;SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env.empty()?nullptr:env.c_str());}}restore{target,worker,allowed,n&&n<32768?env:L""};
                SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();
                auto second=target;second["accountId"]="1002";second["shortcutsVdf"]=toUtf8((p/L"Steam"/L"userdata"/L"1002"/L"config"/L"shortcuts.vdf").wstring());second["readError"]=nullptr;
                const json accounts={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",json::array({target,second})}},request={{"selectedGameDirectories",json::array({f.cache["items"][0]["gameDirectory"]})}};
                const auto control=g_dataRoot/L"account-receipt-worker.json",statePath=g_dataRoot/L"delete-resume-state.json",all=g_dataRoot/L"state"/L"last-steam-delete-all-accounts.json";json rows=json::array();bool safe=true;
                for(const auto* label:{"partial-then-user-retry","all-already-absent","first-account-failure"}){
                    const std::string kind=label;std::error_code ignored;fs::remove(all,ignored);json state={{"absent",kind=="all-already-absent"?json::array({"1001","1002"}):json::array()},{"calls",json::array()}};writeJsonWithLocalBackup(statePath,state,false);
                    const std::string fail=kind=="partial-then-user-retry"?"1002":kind=="first-account-failure"?"1001":"";text(control,json{{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+accounts.dump()+"\n"},{"deleteResumePort",true},{"failAccount",fail}}.dump());
                    json first;std::string error;try{first=deleteFromAllSteamAccounts(request);}catch(const std::exception& e){error=e.what();}const auto beforeRetry=readJson(statePath,json::object());bool passed;
                    if(kind=="partial-then-user-retry"){
                        const auto partial=readJson(all,json::object());const bool honest=!error.empty()&&error.find("owned second-account commit failure")!=std::string::npos&&jsonStringOr(partial,"mode")=="steam-library-delete-partial"&&!jsonBoolOr(partial,"committed",true)&&partial.value("completedAccountCount",0)==1&&jsonArrayOr(partial,"accountResults").size()==1&&beforeRetry["absent"]==json::array({"1001"})&&beforeRetry["calls"].size()==4;
                        json retry;std::string retryError;try{retry=deleteFromAllSteamAccounts(request);}catch(const std::exception& e){retryError=e.what();}const auto afterRetry=readJson(statePath,json::object());passed=honest&&retryError.empty()&&jsonArrayOr(retry,"accountResults").size()==2&&afterRetry["absent"].size()==2&&afterRetry["calls"].size()==8;rows.push_back({{"variant",kind},{"passed",passed},{"firstError",error},{"retryError",retryError},{"partial",partial},{"calls",afterRetry["calls"]}});
                    }else{passed=kind=="first-account-failure"?!error.empty()&&!fs::exists(all)&&beforeRetry["absent"].empty()&&beforeRetry["calls"].size()==3:error.empty()&&jsonArrayOr(first,"accountResults").size()==2&&beforeRetry["calls"].size()==4;rows.push_back({{"variant",kind},{"passed",passed},{"error",error},{"state",beforeRetry}});}
                    safe=safe&&passed;
                }
                writeJsonWithLocalBackup(p/L"delete-resume-observed.json",{{"rows",rows}},false);check(safe,"All-account deletion retry blocked on prior success or reported false completion");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Delete resume escaped owned ports");return;
            }
            if(size&&size<64&&std::wstring(variant)==L"action-completion"){
                const auto f=snapshotCacheFixture(p,1);const auto savedTarget=steamTargetSnapshot();const auto worker=g_worker;const auto allowed=manualEnrichmentFixtureWorker;wchar_t env[32768]{};const auto n=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env,32768);
                struct Restore{json target;fs::path worker;std::wstring allowed,env;~Restore(){setSteamTarget(target);g_worker=worker;manualEnrichmentFixtureWorker=allowed;SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env.empty()?nullptr:env.c_str());}}restore{savedTarget,worker,allowed,n&&n<32768?env:L""};
                SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();
                const auto target=steamTargetSnapshot();const json accounts={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",json::array({target})}};
                const auto count=[&](){size_t total=0;for(const auto& e:fs::directory_iterator(g_dataRoot))if(e.path().filename().wstring().starts_with(L"account-action-commit-"))++total;return total;};
                json rows=json::array();bool safe=true;
                for(const auto* operation:{"refresh","delete"})for(const auto* fault:{"normal","stale-output","global-fallback","warning-drift","both-unavailable","foreign-account","foreign-installation","missing-receipt","duplicate","wrong-hash","not-committed","wrong-mode","nonzero","readback-changed","future-schema","schema-type"}){
                    const std::string action=operation,kind=fault;const bool refresh=action=="refresh";const auto output=g_dataRoot/L"state"/(refresh?L"steam-artwork-refresh-1001.json":L"steam-delete-plan-1001.json"),global=g_dataRoot/L"state"/(refresh?L"last-steam-artwork-refresh.json":L"last-steam-delete.json"),all=g_dataRoot/L"state"/(refresh?L"last-steam-artwork-refresh-all-accounts.json":L"last-steam-delete-all-accounts.json");
                    std::error_code ignored;fs::remove(global,ignored);fs::remove(all,ignored);auto result=f.cache;result["mode"]=refresh?"steam-artwork-refresh":"steam-library-delete-commit";result["committed"]=true;result["customNote"]="actual completed action";
                    auto stale=result;stale["committed"]=false;stale["customNote"]="old requested output";text(output,stale.dump());
                    if(kind=="foreign-account")result["accountId"]="2002";if(kind=="foreign-installation")result["shortcutsVdf"]=toUtf8((p/L"OtherSteam"/L"userdata"/L"1001"/L"config"/L"shortcuts.vdf").wstring());if(kind=="future-schema")result["schemaVersion"]=99;if(kind=="schema-type")result["schemaVersion"]="1";if(kind=="not-committed")result["committed"]=false;if(kind=="wrong-mode")result["mode"]="steam-library-add-plan";
                    json response={{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+accounts.dump()+"\n"},{"actionResult",result}};
                    if(kind=="stale-output"||kind=="global-fallback"||kind=="both-unavailable")response["publishActionOutput"]=false;
                    if(kind=="stale-output"||kind=="global-fallback")response["publishActionGlobal"]=true;
                    if(kind=="warning-drift"){auto changed=result;changed["resultPublicationWarnings"]=json::array({{{"error","earlier request output failed"}}});response["actionOutputResult"]=changed;}
                    const auto prefix=refresh?"STEAM_REFRESH_RESULT_RECEIPT: sha256-v1:":"STEAM_DELETE_RESULT_RECEIPT: sha256-v1:";const auto receipt=std::string(prefix)+custom_steam_library::plan_receipt::addCompletionDigest(result)+"\n";
                    if(kind=="missing-receipt")response["actionReceiptText"]="";if(kind=="duplicate")response["actionReceiptText"]=receipt+receipt;if(kind=="wrong-hash")response["actionReceiptText"]=std::string(prefix)+std::string(64,'0')+"\n";if(kind=="nonzero")response["actionExit"]=1;
                    if(kind=="readback-changed"){auto changed=result;changed["customNote"]="later writer";response["actionOutputResult"]=changed;}
                    text(g_dataRoot/L"account-receipt-worker.json",response.dump());const auto before=count();json got;std::string error;try{got=refresh?refreshArtworkForAllSteamAccounts({{"selectedGameDirectories",json::array({f.cache["items"][0]["gameDirectory"]})}}):deleteFromAllSteamAccounts({{"selectedGameDirectories",json::array({f.cache["items"][0]["gameDirectory"]})}});}catch(const std::exception& e){error=e.what();}
                    const bool rejected=kind!="normal"&&kind!="stale-output"&&kind!="global-fallback"&&kind!="warning-drift";const bool single=count()==before+1;const bool honest=rejected?!error.empty()&&!fs::exists(all):error.empty()&&jsonArrayOr(got,"accountResults").size()==1&&jsonStringOr(got["accountResults"][0],"customNote")=="actual completed action"&&got["accountResults"][0]["committed"]==true;
                    const bool warning=kind!="both-unavailable"||error.find("勿重复操作")!=std::string::npos;const bool passed=honest&&single&&warning;rows.push_back({{"operation",action},{"fault",kind},{"passed",passed},{"expectedReject",rejected},{"singleCommit",single},{"error",error},{"result",got}});safe=safe&&passed;
                }
                writeJsonWithLocalBackup(p/L"action-completion-observed.json",{{"rows",rows}},false);check(rows.size()==32&&safe,"Steam action completion consumed stale output or lost verified fallback");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Action completion escaped owned ports");return;
            }
            if(size&&size<64&&std::wstring(variant)==L"add-completion"){
                const auto f=snapshotCacheFixture(p,1);const auto previousTarget=steamTargetSnapshot();const auto previousWorker=g_worker;const auto previousAllowed=manualEnrichmentFixtureWorker;
                wchar_t previous[32768]{};const auto n=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",previous,32768);
                struct Restore{json target;fs::path worker;std::wstring allowed,environment;~Restore(){setSteamTarget(target);g_worker=worker;manualEnrichmentFixtureWorker=allowed;SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",environment.empty()?nullptr:environment.c_str());}} restore{previousTarget,previousWorker,previousAllowed,n&&n<32768?previous:L""};
                SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();
                const auto target=steamTargetSnapshot();const json accounts={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",json::array({target})}};
                const auto control=g_dataRoot/L"account-receipt-worker.json",output=g_dataRoot/L"state"/L"steam-add-plan-1001.json",account=g_dataRoot/L"state"/L"last-steam-add-1001.json",global=g_dataRoot/L"state"/L"last-steam-add.json",all=g_dataRoot/L"state"/L"last-steam-add-all-accounts.json";
                const auto count=[&]{size_t total=0;for(const auto& entry:fs::directory_iterator(g_dataRoot))if(entry.path().filename().wstring().starts_with(L"account-commit-call-"))++total;return total;};
                json observations=json::array();
                for(const auto* kind:{"stale-output","output","global-fallback","noop","warning-drift","all-unavailable","foreign","missing","duplicate","nonzero"}){
                    std::error_code ignored;fs::remove(account,ignored);fs::remove(global,ignored);fs::remove(all,ignored);
                    auto completed=f.cache;completed["mode"]="steam-library-add-commit";completed["committed"]=std::string(kind)!="noop";completed["customNote"]="actual completed result";if(std::string(kind)=="noop")completed["reason"]="nothing-ready-to-add";
                    auto plan=f.cache;plan["customNote"]="read-only plan";
                    json response={{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+accounts.dump()+"\n"},{"planResult",plan},{"commitResult",completed}};
                    const std::string k=kind;
                    if(k=="output")response["publishOutput"]=true;
                    if(k=="global-fallback"){response["publishAccount"]=false;response["publishGlobal"]=true;}
                    if(k=="all-unavailable")response["publishAccount"]=false;
                    if(k=="warning-drift"){auto saved=completed;saved["resultPublicationWarnings"]=json::array({{{"error","earlier output failed"}}});response["accountResult"]=saved;}
                    if(k=="foreign"){auto saved=completed;saved["accountId"]="2002";response["accountResult"]=saved;}
                    if(k=="missing")response["commitReceiptText"]="";
                    if(k=="duplicate"){auto content=completed;content.erase("resultPublicationWarnings");const auto receipt="STEAM_ADD_RESULT_RECEIPT: sha256-v1:"+custom_steam_library::plan_receipt::digest(content)+"\n";response["commitReceiptText"]=receipt+receipt;}
                    if(k=="nonzero")response["commitExit"]=1;
                    text(control,response.dump());const auto before=count();json result;std::string error;
                    try{result=commitToAllSteamAccounts({{"selectedGameDirectories",json::array({f.cache["items"][0]["gameDirectory"]})}});}catch(const std::exception& e){error=e.what();}
                    writeJsonWithLocalBackup(p/L"add-completion-last-observed.json",{{"fault",kind},{"result",result},{"error",error},{"commitCalls",count()-before}},false);
                    const bool reject=k=="all-unavailable"||k=="foreign"||k=="missing"||k=="duplicate"||k=="nonzero";
                    if(reject){check(!error.empty()&&!fs::exists(all),"Unconfirmed completion was published as success");if(k=="all-unavailable")check(error.find("勿重复加入")!=std::string::npos,"Completed write lost explicit readback uncertainty");}
                    else{check(error.empty()&&jsonStringOr(result["accountResults"][0],"mode")=="steam-library-add-commit"&&jsonStringOr(result["accountResults"][0],"customNote")=="actual completed result","Completed add consumed a stale read-only plan");check(result["accountResults"][0]["committed"]==(k!="noop"),"Healthy no-op completion was changed");}
                    check(count()==before+1,"Completion recovery replayed a Steam write");observations.push_back({{"fault",kind},{"rejected",reject},{"error",error},{"commitCalls",1}});
                }
                writeJsonWithLocalBackup(p/L"add-completion-observations.json",{{"observations",observations},{"scope","production Host account/write/pipe/Job with owned completion publication port; no actual Steam writes"}},false);return;
            }
            const auto output=g_dataRoot/L"state"/L"steam-accounts.json",receipt=g_dataRoot/L"account-receipt-worker.json";
            const json prior={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",json::array({{{"accountId","1001"},{"steamRoot","owned-old"}}})}};
            wchar_t saved[32768]{};const auto savedSize=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",saved,32768);
            SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();
            struct Restore {std::wstring saved;~Restore(){manualEnrichmentFixtureWorker.clear();SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",saved.empty()?nullptr:saved.c_str());}} restore{savedSize&&savedSize<32768?saved:L""};
            const json target={{"selectedGameDirectories",json::array({"owned-selected-game"})}};
            const auto action=[&](int operation){if(operation==0)commitToAllSteamAccounts(target);else if(operation==1)refreshArtworkForAllSteamAccounts(target);else deleteFromAllSteamAccounts(target);};
            const std::vector<std::string> emptyErrors={"未发现可写入的 Steam 大屏用户","未发现可更新的 Steam 大屏用户","未发现可删除的 Steam 大屏用户"};
            const std::vector<json> faults={
                {{"stdout","STEAM_ACCOUNTS_STATUS: unknown\nACCOUNTS_WRITE_CONFIRMED: false\n"},{"stderr","WARNING_STEAM_ACCOUNTS_PUBLICATION: fixture-permission-denied\n"},{"cause","fixture-permission-denied"}},
                {{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: {\n"},{"stderr","fixture-file-locked\n"},{"cause","fixture-file-locked"}},
                {{"stdout",""},{"stderr","fixture-worker-unavailable\n"},{"exitCode",1},{"cause","fixture-worker-unavailable"}}
            };
            const auto run=[&](int operation,const json& response){auto wire=response;wire["rawFile"]=prior.dump();text(receipt,wire.dump());std::string error;try{action(operation);}catch(const std::exception& e){error=e.what();}check(!error.empty(),"Batch account failure was silently accepted");return error;};
            json observations=json::array();
            for(int operation=0;operation<3;++operation){
                for(const auto& fault:faults){
                    const auto error=run(operation,fault);check(error.find(fault["cause"].get<std::string>())!=std::string::npos,"Batch action discarded account discovery cause");
                    check(error!=emptyErrors[operation]&&bytes(output)==prior.dump(),"Account error was reclassified or retained cache changed");
                    const json empty={{"schemaVersion",1},{"steamStatus","installed-no-account"},{"accounts",json::array()}};
                    const auto recovered=run(operation,{{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+empty.dump()+"\n"}});
                    check(recovered==emptyErrors[operation],"Fresh healthy-empty result kept the prior detection error");
                    observations.push_back({{"operation",operation},{"cause",fault["cause"]},{"error",error},{"healthyEmpty",recovered}});
                }
                const json absent={{"schemaVersion",1},{"steamStatus","not-installed"},{"accounts",json::array()}};
                check(run(operation,{{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+absent.dump()+"\n"}})==emptyErrors[operation],"Confirmed missing-install result became a discovery error");
            }
            for(const auto* name:{L"last-steam-add-all-accounts.json",L"last-steam-delete-all-accounts.json",L"last-steam-artwork-refresh-all-accounts.json"})check(!fs::exists(g_dataRoot/L"state"/name),"Blocked account operation wrote a success receipt");
            check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Account error consumers escaped owned boundary");
            writeJsonWithLocalBackup(p/L"account-write-error-observations.json",{{"observations",observations},{"scope","actual three batch functions and Host account reader/owned child/pipe/Job; controlled account Worker; no actual Steam operation"}},false);
        });
        test("discovercfg-cached-snapshot-required-fallbacks",[&](const auto& p){
            wchar_t variant[64]{};const auto variantSize=GetEnvironmentVariableW(L"YMCC_HOST_SNAPSHOT_ACCOUNT_VARIANT",variant,64);
            const auto diagnostics=g_dataRoot/L"state"/L"ui-errors.jsonl";
            const auto verifyRefreshDiagnostic=[&](size_t expectedCount,const char* stage,const char* reason){
                const auto raw=bytes(diagnostics);std::istringstream lines(raw);std::string line;json record;size_t count=0;
                while(std::getline(lines,line)){record=json::parse(line);++count;}
                check(count==expectedCount,"Steam plan refresh failure did not leave exactly one exported diagnostic");
                check(record["type"]=="steam-plan-refresh-failed"&&record["stage"]==stage&&record["reason"]==reason,
                    "Steam plan refresh diagnostic lost the failing stage or classification");
                check(record.size()==5&&record["recordedAt"].is_number()&&record["processId"].is_number()&&
                    raw.size()<=expectedCount*256&&raw.find("this Worker publication")==std::string::npos&&
                    raw.find("fixture-account-")==std::string::npos,
                    "Steam plan diagnostics copied worker output or account evidence");
                return raw;
            };

            if(variantSize&&variantSize<64&&std::wstring(variant)==L"scan-membership"){
                const auto savedData=g_dataRoot,savedLab=g_labRoot,savedWorker=g_worker;const auto savedTarget=steamTargetSnapshot();const auto savedAllowed=manualEnrichmentFixtureWorker;wchar_t env[32768]{};const auto n=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env,32768);
                struct Restore{fs::path data,lab,worker;json target;std::wstring allowed,env;~Restore(){joinHostBackgroundTasks();pipelineFixtureNotice={};manualReplyCapture=false;setSteamTarget(target);g_dataRoot=data;g_labRoot=lab;g_worker=worker;manualEnrichmentFixtureWorker=allowed;SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env.empty()?nullptr:env.c_str());}}restore{savedData,savedLab,savedWorker,savedTarget,savedAllowed,n&&n<32768?env:L""};
                json rows=json::array();bool safe=true;unsigned index=0;
                for(const auto* label:{"scan-failed-valid-cache","scan-failed-plan-rejected","scan-healthy","cancel-before-scan","first-run-scan-failed"}){
                    const std::string kind=label;const auto f=snapshotCacheFixture(p/std::to_wstring(index++),1);RefreshNoticeWindow window;SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();
                    auto config=readJson(g_dataRoot/L"config"/L"library-config.json",json::object());config["automaticScrapingEnabled"]=kind!="scan-healthy";text(g_dataRoot/L"config"/L"library-config.json",config.dump());auto retained=f.cache;retained["items"][0]["status"]="ready-to-add";retained["summary"]["alreadyInSteam"]=0;text(workspacePlanPath(),retained.dump());auto joined=f.cache;joined["items"][0]["status"]="already-in-steam";joined["customNote"]="fresh original pipeline plan";
                    const auto scan=g_dataRoot/L"state"/L"library-scan.json";const auto originalScan=bytes(scan);json response={{"pipelineScanPort",true},{"scanExit",kind=="scan-healthy"?0:23},{"planResult",kind=="scan-failed-plan-rejected"?retained:joined},{"planExit",kind=="scan-failed-plan-rejected"?24:0}};text(g_dataRoot/L"account-receipt-worker.json",response.dump());
                    {std::lock_guard<std::mutex> lock(manualReplyEventMutex);manualReplyEvents=json::array();}manualReplyCapture=true;bool cancellationInjected=false;
                    pipelineFixtureNotice=[&](const json& message){if(kind=="cancel-before-scan"&&!cancellationInjected&&jsonStringOr(message,"event")=="library-open-started"){cancellationInjected=true;std::lock_guard<std::mutex> lock(g_backgroundJobsMutex);for(const auto& job:g_libraryPipelineJobs)job->cancelRequested=true;}};
                    startLibraryPipeline(kind=="first-run-scan-failed");joinHostBackgroundTasks();pipelineFixtureNotice={};manualReplyCapture=false;json messages;{std::lock_guard<std::mutex> lock(manualReplyEventMutex);messages=manualReplyEvents;}
                    unsigned scanCalls=0,planCalls=0;for(const auto& e:fs::directory_iterator(g_dataRoot)){const auto name=e.path().filename().wstring();if(name.starts_with(L"pipeline-scan-call-"))++scanCalls;if(name.starts_with(L"account-plan-call-"))++planCalls;}
                    bool scanError=false,firstError=false,scanComplete=false,cancelled=false,planSeen=false,owned=false,planError=false;for(const auto& message:messages){const auto event=jsonStringOr(message,"event");if(event=="library-open-error"&&jsonStringOr(message,"error").find("owned directory scan failed")!=std::string::npos)scanError=true;if(event=="library-first-run-error")firstError=true;if(event=="library-scan-complete")scanComplete=true;if(event=="library-open-cancelled")cancelled=true;if(event=="library-plan-refreshed"&&message.contains("snapshot")){planSeen=true;const auto snapshot=message["snapshot"];owned=steamPlanItemAlreadyOwned(snapshot["steamPlan"]["items"][0]);planError=!jsonStringOr(snapshot,"steamPlanError").empty();}}
                    const bool cancel=kind=="cancel-before-scan",healthy=kind=="scan-healthy",rejected=kind=="scan-failed-plan-rejected";const bool passed=cancel?cancellationInjected&&cancelled&&scanCalls==0&&planCalls==0&&!planSeen:scanCalls==1&&planCalls==1&&planSeen&&owned==!rejected&&planError==rejected&&scanError==!healthy&&scanComplete==healthy&&firstError==(kind=="first-run-scan-failed");const bool retainedScan=bytes(scan)==originalScan,drained=!g_libraryPipelineRunning.load()&&!g_scrapeQueueRunning.load();
                    rows.push_back({{"variant",kind},{"passed",passed&&retainedScan&&drained},{"scanCalls",scanCalls},{"planCalls",planCalls},{"scanErrorPreserved",scanError},{"scanComplete",scanComplete},{"firstRunError",firstError},{"planSeen",planSeen},{"owned",owned},{"planError",planError},{"cancelled",cancelled},{"originalScanPreserved",retainedScan},{"drained",drained},{"messages",messages}});safe=safe&&passed&&retainedScan&&drained;
                }
                writeJsonWithLocalBackup(p/L"scan-membership-observed.json",{{"rows",rows}},false);check(rows.size()==5&&safe,"Directory scan failure blocked independent Steam membership or crossed cancellation/scrape gates");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Pipeline fixture escaped original owned ports");return;
            }
            if(variantSize&&variantSize<64&&(std::wstring(variant)==L"effective-account"||std::wstring(variant)==L"effective-account-boundaries")){
                const bool boundaries=std::wstring(variant)==L"effective-account-boundaries";
                const auto savedTarget=steamTargetSnapshot();const auto savedData=g_dataRoot,savedLab=g_labRoot,savedWorker=g_worker;const auto allowed=manualEnrichmentFixtureWorker;
                wchar_t env[32768]{};const auto n=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env,32768);
                struct Restore{json target;fs::path data,lab,worker;std::wstring allowed,env;~Restore(){activeAccountFixture=false;activeAccountRoot.clear();setSteamTarget(target);g_dataRoot=data;g_labRoot=lab;g_worker=worker;manualEnrichmentFixtureWorker=allowed;SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env.empty()?nullptr:env.c_str());}}restore{savedTarget,savedData,savedLab,savedWorker,allowed,n&&n<32768?env:L""};
                g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();activeAccountFixture=true;json observations=json::array();bool safe=true;unsigned index=0;
                const auto account=[&](const std::string& id,const fs::path& root){return json{{"accountId",id},{"steamRoot",toUtf8(root.wstring())},{"shortcutsVdf",toUtf8((root/L"userdata"/toWide(id)/L"config"/L"shortcuts.vdf").wstring())},{"readError",nullptr}};};
                if(!boundaries){
                for(const auto* label:{"current","other-account","other-installation","failed-refresh","explicit-historical","legacy-current"}){
                    const std::string mode=label;const auto f=snapshotCacheFixture(p/std::to_wstring(index++),1);SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());
                    const auto steam=f.root/L"Steam";activeAccountUser=1001;activeAccountRoot=steam.wstring();const auto current=account("1001",steam),other=account("1002",mode=="other-installation"?f.root/L"OtherSteam":steam);
                    auto result=f.cache;const bool reject=mode=="other-account"||mode=="other-installation"||mode=="failed-refresh";
                    if(reject||mode=="explicit-historical"){result["accountId"]=other["accountId"];result["shortcutsVdf"]=other["shortcutsVdf"];}
                    if(mode=="other-installation"){result["accountId"]="1001";result["shortcutsVdf"]=account("1001",f.root/L"OtherSteam")["shortcutsVdf"];}
                    if(mode=="legacy-current")result.erase("schemaVersion");result["items"][0]["status"]="already-in-steam";
                    setSteamTarget(mode=="explicit-historical"?json{{"accountId","1002"},{"steamRoot",toUtf8(steam.wstring())}}:json{{"selectionMode","all-steam-accounts"}});
                    const json accounts={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",json::array({current,account("1002",steam)})}};
                    if(mode=="failed-refresh")text(workspacePlanPath(),result.dump());
                    text(g_dataRoot/L"account-receipt-worker.json",json{{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+accounts.dump()+"\n"},{"planResult",result},{"planExit",mode=="failed-refresh"?23:0}}.dump());
                    const auto fresh=workspaceSnapshot(true),local=workspaceSnapshot(false);
                    const bool freshSafe=reject?!jsonStringOr(fresh,"steamPlanError").empty()&&jsonStringOr(fresh["steamPlan"],"accountId")!=jsonStringOr(result,"accountId"):fresh["steamPlanError"].is_null()&&jsonStringOr(fresh["steamPlan"],"accountId")==jsonStringOr(result,"accountId");
                    const bool localSafe=reject?jsonStringOr(local["steamPlan"],"accountId")!=jsonStringOr(result,"accountId"):jsonStringOr(local["steamPlan"],"accountId")==jsonStringOr(result,"accountId");
                    const bool passed=freshSafe&&localSafe;observations.push_back({{"kind","refresh"},{"mode",mode},{"passed",passed},{"freshSafe",freshSafe},{"localSafe",localSafe},{"fresh",fresh["steamPlan"]},{"local",local["steamPlan"]}});safe=safe&&passed;
                }
                for(const auto* label:{"current-first","current-last","signed-out","unknown-current","foreign-active-installation"}){
                    const std::string mode=label;const auto f=snapshotCacheFixture(p/std::to_wstring(index++),1);SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());setSteamTarget({{"selectionMode","all-steam-accounts"}});
                    const auto steam=f.root/L"Steam";activeAccountUser=mode=="current-last"?1002:mode=="signed-out"?0:mode=="unknown-current"?999:1001;activeAccountRoot=mode=="foreign-active-installation"?(f.root/L"OtherSteam").wstring():steam.wstring();
                    json accountRows=json::array(),plans=json::object(),completed=json::object();for(const auto* id:{"1001","1002"}){const auto row=account(id,steam);accountRows.push_back(row);auto value=f.cache;value["accountId"]=id;value["shortcutsVdf"]=row["shortcutsVdf"];value["customNote"]=std::string("account-")+id;value["items"][0]["status"]=std::string(id)=="1001"?"already-in-steam":"ready-to-add";plans[id]=value;value["mode"]="steam-library-add-commit";value["committed"]=false;value["reason"]="nothing-ready-to-add";completed[id]=value;}
                    const json accounts={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",accountRows}};
                    text(g_dataRoot/L"account-receipt-worker.json",json{{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+accounts.dump()+"\n"},{"plansByAccount",plans},{"completionsByAccount",completed}}.dump());
                    const auto result=commitToAllSteamAccounts({{"selectedGameDirectories",json::array({f.cache["items"][0]["gameDirectory"]})}}),local=workspaceSnapshot(false);const auto expected=mode=="current-last"?"1002":"1001";
                    const bool retained=jsonArrayOr(result,"accountResults").size()==2&&jsonStringOr(result["accountResults"][0],"accountId")=="1001"&&jsonStringOr(result["accountResults"][1],"accountId")=="1002";
                    const bool passed=retained&&jsonStringOr(result,"accountId")==expected&&jsonStringOr(local["steamPlan"],"accountId")==expected&&jsonStringOr(local["steamPlan"],"customNote")==std::string("account-")+expected;
                    observations.push_back({{"kind","batch"},{"mode",mode},{"passed",passed},{"allResultsRetained",retained},{"expected",expected},{"actual",jsonStringOr(result,"accountId")},{"local",jsonStringOr(local["steamPlan"],"accountId")}});safe=safe&&passed;
                }
                }
                if(boundaries)for(const auto* label:{"same-root-stale-cache","explicit-account","selection-mode","other-data-root"}){
                    const std::string mode=label;const auto f=snapshotCacheFixture(p/std::to_wstring(index++),1);SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());setSteamTarget({{"selectionMode","all-steam-accounts"}});
                    const auto steam=f.root/L"Steam";activeAccountUser=1001;activeAccountRoot=steam.wstring();const auto acct=account("1001",steam);
                    const json accounts={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",json::array({acct})}};
                    text(g_dataRoot/L"account-receipt-worker.json",json{{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+accounts.dump()+"\n"},{"planResult",f.cache}}.dump());
                    const auto seeded=workspaceSnapshot(true);check(seeded["steamPlanError"].is_null()&&jsonStringOr(seeded["steamPlan"],"accountId")=="1001","Boundary seed did not observe current account");
                    auto cached=f.cache;auto root=steam;
                    if(mode=="other-data-root"){const auto next=snapshotCacheFixture(p/std::to_wstring(index++),1);cached=next.cache;root=next.root/L"Steam";setSteamTarget({{"selectionMode","all-steam-accounts"}});}
                    if(mode=="explicit-account")setSteamTarget({{"accountId","1002"},{"steamRoot",toUtf8(root.wstring())}});
                    if(mode=="selection-mode")setSteamTarget({{"selectionMode","all-ready"}});
                    cached["accountId"]="1002";cached["shortcutsVdf"]=account("1002",root)["shortcutsVdf"];text(workspacePlanPath(),cached.dump());
                    const auto before=bytes(workspacePlanPath());const auto processes=admissionOwnedProcesses.load(),registry=activeAccountReads;
                    const auto local=workspaceSnapshot(false);const auto processDelta=admissionOwnedProcesses.load()-processes,registryDelta=activeAccountReads-registry;
                    const bool correct=mode=="same-root-stale-cache"?jsonStringOr(local["steamPlan"],"accountId").empty():jsonStringOr(local["steamPlan"],"accountId")=="1002";
                    const bool unchanged=bytes(workspacePlanPath())==before;const bool passed=correct&&unchanged&&processDelta==0&&registryDelta==0;
                    observations.push_back({{"kind","boundary"},{"mode",mode},{"passed",passed},{"correctScope",correct},{"filesUntouched",unchanged},{"ownedProcessCalls",processDelta},{"registryReads",registryDelta}});safe=safe&&passed;
                }
                writeJsonWithLocalBackup(p/L"effective-account-observed.json",{{"rows",observations},{"scope","Actual Host snapshot/batch/readback and original owned subprocess/pipe/Job, fake account Worker ports; no real Steam or registry writes"}},false);
                check(observations.size()==(boundaries?4:11)&&safe,"Effective account was lost between discovery, plan readback and local/batch display");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Effective target fixture escaped owned ports");return;
            }
            if(variantSize&&variantSize<64&&std::wstring(variant)==L"partial-accounts"){
                const auto f=snapshotCacheFixture(p,1);const auto savedTarget=steamTargetSnapshot();const auto savedWorker=g_worker;const auto allowed=manualEnrichmentFixtureWorker;
                wchar_t env[32768]{};const auto n=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env,32768);
                struct Restore{json target;fs::path worker;std::wstring allowed,env;~Restore(){activeAccountFixture=false;activeAccountRoot.clear();setSteamTarget(target);g_worker=worker;manualEnrichmentFixtureWorker=allowed;SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env.empty()?nullptr:env.c_str());}}restore{savedTarget,savedWorker,allowed,n&&n<32768?env:L""};
                SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();
                const auto steam=p/L"Steam";auto healthy=savedTarget;healthy["accountId"]="1001";healthy["steamRoot"]=toUtf8(steam.wstring());healthy["shortcutsVdf"]=toUtf8((steam/L"userdata"/L"1001"/L"config"/L"shortcuts.vdf").wstring());healthy["readError"]=nullptr;
                auto bad=healthy;bad["accountId"]="1002";bad["shortcutsVdf"]=toUtf8((steam/L"userdata"/L"1002"/L"config"/L"shortcuts.vdf").wstring());bad["readError"]="owned VDF unavailable";
                const json report={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",json::array({healthy})},{"partialAccounts",true},{"unavailableAccounts",json::array({bad})}};
                auto completed=f.cache;completed["mode"]="steam-library-add-commit";completed["committed"]=true;
                const auto publish=[&](const json& value){text(g_dataRoot/L"account-receipt-worker.json",json{{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+value.dump()+"\n"},{"planResult",f.cache},{"commitResult",completed}}.dump());};
                activeAccountFixture=true;activeAccountUser=1001;activeAccountRoot=steam.wstring();json rows=json::array();bool safe=true;
                const auto observe=[&](const std::string& mode,bool expected,const json& value){publish(value);const auto got=readSteamAccounts();const bool ok=(jsonStringOr(got,"steamStatus")=="error")==!expected;rows.push_back({{"variant",mode},{"passed",ok},{"result",got}});safe=safe&&ok;};
                observe("healthy-current",true,report);activeAccountUser=1002;observe("unreadable-current",false,report);activeAccountUser=1001;
                for(const auto* label:{"wrong-array","empty-id","empty-root","empty-path","empty-error","duplicate","false-partial","nonnumeric-id","foreign-path"}){auto value=report;const std::string mode=label;auto& row=value["unavailableAccounts"][0];if(mode=="wrong-array")value["unavailableAccounts"]=json::object();if(mode=="empty-id")row["accountId"]="";if(mode=="empty-root")row["steamRoot"]="";if(mode=="empty-path")row["shortcutsVdf"]="";if(mode=="empty-error")row["readError"]="";if(mode=="duplicate")row=healthy;if(mode=="false-partial")value["partialAccounts"]=false;if(mode=="nonnumeric-id")row["accountId"]="not-account";if(mode=="foreign-path")row["shortcutsVdf"]=healthy["shortcutsVdf"];observe(mode,false,value);}
                auto foreign=report;foreign["unavailableAccounts"][0]["steamRoot"]=toUtf8((p/L"OtherSteam").wstring());foreign["unavailableAccounts"][0]["shortcutsVdf"]=toUtf8((p/L"OtherSteam"/L"userdata"/L"1002"/L"config"/L"shortcuts.vdf").wstring());activeAccountUser=1002;observe("same-id-other-installation",true,foreign);activeAccountUser=1001;
                json target={{"selectedGameDirectories",json::array({f.cache["items"][0]["gameDirectory"]})}};
                for(const auto* operation:{"add","refresh","delete"}){publish(report);json value;std::string error;try{value=std::string(operation)=="add"?commitToAllSteamAccounts(target):std::string(operation)=="refresh"?refreshArtworkForAllSteamAccounts(target):deleteFromAllSteamAccounts(target);}catch(const std::exception& e){error=e.what();}const bool ok=error.empty()&&jsonBoolOr(value,"partialSteamAccounts",false)&&!jsonStringOr(value,"accountReadWarning").empty()&&jsonArrayOr(value,"accountResults").size()==1;rows.push_back({{"variant",operation},{"passed",ok},{"result",value},{"error",error}});safe=safe&&ok;}
                writeJsonWithLocalBackup(p/L"partial-accounts-observed.json",{{"rows",rows}},false);check(rows.size()==15&&safe,"Partial accounts lost isolation, active-account gate or truthful result");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Partial accounts escaped owned ports");return;
            }
            if(variantSize&&variantSize<64&&std::wstring(variant)==L"active-account"){
                const auto f=snapshotCacheFixture(p,3);const auto savedTarget=steamTargetSnapshot();const auto savedWorker=g_worker,savedLab=g_labRoot;
                const auto allowed=manualEnrichmentFixtureWorker;wchar_t env[32768]{};const auto envSize=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env,32768);
                struct Restore {json target;fs::path worker,lab;std::wstring allowed,env;~Restore(){activeAccountFixture=false;activeAccountWrongType=false;activeAccountRoot.clear();setSteamTarget(target);g_worker=worker;g_labRoot=lab;manualEnrichmentFixtureWorker=allowed;SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",env.empty()?nullptr:env.c_str());}} restore{savedTarget,savedWorker,savedLab,allowed,envSize&&envSize<32768?env:L""};
                SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();
                const auto steam=p/L"Steam";activeAccountFixture=true;json rows=json::array();bool safe=true;
                const auto account=[&](const std::string& id,const fs::path& root){return json{{"accountId",id},{"steamRoot",toUtf8(root.wstring())},{"shortcutsVdf",toUtf8((root/L"userdata"/toWide(id)/L"config"/L"shortcuts.vdf").wstring())},{"readError",nullptr}};};
                const auto history=account("22202",steam),current=account("89250578",steam);
                const auto plan=[&](const json& acct,bool joined){auto value=f.cache;value["accountId"]=acct["accountId"];value["shortcutsVdf"]=acct["shortcutsVdf"];value["summary"]["alreadyInSteam"]=joined?3:0;for(auto& row:value["items"])row["status"]=joined?"already-in-steam":"ready-to-add";return value;};
                for(const auto* label:{"implicit-current","all-accounts-current","explicit-historical","signed-out","unknown-current","other-installation","wrong-registry-type","missing-installation","current-first"}){
                    const std::string mode=label;activeAccountUser=mode=="signed-out"?0:mode=="unknown-current"?999:89250578;activeAccountWrongType=mode=="wrong-registry-type";activeAccountRoot=mode=="missing-installation"?L"":steam.wstring();
                    auto active=mode=="other-installation"?account("89250578",p/L"OtherSteam"):current;
                    json report={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",mode=="current-first"?json::array({active,history}):json::array({history,active})}};
                    const bool expectedCurrent=mode=="implicit-current"||mode=="all-accounts-current"||mode=="current-first";
                    const auto expected=expectedCurrent?"89250578":"22202";
                    setSteamTarget(mode=="implicit-current"?json::object():mode=="explicit-historical"?json{{"accountId","22202"},{"steamRoot",toUtf8(steam.wstring())}}:json{{"selectionMode","all-steam-accounts"}});
                    const auto old=plan(history,false),joined=plan(active,true);text(workspacePlanPath(),old.dump());
                    text(g_dataRoot/L"account-receipt-worker.json",json{{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+report.dump()+"\n"},{"planResult",old},{"plansByAccount",{{"22202",old},{"89250578",joined}}}}.dump());
                    const auto result=workspaceSnapshot(true);const auto& items=result["steamPlan"]["items"];const bool owned=!items.empty()&&steamPlanItemAlreadyOwned(items[0]);
                    const bool passed=result["steamPlanError"].is_null()&&jsonStringOr(result["steamPlan"],"accountId")==expected&&owned==expectedCurrent;
                    rows.push_back({{"mode",mode},{"expectedAccount",expected},{"actualAccount",jsonStringOr(result["steamPlan"],"accountId")},{"owned",owned},{"passed",passed}});safe=safe&&passed;
                }
                writeJsonWithLocalBackup(p/L"active-account-observed.json",{{"rows",rows},{"scope","actual Host workspaceSnapshot, registry-read port, original account/plan subprocess/pipe/Job; fake immutable Worker plans, no real Steam or registry writes"}},false);
                check(rows.size()==9&&safe,"Default plan used historical or foreign Steam account");return;
            }
            if(variantSize&&variantSize<64&&std::wstring(variant)==L"shipping-plan-worker"){
                wchar_t workerValue[32768]{};const auto workerSize=GetEnvironmentVariableW(L"YMCC_SHIPPING_PLAN_WORKER",workerValue,32768);
                check(workerSize&&workerSize<32768,"Shipping plan Worker must be explicit");
                const auto shipping=fs::absolute(workerValue);
                const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
                check(shipping.filename()==L"SteamArtworkLab.exe"&&normalizedPathKey(shipping).starts_with(normalizedPathKey(evidence)+L"\\")&&fs::is_regular_file(shipping),"Shipping Worker escaped owned evidence");
                const auto previousWorker=g_worker;const auto previousTarget=steamTargetSnapshot();const auto previousAllowed=manualEnrichmentFixtureWorker;
                std::vector<std::pair<std::wstring,std::wstring>> saved;
                for(const auto* name:{L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",L"YEMAN_GAME_WHITELIST_PATH",L"YEMAN_PLAYER_BLACKLIST_PATH"}){wchar_t value[32768]{};const auto n=GetEnvironmentVariableW(name,value,32768);check(n<32768,"Owned environment too long");saved.push_back({name,n?value:L""});}
                struct Restore {fs::path worker;json target;std::wstring allowed;std::vector<std::pair<std::wstring,std::wstring>> saved;~Restore(){g_worker=worker;setSteamTarget(target);manualEnrichmentFixtureWorker=allowed;for(const auto& [name,value]:saved)SetEnvironmentVariableW(name.c_str(),value.empty()?nullptr:value.c_str());}} restore{previousWorker,previousTarget,previousAllowed,saved};
                const auto f=snapshotCacheFixture(p,0);const auto steam=p/L"Steam",vdf=steam/L"userdata"/L"1001"/L"config"/L"shortcuts.vdf";
                text(steam/L"steam.exe","owned inert marker, never executed");const char emptyVdf[]={0,'s','h','o','r','t','c','u','t','s',0,8,8};text(vdf,std::string(emptyVdf,sizeof emptyVdf));const auto beforeVdf=bytes(vdf);
                const auto dataIdentity=bytes(g_dataRoot/L"data-root.json");const auto scanPath=g_dataRoot/L"state"/L"library-scan.json";
                const auto scanBytes=bytes(scanPath);const auto white=p/L"owned-whitelist.txt",black=p/L"owned-blacklist.txt";
                text(white,"manual-whitelist-stays\n");text(black,"manual-blacklist-stays\n");const auto whiteBytes=bytes(white),blackBytes=bytes(black);
                SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",g_dataRoot.c_str());SetEnvironmentVariableW(L"YEMAN_GAME_WHITELIST_PATH",white.c_str());SetEnvironmentVariableW(L"YEMAN_PLAYER_BLACKLIST_PATH",black.c_str());
                g_worker=shipping;manualEnrichmentFixtureWorker=shipping.wstring();initializeWorkerProcessJob();json observations=json::array();
                const auto verify=[&](const char* name,bool expected){const auto snapshot=workspaceSnapshot(true);const bool confirmed=snapshot["steamPlanError"].is_null();
                    observations.push_back({{"name",name},{"confirmed",confirmed},{"error",snapshot["steamPlanError"]},{"mode",jsonStringOr(snapshot["steamPlan"],"mode")}});
                    check(confirmed==expected,"Shipping Worker plan result/receipt did not match expected boundary");
                    check(snapshot["library"]["games"].empty()&&snapshot["steamPlan"]["items"].empty(),"Shipping empty plan changed its declared inventory");
                    check(bytes(vdf)==beforeVdf&&bytes(white)==whiteBytes&&bytes(black)==blackBytes,"Read-only shipping plan changed fake Steam or manual rules");};
                verify("healthy-shipping-process",true);
                const auto planFile=workspacePlanPath();text(planFile,R"({"schemaVersion":99,"keep":"future original"})");const auto futurePlan=bytes(planFile);
                verify("future-output-refused",false);check(bytes(planFile)==futurePlan,"Shipping refusal replaced protected plan");
                text(planFile,f.cache.dump());verify("explicit-output-repair",true);
                const auto healthyPlan=bytes(planFile);text(scanPath,R"({"schemaVersion":99,"games":[],"keep":"future scan"})");const auto futureScan=bytes(scanPath);
                verify("future-scan-refused",false);check(bytes(planFile)==healthyPlan&&bytes(scanPath)==futureScan,"Shipping inactive scan changed retained plan or source");
                text(scanPath,scanBytes);verify("explicit-scan-repair",true);
                const auto accounts=readSteamAccounts(json::array({toUtf8(steam.wstring())}));
                check(jsonStringOr(accounts,"steamStatus")=="ready"&&accounts["accounts"].size()==1&&accounts["accounts"][0]["accountId"]=="1001","Shipping account reply did not remain inside explicit fake Steam target");
                check(bytes(g_dataRoot/L"data-root.json")==dataIdentity&&g_activeProcess==nullptr,"Shipping process changed data identity or retained process slot");
                check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Host shipping fixture attempted unowned process/network/shell");
                writeJsonWithLocalBackup(p/L"shipping-plan-observations.json",{{"observations",observations},{"actualWorker",toUtf8(shipping.wstring())},{"accountCount",accounts["accounts"].size()},{"fakeSteamAndManualRulesUnchanged",true},{"scope","actual production Worker process and original Host snapshot/pipe/Job, explicit isolated empty library/fake Steam; no installed UI or physical account test"}},false);return;
            }
            if(variantSize&&variantSize<64&&(std::wstring(variant)==L"plan-receipt"||std::wstring(variant)==L"plan-worker-error-text")){
                const auto f=snapshotCacheFixture(p,3);const auto previousTarget=steamTargetSnapshot();const auto previousWorker=g_worker;
                wchar_t previous[32768]{};const auto previousSize=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",previous,32768);
                const auto previousAllowed=manualEnrichmentFixtureWorker;
                struct Restore {json target;fs::path worker;std::wstring allowed,environment;~Restore(){setSteamTarget(target);g_worker=worker;manualEnrichmentFixtureWorker=allowed;SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",environment.empty()?nullptr:environment.c_str());}} restore{previousTarget,previousWorker,previousAllowed,previousSize&&previousSize<32768?previous:L""};
                SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();
                const auto control=g_dataRoot/L"account-receipt-worker.json";
                const auto receiptFor=[](const json& value){return "STEAM_PLAN_RECEIPT: sha256-v1:"+custom_steam_library::plan_receipt::digest(value)+"\n";};
                auto original=f.cache;original["customNote"]="this Worker publication";auto later=original;later["customNote"]="later shared writer";
                if(std::wstring(variant)==L"plan-worker-error-text"){
                    const auto retained=bytes(workspacePlanPath());
                    const std::string receiptOnly="STEAM_ADD_MODE: plan-only\nSTEAM_ACCOUNT: 1001\nSTEAM_SHORTCUTS: G:/FixtureSteam/userdata/1001/config/shortcuts.vdf\nSTEAM_ADD_READY: 0\nSTEAM_RELOAD_REQUIRED: false\nOUTPUT: G:/FixtureData/state/steam-add-plan.json\n"+receiptFor(original);
                    const std::string detail="ERROR_STEAM_ADD_PLAN_UNCONFIRMED: Fixture plan is temporarily unavailable";
                    std::string longText;for(int i=0;i<1000;++i)longText+="\xe6\xb5\x8b";
                    const auto diagnosticStart=bytes(diagnostics);
                    json rows=json::array();bool all=true;size_t errorCount=0;
                    for(const std::string mode:{"mixed","metadata-only","long-utf8","cancelled"}){
                        const bool cancelled=mode=="cancelled";
                        const auto before=bytes(diagnostics);
                        const auto output=receiptOnly+(mode=="mixed"?detail+"\n":mode=="long-utf8"?longText:std::string{});
                        text(control,json{{"planResult",original},{"skipPlanWrite",true},{"planReceiptText",output},{"planExit",cancelled?ERROR_CANCELLED:static_cast<DWORD>(2)}}.dump());
                        const auto snapshot=workspaceSnapshot(true);const auto error=jsonStringOr(snapshot,"steamPlanError");
                        const bool exact=mode=="mixed"?error==detail:mode=="metadata-only"?error=="Steam 计划刷新失败，请重新检测。":mode=="long-utf8"?error==longText.substr(0,1599):snapshot["steamPlanError"].is_string()&&error.empty();
                        const auto recorded=cancelled?before:verifyRefreshDiagnostic(++errorCount,"plan-worker",mode=="mixed"?"plan-publication-unconfirmed":"worker-failed");
                        check(bytes(diagnostics)==recorded&&recorded.find("FixtureSteam")==std::string::npos,"Plan error lost safe diagnostics or logged raw protocol");
                        check(bytes(workspacePlanPath())==retained&&snapshot["steamPlan"]["items"].size()==3,"Plan error discarded cache or rewrote a plan");
                        rows.push_back({{"mode",mode},{"passed",exact},{"errorBytes",error.size()},{"machineFields",error.find("STEAM_ACCOUNT:")!=std::string::npos},{"diagnosticUnchanged",recorded==diagnosticStart}});
                        all=all&&exact;
                    }
                    text(control,json{{"planResult",original}}.dump());const auto recovered=workspaceSnapshot(true);
                    check(recovered["steamPlanError"].is_null()&&jsonStringOr(recovered["steamPlan"],"customNote")=="this Worker publication","Plan error prevented healthy retry");
                    check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Plan error fixture escaped owned ports");
                    writeJsonWithLocalBackup(p/L"plan-worker-error-text-observations.json",rows,false);
                    check(all,"Failed plan leaked protocol fields or discarded useful detail");return;
                }
                json observations=json::array();
                size_t errorCount=0;
                check(!fs::exists(diagnostics),"Healthy initial state wrote a plan error");
                const std::string workerDetails="WORKER_PLAN_FAILED: fixture-only detail";
                for(const auto* fault:{"later-same-account","missing","duplicate","malformed","wrong-hash","nonzero","nonzero-empty","nonzero-whitespace","cancelled","cancelled-empty"}){
                    json response={{"planResult",original}};
                    const std::string kind=fault;
                    const bool blankFailure=kind=="nonzero-empty"||kind=="nonzero-whitespace";
                    const bool nonzero=kind=="nonzero"||blankFailure;
                    const bool cancelled=kind=="cancelled"||kind=="cancelled-empty";
                    if(kind=="later-same-account")response["laterPlan"]=later;
                    if(kind=="missing")response["planReceiptText"]="";
                    if(kind=="duplicate")response["planReceiptText"]=receiptFor(original)+receiptFor(original);
                    if(kind=="malformed")response["planReceiptText"]="STEAM_PLAN_RECEIPT: sha256-v1:bad\n";
                    if(kind=="wrong-hash")response["planReceiptText"]=receiptFor(later);
                    if(kind=="nonzero")response["planReceiptText"]=workerDetails;
                    if(nonzero)response["planExit"]=23;
                    if(cancelled)response["planExit"]=ERROR_CANCELLED;
                    const auto beforePlan=bytes(workspacePlanPath());
                    if(blankFailure||kind=="cancelled-empty"){
                        response["skipPlanWrite"]=true;
                        response["planReceiptText"]=kind=="nonzero-whitespace"?" \t\r\n":"";
                    }
                    const auto beforeAttempt=bytes(diagnostics);
                    text(control,response.dump());const auto snapshot=workspaceSnapshot(true);
                    writeJsonWithLocalBackup(p/L"plan-receipt-last-observed.json",{{"fault",kind},{"steamPlanError",snapshot["steamPlanError"]},{"displayNote",jsonStringOr(snapshot["steamPlan"],"customNote")}},false);
                    const auto error=jsonStringOr(snapshot,"steamPlanError");
                    if(blankFailure)check(!trimHost(error).empty(),"Blank Worker output must leave a meaningful plan error");
                    else if(cancelled)check(snapshot["steamPlanError"].is_string()&&error.empty(),"Cancellation exposed a Worker receipt or was promoted to a plan failure");
                    else check(!error.empty(),"Later shared plan was accepted as this Worker publication");
                    if(kind=="nonzero")check(error==workerDetails,"Useful Worker failure output was replaced");
                    if(blankFailure||kind=="cancelled-empty")check(bytes(workspacePlanPath())==beforePlan&&jsonStringOr(snapshot["steamPlan"],"customNote")=="this Worker publication","Silent Worker exit did not retain the previous plan");
                    check(snapshot["library"]["games"].size()==3&&snapshot["steamPlan"]["items"].size()==3,"Plan receipt failure discarded current cards");
                    const auto recorded=cancelled?beforeAttempt:verifyRefreshDiagnostic(++errorCount,
                        nonzero?"plan-worker":"plan-receipt",nonzero?"worker-failed":"receipt-unconfirmed");
                    check(recorded.find(workerDetails)==std::string::npos,"Raw Worker output leaked into exported diagnostics");
                    check(bytes(diagnostics)==recorded,"Cancelled refresh was misreported as a plan error");
                    const auto retained=bytes(workspacePlanPath());text(control,json{{"planResult",original}}.dump());const auto recovered=workspaceSnapshot(true);
                    check(recovered["steamPlanError"].is_null()&&jsonStringOr(recovered["steamPlan"],"customNote")=="this Worker publication","Current exact plan receipt did not recover");
                    check(bytes(diagnostics)==recorded,"Successful plan refresh wrote an error");
                    observations.push_back({{"fault",kind},{"failureRetainedCards",3},{"explicitRecovery",true},{"retainedFileBeforeRecoveryBytes",retained.size()}});
                }
                auto large=original;large["largeUnknownField"]=std::string(5u<<20,'p');text(control,json{{"planResult",large}}.dump());const auto snapshot=workspaceSnapshot(true);
                check(snapshot["steamPlanError"].is_null()&&jsonStringOr(snapshot["steamPlan"],"largeUnknownField").size()==(5u<<20),"Constant-size receipt rejected plan beyond original pipe cap");
                check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Plan receipt consumer escaped owned ports");
                writeJsonWithLocalBackup(p/L"plan-receipt-observations.json",{{"observations",observations},{"largePlanBytes",large.dump().size()},{"receiptBytes",receiptFor(large).size()},{"scope","actual production Host snapshot/owned child pipe/Job; controlled plan writer, no Steam or network"}},false);return;
            }
            if(variantSize&&variantSize<64&&std::wstring(variant)==L"worker-start-failure"){
                const auto f=snapshotCacheFixture(p,3);const auto target=steamTargetSnapshot();const auto previousWorker=g_worker;
                struct Restore {fs::path worker;json target;~Restore(){g_worker=worker;setSteamTarget(target);}} restore{previousWorker,target};
                g_worker=p/L"missing-owned-worker.exe";
                const auto original=bytes(workspacePlanPath()),accountBytes=bytes(g_dataRoot/L"state"/L"steam-accounts.json");
                auto mismatched=f.cache;mismatched["accountId"]="1002";
                json observations=json::array();size_t errors=0;std::string recorded;
                for(const auto* label:{"account-missing","plan-missing","plan-no-cache","cancelled-missing"}){
                    const std::string mode=label;const bool account=mode=="account-missing",cancelled=mode=="cancelled-missing",cached=mode!="plan-no-cache";
                    setSteamTarget(account?json::object():target);text(workspacePlanPath(),cached?original:mismatched.dump());
                    const auto beforePlan=bytes(workspacePlanPath());
                    BackgroundJob job;
                    if(cancelled){job=std::make_shared<BackgroundJobControl>();job->cancelRequested=true;}
                    json snapshot;std::string thrown;
                    try{snapshot=workspaceSnapshot(true,json::object(),job);}catch(const std::exception& e){thrown=e.what();}
                    writeJsonWithLocalBackup(p/L"worker-start-last-observed.json",{{"label",label},{"thrown",thrown}},false);
                    check(thrown.empty(),"Worker start failure escaped instead of returning a snapshot");
                    checkCachedSnapshot(snapshot,f,cached);
                    check(bytes(workspacePlanPath())==beforePlan&&bytes(g_dataRoot/L"state"/L"steam-accounts.json")==accountBytes,"Worker start failure changed durable state");
                    if(account)check(jsonStringOr(snapshot["steamAccounts"],"steamStatus")=="error"&&snapshot["steamAccounts"]["accounts"].empty(),"Unstarted account Worker appeared confirmed");
                    if(cancelled)check(bytes(diagnostics)==recorded,"Cancelled missing Worker recorded a refresh failure");
                    else{
                        check(!trimHost(jsonStringOr(snapshot,"steamPlanError")).empty(),"Unstarted Worker did not report an actionable error");
                        recorded=verifyRefreshDiagnostic(++errors,account?"account-discovery":"plan-worker","worker-exception");
                    }
                    observations.push_back({{"label",label},{"cacheRetained",cached},{"error",snapshot["steamPlanError"]}});
                }
                check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Missing Worker fallback crossed an external port");
                writeJsonWithLocalBackup(p/L"worker-start-observations.json",observations,false);return;
            }
            if(variantSize){
                check(variantSize<64&&std::wstring(variant)==L"account-error-gate","Unknown account snapshot variant");
                const auto f=snapshotCacheFixture(p,3);const auto cachedBytes=bytes(workspacePlanPath());
                wchar_t previous[32768]{};const auto previousSize=GetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",previous,32768);
                const auto previousWorker=g_worker;const auto previousAllowed=manualEnrichmentFixtureWorker;const auto previousTarget=steamTargetSnapshot();
                struct Restore {std::wstring environment,allowed;fs::path worker;json target;~Restore(){manualEnrichmentFixtureWorker=allowed;g_worker=worker;setSteamTarget(target);SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",environment.empty()?nullptr:environment.c_str());}} restore{previousSize&&previousSize<32768?previous:L"",previousAllowed,previousWorker,previousTarget};
                SetEnvironmentVariableW(L"YMCC_ACCOUNT_RECEIPT_FIXTURE_ROOT",g_dataRoot.c_str());g_worker=selfExecutable();manualEnrichmentFixtureWorker=g_worker.wstring();initializeWorkerProcessJob();
                const auto receipt=g_dataRoot/L"account-receipt-worker.json";
                const auto countPlans=[&]{size_t count=0;for(const auto& entry:fs::directory_iterator(g_dataRoot))if(entry.path().filename().wstring().starts_with(L"account-plan-call-"))++count;return count;};
                const json good={{"schemaVersion",1},{"steamStatus","ready"},{"accounts",json::array({{{"accountId","1001"},{"steamRoot",toUtf8((p/L"Steam").wstring())}}})}};
                const auto wire=[](const json& value){return "STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: "+value.dump()+"\n";};
                const std::vector<json> failures={
                    {{"stdout","STEAM_ACCOUNTS_STATUS: unknown\nACCOUNTS_WRITE_CONFIRMED: false\n"},{"stderr","fixture-account-file-locked\n"},{"cause","fixture-account-file-locked"}},
                    {{"stdout",""},{"stderr","fixture-account-worker-failed\n"},{"exitCode",1},{"cause","fixture-account-worker-failed"}},
                    {{"stdout","STEAM_ACCOUNTS_STATUS: current\nACCOUNTS_WRITE_CONFIRMED: true\nSTEAM_ACCOUNTS_JSON: {\n"},{"stderr","fixture-account-payload-invalid\n"},{"cause","fixture-account-payload-invalid"}}
                };
                json observations=json::array();size_t errorCount=0;
                for(bool allAccounts:{true,false})for(const auto& failure:failures){
                    setSteamTarget(allAccounts?json{{"selectionMode","all-steam-accounts"}}:json::object());
                    auto response=failure;response["planResult"]=f.cache;text(receipt,response.dump());const auto before=countPlans();
                    const auto result=workspaceSnapshot(true);
                    writeJsonWithLocalBackup(p/L"account-plan-last-observed.json",{{"allAccounts",allAccounts},{"cause",failure["cause"]},{"planCallsBefore",before},{"planCallsAfter",countPlans()},{"steamStatus",jsonStringOr(result["steamAccounts"],"steamStatus")},{"steamPlanError",result["steamPlanError"]}},false);
                    check(countPlans()==before&&jsonStringOr(result,"steamPlanError").find(failure["cause"].get<std::string>())!=std::string::npos,"Account discovery failure still launched a plan or cleared its error");
                    check(jsonStringOr(result["steamAccounts"],"steamStatus")=="error"&&bytes(workspacePlanPath())==cachedBytes,"Account failure changed retained plan bytes or account error");
                    const auto reason=failure.value("exitCode",0)!=0?"worker-failed":"receipt-unconfirmed";
                    const auto recorded=verifyRefreshDiagnostic(++errorCount,"account-discovery",reason);
                    checkCachedSnapshot(result,f,true);
                    text(receipt,json{{"stdout",wire(good)},{"planResult",f.cache}}.dump());const auto recovered=workspaceSnapshot(true);
                    check(countPlans()==before+1&&recovered["steamPlanError"].is_null()&&jsonStringOr(recovered["steamAccounts"],"steamStatus")=="ready","Explicit account retry failed to resume plan once");checkCachedSnapshot(recovered,f,true);
                    check(bytes(diagnostics)==recorded,"Successful account refresh wrote a plan error");
                    observations.push_back({{"allAccounts",allAccounts},{"cause",failure["cause"]},{"blockedPlanCalls",0},{"explicitRecoveryPlanCalls",1}});
                }
                for(const auto* status:{"installed-no-account","not-installed"}){
                    setSteamTarget(json{{"selectionMode","all-steam-accounts"}});const json empty={{"schemaVersion",1},{"steamStatus",status},{"accounts",json::array()}};
                    text(receipt,json{{"stdout",wire(empty)},{"planResult",f.cache}}.dump());const auto before=countPlans();const auto result=workspaceSnapshot(true);
                    check(countPlans()==before+1&&jsonStringOr(result["steamAccounts"],"steamStatus")==status&&result["steamPlanError"].is_null(),"Healthy empty account result was reclassified as a failed detection");
                    observations.push_back({{"healthyStatus",status},{"originalPlanCalls",1}});
                }
                check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Account snapshot escaped owned process/network/shell ports");
                writeJsonWithLocalBackup(p/L"account-plan-observations.json",{{"observations",observations},{"scope","production workspaceSnapshot/account reader/real owned child pipe and Job; controlled Worker publication, no Steam files or HTTP"}},false);return;
            }
            const auto f=snapshotCacheFixture(p,3);json reports=json::array();const auto original=f.cache.dump();
            const auto verify=[&](const char* name,bool cached,const json& value){checkCachedSnapshot(value,f,cached);reports.push_back({{"name",name},{"cached",cached},{"items",value["steamPlan"]["items"].size()}});};
            verify("matched-cache",true,workspaceSnapshot(false));
            auto empty=f.cache;empty["items"]=json::array();text(workspacePlanPath(),empty.dump());const auto emptyResult=workspaceSnapshot(false);check(emptyResult["steamPlan"]["items"].empty()&&emptyResult["library"]["games"].size()==3&&jsonStringOr(emptyResult,"steamPlanSource")==toUtf8(workspacePlanPath().wstring()),"Intentional empty cache was replaced by fallback");reports.push_back({{"name","authoritative-empty"},{"cached",true},{"items",0}});
            for(const auto* defect:{"malformed","future","foreign-account","foreign-installation","foreign-library"}){
                auto changed=f.cache;if(std::string(defect)=="future")changed["schemaVersion"]=99;else if(std::string(defect)=="foreign-account")changed["accountId"]="1002";else if(std::string(defect)=="foreign-installation")changed["shortcutsVdf"]=toUtf8((p/L"OtherSteam"/L"userdata"/L"1001"/L"config"/L"shortcuts.vdf").wstring());else if(std::string(defect)=="foreign-library")changed["libraryState"]=toUtf8((p/L"other-library.json").wstring());
                const auto raw=std::string(defect)=="malformed"?std::string{"{"}:changed.dump();text(workspacePlanPath(),raw);verify(defect,false,workspaceSnapshot(false));check(bytes(workspacePlanPath())==raw,"Fallback rewrote rejected cached bytes");
            }
            text(workspacePlanPath(),original);HANDLE held=CreateFileW(custom_steam_library::ioPath(workspacePlanPath()).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Cannot hold owned plan cache");try{verify("cache-locked",false,workspaceSnapshot(false));}catch(...){CloseHandle(held);throw;}CloseHandle(held);verify("cache-unlocked",true,workspaceSnapshot(false));
            text(g_worker,"owned cancelled-refresh fixture; never executed");auto cancelled=std::make_shared<BackgroundJobControl>();cancelled->cancelRequested=true;verify("cancelled-refresh-cache",true,workspaceSnapshot(true,g_steamTarget,cancelled));
            fs::rename(custom_steam_library::ioPath(workspacePlanPath()),custom_steam_library::ioPath(p/L"retained-plan.json"));verify("cache-missing",false,workspaceSnapshot(false));verify("cancelled-refresh-no-cache",false,workspaceSnapshot(true,g_steamTarget,cancelled));check(!fs::exists(custom_steam_library::ioPath(workspacePlanPath())),"Fallback published a new cache without authorization");
            text(workspacePlanPath(),original);verify("cache-restored",true,workspaceSnapshot(false));check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Fallback variant invoked external work");writeJsonWithLocalBackup(p/L"snapshot-fallbacks.json",{{"variants",reports},{"externalProcessAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()}},false);
        });
        test("discovercfg-native-plan-collisions",[&](const auto& p){
            const auto f=snapshotCacheFixture(p,2);
            const auto scanPath=g_dataRoot/L"state"/L"library-scan.json";
            auto scan=readJson(scanPath,json::object());
            auto& native=scan["games"][0];
            native["steamNative"]=true;
            native["nativeSteamAppId"]=223850;
            const auto directory=jsonStringOr(native,"gameDirectory"), executable=jsonStringOr(native,"primaryExecutable");
            auto current=f.cache["items"][0];
            current.update({{"steamNative",true},{"nativeSteamAppId",223850},{"steamStoreAppId",223850},
                {"status","already-in-steam"},{"compatibleMetadata","keep first exact"}});
            auto alternate=current;
            alternate["compatibleMetadata"]="later exact";
            auto stale=current;
            stale["nativeSteamAppId"]=945360;
            stale["primaryExecutable"]=nullptr;
            stale.erase("compatibleMetadata");
            auto retired=stale;
            retired["primaryExecutable"]=toUtf8((fs::path(toWide(executable)).parent_path()/L"retired.exe").wstring());
            auto owned=f.cache["items"][1];
            owned["shortAppId"]="4000000000";
            struct Variant { const char* name; json rows; bool hasPrimary; bool keepsMetadata; };
            const std::array<Variant,6> variants={{{"current-before-pathless",json::array({current,stale}),true,true},
                {"stale-before-current",json::array({retired,current}),true,true},
                {"two-compatible",json::array({current,alternate}),true,true},
                {"two-stale",json::array({stale,retired}),true,false},
                {"unconfirmed-native",json::array({retired,stale}),false,false},
                {"absent-from-cache",json::array(),true,false}}};
            json observations=json::array();
            bool passed=true;
            for(const auto& variant:variants){
                native["primaryExecutable"]=variant.hasPrimary?json(executable):json(nullptr);
                if(!variant.hasPrimary)native["primaryRole"]=nullptr;
                else native.erase("primaryRole");
                text(scanPath,scan.dump());
                auto cache=f.cache;
                cache["items"]=variant.rows;
                cache["items"].push_back(owned);
                const auto raw=cache.dump();
                text(workspacePlanPath(),raw);
                const auto result=workspaceSnapshot(false);
                json matches=json::array();
                bool shortcut=false;
                for(const auto& item:result["steamPlan"]["items"]){
                    if(jsonStringOr(item,"gameDirectory")==directory)matches.push_back(item);
                    if(jsonStringOr(item,"shortAppId")=="4000000000"&&jsonStringOr(item,"status")=="added-to-steam")shortcut=true;
                }
                const bool fromCache=jsonStringOr(result,"steamPlanSource")==toUtf8(workspacePlanPath().wstring());
                const bool unchanged=bytes(workspacePlanPath())==raw;
                const bool ok=matches.size()==1&&jsonIntegerOr(matches[0],"nativeSteamAppId",0)==223850&&
                    jsonStringOr(matches[0],"primaryExecutable")== (variant.hasPrimary?executable:std::string{})&&
                    jsonStringOr(matches[0],"compatibleMetadata")== (variant.keepsMetadata?"keep first exact":"")&&
                    result["library"]["games"].size()==2&&result["steamPlan"]["items"].size()==2&&shortcut&&fromCache&&unchanged;
                observations.push_back({{"name",variant.name},{"passed",ok},{"nativeRows",matches},
                    {"shortcutKept",shortcut},{"fromCache",fromCache},{"cacheUnchanged",unchanged}});
                passed=passed&&ok;
            }
            writeJsonWithLocalBackup(p/L"native-plan-collisions.json",{{"variants",observations}},false);
            check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Snapshot attempted external work");
            check(passed,"Cached plans did not reduce to one current native row while retaining compatible metadata");
        });
        const std::vector<std::wstring> files={L"library-scan.json",L"steam-accounts.json",L"steam-add-plan.json",L"library-steam-add-plan.json"};
        for(const auto& file:files)for(const auto& invalid:std::vector<json>{nullptr,true,7,"wrong",json::array()})test(("discovercfg-nonobject-default-"+toUtf8(file)+"-"+invalid.dump()).c_str(),[&,file,invalid](const auto& p){const auto target=g_dataRoot/L"state"/file;const auto raw=invalid.dump();text(target,raw);const json fallback={{"inactive",true}};check(readJson(target,fallback)==fallback&&bytes(target)==raw,"Nonobject state reached consumer or original changed");});
        for(const auto& version:std::vector<json>{2,true,1.0,"1",nullptr})test(("discovercfg-incompatible-version-"+version.dump()).c_str(),[&,version](const auto& p){auto value=plan();value["schemaVersion"]=version;const auto target=workspacePlanPath();text(target,value.dump());const auto before=bytes(target);check(readJson(target,{{"inactive",true}})==json{{"inactive",true}}&&newestCachedSteamPlan(false).source.empty()&&bytes(target)==before,"Incompatible plan promoted to cached authority");});
        for(const auto* key:{"items","summary"})for(const auto& invalid:std::vector<json>{nullptr,true,4,"bad"})test((std::string("discovercfg-plan-invalid-required-")+key+"-"+invalid.dump()).c_str(),[&,key,invalid](const auto&){auto value=plan();value[key]=invalid;check(!isPlanForCurrentLibrary(value),"Safe accessor masked invalid required plan field");});
        test("discovercfg-plan-missing-required-items-summary-rejected",[&](const auto&){auto value=plan();value.erase("items");value.erase("summary");check(!isPlanForCurrentLibrary(value),"Missing plan contract accepted");});
        for(const auto& field:std::vector<std::pair<std::wstring,std::string>>{{L"library-scan.json","games"},{L"library-scan.json","roots"},{L"library-scan.json","missingGames"},{L"steam-accounts.json","accounts"}})test(("discovercfg-wrong-container-"+toUtf8(field.first)+"-"+field.second).c_str(),[&,field](const auto&){const auto file=g_dataRoot/L"state"/field.first;const json value={{"schemaVersion",1},{field.second,nullptr}};text(file,value.dump());check(readJson(file,{{"inactive",true}})==json{{"inactive",true}},"Wrong discovery container retained");});
        for(const auto& file:{L"library-scan.json",L"steam-accounts.json",L"steam-add-plan.json"})test(("discovercfg-decoded-duplicate-preserve-"+toUtf8(file)).c_str(),[&,file](const auto&){auto value=plan();value["games"]=json::array();value["accounts"]=json::array();auto raw=value.dump();raw.insert(1,"\"schema\\u0056ersion\":2,");const auto target=g_dataRoot/L"state"/file;text(target,raw);check(readJson(target,{{"inactive",true}})==json{{"inactive",true}}&&bytes(target)==raw,"Decoded duplicate discovery data accepted");});
        test("discovercfg-depth-limit-default-without-write",[&](const auto&){auto value=plan();json nested=json::object();for(unsigned i=0;i<70;++i)nested={{"nested",nested}};value["unknownDepth"]=nested;const auto target=workspacePlanPath();text(target,value.dump());const auto before=bytes(target);check(readJson(target,{{"inactive",true}})==json{{"inactive",true}}&&bytes(target)==before,"Overdepth plan accepted");});
        test("discovercfg-lock-default-then-release-recovers",[&](const auto&){const auto value=plan();const auto target=workspacePlanPath();text(target,value.dump());HANDLE lock=CreateFileW(custom_steam_library::ioPath(target).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(lock!=INVALID_HANDLE_VALUE,"Fixture lock failed");json result;bool threw=false;try{result=readJson(target,{{"inactive",true}});}catch(...){threw=true;}CloseHandle(lock);check(!threw&&result==json{{"inactive",true}},"Locked plan threw rather than fallback");check(readJson(target,json::object())==value,"Plan remained failed after release");});
        test("discovercfg-nonregular-path-default-preserved",[&](const auto&){const auto target=workspacePlanPath();fs::create_directories(target);check(readJson(target,{{"inactive",true}})==json{{"inactive",true}}&&fs::is_directory(target),"Nonregular plan not inactive");});
        test("discovercfg-whole-snapshot-invalid-array-remains-usable",[&](const auto&){text(g_dataRoot/L"state"/L"library-scan.json","[]");text(g_dataRoot/L"state"/L"steam-accounts.json","[]");text(workspacePlanPath(),"[]");const auto snapshot=workspaceSnapshot(false);check(snapshot["library"].is_object()&&snapshot["library"]["games"].is_array()&&snapshot["steamAccounts"].is_object()&&snapshot["steamPlan"].is_object(),"Invalid state crashed snapshot");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Read fallback called external ports");});

        test("discovercfg-next-encoding-error-repair-storm-no-writes",[&](const auto&){const auto target=workspacePlanPath();const auto value=plan();const auto bad=std::string("{\"note\":\"\xff\"}",12);for(unsigned i=0;i<12;++i){text(target,bad);check(readJson(target,{{"inactive",true}})==json{{"inactive",true}}&&bytes(target)==bad,"Invalid encoding was consumed/rewritten");text(target,value.dump());check(readJson(target,json::object())==value,"Valid byte repair did not recover");}});
        test("discovercfg-next-empty-truncated-defaults-and-repair",[&](const auto&){const auto target=workspacePlanPath();for(const auto& bad:std::vector<std::string>{"","{","{\"schemaVersion\":1,"}){text(target,bad);check(readJson(target,{{"inactive",true}})==json{{"inactive",true}}&&bytes(target)==bad,"Torn file consumed or rewritten");}const auto value=plan();text(target,value.dump());check(newestCachedSteamPlan(false).document==value,"Repaired plan remained stale");});
        test("discovercfg-next-size-limit-refuses-before-load",[&](const auto&){const auto target=workspacePlanPath();text(target,"x");fs::resize_file(custom_steam_library::ioPath(target),(64ull<<20)+1);check(readJson(target,{{"inactive",true}})==json{{"inactive",true}}&&fs::file_size(custom_steam_library::ioPath(target))==(64ull<<20)+1,"Overlimit state did not retain original and inactive default");});
        for(const auto& kind:{"game-row","game-exe","account-row","account-id"})test((std::string("discovercfg-next-malformed-consumed-row-")+kind).c_str(),[&,kind](const auto&){const std::string type=kind;json value={{"schemaVersion",1}};const auto account=type.starts_with("account");const auto key=account?"accounts":"games";value[key]=json::array({json::object()});if(type.ends_with("row"))value[key][0]=true;else value[key][0][account?"accountId":"primaryExecutable"]=42;const auto target=g_dataRoot/L"state"/(account?L"steam-accounts.json":L"library-scan.json");text(target,value.dump());check(readJson(target,{{"inactive",true}})==json{{"inactive",true}},"Malformed consumed row accepted");});
        test("discovercfg-next-nested-duplicates-do-not-gain-cache-authority",[&](const auto&){auto value=plan();value["items"]=json::array({{{"primaryExecutable","c:\\games\\a.exe"},{"status","ready-to-add"}}});auto raw=value.dump();const auto pos=raw.find("\"status\":");check(pos!=std::string::npos,"fixture status absent");raw.insert(pos,"\"sta\\u0074us\":\"already-in-steam\",");text(workspacePlanPath(),raw);check(newestCachedSteamPlan(false).source.empty()&&bytes(workspacePlanPath())==raw,"Duplicate row status gained cached state");});
        test("discovercfg-next-missing-primary-does-not-revive-backup-plan",[&](const auto&){const auto value=plan();text(workspacePlanPath().wstring()+L".bak",value.dump());check(readJson(workspacePlanPath(),{{"inactive",true}})==json{{"inactive",true}}&&newestCachedSteamPlan(false).source.empty(),"Missing cache revived historical authority");});
        test("discovercfg-next-batch-plan-valid-candidate-and-policy",[&](const auto&){auto value=plan();text(batchPlanPath(),value.dump());check(newestCachedSteamPlan(false).source.empty(),"Batch plan overrode selected plan policy");check(newestCachedSteamPlan(true).document==value,"Eligible batch fallback lost");});

        test("discovercfg-next-selected-account-and-install-matching-plan-kept",[&](const auto& p){auto value=plan();value["accountId"]="1001";value["shortcutsVdf"]=toUtf8((p/L"Steam"/L"userdata"/L"1001"/L"config"/L"shortcuts.vdf").wstring());text(workspacePlanPath(),value.dump());g_steamTarget={{"accountId","1001"},{"shortcutsVdf",value["shortcutsVdf"]},{"steamRoot",toUtf8((p/L"Steam").wstring())}};check(newestCachedSteamPlan(false).document==value,"Matching account/install cache rejected");});
        test("discovercfg-next-selected-account-cannot-use-other-account-plan",[&](const auto& p){auto value=plan();value["accountId"]="1001";value["shortcutsVdf"]=toUtf8((p/L"Steam"/L"userdata"/L"1001"/L"config"/L"shortcuts.vdf").wstring());text(workspacePlanPath(),value.dump());g_steamTarget={{"accountId","1002"},{"shortcutsVdf",toUtf8((p/L"Steam"/L"userdata"/L"1002"/L"config"/L"shortcuts.vdf").wstring())}};check(newestCachedSteamPlan(false).source.empty(),"Selected account consumed cached status from another account");});
        test("discovercfg-next-selected-install-cannot-use-other-install-plan",[&](const auto& p){auto value=plan();value["accountId"]="1001";value["shortcutsVdf"]=toUtf8((p/L"Steam-A"/L"userdata"/L"1001"/L"config"/L"shortcuts.vdf").wstring());text(workspacePlanPath(),value.dump());g_steamTarget={{"accountId","1001"},{"shortcutsVdf",toUtf8((p/L"Steam-B"/L"userdata"/L"1001"/L"config"/L"shortcuts.vdf").wstring())}};check(newestCachedSteamPlan(false).source.empty(),"Selected installation consumed foreign cached authority");});
        test("discovercfg-current-and-legacy-compatible-plans-kept",[&](const auto&){auto value=plan();text(workspacePlanPath(),value.dump());check(newestCachedSteamPlan(false).document==value,"Healthy current plan rejected");value.erase("schemaVersion");text(workspacePlanPath(),value.dump());check(newestCachedSteamPlan(false).document==value,"Healthy legacy no-schema plan rejected");});
        test("discovercfg-foreign-library-plan-rejected",[&](const auto& p){auto value=plan();value["libraryState"]=toUtf8((p/L"another-library.json").wstring());check(!isPlanForCurrentLibrary(value),"Foreign library plan accepted");});
        test("discovercfg-bom-unicode-and-unknown-preserved",[&](const auto&){auto value=plan();value["unknown"]["note"]="中文";const auto raw=std::string("\xef\xbb\xbf",3)+value.dump();text(workspacePlanPath(),raw);check(readJson(workspacePlanPath(),json::object())==value&&bytes(workspacePlanPath())==raw,"BOM or unknown text normalized on disk");});
        }
        test("json-first-write-and-complete-backup",[](const auto& p){auto f=p/L"config.json";writeJsonWithLocalBackup(f,{{"v",1}},false);const auto old=bytes(f);writeJsonWithLocalBackup(f,{{"v",2}},false);check(readJson(f,json::object())["v"]==2&&bytes(f.wstring()+L".bak")==old,"Backup or activation incomplete");check(!temporaries(p),"Staging residue");});
        test("json-transient-sharing-retries",[](const auto& p){auto f=p/L"config.json";text(f,"old");int n=0;hostMoveFault=[&](const auto&,const auto& to){return to==f.wstring()&&++n<=3?ERROR_SHARING_VIOLATION:0;};writeJsonWithLocalBackup(f,{{"v",2}},false);check(n==4&&readJson(f,json::object())["v"]==2,"Did not retry sharing contention");});
        test("json-transient-access-denied-retries",[](const auto& p){auto f=p/L"config.json";text(f,"old");int n=0;hostMoveFault=[&](const auto&,const auto& to){return to==f.wstring()&&++n==1?ERROR_ACCESS_DENIED:0;};writeJsonWithLocalBackup(f,{{"v",2}},false);check(n==2,"Did not retry transient access denied");});
        test("json-disk-fault-preserves-original-and-cleans-temp",[](const auto& p){auto f=p/L"config.json";text(f,"old");hostMoveFault=[&](const auto&,const auto& to){return to==f.wstring()?ERROR_WRITE_FAULT:0;};mustFail([&]{writeJsonWithLocalBackup(f,{{"v",2}},false);});check(bytes(f)=="old"&&!temporaries(p),"Failed replacement damaged original");});
        test("json-readonly-does-not-force-permissions",[](const auto& p){auto f=p/L"config.json";text(f,"old");SetFileAttributesW(f.c_str(),FILE_ATTRIBUTE_READONLY);mustFail([&]{writeJsonWithLocalBackup(f,{{"v",2}},false);});check(bytes(f)=="old"&&!temporaries(p),"Readonly original changed");SetFileAttributesW(f.c_str(),FILE_ATTRIBUTE_NORMAL);});
        test("json-target-directory-fails-closed",[](const auto& p){auto f=p/L"config.json";fs::create_directory(f);mustFail([&]{writeJsonWithLocalBackup(f,{{"v",2}},false);});check(fs::is_directory(f)&&!temporaries(p),"Directory removed or residue");});
        test("json-backup-fault-preserves-original-and-backup",[](const auto& p){auto f=p/L"config.json";auto b=fs::path(f.wstring()+L".bak");text(f,R"({"v":1})");text(b,R"({"v":0})");hostMoveFault=[&](const auto&,const auto& to){return to==b.wstring()?ERROR_WRITE_FAULT:0;};mustFail([&]{writeJsonWithLocalBackup(f,{{"v",2}},false);});check(bytes(f)==R"({"v":1})"&&bytes(b)==R"({"v":0})"&&!temporaries(p),"Failed backup damaged files");});
        test("json-real-reader-lock-is-transient",[](const auto& p){auto f=p/L"config.json";text(f,"old");HANDLE h=CreateFileW(f.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);check(h!=INVALID_HANDLE_VALUE,"Fixture lock failed");std::thread unlock([=]{Sleep(200);CloseHandle(h);});try{writeJsonWithLocalBackup(f,{{"v",2}},false);}catch(...){unlock.join();throw;}unlock.join();check(readJson(f,json::object())["v"]==2&&!temporaries(p),"Real lock not retried");});
        test("json-invalid-utf8-has-no-filesystem-side-effect",[](const auto& p){auto f=p/L"config.json";text(f,"old");text(f.wstring()+L".bak","older");mustFail([&]{writeJsonWithLocalBackup(f,{{"bad",std::string(1,char(0xff))}},false);});check(bytes(f)=="old"&&bytes(f.wstring()+L".bak")=="older"&&!temporaries(p),"Serialization fault touched backup");});
        test("json-missing-parent-can-be-created",[](const auto& p){writeJsonWithLocalBackup(p/L"a"/L"b"/L"c.json",{{"ok",true}},false);check(fs::is_regular_file(p/L"a"/L"b"/L"c.json"),"Missing directories not created");});
        test("json-parent-is-file-fails-closed",[](const auto& p){text(p/L"a","not a directory");mustFail([&]{writeJsonWithLocalBackup(p/L"a"/L"b.json",{{"ok",true}},false);});check(bytes(p/L"a")=="not a directory","Parent file lost");});
        test("manual-image-activation-fault-keeps-old-image",[](const auto& p){auto src=p/L"source.png";text(src,"new");const auto dest=g_dataRoot/L"artwork"/L"overrides"/L"image-fault"/L"cover.png";text(dest,"old");hostMoveFault=[&](const auto&,const auto& to){return to==dest.wstring()?ERROR_WRITE_FAULT:0;};mustFail([&]{copyArtworkToIsolation(src,"image-fault","cover");});check(bytes(dest)=="old"&&!temporaries(g_dataRoot),"Old manual artwork truncated");
            hostMoveFault={};
            const auto originalRoot=g_dataRoot;struct Restore {fs::path root;~Restore(){hostMoveFault={};identityAfterMove={};configIoMode=0;configIoTarget.clear();g_dataRoot=root;}} restore{originalRoot};
            const auto bitmap=[](unsigned char color){const DWORD stride=900*3,body=stride*420;BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+body;BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=900;info.biHeight=420;info.biPlanes=1;info.biBitCount=24;info.biSizeImage=body;std::string data(file.bfSize,static_cast<char>(color));memcpy(data.data(),&file,sizeof(file));memcpy(data.data()+sizeof(file),&info,sizeof(info));return data;};
            json rows=json::array();bool safe=true;
            for(const auto* label:{"missing-first","unsupported-first","missing-later","second-asset-activation","config-activation","adjacent-backup","post-activation-readback","cover-success","all-success","clear-only","clear-and-later-failure","retained-source-success"}){
                const std::string mode=label;const auto root=p/L"variants"/toWide(mode);g_dataRoot=root/L"data";trustedAdmissionFixtureRoot(g_dataRoot);hostMoveFault={};identityAfterMove={};configIoMode=0;configIoTarget.clear();
                const auto exe=root/L"Game"/L"game.exe";text(exe,"owned inert executable, never run");
                const auto assetDir=g_dataRoot/L"artwork"/L"overrides"/toWide(artworkGameKey(exe)),config=g_dataRoot/L"config"/L"manual-overrides.json",peer=g_dataRoot/L"artwork"/L"overrides"/L"peer"/L"cover.bmp";
                const auto cover=assetDir/L"cover.bmp",landscape=assetDir/L"long.bmp",wallpaper=assetDir/L"wallpaper.bmp";
                const auto oldCover=bitmap(0x11),oldLong=bitmap(0x22),oldWallpaper=bitmap(0x33),newCover=bitmap(0x55),newLong=bitmap(0x66),newWallpaper=bitmap(0x77);
                text(cover,oldCover);text(landscape,oldLong);text(wallpaper,oldWallpaper);text(peer,"unrelated peer stays");
                const auto srcCover=root/L"incoming"/L"cover.bmp",srcLong=root/L"incoming"/L"long.bmp",srcWallpaper=root/L"incoming"/L"wallpaper.bmp";
                text(srcCover,newCover);text(srcLong,newLong);text(srcWallpaper,newWallpaper);text(root/L"unsupported.txt","unsupported image marker");
                const auto key=toUtf8(normalizedPathKey(exe));const auto slot=[](const fs::path& path){return json{{"source","manual"},{"file",toUtf8(path.wstring())},{"portableFile",toUtf8(path.wstring())},{"updatedAt",1}};};
                const json seed={{"schemaVersion",1},{"unknownRoot","retained"},{"items",{{key,{{"cover",slot(cover)},{"long",slot(landscape)},{"wallpaper",slot(wallpaper)},{"userNote","retained"},{"artworkProtection",{{"cover","manual"},{"long","manual"},{"wallpaper","manual"}}}}}}}};
                text(config,seed.dump());const auto before=bytes(config);
                json args={{"executable",toUtf8(exe.wstring())},{"coverPath",toUtf8(srcCover.wstring())},{"deferSnapshot",true}};
                const bool committed=mode=="post-activation-readback"||mode=="cover-success"||mode=="all-success"||mode=="clear-only"||mode=="retained-source-success";
                const bool raises=mode!="cover-success"&&mode!="all-success"&&mode!="clear-only"&&mode!="retained-source-success";
                if(mode=="missing-first")args["coverPath"]=toUtf8((root/L"missing.bmp").wstring());
                if(mode=="unsupported-first")args["coverPath"]=toUtf8((root/L"unsupported.txt").wstring());
                if(mode=="missing-later")args["longPath"]=toUtf8((root/L"missing.bmp").wstring());
                if(mode=="second-asset-activation"||mode=="config-activation"||mode=="adjacent-backup"||mode=="post-activation-readback"||mode=="all-success"){
                    args["longPath"]=toUtf8(srcLong.wstring());args["wallpaperPath"]=toUtf8(srcWallpaper.wstring());
                }
                if(mode=="retained-source-success")args["coverPath"]=toUtf8(cover.wstring());
                if(mode=="clear-only"){args.erase("coverPath");args["clearCover"]=true;args["clearLong"]=true;args["clearWallpaper"]=true;}
                if(mode=="clear-and-later-failure"){args.erase("coverPath");args["clearCover"]=true;args["longPath"]=toUtf8(srcLong.wstring());args["wallpaperPath"]=toUtf8((root/L"missing.bmp").wstring());}
                if(mode=="second-asset-activation")hostMoveFault=[&](const auto&,const auto& to)->DWORD{return to.find(assetDir.wstring())!=std::wstring::npos&&fs::path(to).filename().wstring().starts_with(L"long")?ERROR_WRITE_FAULT:0;};
                if(mode=="config-activation")hostMoveFault=[&](const auto&,const auto& to)->DWORD{return to.ends_with(config.wstring())?ERROR_WRITE_FAULT:0;};
                if(mode=="adjacent-backup")hostMoveFault=[&](const auto&,const auto& to)->DWORD{return to.ends_with(config.wstring()+L".bak")?ERROR_WRITE_FAULT:0;};
                if(mode=="post-activation-readback"){configIoTarget=config.wstring();identityAfterMove=[&](const auto&,const auto& to){if(to.ends_with(config.wstring()))configIoMode=3;};}
                std::string error;json result;try{result=executeWorkspaceCommand("saveArtworkOverride",args);}catch(const std::exception& e){error=e.what();}
                hostMoveFault={};identityAfterMove={};configIoMode=0;configIoTarget.clear();
                const auto current=json::parse(bytes(config));const auto& item=current["items"][key];const bool retained=bytes(cover)==oldCover&&bytes(landscape)==oldLong&&bytes(wallpaper)==oldWallpaper;
                bool references=true,desired=true;std::set<std::wstring> referenced;
                for(const auto* type:{"cover","long","wallpaper"})if(item.contains(type)){const auto file=fs::path(toWide(jsonStringOr(item[type],"portableFile")));references=references&&hostRegularFile(file);referenced.insert(file.wstring());}
                size_t extra=0;for(const auto& file:fs::directory_iterator(assetDir))if(file.is_regular_file()&&file.path()!=cover&&file.path()!=landscape&&file.path()!=wallpaper){++extra;if(committed)desired=desired&&referenced.count(file.path().wstring())!=0;}
                if(!committed)desired=desired&&bytes(config)==before&&extra==0;
                else{
                    desired=desired&&bytes(config)!=before&&current["unknownRoot"]=="retained"&&item["userNote"]=="retained"&&bytes(config.parent_path()/L"manual-overrides.json.bak")==before;
                    if(mode=="clear-only")for(const auto* type:{"cover","long","wallpaper"})desired=desired&&!item.contains(type)&&item["artworkProtection"][type]=="deleted";
                    else{
                        const auto acceptedCover=fs::path(toWide(jsonStringOr(item["cover"],"portableFile")));
                        desired=desired&&bytes(acceptedCover)==(mode=="retained-source-success"?oldCover:newCover);
                        if(mode=="all-success"||mode=="post-activation-readback")desired=desired&&bytes(fs::path(toWide(jsonStringOr(item["long"],"portableFile"))))==newLong&&bytes(fs::path(toWide(jsonStringOr(item["wallpaper"],"portableFile"))))==newWallpaper;
                    }
                }
                const bool passed=retained&&references&&desired&&(raises==!error.empty())&&bytes(peer)=="unrelated peer stays"&&!temporaries(g_dataRoot);
                rows.push_back({{"mode",mode},{"passed",passed},{"expectedCommitted",committed},{"raised",!error.empty()},{"error",error},{"oldAssetsRetained",retained},{"referencesExist",references},{"desiredState",desired},{"extraAssets",extra}});safe=safe&&passed;
            }
            writeJsonWithLocalBackup(p/L"manual-artwork-save-observed.json",{{"rows",rows},{"scope","Actual Host saveArtworkOverride/copy/config consumer and owned valid BMP files; exact IO failures, no installed Steam grids or external actions"}},false);
            check(rows.size()==12&&safe,"Manual artwork bundle damaged old assets or lost committed references");
        });
        test("manual-image-transient-replace-retries",[](const auto& p){auto src=p/L"source.png";text(src,"new");int count=0;hostMoveFault=[&](const auto&,const auto& to){return to.ends_with(L"cover.png")&&++count==1?ERROR_LOCK_VIOLATION:0;};const auto dest=copyArtworkToIsolation(src,"image-retry","cover");check(bytes(dest)=="new"&&count==2,"Manual retry failed");});
        test("copy-source-missing-keeps-destination",[](const auto& p){auto target=p/L"target";text(target,"old");mustFail([&]{copyHostFileAtomic(p/L"missing",target);});check(bytes(target)=="old"&&!temporaries(p),"Missing source destroyed destination");});
        test("copy-to-self-is-safe",[](const auto& p){auto f=p/L"image.png";text(f,"old");copyHostFileAtomic(f,f);check(bytes(f)=="old"&&!temporaries(p),"Same-file copy damaged bytes");});
        test("exit-diagnostic-failure-never-throws",[&](const auto& p){g_labRoot=p/L"not-directory";text(g_labRoot,"x");g_hostExitRecordWritten=false;writeHostExitRecord();writeHostExitRecord();check(g_hostExitRecordWritten.load(),"Exit diagnostic not bounded");g_labRoot=root;});
        auto run=[&](const wchar_t* mode,DWORD timeout=3000){return runProcess(selfExecutable(),{L"--fixture-worker",mode},timeout);};
        test("worker-empty-output",[&](const auto&){auto r=run(L"empty");check(r.exitCode==0&&r.output.empty(),"Empty-output worker failed");});
        test("worker-nonzero-output",[&](const auto&){auto r=run(L"nonzero");check(r.exitCode==23&&r.output.find("fixture error")!=std::string::npos,"Exit code or stderr lost");});
        test("worker-invalid-utf8-output-does-not-crash-host",[&](const auto&){auto r=run(L"invalid-output");check(r.exitCode==0&&r.output.size()>=4&&r.output.find("\xef\xbf\xbd")!=std::string::npos,"Binary output not handled");});
        test("worker-timeout-terminates-and-clears-slot",[&](const auto&){auto t=GetTickCount64();auto r=run(L"sleep",120);check(r.exitCode==ERROR_TIMEOUT&&GetTickCount64()-t<2000&&g_activeProcess==nullptr,"Timeout leaked worker/slot");});
        test("worker-cancel-before-start",[&](const auto&){g_cancelRequested=true;auto r=run(L"sleep");check(r.exitCode==ERROR_CANCELLED&&g_activeProcess==nullptr,"Cancelled worker started");});
        test("worker-shutdown-before-start",[&](const auto&){g_shuttingDown=true;auto r=run(L"sleep");check(r.exitCode==ERROR_CANCELLED,"Shutdown started new worker");});
        test("worker-output-retention-is-bounded",[&](const auto&){auto r=run(L"flood",15000);if(!((r.exitCode==0||r.exitCode==ERROR_BUFFER_OVERFLOW)&&r.output.size()<=(4u<<20)+100&&r.output.find("WORKER_OUTPUT_TRUNCATED")!=std::string::npos))throw std::runtime_error("Worker output: exit="+std::to_string(r.exitCode)+" bytes="+std::to_string(r.output.size())+" tail="+r.output.substr(r.output.size()>50?r.output.size()-50:0));});
        test("worker-output-flood-cannot-starve-timeout",[&](const auto&){auto t=GetTickCount64();auto r=run(L"continuous-flood",150);check((r.exitCode==ERROR_TIMEOUT||r.exitCode==ERROR_BUFFER_OVERFLOW)&&GetTickCount64()-t<2000,"Flood blocked timeout");});
        test("worker-descendant-holding-pipe-does-not-block",[&](const auto&){auto t=GetTickCount64();auto r=run(L"hold-pipe");check(r.exitCode==0&&GetTickCount64()-t<1500,"Final pipe read waited for descendant");});
        test("worker-missing-executable-does-not-leak-handles",[&](const auto& p){DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);for(int i=0;i<20;++i)mustFail([&]{runProcess(p/L"missing.exe",{},100);});GetProcessHandleCount(GetCurrentProcess(),&after);if(after>before+2)throw std::runtime_error("Handle count before="+std::to_string(before)+" after="+std::to_string(after));});
        test("shutdown-joins-multiple-running-workers",[&](const auto&){initializeWorkerProcessJob();std::atomic<int> finished=0;for(int i=0;i<3;++i)launchHostBackgroundTask([&]{auto r=run(L"sleep",10000);if(r.exitCode==ERROR_CANCELLED||r.exitCode!=0)++finished;});Sleep(150);g_shuttingDown=true;requestTaskCancellation();closeWorkerProcessJob();joinHostBackgroundTasks();if(!(finished==3&&g_hostThreads.empty()&&g_activeProcess==nullptr))throw std::runtime_error("Shutdown: finished="+std::to_string(finished)+" threads="+std::to_string(g_hostThreads.size())+" slot="+std::to_string(reinterpret_cast<uintptr_t>(g_activeProcess)));});
        test("worker-job-close-kills-suspended-child",[&](const auto&){initializeWorkerProcessJob();const auto exe=selfExecutable();auto cmd=quoteArgument(exe.wstring())+L" --fixture-worker sleep";std::vector<wchar_t> c(cmd.begin(),cmd.end());c.push_back(0);STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};check(CreateProcessW(exe.c_str(),c.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi),"Fixture child failed");assignProcessToHostWorkerJob(pi.hProcess);BOOL assigned=FALSE;IsProcessInJob(pi.hProcess,g_workerProcessJob,&assigned);closeWorkerProcessJob();const auto wait=WaitForSingleObject(pi.hProcess,1000);if(wait!=WAIT_OBJECT_0)TerminateProcess(pi.hProcess,99);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);check(assigned&&wait==WAIT_OBJECT_0,"Host job did not own/terminate child");});
        test("background-late-post-is-rejected-after-close",[](const auto&){HWND w=CreateWindowExW(0,L"STATIC",L"fixture",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);check(w!=nullptr,"Fixture window failed");g_window=w;auto a=std::make_unique<AsyncResult>();a->message={{"id",1}};check(enqueueWorkspaceResult(std::move(a)),"Initial result was not queued");stopWorkspaceResultPosting(w);auto b=std::make_unique<AsyncResult>();check(!enqueueWorkspaceResult(std::move(b)),"Result queued after shutdown");MSG m{};check(!PeekMessageW(&m,w,WM_WORKSPACE_RESULT,WM_WORKSPACE_RESULT,PM_REMOVE),"Close did not drain queued payload");DestroyWindow(w);g_window=nullptr;});
        test("background-unhandled-task-error-is-contained",[](const auto&){launchHostBackgroundTask([]{throw std::runtime_error("fixture background failure");});joinHostBackgroundTasks();check(g_hostThreads.empty(),"Failed background task not joined");});
        test("shutdown-forbids-new-background-thread",[](const auto&){g_shuttingDown=true;mustFail([]{launchHostBackgroundTask([]{});});check(g_hostThreads.empty(),"Shutdown started thread");});
        test("new-job-generation-invalidates-late-result",[](const auto&){std::vector<BackgroundJob> jobs;std::unordered_map<std::string,uint64_t> generations;auto a=beginBackgroundJob("fixture",jobs,generations,"game");auto b=beginBackgroundJob("fixture",jobs,generations,"game");check(!jobIsCurrent(generations,a)&&jobIsCurrent(generations,b)&&a->cancelRequested,"Old generation accepted");finishBackgroundJob(jobs,a);finishBackgroundJob(jobs,b);});
        test("different-game-generations-remain-independent",[](const auto&){std::vector<BackgroundJob> jobs;std::unordered_map<std::string,uint64_t> generations;auto a=beginBackgroundJob("fixture",jobs,generations,"a");auto b=beginBackgroundJob("fixture",jobs,generations,"b");check(jobIsCurrent(generations,a)&&jobIsCurrent(generations,b),"Other game cancelled");finishBackgroundJob(jobs,a);finishBackgroundJob(jobs,b);});
        test("native-grid-activation-failure-keeps-all-old-extensions",[](const auto& p){
            NativeGridProcessScope processScope;
            wchar_t observation[8]{};const auto count=GetEnvironmentVariableW(L"YMCC_NATIVE_ARTWORK_OBSERVATION_ONLY",observation,8);
            if(count)check(count<8&&(std::wstring(observation)==L"1"||std::wstring(observation)==L"0"),"Unknown native artwork observation selection");
            if(count&&std::wstring(observation)==L"1"){nativeArtworkObservationFixture(p);return;}
            const auto grid=p/L"SteamFixture"/L"grid";text(grid/L"620p.jpg","old jpg");text(grid/L"620p.png","old png");text(p/L"new.png","new");NativeSteamArtworkTarget target;target.appId=620;target.grids.push_back({"fixture",grid});hostMoveFault=[&](const auto&,const auto& to){return to==(grid/L"620p.png").wstring()?ERROR_WRITE_FAULT:0;};mustFail([&]{writeNativeSteamArtwork(target,"cover",toUtf8((p/L"new.png").wstring()),false);});check(bytes(grid/L"620p.jpg")=="old jpg"&&bytes(grid/L"620p.png")=="old png","Old extensions deleted before activation");check(!temporaries(p),"Native staging leaked");
            hostMoveFault={};const auto originalRoot=g_dataRoot;
            struct Restore {fs::path root;~Restore(){hostMoveFault={};identityAfterMove={};configIoMode=0;configIoTarget.clear();g_dataRoot=root;}} restore{originalRoot};
            const auto bitmap=[](char color){BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+900*3*420;BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=900;info.biHeight=420;info.biPlanes=1;info.biBitCount=24;info.biSizeImage=900*3*420;std::string data(header.bfSize,color);memcpy(data.data(),&header,sizeof(header));memcpy(data.data()+sizeof(header),&info,sizeof(info));return data;};
            const auto tree=[](const fs::path& root){std::map<std::wstring,std::string> value;std::error_code ec;if(!fs::is_directory(root,ec))return value;for(const auto& file:fs::directory_iterator(root))if(file.is_regular_file())value[file.path().filename().wstring()]=bytes(file.path());return value;};
            json rows=json::array(),processRows=json::array();bool safe=true,processSafe=true;
            for(const auto* label:{"direct-second-account","direct-missing-later-grid","second-account","second-slot","configuration","native-backup","clear-configuration","clear-then-later-failure","all-success","post-activation-readback","unsupported-source","readonly-second-account","rollback-blocked","external-change","immutable-accepted-source","duplicate-account-id","steam-running","missing-later-source",
                "owner-complete-absent","owner-complete-present","owner-query-failure","owner-snapshot-failure","owner-first-failure","owner-partial-failure","owner-cached-absent-final-unknown","owner-cached-absent-final-present"}){
                const std::string mode=label;const auto root=p/L"variants"/toWide(mode);g_dataRoot=root/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const bool processBoundary=mode.starts_with("owner-"),cacheBoundary=mode.starts_with("owner-cached-");
                auto processMode=NativeGridProcessMode::Absent;
                if(mode=="steam-running"||mode=="owner-complete-present")processMode=NativeGridProcessMode::Present;
                if(mode=="owner-query-failure")processMode=NativeGridProcessMode::QueryFailure;
                if(mode=="owner-snapshot-failure")processMode=NativeGridProcessMode::SnapshotFailure;
                if(mode=="owner-first-failure")processMode=NativeGridProcessMode::FirstFailure;
                if(mode=="owner-partial-failure")processMode=NativeGridProcessMode::PartialFailure;
                nativeGridSelect(processMode);nativeGridCalls={};hostMoveFault={};identityAfterMove={};configIoMode=0;configIoTarget.clear();
                const auto exe=root/L"Game"/L"game.exe";text(exe,"owned inert executable, never run");
                const auto steamA=root/L"SteamA",steamB=root/L"SteamB",gridA=steamA/L"userdata"/L"1001"/L"config"/L"grid",gridB=steamB/L"userdata"/(mode=="duplicate-account-id"?L"1001":L"1002")/L"config"/L"grid";
                NativeSteamArtworkTarget native;native.appId=620;native.grids={{"1001",gridA},{mode=="duplicate-account-id"?"1001":"1002",gridB}};
                std::vector<std::string> stems={"620p","620","620_hero"};for(size_t i=0;i<2;++i)for(const auto& stem:stems)for(const auto* ext:{L".jpg",L".png"})text((i?gridB:gridA)/(toWide(stem)+ext),"old-account-"+std::to_string(i)+"-"+stem+toUtf8(ext));
                text(gridA/L"999p.jpg","unrelated-A");text(gridB/L"999p.jpg","unrelated-B");const auto originalA=tree(gridA),originalB=tree(gridB);
                const auto assetDir=g_dataRoot/L"artwork"/L"overrides"/toWide(artworkGameKey(exe));const auto local=assetDir/L"cover.bmp";const auto oldLocal=bitmap(0x11),incoming=bitmap(0x55),changedSource=bitmap(0x77);text(local,oldLocal);
                const auto source=root/L"incoming.bmp";text(source,incoming);text(root/L"unsupported.txt","owned unsupported marker");const auto key=toUtf8(normalizedPathKey(exe));
                const auto config=g_dataRoot/L"config"/L"manual-overrides.json";const json seed={{"schemaVersion",1},{"unknownRoot","retain"},{"items",{{key,{{"cover",{{"source","manual"},{"file",toUtf8(local.wstring())},{"portableFile",toUtf8(local.wstring())}}},{"userNote","retain"}}}}}};text(config,seed.dump());const auto originalConfig=bytes(config);
                text(g_dataRoot/L"state"/L"library-scan.json",json{{"schemaVersion",1},{"games",json::array({{{"gameDirectory",toUtf8(exe.parent_path().wstring())},{"primaryExecutable",toUtf8(exe.wstring())},{"directoryName","Owned native game"},{"status","ready"},{"steamNative",true},{"nativeSteamAppId",620}}})}}.dump());
                json accounts=json::array();for(const auto& [id,grid]:native.grids)accounts.push_back({{"accountId",id},{"shortcutsVdf",toUtf8((grid.parent_path()/L"shortcuts.vdf").wstring())}});text(g_dataRoot/L"state"/L"steam-accounts.json",json{{"schemaVersion",1},{"accounts",accounts}}.dump());
                json args={{"executable",toUtf8(exe.wstring())},{"coverPath",toUtf8(source.wstring())},{"longPath",toUtf8(source.wstring())},{"wallpaperPath",toUtf8(source.wstring())},{"deferSnapshot",true}};
                const bool direct=mode.starts_with("direct-"),committed=mode=="all-success"||mode=="post-activation-readback"||mode=="immutable-accepted-source"||mode=="owner-complete-absent";
                const bool raises=mode!="all-success"&&mode!="immutable-accepted-source"&&mode!="owner-complete-absent";
                if(mode=="clear-configuration"){args.erase("coverPath");args.erase("longPath");args.erase("wallpaperPath");args["clearCover"]=true;args["clearLong"]=true;args["clearWallpaper"]=true;}
                if(mode=="clear-then-later-failure"){args.erase("coverPath");args["clearCover"]=true;}
                if(mode=="unsupported-source")args["coverPath"]=toUtf8((root/L"unsupported.txt").wstring());
                if(mode=="missing-later-source")args["longPath"]=toUtf8((root/L"missing.bmp").wstring());
                if(mode=="immutable-accepted-source")identityAfterMove=[&](const auto&,const auto& to){if(to.find(assetDir.wstring())!=std::wstring::npos)text(source,changedSource);};
                if(mode=="configuration"||mode=="clear-configuration"||mode=="duplicate-account-id")hostMoveFault=[&](const auto&,const auto& to)->DWORD{return to.ends_with(config.wstring())?ERROR_WRITE_FAULT:0;};
                if(mode=="native-backup")hostMoveFault=[&](const auto&,const auto& to)->DWORD{return to.find(L"native-steam-grid")!=std::wstring::npos&&fs::path(to).filename()==L"620_hero.png"?ERROR_WRITE_FAULT:0;};
                if(mode=="post-activation-readback"){configIoTarget=config.wstring();identityAfterMove=[&](const auto&,const auto& to){if(to.ends_with(config.wstring()))configIoMode=3;};}
                if(mode=="second-account"||mode=="direct-second-account")hostMoveFault=[&](const auto&,const auto& to)->DWORD{return to.ends_with((gridB/L"620p.bmp").wstring())?ERROR_WRITE_FAULT:0;};
                if(mode=="second-slot"||mode=="clear-then-later-failure")hostMoveFault=[&](const auto&,const auto& to)->DWORD{return to.ends_with((gridA/L"620.bmp").wstring())?ERROR_WRITE_FAULT:0;};
                if(mode=="direct-missing-later-grid"){native.grids[1].second=root/L"not-a-directory";text(root/L"not-a-directory","owned obstructing file");}
                if(mode=="readonly-second-account"){text(gridB/L"620p.bmp","readonly-old");SetFileAttributesW((gridB/L"620p.bmp").c_str(),FILE_ATTRIBUTE_READONLY);}
                const auto beforeB=tree(gridB);bool laterFailed=false;
                if(mode=="rollback-blocked"||mode=="external-change")hostMoveFault=[&](const auto&,const auto& to)->DWORD{
                    if(to.ends_with((gridB/L"620_hero.bmp").wstring())){laterFailed=true;if(mode=="external-change")text(gridA/L"620p.bmp","external-owner-bytes");return ERROR_WRITE_FAULT;}
                    if(mode=="rollback-blocked"&&laterFailed&&to.ends_with((gridA/L"620p.png").wstring()))return ERROR_WRITE_FAULT;
                    return 0;
                };
                bool processFlipped=false,cacheShared=false;NativeGridProcessCalls callsAtFlip;
                std::set<std::wstring> preparedBackupFiles;
                std::shared_ptr<const ymcc::processInventory::Snapshot> cachedAbsent;
                if(cacheBoundary)identityAfterMove=[&](const auto&,const auto& to){
                    if(processFlipped||to.find((g_dataRoot/L"backups"/L"native-steam-grid").wstring())==std::wstring::npos)return;
                    preparedBackupFiles.insert(normalizedPathKey(fs::path(to)));
                    if(preparedBackupFiles.size()!=12)return;
                    cachedAbsent=ymcc::steamprocess::candidates(0);
                    const auto queryBefore=nativeGridCalls.query;
                    const auto shared=ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),cachedAbsent->observedAt,250);
                    cacheShared=shared.get()==cachedAbsent.get()&&nativeGridCalls.query==queryBefore;
                    check(cacheShared&&cachedAbsent->status==ymcc::processInventory::Status::Complete&&cachedAbsent->rows.empty(),"Cache precondition must be the actual shared owner's complete Absent");
                    processFlipped=true;callsAtFlip=nativeGridCalls;
                    nativeGridSelect(mode=="owner-cached-absent-final-present"?NativeGridProcessMode::Present:NativeGridProcessMode::QueryFailure,false);
                };
                const auto createBefore=admissionProcesses.load(),shellBefore=admissionShell.load(),networkBefore=admissionNetwork.load();
                std::string error;try{if(direct)writeNativeSteamArtwork(native,"cover",toUtf8(source.wstring()),false);else executeWorkspaceCommand("saveArtworkOverride",args);}catch(const std::exception& e){error=e.what();}
                const auto productionCalls=nativeGridCalls;
                hostMoveFault={};identityAfterMove={};configIoMode=0;configIoTarget.clear();
                if(mode=="readonly-second-account")SetFileAttributesW((gridB/L"620p.bmp").c_str(),FILE_ATTRIBUTE_NORMAL);
                const auto afterA=tree(gridA),afterB=tree(gridB);bool desired=true,configState=committed?bytes(config)!=originalConfig:bytes(config)==originalConfig;
                if(committed){
                    for(const auto& grid:{gridA,gridB})for(const auto& stem:stems){const auto& accepted=mode=="immutable-accepted-source"&&stem!="620p"?changedSource:incoming;desired=desired&&bytes(grid/(toWide(stem)+L".bmp"))==accepted&&!fs::exists(grid/(toWide(stem)+L".jpg"))&&!fs::exists(grid/(toWide(stem)+L".png"));}
                    const auto saved=json::parse(bytes(config));for(const auto* type:{"cover","long","wallpaper"}){const auto file=fs::path(toWide(jsonStringOr(saved["items"][key][type],"portableFile")));desired=desired&&hostRegularFile(file)&&bytes(file)==(mode=="immutable-accepted-source"&&std::string(type)!="cover"?changedSource:incoming);}
                }else if(mode=="rollback-blocked")desired=error.find("恢复未完成")!=std::string::npos&&error.find("备份")!=std::string::npos&&afterB==originalB&&!fs::exists(gridA/L"620p.png");
                else if(mode=="external-change"){auto expected=originalA;expected[L"620p.bmp"]="external-owner-bytes";desired=afterA==expected&&afterB==originalB&&error.find("恢复未完成")!=std::string::npos;}
                else desired=afterA==originalA&&afterB==beforeB;
                size_t pending=0;for(const auto& file:fs::directory_iterator(assetDir))if(file.is_regular_file()&&file.path()!=local)++pending;
                const bool oldLocalPreserved=bytes(local)==oldLocal;const bool cleanup=committed||pending==0;
                size_t backups=0;const auto backupRoot=g_dataRoot/L"backups"/L"native-steam-grid";if(fs::exists(custom_steam_library::ioPath(backupRoot)))for(const auto& file:fs::recursive_directory_iterator(custom_steam_library::ioPath(backupRoot,true)))if(file.is_regular_file())++backups;
                if(mode=="rollback-blocked"||mode=="external-change")desired=desired&&backups>=12;
                const bool passed=desired&&configState&&oldLocalPreserved&&cleanup&&(raises==!error.empty())&&!temporaries(g_dataRoot)&&bytes(gridA/L"999p.jpg")=="unrelated-A"&&bytes(gridB/L"999p.jpg")=="unrelated-B";
                if(processBoundary){
                    const bool present=mode=="owner-complete-present"||mode=="owner-cached-absent-final-present";
                    const bool absent=mode=="owner-complete-absent",unknown=!present&&!absent;
                    const auto receipt=ymcc::steamprocess::candidates(0);
                    const auto probeQueries=nativeGridCalls.query;
                    const auto shared=ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),receipt->observedAt,250);
                    const bool sameOwner=shared.get()==receipt.get()&&nativeGridCalls.query==probeQueries;
                    const auto presence=ymcc::steamprocess::presence(L"steam.exe",0);
                    const bool classification=unknown?presence==ymcc::steamprocess::Presence::Unknown&&receipt->status==ymcc::processInventory::Status::Failed&&receipt->rows.empty()&&receipt->error==ERROR_ACCESS_DENIED:
                        presence==(present?ymcc::steamprocess::Presence::Present:ymcc::steamprocess::Presence::Absent)&&receipt->status==ymcc::processInventory::Status::Complete&&receipt->rows.size()==(present?1u:0u);
                    const bool partial=mode=="owner-partial-failure";
                    const bool hints=partial?receipt->rejectionHints.size()==1&&receipt->rejectionHints[0].name==L"steam.exe":receipt->rejectionHints.empty();
                    const bool hard=mode=="owner-query-failure"||mode=="owner-cached-absent-final-unknown";
                    const bool basic=receipt->basicStatus==(hard?ymcc::processInventory::Status::Failed:ymcc::processInventory::Status::Unavailable);
                    const uint64_t queries=cacheBoundary?4:absent?3:1;
                    const uint64_t snapshots=hard?(cacheBoundary?3:0):queries;
                    const uint64_t firsts=mode=="owner-snapshot-failure"?0:snapshots;
                    const uint64_t nexts=present||partial?1:0;
                    const bool counts=productionCalls.query==queries&&productionCalls.snapshot==snapshots&&productionCalls.first==firsts&&productionCalls.next==nexts;
                    const bool cacheWindow=processFlipped&&productionCalls.lastQueryAt>=cachedAbsent->observedAt&&productionCalls.lastQueryAt-cachedAbsent->observedAt<250;
                    const bool finalFresh=!cacheBoundary||(processFlipped&&cacheShared&&cacheWindow&&productionCalls.query==callsAtFlip.query+1&&receipt->generation>cachedAbsent->generation&&cachedAbsent->status==ymcc::processInventory::Status::Complete&&cachedAbsent->rows.empty()&&backups==12);
                    const bool noControl=productionCalls.openProcess==0&&productionCalls.terminate==0&&admissionProcesses.load()==createBefore&&admissionShell.load()==shellBefore&&admissionNetwork.load()==networkBefore;
                    const bool released=productionCalls.opened==productionCalls.closed&&nativeGridSnapshots.empty();
                    const bool explicitError=unknown?error.find("[steam-process-unknown]")!=std::string::npos:absent?error.empty():error.find("原生 Steam 素材写入需要先关闭 Steam")!=std::string::npos;
                    const bool processPassed=passed&&classification&&hints&&basic&&sameOwner&&counts&&finalFresh&&noControl&&released&&productionCalls.unexpected==0&&explicitError;
                    processRows.push_back({{"mode",mode},{"passed",processPassed},{"error",error},{"originalFileAssertions",passed},{"classification",classification},{"partialHintsOnly",hints},{"basicStatus",basic},{"sameOwner",sameOwner},{"exactProductionCalls",counts},{"finalFresh",finalFresh},{"noProcessControl",noControl},{"snapshotsReleased",released},{"backups",backups},
                        {"productionCalls",{{"query",productionCalls.query},{"snapshot",productionCalls.snapshot},{"first",productionCalls.first},{"next",productionCalls.next},{"opened",productionCalls.opened},{"closed",productionCalls.closed}}},{"probeAdditionalQueries",nativeGridCalls.query-productionCalls.query},{"flippedAfterFinalBackup",processFlipped},{"cacheWindowValid",cacheWindow},{"cacheAgeAtFinalQueryMs",processFlipped?productionCalls.lastQueryAt-cachedAbsent->observedAt:0}});
                    processSafe=processSafe&&processPassed;
                }else {
                rows.push_back({{"mode",mode},{"passed",passed},{"error",error},{"desiredState",desired},{"configState",configState},{"oldLocalPreserved",oldLocalPreserved},{"uncommittedAssetsCleaned",cleanup},{"backups",backups},{"expectedCommitted",committed}});safe=safe&&passed;
                }
            }
            writeJsonWithLocalBackup(p/L"native-artwork-save-observed.json",{{"rows",rows},{"processRows",processRows},{"scope","Actual native writer/full editor command and owned fake account grids; controlled process enumeration and IO failures. No installed Steam operations."}},false);
            check(rows.size()==18&&safe,"Native artwork bundle left partial writes or hid incomplete recovery");
            check(processRows.size()==8&&processSafe,"Shared owner tri-state or final fresh0 boundary failed");
        });
        test("native-grid-success-replaces-only-own-slot",[](const auto& p){NativeGridProcessScope processScope;const auto grid=p/L"SteamFixture"/L"grid";text(grid/L"620p.jpg","old cover");text(grid/L"620_hero.png","hero stays");text(p/L"new.png","new cover");NativeSteamArtworkTarget target;target.appId=620;target.grids.push_back({"fixture",grid});writeNativeSteamArtwork(target,"cover",toUtf8((p/L"new.png").wstring()),false);check(bytes(grid/L"620p.png")=="new cover"&&!fs::exists(grid/L"620p.jpg")&&bytes(grid/L"620_hero.png")=="hero stays","Slot/extension replacement damaged other files");check(nativeGridCalls.query==2&&nativeGridCalls.snapshot==2&&nativeGridCalls.first==2&&nativeGridCalls.next==0&&nativeGridCalls.opened==2&&nativeGridCalls.closed==2&&nativeGridCalls.unexpected==0&&nativeGridCalls.openProcess==0&&nativeGridCalls.terminate==0&&nativeGridSnapshots.empty(),"Positive control bypassed the real shared owner or leaked a snapshot");});
        test("unicode-artwork-and-config-paths",[](const auto& p){const auto source=p/L"测试游戏 Ω"/L"封面.png";text(source,"local artwork");const auto dest=p/L"数据"/L"图片.png";copyHostFileAtomic(source,dest);writeJsonWithLocalBackup(p/L"数据"/L"配置.json",{{"game","测试游戏"}},false);check(bytes(dest)=="local artwork"&&!temporaries(p),"Unicode paths failed");});
        test("manifest-wrong-root-and-invalid-json-are-skipped",[](const auto& p){const auto saved=g_dataRoot;g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);text(g_dataRoot/L"artwork"/L"bad"/L"manifest.json","{");writeJsonWithLocalBackup(g_dataRoot/L"artwork"/L"array"/L"manifest.json",json::array(),false);const auto exe=p/L"Game.exe";writeJsonWithLocalBackup(g_dataRoot/L"artwork"/L"valid"/L"manifest.json",{{"exe",toUtf8(exe.wstring())}},false);auto result=findPortableArtworkManifestForHost(toUtf8(exe.wstring()));g_dataRoot=saved;check(result&&result->second.parent_path().filename()==L"valid","Bad manifest hid valid game");});
        test("empty-artwork-manifest-exe-does-not-match",[](const auto& p){const auto saved=g_dataRoot;g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);writeJsonWithLocalBackup(g_dataRoot/L"artwork"/L"manifest.json",{{"exe",""}},false);auto result=findPortableArtworkManifestForHost("");g_dataRoot=saved;check(!result,"Empty path accepted as identity");});
        test("multiple-host-fixture-mutex-denies-second-owner",[](const auto&){const auto name=L"Local\\YeManSteamLibraryFixture-"+std::to_wstring(GetCurrentProcessId());HANDLE first=CreateMutexW(nullptr,TRUE,name.c_str());check(first!=nullptr,"Fixture mutex failed");HANDLE second=CreateMutexW(nullptr,TRUE,name.c_str());const auto error=GetLastError();CloseHandle(second);ReleaseMutex(first);CloseHandle(first);check(error==ERROR_ALREADY_EXISTS,"Second instance not detected");});
        test("host-json-and-image-copy-beyond-max-path",[](const auto& p){auto dir=p;for(int i=0;i<8;++i)dir/=L"long-config-artwork-fixture-012345";const auto config=dir/L"config.json";check(config.wstring().size()>300,"Long-path fixture too short");writeJsonWithLocalBackup(config,{{"id",1}},false);writeJsonWithLocalBackup(config,{{"id",2}},false);check(readJson(config,json::object())["id"]==2,"Long-path Host JSON failed");const auto copy=dir/L"local-artwork.png";copyHostFileAtomic(config,copy);check(bytes(copy)==bytes(config),"Long-path artwork copy failed");});
        test("automatic-no-id-queue-applies-pictures-without-retrying-identity",[](const auto& p){
            wchar_t retry[32]{};const auto count=GetEnvironmentVariableW(L"YMCC_QUEUE_NETWORK_RETRY_CONTROL",retry,32);
            if(count){
                check(count<32,"Network retry control too long");const auto mode=toUtf8(retry);
                check(mode=="queued"||mode=="unconfirmed"||mode=="duplicate"||mode=="manual"||mode=="artwork","Unknown network retry control");
                runQueueFixture(p,mode=="manual",false,false,mode);return;
            }
            runQueueFixture(p,false,false);runQueueFixture(p/L"cached-programs",false,false,true);
            // Reproduce the directory-creation limit while paths and Worker
            // arguments retain their ordinary spelling. Derive the padding
            // from the actual PID/next batch rather than a fixed path prefix.
            const auto suffix=fs::path(L"data")/L"cache"/L"jobs"/L"library-batch-identification"/
                (L"scrape-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(g_scrapeBatchSequence.load()+1))/L"artwork-fallback"/L"item-1";
            const auto fixedLength=((p/L"long-queue")/suffix).wstring().size();
            check(fixedLength<251,"Queue boundary fixture root too long");
            const auto longRoot=p/(L"long-queue"+std::wstring(252-fixedLength,L'q'));
            check((longRoot/suffix).wstring().size()==252,"Queue boundary fixture not exact");
            runQueueFixture(longRoot,false,false);
            const auto savedData=g_dataRoot,savedLab=g_labRoot;
            try {
                g_dataRoot=longRoot/L"data";g_labRoot=longRoot;trustedAdmissionFixtureRoot(g_dataRoot);
                // The produced nested manifest itself exceeds MAX_PATH.
                // Read it through the real artifact consumer and migrate a
                // legacy identity hint without leaking extended IO spelling.
                const auto executable=longRoot/L"Games"/L"Ishin2"/L"ISHIN2.EXE";
                const auto artifact=scrapeArtifactSummary(longRoot/suffix,toUtf8(executable.wstring()));
                check(!artifact.value("artifactReadError",false)&&artifact.contains("manifest"),"Long queue artifact was unreadable");
                const auto manifestPath=fs::path(toWide(jsonStringOr(artifact,"manifestPath")));
                check(manifestPath.wstring().size()>260&&manifestPath==longRoot/suffix/L"result"/L"manifest.json","Long artifact lost ordinary path");
                auto manifest=readJson(manifestPath,json::object());manifest["match"]["formalName"]="Owned legacy identity hint";
                writeJsonWithLocalBackup(manifestPath,manifest,false);json migrated=json::object();attachResolvedIdentityCache(migrated);
                const auto key=toUtf8(normalizedPathKey(executable));
                writeJsonWithLocalBackup(p/L"queue-migration-observed.json",{{"snapshot",migrated},{"expectedManifestPath",toUtf8(manifestPath.wstring())},{"key",key}},false);
                check(migrated["resolvedIdentities"].contains(key)&&jsonStringOr(migrated["resolvedIdentities"][key],"manifestPath")==toUtf8(manifestPath.wstring()),"Long legacy migration lost identity or ordinary path");
            }catch(...){g_dataRoot=savedData;g_labRoot=savedLab;throw;}
            g_dataRoot=savedData;g_labRoot=savedLab;
            writeJsonWithLocalBackup(p/L"queue-path-boundary-observed.json",{{"root",toUtf8(longRoot.wstring())},
                {"outputDirectory",toUtf8((longRoot/suffix).wstring())},{"length",(longRoot/suffix).wstring().size()},
                {"ordinaryPaths",!(longRoot/suffix).wstring().starts_with(LR"(\\?\)")},
                {"running",g_scrapeQueueRunning.load()},{"pendingJobs",g_scrapeQueueJobs.size()}},false);
            bool safe=true;for(bool artwork:{false,true})for(bool reject:{false,true}){
                try{runQueueRemovalFixture(p/(artwork?L"artwork":L"identity")/(reject?L"failed":L"confirmed"),artwork,reject);}
                catch(const std::exception& error){if(std::string(error.what())!="Removed game continued automatic writes/stages or interrupted retained games")throw;safe=false;}
            }
            check(safe,"Removed game continued automatic writes/stages or interrupted retained games");
        });
        test("identity-cooldown-does-not-block-no-id-artwork",[](const auto& p){runQueueFixture(p,false,true);});
        test("manual-refresh-still-excludes-already-added-shortcuts",[](const auto& p){runQueueFixture(p,true,false);runQueueFixture(p/L"manual-program",true,false,true);});
        test("gpu-utility-frame-failures-do-not-reload-or-close-editor",[](const auto&){for(const auto kind:{COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED,COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED,COREWEBVIEW2_PROCESS_FAILED_KIND_UTILITY_PROCESS_EXITED})check(!workspaceWebViewFailureNeedsRecovery(kind),"Resource subprocess warning escalated to whole-window recovery");});
        test("actual-browser-renderer-failures-still-recover",[](const auto&){for(const auto kind:{COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED,COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED,COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE})check(workspaceWebViewFailureNeedsRecovery(kind),"Fatal WebView process failure ignored");});

        test("builtin-steam-tool-direct-id-identify-blocked-before-worker",[](const auto&){
            const json args={{"executable","G:/Fixture/Game.exe"},{"steamId","228980"}};
            mustFail([&]{executeWorkspaceCommand("identifyOne",args);});
            mustFail([&]{startManualIdentifyTask(args);});
        });
        test("builtin-steam-tool-name-path-search-blocked-before-job",[](const auto&){
            mustFail([&]{startIdentitySearchRequest({{"executable","G:/Fixture/Game.exe"},{"query","Steamworks Common Redistributables"}});});
            mustFail([&]{startIdentitySearchTask({{"executable","G:/Steam/steamapps/common/Steamworks Shared/vcredist.exe"},{"query","Portal 2"}});});
        });
        test("builtin-steam-tool-artwork-search-does-not-launch-worker",[](const auto&){
            mustFail([&]{executeWorkspaceCommand("searchArtwork",{{"query","Renamed Tool"},{"steamAppId","228980"},{"type","cover"}});});
        });
        test("empty-root-config-always-enters-first-run-retry-path",[](const auto& p){
            g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);
            for(const auto& status:{"pending","running","completed","skipped-existing-roots"}) {
                writeJsonWithLocalBackup(g_dataRoot/L"config"/L"library-config.json",{{"roots",json::array()},{"firstRunDefaultScan",{{"status",status}}}},false);
                check(firstRunDefaultScanPending(),"Zero-root config incorrectly treated as permanently complete");
            }
        });
        test("explicitly-cleared-roots-do-not-enter-first-run-discovery",[](const auto& p){
            g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);
            writeJsonWithLocalBackup(g_dataRoot/L"config"/L"library-config.json",{{"roots",json::array()},{"scanRootsExplicitlyCleared",true}},false);
            check(!firstRunDefaultScanPending(),"User-cleared roots triggered discovery");
            const auto arguments=libraryScanWorkerArguments();
            check(std::find(arguments.begin(),arguments.end(),L"--first-run-defaults")==arguments.end(),"User-cleared roots added discovery flag");
        });
        test("builtin-steam-tool-cache-is-not-exposed",[](const auto& p){
            g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);g_labRoot=p;const auto cache=g_dataRoot/L"cache"/L"identity";
            writeJsonWithLocalBackup(cache/L"tool.json",{{"executable","G:/Fixture/Tool.exe"},{"match",{{"appId",228980},{"formalName","Renamed Tool"}}}},false);
            writeJsonWithLocalBackup(cache/L"game.json",{{"executable","G:/Fixture/Portal.exe"},{"match",{{"appId",620},{"formalName","Portal 2"}}}},false);
            json snapshot=json::object();attachResolvedIdentityCache(snapshot);
            check(snapshot["resolvedIdentities"].size()==1,"Tool cache leaked or valid game cache disappeared");
        });

        for(const auto* name:{L"library_header.jpg",L"library_header_schinese.jpg",L"library_header_2x.png",L"header.jpg",L"header_schinese.jpg"}) {
            const auto title="native-local-horizontal-"+toUtf8(name);
            test(title.c_str(),[=](const auto& p){
                const auto root=p/L"SteamFixture",source=root/L"appcache"/L"librarycache"/L"620"/L"hash"/name;
                text(source,"local steam horizontal");const auto found=directNativeSteamLibraryCacheArtwork(root,"620","long");
                check(found&&*found==source,"Native Steam header filename was ignored");
            });
        }
        test("native-modern-header-preferred-over-legacy-header",[](const auto& p){
            const auto root=p/L"SteamFixture",cache=root/L"appcache"/L"librarycache"/L"620";
            text(cache/L"header.jpg","legacy header");text(cache/L"hash"/L"library_header.jpg","modern header");
            const auto found=directNativeSteamLibraryCacheArtwork(root,"620","long");
            check(found&&bytes(*found)=="modern header","Modern library header lost to legacy capsule");
        });
        test("native-horizontal-never-uses-hero-capsule-blur-or-other-app",[](const auto& p){
            const auto root=p/L"SteamFixture",cache=root/L"appcache"/L"librarycache";
            for(const auto* name:{L"library_hero.jpg",L"library_capsule.jpg",L"library_header_blur.jpg",L"headerFake.jpg"})text(cache/L"620"/name,"wrong artwork");
            text(cache/L"621"/L"library_header.jpg","another game");
            check(!directNativeSteamLibraryCacheArtwork(root,"620","long"),"Wrong native artwork was used as horizontal cover");
        });
        test("native-modern-horizontal-is-copied-persisted-and-reused-without-steam-read",[](const auto& p){
            g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto root=p/L"SteamFixture",source=root/L"appcache"/L"librarycache"/L"620"/L"hash"/L"library_header_schinese.jpg";
            static std::vector<ymcc::steaminfo::Facts> facts;
            static size_t reads=0;
            static bool masksValid=true;
            const auto readFacts=+[](std::uint32_t mask)noexcept -> ymcc::steaminfo::Facts {
                masksValid=masksValid&&mask==ymcc::steaminfo::Installations;
                const auto index=reads++;
                return index<facts.size()?std::move(facts[index]):ymcc::steaminfo::Facts{};
            };
            const auto receipt=[](const fs::path& install){
                ymcc::steaminfo::Facts result;result.available=true;
                result.installations.push_back({toUtf8(install.wstring()),"owned-fixture",ERROR_SUCCESS});
                return result;
            };
            text(source,"modern native header");
            const json item={{"steamNative",true},{"nativeSteamAppId",620},{"steamStoreAppId",620},
                {"gameDirectory",toUtf8((root/L"steamapps"/L"common"/L"Portal 2").wstring())},
                {"nativeSteamManifestPath",toUtf8((root/L"steamapps"/L"appmanifest_620.acf").wstring())}};
            facts={receipt(root)};reads=0;masksValid=true;
            json plan={{"items",json::array({item})}};attachNativeSteamArtwork(plan,json::object(),true,readFacts);
            check(reads==1&&masksValid,"One native game asked for installations more than once");
            const auto preview=plan["items"][0]["artworkPreview"]["long"];
            check(preview["provider"]=="steam-librarycache-native"&&preview["file"]==toUtf8(source.wstring()),"Native horizontal did not reach plan");
            check(bytes(fs::path(toWide(preview["portableFile"].get<std::string>())))=="modern native header","Cached native bytes changed");
            check(plan["items"][0]["nativeSteamArtwork"]["longAvailable"]==true,"Native long availability not recorded");
            fs::remove(source);json next={{"items",json::array({item})}};attachNativeSteamArtwork(next,json::object(),false,readFacts);
            check(next["items"][0]["artworkPreview"]["long"]["url"]==preview["url"],"Background snapshot dropped portable native horizontal");
            check(reads==1,"Cache-only snapshot asked Steam for installations");

            const auto alternate=p/L"OtherSteam";
            const auto batch=[] {return json{{"items",json::array({{{"steamNative",false}},
                {{"steamNative",true},{"nativeSteamAppId","621"}},{{"steamNative",true},{"nativeSteamAppId","622"}}})}};};
            const auto first=alternate/L"appcache"/L"librarycache"/L"621"/L"header.jpg";
            const auto second=alternate/L"userdata"/L"42"/L"config"/L"grid"/L"622.jpg";
            text(first,"first appcache bytes");text(second,"second grid bytes");
            facts={ymcc::steaminfo::Facts{},receipt(alternate)};reads=0;
            auto recovered=batch();attachNativeSteamArtwork(recovered,json::object(),true,readFacts);
            check(reads==2&&masksValid,"Unavailable facts were retained, or success was not shared by both games");
            check(recovered["items"][1]["artworkPreview"]["long"]["file"]==toUtf8(first.wstring())&&
                recovered["items"][2]["artworkPreview"]["long"]["file"]==toUtf8(second.wstring()),"Recovered shared root did not feed appcache and grid");

            const auto later=root/L"appcache"/L"librarycache"/L"621"/L"header.jpg";
            text(later,"new batch bytes");facts={receipt(root)};reads=0;
            auto refreshed=batch();attachNativeSteamArtwork(refreshed,json::object(),true,readFacts);
            check(reads==1&&masksValid,"Next batch did not take exactly one fresh installation receipt");
            check(refreshed["items"][1]["artworkPreview"]["long"]["file"]==toUtf8(later.wstring())&&
                !refreshed["items"][2].contains("artworkPreview"),"Previous batch installation leaked into fresh refresh");
            auto skipped=json{{"items",json::array({{{"steamNative",false}},{{"steamNative",true}}})}};
            reads=0;attachNativeSteamArtwork(skipped,json::object(),true,readFacts);
            check(reads==0,"Refresh without valid native AppID asked Steam for installations");
        });

        wchar_t corpusBuffer[32768]{};
        if(GetEnvironmentVariableW(L"YMCC_NATIVE_ARTWORK_EVIDENCE",corpusBuffer,32768)>0) {
            const std::wstring corpusFile=corpusBuffer;
            test("native-horizontal-real-steam-cache-corpus",[&](const auto& p){
                const auto file=fs::absolute(fs::path(corpusFile));
                check(normalizedPathKey(file).starts_with(normalizedPathKey(build)+L"\\"),"Corpus index must stay below Build");
                const auto corpus=readJson(file,json::object());const auto steamRoot=fs::path(toWide(jsonStringOr(corpus,"steamRoot")));
                check(normalizedPathKey(steamRoot).starts_with(normalizedPathKey(build)+L"\\"),"Steam corpus must be an isolated Build copy");
                check(jsonArrayOr(corpus,"cases").size()>=1,"No actual Steam header evidence");g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);
                for(const auto& entry:jsonArrayOr(corpus,"cases")){
                    const auto appId=jsonStringOr(entry,"appId");const auto found=directNativeSteamLibraryCacheArtwork(steamRoot,appId,"long");
                    check(found.has_value(),"Actual Steam horizontal header ignored");
                    const auto expected=fs::path(toWide(jsonStringOr(entry,"expectedSource")));
                    check(normalizedPathKey(*found)==normalizedPathKey(expected)&&bytes(*found)==bytes(expected),"Actual Steam header selected another source");
                    const json item={{"steamNative",true},{"nativeSteamAppId",std::stoi(appId)},{"steamStoreAppId",std::stoi(appId)},
                        {"nativeSteamManifestPath",toUtf8((steamRoot/L"steamapps"/(L"appmanifest_"+toWide(appId)+L".acf")).wstring())}};
                    json plan={{"items",json::array({item})}};attachNativeSteamArtwork(plan,json::object(),true);
                    const auto preview=plan["items"][0]["artworkPreview"]["long"];
                    check(bytes(fs::path(toWide(preview["portableFile"].get<std::string>())))==bytes(expected),"Actual Steam header was not copied to portable preview");
                }
            });
        }

        for(const auto* family:{"library-config","manual-overrides"}) {
            const std::string kind=family;
            test(("storm-"+kind+"-damaged-edit-reread").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");for(const auto& bad:std::vector<std::string>{"","{","null","[]","true"}){text(f,bad);text(f.wstring()+L".bak","{");auto v=readJson(f,json::object());v["userNote"]="edit";writeJsonWithLocalBackup(f,v);check(readJson(f,json::object())["userNote"]=="edit","Edit did not heal bad config");bool retained=false;for(const auto& e:fs::directory_iterator(g_dataRoot/L"backups"/L"corrupt"))if(bytes(e.path())==bad)retained=true;check(retained,"Bad original not retained");}});
            test(("storm-"+kind+"-future-schema-preserved").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");text(f,R"({"schemaVersion":999,"enabled":true,"items":{"fake.exe":{"steamId":"620","identityStatus":"verified"}},"futureUnknown":7})");const auto original=bytes(f);auto v=readJson(f,json::object());check(!jsonBoolOr(v,"enabled")&&jsonObjectOr(v,"items").empty(),"Future schema supplied authority");v["userNote"]="edit";mustFail([&]{writeJsonWithLocalBackup(f,v);});check(bytes(f)==original,"Future schema overwritten");});
            test(("storm-"+kind+"-oversize-heals").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");const std::string bad((kind=="library-config"?4u:1u)*1024*1024+1,'x');text(f,bad);auto v=readJson(f,json::object());v["userNote"]="edit";writeJsonWithLocalBackup(f,v);check(readJson(f,json::object())["userNote"]=="edit","Large config did not heal");bool retained=false;for(const auto& e:fs::directory_iterator(g_dataRoot/L"backups"/L"corrupt"))if(bytes(e.path())==bad)retained=true;check(retained,"Large original lost");});

            test(("storm-"+kind+"-short-write-flush-failure-and-retry").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");text(f,R"({"schemaVersion":1,"retained":7})");auto v=readJson(f,json::object());const auto original=bytes(f);v["userNote"]="after-retry";for(int mode:{1,2}){configIoTarget=toWide(kind+".json.tmp-");configIoMode=mode;configIoPartial=false;mustFail([&]{writeJsonWithLocalBackup(f,v);});configIoMode=0;configIoTarget.clear();check(bytes(f)==original&&!temporaries(p),"Interrupted write changed main file or leaked stage");}writeJsonWithLocalBackup(f,v);check(readJson(f,json::object())["userNote"]=="after-retry","Write fault retry failed");});
            test(("storm-"+kind+"-activation-failure-and-retry").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");text(f,R"({"schemaVersion":1,"retained":7})");auto v=readJson(f,json::object());const auto original=bytes(f);v["userNote"]="after-retry";hostMoveFault=[&](const auto&,const auto& target)->DWORD{return target==f.wstring()?ERROR_WRITE_FAULT:ERROR_SUCCESS;};mustFail([&]{writeJsonWithLocalBackup(f,v);});check(bytes(f)==original&&!temporaries(p),"Activation fault damaged original or leaked stage");hostMoveFault={};writeJsonWithLocalBackup(f,v);check(readJson(f,json::object())["userNote"]=="after-retry","Activation retry failed");});
            test(("storm-"+kind+"-same-field-conflict-refuses-overwrite").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");text(f,R"({"schemaVersion":1,"userNote":"original"})");auto stale=readJson(f,json::object());std::thread newer([&]{auto v=readJson(f,json::object());v["userNote"]="newer";writeJsonWithLocalBackup(f,v);});newer.join();stale["userNote"]="stale";mustFail([&]{writeJsonWithLocalBackup(f,stale);});check(readJson(f,json::object())["userNote"]=="newer","Same-field conflict overwrote newer edit");});
            test(("storm-"+kind+"-oversize-submission-refused").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");text(f,R"({"schemaVersion":1,"unknown":7})");auto v=readJson(f,json::object());const auto original=bytes(f);v["large"]=std::string((kind=="library-config"?4u:1u)*1024*1024,'z');mustFail([&]{writeJsonWithLocalBackup(f,v);});check(bytes(f)==original,"Oversized submission changed original");});

            test(("storm-"+kind+"-real-access-denied-and-unlock-retry").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");text(f,R"({"schemaVersion":1,"retained":7})");const auto original=bytes(f);withConfigReadDenied(f,[&]{HANDLE h=CreateFileW(f.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);const auto error=GetLastError();if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);check(h==INVALID_HANDLE_VALUE&&error==ERROR_ACCESS_DENIED,"ACL did not deny real read");auto runtime=readJson(f,json::object());check(!jsonBoolOr(runtime,"enabled"),"Denied config supplied settings");runtime["userNote"]="blocked";mustFail([&]{writeJsonWithLocalBackup(f,runtime);});});check(bytes(f)==original,"Denied file treated as absence");auto v=readJson(f,json::object());v["userNote"]="after-unlock";writeJsonWithLocalBackup(f,v);check(readJson(f,json::object())["retained"]==7,"Unlock retry lost original data");});

            test(("storm-"+kind+"-partial-primary-recovery-preserves-fields").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");const std::string field=kind=="library-config"?"roots":"items";json primary={{"schemaVersion",1},{field,nullptr},{"vendorExtension",{{"latest",7}}}};json backup={{"schemaVersion",1},{"backupExtension",9}};backup[field]=kind=="library-config"?json::array({"Z:\\Retained"}):json{{"retained.exe",{{"name","Retained title"}}}};text(f,primary.dump());text(f.wstring()+L".bak",backup.dump());auto v=readJson(f,json::object());check(v[field]==backup[field]&&v["vendorExtension"]["latest"]==7&&v["backupExtension"]==9,"Recovery lost primary/backup valid extension fields");v["userNote"]="healed";writeJsonWithLocalBackup(f,v);check(readJson(f,json::object())["vendorExtension"]["latest"]==7,"Recovery edit lost primary extension");});
            test(("storm-"+kind+"-future-main-never-uses-old-backup").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");text(f,R"({"schemaVersion":999,"unknown":7})");text(f.wstring()+L".bak",R"({"schemaVersion":1,"enabled":true,"items":{"fake.exe":{"steamId":620,"identityStatus":"verified"}}})");const auto main=bytes(f),backup=bytes(f.wstring()+L".bak");auto v=readJson(f,json::object());check(!jsonBoolOr(v,"enabled")&&jsonObjectOr(v,"items").empty(),"Future main borrowed backup authority");check(bytes(f)==main&&bytes(f.wstring()+L".bak")==backup,"Future read altered original files");});
            test(("storm-"+kind+"-stale-snapshots-rebase").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");text(f,R"({"schemaVersion":1,"unknown":{"keep":3}})");auto stale=readJson(f,json::object());std::thread newer([&]{auto v=readJson(f,json::object());v["otherUserEdit"]=42;writeJsonWithLocalBackup(f,v);});newer.join();stale["userNote"]="mine";writeJsonWithLocalBackup(f,stale);auto v=readJson(f,json::object());check(v["otherUserEdit"]==42&&v["userNote"]=="mine"&&v["unknown"]["keep"]==3,"Stale snapshot overwrote newer fields");});
            test(("storm-"+kind+"-unreadable-is-not-absent").c_str(),[=](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/toWide(kind+".json");text(f,R"({"schemaVersion":1})");const auto original=bytes(f);HANDLE h=CreateFileW(f.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(h!=INVALID_HANDLE_VALUE,"Read lock failed");try{auto runtime=readJson(f,json::object());check(!jsonBoolOr(runtime,"enabled")&&jsonObjectOr(runtime,"items").empty(),"Unreadable config supplied authority");mustFail([&]{writeJsonWithLocalBackup(f,{{"userNote","bad"}});});}catch(...){CloseHandle(h);throw;}CloseHandle(h);check(bytes(f)==original,"Unreadable file changed");});
        }
        test("storm-library-nested-types-preserve-siblings",[](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);const auto f=g_dataRoot/L"config"/L"library-config.json";text(f,R"({"roots":["Z:\\Games"],"firstRunDefaultScan":{"status":null,"unknown":7},"manualPrimary":{"game":{"exe":42,"unknown":9}},"vendorExtension":{"x":true}})");auto v=readJson(f,json::object());check(v["manualPrimary"]["game"].value("exe",std::string{}).empty(),"Bad exe escaped normalization");check(v["firstRunDefaultScan"]["unknown"]==7,"Nested unknown lost");v["enabled"]=true;writeJsonWithLocalBackup(f,v);check(readJson(f,json::object())["vendorExtension"]["x"]==true,"Unknown field lost");});

        test("storm-host-real-save-name-command-and-failure-retry",[](const auto& p){
            g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);g_labRoot=p;g_worker=p/L"worker-must-not-run.exe";g_steamTarget=json::object();const auto f=g_dataRoot/L"config"/L"manual-overrides.json",exe=p/L"Games"/L"StormGame.exe";text(exe,"isolated file, never executed");text(f,"{");
            const auto result=executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","Recovered user title"}});const auto key=toUtf8(normalizedPathKey(exe));check(result["manualOverrides"]["items"][key]["name"]=="Recovered user title","Actual name command did not recover damaged config");const auto original=bytes(f);
            hostMoveFault=[&](const auto&,const auto& target)->DWORD{return target==f.wstring()?ERROR_WRITE_FAULT:ERROR_SUCCESS;};mustFail([&]{executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","Retry title"}});});check(bytes(f)==original,"Failed command published its edit");hostMoveFault={};const auto retried=executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","Retry title"}});check(retried["manualOverrides"]["items"][key]["name"]=="Retry title","Actual name command retry failed");
            text(f,R"({"schemaVersion":999,"items":{}})");const auto future=bytes(f);mustFail([&]{executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","blocked"}});});check(bytes(f)==future,"Actual command overwrote future schema");
        });
        test("storm-host-bad-overrides-cannot-forge-identity-or-steam-success",[](const auto& p){g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);g_labRoot=p;g_steamTarget=json::object();const auto exe=p/L"Games"/L"StormGame.exe";text(exe,"isolated file");const auto key=toUtf8(normalizedPathKey(exe));text(g_dataRoot/L"config"/L"manual-overrides.json",json{{"items",{{key,{{"steamId",620},{"identityStatus","verified"},{"steamOwnership","owned"},{"downloadConfirmed",true},{"cover",{{"file",nullptr},{"available",true}}}}}}}}.dump());text(g_dataRoot/L"state"/L"library-scan.json",json{{"games",json::array({{{"gameDirectory",toUtf8(exe.parent_path().wstring())},{"primaryExecutable",toUtf8(exe.wstring())},{"status","ready"}}})}}.dump());const auto result=workspaceSnapshot(false);check(result["steamPlan"]["items"].size()==1,"Missing real local plan consumer row");const auto item=result["steamPlan"]["items"][0];check(item["status"]!="already-in-steam"&&item["status"]!="ready-to-add"&&!jsonBoolOr(item,"downloadConfirmed"),"Bad config forged identity/ownership/download confirmation");writeJsonWithLocalBackup(p/L"consumed-plan.json",result["steamPlan"],false);});

        auto setup=[](const fs::path& p){g_dataRoot=p/L"data";g_labRoot=p;g_worker=p/L"fake-worker.exe";g_steamTarget=json::object();admissionProcesses=0;admissionNetwork=0;admissionShell=0;text(g_worker,"never run");return g_dataRoot;};
        auto manifest=[](const fs::path& data){return json{{"schemaVersion",2},{"dataRoot",toUtf8(data.wstring())},{"dataLayoutVersion",2},{"createdAt",uint64_t{1}},{"unknown",{{"keep",true}}}};};
        auto tree=[](const fs::path& root){json result=json::object();if(fs::exists(root))for(const auto& item:fs::recursive_directory_iterator(root)){const auto key=toUtf8(fs::relative(item.path(),root).wstring());result[key]=item.is_regular_file()?json(bytes(item.path())):json("<directory>");}return result;};
        auto readonlyError=[](const std::function<void()>& action){bool rejected=false;try{action();}catch(const std::exception& e){rejected=std::string(e.what()).find("library-data-root-read-only")!=std::string::npos;}check(rejected,"Write did not reject at shared root admission");};
        test("root-admission-startup-missing-record-reaches-owned-worker",[&](const fs::path& p){
            const auto data=setup(p);g_worker=selfExecutable();
            text(data/L"config"/L"owned.txt","preserve existing library");
            SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",data.c_str());
            const auto accounts=executeWorkspaceCommand("steamAccounts",json::object());
            check(accounts.value("readOnly",false)&&accounts["dataRootReadiness"]["reason"]=="missing-root-record","Missing record did not retain read-only diagnostic");
            readonlyError([&]{runWorkerBounded({L"--cleanup-interrupted-transactions"},10000);});
            initializeWorkerProcessJob();admissionFixtureOnly=false;
            ProcessResult result;
            try { result=runStartupTransactionCleanup(); } catch (...) {admissionFixtureOnly=true;throw;}
            admissionFixtureOnly=true;
            check(result.exitCode==0&&result.output=="owned startup Worker port"&&hostDataRootReadiness().ready,"Startup did not reach bounded initializer Worker");
            check(bytes(data/L"config"/L"owned.txt")=="preserve existing library"&&admissionNetwork==0&&admissionShell==0,"Startup changed unrelated data or used external ports");
            const auto raw=bytes(data/L"data-root.json");admissionFixtureOnly=false;
            try { result=runStartupTransactionCleanup(); } catch (...) {admissionFixtureOnly=true;throw;}
            admissionFixtureOnly=true;
            check(result.exitCode==0&&bytes(data/L"data-root.json")==raw,"Healthy startup rewrote root identity");
        });
        test("root-admission-startup-exception-stays-narrow",[&](const fs::path& p){
            const auto data=setup(p);const auto before=tree(data);bool denied=false;
            try {runProcess(g_worker,{L"--list-steam-accounts"},10000,nullptr,nullptr,HostProcessAdmission::startupRootInitialization);}catch(...){denied=true;}
            check(denied&&admissionProcesses==0&&tree(data)==before,"Startup permission admitted another Worker command");
            g_updateHealthOnly=true;denied=false;try {runStartupTransactionCleanup();}catch(...){denied=true;}g_updateHealthOnly=false;
            check(denied&&admissionProcesses==0&&tree(data)==before,"Health mode initialized an unknown root");
            text(data/L"data-root.json","{");const auto original=tree(data);denied=false;try {runStartupTransactionCleanup();}catch(...){denied=true;}
            check(denied&&admissionProcesses==0&&tree(data)==original,"Startup signed or discarded an invalid record");
        });
        for(const auto* defect:{"missing","empty","truncated","array","type","future","wrong-root","duplicate","oversize"})test((std::string("root-admission-bad-root-")+defect).c_str(),[&,defect](const fs::path& p){const auto data=setup(p),file=data/L"data-root.json",exe=p/L"Game.exe";text(exe,"owned exe");auto doc=manifest(data);const std::string kind=defect;std::string raw;
            if(kind=="empty")raw="";else if(kind=="truncated")raw="{";else if(kind=="array")raw="[]";else if(kind=="duplicate")raw="{\"schemaVersion\":99,\"schemaVersion\":2,\"dataRoot\":"+doc["dataRoot"].dump()+"}";else if(kind=="oversize")raw=std::string((1u<<20)+1,'x');else{if(kind=="type")doc["schemaVersion"]="2";if(kind=="future")doc["schemaVersion"]=99;if(kind=="wrong-root")doc["dataRoot"]=toUtf8((p/L"foreign").wstring());raw=doc.dump();}if(kind!="missing")text(file,raw);
            const auto before=tree(data);for(int n=0;n<4;++n){readonlyError([&]{executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","must not save"}});});check(tree(data)==before,"Rejected name command mutated unknown root");}
            text(file,manifest(data).dump());const auto applied=executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","trusted retry"}});check(readJson(data/L"config"/L"manual-overrides.json",json::object())["items"][toUtf8(normalizedPathKey(exe))]["name"]=="trusted retry","Trusted record repair did not unlock real name consumer");check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Local edit invoked process/network/shell");
        });
        test("root-admission-state-is-side-effect-free-diagnostic",[&](const fs::path& p){const auto data=setup(p);text(data/L"data-root.json","{");const auto before=tree(data);for(const auto* command:{"state","startup","openLibrary","mediaState","mediaDownloadStatus"}){const auto result=executeWorkspaceCommand(command,json::object());check(result.value("readOnly",false)&&result["dataRootReadiness"]["status"]=="unknown","Unknown root did not return readable diagnostic");check(result["config"]==custom_steam_library::config::defaults(true)&&result["manualOverrides"]==custom_steam_library::config::defaults(false),"Readonly view did not use original defaults");}check(tree(data)==before&&admissionProcesses==0&&admissionNetwork==0,"Readonly state started work or repaired files");});
        test("root-admission-direct-image-and-json-sinks-block",[&](const fs::path& p){const auto data=setup(p),source=p/L"cover.png";text(source,"fake isolated image bytes");text(data/L"data-root.json","{");const auto before=tree(data);readonlyError([&]{copyArtworkToIsolation(source,"owned","cover");});readonlyError([&]{writeJsonWithLocalBackup(data/L"state"/L"direct.json",{{"saved",true}},false);});readonlyError([&]{writeHostBytesAtomic(data/L"state"/L"direct.bin","do not save");});check(tree(data)==before,"Direct sink bypass wrote unknown root");text(data/L"data-root.json",manifest(data).dump());const auto copied=copyArtworkToIsolation(source,"owned","cover");check(bytes(copied)==bytes(source),"Trusted root did not unlock actual image copy/readback");});
        for(const auto* command:{"mediaCreate","mediaEdit","mediaImport","mediaSelectVideo","mediaDownloadNow","mediaDownloadCancel","downloadArtworkCandidate","saveArtworkOverride","scan","prepare","commit","backup","identifyOne","searchIdentity","searchArtwork","wakeNetwork","addRoot","removeRoot","setEnabled","setScrapingEnabled","setPrimary","removeGame","setBucket","clearManualSteamId","refreshNativeArtwork","refreshSteamArtwork","deleteFromSteam"})test((std::string("root-admission-command-")+command).c_str(),[&,command](const fs::path& p){const auto data=setup(p);text(data/L"data-root.json","{");const auto before=tree(data);readonlyError([&]{executeWorkspaceCommand(command,json::object());});check(tree(data)==before&&admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Blocked command touched root or external port");});
        test("root-admission-lock-release-and-retry",[&](const fs::path& p){const auto data=setup(p),file=data/L"data-root.json",exe=p/L"Game.exe";text(file,manifest(data).dump());text(exe,"fixture");HANDLE held=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Cannot lock fixture manifest");try{readonlyError([&]{executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","locked"}});});}catch(...){CloseHandle(held);throw;}CloseHandle(held);executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","released"}});check(readJson(data/L"config"/L"manual-overrides.json",json::object())["items"][toUtf8(normalizedPathKey(exe))]["name"]=="released","Released manifest not reread");});
        test("root-admission-pending-migration-remains-readonly",[&](const fs::path& p){const auto data=setup(p),exe=p/L"Game.exe",marker=data/L"state"/L"data-migration.json";text(data/L"data-root.json",manifest(data).dump());text(exe,"fixture");json pending={{"schemaVersion",2},{"targetDataRoot",toUtf8(data.wstring())},{"sources",json::array({toUtf8((p/L"Steam大屏").wstring())})},{"strategy","copy-missing-files-target-wins"},{"phase","copying"},{"startedAt",uint64_t{1}},{"unknownDebt",{{"keep",true}}}};text(marker,pending.dump());const auto raw=bytes(marker);readonlyError([&]{executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","must not save"}});});check(bytes(marker)==raw,"Host discarded or signed pending debt");});

        auto migration=[&](const fs::path& data,const fs::path& lab){return json{{"schemaVersion",2},{"targetDataRoot",toUtf8(data.wstring())},{"strategy","copy-missing-files-target-wins"},{"sources",json::array({toUtf8((lab/L"Steam大屏").wstring())})},{"backupDirectory",nullptr},{"filesCopied",uint64_t{0}},{"filesSkippedBecauseTargetWins",uint64_t{0}},{"bytesCopied",uint64_t{0}},{"symlinksSkipped",uint64_t{0}},{"pathNormalization","legacy-data-root-and-artwork-overrides-rewritten"},{"completedAt",uint64_t{1}},{"annotation",{{"retain",true}}}};};
        for(const auto* defect:{"empty","array","truncated","future","schema-type","target","sources","foreign-source","count","count-negative","completion","phase","duplicate","oversize"})test((std::string("root-admission-migration-")+defect).c_str(),[&,defect](const fs::path& p){const auto data=setup(p),marker=data/L"state"/L"data-migration.json",exe=p/L"Game.exe";text(data/L"data-root.json",manifest(data).dump());text(exe,"fixture");auto debt=migration(data,p);const std::string kind=defect;std::string raw;if(kind=="empty")raw="";else if(kind=="array")raw="[]";else if(kind=="truncated")raw="{";else if(kind=="duplicate")raw="{\"schemaVersion\":99,\"schemaVersion\":2}";else if(kind=="oversize")raw=std::string((4u<<20)+1,'x');else{if(kind=="future")debt["schemaVersion"]=99;if(kind=="schema-type")debt["schemaVersion"]="2";if(kind=="target")debt["targetDataRoot"]=toUtf8((p/L"foreign").wstring());if(kind=="sources")debt["sources"]=true;if(kind=="foreign-source")debt["sources"]=json::array({toUtf8((p/L"foreign").wstring())});if(kind=="count")debt["filesCopied"]="1";if(kind=="count-negative")debt["bytesCopied"]=-1;if(kind=="completion")debt["completedAt"]=false;if(kind=="phase")debt["phase"]="copying";raw=debt.dump();}text(marker,raw);const auto original=tree(data);readonlyError([&]{executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","not allowed"}});});check(tree(data)==original,"Host changed unknown migration debt");text(marker,migration(data,p).dump());executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","trusted debt retry"}});check(readJson(marker,json::object())["annotation"]["retain"]==true,"Host erased valid annotation/debt");});
        for(const auto* command:{"searchArtwork","searchIdentity","wakeNetwork","state"})test((std::string("root-admission-dispatch-bypass-")+command).c_str(),[&,command](const fs::path& p){const auto data=setup(p),exe=p/L"Game.exe";text(data/L"data-root.json","{");text(exe,"fixture");const auto original=tree(data);beginCommand(991,command,{{"executable",toUtf8(exe.wstring())},{"query","isolated"}});joinHostBackgroundTasks();check(tree(data)==original&&admissionProcesses==0&&admissionNetwork==0&&!g_taskRunning,"UI dispatcher bypass admitted task or storage");});
        test("root-admission-read-cannot-autoheal-or-reset-debt",[&](const fs::path& p){const auto data=setup(p),file=data/L"config"/L"library-config.json";text(data/L"data-root.json","{");text(file,"{");auto prior=custom_steam_library::config::defaults(true);prior["roots"]=json::array({toUtf8((p/L"Games").wstring())});text(fs::path(file.wstring()+L".bak"),prior.dump());const auto original=tree(data);const auto result=readJson(file,json::object());check(result==custom_steam_library::config::defaults(true)&&tree(data)==original,"Ordinary read silently repaired unknown root");});
        test("root-admission-valid-media-command-actual-storage-and-retry",[&](const fs::path& p){const auto data=setup(p);text(data/L"data-root.json","{");const json args={{"key","owned-fixture-media"},{"name","owned local entry"}};readonlyError([&]{executeWorkspaceCommand("mediaCreate",args);});check(!fs::exists(data/L"config"/L"game-media.json"),"Unknown root reached shared media writer");text(data/L"data-root.json",manifest(data).dump());const auto result=executeWorkspaceCommand("mediaCreate",args);check(result["key"]=="owned-fixture-media"&&readJson(data/L"config"/L"game-media.json",json::object())["entries"].contains("owned-fixture-media"),"Trusted retry did not reach original shared Store writer/readback");check(admissionProcesses==0&&admissionNetwork==0,"Media register performed external work");});
        test("root-admission-uierror-does-not-write-unknown-root",[&](const fs::path& p){const auto data=setup(p);text(data/L"data-root.json","{");const auto original=tree(data);const auto result=executeWorkspaceCommand("uiError",{{"message","diagnostic"}});beginCommand(992,"uiError",{{"message","diagnostic"}});check(result.value("recorded",true)==false&&tree(data)==original,"UI error bypass wrote root");});
        test("root-admission-profile-uses-owned-scratch-without-changing-root",[&](const fs::path& p){const auto data=setup(p);text(data/L"data-root.json","{");const auto original=tree(data);const auto selected=g_dataRoot;const auto scratch=admissionBrowserPath(g_dataRoot);wchar_t temporary[32768]{};GetTempPathW(32768,temporary);check(!normalizedPathKey(scratch).starts_with(normalizedPathKey(data)+L"\\")&&normalizedPathKey(scratch).starts_with(normalizedPathKey(fs::path(temporary))+L"\\"),"Unknown root browser profile stayed under data root");check(g_dataRoot==selected&&tree(data)==original,"Browser scratch changed selected library or journal");text(data/L"data-root.json",manifest(data).dump());check(admissionBrowserPath(g_dataRoot)==custom_steam_library::webviewUserDataRoot(data),"Trusted browser storage did not reuse original policy");});
        test("root-admission-health-probe-does-not-sign-unknown-root",[&](const fs::path& p){
            const auto priorHealth=g_updateHealthOnly;json observations=json::array();bool all=true;
            for(const auto* label:{"unknown-root-probe","health-views","health-business-denied","health-direct-startup","health-dispatch-startup","health-direct-write","health-root-probe","normal-startup","normal-save"}){
                const std::string mode=label;const auto current=p/toWide(mode);const auto data=setup(current),exe=current/L"Game.exe";text(exe,"inert owned executable");
                g_shuttingDown=false;g_cancelRequested=false;g_updateHealthOnly=mode.starts_with("health-");
                if(mode=="unknown-root-probe")text(data/L"data-root.json","{");
                else{text(data/L"data-root.json",manifest(data).dump());text(data/L"state"/L"library-scan.json",json{{"games",json::array()}}.dump());text(data/L"config"/L"library-config.json",json{{"roots",json::array()},{"automaticScrapingEnabled",false},{"firstRunDefaultScan",{{"status","completed"}}}}.dump());}
                const auto original=tree(data);const json arguments={{"executable",toUtf8(exe.wstring())},{"name","Health must not save"}};
                bool passed=true;std::string error;json detail=json::object();
                try{
                    if(mode=="unknown-root-probe"){std::string failure;check(!probeUpdateHealthDataRoot(failure)&&failure.find("library-data-root-read-only")!=std::string::npos&&tree(data)==original,"Health probe wrote or accepted unknown root");}
                    else if(mode=="health-views"){
                        for(const auto* command:{"state","startup","openLibrary","mediaState","mediaDownloadStatus","steamAccounts","steamCommitPreflight","uiError"}){
                            const auto result=executeWorkspaceCommand(command,json::object());detail[command]=result;
                            check(result.value("healthOnly",false)&&result.value("readOnly",false),"Health view entered ordinary business path");
                        }
                    }else if(mode=="health-business-denied"){
                        for(const auto* command:{"scan","prepare","commit","backup","identifyOne","searchIdentity","searchArtwork","wakeNetwork","addRoot","removeRoot","setEnabled","setScrapingEnabled","setPrimary","removeGame","setBucket","clearManualSteamId","refreshNativeArtwork","refreshSteamArtwork","deleteFromSteam","saveEditor","saveManualName","mediaCreate","mediaEdit","mediaImport","mediaSelectVideo","mediaDownloadNow","mediaDownloadCancel","downloadArtworkCandidate","saveArtworkOverride","pickExecutable","pickDirectory","keyboard","activateWorkspace","future-unknown-command"}){
                            bool denied=false;try{admitHostDataRootCommand(command);}catch(const std::exception& e){denied=std::string(e.what()).find("library-update-health-read-only")!=std::string::npos;}detail[command]=denied;check(denied,"Health mode admitted a business/control entry");
                        }
                    }else if(mode=="health-direct-startup"){
                        const auto result=executeWorkspaceCommand("startup",json::object());detail["result"]=result;check(result.value("healthOnly",false)&&admissionProcesses==0,"Health startup launched the original Worker path");
                    }else if(mode=="health-dispatch-startup"){
                        beginCommand(996,"startup",json::object());joinHostBackgroundTasks();check(admissionProcesses==0&&!g_taskRunning,"Health UI dispatcher queued business startup");
                    }else if(mode=="health-direct-write"){
                        bool denied=false;try{executeWorkspaceCommand("saveManualName",arguments);}catch(const std::exception& e){denied=std::string(e.what()).find("library-update-health-read-only")!=std::string::npos;}check(denied,"Health mode saved ordinary configuration");
                    }else if(mode=="health-root-probe"){
                        std::string failure;check(probeUpdateHealthDataRoot(failure)&&failure.empty(),"Dedicated writable-root health probe stopped working");
                    }else if(mode=="normal-startup"){
                        try{executeWorkspaceCommand("startup",json::object());}catch(const std::exception& e){detail["inertWorkerError"]=e.what();}check(admissionProcesses>0,"Ordinary startup lost the original Worker/account refresh path");
                    }else if(mode=="normal-save"){
                        executeWorkspaceCommand("saveManualName",arguments);const auto saved=readJson(data/L"config"/L"manual-overrides.json",json::object());check(saved["items"][toUtf8(normalizedPathKey(exe))]["name"]=="Health must not save","Ordinary local save was blocked by health policy");
                    }
                    joinHostBackgroundTasks();
                    if(mode.starts_with("health-"))check(tree(data)==original&&admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Health mode changed business data or requested external work");
                }catch(const std::exception& e){passed=false;error=e.what();}
                joinHostBackgroundTasks();
                observations.push_back({{"mode",mode},{"passed",passed},{"error",error},{"processAttempts",admissionProcesses.load()},{"networkAttempts",admissionNetwork.load()},{"shellAttempts",admissionShell.load()},{"dataUnchanged",tree(data)==original},{"detail",detail}});all=all&&passed;g_updateHealthOnly=priorHealth;
            }
            g_updateHealthOnly=priorHealth;writeJsonWithLocalBackup(p/L"health-observations.json",observations,false);check(all,"Health read-only business boundary failed");
        });
        test("root-admission-ACL-denied-and-release",[&](const fs::path& p){const auto data=setup(p),file=data/L"data-root.json",exe=p/L"Game.exe";text(file,manifest(data).dump());text(exe,"fixture");withConfigReadDenied(file,[&]{readonlyError([&]{executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","denied"}});});});executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","released ACL"}});check(fs::is_regular_file(data/L"config"/L"manual-overrides.json"),"Released ACL did not allow retry");});
        test("root-admission-manifest-changes-before-publication",[&](const fs::path& p){const auto data=setup(p),file=data/L"data-root.json",exe=p/L"Game.exe",target=data/L"config"/L"manual-overrides.json";text(file,manifest(data).dump());text(exe,"fixture");auto foreign=manifest(data);foreign["schemaVersion"]=99;bool changed=false;hostMoveFault=[&](const auto&,const auto& to)->DWORD{if(to==target.wstring()&&!changed){changed=true;text(file,foreign.dump());return ERROR_SHARING_VIOLATION;}return 0;};readonlyError([&]{executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","changed"}});});hostMoveFault={};check(changed&&!fs::exists(target)&&bytes(file)==foreign.dump(),"Late root change was overwritten/ignored at direct sink");text(file,manifest(data).dump());executeWorkspaceCommand("saveManualName",{{"executable",toUtf8(exe.wstring())},{"name","real retry"}});check(fs::is_regular_file(target),"Late root repair not retryable");});


        auto identitySetup=[](const fs::path& p){g_dataRoot=p/L"data";g_labRoot=p;trustedAdmissionFixtureRoot(g_dataRoot);admissionProcesses=0;admissionNetwork=0;admissionShell=0;};
        auto official=[](){return json{{"appId",620},{"storefrontAppId",620},{"formalName","Portal 2"},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"},{"source","steam-official"}};};
        auto observation=[&](const fs::path& p,const fs::path& exe){const auto source=p/L"job"/L"manifest.json";text(source,json{{"exe",toUtf8(exe.wstring())},{"match",official()},{"metadata",{{"observed",true}}}}.dump());return source;};
        auto hostCache=[](const fs::path& exe){return g_dataRoot/L"cache"/L"identity"/toWide(safeArtworkSegment(toUtf8(normalizedPathKey(exe)))+".json");};
        auto entry=[&](const fs::path& exe){return json{{"schemaVersion",1},{"executable",toUtf8(exe.wstring())},{"updatedAt",unixTimeMsHost()},{"match",official()},{"steamAppId",620},{"metadata",json::object()},{"customNote",{{"keep",true}}}};};
        auto rejectIdentity=[](const std::function<bool()>& action){bool rejected=false;try{rejected=!action();}catch(const std::exception&){rejected=true;}check(rejected,"Identity caller ACKed unknown/failed operation");};
        auto persist=[&](const fs::path& source){return identityCallerPersist([&]{return persistResolvedIdentityCache(source);});};
        auto readHostIdentity=[](){json result=json::object();attachResolvedIdentityCache(result);return result["resolvedIdentities"];};
        test("identity-caller-official-cache-refreshes-old-card",[&](const fs::path& p){
            wchar_t supplied[32768]{};const auto suppliedCount=GetEnvironmentVariableW(L"YMCC_HOST_IDENTITY_OBSERVATION",supplied,32768);
            auto observed=suppliedCount&&suppliedCount<32768?readJson(fs::path(supplied),json::object()):json{{"match",official()},{"metadata",json::object()}};
            const auto expectedId=jsonIntegerOr(observed["match"],"appId",0);const auto expectedName=jsonStringOr(observed["match"],"formalName");
            check(expectedId>0&&custom_steam_library::verifiedSteamIdentityMatch(observed["match"]),"Need actual officially verified worker observation");
            const std::set<std::string> knownModes={"pending-not-selected","pending-selected","manual-id-cleared","manual-other-id","already-owned","existing-official","changed-primary","missing-artwork","legacy-selected","native-flag","worker-initial-unselected","worker-initial-selected","worker-initial-missing-artwork","worker-initial-unverified","artwork-complete-selected","artwork-complete-unselected","artwork-missing","artwork-user-deleted","artwork-unverified","artwork-complete-no-cache","artwork-primary-changed","artwork-already-owned","artwork-id-cleared","artwork-other-id"};
            std::set<std::string> requestedModes;wchar_t selectedModes[32768]{};SetLastError(ERROR_SUCCESS);
            const auto modeCount=GetEnvironmentVariableW(L"YMCC_HOST_IDENTITY_PROJECTION_MODES",selectedModes,32768);
            check(modeCount<32768,"Projection mode selection is too long");
            if(modeCount||GetLastError()!=ERROR_ENVVAR_NOT_FOUND){
                const auto selection=toUtf8(selectedModes);size_t start=0;
                do{const auto end=selection.find(',',start);const auto mode=selection.substr(start,end==std::string::npos?end:end-start);check(knownModes.contains(mode)&&requestedModes.insert(mode).second,"Unknown, empty or duplicate identity projection mode");if(end==std::string::npos)break;start=end+1;}while(true);
            }
            json rows=json::array();bool all=true;
            for(const auto* label:{"pending-not-selected","pending-selected","manual-id-cleared","manual-other-id","already-owned","existing-official","changed-primary","missing-artwork","legacy-selected","native-flag","worker-initial-unselected","worker-initial-selected","worker-initial-missing-artwork","worker-initial-unverified","artwork-complete-selected","artwork-complete-unselected","artwork-missing","artwork-user-deleted","artwork-unverified","artwork-complete-no-cache","artwork-primary-changed","artwork-already-owned","artwork-id-cleared","artwork-other-id"}){
                if(!requestedModes.empty()&&!requestedModes.contains(label))continue;
                const std::string mode=label;bool good=true;std::string failure;
                const auto fixture=snapshotCacheFixture(p/toWide(mode),1);const auto exe=fs::path(toWide(jsonStringOr(fixture.cache["items"][0],"primaryExecutable")));const auto key=toUtf8(normalizedPathKey(exe));
                admissionProcesses=0;admissionNetwork=0;admissionShell=0;
                auto plan=fixture.cache;auto& item=plan["items"][0];item["status"]="waiting-steam-verification";item["formalName"]="Wrong related content";item["igdbId"]=396025;item["identityStatus"]="metadata-verified";item["steamVerificationStatus"]="not-available";item["selectedForAdd"]=mode=="pending-selected";
                plan["summary"]={{"needsIdentity",1},{"needsSteamVerification",1},{"alreadyInSteam",0},{"readyToAdd",0},{"readyNotSelected",0},{"needsMinimumArtwork",0}};
                if(mode=="already-owned"){item["status"]="already-in-steam";item["shortAppId"]="2147483648";item["longAppId"]="9223372036854775808";item["steamShortcutName"]="Retained shortcut label";}
                if(mode=="existing-official"){item["status"]="ready-to-add";item["steamStoreAppId"]=570;item["storefrontAppId"]=570;item["identityStatus"]="verified";item["steamVerificationStatus"]="steam-verified";item["formalName"]="Existing official plan";}
                if(mode.starts_with("worker-initial-")){
                    item["status"]="needs-identity-and-artwork";item["suggestedName"]="Initial worker title";
                    item["selectedForAdd"]=mode=="worker-initial-selected";item.erase("igdbId");item.erase("formalName");item.erase("identityStatus");item.erase("steamVerificationStatus");item.erase("artworkManifest");
                    plan["summary"]["needsSteamVerification"]=0;
                }
                if(mode.starts_with("artwork-")){
                    for(const auto* field:{"formalName","displayName","identityStatus","steamVerificationStatus","metadataVerificationStatus","resolverVersion","identityKind","contentRelation","baseGameAppId","matchedContentAppId","igdbId"})if(observed["match"].contains(field))item[field]=observed["match"][field];
                    item["steamStoreAppId"]=expectedId;item["storefrontAppId"]=expectedId;item.erase("primaryProvider");item.erase("appId");
                    item["status"]="needs-minimum-artwork";item["artwork"]={{"minimumComplete",false},{"tall",false},{"hero",false}};item["artworkFallbackComplete"]=false;
                    item["selectedForAdd"]=mode!="artwork-complete-unselected";
                    plan["summary"]={{"needsIdentity",0},{"needsSteamVerification",0},{"alreadyInSteam",0},{"readyToAdd",0},{"readyNotSelected",0},{"needsMinimumArtwork",1}};
                    if(mode=="artwork-unverified"){item["identityStatus"]="candidate";item["steamVerificationStatus"]="network-pending";}
                    if(mode=="artwork-already-owned"){item["status"]="already-in-steam";item["shortAppId"]="2147483648";item["longAppId"]="9223372036854775808";plan["summary"]["alreadyInSteam"]=1;plan["summary"]["needsMinimumArtwork"]=0;}
                }
                if(mode=="legacy-selected")item.erase("selectedForAdd");
                if(mode=="native-flag")item["steamNative"]=true;
                writeJsonWithLocalBackup(workspacePlanPath(),plan,false);
                auto manual=readJson(g_dataRoot/L"config"/L"manual-overrides.json",json::object());manual["items"][key].erase("name");manual["items"][key]["futureNote"]="keep";
                if(mode=="manual-id-cleared"||mode=="artwork-id-cleared")manual["items"][key]["idCleared"]=true;
                if(mode=="manual-other-id"||mode=="artwork-other-id")manual["items"][key]["steamId"]=777;
                if(mode=="artwork-user-deleted")manual["items"][key]["artworkProtection"]["cover"]="deleted";
                writeJsonWithLocalBackup(g_dataRoot/L"config"/L"manual-overrides.json",manual,false);
                const auto originalManifest=fs::path(toWide(jsonStringOr(fixture.cache["items"][0],"artworkManifest")));auto old=readJson(originalManifest,json::object());old["match"]={{"igdbId",396025},{"formalName","Wrong related content"},{"primaryProvider","playnite-igdb"},{"identityStatus","metadata-verified"},{"metadataVerificationStatus","metadata-verified"},{"steamVerificationStatus","not-available"},{"resolverVersion","steam-official-fuzzy-global-v4"}};if(mode.starts_with("artwork-"))old["match"]=observed["match"];writeJsonWithLocalBackup(originalManifest,old,false);
                if(mode=="changed-primary"||mode=="artwork-primary-changed"){auto scan=readJson(g_dataRoot/L"state"/L"library-scan.json",json::object());const auto other=exe.parent_path()/L"Other.exe";text(other,"owned inert alternate");scan["games"][0]["primaryExecutable"]=toUtf8(other.wstring());writeJsonWithLocalBackup(g_dataRoot/L"state"/L"library-scan.json",scan,false);}
                if(mode=="missing-artwork"||mode=="worker-initial-missing-artwork"||mode=="artwork-missing")fs::remove(fixture.png);
                const auto manualBefore=bytes(g_dataRoot/L"config"/L"manual-overrides.json"),planBefore=bytes(workspacePlanPath());
                auto incoming=observed;incoming["exe"]=toUtf8(exe.wstring());if(mode=="worker-initial-unverified")incoming["match"]={{"igdbId",77},{"formalName","Only metadata"},{"primaryProvider","playnite-igdb"},{"identityStatus","metadata-verified"},{"steamVerificationStatus","not-available"}};const auto output=p/toWide(mode)/L"worker-output";const auto source=output/L"owned"/L"manifest.json";text(source,incoming.dump());
                const bool persisted=persistResolvedIdentityFromOutput(output,toUtf8(exe.wstring()));if(mode=="artwork-complete-no-cache")fs::remove(hostCache(exe));RefreshNoticeWindow noticeWindow;acceptWorkspaceRefreshProtocol({{"advisoryRefreshProtocol",1}});publishArtworkStageRefresh(nullptr,toUtf8(exe.wstring()));const auto notice=noticeWindow.take();const auto snapshot=workspaceSnapshot(false);const auto actual=snapshot["steamPlan"]["items"][0];
                try{
                    check(persisted,"Host rejected real official output");check(jsonStringOr(notice,"event")=="library-plan-refreshed"&&jsonBoolOr(notice,"refreshRequested",false)&&!notice.contains("snapshot"),"Expected negotiated advisory followed by current state");if(mode!="manual-id-cleared"&&mode!="manual-other-id"&&mode!="artwork-id-cleared"&&mode!="artwork-other-id"&&mode!="artwork-complete-no-cache")check(snapshot["resolvedIdentities"].contains(key),"Host did not reload new canonical cache");
                    if(mode.starts_with("artwork-")){
                        const bool complete=mode=="artwork-complete-selected"||mode=="artwork-complete-unselected"||mode=="artwork-complete-no-cache";
                        const bool manualChanged=mode=="artwork-id-cleared"||mode=="artwork-other-id";
                        const bool owned=mode=="artwork-already-owned";
                        const auto wanted=complete?(mode=="artwork-complete-unselected"?"ready-not-selected":"ready-to-add"):manualChanged?"waiting-steam-verification":owned?"already-in-steam":"needs-minimum-artwork";
                        check(jsonStringOr(actual,"status")==wanted,"Fresh artwork preview left cached artwork status stale or bypassed a control");
                        if(!manualChanged)check(jsonIntegerOr(actual,"steamStoreAppId",0)==expectedId&&jsonStringOr(actual,"formalName")==expectedName,"Artwork-only refresh rewrote established identity");
                        else check(jsonIntegerOr(actual,"steamStoreAppId",0)!=expectedId,"Artwork-only refresh restored a cleared/manual-conflicting identity");
                        const auto totals=snapshot["steamPlan"]["summary"];
                        check(jsonIntegerOr(totals,"needsMinimumArtwork",0)==(complete||manualChanged||owned?0:1)&&jsonIntegerOr(totals,"needsIdentity",0)==(manualChanged?1:0),"Artwork-only refresh changed unrelated counts or left a stale pending count");
                        if(complete)check(jsonIntegerOr(totals,mode=="artwork-complete-unselected"?"readyNotSelected":"readyToAdd",0)==1,"Completed artwork did not enter the selected/unselected counter");
                        if(owned)check(actual["shortAppId"]==item["shortAppId"]&&actual["longAppId"]==item["longAppId"],"Artwork-only refresh rewrote Steam membership IDs");
                        if(mode=="artwork-complete-no-cache")check(snapshot["resolvedIdentities"].empty(),"No-cache control accidentally gained cache proof");
                    }else if(mode.starts_with("pending-")||mode=="missing-artwork"||mode=="legacy-selected"||(mode.starts_with("worker-initial-")&&mode!="worker-initial-unverified")){
                        check(jsonIntegerOr(actual,"steamStoreAppId",0)==expectedId&&jsonStringOr(actual,"formalName")==expectedName,"Fresh state still returns the old pending card after official cache publication");
                        check(jsonStringOr(actual,"status")== ((mode=="missing-artwork"||mode=="worker-initial-missing-artwork")?"needs-minimum-artwork":(mode=="pending-selected"||mode=="legacy-selected"||mode=="worker-initial-selected")?"ready-to-add":"ready-not-selected"),"Identity refresh lost explicit selection or artwork readiness");
                        check(jsonIntegerOr(actual,"igdbId",0)!=396025,"New official identity borrowed old related-content ID");
                        check(jsonIntegerOr(snapshot["steamPlan"]["summary"],(mode=="missing-artwork"||mode=="worker-initial-missing-artwork")?"needsMinimumArtwork":(mode=="pending-selected"||mode=="legacy-selected"||mode=="worker-initial-selected")?"readyToAdd":"readyNotSelected",0)==1&&jsonIntegerOr(snapshot["steamPlan"]["summary"],"needsIdentity",-1)==0,"Pending counter did not advance with the card");
                    }else if(mode=="already-owned")check(actual["status"]==item["status"]&&actual["shortAppId"]==item["shortAppId"]&&actual["longAppId"]==item["longAppId"],"Identity refresh changed shortcut membership");
                    else if(mode=="existing-official")check(jsonIntegerOr(actual,"steamStoreAppId",0)==570,"A different established official plan was overwritten");
                    else check(jsonIntegerOr(actual,"steamStoreAppId",0)!=expectedId,"Cleared/different manual selection or old primary acquired cache authority");
                    check(bytes(g_dataRoot/L"config"/L"manual-overrides.json")==manualBefore&&bytes(workspacePlanPath())==planBefore,"Read-only projection rewrote user config or Steam plan");
                    check(admissionProcesses==0&&admissionNetwork==0&&admissionShell==0,"Current-state read requested external work");
                }catch(const std::exception& error){good=false;failure=error.what();}
                rows.push_back({{"mode",mode},{"passed",good},{"error",failure},{"snapshot",snapshot},{"sourceManifest",toUtf8(source.wstring())}});all=all&&good;
            }
            check(rows.size()==(requestedModes.empty()?knownModes.size():requestedModes.size()),"Projection selection did not execute the exact requested modes");
            writeJsonWithLocalBackup(p/L"projection-observations.json",rows,false);check(all,"Cached card projection boundary failed");
        });
        test("identity-caller-verified-roundtrip-through-header-consumer",[&](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned fixture");const auto source=observation(p,exe);check(persist(source),"Legal observation rejected");const auto cache=hostCache(exe);auto snap=custom_steam_library::identity_cache::readSnapshot(cache);check(custom_steam_library::identity_cache::recordValid(snap,toUtf8(exe.wstring()),unixTimeMsHost()),"Host did not publish full header-valid record");check(ymcc::media::Store(g_dataRoot).identity(toUtf8(exe.wstring())).value("verified",false),"Shared consumer cannot read Host record");check(readHostIdentity().size()==1,"Host cannot consume legal record");});
        for(const auto* defect:{"empty","truncated","array","duplicate","metadata-type","timestamp-type","timestamp-future","id-mismatch","schema-type","igdb-root-type","igdb-match-type","oversize"})test((std::string("identity-caller-corrupt-preserved-repair-")+defect).c_str(),[&,defect](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned");const auto source=observation(p,exe),cache=hostCache(exe);auto doc=entry(exe);const std::string kind=defect;std::string raw;if(kind=="empty")raw="";else if(kind=="truncated")raw="{";else if(kind=="array")raw="[]";else if(kind=="duplicate")raw="{\"schemaVersion\":9,\"schemaVersion\":1,\"executable\":"+doc["executable"].dump()+",\"match\":"+official().dump()+"}";else if(kind=="oversize")raw=std::string((16u<<20)+1,'x');else{if(kind=="metadata-type")doc["metadata"]=false;if(kind=="timestamp-type")doc["updatedAt"]="now";if(kind=="timestamp-future")doc["updatedAt"]=unixTimeMsHost()+600000;if(kind=="id-mismatch")doc["steamAppId"]=570;if(kind=="schema-type")doc["schemaVersion"]="1";if(kind=="igdb-root-type")doc["igdbId"]="77";if(kind=="igdb-match-type")doc["match"]["igdbId"]=true;raw=doc.dump();}text(cache,raw);check(readHostIdentity().empty(),"Predicate-only match leaked invalid whole record");check(persist(source),"New valid observation could not repair corrupted cache");bool preserved=false;for(const auto& file:fs::directory_iterator(cache.parent_path()))if(file.path().filename().wstring().find(L".identity-corrupt-")!=std::wstring::npos&&bytes(file.path())==raw)preserved=true;check(preserved,"Corrupt canonical bytes not preserved through header commit");check(custom_steam_library::identity_cache::recordValid(custom_steam_library::identity_cache::readSnapshot(cache),toUtf8(exe.wstring()),unixTimeMsHost()),"Repaired cache unreadable by header");});
        for(const auto* field:{"schemaVersion","schema"})test((std::string("identity-caller-future-preserved-")+field).c_str(),[&,field](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned");const auto source=observation(p,exe),cache=hostCache(exe);auto doc=entry(exe);if(std::string(field)=="schemaVersion")doc[field]=99;else doc[field]="future.identity.v9";text(cache,doc.dump());const auto original=bytes(cache);check(readHostIdentity().empty(),"Future root supplied verified identity");rejectIdentity([&]{return persist(source);});check(bytes(cache)==original,"Future cache overwritten by matching predicate");});
        test("identity-caller-valid-annotation-survives-new-observation",[&](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned");const auto source=observation(p,exe),cache=hostCache(exe);text(cache,entry(exe).dump());check(persist(source)&&readJson(cache,json::object())["customNote"]["keep"]==true,"Valid unknown root fields were dropped");});
        test("identity-caller-lock-read-failure-unknown-and-retry",[&](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned");const auto source=observation(p,exe),cache=hostCache(exe);text(cache,entry(exe).dump());const auto original=bytes(cache);HANDLE held=CreateFileW(custom_steam_library::ioPath(cache).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Cannot lock owned cache");try{check(readHostIdentity().empty(),"Read failure supplied identity");rejectIdentity([&]{return persist(source);});}catch(...){CloseHandle(held);throw;}CloseHandle(held);check(bytes(cache)==original&&persist(source),"Locked cache lost original or did not recover");});
        test("identity-caller-foreign-path-binding-not-overwritten",[&](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe",foreign=p/L"Other.exe";text(exe,"owned");text(foreign,"owned other");const auto source=observation(p,exe),cache=hostCache(exe);text(cache,entry(foreign).dump());const auto original=bytes(cache);rejectIdentity([&]{return persist(source);});check(bytes(cache)==original,"Valid different binding replaced");});
        test("identity-caller-cache-does-not-follow-manifest-fallback",[&](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned");const auto source=observation(p,exe),cache=hostCache(exe);text(cache,json{{"executable",toUtf8(exe.wstring())},{"manifestPath",toUtf8(source.wstring())}}.dump());const auto original=bytes(cache);check(readHostIdentity().empty()&&bytes(cache)==original,"Missing match borrowed old manifest authority");});
        for(const auto* defect:{"truncated","metadata-type","exe-type","relative-exe"})test((std::string("identity-caller-bad-observation-")+defect).c_str(),[&,defect](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned");const auto source=observation(p,exe),cache=hostCache(exe);if(std::string(defect)=="truncated")text(source,"{");else{auto doc=readJson(source,json::object());if(std::string(defect)=="metadata-type")doc["metadata"]=false;if(std::string(defect)=="exe-type")doc["exe"]=7;if(std::string(defect)=="relative-exe")doc["exe"]="relative.exe";text(source,doc.dump());}rejectIdentity([&]{return persist(source);});check(!fs::exists(cache),"Invalid observation published canonical cache");});
        test("identity-caller-stage-write-failure-does-not-ACK",[&](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned");const auto source=observation(p,exe),cache=hostCache(exe);text(cache,entry(exe).dump());const auto original=bytes(cache);configIoTarget=L".identity-next-";configIoMode=1;rejectIdentity([&]{return persist(source);});configIoMode=0;check(bytes(cache)==original&&persist(source),"Partial stage write damaged original or blocked retry");});
        test("identity-caller-publish-ACK-readback-is-verified",[&](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned");const auto source=observation(p,exe),cache=hostCache(exe);bool fired=false;identityAfterMove=[&](const auto& from,const auto& to){if(!fired&&from.find(L".identity-next-")!=std::wstring::npos&&to.ends_with(cache.filename().wstring())){fired=true;text(cache,"{");}};rejectIdentity([&]{return persist(source);});identityAfterMove={};check(fired,"Actual identity publication port not reached");check(readHostIdentity().empty(),"Bad readback supplied verified identity");check(persist(source),"After readback fault release valid observation could not recover");});
        for(const auto* kind:{"observation","cache"})test((std::string("identity-caller-concurrent-")+kind).c_str(),[&,kind](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned");const auto source=observation(p,exe),cache=hostCache(exe);text(cache,entry(exe).dump());bool fired=false;const std::string change=kind;auto foreign=entry(exe);foreign["schemaVersion"]=99;identityAfterFlush=[&](HANDLE handle){wchar_t name[32768]{};GetFinalPathNameByHandleW(handle,name,32768,0);if(!fired&&std::wstring(name).find(L".identity-next-")!=std::wstring::npos){fired=true;if(change=="cache")text(cache,foreign.dump());else text(source,"{");}};rejectIdentity([&]{return persist(source);});identityAfterFlush={};check(fired,"Identity stage flush callback not reached");if(change=="cache")check(bytes(cache)==foreign.dump(),"Concurrent future cache overwritten");else check(readJson(cache,json::object())["customNote"]["keep"]==true,"Changed observation replaced old cache");});
        test("identity-caller-legal-igdb-candidate-remains-unverified",[&](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned");const auto source=p/L"job"/L"manifest.json";text(source,json{{"exe",toUtf8(exe.wstring())},{"match",{{"igdbId",77},{"formalName","Candidate"},{"primaryProvider","igdb"}}},{"metadata",json::object()}}.dump());check(persist(source),"Legal non-Steam candidate lost");auto host=readHostIdentity();check(host.size()==1&&!custom_steam_library::verifiedSteamIdentityMatch(host.begin().value()),"Candidate promoted or dropped");check(ymcc::media::Store(g_dataRoot).identity(toUtf8(exe.wstring())).empty(),"Unverified candidate became shared verified identity");});
        test("identity-caller-legacy-no-schema-compatible",[&](const fs::path& p){identitySetup(p);const auto exe=p/L"Game.exe";text(exe,"owned");const auto source=observation(p,exe),cache=hostCache(exe);auto doc=entry(exe);doc.erase("schemaVersion");text(cache,doc.dump());check(readHostIdentity().size()==1&&persist(source),"Valid schema-less legacy identity lost");check(readJson(cache,json::object())["customNote"]["keep"]==true,"Legacy unknown annotation lost");});
        check(!identityCaller||!cases.empty(),"No identity caller cases selected");

        check(!admission||!cases.empty(),"No admission cases selected");

        check(queueCase.empty()||(cases.size()==1&&jsonStringOr(cases[0],"name")==queueCase),"Default queue selection did not run exactly one original case");
        size_t failures=0;for(const auto& t:cases)if(!t["passed"].get<bool>())++failures;
        json report={{"allPassed",failures==0},{"caseCount",cases.size()},{"failedCount",failures},{"cases",cases},{"realSteamFilesModified",false},{"steamStoppedOrLaunched",false}};
        g_labRoot=root;g_dataRoot=root/L"reporting-only-data";hostMoveFault={};writeJsonWithLocalBackup(root/L"summary.json",report,false);std::cout<<report.dump(2)<<"\n";return failures?1:0;
    } catch(const std::exception& e){g_shuttingDown=true;requestTaskCancellation();closeWorkerProcessJob();joinHostBackgroundTasks();std::cerr<<e.what()<<"\n";return 1;}
}
