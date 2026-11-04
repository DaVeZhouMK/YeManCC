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
static std::atomic<int> admissionProcesses{0},admissionNetwork{0},admissionShell{0};
static BOOL WINAPI admissionCreateProcess(LPCWSTR app,LPWSTR cmd,LPSECURITY_ATTRIBUTES pa,LPSECURITY_ATTRIBUTES ta,BOOL inherit,DWORD flags,LPVOID env,LPCWSTR dir,LPSTARTUPINFOW start,LPPROCESS_INFORMATION info) {
    if(admissionFixtureOnly){++admissionProcesses;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
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
#define wWinMain unusedHostMain
#include "../native/custom-steam-library/workspace_host.cpp"
#undef wWinMain
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
static void mustFail(const std::function<void()>& action) { bool failed=false; try { action(); } catch (const std::exception&) { failed=true; } check(failed,"Expected a controlled error"); }
static std::string bytes(const fs::path& path) { std::ifstream in(custom_steam_library::ioPath(path),std::ios::binary); return {std::istreambuf_iterator<char>(in),{}}; }
static void text(const fs::path& path, const std::string& value) { fs::create_directories(custom_steam_library::ioPath(path.parent_path())); std::ofstream output(custom_steam_library::ioPath(path),std::ios::binary);output<<value;output.flush();if(!output)throw std::runtime_error("Cannot write owned fixture bytes"); }
static void trustedAdmissionFixtureRoot(const fs::path& root) {
    text(root/L"data-root.json",json{{"schemaVersion",2},{"dataRoot",toUtf8(root.wstring())},{"createdAt",uint64_t{1}}}.dump());
}
static size_t temporaries(const fs::path& root) { size_t count=0; for(const auto& e:fs::recursive_directory_iterator(root)) if(e.path().filename().wstring().find(L".tmp-")!=std::wstring::npos) ++count; return count; }
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

// End-to-end queue worker: no network, Steam discovery or user-data access.
// Every subprocess is this fixture EXE and every path must remain in Build.
static int queueFixtureWorker(int argc, wchar_t** argv) {
    wchar_t value[32768]{};
    const DWORD length=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",value,32768);
    if(!length || length>=32768) return 98;
    const auto data=fs::absolute(value);
    const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
    check(normalizedPathKey(data).starts_with(normalizedPathKey(build)+L"\\"),"Queue fixture escaped Build");
    g_dataRoot=data;
    auto has=[&](const wchar_t* flag){for(int i=1;i<argc;++i)if(std::wstring(argv[i])==flag)return true;return false;};
    auto argument=[&](const wchar_t* flag){for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==flag)return fs::path(argv[i+1]);return fs::path{};};
    auto output=argument(L"--output");
    if(has(L"--list-steam-accounts")) {
        check(normalizedPathKey(output).starts_with(normalizedPathKey(data)+L"\\"),"Accounts output escaped fixture");
        writeJsonWithLocalBackup(output,{{"accounts",json::array()},{"steamStatus","fixture"}},false);return 0;
    }
    if(has(L"--plan-add-library-to-steam")) {
        check(normalizedPathKey(output).starts_with(normalizedPathKey(data)+L"\\"),"Plan output escaped fixture");
        json snapshot={{"library",readJson(data/L"state"/L"library-scan.json",json::object())},
            {"manualOverrides",readJson(data/L"config"/L"manual-overrides.json",json::object())}};
        auto plan=buildLocalSteamPlanSnapshot(snapshot,readJson(data/L"state"/L"fixture-plan.json",json::object()));
        writeJsonWithLocalBackup(output,plan,false);return 0;
    }
    check(argc>=3,"Missing fixture EXE/output");const auto exe=fs::absolute(argv[1]);const auto out=fs::absolute(argv[2]);
    check(normalizedPathKey(exe).starts_with(normalizedPathKey(data.parent_path())+L"\\")&&
        normalizedPathKey(out).starts_with(normalizedPathKey(data)+L"\\"),"Worker path escaped fixture");
    if(has(L"--metadata-only")) {
        writeJsonWithLocalBackup(out/L"rounds-failed.json",{{"executable",toUtf8(exe.wstring())},
            {"failure",{{"kind","unrecognized"},{"retryWhenNetworkReturns",false}}}},false);
        std::cout<<"FAILURE_KIND: unrecognized\n";return 1;
    }
    check(has(L"--igdb-artwork-only"),"No-ID queue used a Steam artwork/identity worker");
    const auto artwork=data/L"artwork"/L"old-game";json slots=json::array();
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
static void runQueueFixture(const fs::path& root,bool manual,bool cooldown) {
    const auto savedData=g_dataRoot,savedLab=g_labRoot,savedWorker=g_worker;
    const auto savedTarget=steamTargetSnapshot();
    wchar_t prior[32768]{};const auto priorLength=GetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",prior,32768);
    auto restore=[&]{
        g_shuttingDown=true;requestTaskCancellation();joinHostBackgroundTasks();closeWorkerProcessJob();
        g_dataRoot=savedData;g_labRoot=savedLab;g_worker=savedWorker;g_steamTarget=savedTarget;
        SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",priorLength&&priorLength<32768?prior:nullptr);
        g_shuttingDown=false;g_cancelRequested=false;
    };
    try {
        g_dataRoot=root/L"data";trustedAdmissionFixtureRoot(g_dataRoot);g_labRoot=root;g_worker=selfExecutable();g_steamTarget=json::object();
        SetEnvironmentVariableW(L"YMCC_QUEUE_FIXTURE_DATA_ROOT",g_dataRoot.c_str());
        const auto joined=root/L"Games"/L"Joined"/L"Joined.exe",old=root/L"Games"/L"Ishin2"/L"ISHIN2.EXE";
        text(joined,"already-added fixture");text(old,"old no-ID fixture");json games=json::array();
        for(const auto& exe:{joined,old}) games.push_back({{"primaryExecutable",toUtf8(exe.wstring())},
            {"gameDirectory",toUtf8(exe.parent_path().wstring())},{"directoryName",toUtf8(exe.parent_path().filename().wstring())},
            {"status","ready"},{"contentType","game"}});
        writeJsonWithLocalBackup(g_dataRoot/L"state"/L"library-scan.json",{{"games",games}},false);
        writeJsonWithLocalBackup(g_dataRoot/L"config"/L"library-config.json",{{"roots",json::array({toUtf8((root/L"Games").wstring())})}},false);
        json overrides={{"name","維新の嵐 幕末志士伝"}};
        if(cooldown) overrides["automaticIdentityRetryAfterAt"]=unixTimeMsHost()+600000;
        writeJsonWithLocalBackup(g_dataRoot/L"config"/L"manual-overrides.json",{{"items",{{toUtf8(old.wstring()),overrides}}}},false);
        const json plan={{"libraryState",toUtf8((g_dataRoot/L"state"/L"library-scan.json").wstring())},
            {"summary",{{"alreadyInSteam",1}}},{"items",json::array({
                {{"primaryExecutable",toUtf8(joined.wstring())},{"gameDirectory",toUtf8(joined.parent_path().wstring())},
                 {"status","already-in-steam"},{"shortAppId","2388972698"}},
                {{"primaryExecutable",toUtf8(old.wstring())},{"gameDirectory",toUtf8(old.parent_path().wstring())},
                 {"status","needs-identity-and-artwork"},{"artwork",{{"minimumComplete",false},{"long",false},{"automaticSlotsComplete",false}}}}
            })}};
        writeJsonWithLocalBackup(workspacePlanPath(),plan,false);writeJsonWithLocalBackup(g_dataRoot/L"state"/L"fixture-plan.json",plan,false);
        check(startScrapeQueue(manual),"Queue fixture did not start");
        const auto deadline=GetTickCount64()+15000;
        while(g_scrapeQueueRunning.load()&&GetTickCount64()<deadline)Sleep(10);
        check(!g_scrapeQueueRunning.load(),"Queue waited for identity retries/batch barrier");joinHostBackgroundTasks();
        json history;
        for(const auto& entry:fs::directory_iterator(scrapeHistoryRoot())) {
            if(entry.path().extension()==L".json"&&entry.path().filename().wstring().starts_with(L"round-"))history=readJson(entry.path(),json::object());
        }
        check(history["status"]=="completed"&&history["summary"]["total"]==1&&history["summary"]["completed"]==1,"No-ID queue did not complete or scraped joined game");
        const auto runs=jsonArrayOr(history,"workerRuns");
        check(runs.size()==(cooldown&&!manual?1:2),"Identity failure blocked fallback or caused redundant identity retries");
        for(const auto& run:runs)check(jsonStringOr(run,"executable")==toUtf8(old.wstring()),"Joined shortcut underwent automatic ID verification");
        check(jsonStringOr(runs.back(),"stage")=="artwork-fallback","No-ID game did not directly enter visual fallback");
        const auto snapshot=workspaceSnapshot(false);const auto items=snapshot["steamPlan"]["items"];
        check(items[0]["status"]=="already-in-steam","Artwork refresh moved joined game");
        check(items[1]["status"]=="waiting-steam-verification"&&items[1]["artworkPreview"].size()==3,"No-ID pictures not applied or invented import identity");
        restore();
    } catch(...) {restore();throw;}
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
int wmain(int argc,wchar_t** argv) {
    if(argc==3 && std::wstring(argv[2])==L"--library-media-demand") return libraryMediaDemandProbe(fs::absolute(argv[1]));
    if(argc==3 && std::wstring(argv[1])==L"--fixture-worker") return helper(argv[2]);
    if(argc>2 && std::wstring(argv[2])!=L"--config-storm" && std::wstring(argv[2])!=L"--data-root-admission" && std::wstring(argv[2])!=L"--identity-caller" && std::wstring(argv[2])!=L"--discovery-state" && std::wstring(argv[2])!=L"--editor-queue" && std::wstring(argv[2])!=L"--activity") { try{return queueFixtureWorker(argc,argv);}catch(const std::exception& e){std::cerr<<e.what();return 97;} }
    try {
        check(argc==2 || (argc==3 && (std::wstring(argv[2])==L"--config-storm" || std::wstring(argv[2])==L"--data-root-admission" || std::wstring(argv[2])==L"--identity-caller" || std::wstring(argv[2])==L"--discovery-state" || std::wstring(argv[2])==L"--editor-queue" || std::wstring(argv[2])==L"--activity")),"Pass a fresh isolated root");const auto root=fs::absolute(argv[1]);
        const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
        const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
        check((normalizedPathKey(root).starts_with(normalizedPathKey(build)+L"\\")||normalizedPathKey(root).starts_with(normalizedPathKey(evidence)+L"\\"))&&!fs::exists(root),"Unsafe/reused fixture root");
        fs::create_directories(root);g_dataRoot=root/L"data";trustedAdmissionFixtureRoot(g_dataRoot);g_labRoot=root;SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",g_dataRoot.c_str());const bool admission=argc==3&&std::wstring(argv[2])==L"--data-root-admission";const bool identityCaller=argc==3&&std::wstring(argv[2])==L"--identity-caller";const bool discoveryState=argc==3&&std::wstring(argv[2])==L"--discovery-state";admissionFixtureOnly=admission||identityCaller||discoveryState;json cases=json::array();
        auto test=[&](const char* name,const std::function<void(const fs::path&)>& action){if(discoveryState){if(!std::string(name).starts_with("discovercfg-"))return;wchar_t selected[256]{};const auto n=GetEnvironmentVariableW(L"YMCC_DISCOVERY_STATE_CASE",selected,256);if(n&&n<256&&std::string(name)!=toUtf8(selected))return;}if(identityCaller&&!std::string(name).starts_with("identity-caller-"))return;if(identityCaller){wchar_t choice[256]{};const auto n=GetEnvironmentVariableW(L"YMCC_HOST_IDENTITY_SELFTEST_CASE",choice,256);if(n&&n<256&&std::string(name)!=toUtf8(choice))return;}if(admission&&!std::string(name).starts_with("root-admission-"))return;if(admission){wchar_t selected[256]{};const auto count=GetEnvironmentVariableW(L"YMCC_HOST_ROOT_SELFTEST_CASE",selected,256);if(count&&count<256&&std::string(name)!=toUtf8(selected))return;}if(argc==3 && !admission && !identityCaller && !discoveryState && !std::string(name).starts_with("storm-") && !std::string(name).starts_with("json-") &&
                std::string(name)!="empty-root-config-always-enters-first-run-retry-path" && std::string(name)!="explicitly-cleared-roots-do-not-enter-first-run-discovery")return;hostMoveFault={};identityAfterMove={};identityAfterFlush={};configIoMode=0;configIoTarget.clear();configIoPartial=false;g_shuttingDown=false;g_cancelRequested=false;const auto p=root/std::to_wstring(cases.size());fs::create_directories(p);if(!admission){g_dataRoot=p/L"data";g_labRoot=p;trustedAdmissionFixtureRoot(g_dataRoot);}if(discoveryState){g_steamTarget=json::object();g_worker=p/L"worker-never-launched.exe";admissionProcesses=0;admissionNetwork=0;admissionShell=0;}
            try { action(p);cases.push_back({{"name",name},{"passed",true}}); } catch(const std::exception& e) { cases.push_back({{"name",name},{"passed",false},{"error",toUtf8(toWide(e.what()))}}); }
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
            auto editorCase=[&](const char* name,const std::function<void()>& fn){try{fn();cases.push_back({{"name",name},{"passed",true}});}catch(const std::exception& e){cases.push_back({{"name",name},{"passed",false},{"error",e.what()}});}};
            const auto game=g_dataRoot/L"games"/L"test.exe";text(game,"local fixture");
            const auto key=toUtf8(normalizedPathKey(game));
            auto submission=[&](const std::string& name){return json{{"operations",json::array({{{"command","saveManualName"},{"arguments",{{"executable",toUtf8(game.wstring())},{"name",name}}}}})}};};
            auto waitQueue=[&]{const auto until=GetTickCount64()+5000;while(g_taskRunning.load()&&GetTickCount64()<until)Sleep(5);check(!g_taskRunning.load(),"Native command FIFO did not drain");};
            editorCase("editor-queue-submission-validates-all-commands-before-write",[&]{
                const auto before=bytes(g_dataRoot/L"config"/L"manual-overrides.json");auto args=submission("must-not-write");args["operations"].push_back({{"command","commit"},{"arguments",json::object()}});
                bool rejected=false;try{executeWorkspaceCommand("saveEditor",args);}catch(...){rejected=true;}
                check(rejected&&bytes(g_dataRoot/L"config"/L"manual-overrides.json")==before,"Invalid batch partially wrote user choices");
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
        });
        test("discovercfg-cached-snapshot-required-fallbacks",[&](const auto& p){
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
        test("manual-image-activation-fault-keeps-old-image",[](const auto& p){auto src=p/L"source.png";text(src,"new");const auto dest=g_dataRoot/L"artwork"/L"overrides"/L"image-fault"/L"cover.png";text(dest,"old");hostMoveFault=[&](const auto&,const auto& to){return to==dest.wstring()?ERROR_WRITE_FAULT:0;};mustFail([&]{copyArtworkToIsolation(src,"image-fault","cover");});check(bytes(dest)=="old"&&!temporaries(g_dataRoot),"Old manual artwork truncated");});
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
        test("native-grid-activation-failure-keeps-all-old-extensions",[](const auto& p){const auto grid=p/L"SteamFixture"/L"grid";text(grid/L"620p.jpg","old jpg");text(grid/L"620p.png","old png");text(p/L"new.png","new");NativeSteamArtworkTarget target;target.appId=620;target.grids.push_back({"fixture",grid});hostMoveFault=[&](const auto&,const auto& to){return to==(grid/L"620p.png").wstring()?ERROR_WRITE_FAULT:0;};mustFail([&]{writeNativeSteamArtwork(target,"cover",toUtf8((p/L"new.png").wstring()),false);});check(bytes(grid/L"620p.jpg")=="old jpg"&&bytes(grid/L"620p.png")=="old png","Old extensions deleted before activation");check(!temporaries(p),"Native staging leaked");});
        test("native-grid-success-replaces-only-own-slot",[](const auto& p){const auto grid=p/L"SteamFixture"/L"grid";text(grid/L"620p.jpg","old cover");text(grid/L"620_hero.png","hero stays");text(p/L"new.png","new cover");NativeSteamArtworkTarget target;target.appId=620;target.grids.push_back({"fixture",grid});writeNativeSteamArtwork(target,"cover",toUtf8((p/L"new.png").wstring()),false);check(bytes(grid/L"620p.png")=="new cover"&&!fs::exists(grid/L"620p.jpg")&&bytes(grid/L"620_hero.png")=="hero stays","Slot/extension replacement damaged other files");});
        test("unicode-artwork-and-config-paths",[](const auto& p){const auto source=p/L"测试游戏 Ω"/L"封面.png";text(source,"local artwork");const auto dest=p/L"数据"/L"图片.png";copyHostFileAtomic(source,dest);writeJsonWithLocalBackup(p/L"数据"/L"配置.json",{{"game","测试游戏"}},false);check(bytes(dest)=="local artwork"&&!temporaries(p),"Unicode paths failed");});
        test("manifest-wrong-root-and-invalid-json-are-skipped",[](const auto& p){const auto saved=g_dataRoot;g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);text(g_dataRoot/L"artwork"/L"bad"/L"manifest.json","{");writeJsonWithLocalBackup(g_dataRoot/L"artwork"/L"array"/L"manifest.json",json::array(),false);const auto exe=p/L"Game.exe";writeJsonWithLocalBackup(g_dataRoot/L"artwork"/L"valid"/L"manifest.json",{{"exe",toUtf8(exe.wstring())}},false);auto result=findPortableArtworkManifestForHost(toUtf8(exe.wstring()));g_dataRoot=saved;check(result&&result->second.parent_path().filename()==L"valid","Bad manifest hid valid game");});
        test("empty-artwork-manifest-exe-does-not-match",[](const auto& p){const auto saved=g_dataRoot;g_dataRoot=p/L"data";trustedAdmissionFixtureRoot(g_dataRoot);writeJsonWithLocalBackup(g_dataRoot/L"artwork"/L"manifest.json",{{"exe",""}},false);auto result=findPortableArtworkManifestForHost("");g_dataRoot=saved;check(!result,"Empty path accepted as identity");});
        test("multiple-host-fixture-mutex-denies-second-owner",[](const auto&){const auto name=L"Local\\YeManSteamLibraryFixture-"+std::to_wstring(GetCurrentProcessId());HANDLE first=CreateMutexW(nullptr,TRUE,name.c_str());check(first!=nullptr,"Fixture mutex failed");HANDLE second=CreateMutexW(nullptr,TRUE,name.c_str());const auto error=GetLastError();CloseHandle(second);ReleaseMutex(first);CloseHandle(first);check(error==ERROR_ALREADY_EXISTS,"Second instance not detected");});
        test("host-json-and-image-copy-beyond-max-path",[](const auto& p){auto dir=p;for(int i=0;i<8;++i)dir/=L"long-config-artwork-fixture-012345";const auto config=dir/L"config.json";check(config.wstring().size()>300,"Long-path fixture too short");writeJsonWithLocalBackup(config,{{"id",1}},false);writeJsonWithLocalBackup(config,{{"id",2}},false);check(readJson(config,json::object())["id"]==2,"Long-path Host JSON failed");const auto copy=dir/L"local-artwork.png";copyHostFileAtomic(config,copy);check(bytes(copy)==bytes(config),"Long-path artwork copy failed");});
        test("automatic-no-id-queue-applies-pictures-without-retrying-identity",[](const auto& p){runQueueFixture(p,false,false);});
        test("identity-cooldown-does-not-block-no-id-artwork",[](const auto& p){runQueueFixture(p,false,true);});
        test("manual-refresh-still-excludes-already-added-shortcuts",[](const auto& p){runQueueFixture(p,true,false);});
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
            text(source,"modern native header");
            const json item={{"steamNative",true},{"nativeSteamAppId",620},{"steamStoreAppId",620},
                {"gameDirectory",toUtf8((root/L"steamapps"/L"common"/L"Portal 2").wstring())},
                {"nativeSteamManifestPath",toUtf8((root/L"steamapps"/L"appmanifest_620.acf").wstring())}};
            json plan={{"items",json::array({item})}};attachNativeSteamArtwork(plan,json::object(),true);
            const auto preview=plan["items"][0]["artworkPreview"]["long"];
            check(preview["provider"]=="steam-librarycache-native"&&preview["file"]==toUtf8(source.wstring()),"Native horizontal did not reach plan");
            check(bytes(fs::path(toWide(preview["portableFile"].get<std::string>())))=="modern native header","Cached native bytes changed");
            check(plan["items"][0]["nativeSteamArtwork"]["longAvailable"]==true,"Native long availability not recorded");
            fs::remove(source);json next={{"items",json::array({item})}};attachNativeSteamArtwork(next,json::object(),false);
            check(next["items"][0]["artworkPreview"]["long"]["url"]==preview["url"],"Background snapshot dropped portable native horizontal");
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
        test("root-admission-health-probe-does-not-sign-unknown-root",[&](const fs::path& p){const auto data=setup(p);text(data/L"data-root.json","{");const auto original=tree(data);std::string failure;check(!probeUpdateHealthDataRoot(failure)&&failure.find("library-data-root-read-only")!=std::string::npos&&tree(data)==original,"Health probe wrote or accepted unknown root");});
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

        size_t failures=0;for(const auto& t:cases)if(!t["passed"].get<bool>())++failures;
        json report={{"allPassed",failures==0},{"caseCount",cases.size()},{"failedCount",failures},{"cases",cases},{"realSteamFilesModified",false},{"steamStoppedOrLaunched",false}};
        g_labRoot=root;g_dataRoot=root/L"reporting-only-data";hostMoveFault={};writeJsonWithLocalBackup(root/L"summary.json",report,false);std::cout<<report.dump(2)<<"\n";return failures?1:0;
    } catch(const std::exception& e){g_shuttingDown=true;requestTaskCancellation();closeWorkerProcessJob();joinHostBackgroundTasks();std::cerr<<e.what()<<"\n";return 1;}
}
