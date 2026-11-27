#if defined(YMCC_LIBRARY_TYPED_SHUTDOWN) && !defined(YMCC_LIBRARY_HOST_CHAIN)
#define YMCC_LIBRARY_HOST_CHAIN 1
#endif
#if defined(YMCC_LIBRARY_HOST_CHAIN) && !defined(YMCC_LIBRARY_FACTS_IPC)
#define YMCC_LIBRARY_FACTS_IPC 1
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <aclapi.h>
#include <functional>
#include <string>
#include <algorithm>
#include <cstring>

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
    return FlushFileBuffers(h);
}
#define WriteFile configFixtureWrite
#define FlushFileBuffers configFixtureFlush


static std::wstring transactionFixtureSteamRoot;
static LSTATUS WINAPI transactionFixtureRegistry(HKEY,LPCWSTR,LPCWSTR,DWORD,LPDWORD type,PVOID data,LPDWORD bytes){
    if(transactionFixtureSteamRoot.empty())return ERROR_FILE_NOT_FOUND;
    const DWORD size=static_cast<DWORD>((transactionFixtureSteamRoot.size()+1)*sizeof(wchar_t));if(type)*type=REG_SZ;
    if(!data){*bytes=size;return ERROR_SUCCESS;}if(*bytes<size){*bytes=size;return ERROR_MORE_DATA;}
    std::memcpy(data,transactionFixtureSteamRoot.c_str(),size);*bytes=size;return ERROR_SUCCESS;
}
#define RegGetValueW transactionFixtureRegistry

// This original consumer never discovers real legacy installations.
#include "../native/custom-steam-library/custom_steam_library_paths.h"
namespace custom_steam_library {
inline std::vector<fs::path> dataRootFixtureLegacyRoots(const fs::path& lab) {
    return {lab/L"data", lab/L"data"/L"Steam大屏", lab/L"Steam大屏"};
}
}
#define legacyDataRoots dataRootFixtureLegacyRoots

#ifdef YMCC_LIBRARY_FACTS_IPC
#include "steam_client_action_channel_fixtures/native_resource_observation.inc"
#endif
// These ports are active only for this original consumer's exact identity mode.
#include <winhttp.h>
#include <mutex>
#include <vector>
#ifdef YMCC_LIBRARY_FACTS_IPC
static bool factsIpcRealClient=false;
static DWORD factsIpcOwnedProviderPid=0;
static unsigned factsIpcAllowedProcessReads=0;
#endif
static bool identityCallerActive=false;
static unsigned identityCallerNetwork=0,identityCallerProcesses=0;
static std::function<void(const std::wstring&)> identityCallerReadHook;
static std::mutex identityCallerReadMx;
static std::vector<std::wstring> identityCallerReads;
static std::wstring identityCallerPath(std::wstring path){if(path.starts_with(L"\\\\?\\"))path=path.substr(4);std::replace(path.begin(),path.end(),L'/',L'\\');for(auto& c:path)c=static_cast<wchar_t>(towlower(c));return path;}
static BOOL WINAPI identityCallerRead(HANDLE handle,LPVOID data,DWORD count,LPDWORD read,LPOVERLAPPED over){
 const auto ok=ReadFile(handle,data,count,read,over);if(ok&&identityCallerActive){wchar_t path[32768]{};const auto n=GetFinalPathNameByHandleW(handle,path,32768,FILE_NAME_NORMALIZED);if(n&&n<32768){const auto file=identityCallerPath(path);{std::lock_guard<std::mutex> guard(identityCallerReadMx);identityCallerReads.push_back(file);}if(identityCallerReadHook)identityCallerReadHook(file);}}return ok;
}
static HANDLE WINAPI identityCallerOpenProcess(DWORD access,BOOL inherit,DWORD pid){
#ifdef YMCC_LIBRARY_FACTS_IPC
    if(identityCallerActive&&factsIpcRealClient&&(pid==GetCurrentProcessId()||pid==factsIpcOwnedProviderPid)){++factsIpcAllowedProcessReads;return nativeObserveOpenProcess(access,inherit,pid);}
#endif
    if(identityCallerActive){++identityCallerProcesses;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
#ifdef YMCC_LIBRARY_FACTS_IPC
    return nativeObserveOpenProcess(access,inherit,pid);
#else
    return OpenProcess(access,inherit,pid);
#endif
}
static BOOL WINAPI identityCallerCreateProcess(LPCWSTR a,LPWSTR b,LPSECURITY_ATTRIBUTES c,LPSECURITY_ATTRIBUTES d,BOOL e,DWORD f,LPVOID g,LPCWSTR h,LPSTARTUPINFOW i,LPPROCESS_INFORMATION j){if(identityCallerActive){++identityCallerProcesses;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return CreateProcessW(a,b,c,d,e,f,g,h,i,j);}
static HINTERNET WINAPI identityCallerHttp(LPCWSTR agent,DWORD type,LPCWSTR proxy,LPCWSTR bypass,DWORD flags){if(identityCallerActive){++identityCallerNetwork;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}return WinHttpOpen(agent,type,proxy,bypass,flags);}
static std::function<void(const std::wstring&)> identityCallerCloseHook;
static BOOL WINAPI identityCallerClose(HANDLE handle){std::wstring file;if(identityCallerActive&&identityCallerCloseHook){wchar_t path[32768]{};const auto n=GetFinalPathNameByHandleW(handle,path,32768,FILE_NAME_NORMALIZED);if(n&&n<32768)file=identityCallerPath(path);}const auto ok=
#ifdef YMCC_LIBRARY_FACTS_IPC
        nativeObserveClose(handle);
#else
        CloseHandle(handle);
#endif
    if(ok&&!file.empty()&&identityCallerCloseHook)identityCallerCloseHook(file);return ok;}
#define CloseHandle identityCallerClose
#define ReadFile identityCallerRead
#define OpenProcess identityCallerOpenProcess
#define CreateProcessW identityCallerCreateProcess
#define WinHttpOpen identityCallerHttp
// Observe the real owned MoveFileEx success before canonical readback opens.
static std::function<void(const std::wstring&,const std::wstring&)> ruleTextAfterMove;
static BOOL WINAPI ruleTextObservedMove(LPCWSTR from,LPCWSTR to,DWORD flags){const auto ok=MoveFileExW(from,to,flags);if(ok&&ruleTextAfterMove)ruleTextAfterMove(from,to);return ok;}
#define MoveFileExW ruleTextObservedMove
// Renamed CLI entrypoints are invoked only with explicit owned fixture paths.
static bool discoveryProducerPorts=false;
static unsigned discoveryProducerPriorityWrites=0,discoveryProducerDriveDiscovery=0;
static BOOL WINAPI producerGetProcessInformation(HANDLE process,PROCESS_INFORMATION_CLASS kind,LPVOID value,DWORD size){if(discoveryProducerPorts){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}return GetProcessInformation(process,kind,value,size);}
static BOOL WINAPI producerSetProcessInformation(HANDLE process,PROCESS_INFORMATION_CLASS kind,LPVOID value,DWORD size){if(discoveryProducerPorts){++discoveryProducerPriorityWrites;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return SetProcessInformation(process,kind,value,size);}
static DWORD WINAPI producerGetPriorityClass(HANDLE process){return discoveryProducerPorts?IDLE_PRIORITY_CLASS:GetPriorityClass(process);}
static BOOL WINAPI producerSetPriorityClass(HANDLE process,DWORD value){if(discoveryProducerPorts){++discoveryProducerPriorityWrites;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return SetPriorityClass(process,value);}
static DWORD WINAPI producerLogicalDrives(){if(discoveryProducerPorts){++discoveryProducerDriveDiscovery;return 0;}return GetLogicalDrives();}
static UINT WINAPI producerDriveType(LPCWSTR path){return discoveryProducerPorts?DRIVE_FIXED:GetDriveTypeW(path);}
static BOOL WINAPI producerConsoleOutput(UINT value){return discoveryProducerPorts?TRUE:SetConsoleOutputCP(value);}
static BOOL WINAPI producerConsoleInput(UINT value){return discoveryProducerPorts?TRUE:SetConsoleCP(value);}
#define GetProcessInformation producerGetProcessInformation
#define SetProcessInformation producerSetProcessInformation
#define GetPriorityClass producerGetPriorityClass
#define SetPriorityClass producerSetPriorityClass
#define GetLogicalDrives producerLogicalDrives
#define GetDriveTypeW producerDriveType
#define SetConsoleOutputCP producerConsoleOutput
#define SetConsoleCP producerConsoleInput
// Observe the actual production mutex waits; no shipping source hook is introduced.
static std::function<void(HANDLE,DWORD)> planPublicationWaitHook;
static DWORD WINAPI planPublicationWait(HANDLE handle,DWORD timeout){
    if(planPublicationWaitHook)planPublicationWaitHook(handle,timeout);
    return WaitForSingleObject(handle,timeout);
}
#define WaitForSingleObject planPublicationWait
#include "custom_steam_library_test_runtime.h"
#include "../native/shared_game_media.h"
#undef GetProcessInformation
#undef SetProcessInformation
#undef GetPriorityClass
#undef SetPriorityClass
#undef GetLogicalDrives
#undef GetDriveTypeW
#undef SetConsoleOutputCP
#undef SetConsoleCP
#undef ReadFile
#undef OpenProcess
#undef CreateProcessW
#undef WinHttpOpen
#undef legacyDataRoots
#undef RegGetValueW
#undef WriteFile
#undef FlushFileBuffers
#undef WaitForSingleObject
#include <winioctl.h>
#include <thread>


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

static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void textFile(const fs::path& path, const std::string& text) { writeAtomic(path, std::vector<unsigned char>(text.begin(), text.end())); }
static void mustThrow(const std::function<void()>& fn) { bool threw=false; try { fn(); } catch (...) { threw=true; } check(threw, "Expected operation to fail safely"); }
static std::string fileText(const fs::path& path) { const auto bytes=readBinaryFile(path); return std::string(bytes.begin(),bytes.end()); }
static bool hasStages(const fs::path& root) {
    for (const auto& entry: fs::recursive_directory_iterator(root)) {
        const auto name=entry.path().filename().wstring();
        if (name.find(L".yeman-stage-")!=std::wstring::npos || name.find(L".yeman-delete-stage-")!=std::wstring::npos) return true;
    }
    return false;
}
static std::vector<fs::path> pendingJournals(const fs::path& root) {
    std::vector<fs::path> result;
    for(const auto& entry:fs::recursive_directory_iterator(root)) if(entry.path().filename()==L"transaction.pending.json") result.push_back(entry.path());
    return result;
}
struct Fixture {
    fs::path root, data, local, planFile;
    SteamShortcutTarget target;
    json plan;
    Fixture(const fs::path& base, bool existing=false):root(base),data(base/L"data"),
        local(base/L"Steam"/L"userdata"/L"19627"/L"config"/L"localconfig.vdf"),planFile(data/L"state"/L"plan.json"),target{local.parent_path()/L"shortcuts.vdf","19627"} {
        fs::create_directories(local.parent_path());
        textFile(base/L"Steam"/L"steam.exe", "fixture only, not an executable");
        if(existing) writeAtomic(target.path,BinaryVdfDocument::emptySteamShortcuts().serialize());
        const uint32_t id=0xc0000001u; int32_t stored=0; std::memcpy(&stored,&id,sizeof(id));
        plan={{"realSteamTarget",true},{"shortcutsBeforeSha256",sha256(existing?readBinaryFile(target.path):std::vector<unsigned char>{})},
            {"summary",{{"readyToAdd",1},{"shortcutIdConflicts",0}}},{"unmatchedSelections",json::array()},
            {"items",json::array({{{"status","ready-to-add"},{"formalName","Data chain fixture"},{"storedAppId",stored},{"shortAppId",std::to_string(id)},
              {"longAppId",std::to_string((static_cast<uint64_t>(id)<<32)|0x02000000ull)},
              {"quotedExecutable",quotedSteamPath(root/L"Game"/L"Game.exe")},{"quotedStartDirectory",quotedSteamPath(root/L"Game")}}})}};
    }
    json commit() { return commitPreparedSteamAddPlan(data,target,planFile,true,plan); }
};
static bool createFixtureJunction(const fs::path& link, const fs::path& target) {
    fs::create_directories(link);
    const auto print = fs::absolute(target).wstring(), substitute = L"\\??\\" + print;
    struct MountPointData { DWORD tag; WORD length, reserved, substituteOffset, substituteLength, printOffset, printLength; WCHAR paths[1]; };
    const auto names = substitute + L'\0' + print + L'\0';
    std::vector<unsigned char> buffer(offsetof(MountPointData, paths) + names.size() * sizeof(wchar_t));
    auto* data = reinterpret_cast<MountPointData*>(buffer.data());
    data->tag = IO_REPARSE_TAG_MOUNT_POINT; data->length = static_cast<WORD>(buffer.size() - 8);
    data->substituteLength = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data->printOffset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t)); data->printLength = static_cast<WORD>(print.size() * sizeof(wchar_t));
    std::memcpy(data->paths, names.data(), names.size() * sizeof(wchar_t));
    HANDLE h = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD returned = 0; const bool ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(h); return ok;
}

static fs::path fixtureScanState(Fixture& f) {
    const auto state=f.data/L"state"/L"library-scan.json";
    textFile(f.root/L"Game"/L"Game.exe","fixture executable");
    writeJsonAtomic(state,{{"games",json::array({{{"gameDirectory",toUtf8((f.root/L"Game").wstring())},{"primaryExecutable",toUtf8((f.root/L"Game"/L"Game.exe").wstring())}}})}});
    return state;
}
// Exercise both real worker entry orders against one local plan path and shortcuts file.
static void fixturePlanCommitPublication(const fs::path& root,bool planFirst){
    Fixture f(root,true);const auto scan=fixtureScanState(f);
    auto inventory=loadJsonDocument(scan);inventory["games"][0]["status"]="ready";writeJsonAtomic(scan,inventory);
    f.plan["libraryState"]=toUtf8(scan.wstring());f.plan["accountId"]=f.target.accountId;f.plan["shortcutsVdf"]=toUtf8(f.target.path.wstring());
    f.plan["items"][0]["gameDirectory"]=toUtf8((f.root/L"Game").wstring());f.plan["items"][0]["primaryExecutable"]=toUtf8((f.root/L"Game"/L"Game.exe").wstring());
    HANDLE paused=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr),waiting=CreateEventW(nullptr,TRUE,FALSE,nullptr),secondDone=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!paused||!release||!waiting||!secondDone){for(auto h:{paused,release,waiting,secondDone})if(h)CloseHandle(h);throw std::runtime_error("Cannot create owned publication gates");}
    std::atomic<DWORD> pausedThread{0},observedThread{0};std::atomic<unsigned> waitCalls{0},shortcutReads{0};std::atomic<bool> pauseOnce{false};
    json planned,committed;std::string planError,commitError;
    const auto pausePath=identityCallerPath((planFirst?f.planFile:f.target.path).wstring());
    selftestMoveFault=[&](const auto&,const auto& to)->DWORD{
        if(!planFirst&&GetCurrentThreadId()==pausedThread.load()&&identityCallerPath(to)==pausePath&&!pauseOnce.exchange(true)){
            SetEvent(paused);if(WaitForSingleObject(release,5000)!=WAIT_OBJECT_0)return ERROR_TIMEOUT;
        }return ERROR_SUCCESS;
    };
    identityCallerReadHook=[&](const std::wstring& file){
        if(planFirst&&GetCurrentThreadId()==pausedThread.load()&&file==identityCallerPath(f.target.path.wstring())&&++shortcutReads==2&&!pauseOnce.exchange(true)){
            SetEvent(paused);if(WaitForSingleObject(release,5000)!=WAIT_OBJECT_0)throw std::runtime_error("Owned plan read gate timed out");
        }
    };
    planPublicationWaitHook=[&](HANDLE,DWORD){
        if(GetCurrentThreadId()==observedThread.load()&&++waitCalls==(planFirst?2u:1u))SetEvent(waiting);
    };
    auto plan=[&]{try{planned=buildSteamLibraryAddPlan(f.data,scan,f.target,f.planFile);}catch(const std::exception& e){planError=e.what();}};
    auto commit=[&]{try{committed=f.commit();}catch(const std::exception& e){commitError=e.what();}};
    std::thread first([&]{pausedThread=GetCurrentThreadId();if(planFirst)plan();else commit();});
    const bool reachedPause=WaitForSingleObject(paused,5000)==WAIT_OBJECT_0;
    bool dataHeldAtPause=false;std::thread ownershipProbe([&]{try{DataTransactionMutex probe(0);}catch(const std::exception&){dataHeldAtPause=true;}});ownershipProbe.join();
    std::thread second([&]{observedThread=GetCurrentThreadId();if(planFirst)commit();else plan();SetEvent(secondDone);});
    const bool reachedWait=WaitForSingleObject(waiting,5000)==WAIT_OBJECT_0;
    const bool secondBlocked=WaitForSingleObject(secondDone,150)==WAIT_TIMEOUT;
    SetEvent(release);first.join();second.join();selftestMoveFault={};identityCallerReadHook={};planPublicationWaitHook={};
    for(auto h:{paused,release,waiting,secondDone})CloseHandle(h);
    const auto cached=loadJsonDocument(f.planFile);const auto shortcuts=readShortcuts(f.target.path,f.target.accountId);
    const auto status=[](const json& value){if(!value.is_object())return std::string{"unavailable"};const auto items=value.value("items",json::array());return items.empty()?std::string{}:jsonStringOr(items[0],"status");};
    const json observation={{"order",planFirst?"plan-then-commit":"commit-then-plan"},{"pausePoint",planFirst?"second-shortcuts-read-before-publication":"shortcut-activation"},{"planShortcutReads",shortcutReads.load()},{"pauseReached",reachedPause},{"dataMutexHeldAtPause",dataHeldAtPause},{"nativeMutexWaitObserved",reachedWait},{"waitCalls",waitCalls.load()},{"secondBlockedUntilRelease",secondBlocked},{"planError",planError},{"commitError",commitError},{"planStatus",status(planned)},{"cachedStatus",status(cached)},{"cachedMode",jsonStringOr(cached,"mode")},{"committed",committed.is_object()&&committed.value("committed",false)},{"shortcutCount",shortcuts.size()},{"planPath",toUtf8(f.planFile.wstring())}};
    writeJsonAtomic(root/L"plan-publication-observations.json",observation);
    check(reachedPause,"Owned publication pause was not reached");
    check(dataHeldAtPause,"Plan/commit publication gate is not protected by the data transaction");
    check(planError.empty()&&commitError.empty(),"Concurrent planner and commit did not finish successfully");
    check(committed.value("committed",false)&&shortcuts.size()==1&&pendingJournals(f.data).empty()&&!hasStages(root),"Concurrent test lost the committed shortcut or transaction cleanup");
    const bool ordered=planFirst?status(cached)=="added-to-steam":status(planned)=="already-in-steam"&&status(cached)=="already-in-steam";
    check(reachedWait&&secondBlocked&&ordered,"Concurrent plan publication did not retain committed ownership ordering");
}
static json fixtureDelete(Fixture& f) { return commitSteamLibraryDeletePlan(f.data,fixtureScanState(f),f.target,f.data/L"state"/L"delete.json",true,{canonicalPathKey(f.root/L"Game")}); }
struct ArtworkRepairFixture : Fixture {
    explicit ArtworkRepairFixture(const fs::path& path, bool seedLegacy = true) : Fixture(path) {
        const auto exe = root / L"Game" / L"Game.exe";
        textFile(exe, "fixture executable");
        plan["items"][0]["primaryExecutable"] = toUtf8(exe.wstring());
        if (!seedLegacy) return;
        auto before = BinaryVdfDocument::emptySteamShortcuts();
        vdfFindObject(before.root(), "shortcuts")->objectValue.push_back({"0", steamShortcutNode(-1073741823,
            "Legacy artwork card", quotedSteamPath(exe), quotedSteamPath(exe.parent_path()), 21)});
        writeAtomic(target.path, before.serialize());
    }
};
static void writeRepairArtwork(const fs::path& file, BYTE shade = 42, bool jpeg = false) {
    (void)artworkInspectionGdiPlusToken();
    Gdiplus::Bitmap image(4, 4, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&image);
        check(graphics.Clear(Gdiplus::Color(255, shade, 90, 50)) == Gdiplus::Ok, "Fixture artwork draw failed");
    }
    const CLSID jpegEncoder{0x557cf401, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    check(image.Save(file.c_str(), jpeg ? &jpegEncoder : &pngEncoderClsid(), nullptr) == Gdiplus::Ok, "Fixture artwork encoding failed");
}
static void verifyManualArtworkCacheWrite(const fs::path& path, std::string_view mode) {
    const bool adding = mode.starts_with("add-");
    ArtworkRepairFixture f(path, !adding);
    const auto game=path/L"Game", exe=game/L"Game.exe", original=path/L"original.png", portable=path/L"bad.png";
    const auto art=f.data/L"artwork"/toWide(gameDataId(game));
    const auto manifest=art/L"manifest.json";
    const auto recovered=(mode=="repair-foreign-cache"?path:art)/L"tall.png";
    fs::create_directories(art);
    writeRepairArtwork(recovered, 80);
    textFile(portable,"broken portable original");
    const auto text=[](const fs::path& value){return toUtf8(value.wstring());};
    const json choice={{"file",text(original)},{"portableFile",text(portable)},{"updatedAt",42}};
    json row={{"type","tall"},{"steamSuffix","p"},{"provider","manual-override"},{"ok",true},
        {"file",text(original)},{"portableFile",text(recovered)},{"sha256",sha256(readBinaryFile(recovered))},{"manualRecovery",choice}};
    if(mode=="repair-altered-bytes"){fs::remove(recovered);writeRepairArtwork(recovered,92);}
    if(mode=="repair-live-original")writeRepairArtwork(original,61);
    if(mode=="repair-preview-only")row["previewOnlyManualFallback"]=true;
    const auto manifestExe=mode=="add-foreign-game"?path/L"Other.exe":exe;
    writeJsonAtomic(manifest,{{"exe",text(manifestExe)},{"artwork",json::array({row})}});
    json saved={{"cover",choice},{"artworkProtection",{{"cover","manual"}}}};
    if(mode=="repair-reselected")saved["cover"]["updatedAt"]=43;
    if(mode=="repair-deleted")saved["artworkProtection"]["cover"]="deleted";
    if(mode.ends_with("cleared"))saved=json::object();
    const auto config=f.data/L"config"/L"manual-overrides.json";
    writeJsonAtomic(config,{{"items",{{canonicalPathKey(exe),saved}}}});
    const auto originalConfig=readBinaryFile(config);
    const auto grid=f.target.path.parent_path()/L"grid", portrait=grid/L"3221225473p.png";
    const bool useRecovery=mode=="add-recovery"||mode=="repair-recovery";
    if(adding){
        f.plan["items"][0]["gameDirectory"]=text(game);
        f.plan["items"][0]["artworkManifest"]=text(manifest);
        const auto result=f.commit();
        const auto shortcuts=readShortcuts(f.target.path,f.target.accountId);
        check(result.value("committed",false)&&shortcuts.size()==1&&shortcuts[0].exe==quotedSteamPath(exe)&&
            shortcuts[0].launchOptions.empty(),"Static recovery affected ordinary launch or commit");
        check(useRecovery?(fs::exists(portrait)&&readBinaryFile(portrait)==readBinaryFile(recovered)):!fs::exists(portrait),
            "First addition lost the verified manual recovery or imported stale/foreign artwork");
    }else{
        textFile(portrait,"old portrait bytes");
        const auto oldPortrait=readBinaryFile(portrait), oldCard=readBinaryFile(f.target.path);
        const auto state=fixtureScanState(f);
        const auto result=refreshExistingSteamArtwork(f.data,state,f.target,f.data/L"state"/L"repair.json",true,{canonicalPathKey(game)});
        const bool usingLiveOriginal=mode=="repair-live-original";
        const auto expected=useRecovery||usingLiveOriginal?readBinaryFile(usingLiveOriginal?original:recovered):oldPortrait;
        writeJsonAtomic(path/L"observed.json",result);
        check(readBinaryFile(portrait)==expected&&readBinaryFile(f.target.path)==oldCard,
            "Same-card recovery lost or replaced the portrait without current authority");
        const auto& verified=result.at("items").at(0);
        check(!verified.value("verifiedStaticArtworkWrites",json::array()).empty()==(useRecovery||usingLiveOriginal),
            "Readback misreported which portrait was published");
    }
    check(readBinaryFile(config)==originalConfig&&fileText(portable)=="broken portable original","Repair rewrote the manual selection or source");
}
static json fixtureRefresh(Fixture& f) {
    const auto cover=f.root/L"new-cover.png";writeRepairArtwork(cover);
    writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(f.root/L"Game"/L"Game.exe"),{{"cover",{{"file",toUtf8(cover.wstring())}}}}}}}});
    return refreshExistingSteamArtwork(f.data,fixtureScanState(f),f.target,f.data/L"state"/L"refresh.json",true,{canonicalPathKey(f.root/L"Game")});
}

static void fixtureSteamArtworkPreimage(const fs::path& path, std::string_view variant) {
    ArtworkRepairFixture f(path);
    const auto grid=f.target.path.parent_path()/L"grid";
    const bool caseVariant=variant.starts_with("case-");
    if(caseVariant)variant.remove_prefix(5);
    const auto original=grid/(caseVariant?L"3221225473p.PNG":L"3221225473p.jpg"), replacement=grid/L"3221225473p.png";
    const auto dynamic=grid/L"3221225473p.webm";
    textFile(original,"old Steam portrait bytes");
    const auto wantedSize=caseVariant?static_cast<size_t>(fs::file_size(original)):
        variant=="above-backup-limit" ? (64u<<20)+1 : MAX_IMAGE_BYTES+1;
    fs::resize_file(original,wantedSize);
    textFile(dynamic,"untouched dynamic preview");
    const auto originalHash=sha256(readBinaryFile(original,wantedSize));
    const auto shortcut=readBinaryFile(f.target.path);
    const auto originalIntact=[&]{return fs::file_size(original)==wantedSize&&sha256(readBinaryFile(original,wantedSize))==originalHash;};
    if(variant=="replace") {
        const auto result=fixtureRefresh(f);
        check(result["artworkFilesActivated"]==1&&(caseVariant||!fs::exists(original))&&
            readBinaryFile(replacement)==readBinaryFile(path/L"new-cover.png"),"Old cover blocked replacement");
        if(caseVariant)check(existingArtworkForStem(grid,"3221225473p")==std::vector<fs::path>{original},
            "Case-only replacement did not keep one canonical grid file");
        const auto backup=fs::path(toWide(jsonStringOr(result,"backup")))/L"grid-before"/original.filename();
        check(sha256(readBinaryFile(backup,wantedSize))==originalHash,"Large original was not backed up byte-for-byte");
    } else if(variant=="above-backup-limit") {
        mustThrow([&]{fixtureRefresh(f);});
        check(originalIntact()&&!fs::exists(replacement),"Too-large original was not left alone");
    } else if(variant=="oversize-new-source") {
        const auto source=path/L"new-source.png";
        writeRepairArtwork(source);fs::resize_file(source,MAX_IMAGE_BYTES+1);
        writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(path/L"Game"/L"Game.exe"),
            {{"cover",{{"file",toUtf8(source.wstring())}}}}}}}});
        const auto result=refreshExistingSteamArtwork(f.data,fixtureScanState(f),f.target,
            f.data/L"state"/L"refresh.json",true,{canonicalPathKey(path/L"Game")});
        check(result["artworkFilesActivated"]==0&&originalIntact()&&!fs::exists(replacement),"New image size admission was relaxed");
    } else {
        bool completionReached=false, recoveryWriteBlocked=false;
        selftestMoveFault=[&](const auto&,const auto& to)->DWORD{
            if(to.ends_with(L"transaction.complete.json")){completionReached=true;return ERROR_WRITE_FAULT;}
            if(variant=="recover"&&completionReached&&fs::path(to).filename()==original.filename()&&
                to.find(L"\\config\\grid\\")!=std::wstring::npos){recoveryWriteBlocked=true;return ERROR_WRITE_FAULT;}
            return ERROR_SUCCESS;
        };
        mustThrow([&]{fixtureRefresh(f);});selftestMoveFault={};
        check(completionReached,"Repair did not reach activation and completion");
        if(variant=="recover") {
            check(recoveryWriteBlocked&&!pendingJournals(f.data).empty(),"Rollback failure lost the large original's journal");
            if(caseVariant) {
                const auto newBytes=readBinaryFile(path/L"new-cover.png");
                check(readBinaryFile(original)==newBytes,"Failed rollback removed the live same-path replacement");
                bool retryWriteBlocked=false;
                selftestMoveFault=[&](const auto&,const auto& to)->DWORD{
                    if(fs::path(to)==original){retryWriteBlocked=true;return ERROR_WRITE_FAULT;}
                    return ERROR_SUCCESS;
                };
                cleanupInterruptedSteamTransactions(f.data);selftestMoveFault={};
                check(retryWriteBlocked&&readBinaryFile(original)==newBytes&&!pendingJournals(f.data).empty(),
                    "Failed recovery removed the live same-path replacement or lost its journal");
                fs::remove(original);
                cleanupInterruptedSteamTransactions(f.data);
                check(!fs::exists(original)&&!pendingJournals(f.data).empty(),"External deletion was mistaken for an incomplete write");
                writeAtomic(original,newBytes);
            }
            cleanupInterruptedSteamTransactions(f.data);
        }
        check(originalIntact()&&(caseVariant||!fs::exists(replacement)),"Original was not restored byte-for-byte");
    }
    check(readBinaryFile(f.target.path)==shortcut&&fileText(dynamic)=="untouched dynamic preview"&&
        pendingJournals(f.data).empty()&&!hasStages(path),"Artwork repair touched unrelated data or left debt");
}

static void fixtureSteamArtworkAliasRecovery(const fs::path& p, std::string_view variant) {
    ArtworkRepairFixture f(p, false);
    const auto game = p/L"Game", directory = game/L"bin", primary = directory/L"Game.exe", scan = fixtureScanState(f);
    const auto text = [](const fs::path& path) { return toUtf8(path.wstring()); };
    textFile(primary, "isolated primary executable");
    json inventory{{"games",json::array({{{"gameDirectory",text(directory)},{"primaryExecutable",text(primary)},
        {"status","ready"},{"alternateGameDirectories",json::array({text(game)})}}})}};
    if (variant=="recovered-scan") inventory["libraryScanRecovery"]={{"schemaVersion",1},{"requiresFreshScan",true}};
    writeJsonAtomic(scan,inventory);
    auto document = BinaryVdfDocument::emptySteamShortcuts();
    vdfFindObject(document.root(),"shortcuts")->objectValue.push_back({"0",steamShortcutNode(-1073741823,
        "Existing game",quotedSteamPath(primary),quotedSteamPath(directory),21)});
    const auto before = document.serialize();
    writeAtomic(f.target.path,before);
    const auto grid = f.target.path.parent_path()/L"grid";
    const auto cover = p/L"old-good-portrait.png", hero = p/L"old-good-hero.png", horizontal = p/L"current-good-horizontal.png";
    const auto currentHero = p/L"current-good-hero.png", broken = p/L"current-broken.png";
    writeRepairArtwork(cover,40); writeRepairArtwork(hero,41); writeRepairArtwork(horizontal,42); writeRepairArtwork(currentHero,43);
    textFile(broken,"bad current portrait");
    textFile(grid/L"3221225473p.jpg","broken Steam portrait");
    const auto row = [&](const char* type, const char* suffix, const fs::path& file) {
        return json{{"type",type},{"steamSuffix",suffix},{"ok",true},{"file",text(file)}};
    };
    const auto currentPath = f.data/L"artwork"/toWide(gameDataId(directory))/L"manifest.json";
    const auto oldPath = f.data/L"artwork"/toWide(gameDataId(game))/L"manifest.json";
    auto currentRows = json::array({row("long","",horizontal),row("tall","p",broken)});
    if (variant=="current-priority") currentRows.push_back(row("hero","_hero",currentHero));
    writeJsonAtomic(currentPath,{{"exe",text(primary)},{"match",{{"appId",1091500}}},{"artwork",currentRows}});
    writeJsonAtomic(oldPath,{{"exe",text(variant=="foreign-exe"?game/L"Other.exe":primary)},
        {"match",{{"appId",variant=="different-store"||variant=="conflicting-cache-id"?2400700:1091500}}},
        {"artwork",json::array({row("tall","p",cover),row("hero","_hero",hero)})}});
    if (variant=="deleted-and-handpicked" || variant=="different-store") {
        const auto item = variant=="different-store" ? json{{"steamId",1091500}} :
            json{{"wallpaper",{{"file",text(currentHero)}}},{"artworkProtection",{{"cover","deleted"}}}};
        writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(primary),item}}}});
    }
    if (variant=="cleared-identity") writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",
        {{"items",{{canonicalPathKey(primary),{{"idCleared",true}}}}}});
    const auto result = refreshExistingSteamArtwork(f.data,scan,f.target,f.data/L"state"/L"refresh.json",true,{canonicalPathKey(directory)});
    check(readBinaryFile(f.target.path)==before && readBinaryFile(grid/L"3221225473.png")==readBinaryFile(horizontal),
        "Alias recovery changed the original card or discarded current artwork");
    const bool recovered = variant=="partial-current" || variant=="current-priority";
    if (recovered) {
        check(result["artworkFilesActivated"]==4 && !fs::exists(grid/L"3221225473p.jpg") &&
            readBinaryFile(grid/L"3221225473p.png")==readBinaryFile(cover),"Current manifest stranded the same EXE's historic portrait");
        check(readBinaryFile(grid/L"3221225473_hero.png")==readBinaryFile(variant=="current-priority"?currentHero:hero),
            "Alias did not fill a missing background or displaced a valid current background");
    } else {
        check(fileText(grid/L"3221225473p.jpg")=="broken Steam portrait" && !fs::exists(grid/L"3221225473p.png"),
            "Alias overrode a deleted choice or took artwork from an unrelated EXE or store selection");
        if (variant=="deleted-and-handpicked")
            check(result["artworkFilesActivated"]==3 && readBinaryFile(grid/L"3221225473_hero.png")==readBinaryFile(currentHero),
                "Alias overrode the manually chosen background");
        else check(result["artworkFilesActivated"]==2 && !fs::exists(grid/L"3221225473_hero.png"),"Foreign alias wrote a background");
    }
}

static void fixtureSteamScriptCardIdentity(const fs::path& p, std::string_view variant) {
    namespace launch = custom_steam_library::launch_file;
    ArtworkRepairFixture f(p, false);
    const auto game = p / L"Game", primary = game / L"bin" / L"Game.exe";
    const bool update = variant.starts_with("update-");
    if (update) variant.remove_prefix(7);
    const bool aliasReverse = variant.ends_with("-alias");
    const bool unrelated = variant == "legacy-unrelated-alias";
    const auto directory = variant == "reverse" || aliasReverse || update ? primary.parent_path() : game;
    const auto oldDirectory = unrelated ? p / L"Other Game" : game;
    const auto scan = fixtureScanState(f), script = launch::wrapperPath(oldDirectory);
    const auto utf8Path = [](const fs::path& path) { return toUtf8(path.wstring()); };
    textFile(primary, "isolated primary executable; never launched");
    auto state = loadJsonDocument(scan);
    auto& gameRow = state["games"][0];
    gameRow["gameDirectory"] = utf8Path(directory);
    gameRow["primaryExecutable"] = utf8Path(primary);
    gameRow["status"] = "ready";
    if (aliasReverse) gameRow["alternateGameDirectories"] = json::array({utf8Path(oldDirectory)});
    const bool exactOwnerConflict = variant == "legacy-exact-contested" || variant == "legacy-exact-unresolved";
    if (variant == "legacy-contested-alias" || exactOwnerConflict) {
        const auto other = game / L"Other.exe";
        textFile(other, "independent game under the old directory; never launched");
        const bool unresolved = variant == "legacy-exact-unresolved";
        state["games"].push_back({{"gameDirectory", utf8Path(game)}, {"primaryExecutable", unresolved ? "" : utf8Path(other)},
            {"status", unresolved ? "needs-primary" : "ready"}});
    }
    writeJsonAtomic(scan, state);

    const bool direct = variant == "direct", legacy = variant.starts_with("legacy");
    const uint32_t id = 3221225473u;
    auto start = oldDirectory;
    if (!legacy && !direct) {
        const auto observedPrimary = variant == "wrong-primary" ? game / L"Other.exe" : primary;
        const json binding{{"schemaVersion", 1}, {"accountId", variant.starts_with("wrong-account") ? "89250578" : f.target.accountId},
            {"shortcutAppId", id}, {"shortcutsVdf", utf8Path(f.target.path)}, {"script", utf8Path(script)},
            {"primaryExecutable", utf8Path(observedPrimary)}};
        const auto bytes = binding.dump();
        start = launch::bindingRoot(f.data) / toWide(sha256(std::vector<unsigned char>(bytes.begin(), bytes.end())));
        textFile(start / L"binding.json", bytes);
    }
    auto cards = BinaryVdfDocument::emptySteamShortcuts();
    auto& rows = vdfFindObject(cards.root(), "shortcuts")->objectValue;
    rows.push_back({"0", steamShortcutNode(-1073741823, "Original Steam card",
        quotedSteamPath(direct ? primary : launch::systemFile(L"wscript.exe")),
        quotedSteamPath(direct ? primary.parent_path() : start), 21, {}, direct ? (update ? "--original" : "") : toUtf8(launch::steamOptions(script)))});
    if (variant == "duplicate") rows.push_back({"1", steamShortcutNode(-1073741822, "Other Steam card",
        quotedSteamPath(primary), quotedSteamPath(primary.parent_path()), 22)});
    const bool duplicateStoredId = variant.starts_with("stored-id-");
    if (duplicateStoredId) rows.push_back({"1", steamShortcutNode(-1073741823,
        variant == "stored-id-hidden" ? "" : "Other Steam card",
        quotedSteamPath(game / L"Other.exe"), quotedSteamPath(game), 22)});
    const auto original = cards.serialize();
    writeAtomic(f.target.path, original);
    const std::set<std::string> selected{canonicalPathKey(directory)};
    if (update) {
        const bool toDirect = direct || variant == "to-direct";
        const bool missing = variant == "missing", unavailable = variant == "unavailable";
        const std::string scriptContent = unavailable ? "" : "' existing script; preserve its exact bytes\r\n";
        if (!direct && !missing) textFile(script, scriptContent);
        writeJsonAtomic(f.data / L"config" / L"manual-overrides.json", {{"items", {{canonicalPathKey(primary),
            {{"launchConfiguration", {{"mode", toDirect ? "direct" : "auto"}}}}}}}});
        const auto commit = [&] { return commitSteamLibraryAddPlan(f.data, scan, f.target, f.planFile, true, selected, true, true); };
        if (variant.starts_with("wrong-account")) {
            mustThrow(commit);
            check(readBinaryFile(f.target.path) == original && fileText(script) == scriptContent && !fs::exists(launch::wrapperPath(directory)),
                "Conflicting account modified a script or Steam card");
            return;
        }
        const auto result = commit();
        const auto after = readBinaryFile(f.target.path);
        const auto shortcuts = shortcutsFromBytes(after, f.target.path, f.target.accountId);
        check(shortcuts.size() == 1 && static_cast<uint32_t>(shortcuts[0].storedAppId) == id, "Launch update changed the original card ID");
        const auto& row = shortcuts[0];
        if (missing || unavailable) {
            check(!result.value("committed", false) && result["summary"]["launchFailures"] == 1 && after == original &&
                (missing ? !fs::exists(script) : fs::exists(script) && fileText(script).empty()), "Missing or unusable original script was replaced");
        } else if (toDirect) {
            check(row.exe == quotedSteamPath(primary) && row.launchOptions == (direct ? "--original" : "") &&
                (direct ? after == original : result.value("committed", false) && fileText(script) == scriptContent),
                "Ordinary launch was modified without opt-in or did not restore the real EXE");
        } else {
            const auto target = toWide(row.exe), startIn = toWide(row.startDir), options = toWide(row.launchOptions);
            check(launch::samePath(launch::steamScriptOperand(target, options), script) && fileText(script) == scriptContent,
                "Update switched away from the original script");
            check(launch::samePath(launch::readPrimaryBinding(f.data, f.target.path, f.target.accountId, id, target, startIn, options, toWide), primary),
                "Original script no longer maps to the game's real EXE");
            check(result.value("committed", false) == legacy, "An already current managed card was rewritten or a legacy binding not repaired");
        }
        check(!fs::exists(launch::wrapperPath(directory)), "Launch update generated a second wrapper under bin");
        check(!commit().value("committed", false) && readBinaryFile(f.target.path) == after, "Repeating launch update wrote Steam again");
        return;
    }
    const auto grid = f.target.path.parent_path() / L"grid", cover = p / L"cover.png";
    const auto old = grid / L"3221225473p.jpg", fresh = grid / L"3221225473p.png";
    textFile(old, "original grid file");
    writeRepairArtwork(cover);
    writeJsonAtomic(f.data / L"config" / L"manual-overrides.json", {{"items", {{canonicalPathKey(primary),
        {{"cover", {{"file", utf8Path(cover)}}}}}}}});
    const auto add = buildSteamLibraryAddPlan(f.data, scan, f.target, f.planFile, selected, true);
    const auto refreshed = refreshExistingSteamArtwork(f.data, scan, f.target, f.data / L"state" / L"refresh.json", true, selected);
    const auto deletionPath = f.data / L"state" / L"delete.json";
    const auto remove = buildSteamLibraryDeletePlan(f.data, scan, f.target, deletionPath, selected);
    const auto addStatus = jsonStringOr(add.at("items").at(0), "status");
    const auto refreshStatus = jsonStringOr(refreshed.at("items").at(0), "status");
    const auto deleteStatus = jsonStringOr(remove.at("items").at(0), "status");
    if (duplicateStoredId) {
        bool deleteRejected = false;
        try { commitSteamLibraryDeletePlan(f.data, scan, f.target, deletionPath, true, selected, true); }
        catch (const std::runtime_error&) { deleteRejected = true; }
        const bool shortcutsPreserved = readBinaryFile(f.target.path) == original;
        const bool coverPreserved = fs::is_regular_file(old) && fileText(old) == "original grid file" && !fs::exists(fresh);
        writeJsonAtomic(p / L"lookup.json", {{"add", addStatus}, {"refresh", refreshStatus}, {"delete", deleteStatus},
            {"deleteRejected", deleteRejected}, {"shortcutsPreserved", shortcutsPreserved}, {"coverPreserved", coverPreserved}});
        check(addStatus == "shortcut-id-conflict" && refreshStatus == "ambiguous-card" && deleteStatus == "ambiguous" &&
            deleteRejected && shortcutsPreserved && coverPreserved && !hasStages(p),
            "An aliased stored app ID authorized membership, repair or delete");
        // Simulate the source being corrected externally; no sticky quarantine.
        rows.pop_back();
        const auto corrected = cards.serialize();
        writeAtomic(f.target.path, corrected);
        const auto recovered = buildSteamLibraryAddPlan(f.data, scan, f.target, f.planFile, selected, true);
        const auto repaired = refreshExistingSteamArtwork(f.data, scan, f.target, f.data / L"state" / L"refresh.json", true, selected);
        const auto recoveredDelete = buildSteamLibraryDeletePlan(f.data, scan, f.target, deletionPath, selected);
        check(recovered["items"][0]["status"] == "already-in-steam" && recovered["items"][0]["shortAppId"] == std::to_string(id) &&
            repaired["artworkFilesActivated"] == 1 && readBinaryFile(fresh) == readBinaryFile(cover) &&
            recoveredDelete["summary"]["readyToDelete"] == 1 && readBinaryFile(f.target.path) == corrected,
            "Resolving the duplicate did not recover the same original card");
        if (variant == "stored-id-hidden") {
            rows.clear();
            writeAtomic(f.target.path, cards.serialize());
            const auto clean = buildSteamLibraryAddPlan(f.data, scan, f.target, f.planFile, selected, true);
            const auto& proposed = clean["items"][0];
            check(proposed["status"] == "ready-to-add", "Fixture cannot derive the proposed ordinary-launch ID");
            rows.push_back({"0", steamShortcutNode(proposed.at("storedAppId").get<int32_t>(), "",
                quotedSteamPath(game / L"Other.exe"), quotedSteamPath(game), 22)});
            const auto occupied = cards.serialize();
            writeAtomic(f.target.path, occupied);
            const auto refused = buildSteamLibraryAddPlan(f.data, scan, f.target, f.planFile, selected, true);
            check(refused["items"][0]["status"] == "shortcut-id-collision", "Hidden row did not reserve the proposed ID");
            mustThrow([&] { commitSteamLibraryAddPlan(f.data, scan, f.target, f.planFile, true, selected, true); });
            check(readBinaryFile(f.target.path) == occupied && !hasStages(p), "Conflicting add changed the hidden row");
        }
        return;
    }
    const bool conflict = variant == "wrong-primary" || variant.starts_with("wrong-account") || variant == "duplicate" ||
        variant == "legacy-contested-alias" || exactOwnerConflict;
    const bool originalPreserved = readBinaryFile(f.target.path) == original;
    writeJsonAtomic(p / L"lookup.json", {{"add", addStatus}, {"refresh", refreshStatus}, {"delete", deleteStatus},
        {"filesActivated", refreshed.value("artworkFilesActivated", 0)}, {"shortcutsPreserved", originalPreserved}});
    check(originalPreserved, "Planning or static repair rewrote shortcut bytes");
    if (unrelated) {
        check(addStatus == "ready-to-add" && refreshStatus == "not-in-steam" && deleteStatus == "not-found" &&
            fileText(old) == "original grid file" && !fs::exists(fresh), "An alias outside the real EXE's directory matched a foreign card");
    } else if (conflict) {
        check(addStatus == "shortcut-id-conflict" && refreshStatus == "ambiguous-card" && deleteStatus == "ambiguous",
            "Add/repair/delete disagree about an ambiguous or invalid binding");
        mustThrow([&] { commitSteamLibraryDeletePlan(f.data, scan, f.target, deletionPath, true, selected, true); });
        check(readBinaryFile(f.target.path) == original && fileText(old) == "original grid file" && !fs::exists(fresh),
            "Binding conflict allowed a Steam write");
        if (exactOwnerConflict) {
            state["games"].erase(1);
            writeJsonAtomic(scan, state);
            const auto recovered = buildSteamLibraryAddPlan(f.data, scan, f.target, f.planFile, selected, true);
            const auto& item = recovered.at("items").at(0);
            check(item["status"] == "already-in-steam" && item["shortAppId"] == std::to_string(id) &&
                readBinaryFile(f.target.path) == original, "Unique inventory did not recover the same original card");
        }
    } else {
        check(addStatus == "already-in-steam" && remove["summary"]["readyToDelete"] == 1 &&
            add["items"][0]["shortAppId"] == std::to_string(id) && remove["items"][0]["shortAppId"] == std::to_string(id) &&
            refreshed.value("artworkFilesActivated", 0) == 1 && !fs::exists(old) && readBinaryFile(fresh) == readBinaryFile(cover),
            "Expected the same original Steam card in add/repair/delete");
        const auto result = commitSteamLibraryDeletePlan(f.data, scan, f.target, deletionPath, true, selected);
        check(result.value("deletedCount", 0) == 1 && readShortcuts(f.target.path, f.target.accountId).empty(),
            "Explicit deletion did not delete the verified original card");
    }
}

static void fixtureSteamProspectiveLaunchOwner(const fs::path& root, bool update) {
    namespace launch = custom_steam_library::launch_file;
    Fixture f(root);
    const auto game = root/L"Game", scan = fixtureScanState(f), primary = game/L"Game.exe", script = launch::wrapperPath(game);
    const auto text = [](const fs::path& p){return toUtf8(p.wstring());};
    auto inventory = loadJsonDocument(scan);
    inventory["games"][0]["status"] = "ready";
    const auto other = game/L"Other.exe";
    textFile(other,"second game; never launched");
    inventory["games"].push_back({{"gameDirectory",text(game)},{"primaryExecutable",update ? "" : text(other)},
        {"status",update ? "needs-primary" : "ready"}});
    writeJsonAtomic(scan,inventory);
    const auto config = f.data/L"config"/L"manual-overrides.json";
    const auto save = [&](const char* mode){writeJsonAtomic(config,{{"items",{{canonicalPathKey(primary),{{"launchConfiguration",{{"mode",mode}}}}}}}});};
    const auto commit = [&]{return commitSteamLibraryAddPlan(f.data,scan,f.target,f.planFile,true,{canonicalPathKey(game)},true,true);};
    uint32_t originalId = 0;
    std::vector<unsigned char> before;
    if (update) {
        save("direct");
        check(commit().value("committed",false)&&!fs::exists(script),"Ordinary card could not be added while VBS owner was ambiguous");
        before = readBinaryFile(f.target.path);
        originalId = static_cast<uint32_t>(shortcutsFromBytes(before,f.target.path,f.target.accountId).at(0).storedAppId);
        textFile(script,"WScript.Quit 7\r\n");
    }
    save("auto");
    const auto denied = commit();
    writeJsonAtomic(root/L"attempt.json",denied);
    check(!denied.value("committed",false)&&denied["summary"]["launchFailures"]==1&&
        !fs::exists(launch::bindingRoot(f.data))&&
        (update ? readBinaryFile(f.target.path)==before&&fileText(script)=="WScript.Quit 7\r\n" :
            !fs::exists(f.target.path)&&!fs::exists(script)),"Worker used an unowned wrapper and changed Steam or launch files");
    inventory["games"].erase(1);
    writeJsonAtomic(scan,inventory);
    check(commit().value("committed",false)&&fs::is_regular_file(script),"Unique owner could not publish its VBS card on retry");
    const auto active = readBinaryFile(f.target.path);
    const auto cards = shortcutsFromBytes(active,f.target.path,f.target.accountId);
    check(cards.size()==1&&(!update||static_cast<uint32_t>(cards[0].storedAppId)==originalId)&&
        launch::samePath(launch::steamScriptOperand(toWide(cards[0].exe),toWide(cards[0].launchOptions)),script),
        "VBS retry duplicated the card, changed the original ID, or used the wrong script");
    inventory["games"].push_back({{"gameDirectory",text(game)},{"primaryExecutable",text(other)},{"status","ready"}});
    writeJsonAtomic(scan,inventory);
    const auto steady = commit();
    check(!steady.value("committed",false)&&steady["summary"]["launchFailures"]==0&&
        steady["items"][0].value("launchAlreadyCurrent",false)&&readBinaryFile(f.target.path)==active,
        "Trusted existing card lost its binding after an ambiguous scan");
}

static void fixtureSteamLauncherCardOverlap(const fs::path& root) {
    Fixture f(root);
    const auto game = root/L"Game", scan = fixtureScanState(f), primary = game/L"Game.exe", launcher = game/L"Launcher.exe";
    textFile(launcher, "fixture launcher executable");
    auto inventory = loadJsonDocument(scan);
    inventory["schemaVersion"] = 1;
    auto& item = inventory["games"][0];
    item.update({{"status","ready"},{"primaryRole","main-game"},{"contentType","game"},{"scanComplete",true},
        {"candidateLimitReached",false},{"uniquePrimaryInvariant",true},{"unrecognizedTool",false},
        {"candidates",json::array({{{"path",toUtf8(primary.wstring())},{"role","main-game"},{"selected",true}},
            {{"path",toUtf8(launcher.wstring())},{"role","launcher"},{"selected",false}}})}});
    writeJsonAtomic(scan,inventory);
    LibraryScanObservation observed;
    const auto links = libraryLauncherPrimaryLinks(loadLibraryScanState(scan,&observed));
    check(observed.complete&&links.size()==1&&links.at(canonicalPathKey(launcher))==toUtf8(primary.wstring()),"Fixture launcher link not confirmed");

    const auto cards = [&](bool direct, bool wrapped) {
        auto document=BinaryVdfDocument::emptySteamShortcuts();
        auto& rows=vdfFindObject(document.root(),"shortcuts")->objectValue;
        if(direct)rows.push_back({"0",steamShortcutNode(-1073741823,"Ordinary card",quotedSteamPath(primary),quotedSteamPath(game),21)});
        if(wrapped)rows.push_back({"1",steamShortcutNode(-1073741822,"Launcher card",quotedSteamPath(launcher),quotedSteamPath(game),22,{"original tag"},"--original")});
        return document.serialize();
    };
    const auto both=cards(true,true);
    writeAtomic(f.target.path,both);
    const std::set<std::string> selected{canonicalPathKey(game)};
    const auto plan=[&]{return buildSteamLibraryAddPlan(f.data,scan,f.target,f.planFile,selected,true);};
    const auto selectFirst=[](const json& result){return result.at("items").at(0);};
    const auto add=selectFirst(plan());
    const auto remove=selectFirst(buildSteamLibraryDeletePlan(f.data,scan,f.target,f.data/L"state"/L"delete.json",selected));
    const auto repair=refreshExistingSteamArtwork(f.data,scan,f.target,f.data/L"state"/L"refresh.json",true,selected);
    const auto grid=f.target.path.parent_path()/L"grid";
    const auto cover=root/L"cover.png";
    writeRepairArtwork(cover);
    const auto config=f.data/L"config"/L"manual-overrides.json";
    writeJsonAtomic(config,{{"items",{{canonicalPathKey(primary),{{"launchConfiguration",{{"mode","auto"}}},
        {"cover",{{"file",toUtf8(cover.wstring())}}}}}}}});
    const auto repairWithCover=refreshExistingSteamArtwork(f.data,scan,f.target,f.data/L"state"/L"refresh-cover.json",true,selected);
    bool updateRejected=false;
    try { commitSteamLibraryAddPlan(f.data,scan,f.target,f.planFile,true,selected,true,true); }
    catch(const std::runtime_error&){updateRejected=true;}
    const auto unchanged=readBinaryFile(f.target.path)==both;
    const auto hasScript=fs::exists(libLaunch::wrapperPath(game));
    writeJsonAtomic(root/L"overlap.json",{{"add",add},{"delete",remove},{"repair",selectFirst(repair)},
        {"repairWithCover",selectFirst(repairWithCover)},{"updateRejected",updateRejected},{"unchanged",unchanged},{"scriptCreated",hasScript}});
    check(add["status"]=="shortcut-id-conflict"&&remove["status"]=="ambiguous"&&selectFirst(repair)["status"]=="ambiguous-card"&&
        selectFirst(repairWithCover)["status"]=="ambiguous-card"&&updateRejected&&unchanged&&!hasScript&&
        !fs::exists(grid/L"3221225473p.png")&&!fs::exists(grid/L"3221225474p.png"),"Two confirmed cards authorized a launch update or lost their conflict status");

    writeAtomic(f.target.path,cards(true,false));
    const auto direct=selectFirst(plan());
    writeAtomic(f.target.path,cards(false,true));
    const auto viaLauncher=selectFirst(plan());
    const auto healedRepair=refreshExistingSteamArtwork(f.data,scan,f.target,f.data/L"state"/L"healed-repair.json",true,selected);
    const auto healedDelete=selectFirst(buildSteamLibraryDeletePlan(f.data,scan,f.target,f.data/L"state"/L"healed-delete.json",selected));
    writeJsonAtomic(root/L"recovered.json",{{"direct",direct},{"launcher",viaLauncher},{"repair",selectFirst(healedRepair)},{"delete",healedDelete}});
    check(direct["status"]=="already-in-steam"&&direct["shortAppId"]=="3221225473"&&viaLauncher["status"]=="already-in-steam"&&
        viaLauncher["shortAppId"]=="3221225474"&&viaLauncher["steamMembershipSource"]=="confirmed-library-launcher"&&
        healedRepair["artworkFilesActivated"]==1&&readBinaryFile(grid/L"3221225474p.png")==readBinaryFile(cover)&&
        healedDelete["shortAppId"]=="3221225474"&&readBinaryFile(f.target.path)==cards(false,true)&&!hasStages(root),
        "Resolving the overlap failed to recover the unique card and artwork slot");
}

static void fixtureSteamOrdinaryLaunchRoundtrip(const fs::path& root, bool twoAccounts, bool staleAlias = false) {
    namespace launch = custom_steam_library::launch_file;
    Fixture f(root);
    const auto game = root/L"Game", scan = fixtureScanState(f), primary = game/L"Game.exe";
    auto inventory = loadJsonDocument(scan);
    inventory["games"][0]["status"] = "ready";
    writeJsonAtomic(scan, inventory);
    const auto config = f.data/L"config"/L"manual-overrides.json";
    const auto writeMode = [&](const char* mode) {
        json items = {{canonicalPathKey(primary),{{"launchConfiguration",{{"mode",mode}}}}}};
        if (staleAlias) {
            auto mixedCase=toUtf8(primary.wstring());
            mixedCase.front()=static_cast<char>(std::toupper(static_cast<unsigned char>(mixedCase.front())));
            check(mixedCase < canonicalPathKey(primary),"Legacy alias must sort before the saved record");
            items[mixedCase]={{"launchConfiguration",{{"mode","auto"}}},{"name","legacy metadata to keep"}};
        }
        writeJsonAtomic(config, {{"items",items}});
    };
    const std::set<std::string> selected{canonicalPathKey(game)};
    const auto commit = [&](const SteamShortcutTarget& target) {
        return commitSteamLibraryAddPlan(f.data, scan, target, f.planFile, true, selected, true, true);
    };
    constexpr uint32_t id = 3221225473u;
    const auto secondary = SteamShortcutTarget{f.target.path.parent_path().parent_path().parent_path()/L"22202"/L"config"/L"shortcuts.vdf", "22202"};
    std::vector<SteamShortcutTarget> targets{f.target};
    if (twoAccounts) targets.push_back(secondary);
    std::vector<SteamShortcut> originals;
    std::vector<std::vector<unsigned char>> enabled;
    writeMode("auto");
    for (const auto& target : targets) {
        auto document = BinaryVdfDocument::emptySteamShortcuts();
        auto& rows = vdfFindObject(document.root(), "shortcuts")->objectValue;
        const auto workingDirectory = game/toWide("Install Data "+target.accountId);
        fs::create_directories(workingDirectory);
        rows.push_back({"0", steamShortcutNode(-1073741823, "Original name", quotedSteamPath(primary),
            target.accountId == "22202" ? "" : quotedSteamPath(workingDirectory), 21, {"original tag"},
            "--profile=\"user "+target.accountId+"\" --original")});
        writeAtomic(target.path, document.serialize());
        originals.push_back(readShortcuts(target.path,target.accountId).at(0));
        const auto update = commit(target);
        const auto row = readShortcuts(target.path,target.accountId).at(0);
        check(update["summary"]["launchUpdates"] == 1 && static_cast<uint32_t>(row.storedAppId) == id &&
            launch::samePath(launch::steamScriptOperand(toWide(row.exe),toWide(row.launchOptions)),launch::wrapperPath(game)),
            "VBS opt-in did not update the original Steam card");
        enabled.push_back(readBinaryFile(target.path));
        check(!commit(target).value("committed", false) && readBinaryFile(target.path) == enabled.back(),
            "A repeated VBS update rewrote the original card");
    }
    if (!twoAccounts && !staleAlias) {
        const auto script=launch::wrapperPath(game);
        check(fs::remove(script),"Fixture could not remove the VBS");
        writeMode("obsolete-launch-mode");
        const auto before=readBinaryFile(config);
        const auto denied=commit(f.target);
        writeJsonAtomic(root/L"missing-legacy-vbs-attempt.json",denied);
        check(!denied.value("committed",false)&&denied["summary"]["launchFailures"]==1&&
            readBinaryFile(f.target.path)==enabled.front()&&readBinaryFile(config)==before&&!fs::exists(script),
            "Unknown mode implicitly replaced the broken VBS or original card");
    }
    writeMode("direct");
    const auto savedConfig=readBinaryFile(config);
    for (size_t i=0;i<targets.size();++i) {
        const auto& target=targets[i];
        const auto result=commit(target);
        const auto rows=readShortcuts(target.path,target.accountId);
        check(result["summary"]["launchUpdates"] == 1 && rows.size() == 1 && static_cast<uint32_t>(rows[0].storedAppId) == id,
            "Returning to ordinary launch did not update the original card");
        const auto& before=originals[i];
        const auto& after=rows[0];
        writeJsonAtomic(root/toWide("ordinary-readback-"+target.accountId+".json"), {{"id",id},{"beforeExe",before.exe},{"afterExe",after.exe},
            {"beforeStartIn",before.startDir},{"afterStartIn",after.startDir},{"beforeOptions",before.launchOptions},{"afterOptions",after.launchOptions}});
        check(after.appName==before.appName && after.exe==before.exe && after.startDir==before.startDir && after.launchOptions==before.launchOptions,
            "Returning from VBS discarded or borrowed ordinary launch fields");
        for (size_t j=i+1;j<targets.size();++j)
            check(readBinaryFile(targets[j].path)==enabled[j],"Restoring one account modified another account's card");
        check(!commit(target).value("committed",false),"Repeating ordinary restore rewrote Steam again");
    }
    check(readBinaryFile(config)==savedConfig,"Steam write changed the legacy manual overrides");
    if (!twoAccounts && !staleAlias) check(!fs::exists(launch::wrapperPath(game)),"Restoring ordinary launch recreated the missing VBS");
}

static void fixtureSteamConflictingLaunchAliases(const fs::path& root) {
    ArtworkRepairFixture f(root, false);
    const auto game=root/L"Game", exe=game/L"Game.exe";
    const auto alias1=toUtf8((game/L"."/L"Game.exe").wstring());
    const auto alias2=toUtf8((game/L"unused"/L".."/L"Game.exe").wstring());
    check(alias1!=alias2&&alias1!=canonicalPathKey(exe)&&alias2!=canonicalPathKey(exe),"Fixture keys are not independent aliases");
    fs::create_directory(game/L"unused");
    const auto path=f.data/L"config"/L"manual-overrides.json";
    writeJsonAtomic(path,{{"items",{
        {alias1,{{"launchConfiguration",{{"mode","auto"}}}}},
        {alias2,{{"launchConfiguration",{{"mode","direct"}}}}}}}});
    f.plan["items"][0]["gameDirectory"]=toUtf8(game.wstring());
    const auto original=readBinaryFile(path);
    check(f.commit().value("committed",false),"Conflicting aliases blocked ordinary import");
    const auto cards=readShortcuts(f.target.path,f.target.accountId);
    check(cards.size()==1&&cards[0].exe==quotedSteamPath(exe)&&cards[0].launchOptions.empty()&&
        !fs::exists(libLaunch::wrapperPath(game))&&readBinaryFile(path)==original,
        "Conflicting legacy aliases activated VBS or rewrote settings");
}

static void fixtureSteamLaunchBindingRepair(const fs::path& root, std::string_view variant) {
    namespace launch = custom_steam_library::launch_file;
    ArtworkRepairFixture f(root);
    const auto game=root/L"Game", primary=game/L"Game.exe", scan=fixtureScanState(f);
    auto inventory=loadJsonDocument(scan);
    inventory["games"][0]["status"]="ready";
    writeJsonAtomic(scan,inventory);
    const auto config=f.data/L"config"/L"manual-overrides.json";
    const std::set<std::string> selection{canonicalPathKey(game)};
    const auto setMode=[&](const char* mode) {
        writeJsonAtomic(config,{{"items",{{canonicalPathKey(primary),{{"launchConfiguration",{{"mode",mode}}}}}}}});
    };
    const auto commit=[&] { return commitSteamLibraryAddPlan(f.data,scan,f.target,f.planFile,true,selection,true,true); };
    auto original=BinaryVdfDocument::emptySteamShortcuts();
    vdfFindObject(original.root(),"shortcuts")->objectValue.push_back({"0",steamShortcutNode(-1073741823,"Original card",
        quotedSteamPath(primary),"",21,{"original tag"},"--profile=\"custom one\"")});
    writeAtomic(f.target.path,original.serialize());
    const auto direct=readShortcuts(f.target.path,f.target.accountId).at(0);
    setMode("auto");
    check(commit()["summary"]["launchUpdates"]==1,"Fixture could not enable VBS on the original card");
    const auto enabled=readShortcuts(f.target.path,f.target.accountId).at(0);
    const auto script=launch::steamScriptOperand(toWide(enabled.exe),toWide(enabled.launchOptions));
    const auto binding=fs::path(launch::unquoted(toWide(enabled.startDir)))/L"binding.json";
    const auto clean=readBinaryFile(binding), scriptBytes=readBinaryFile(script);
    setMode("direct");
    check(commit()["summary"]["launchUpdates"]==1,"Fixture could not restore the original ordinary card");
    const auto ordinary=readBinaryFile(f.target.path);
    const auto fault=variant=="empty"?std::string{}:variant=="foreign"?R"({"schemaVersion":1,"accountId":"89250578"})":R"({"schemaVersion":)";
    const bool shouldRepair=variant=="empty"||variant=="truncated";
    textFile(binding,fault);
    setMode("auto");
    bool editInjected=false;
    const auto backupDir=f.data/L"backups"/L"corrupt";
    const std::string newer=R"({"other":"newer write"})";
    if(variant=="backup-race")selftestMoveFault=[&](const auto&,const auto& to)->DWORD {
        const fs::path path(to);
        if(!editInjected&&path.parent_path()==backupDir&&path.filename().native().starts_with(L"steam-launch-binding-")) {
            editInjected=true;
            textFile(binding,newer);
        }
        return ERROR_SUCCESS;
    };
    json outcome;
    if(variant=="denied")withConfigReadDenied(binding,[&]{mustThrow([&]{readBinaryFile(binding);});outcome=commit();});
    else outcome=commit();
    selftestMoveFault={};
    size_t backups=0;
    if(fs::exists(backupDir))for(const auto& entry:fs::directory_iterator(backupDir)) {
        if(entry.path().filename().native().starts_with(L"steam-launch-binding-")) {
            check(fileText(entry.path())==fault,"Binding recovery did not retain the exact invalid bytes");
            ++backups;
        }
    }
    writeJsonAtomic(root/L"binding-observed.json",{{"outcome",outcome},{"editInjected",editInjected},{"backupCount",backups},
        {"localBindingRecovered",readBinaryFile(binding)==clean},{"ordinaryCardPreserved",readBinaryFile(f.target.path)==ordinary}});
    check(readBinaryFile(script)==scriptBytes,"Binding repair rewrote the existing script");
    if(shouldRepair) {
        const auto row=readShortcuts(f.target.path,f.target.accountId).at(0);
        check(outcome["summary"]["launchUpdates"]==1&&outcome["items"][0]["launchBindingRecovered"]==true&&backups==1&&
            row.storedAppId==direct.storedAppId&&row.startDir==enabled.startDir&&readBinaryFile(binding)==clean,
            "Corrupt binding blocked recoverable VBS reactivation or changed the card identity");
        check(!commit().value("committed",false),"Repeated reactivation rewrote the verified card");
        setMode("direct");
        check(commit()["summary"]["launchUpdates"]==1,"Recovered binding did not support the reverse transition");
        const auto restored=readShortcuts(f.target.path,f.target.accountId).at(0);
        check(restored.storedAppId==direct.storedAppId&&restored.exe==direct.exe&&restored.startDir==direct.startDir&&
            restored.launchOptions==direct.launchOptions,"Binding recovery lost the ordinary launch fields");
    } else {
        check(outcome["summary"]["launchFailures"]==1&&readBinaryFile(f.target.path)==ordinary,
            "An unverified binding changed the Steam shortcut");
        check(variant=="backup-race"?(editInjected&&backups==1&&fileText(binding)==newer):(backups==0&&fileText(binding)==fault),
            "Foreign, unreadable or externally replaced binding was not preserved");
    }
    check(!hasStages(root)&&pendingJournals(f.data).empty(),"Binding repair leaked Steam transaction state");
}

static void fixtureSteamLaunchBindingMirror(const fs::path& root, std::string_view variant) {
    namespace launch=custom_steam_library::launch_file;
    ArtworkRepairFixture f(root);
    const auto game=root/L"Game", primary=game/L"Game.exe", scan=fixtureScanState(f);
    auto inventory=loadJsonDocument(scan);
    inventory["games"][0]["status"]="ready";
    writeJsonAtomic(scan,inventory);
    const auto config=f.data/L"config"/L"manual-overrides.json";
    const std::set<std::string> selection{canonicalPathKey(game)};
    const auto setMode=[&](const char* mode) {
        writeJsonAtomic(config,{{"items",{{canonicalPathKey(primary),{{"launchConfiguration",{{"mode",mode}}}}}}}});
    };
    const auto commit=[&]{return commitSteamLibraryAddPlan(f.data,scan,f.target,f.planFile,true,selection,true,true);};
    auto cards=BinaryVdfDocument::emptySteamShortcuts();
    vdfFindObject(cards.root(),"shortcuts")->objectValue.push_back({"0",steamShortcutNode(-1073741823,"Original card",
        quotedSteamPath(primary),"",21,{"original tag"},"--profile=\"custom one\"")});
    writeAtomic(f.target.path,cards.serialize());
    const auto direct=readShortcuts(f.target.path,f.target.accountId).at(0);
    setMode("auto");
    check(commit()["summary"]["launchUpdates"]==1,"Fixture could not enable VBS");
    const auto originalVdf=readBinaryFile(f.target.path);
    const auto row=readShortcuts(f.target.path,f.target.accountId).at(0);
    const auto start=fs::path(launch::unquoted(toWide(row.startDir)));
    const auto binding=start/L"binding.json", fallback=start/L"binding.recovery.json";
    const auto exact=readBinaryFile(binding);
    const bool created=fs::is_regular_file(fallback) && readBinaryFile(fallback)==exact;
    if(variant=="healthy") {
        if(fs::exists(fallback))fs::remove(fallback);
        const auto result=commit();
        check(created && fs::is_regular_file(fallback) && readBinaryFile(fallback)==exact &&
            result["items"][0]["launchAlreadyCurrent"]==true && readBinaryFile(f.target.path)==originalVdf,
            "Verified opt-in or explicit update did not retain a read-back binding copy");
        return;
    }
    // Seed a known valid copy to isolate the reader on older builds as well.
    writeAtomic(fallback,exact);
    if(variant=="wrong-mirror")textFile(fallback,R"({"schemaVersion":1,"accountId":"89250578"})");
    if(variant=="missing"||variant=="wrong-mirror")fs::remove(binding);
    else if(variant=="foreign")textFile(binding,R"({"schemaVersion":1,"accountId":"89250578"})");
    else textFile(binding,R"({"schemaVersion":)");
    const bool primaryExists=fs::exists(binding);
    const auto originalBinding=primaryExists?readBinaryFile(binding):std::vector<unsigned char>{};
    const bool shouldRecover=variant=="missing"||variant=="truncated";
    const auto lookup=[&](const std::string& account) {
        return launch::readPrimaryBinding(f.data,f.target.path,account,static_cast<uint32_t>(row.storedAppId),
            toWide(row.exe),toWide(row.startDir),toWide(row.launchOptions),toWide);
    };
    fs::path resolved;
    if(variant=="denied")withConfigReadDenied(binding,[&]{mustThrow([&]{readBinaryFile(binding);});resolved=lookup(f.target.accountId);});
    else resolved=lookup(f.target.accountId);
    const auto planned=buildSteamLibraryAddPlan(f.data,scan,f.target,f.planFile,selection,true);
    writeJsonAtomic(root/L"mirror-observed.json",{{"bindingCopyCreated",created},{"resolved",toUtf8(resolved.wstring())},
        {"planStatus",planned["items"][0]["status"]},{"originalVdfSame",readBinaryFile(f.target.path)==originalVdf}});
    check(lookup("89250578").empty(),"Wrong account reused a binding copy");
    check(fs::exists(binding)==primaryExists&&(!primaryExists||readBinaryFile(binding)==originalBinding)&&
        readBinaryFile(f.target.path)==originalVdf,"A read-only lookup or plan wrote the primary binding or shortcut");
    if(!shouldRecover) {
        check(resolved.empty(),"An invalid primary or mirror admitted a game identity");
        return;
    }
    check(launch::samePath(resolved,primary)&&planned["items"][0]["status"]=="already-in-steam",
        "Active VBS card could not be recognized from the exact binding copy");
    const auto result=commit();
    check(result["items"][0]["launchBindingRecovered"]==true&&readBinaryFile(binding)==exact&&
        readBinaryFile(f.target.path)==originalVdf,"Explicit binding recovery rewrote the card or failed read-back");
    setMode("direct");
    // Prove the reverse transition still works from verified evidence if the primary is lost again.
    fs::remove(binding);
    check(commit()["summary"]["launchUpdates"]==1,"Cannot return to ordinary launch from the verified copy");
    const auto restored=readShortcuts(f.target.path,f.target.accountId).at(0);
    check(restored.storedAppId==direct.storedAppId&&restored.exe==direct.exe&&restored.startDir==direct.startDir&&
        restored.launchOptions==direct.launchOptions&&!fs::exists(binding),"Reverse transition lost original fields or rewrote the unused binding");
}

static fs::path fixtureAutomaticIcon(Fixture& f) {
    const auto executable = f.root/L"Game"/L"Game.exe";
    textFile(executable, "fixture executable");
    const auto icon = f.root/L"automatic-icon.png";
    writeRepairArtwork(icon, 93);
    const auto manifest = f.data/L"artwork"/toWide(gameDataId(executable.parent_path()))/L"manifest.json";
    writeJsonAtomic(manifest, {{"exe",toUtf8(executable.wstring())},{"match",json::object()},
        {"artwork",json::array({{{"type","icon"},{"steamSuffix","_icon"},{"ok",true},
            {"provider","steamgriddb"},{"file",toUtf8(icon.wstring())}}})}});
    f.plan["items"][0]["artworkManifest"] = toUtf8(manifest.wstring());
    return icon;
}
static std::string fixtureShortcutIcon(const Fixture& f) {
    const auto root=BinaryVdfReader(readBinaryFile(f.target.path)).parse();
    return jsonStringCaseInsensitive(root["shortcuts"]["0"], "icon");
}
static const std::string validLocal="\"localconfig\" { \"KeepThisSetting\" \"original\" \"RecentLocalPlayedGameIDs\" \"\" }";

// The old void initializer ACKed implicitly; the original caller used no status.
template<class F> static bool dataRootReady(F&& action) {
    if constexpr(std::is_void_v<decltype(action())>) { action(); return true; }
    else return action();
}
static json dataRootManifest(const fs::path& data, int schema=2) {
    return {{"schemaVersion",schema},{"dataRoot",toUtf8(data.wstring())},{"createdAt",uint64_t{1}},
        {"featureName","Custom Steam Library"},{"dataLayoutVersion",2},{"encoding","UTF-8"},
        {"portableReinstallData",true},{"userAnnotation",{{"keep",true}}}};
}
static json dataRootMigration(const fs::path& data,const fs::path& lab,int schema=2) {
    return {{"schemaVersion",schema},{"targetDataRoot",toUtf8(data.wstring())},
        {"sources",json::array({toUtf8((lab/L"Steam大屏").wstring())})},
        {"strategy","copy-missing-files-target-wins"},{"backupDirectory",nullptr},
        {"filesCopied",uint64_t{0}},{"filesSkippedBecauseTargetWins",uint64_t{0}},
        {"bytesCopied",uint64_t{0}},{"symlinksSkipped",uint64_t{0}},
        {"pathNormalization","legacy-data-root-and-artwork-overrides-rewritten"},
        {"completedAt",uint64_t{1}},{"note",{{"retain","legacy annotation"}}}};
}


// Exact shipping caller + existing Store/header persistence, owned paths only.
struct IdentityCallerFixture {
 fs::path data,executable,cache,manifest;std::string exe;ymcc::media::Store store;
 explicit IdentityCallerFixture(const fs::path& path):data(path/L"data"),executable(path/L"games"/L"Owned"/L"Game.exe"),cache(custom_steam_library::identity_cache::cachePath(data,toUtf8(executable.wstring()))),manifest(data/L"cache"/L"jobs"/L"game-media-identity"/L"owned-job"/L"game"/L"manifest.json"),exe(toUtf8(executable.wstring())),store(data){observation();}
 static json match(int64_t id=555){return {{"appId",id},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"},{"formalName","Owned Game"}};}
 void observation(const json& m=match()){writeJsonAtomic(manifest,{{"exe",exe},{"match",m},{"metadata",json::object()}});}
 json record(const json& m=match())const{return {{"schemaVersion",1},{"executable",exe},{"match",m},{"steamAppId",m.value("appId",int64_t{0})},{"metadata",json::object()},{"updatedAt",unixTimeMs()},{"manifestPath",toUtf8(manifest.wstring())}};}
 void save(const json& value){writeJsonAtomic(cache,value);}
 std::optional<json> read()const{return cachedVerifiedSteamIdentity(data,executable);}
 bool observed(const fs::path& path)const{const auto key=identityCallerPath(path.wstring());std::lock_guard<std::mutex> guard(identityCallerReadMx);return std::find(identityCallerReads.begin(),identityCallerReads.end(),key)!=identityCallerReads.end();}
 void fresh(){const auto result=store.rememberIdentity(exe,manifest,ymcc::media::now());check(result.value("verified",false)&&result.value("cachePersisted",false),"Real Store/header observation did not persist");}
};

struct DiscoveryProducerFixture {
    fs::path root,data,lab,games,trainers,job,scan,accounts,steam,exe;
    fs::path previousRoot,previousWorker;std::wstring previousSteam;
    std::string output,error;unsigned callCount=0;
    explicit DiscoveryProducerFixture(const fs::path& p):root(p),data(p/L"data"),lab(p/L"lab"),games(p/L"Games"),trainers(p/L"Trainers"),job(p/L"job"),scan(data/L"state"/L"library-scan.json"),accounts(data/L"state"/L"steam-accounts.json"),steam(p/L"Steam"),exe(p/L"Outside"/L"Game"/L"bin"/L"Game.exe"),previousRoot(custom_steam_library::configuredDataRoot()),previousWorker(g_workerDataRoot),previousSteam(transactionFixtureSteamRoot){
        check(discoveryProducerPorts,"Producer fixture requires inert CLI ports");
        fs::create_directories(games);fs::create_directories(trainers);fs::create_directories(job);fs::create_directories(lab/L"bin");textFile(exe,"owned fake exe, never launched");textFile(steam/L"steam.exe","owned fake Steam marker, never launched");
        SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",data.c_str());g_workerDataRoot=data;transactionFixtureSteamRoot=steam.wstring();selftestSteamInstallationRoot=transactionFixtureSteamRoot;writeJsonAtomic(data/L"data-root.json",dataRootManifest(data));
        auto config=defaultLibraryConfig();config["roots"]=json::array({toUtf8(games.wstring())});config["trainerRoots"]=json::array({toUtf8(trainers.wstring())});config["manualGameDirectories"]=json::array();config["firstRunDefaultScan"]["status"]="completed";config["userProducerNote"]="keep";writeJsonAtomic(data/L"config"/L"library-config.json",config);
        writeAtomic(steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf",BinaryVdfDocument::emptySteamShortcuts().serialize());
    }
    ~DiscoveryProducerFixture(){SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",previousRoot.empty()?nullptr:previousRoot.c_str());g_workerDataRoot=previousWorker;transactionFixtureSteamRoot=previousSteam;selftestSteamInstallationRoot=transactionFixtureSteamRoot;}
    json catalog(int schema=1)const{return {{"schemaVersion",schema},{"games",json::array({{{"gameDirectory",toUtf8(exe.parent_path().parent_path().wstring())},{"primaryExecutable",toUtf8(exe.wstring())},{"status","ready"},{"primaryRole","main-game"},{"selectionSource","manual"},{"scanComplete",true}}})},{"missingGames",json::array()},{"unscannedGames",json::array()},{"roots",json::array()},{"futureUnknown",{{"keep",true}}}};}
    int call(std::vector<std::wstring> tail){std::vector<std::wstring> args{(lab/L"bin"/L"SteamArtworkLab.exe").wstring()};args.insert(args.end(),tail.begin(),tail.end());std::vector<wchar_t*> argv;for(auto& arg:args)argv.push_back(arg.data());std::ostringstream out,err;auto oldOut=std::cout.rdbuf(out.rdbuf()),oldErr=std::cerr.rdbuf(err.rdbuf());int code;try{code=workerMainNotUsedBySelftest(static_cast<int>(argv.size()),argv.data());}catch(...){std::cout.rdbuf(oldOut);std::cerr.rdbuf(oldErr);throw;}std::cout.rdbuf(oldOut);std::cerr.rdbuf(oldErr);output=out.str();error=err.str();json wireArgs=json::array();for(const auto& arg:args)wireArgs.push_back(toUtf8(arg));writeJsonAtomic(root/L"calls"/toWide(std::to_string(++callCount)+".json"),{{"scope","in-process renamed original workerMainNotUsedBySelftest; no product launch"},{"consumerPid",GetCurrentProcessId()},{"args",wireArgs},{"exitCode",code},{"stdout",output},{"stderr",error},{"networkCalls",identityCallerNetwork},{"externalProcessCalls",identityCallerProcesses},{"priorityWriteAttempts",discoveryProducerPriorityWrites},{"automaticDriveDiscovery",discoveryProducerDriveDiscovery}});check(identityCallerNetwork==0&&identityCallerProcesses==0&&discoveryProducerPriorityWrites==0&&discoveryProducerDriveDiscovery==0,"Producer escaped explicit inert ports");return code;}
    int fresh(){return call({L"--scan-library",job.wstring(),L"--root",games.wstring(),L"--trainer-root",trainers.wstring(),L"--max-depth",L"1"});}
    int list(){return call({L"--list-steam-accounts",L"--steam-root",steam.wstring(),L"--output",accounts.wstring()});}
    bool scanAck()const{return output.find("LIBRARY_SCAN_COMMITTED: true")!=std::string::npos;}
    bool accountAck()const{return output.find("ACCOUNTS_WRITE_CONFIRMED: true")!=std::string::npos;}
};
static bool producerArchivedBytes(const fs::path& root,const std::string& expected){std::error_code error;if(!fs::is_directory(root,error))return false;for(const auto& entry:fs::recursive_directory_iterator(root,fs::directory_options::skip_permission_denied,error)){if(error)break;if(entry.is_regular_file(error)&&fileText(entry.path())==expected)return true;}return false;}

#ifdef YMCC_LIBRARY_FACTS_IPC
#include "custom_steam_library_facts_ipc_cases.inc"
#endif

static void bootstrapPeIdentityBoundary(const fs::path& p) {
    const auto input=json::parse(R"BOOTSTRAP([{"name":"actual-paw-studio-product","version":{"CompanyName":"Epic Games, Inc.","FileDescription":"3DClouds","InternalName":"UnrealEngine","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","OriginalFilename":"BootstrapPackagedGame-Win64-Shipping.exe","ProductName":"3DClouds"},"expected":"launcher","pathRole":"main-game","directory":"PAW Patrol Grand Prix Pup Treat Arena"},{"name":"bootstrap-game-title-agrees","version":{"CompanyName":"Epic Games, Inc.","FileDescription":"Owned Game","InternalName":"UnrealEngine","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","OriginalFilename":"BootstrapPackagedGame-Win64-Shipping.exe","ProductName":"Owned Game"},"expected":"launcher","pathRole":"main-game","directory":"Owned Game"},{"name":"bootstrap-case-insensitive-original","version":{"CompanyName":"Epic Games, Inc.","FileDescription":"3DClouds","InternalName":"UnrealEngine","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","OriginalFilename":"BOOTSTRAPPACKAGEDGAME-WIN64-SHIPPING.EXE","ProductName":"3DClouds"},"expected":"launcher","pathRole":"main-game","directory":"Owned Game"},{"name":"missing-original-studio-is-not-new-authority","version":{"CompanyName":"Epic Games, Inc.","FileDescription":"3DClouds","InternalName":"UnrealEngine","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","ProductName":"3DClouds"},"expected":"main-game","pathRole":"main-game","directory":"Owned Game"},{"name":"prefixed-filename-is-not-exact","version":{"CompanyName":"Epic Games, Inc.","FileDescription":"3DClouds","InternalName":"UnrealEngine","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","OriginalFilename":"MyBootstrapPackagedGame-Win64-Shipping.exe","ProductName":"3DClouds"},"expected":"main-game","pathRole":"main-game","directory":"Owned Game"},{"name":"backup-suffix-is-not-exact","version":{"CompanyName":"Epic Games, Inc.","FileDescription":"3DClouds","InternalName":"UnrealEngine","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","OriginalFilename":"BootstrapPackagedGame-Win64-Shipping.exe.bak","ProductName":"3DClouds"},"expected":"main-game","pathRole":"main-game","directory":"Owned Game"},{"name":"truncated-extension-is-not-exact","version":{"CompanyName":"Epic Games, Inc.","FileDescription":"3DClouds","InternalName":"UnrealEngine","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","OriginalFilename":"BootstrapPackagedGame-Win64-Shipping.ex","ProductName":"3DClouds"},"expected":"main-game","pathRole":"main-game","directory":"Owned Game"},{"name":"normal-shipping-game","version":{"CompanyName":"Outright Games Ltd","FileDescription":"Paw Patrol Grand Prix","InternalName":"PAW","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","OriginalFilename":"PAW-Win64-Shipping.exe","ProductName":"Paw Patrol Grand Prix"},"expected":"main-game","pathRole":"main-game","directory":"Owned Game"},{"name":"unreal-internal-name-alone","version":{"ProductName":"Owned Game","FileDescription":"Owned Game","InternalName":"UnrealEngine"},"expected":"main-game","pathRole":"main-game","directory":"Owned Game"},{"name":"legitimate-game-launcher","version":{"ProductName":"Total War: SHOGUN 2","FileDescription":"Total War: SHOGUN 2"},"expected":"main-game","pathRole":"launcher","directory":"Total War - SHOGUN 2"},{"name":"bootstrap-cannot-replace-installer-path","version":{"CompanyName":"Epic Games, Inc.","FileDescription":"3DClouds","InternalName":"UnrealEngine","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","OriginalFilename":"BootstrapPackagedGame-Win64-Shipping.exe","ProductName":"3DClouds"},"expected":"launcher","pathRole":"installer","directory":"Owned Game"},{"name":"bootstrap-cannot-replace-crash-path","version":{"CompanyName":"Epic Games, Inc.","FileDescription":"3DClouds","InternalName":"UnrealEngine","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","OriginalFilename":"BootstrapPackagedGame-Win64-Shipping.exe","ProductName":"3DClouds"},"expected":"launcher","pathRole":"crash-handler","directory":"Owned Game"},{"name":"explicit-tool-description-keeps-role","version":{"CompanyName":"Epic Games, Inc.","FileDescription":"Creation Kit","InternalName":"UnrealEngine","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","OriginalFilename":"BootstrapPackagedGame-Win64-Shipping.exe","ProductName":"Owned Game"},"expected":"tool","pathRole":"main-game","directory":"Owned Game"},{"name":"explicit-installer-product-keeps-role","version":{"CompanyName":"Epic Games, Inc.","FileDescription":"Owned Game","InternalName":"UnrealEngine","LegalCopyright":"Fill out your copyright notice in the Description page of Project Settings.","OriginalFilename":"BootstrapPackagedGame-Win64-Shipping.exe","ProductName":"Setup Installer"},"expected":"installer","pathRole":"main-game","directory":"Owned Game"}])BOOTSTRAP");
    json observed=json::array();bool passed=true;
    for(const auto& row:input){
        const auto value=assessPeIdentity(row["version"].get<std::map<std::string,std::string>>(),row["pathRole"],row["directory"]);
        const auto expected=row["expected"].get<std::string>();
        const bool role=value.role==expected;
        const bool source=expected=="main-game"?value.primaryEvidenceSource=="pe-product-name":value.primaryEvidenceSource=="pe-tool-classification";
        const bool confidence=expected=="main-game"?value.identityConfidence==0.98:value.identityConfidence==0;
        observed.push_back({{"name",row["name"]},{"role",value.role},{"source",value.primaryEvidenceSource},{"confidence",value.identityConfidence},{"expected",expected},{"passed",role&&source&&confidence}});
        passed=passed&&role&&source&&confidence;
    }
    writeJsonAtomic(p/L"bootstrap-pe-observations.json",observed);
    check(passed,"EXACT_UNREAL_BOOTSTRAP_CLASSIFIED_AS_MAIN_GAME");
}

int wmain(int argc,wchar_t** argv) {
    try {
#ifdef YMCC_LIBRARY_HOST_CHAIN
        if(argc>1 && (std::wstring(argv[1])==L"--plan-add-library-to-steam" || std::wstring(argv[1])==L"--commit-add-library-to-steam"))return LibraryFactsIpc::chainCli(argc,argv);
#endif
#ifdef YMCC_LIBRARY_FACTS_IPC
        return LibraryFactsIpc::entry(argc,argv);
#endif
        const bool factsOnly=argc==4&&std::wstring(argv[2])==L"--steam-facts";
        const std::vector<std::string> factsNames={"steamfacts-owned-installation-nesting","steamfacts-legacy-bool-and-owned-installation","steamfacts-absent-commits-real-files","steamfacts-unknown-first-preserves-original","steamfacts-final-unknown-preserves-original","steamfacts-final-running-preserves-original","steamfacts-hook-exception-is-unavailable","steamfacts-missing-whitelist-recovery","delete-success-keeps-backup-receipt","rollback-first-shortcut-restores-absence","rollback-existing-shortcuts-restores-bytes","real-target-guard-does-not-trust-plan-flag"};
        std::string factsSelected;
        if(factsOnly){factsSelected=toUtf8(argv[3]);if(factsSelected.empty()||std::find(factsNames.begin(),factsNames.end(),factsSelected)==factsNames.end())return 2;}
        const bool artworkGridRace=argc==4 && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"alias-grid-race";
        const bool artworkHorizontalSuffix=argc==4 && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"horizontal-suffix";
        const bool artworkManualAlias=argc==4 && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"manual-alias";
        const bool artworkManualWrite=argc==4 && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"manual-write";
        const bool artworkManualCache=argc==4 && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"manual-cache";
        const bool artworkManualScrape=argc==4 && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"manual-scrape";
        const bool artworkSlotRace=argc==4 && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"slot-grid-race";
        const bool artworkSlotVariants=(argc==4||argc==5) && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"slot-variants";
        const auto artworkSlotCase=artworkSlotVariants&&argc==5?toUtf8(argv[4]):std::string{};
        if(artworkSlotVariants&&argc==5&&artworkSlotCase.empty())return 2;
        const bool artworkAlias=artworkGridRace||(argc==4 && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"alias");
        const bool artworkPreimageCase=argc==4 && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"preimage-case";
        const bool artworkPreimageIsolation=argc==4 && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"preimage-isolation";
        const bool artworkPreimage=artworkPreimageCase||artworkPreimageIsolation||(argc==4 && std::wstring_view(argv[2])==L"--artwork-repair" && std::wstring_view(argv[3])==L"preimage");
        const bool artworkRepair=artworkAlias||artworkHorizontalSuffix||artworkManualAlias||artworkManualWrite||artworkManualCache||artworkManualScrape||artworkSlotRace||artworkSlotVariants||artworkPreimage||(argc==3 && std::wstring_view(argv[2])==L"--artwork-repair");
        const bool scriptCardUpdate=argc==4 && std::wstring_view(argv[2])==L"--script-card-identity" && std::wstring_view(argv[3])==L"update";
        const bool scriptCardRoundtrip=argc==4 && std::wstring_view(argv[2])==L"--script-card-identity" && std::wstring_view(argv[3])==L"roundtrip";
        const bool scriptBindingRepair=argc==4 && std::wstring_view(argv[2])==L"--script-card-identity" && std::wstring_view(argv[3])==L"binding-repair";
        const bool scriptBindingMirror=argc==4 && std::wstring_view(argv[2])==L"--script-card-identity" && std::wstring_view(argv[3])==L"binding-mirror";
        const bool scriptLegacyOwner=argc==4 && std::wstring_view(argv[2])==L"--script-card-identity" && std::wstring_view(argv[3])==L"legacy-owner";
        const bool scriptProspectiveOwner=argc==4 && std::wstring_view(argv[2])==L"--script-card-identity" && std::wstring_view(argv[3])==L"prospective-owner";
        const bool scriptLauncherOverlap=argc==4 && std::wstring_view(argv[2])==L"--script-card-identity" && std::wstring_view(argv[3])==L"launcher-overlap";
        const bool scriptCardIdentity=scriptCardUpdate||scriptCardRoundtrip||scriptBindingRepair||scriptBindingMirror||scriptLegacyOwner||scriptProspectiveOwner||scriptLauncherOverlap||(argc==3 && std::wstring_view(argv[2])==L"--script-card-identity");
        const bool snapshotDebt=argc==4 && std::wstring_view(argv[2])==L"--transaction-storm" && std::wstring_view(argv[3])==L"snapshot-debt";
        const bool snapshots=snapshotDebt||(argc==4 && std::wstring_view(argv[2])==L"--transaction-storm" && std::wstring_view(argv[3])==L"snapshot");
        check(artworkRepair || scriptCardIdentity || factsOnly || snapshots || argc==2 || (argc==3 && (std::wstring(argv[2])==L"--config-storm" || std::wstring(argv[2])==L"--transaction-storm" || std::wstring(argv[2])==L"--data-root-storm" || std::wstring(argv[2])==L"--identity-storm" || std::wstring(argv[2])==L"--membership-storm" || std::wstring(argv[2])==L"--rule-text-storm" || std::wstring(argv[2])==L"--publisher-text-storm" || std::wstring(argv[2])==L"--discovery-storm")),"Pass new isolated fixture root"); const auto root=fs::absolute(argv[1]);
        const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
        const auto evidence=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L".."/L"Testing"/L"YMCC"/L"Evidence").lexically_normal();
        check((pathWithin(root,build)||pathWithin(root,evidence))&&!fs::exists(root),"Unsafe or reused fixture root"); fs::create_directories(root);g_workerDataRoot=root;SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",root.c_str());
        transactionFixtureSteamRoot=(root/L"isolated-steam-discovery").wstring();textFile(fs::path(transactionFixtureSteamRoot)/L"steam.exe","isolated marker, never executed");
        selftestSteamInstallationRoot=transactionFixtureSteamRoot;
        SetEnvironmentVariableW(L"YEMAN_GAME_WHITELIST_PATH",(root/L"isolated-whitelist.txt").c_str());SetEnvironmentVariableW(L"YEMAN_PLAYER_BLACKLIST_PATH",(root/L"isolated-blacklist.txt").c_str());
        const bool discoveries=argc==3 && std::wstring(argv[2])==L"--discovery-storm";
        wchar_t producerOnlyValue[8]{};const bool producerOnly=GetEnvironmentVariableW(L"YMCC_DISCOVERY_PRODUCER_ONLY",producerOnlyValue,8)==1&&producerOnlyValue[0]==L'1';discoveryProducerPorts=discoveries&&producerOnly;
        const bool publisherTexts=argc==3 && std::wstring(argv[2])==L"--publisher-text-storm";
        const bool ruleTexts=argc==3 && std::wstring(argv[2])==L"--rule-text-storm";
        const bool memberships=argc==3 && std::wstring(argv[2])==L"--membership-storm";
        const bool identities=argc==3 && std::wstring(argv[2])==L"--identity-storm";identityCallerActive=factsOnly||identities||memberships||ruleTexts||publisherTexts||discoveries;
        const bool dataRoots=argc==3 && std::wstring(argv[2])==L"--data-root-storm";
        const bool transactions=argc==3 && std::wstring(argv[2])==L"--transaction-storm";
        if (artworkManualScrape) { discoveryProducerPorts=true; identityCallerActive=true; }
        json reports=json::array(); size_t failures=0;
        auto run=[&](const char* name,const std::function<void(const fs::path&)>& fn) {
            const std::string label=name;
            if(snapshots&&!label.starts_with("snapshot-")&&!label.starts_with("txstorm-snapshot-"))return;
            if(snapshotDebt&&label!="txstorm-snapshot-restore-cannot-erase-pending-debt")return;
            if(scriptCardIdentity&&!label.starts_with("script-card-identity-"))return;
            if(scriptCardUpdate&&!label.starts_with("script-card-identity-update-"))return;
            if(scriptCardRoundtrip&&!label.starts_with("script-card-identity-roundtrip-"))return;
            if(scriptBindingRepair&&!label.starts_with("script-card-identity-binding-repair-"))return;
            if(scriptBindingMirror&&!label.starts_with("script-card-identity-binding-mirror-"))return;
            if(scriptProspectiveOwner&&!label.starts_with("script-card-identity-prospective-owner-"))return;
            if(scriptLauncherOverlap&&label!="script-card-identity-launcher-overlap")return;
            if(scriptLegacyOwner&&!label.starts_with("script-card-identity-legacy-exact-")&&label!="script-card-identity-legacy-contested-alias"&&
                label!="script-card-identity-legacy"&&label!="script-card-identity-direct")return;
            if(artworkAlias&&!label.starts_with("refresh-artwork-alias-"))return;
            if(artworkManualAlias&&!label.starts_with("refresh-artwork-manual-alias-")&&label!="artwork-add-preserves-existing-grid-and-deleted-selection")return;
            if(artworkManualWrite&&!label.starts_with("artwork-add-manual-write-")&&label!="refresh-artwork-manual-write-recover")return;
            if(artworkManualCache&&!label.starts_with("artwork-slot-manual-cache-"))return;
            if(artworkManualScrape&&!label.starts_with("artwork-slot-manual-scrape-"))return;
            if(artworkGridRace&&!label.starts_with("refresh-artwork-alias-grid-race-"))return;
            if(artworkHorizontalSuffix&&!label.starts_with("refresh-artwork-alias-horizontal-suffix-"))return;
            if(artworkSlotRace&&!label.starts_with("refresh-artwork-slot-grid-race-"))return;
            if(artworkSlotVariants&&(!label.starts_with("refresh-artwork-slot-variants-")||
                (!artworkSlotCase.empty()&&label!=artworkSlotCase)))return;
            if(artworkPreimage&&!label.starts_with("refresh-artwork-preimage-"))return;
            if(artworkPreimageCase&&!label.starts_with("refresh-artwork-preimage-case-"))return;
            if(artworkPreimageIsolation&&!label.starts_with("refresh-artwork-preimage-isolation-"))return;
            if(artworkRepair&&!label.starts_with("refresh-")&&!label.starts_with("automatic-icon-")&&!label.starts_with("artwork-slot-")&&!label.starts_with("artwork-add-")&&
                label!="failure-before-journal-cleans-localconfig-stage"&&label!="partial-artwork-staging-cleans-earlier-files"&&label!="three-manual-artwork-slots-stage-independently")return;
            if(factsOnly&&label!=factsSelected)return;
            if(discoveries&&!label.starts_with("discovery-"))return;
            if(discoveries&&producerOnly&&!label.starts_with("discovery-producer-"))return;
            if(discoveries){wchar_t prefix[256]{};const auto n=GetEnvironmentVariableW(L"YMCC_DISCOVERY_SELFTEST_PREFIX",prefix,256);if(n&&n<256&&!label.starts_with(toUtf8(prefix)))return;}
            if(discoveries){wchar_t selected[256]{};const auto n=GetEnvironmentVariableW(L"YMCC_DISCOVERY_SELFTEST_CASE",selected,256);if(n&&n<256&&label!=toUtf8(selected))return;}
            if(publisherTexts&&!label.starts_with("publishertext-"))return;
            if(publisherTexts){wchar_t selected[256]{};const auto n=GetEnvironmentVariableW(L"YMCC_PUBLISHER_TEXT_SELFTEST_CASE",selected,256);if(n&&n<256&&label!=toUtf8(selected))return;}
            if(ruleTexts&&!label.starts_with("ruletext-"))return;
            if(ruleTexts){wchar_t selected[256]{};const auto n=GetEnvironmentVariableW(L"YMCC_RULE_TEXT_SELFTEST_CASE",selected,256);if(n&&n<256&&label!=toUtf8(selected))return;}
            if(memberships&&!label.starts_with("txstorm-membership-"))return;
            if(memberships){wchar_t selected[256]{};const auto size=GetEnvironmentVariableW(L"YMCC_MEMBERSHIP_SELFTEST_CASE",selected,256);if(size&&size<256&&label!=toUtf8(selected))return;}
            if(identities && !label.starts_with("identitycaller-"))return;
            if(identities){wchar_t selected[256]{};const auto size=GetEnvironmentVariableW(L"YMCC_IDENTITY_CALLER_CASE",selected,256);if(size&&size<256&&label!=toUtf8(selected))return;}
            if(dataRoots && !label.starts_with("dataroot-"))return;
            if(dataRoots){wchar_t selected[256]{};const auto size=GetEnvironmentVariableW(L"YMCC_DATA_ROOT_SELFTEST_CASE",selected,256);if(size&&size<256&&label!=toUtf8(selected))return;}
            if(transactions && !label.starts_with("txstorm-") && !label.starts_with("rollback-") && !label.starts_with("recovery-") && !label.starts_with("snapshot-") && !label.starts_with("pending-recovery-") && label!="failed-rollback-retains-recovery-journal" && label!="delete-failed-rollback-keeps-journal" && label!="refresh-failed-rollback-keeps-journal" && label!="completion-marker-failure-rolls-back")return;
            if(argc==3 && !artworkRepair && !scriptCardIdentity && !transactions && !dataRoots && !identities && !memberships && !ruleTexts && !publisherTexts && !discoveries && !std::string(name).starts_with("storm-") && !std::string(name).starts_with("config-") &&
                std::string(name)!="manual-overrides-invalid-items-recovers-backup" && std::string(name)!="initialization-restores-missing-config-before-defaults" &&
                std::string(name)!="empty-root-first-run-scan-is-retryable") return;
            if(factsOnly){selftestSteamFactsAvailable=true;selftestSteamPresenceOverride=-2;selftestSteamFactsReads=selftestSteamPresenceReads=0;selftestSteamFactsBeforeRead={};}
            const auto caseRoot=root/(L"c"+std::to_wstring(reports.size())); fs::create_directories(caseRoot); selftestMoveFault={}; selftestSteamRunning=false; configIoMode=0;configIoTarget.clear();configIoPartial=false;
            identityCallerReadHook={};ruleTextAfterMove={};{std::lock_guard<std::mutex> guard(identityCallerReadMx);identityCallerReads.clear();}
            try {fn(caseRoot);if(factsOnly||memberships||ruleTexts||publisherTexts||discoveries)check(identityCallerNetwork==0&&identityCallerProcesses==0,"Selected file consumer invoked an external port");reports.push_back({{"name",name},{"passed",true}});}
            catch(const std::exception& e){++failures;reports.push_back({{"name",name},{"passed",false},{"error",e.what()}});}
            selftestMoveFault={};ruleTextAfterMove={}; selftestSteamRunning=false;configIoMode=0;configIoTarget.clear();configIoPartial=false;
        };
        if(factsOnly){
            run("steamfacts-owned-installation-nesting",[](const fs::path& p){
                const auto previous=selftestSteamInstallationRoot;const bool previousPorts=discoveryProducerPorts;discoveryProducerPorts=true;
                try{
                    const auto matches=[](const fs::path& expected){const auto actual=steamInstallPath(true);return actual&&canonicalPathKey(*actual)==canonicalPathKey(expected);};
                    {DiscoveryProducerFixture outer(p/L"outer");
                     check(matches(outer.steam),"Outer producer did not project owned installation");
                     {DiscoveryProducerFixture inner(p/L"inner");check(matches(inner.steam),"Nested producer retained stale installation");}
                     check(matches(outer.steam),"Nested destruction did not restore outer owned installation");}
                    const auto restored=steamInstallPath(true);check(restored&&canonicalPathKey(*restored)==canonicalPathKey(fs::path(previous)),"Producer destruction did not restore previous owned installation");
                }catch(...){discoveryProducerPorts=previousPorts;throw;}discoveryProducerPorts=previousPorts;
                check(identityCallerProcesses==0&&identityCallerNetwork==0,"Owned installation nesting invoked live provider");
                writeJsonAtomic(p/L"facts-observed.json",{{"factsReads",selftestSteamFactsReads},{"restoredOwnedRoot",toUtf8(previous)},{"realProducerInvoked",false}});
            });
            run("steamfacts-legacy-bool-and-owned-installation",[](const fs::path& p){
                const auto install=steamInstallPath(true);check(install&&canonicalPathKey(*install)==canonicalPathKey(fs::path(transactionFixtureSteamRoot)),"Test consumer did not read the explicitly owned installation");
                selftestSteamRunning=false;check(!steamRunning(),"Old false bool no longer controls shared presence");
                selftestSteamRunning=true;check(steamRunning(),"Old true bool no longer controls shared presence");
                const auto facts=ymcc::steaminfo_selftest::read(ymcc::steaminfo::Presence);
                check(facts.available&&facts.processPid==0&&facts.processBirth==0&&facts.actionPowerGeneration==0,"Test presence minted action identity");
                check(selftestSteamPresenceReads>=3&&selftestSteamFactsReads>selftestSteamPresenceReads,"Actual worker bypassed test facts port");
                writeJsonAtomic(p/L"facts-observed.json",{{"factsReads",selftestSteamFactsReads},{"presenceReads",selftestSteamPresenceReads},{"ownedRoot",toUtf8(transactionFixtureSteamRoot)},{"liveProviderUsed",false}});
            });
            run("steamfacts-absent-commits-real-files",[](const fs::path& p){
                Fixture f(p,true);const auto before=readBinaryFile(f.target.path);textFile(f.local,validLocal);const auto localBefore=readBinaryFile(f.local);
                const auto result=f.commit();check(result.value("committed",false)&&readShortcuts(f.target.path,f.target.accountId).size()==1,"Absent test facts did not allow original real-file transaction");
                const auto localAfter=readBinaryFile(f.local);
                const bool keptUnrelated=fileText(f.local).find("\"KeepThisSetting\" \"original\"")!=std::string::npos;
                writeJsonAtomic(p/L"facts-observed.json",{{"presenceReads",selftestSteamPresenceReads},{"targetChanged",readBinaryFile(f.target.path)!=before},{"localChanged",localAfter!=localBefore},{"keptUnrelated",keptUnrelated},{"stagesCleaned",!hasStages(p)},{"result",result}});
                check(readBinaryFile(f.target.path)!=before&&selftestSteamPresenceReads>=2&&!hasStages(p),"Transaction skipped final facts or left staging files");
                check(keptUnrelated&&localAfter!=localBefore&&result.value("recentSignalStatus",std::string{})=="updated", "Expected recent-game update damaged unrelated local config or was not applied");
                check(result.value("localConfigBeforeSha256",std::string{})==sha256(localBefore)&&result.value("localConfigAfterSha256",std::string{})==sha256(localAfter),"Committed local config does not match original transaction hashes");
                check(readBinaryFile(fs::path(toWide(result.at("localConfigBackup").get<std::string>())))==localBefore,"Recent update lost original localconfig backup");
                check(fs::is_regular_file(fs::path(toWide(result.at("transactionReceipt").get<std::string>()))),"Actual complete transaction receipt missing");
            });
            const auto refusal=[&](const fs::path& p,const std::string& mode){
                Fixture f(p,true);textFile(f.local,validLocal);const auto before=readBinaryFile(f.target.path),localBefore=readBinaryFile(f.local);bool staged=false;
                if(mode=="first")selftestSteamFactsAvailable=false;
                if(mode=="exception")selftestSteamFactsBeforeRead=[](std::uint32_t){throw std::runtime_error("controlled facts callback exception");};
                selftestMoveFault=[&](const std::wstring&,const std::wstring& to)->DWORD{
                    if(to.ends_with(L"transaction.pending.json")){
                        staged=true;if(mode=="unknown")selftestSteamPresenceOverride=-1;if(mode=="running")selftestSteamRunning=true;
                    }return ERROR_SUCCESS;
                };
                bool refused=false;std::string error;json result;
                try{result=f.commit();}catch(const std::exception& e){refused=true;error=e.what();}
                selftestMoveFault={};selftestSteamFactsBeforeRead={};
                const bool targetPreserved=readBinaryFile(f.target.path)==before,localPreserved=readBinaryFile(f.local)==localBefore;
                writeJsonAtomic(p/L"facts-observed.json",{{"mode",mode},{"refused",refused},{"error",error},{"staged",staged},
                    {"targetPreserved",targetPreserved},{"localPreserved",localPreserved},{"stagesCleaned",!hasStages(p)},
                    {"presenceReads",selftestSteamPresenceReads},{"factsReads",selftestSteamFactsReads},{"transactionResult",result},{"liveProviderUsed",false}});
                check(refused&&targetPreserved&&localPreserved&&!hasStages(p),"Unknown/running shared facts allowed final Steam activation or damaged original bytes");
                if(mode=="unknown"||mode=="running")check(staged&&selftestSteamPresenceReads>=2,"Late facts case did not reach staged final guard");
                if(mode!="running")check(error.find("[steam-process-unknown]")!=std::string::npos,"Unknown observation became absent or lost original error");
            };
            run("steamfacts-unknown-first-preserves-original",[&](const fs::path& p){refusal(p,"first");});
            run("steamfacts-final-unknown-preserves-original",[&](const fs::path& p){refusal(p,"unknown");});
            run("steamfacts-final-running-preserves-original",[&](const fs::path& p){refusal(p,"running");});
            run("steamfacts-hook-exception-is-unavailable",[&](const fs::path& p){refusal(p,"exception");});
            run("steamfacts-missing-whitelist-recovery",[](const fs::path& p){
                namespace m = custom_steam_library::ymcc_whitelist;
                const auto destination = gameWhitelistPath();
                const auto expected = std::string(m::beginMarker);
                for (const std::string mode : {"missing", "wrong-binding", "no-live-rows", "vdf-changed", "existing-empty", "concurrent-manual"}) {
                    Fixture f(p/toWide(mode), true);
                    const auto scan = fixtureScanState(f);
                    const auto ledger = f.data/L"state"/L"ymcc-game-whitelist-membership.json";
                    const auto observed = f.root/L"Games"/L"OwnedGame.exe";
                    const auto other = f.root/L"Games"/L"SecondOwned.exe";
                    const auto stale = f.root/L"Games"/L"StaleGame.exe";
                    const auto unowned = f.root/L"Games"/L"Unowned.exe";
                    for (const auto& path : {observed, other, stale, unowned}) textFile(path,"not executable");
                    auto vdf = BinaryVdfDocument::emptySteamShortcuts();
                    auto* rows = vdfFindObject(vdf.root(),"shortcuts");
                    if (mode!="no-live-rows") for (const auto& path : {observed, other, unowned})
                        rows->objectValue.push_back({std::to_string(rows->objectValue.size()),steamShortcutNode(-1073741823,toUtf8(path.stem().wstring()),quotedSteamPath(path),quotedSteamPath(path.parent_path()),0)});
                    const auto vdfBytes = vdf.serialize();
                    const auto clearedVdf = BinaryVdfDocument::emptySteamShortcuts().serialize();
                    writeAtomic(f.target.path, vdfBytes);
                    const auto bound = mode=="wrong-binding" ?
                        f.root/L"Foreign"/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf" : f.target.path;
                    json entries = json::object();
                    for (const auto& path : {observed, other, stale}) entries[m::executableKey(toUtf8(path.wstring()))] = normalizeGameRule(toUtf8(path.wstring()));
                    writeJsonAtomic(ledger,{{"schemaVersion",1},{"owner","custom-steam-library"},
                        {"accounts",{{"19627",{{"entries",entries},{"revision",17}}}}},
                        {"bindings",{{"19627",{{"shortcutsVdf",toUtf8(bound.wstring())}}}}}});
                    const auto originalLedger = fileText(ledger);
                    if (mode=="existing-empty") textFile(destination, "");
                    selftestSteamFactsAvailable = false;
                    selftestSteamFactsBeforeRead = [&](std::uint32_t mask){ if(mode=="vdf-changed" && (mask & ymcc::steaminfo::Presence)) writeAtomic(f.target.path, clearedVdf); };
                    bool concurrentWriter = false;
                    selftestMoveFault = [&](const std::wstring&, const std::wstring& to){
                        if(mode=="concurrent-manual" && identityCallerPath(to)==identityCallerPath(destination.wstring()) && !concurrentWriter){
                            concurrentWriter=true;
                            textFile(destination,"manual-race\n");
                        }
                        return DWORD{ERROR_SUCCESS};
                    };
                    std::string error;
                    try { (void)buildSteamLibraryAddPlan(f.data,scan,f.target,f.planFile); }
                    catch(const std::exception& e){ error=e.what(); }
                    selftestMoveFault = {};
                    selftestSteamFactsBeforeRead = {};
                    check(error.find("[steam-process-unknown]")!=std::string::npos && !fs::exists(f.planFile),"Missing whitelist repair loosened the unavailable Steam guard");
                    check(fileText(ledger)==originalLedger && readBinaryFile(f.target.path)==(mode=="vdf-changed" ? clearedVdf : vdfBytes),"Recovery changed Steam bytes or library ownership");
                    if(mode=="missing") {
                        const auto text=fileText(destination);
                        check(text.find(expected)!=std::string::npos && text.find("ownedgame")!=std::string::npos && text.find("secondowned")!=std::string::npos,"Lost derived file did not restore both exact shortcut members");
                        check(text.find("stalegame")==std::string::npos && text.find("unowned")==std::string::npos,"Restore created a rule for an unconfirmed or unowned member");
                    } else if (mode=="existing-empty") check(fileText(destination).empty(),"Present empty rules was overwritten");
                    else if (mode=="concurrent-manual") check(concurrentWriter && fileText(destination)=="manual-race\n","Concurrent manual write not preserved");
                    else check(!fs::exists(destination),"A wrong or changed binding created whitelist rules");
                    std::error_code cleanup;
                    if (fs::exists(destination)) fs::remove(destination,cleanup); // Owned fixture output, reused for the next arm.
                    check(!cleanup && !fs::exists(destination), "Fixture whitelist did not reset");
                }
            });
        }
        run("rollback-first-shortcut-restores-absence",[](const fs::path& p){Fixture f(p);textFile(f.local,validLocal);
            selftestMoveFault=[](const auto& from,const auto& to){return to.ends_with(L"localconfig.vdf")&&from.find(L".yeman-stage-")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{f.commit();}); check(!fs::exists(f.target.path),"Failed first import left an empty shortcuts.vdf");check(fileText(f.local)==validLocal,"Existing config changed");check(!hasStages(p),"Staged files leaked");});
        run("rollback-existing-shortcuts-restores-bytes",[](const fs::path& p){Fixture f(p,true);const auto original=readBinaryFile(f.target.path);textFile(f.local,validLocal);
            selftestMoveFault=[](const auto& from,const auto& to){return to.ends_with(L"localconfig.vdf")&&from.find(L".yeman-stage-")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{f.commit();});check(readBinaryFile(f.target.path)==original,"Original shortcut bytes not restored");check(pendingJournals(p).empty(),"Successful rollback left pending journal");});
        run("failed-rollback-retains-recovery-journal",[](const fs::path& p){Fixture f(p,true);const auto original=readBinaryFile(f.target.path);textFile(f.local,validLocal);
            selftestMoveFault=[](const auto& from,const auto& to){return (to.ends_with(L"localconfig.vdf")&&from.find(L".yeman-stage-")!=std::wstring::npos)||(to.ends_with(L"shortcuts.vdf")&&from.find(L".yeman-stage-")==std::wstring::npos)?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{f.commit();});check(!pendingJournals(p).empty(),"Rollback failure discarded the only recovery journal");selftestMoveFault={};cleanupInterruptedSteamTransactions(f.data);check(readBinaryFile(f.target.path)==original,"Next-open recovery failed");check(pendingJournals(p).empty(),"Recovered journal not consumed");});
        run("script-card-identity-prospective-owner-new",[](const fs::path& p){fixtureSteamProspectiveLaunchOwner(p,false);});
        run("script-card-identity-prospective-owner-update",[](const fs::path& p){fixtureSteamProspectiveLaunchOwner(p,true);});
        run("script-card-identity-launcher-overlap",fixtureSteamLauncherCardOverlap);
        for (const auto* variant : {"direct", "legacy", "valid", "reverse", "wrong-primary", "wrong-account", "duplicate",
                "legacy-confirmed-alias", "wrong-account-reverse-alias", "legacy-contested-alias", "legacy-unrelated-alias",
                "stored-id-visible", "stored-id-hidden", "legacy-exact-contested", "legacy-exact-unresolved"})
            run((std::string("script-card-identity-") + variant).c_str(), [variant](const fs::path& p) { fixtureSteamScriptCardIdentity(p, variant); });
        for (const auto* variant : {"update-reverse", "update-legacy-confirmed-alias", "update-missing", "update-unavailable",
                "update-wrong-account-reverse-alias", "update-direct", "update-to-direct"})
            run((std::string("script-card-identity-") + variant).c_str(), [variant](const fs::path& p) { fixtureSteamScriptCardIdentity(p, variant); });
        run("script-card-identity-roundtrip-single", [](const fs::path& p) { fixtureSteamOrdinaryLaunchRoundtrip(p, false); });
        run("script-card-identity-roundtrip-two-accounts", [](const fs::path& p) { fixtureSteamOrdinaryLaunchRoundtrip(p, true); });
        run("script-card-identity-roundtrip-stale-alias", [](const fs::path& p) { fixtureSteamOrdinaryLaunchRoundtrip(p, false, true); });
        run("script-card-identity-roundtrip-conflicting-aliases", fixtureSteamConflictingLaunchAliases);
        for(const auto* variant:{"empty","truncated","foreign","denied","backup-race"})
            run((std::string("script-card-identity-binding-repair-")+variant).c_str(),[variant](const fs::path& p){fixtureSteamLaunchBindingRepair(p,variant);});
        for(const auto* variant:{"healthy","missing","truncated","foreign","wrong-mirror","denied"})
            run((std::string("script-card-identity-binding-mirror-")+variant).c_str(),[variant](const fs::path& p){fixtureSteamLaunchBindingMirror(p,variant);});
        run("failure-before-journal-cleans-localconfig-stage",[](const fs::path& p){ArtworkRepairFixture f(p,false);textFile(f.local,validLocal);
            bool hit=false;selftestMoveFault=[&](const auto&,const auto& to){if(to.find(L"shortcuts.vdf.yeman-stage-")==std::wstring::npos)return DWORD{0};hit=true;return DWORD{ERROR_WRITE_FAULT};};
            mustThrow([&]{f.commit();});check(hit&&!hasStages(p),"Preparation failure leaked localconfig stage");check(!fs::exists(f.target.path)&&fileText(f.local)==validLocal,"Preparation modified Steam files");});
        run("partial-artwork-staging-cleans-earlier-files",[](const fs::path& p){ArtworkRepairFixture f(p,false);const auto cover=p/L"cover.png", hero=p/L"hero.png";writeRepairArtwork(cover);writeRepairArtwork(hero);
            f.plan["items"][0]["manualCoverPath"]=toUtf8(cover.wstring());f.plan["items"][0]["manualWallpaperPath"]=toUtf8(hero.wstring());
            bool coverStaged=false, hit=false;selftestMoveFault=[&](const auto&,const auto& to){if(to.find(L"3221225473p.png.yeman-stage-")!=std::wstring::npos)coverStaged=true;if(to.find(L"3221225473_hero.png.yeman-stage-")==std::wstring::npos)return DWORD{0};hit=true;return DWORD{ERROR_WRITE_FAULT};};
            mustThrow([&]{f.commit();});check(hit&&coverStaged&&!hasStages(p)&&!fs::exists(f.target.path),"Earlier artwork stage leaked after later write failure");});
        run("completion-marker-failure-rolls-back",[](const fs::path& p){Fixture f(p);selftestMoveFault=[](const auto&,const auto& to){return to.ends_with(L"transaction.complete.json")?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{f.commit();});check(!fs::exists(f.target.path),"Completion marker failure left an empty shortcut file");});
        run("recovery-keeps-journal-if-backup-missing",[](const fs::path& p){Fixture f(p,true);const auto journal=f.data/L"backups"/L"steam-shortcuts"/L"19627"/L"1"/L"transaction.pending.json";
            writeJsonAtomic(journal,{{"schemaVersion",1},{"targetPath",toUtf8(f.target.path.wstring())},{"targetExisted",true},{"targetBackup",toUtf8((journal.parent_path()/L"missing.before.vdf").wstring())},{"localConfigPath",nullptr},{"artworkTargets",json::array()},{"stagedFiles",json::array()}});
            cleanupInterruptedSteamTransactions(f.data);check(fs::exists(journal),"Missing backup was treated as successful recovery");});
        run("recovery-rejects-unrelated-target",[](const fs::path& p){Fixture f(p);const auto outside=p/L"unrelated-user-file.txt";textFile(outside,"do not modify");const auto journal=f.data/L"backups"/L"steam-shortcuts"/L"19627"/L"1"/L"transaction.pending.json";const auto backup=journal.parent_path()/L"shortcuts.before.vdf";textFile(backup,"overwritten");
            writeJsonAtomic(journal,{{"schemaVersion",1},{"targetPath",toUtf8(outside.wstring())},{"targetExisted",true},{"targetBackup",toUtf8(backup.wstring())},{"localConfigPath",nullptr},{"artworkTargets",json::array()},{"stagedFiles",json::array()}});
            cleanupInterruptedSteamTransactions(f.data);check(fileText(outside)=="do not modify","Untrusted journal overwrote unrelated data");check(fs::exists(journal),"Unsafe journal was discarded");});
        run("cleanup-preserves-unowned-temp-and-snapshots",[](const fs::path& p){textFile(p/L"config"/L"user-notes.tmp","owned by user");textFile(p/L"backups"/L"snapshots"/L"1"/L"config"/L"library-config.json.tmp","immutable snapshot");cleanupInterruptedTemporaryFiles(p);
            check(fs::exists(p/L"config"/L"user-notes.tmp"),"Cleanup deleted arbitrary user temp");check(fs::exists(p/L"backups"/L"snapshots"/L"1"/L"config"/L"library-config.json.tmp"),"Cleanup edited immutable backup");});
        run("same-account-in-two-installations-not-collapsed",[](const fs::path& p){const auto a=p/L"SteamA",b=p/L"SteamB";fs::create_directories(a/L"userdata"/L"19627");fs::create_directories(b/L"userdata"/L"19627");check(steamAccountSources({a,b}).size()==2,"Same account ID collapsed across Steam roots");});
        run("config-null-status-preserves-roots",[](const fs::path& p){auto config=defaultLibraryConfig();config["roots"]=json::array({"Z:\\Games"});config["firstRunDefaultScan"]["status"]=nullptr;const auto path=p/L"config"/L"library-config.json";writeJsonAtomic(path,config);
            check(loadLibraryConfig(path)["roots"]==config["roots"],"Null optional status reset unrelated roots");});
        run("manual-overrides-invalid-items-recovers-backup",[](const fs::path& p){const auto path=p/L"config"/L"manual-overrides.json";writeJsonAtomic(path,{{"items",nullptr}});writeJsonAtomic(fs::path(path.wstring()+L".bak"),{{"items",{{"game.exe",{{"name","retained"}}}}}});
            check(loadManualOverrides(path)["items"].contains("game.exe"),"Invalid items silently erased usable manual edits");});
        run("config-recovery-prefers-newest-valid-candidate",[](const fs::path& p){const auto path=p/L"config"/L"library-config.json";textFile(path,"bad");const auto adjacent=fs::path(path.wstring()+L".bak"),latest=p/L"backups"/L"config"/L"library-config-9999999999999-10.json";
            auto old=defaultLibraryConfig(),fresh=old;old["roots"]=json::array({"Old"});fresh["roots"]=json::array({"New"});writeJsonAtomic(adjacent,old);writeJsonAtomic(latest,fresh);fs::last_write_time(adjacent,fs::file_time_type::clock::now()-std::chrono::hours(1));
            check(loadLibraryConfig(path)["roots"]==fresh["roots"],"Lexical filename sorting selected stale configuration");});
        run("scan-corrupt-primary-falls-back-to-bak",[](const fs::path& p){const auto path=p/L"state"/L"library-scan.json";textFile(path,"bad JSON");writeJsonAtomic(fs::path(path.wstring()+L".bak"),{{"games",json::array()}});check(loadLibraryScanState(path)["games"].is_array(),"Scan state was not recovered");});
        run("scan-invalid-rows-do-not-crash-all-games",[](const fs::path& p){const auto path=p/L"state"/L"library-scan.json";writeJsonAtomic(path,{{"games",json::array({nullptr,7,{{"gameDirectory","Z:\\Games\\Good"},{"primaryExecutable",nullptr},{"status",nullptr}}})}});const auto state=loadLibraryScanState(path);
            check(state["games"].size()==1&&state["games"][0]["primaryExecutable"].is_string(),"Malformed game records reached destructive planner");});
        run("retry-invalid-jobs-recovers-backup",[](const fs::path& p){const auto path=networkRetryQueuePath(p);writeJsonAtomic(path,{{"jobs",nullptr}});writeJsonAtomic(fs::path(path.wstring()+L".bak"),{{"jobs",{{"retained",{{"status","pending"}}}}}});
            check(loadNetworkRetryQueue(p)["jobs"].contains("retained"),"Invalid retry jobs silently discarded backup queue");});
        run("retry-null-entry-is-quarantined",[](const fs::path& p){writeJsonAtomic(networkRetryQueuePath(p),{{"jobs",{{"bad",nullptr},{"good",{{"status","pending"}}}}}});const auto q=loadNetworkRetryQueue(p);check(!q["jobs"].contains("bad")&&q["jobs"].contains("good"),"Malformed retry job breaks later wake processing");});
        run("snapshot-roundtrip-retains-config-and-assets",[](const fs::path& p){textFile(p/L"config"/L"a.json","old");textFile(p/L"artwork"/L"image.png","asset");const auto snap=createPortableBackupSnapshot(p,L"snapshots","test");check(verifyPortableBackupSnapshot(snap)["verifiedFiles"]==2,"Incorrect backup inventory");textFile(p/L"config"/L"a.json","new");restorePortableBackupSnapshot(p,snap);check(fileText(p/L"config"/L"a.json")=="old","Restore did not recover original config");});
        run("snapshot-legacy-preserves-live-steam-state",[](const fs::path& p){
            const auto binding=libLaunch::bindingRoot(p)/L"current", receipt=p/L"state"/L"last-steam-add.json", note=p/L"state"/L"note.json";
            textFile(note,"old");
            const auto snap=createPortableBackupSnapshot(p,L"snapshots","legacy-no-authorization");
            textFile(note,"new");textFile(receipt,"current receipt");
            textFile(binding/L"binding.json","primary bytes");
            textFile(binding/libLaunch::bindingRecoveryFileName,"mirror bytes");
            restorePortableBackupSnapshot(p,snap);
            check(fileText(note)=="old","Ordinary state was not restored");
            check(fs::exists(binding/L"binding.json") && fs::exists(binding/libLaunch::bindingRecoveryFileName),"Legacy backup erased current VBS binding or its recovery copy");
            check(fileText(binding/L"binding.json")=="primary bytes" && fileText(binding/libLaunch::bindingRecoveryFileName)=="mirror bytes","Legacy backup altered current VBS binding or its recovery copy");
            check(fileText(receipt)=="current receipt","Legacy backup erased a current Steam receipt");
        });
        for (const bool current : {false,true}) run(current?"snapshot-stale-binding-cannot-replace-current":"snapshot-stale-binding-cannot-resurrect",[current](const fs::path& p){
            const auto data=p/L"data",binding=libLaunch::bindingRoot(data)/L"game";
            textFile(binding/L"binding.json","old primary");
            textFile(binding/libLaunch::bindingRecoveryFileName,"old mirror");
            textFile(data/L"config"/L"a.json","old config");
            const auto snap=createPortableBackupSnapshot(data,L"snapshots","historical-binding");
            textFile(data/L"config"/L"a.json","new config");
            fs::remove(binding/L"binding.json");
            if(current)textFile(binding/libLaunch::bindingRecoveryFileName,"current mirror");
            else {fs::remove(binding/libLaunch::bindingRecoveryFileName);fs::remove(binding);fs::remove(binding.parent_path());}
            restorePortableBackupSnapshot(data,snap);
            check(fileText(data/L"config"/L"a.json")=="old config","Portable config was not restored");
            check(!fs::exists(binding/L"binding.json"),"Restore revived a historical primary binding");
            if(current)check(fileText(binding/libLaunch::bindingRecoveryFileName)=="current mirror","Current recoverable state was overwritten");
            else check(!fs::exists(binding.parent_path()),"Restore adopted a historical Steam binding on an unbound installation");
            check(fileText(libLaunch::bindingRoot(snap)/L"game"/L"binding.json")=="old primary","Restore edited the archive");
        });
        run("snapshot-rejects-tampered-content",[](const fs::path& p){textFile(p/L"config"/L"a.json","original");const auto snap=createPortableBackupSnapshot(p,L"snapshots","test");textFile(snap/L"config"/L"a.json","tampered");mustThrow([&]{verifyPortableBackupSnapshot(snap);});});
        run("snapshot-rejects-inconsistent-count",[](const fs::path& p){textFile(p/L"config"/L"a.json","original");const auto snap=createPortableBackupSnapshot(p,L"snapshots","test");auto m=loadJsonDocument(snap/L"snapshot-manifest.json");m["fileCount"]=99;writeJsonAtomic(snap/L"snapshot-manifest.json",m);mustThrow([&]{verifyPortableBackupSnapshot(snap);});});
        run("snapshot-rejects-duplicate-path",[](const fs::path& p){textFile(p/L"config"/L"a.json","original");const auto snap=createPortableBackupSnapshot(p,L"snapshots","test");auto m=loadJsonDocument(snap/L"snapshot-manifest.json");m["files"].push_back(m["files"][0]);m["fileCount"]=2;m["totalBytes"]=16;writeJsonAtomic(snap/L"snapshot-manifest.json",m);mustThrow([&]{verifyPortableBackupSnapshot(snap);});});
        run("snapshot-rejects-extra-unlisted-file",[](const fs::path& p){textFile(p/L"config"/L"a.json","original");const auto snap=createPortableBackupSnapshot(p,L"snapshots","test");textFile(snap/L"config"/L"extra.json","unlisted");mustThrow([&]{verifyPortableBackupSnapshot(snap);});});
        for(const auto& unsafe:{L"..\\escape.json",L"\\root-relative.json",L"C:drive-relative.json",L"config\\file.json:ads",L"config\\.\\alias.json"}) {
            const auto name="unsafe-relative-path-"+std::to_string(reports.size());run(name.c_str(),[&](const fs::path&){check(!safeSnapshotRelativePath(fs::path(unsafe)),"Unsafe Windows path accepted");});
        }
        run("snapshot-junction-source-is-not-followed",[](const fs::path& p){const auto data=p/L"data",outside=p/L"outside";textFile(outside/L"secret.json","not library data");fs::create_directories(data/L"config");
            const auto link=data/L"config"/L"external"; const DWORD flags=SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
            check(createFixtureJunction(link,outside),"Unable to create junction fixture");mustThrow([&]{createPortableBackupSnapshot(data,L"snapshots","symlink-test");});});

        run("delete-completion-failure-rolls-back",[](const fs::path& p){Fixture f(p);f.commit();const auto original=readBinaryFile(f.target.path);
            selftestMoveFault=[](const auto&,const auto& to){return to.ends_with(L"transaction.complete.json")?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{fixtureDelete(f);});check(readBinaryFile(f.target.path)==original,"Deletion completion failure lost original shortcuts");check(!hasStages(p),"Delete stages leaked");});
        run("delete-failed-rollback-keeps-journal",[](const fs::path& p){Fixture f(p);f.commit();const auto original=readBinaryFile(f.target.path);
            selftestMoveFault=[](const auto& from,const auto& to){return to.ends_with(L"transaction.complete.json")||(to.ends_with(L"shortcuts.vdf")&&from.find(L".yeman-delete-stage-")==std::wstring::npos)?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{fixtureDelete(f);});check(!pendingJournals(f.data).empty(),"Deletion lost its recovery journal");selftestMoveFault={};cleanupInterruptedSteamTransactions(f.data);check(readBinaryFile(f.target.path)==original,"Deletion did not recover on reopen");});
        run("import-result-publication-failure-is-not-false-failure",[](const fs::path& p){Fixture f(p);selftestMoveFault=[](const auto&,const auto& to){return to.ends_with(L"last-steam-add.json")?ERROR_WRITE_FAULT:0;};
            const auto result=f.commit();check(result.value("committed",false),"Durable commit was reported as failure");check(readShortcuts(f.target.path,f.target.accountId).size()==1,"Committed shortcut missing");});
        run("refresh-completion-failure-restores-old-image",[](const fs::path& p){ArtworkRepairFixture f(p);const auto old=f.target.path.parent_path()/L"grid"/L"3221225473p.jpg";textFile(old,"old cover");
            selftestMoveFault=[](const auto&,const auto& to){return to.ends_with(L"transaction.complete.json")?ERROR_WRITE_FAULT:0;};mustThrow([&]{fixtureRefresh(f);});
            check(fs::exists(old)&&fileText(old)=="old cover","Image replacement did not roll back");check(!fs::exists(old.parent_path()/L"3221225473p.png"),"Failed replacement left new image");});
        run("refresh-failed-rollback-keeps-journal",[](const fs::path& p){ArtworkRepairFixture f(p);const auto old=f.target.path.parent_path()/L"grid"/L"3221225473p.jpg";textFile(old,"old cover");
            selftestMoveFault=[](const auto&,const auto& to){return to.ends_with(L"transaction.complete.json")||(to.ends_with(L"3221225473p.jpg")&&to.find(L"\\config\\grid\\")!=std::wstring::npos)?ERROR_WRITE_FAULT:0;};mustThrow([&]{fixtureRefresh(f);});
            check(!pendingJournals(f.data).empty(),"Image refresh lost recovery journal");selftestMoveFault={};cleanupInterruptedSteamTransactions(f.data);check(fileText(old)=="old cover","Interrupted image refresh did not restore old image");});

        run("scan-missing-primary-recovers-valid-backup",[](const fs::path& p){const auto state=p/L"state"/L"library-scan.json";writeJsonAtomic(fs::path(state.wstring()+L".bak"),{{"games",json::array()}});check(loadLibraryScanState(state)["games"].is_array(),"Missing primary did not recover backup");});
        run("scan-duplicate-and-malformed-rows-sanitized",[](const fs::path& p){const auto dir=toUtf8((p/L"Game").wstring());const auto clean=validateLibraryScanState({{"games",json::array({{{"gameDirectory",dir},{"status",17},{"contentType",nullptr},{"steamNative","false"}},{{"gameDirectory",dir},{"status","ready"}}})}});check(clean["games"].size()==1,"Duplicate game directory survived");check(clean["games"][0]["status"]=="data-invalid-review-required","Malformed game is importable");});
        run("scan-invalid-lifecycle-inventory-rejected",[](const fs::path&){mustThrow([&]{validateLibraryScanState({{"games",json::array()},{"missingGames",7}});});});
        run("scan-offline-root-is-not-complete",[](const fs::path& p){const auto root=p/L"offline";const auto config=defaultLibraryConfig();const auto report=scanLibraryRoots({root},{},2,config);check(!report["roots"][0].value("scanComplete",true),"Offline root reported complete");check(!libraryScanCoversGame(report,config,root/L"Game"),"Offline games would be marked missing");});
        run("scan-empty-online-root-is-complete",[](const fs::path& p){const auto root=p/L"Games";fs::create_directories(root);const auto config=defaultLibraryConfig();const auto report=scanLibraryRoots({root},{},2,config);check(report["roots"][0].value("scanComplete",false),"Empty healthy root is incomplete");check(libraryScanCoversGame(report,config,root/L"RemovedGame"),"Genuinely deleted game cannot be detected");});
        run("scan-partial-overlapping-root-retains-old-game",[](const fs::path& p){const auto root=p/L"Games";const json report{{"roots",json::array({{{"path",toUtf8(root.wstring())},{"scanComplete",true},{"status","scanned"}},{{"path",toUtf8((root/L"Nested").wstring())},{"scanComplete",false},{"status","scan-incomplete"}}})}};check(!libraryScanCoversGame(report,defaultLibraryConfig(),root/L"Nested"/L"Game"),"Partial overlapping root incorrectly marks game missing");});
        run("scan-manual-unavailable-is-not-missing",[](const fs::path& p){const auto dir=p/L"Game";auto config=defaultLibraryConfig();config["manualGameDirectories"]=json::array({{{"path",toUtf8(dir.wstring())}}});const json report{{"roots",json::array()},{"skippedDirectories",json::array({{{"path",toUtf8(dir.wstring())},{"reason","manual-game-scan-unavailable"}}})}};check(!libraryScanCoversGame(report,config,dir),"Unavailable manual directory would be marked missing");});
        run("pending-recovery-blocks-second-import",[](const fs::path& p){Fixture f(p,true);textFile(f.local,validLocal);selftestMoveFault=[](const auto& from,const auto& to){return (to.ends_with(L"localconfig.vdf")&&from.find(L".yeman-stage-")!=std::wstring::npos)||(to.ends_with(L"shortcuts.vdf")&&from.find(L".yeman-stage-")==std::wstring::npos)?ERROR_WRITE_FAULT:0;};mustThrow([&]{f.commit();});selftestMoveFault={};const auto before=readBinaryFile(f.target.path);mustThrow([&]{f.commit();});check(readBinaryFile(f.target.path)==before,"New import overwrote unresolved recovery");});
        run("rapid-transactions-use-distinct-directories",[](const fs::path& p){std::set<std::string> seen;for(int n=0;n<100;n++)check(seen.insert(toUtf8(steamTransactionDirectory(p,"19627").wstring())).second,"Same-millisecond backup collision");});
        run("refresh-success-replaces-extension",[](const fs::path& p){ArtworkRepairFixture f(p);const auto old=f.target.path.parent_path()/L"grid"/L"3221225473p.jpg";textFile(old,"old cover");const auto shortcuts=readBinaryFile(f.target.path);const auto result=fixtureRefresh(f);check(result.value("committed",false),"Refresh not committed");check(!fs::exists(old)&&readBinaryFile(old.parent_path()/L"3221225473p.png")==readBinaryFile(p/L"new-cover.png"),"Old extension was not replaced");check(readBinaryFile(f.target.path)==shortcuts,"Artwork refresh rewrote shortcut bytes");});
        run("refresh-manual-horizontal-cover-is-independent",[](const fs::path& p){ArtworkRepairFixture f(p);const auto state=fixtureScanState(f),image=p/L"long.png";writeRepairArtwork(image);writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(p/L"Game"/L"Game.exe"),{{"long",{{"file",toUtf8(image.wstring())}}}}}}}});const auto result=refreshExistingSteamArtwork(f.data,state,f.target,f.data/L"state"/L"refresh.json",true,{canonicalPathKey(p/L"Game")});check(result.value("artworkFilesActivated",0)==2,"Horizontal cover did not populate both Steam IDs");check(readBinaryFile(f.target.path.parent_path()/L"grid"/L"3221225473.png")==readBinaryFile(image),"Manual horizontal cover ignored");});
        run("refresh-vbs-legacy-card-and-direct-card-use-their-own-ids",[](const fs::path& p) {
            Fixture f(p); f.commit();
            const auto state=fixtureScanState(f), cover=p/L"saved-cover.png", portable=p/L"broken-portable.png";
            const auto grid=f.target.path.parent_path()/L"grid", old=grid/L"3221225473p.jpg";
            const auto script=libLaunch::wrapperPath(p/L"Game");
            auto before=BinaryVdfDocument::emptySteamShortcuts();
            vdfFindObject(before.root(),"shortcuts")->objectValue.push_back({"0",steamShortcutNode(-1073741823,"old card",
                quotedSteamPath(libLaunch::systemFile(L"wscript.exe")),quotedSteamPath(p),17,{"retain"},toUtf8(libLaunch::steamOptions(script)))});
            writeAtomic(f.target.path,before.serialize());
            textFile(old,"corrupt old jpg");textFile(grid/L"3221225473p.png","conflicting old png");
            textFile(grid/L"4258967860p.jpg","another card");
            writeRepairArtwork(cover);textFile(portable,"corrupt choice copy");
            writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(p/L"Game"/L"Game.exe"),
                {{"cover",{{"file",toUtf8(cover.wstring())},{"portableFile",toUtf8(portable.wstring())}}}}}}}});
            const auto output=f.data/L"state"/L"refresh.json";
            const std::set<std::string> selected{canonicalPathKey(p/L"Game")};
            const auto repaired=refreshExistingSteamArtwork(f.data,state,f.target,output,true,selected);
            check(repaired.value("artworkFilesActivated",0)==1&&!fs::exists(old)&&readBinaryFile(grid/L"3221225473p.png")==readBinaryFile(cover),"VBS card kept stale artwork or did not recover saved selection");
            check(fileText(grid/L"4258967860p.jpg")=="another card"&&readBinaryFile(f.target.path)==before.serialize(),"Repair changed another card or launch settings");
            const SteamShortcutTarget secondary{p/L"secondary"/L"userdata"/L"42"/L"config"/L"shortcuts.vdf","42"};
            auto direct=BinaryVdfDocument::emptySteamShortcuts();
            vdfFindObject(direct.root(),"shortcuts")->objectValue.push_back({"0",steamShortcutNode(-1073741822,"direct card",
                quotedSteamPath(p/L"Game"/L"Game.exe"),quotedSteamPath(p/L"Game"),0)});
            writeAtomic(secondary.path,direct.serialize());
            const auto filled=refreshExistingSteamArtwork(f.data,state,secondary,output,true,selected);
            check(filled.value("artworkFilesActivated",0)==1&&readBinaryFile(secondary.path.parent_path()/L"grid"/L"3221225474p.png")==readBinaryFile(cover),"Second account was not repaired with its real card ID");
            check(readBinaryFile(secondary.path)==direct.serialize(),"Ordinary card launch configuration changed");
        });
        run("refresh-empty-account-does-not-block-other-accounts",[](const fs::path& p) {
            ArtworkRepairFixture f(p);const auto scan=fixtureScanState(f), output=f.data/L"state"/L"refresh.json";
            const SteamShortcutTarget absent{p/L"secondary"/L"userdata"/L"42"/L"config"/L"shortcuts.vdf","42"};
            const std::set<std::string> selected{canonicalPathKey(p/L"Game")};
            const auto skipped=refreshExistingSteamArtwork(f.data,scan,absent,output,true,selected);
            check(skipped["items"][0]["status"]=="not-in-steam"&&skipped.value("artworkFilesActivated",-1)==0&&!fs::exists(absent.path.parent_path()),"Empty account was not safely skipped");
            check(fixtureRefresh(f).value("artworkFilesActivated",0)==1,"Repair did not reach another account");
        });
        for (const auto* variant : {"saved-replacement", "saved-deletion", "legacy-only", "split-metadata", "split-conflict"})
            run((std::string("refresh-artwork-manual-alias-")+variant).c_str(),[variant](const fs::path& p){
                ArtworkRepairFixture f(p);
                const auto game=p/L"Game", exe=game/L"Game.exe", scan=fixtureScanState(f);
                const auto key=canonicalPathKey(exe), alias=toUtf8(exe.wstring());
                check(alias<key&&canonicalPathKey(fs::path(toWide(alias)))==key,"Fixture legacy alias must sort before its canonical key");
                const auto old=p/L"old.png", newer=p/L"new.png", hero=p/L"hero.png";
                writeRepairArtwork(old,12);writeRepairArtwork(newer,72);writeRepairArtwork(hero,93);
                const auto file=[](const auto& path){return json{{"file",toUtf8(path.wstring())}};};
                const std::string_view mode(variant);
                json items={{alias,{{"cover",file(old)},{"wallpaper",file(hero)}}}};
                if(mode!="legacy-only"){
                    items[key]={{"name","Current name"}};
                    if(mode=="split-conflict"){
                        const auto peer=key.substr(0,key.size()-8)+"GAME.exe";
                        items[peer]={{"cover",file(newer)}};
                        items[key]["wallpaper"]=file(hero);
                    }
                    if(mode=="saved-deletion")items[key]["artworkProtection"]={{"cover","deleted"}};
                    if(mode=="saved-replacement")items[key]["cover"]=file(newer);
                }
                const auto config=f.data/L"config"/L"manual-overrides.json";
                writeJsonAtomic(config,{{"items",items}});
                const auto configBefore=readBinaryFile(config), vdf=readBinaryFile(f.target.path);
                const auto grid=f.target.path.parent_path()/L"grid", portrait=grid/L"3221225473p.png";
                const auto result=refreshExistingSteamArtwork(f.data,scan,f.target,f.data/L"state"/L"refresh.json",true,{canonicalPathKey(game)});
                writeJsonAtomic(p/L"observed.json",result);
                if(mode=="saved-deletion"||mode=="split-conflict")check(!fs::exists(portrait),"Deleted or ambiguous legacy portrait was restored");
                else check(readBinaryFile(portrait)==readBinaryFile(mode=="saved-replacement"?newer:old),"Repair used the wrong portrait for this alias state");
                check(readBinaryFile(grid/L"3221225473_hero.png")==readBinaryFile(hero)&&readBinaryFile(f.target.path)==vdf&&
                    readBinaryFile(config)==configBefore,"Repair disturbed another slot, original card or configuration");
            });
        run("refresh-invalid-new-image-does-not-replace-good-grid",[](const fs::path& p) {
            ArtworkRepairFixture f(p);const auto state=fixtureScanState(f);
            const auto old=f.target.path.parent_path()/L"grid"/L"3221225473p.png", bad=p/L"invalid.png";
            fs::create_directories(old.parent_path());writeRepairArtwork(old);textFile(bad,"not an image");
            const auto before=readBinaryFile(old), shortcuts=readBinaryFile(f.target.path);
            writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(p/L"Game"/L"Game.exe"),{{"cover",{{"file",toUtf8(bad.wstring())}}}}}}}});
            const auto result=refreshExistingSteamArtwork(f.data,state,f.target,f.data/L"state"/L"refresh.json",true,{canonicalPathKey(p/L"Game")});
            check(result.value("artworkFilesActivated",-1)==0&&result["items"][0]["unusableArtworkSlots"]==json::array({"cover"})&&readBinaryFile(old)==before&&readBinaryFile(f.target.path)==shortcuts,"Invalid image was not safely rejected");
        });
        for (const auto* variant : {"full-to-short", "short-to-full", "ambiguous", "held", "covered"})
            run((std::string("refresh-artwork-alias-grid-") + variant).c_str(), [variant](const fs::path& p) {
                ArtworkRepairFixture f(p);
                const std::string_view mode(variant);
                const auto game=p/L"Game", exe=game/L"Game.exe", grid=f.target.path.parent_path()/L"grid";
                const auto shortGrid=grid/L"3221225473.png", longGrid=grid/L"13835058059610685440.png";
                const auto donor=mode=="short-to-full"?shortGrid:longGrid, peer=donor==shortGrid?longGrid:shortGrid;
                fs::create_directories(grid);writeRepairArtwork(donor,71);
                const auto original=readBinaryFile(f.target.path), donorBytes=readBinaryFile(donor);
                const auto dynamic=grid/L"3221225473p.webm", background=grid/L"3221225473_hero.png";
                textFile(dynamic,"unchanged-video");
                if(mode=="short-to-full")textFile(peer,"corrupt alternate filename");
                if(mode=="ambiguous")textFile(grid/L"13835058059610685440.jpg","ambiguous alternate file");
                if(mode=="covered")writeRepairArtwork(background,90);
                const auto cover=p/L"new-cover.png";writeRepairArtwork(cover);
                json choice={{"cover",{{"file",toUtf8(cover.wstring())}}}};
                if(mode=="held")choice["artworkProtection"]={{"long","deleted"},{"wallpaper","deleted"}};
                writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(exe),choice}}}});
                const auto result=refreshExistingSteamArtwork(f.data,fixtureScanState(f),f.target,
                    f.data/L"state"/L"refresh.json",true,{canonicalPathKey(game)});
                const auto& row=result["items"][0];
                const bool restored=mode=="short-to-full"||mode=="full-to-short"||mode=="covered";
                writeJsonAtomic(p/L"grid-observed.json",{{"row",row},{"peerExists",fs::exists(peer)},
                    {"activated",result["artworkFilesActivated"]}});
                check(result["artworkFilesActivated"]==(restored?2:1)&&fs::exists(peer)==restored,
                    "Horizontal peer was not restored, or an unqualified donor was used");
                if(restored)check(readBinaryFile(peer)==donorBytes&&row["recoveredArtworkSlots"]==json::array({"long"}),"Horizontal peer has wrong bytes or no recovery receipt");
                const auto expected=mode=="covered"||mode=="held"?json::array():json::array({"wallpaper"});
                check(row.value("unresolvedRequiredArtworkSlots",json::array())==expected,"Missing, existing or explicitly deleted hero was misreported");
                check(readBinaryFile(donor)==donorBytes&&readBinaryFile(f.target.path)==original&&fileText(dynamic)=="unchanged-video",
                    "Peer recovery modified its donor, dynamic preview or shortcut");
            });
        for (const auto* variant : {"wrong-short-corrupt-full", "wrong-full-missing-short", "different-valid-images", "second-activation-fails",
                "corrupt-full-variants", "corrupt-short-variants", "valid-peer-variant", "unsupported-peer-variant"})
            run((std::string("refresh-artwork-alias-horizontal-suffix-") + variant).c_str(), [variant](const fs::path& p) {
                ArtworkRepairFixture f(p);
                const std::string_view mode(variant);
                const auto state=fixtureScanState(f), output=f.data/L"state"/L"refresh.json";
                const auto game=p/L"Game", grid=f.target.path.parent_path()/L"grid";
                const auto shortPng=grid/L"3221225473.png", fullPng=grid/L"13835058059610685440.png";
                const auto shortJpg=grid/L"3221225473.jpg", fullJpg=grid/L"13835058059610685440.jpg";
                if(mode=="corrupt-full-variants"||mode=="corrupt-short-variants"||mode=="valid-peer-variant"||mode=="unsupported-peer-variant"){
                    const bool fullDonor=mode=="corrupt-short-variants", usablePeer=mode=="valid-peer-variant", unknownPeer=mode=="unsupported-peer-variant";
                    const auto donor=fullDonor?fullPng:shortPng, peer=fullDonor?shortPng:fullPng;
                    auto alternate=fullDonor?shortJpg:fullJpg;
                    if(unknownPeer)alternate.replace_extension(L".tga");
                    fs::create_directories(grid);
                    writeRepairArtwork(donor,11);
                    textFile(peer,"corrupt main PNG");
                    if(usablePeer)writeRepairArtwork(alternate,51,true);else textFile(alternate,"old competing variant");
                    const auto dynamic=grid/L"3221225473p.webm";
                    textFile(dynamic,"unchanged preview video");
                    const auto original=readBinaryFile(f.target.path), originalPeer=readBinaryFile(peer), originalAlternate=readBinaryFile(alternate);
                    const auto donorBytes=readBinaryFile(donor);
                    const auto refresh=[&]{return refreshExistingSteamArtwork(f.data,state,f.target,output,true,{canonicalPathKey(game)});};
                    const auto result=refresh();
                    const auto& row=result.at("items").at(0);
                    writeJsonAtomic(p/L"observed.json",result);
                    if(usablePeer||unknownPeer){
                        check(result.at("artworkFilesActivated")==0&&readBinaryFile(peer)==originalPeer&&readBinaryFile(alternate)==originalAlternate,
                            "A usable or unsupported peer variant was erased");
                    }else{
                        check(result.at("artworkFilesActivated")==1&&row.at("recoveredArtworkSlots")==json::array({"long"})&&
                            row.at("verifiedStaticArtworkWrites").size()==1&&readBinaryFile(peer)==donorBytes&&!fs::exists(alternate),
                            "Confirmed corrupt peer variants blocked horizontal repair");
                        const auto backup=fs::path(toWide(result.at("backup").get<std::string>()))/L"grid-before";
                        check(readBinaryFile(backup/peer.filename())==originalPeer&&readBinaryFile(backup/alternate.filename())==originalAlternate,
                            "A corrupt peer variant was not backed up");
                        check(refresh().at("artworkFilesActivated")==0,"Peer repair rewrote healthy horizontal artwork");
                    }
                    check(readBinaryFile(f.target.path)==original&&readBinaryFile(donor)==donorBytes&&fileText(dynamic)=="unchanged preview video"&&
                        !hasStages(p)&&pendingJournals(f.data).empty(),"Peer repair disturbed the original card, donor or dynamic artwork");
                    return;
                }
                const bool shortDonor=mode!="wrong-full-missing-short", different=mode=="different-valid-images", rollback=mode=="second-activation-fails";
                const auto wrong=shortDonor?shortJpg:fullJpg, corrected=shortDonor?shortPng:fullPng, peer=shortDonor?fullPng:shortPng;
                fs::create_directories(grid);
                writeRepairArtwork(wrong,42);
                if(different)writeRepairArtwork(peer,92);
                else if(shortDonor)textFile(peer,"corrupt horizontal peer");
                const auto donorBytes=readBinaryFile(wrong), peerBytes=fs::exists(peer)?readBinaryFile(peer):std::vector<unsigned char>{};
                const auto vdf=readBinaryFile(f.target.path);
                const auto refresh=[&]{return refreshExistingSteamArtwork(f.data,state,f.target,output,true,{canonicalPathKey(game)});};
                if(rollback){
                    bool injected=false;
                    selftestMoveFault=[&](const auto& source,const auto& destination)->DWORD{
                        if(!injected&&destination==corrected.wstring()&&source.find(L".yeman-stage-")!=std::wstring::npos){injected=true;return ERROR_WRITE_FAULT;}
                        return ERROR_SUCCESS;
                    };
                    std::string error;
                    try{refresh();}catch(const std::exception& e){error=e.what();}
                    selftestMoveFault={};
                    writeJsonAtomic(p/L"observed.json",{{"injected",injected},{"error",error},{"receipt",fs::exists(output)}});
                    check(injected&&!error.empty()&&!fs::exists(output)&&fs::is_regular_file(wrong)&&readBinaryFile(wrong)==donorBytes&&
                        !fs::exists(corrected)&&readBinaryFile(peer)==peerBytes,"Second horizontal activation did not roll back both filenames");
                } else {
                    const auto result=refresh();
                    writeJsonAtomic(p/L"observed.json",result);
                    const auto& row=result.at("items").at(0);
                    const auto expected=different?1:2;
                    check(result.at("artworkFilesActivated")==expected&&row.at("recoveredArtworkSlots")==json::array({"long"})&&
                        row.at("verifiedStaticArtworkWrites").size()==expected&&!fs::exists(wrong)&&readBinaryFile(corrected)==donorBytes&&
                        readBinaryFile(peer)==(different?peerBytes:donorBytes),"Horizontal file was not normalized independently of its peer");
                    const auto backup=fs::path(toWide(result.at("backup").get<std::string>()))/L"grid-before";
                    check(readBinaryFile(backup/wrong.filename())==donorBytes&&
                        (peerBytes.empty()||different||readBinaryFile(backup/peer.filename())==peerBytes),"Original horizontal image bytes lost during repair");
                    check(refresh().at("artworkFilesActivated")==0,"Healthy horizontal filenames were written again");
                }
                check(readBinaryFile(f.target.path)==vdf&&!hasStages(p)&&pendingJournals(f.data).empty(),"Horizontal repair changed its original card or left transaction debt");
            });
        for (const auto* variant : {"stage-new-target", "pending-alternate-extension", "pending-changed-donor", "pending-changed-peer", "unreadable-peer"})
            run((std::string("refresh-artwork-alias-grid-race-") + variant).c_str(), [variant](const fs::path& p) {
                ArtworkRepairFixture f(p);
                const std::string_view mode(variant);
                const auto state=fixtureScanState(f), output=f.data/L"state"/L"refresh.json";
                const auto game=p/L"Game", exe=game/L"Game.exe", grid=f.target.path.parent_path()/L"grid";
                const auto donor=grid/L"13835058059610685440.png", peer=grid/L"3221225473.png";
                const auto alternate=grid/L"3221225473.jpg", portrait=grid/L"3221225473p.png";
                const auto newCover=p/L"new-cover.png", externalImage=p/L"external.png";
                fs::create_directories(grid);writeRepairArtwork(donor,71);writeRepairArtwork(newCover,80);writeRepairArtwork(externalImage,91);
                const auto oldVdf=readBinaryFile(f.target.path), donorBytes=readBinaryFile(donor), externalBytes=readBinaryFile(externalImage);
                const bool hadPeer=mode=="pending-changed-peer"||mode=="unreadable-peer";
                if(hadPeer)textFile(peer,"corrupt horizontal preimage");
                writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(exe),
                    {{"cover",{{"file",toUtf8(newCover.wstring())}}}}}}}});
                const auto refresh=[&]{return refreshExistingSteamArtwork(f.data,state,f.target,output,true,{canonicalPathKey(game)});};
                if(mode=="unreadable-peer") {
                    withConfigReadDenied(peer,[&]{
                        mustThrow([&]{readBinaryFile(peer);});
                        const auto first=refresh();
                        check(first["artworkFilesActivated"]==1&&!first["items"][0].contains("recoveredArtworkSlots"),"Unreadable peer was treated as corrupt or blocked unrelated portrait repair");
                    });
                    check(fileText(peer)=="corrupt horizontal preimage","Unreadable peer did not survive unmodified");
                    const auto retry=refresh();
                    check(retry["artworkFilesActivated"]==2&&readBinaryFile(peer)==donorBytes&&readBinaryFile(portrait)==readBinaryFile(newCover),"Repair did not recover after read permission returned");
                } else {
                    const auto edited=mode=="pending-alternate-extension"?alternate:mode=="pending-changed-donor"?donor:peer;
                    bool injected=false;
                    selftestMoveFault=[&](const auto&,const auto& destination)->DWORD {
                        const bool atPeerStage=destination.starts_with(peer.wstring()+L".yeman-stage-");
                        if(!injected&&(mode=="stage-new-target"?atPeerStage:destination.ends_with(L"transaction.pending.json"))) {
                            injected=true;
                            writeAtomic(edited,externalBytes);
                        }
                        return ERROR_SUCCESS;
                    };
                    std::string error;
                    try { refresh(); } catch(const std::exception& e) { error=e.what(); }
                    selftestMoveFault={};
                    writeJsonAtomic(p/L"grid-race-observed.json",{{"injected",injected},{"rejected",!error.empty()},{"error",error},
                        {"externalKept",fs::exists(edited)&&readBinaryFile(edited)==externalBytes},{"portraitExists",fs::exists(portrait)}});
                    check(injected&&!error.empty(),"Late grid change did not abort the pending repair");
                    check(!fs::exists(portrait)&&!fs::exists(output)&&readBinaryFile(edited)==externalBytes,"Repair published or committed after an external edit");
                    if(edited!=peer)check(!fs::exists(peer),"Repair created a conflicting copy");
                    if(edited!=donor)check(readBinaryFile(donor)==donorBytes,"Repair changed the original donor");
                }
                check(readBinaryFile(f.target.path)==oldVdf&&!hasStages(p)&&pendingJournals(f.data).empty(),"Grid repair changed shortcut or leaked staging debt");
            });
        for (const auto* variant : {"portrait", "hero", "unchanged"})
            run((std::string("refresh-artwork-slot-grid-race-") + variant).c_str(), [variant](const fs::path& p) {
                ArtworkRepairFixture f(p);
                const std::string_view mode(variant);
                const auto game=p/L"Game", grid=f.target.path.parent_path()/L"grid";
                const auto scan=fixtureScanState(f), output=f.data/L"state"/L"refresh.json";
                const auto portrait=grid/L"3221225473p.png", hero=grid/L"3221225473_hero.png";
                const auto alternate=grid/(mode=="hero"?L"3221225473_hero.jpg":L"3221225473p.jpg");
                const auto coverSource=p/L"new-cover.png", heroSource=p/L"new-hero.png", external=p/L"external.png", video=p/L"video.mp4";
                fs::create_directories(grid);
                writeRepairArtwork(portrait,23); writeRepairArtwork(hero,26);
                writeRepairArtwork(coverSource,34); writeRepairArtwork(heroSource,38); writeRepairArtwork(external,43);
                textFile(video,"unchanged dynamic source");
                const auto oldVdf=readBinaryFile(f.target.path), oldPortrait=readBinaryFile(portrait), oldHero=readBinaryFile(hero), externalBytes=readBinaryFile(external);
                writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(game/L"Game.exe"),{
                    {"cover",{{"file",toUtf8(coverSource.wstring())}}},{"wallpaper",{{"file",toUtf8(heroSource.wstring())}}}
                }}}}});
                const auto refresh=[&]{return refreshExistingSteamArtwork(f.data,scan,f.target,output,true,{canonicalPathKey(game)});};
                if(mode!="unchanged") {
                    bool injected=false;
                    selftestMoveFault=[&](const auto&,const auto& destination)->DWORD {
                        if(!injected&&destination.ends_with(L"transaction.pending.json")) { injected=true; writeAtomic(alternate,externalBytes); }
                        return ERROR_SUCCESS;
                    };
                    std::string error;
                    try { refresh(); } catch(const std::exception& e) { error=e.what(); }
                    selftestMoveFault={};
                    writeJsonAtomic(p/L"grid-race-observed.json",{{"injected",injected},{"error",error},{"receipt",fs::exists(output)},
                        {"portraitUnchanged",readBinaryFile(portrait)==oldPortrait},{"heroUnchanged",readBinaryFile(hero)==oldHero}});
                    check(injected&&!error.empty()&&!fs::exists(output),"Refresh accepted a new competing static extension");
                    check(readBinaryFile(alternate)==externalBytes&&readBinaryFile(portrait)==oldPortrait&&readBinaryFile(hero)==oldHero,
                        "Aborted refresh modified the external image or original static artwork");
                }
                const auto result=refresh();
                check(result["artworkFilesActivated"]==2&&!fs::exists(alternate)&&readBinaryFile(portrait)==readBinaryFile(coverSource)&&
                    readBinaryFile(hero)==readBinaryFile(heroSource),"Explicit stable refresh did not restore both static slots");
                check(readBinaryFile(f.target.path)==oldVdf&&fileText(video)=="unchanged dynamic source"&&
                    !hasStages(p)&&pendingJournals(f.data).empty(),"Refresh changed shortcut, dynamic source or left a pending transaction");
            });
        for (const auto* variant : {"portrait", "hero", "competing-valid", "unreadable", "manual-choice", "deleted", "late-edit"})
            run((std::string("refresh-artwork-slot-variants-")+variant).c_str(), [variant](const fs::path& p) {
                ArtworkRepairFixture f(p);
                const std::string_view mode(variant);
                const auto scan=fixtureScanState(f), game=p/L"Game", output=f.data/L"state"/L"refresh.json";
                const auto grid=f.target.path.parent_path()/L"grid";
                const bool hero=mode=="hero", canRepair=hero||mode=="portrait";
                const std::string slot=hero?"wallpaper":"cover";
                const auto good=grid/(hero?L"3221225473_hero.png":L"3221225473p.png");
                const auto older=grid/(hero?L"3221225473_hero.jpg":L"3221225473p.jpg");
                const auto other=grid/(hero?L"3221225473p.png":L"3221225473_hero.png");
                fs::create_directories(grid);
                writeRepairArtwork(good,23);writeRepairArtwork(other,51);
                if(mode=="competing-valid")writeRepairArtwork(older,75,true);else textFile(older,"broken duplicate");
                const auto before=readBinaryFile(f.target.path), goodBytes=readBinaryFile(good), olderBytes=readBinaryFile(older), otherBytes=readBinaryFile(other);
                if(mode=="manual-choice"||mode=="deleted"){
                    const json selection=mode=="manual-choice"?json{{slot,{{"file",toUtf8((p/L"missing.png").wstring())}}}}:
                        json{{"artworkProtection",{{slot,"deleted"}}}};
                    writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(game/L"Game.exe"),selection}}}});
                }
                const auto refresh=[&]{return refreshExistingSteamArtwork(f.data,scan,f.target,output,true,{canonicalPathKey(game)});};
                if(mode=="late-edit") {
                    const auto external=p/L"external.jpg";writeRepairArtwork(external,87,true);const auto externalBytes=readBinaryFile(external);
                    bool injected=false;
                    selftestMoveFault=[&](const auto&,const auto& destination)->DWORD {
                        if(!injected&&destination.ends_with(L"transaction.pending.json")){injected=true;writeAtomic(older,externalBytes);}
                        return ERROR_SUCCESS;
                    };
                    std::string error;
                    try{refresh();}catch(const std::exception& e){error=e.what();}
                    selftestMoveFault={};
                    writeJsonAtomic(p/L"observed.json",{{"injected",injected},{"error",error},{"receipt",fs::exists(output)}});
                    check(injected&&!error.empty()&&!fs::exists(output)&&readBinaryFile(older)==externalBytes,"Race did not preserve the externally edited slot and refuse completion");
                } else {
                    json result;
                    if(mode=="unreadable")withConfigReadDenied(older,[&]{result=refresh();});else result=refresh();
                    writeJsonAtomic(p/L"observed.json",result);
                    const auto row=result.at("items").at(0);
                    if(canRepair) {
                        check(result.at("artworkFilesActivated")==1&&row.at("recoveredArtworkSlots")==json::array({slot})&&
                            !fs::exists(older),"Did not recover a unique good image competing with a corrupt legacy file");
                        const auto backup=fs::path(toWide(result.at("backup").get<std::string>()))/L"grid-before"/older.filename();
                        check(readBinaryFile(backup)==olderBytes,"Lost the corrupt original instead of preserving it in the transaction backup");
                        check(refresh().at("artworkFilesActivated")==0,"A now-healthy slot kept needing repair");
                    } else {
                        check(result.at("artworkFilesActivated")==0&&readBinaryFile(older)==olderBytes,"Ambiguous, locked or user-protected slot was overwritten");
                        const auto unresolved=row.value("unresolvedRequiredArtworkSlots",json::array());
                        check(mode=="deleted"?unresolved.empty():unresolved==json::array({slot}),"Ambiguous slot was reported as usable or overrode intentional deletion");
                    }
                }
                check(readBinaryFile(good)==goodBytes&&readBinaryFile(other)==otherBytes&&readBinaryFile(f.target.path)==before&&
                    !hasStages(p)&&pendingJournals(f.data).empty(),"Repair changed the donor, an unrelated slot or the shortcut, or left pending work");
            });
        for (const auto* variant : {"suffix-portrait", "suffix-hero", "suffix-handpicked", "suffix-late-peer", "suffix-rollback", "unchanged",
                "mixed-portrait", "mixed-hero", "mixed-handpicked", "mixed-competing", "mixed-unreadable", "mixed-late-peer", "mixed-rollback"})
            run((std::string("refresh-artwork-slot-variants-")+variant).c_str(), [variant](const fs::path& p) {
                ArtworkRepairFixture f(p);
                const std::string_view mode(variant);
                const auto scan=fixtureScanState(f), game=p/L"Game", output=f.data/L"state"/L"refresh.json";
                const auto grid=f.target.path.parent_path()/L"grid";
                const bool mixed=mode.starts_with("mixed-"), hero=mode=="suffix-hero"||mode=="mixed-hero", unchanged=mode=="unchanged";
                const bool handpicked=mode.ends_with("-handpicked"), latePeer=mode.ends_with("-late-peer"), rollback=mode.ends_with("-rollback");
                const std::string slot=hero?"wallpaper":"cover";
                const auto correct=grid/(hero?L"3221225473_hero.png":L"3221225473p.jpg");
                const auto old=unchanged?correct:grid/(hero?L"3221225473_hero.jpg":L"3221225473p.png");
                const auto other=grid/(hero?L"3221225473p.png":L"3221225473_hero.png");
                fs::create_directories(grid);
                writeRepairArtwork(old,29,!hero);writeRepairArtwork(other,51);
                if (mixed) {
                    if (mode=="mixed-competing") writeRepairArtwork(correct,72,!hero);
                    else textFile(correct,"corrupt file with the expected suffix");
                }
                const auto oldPeer=mixed?readBinaryFile(correct):std::vector<unsigned char>{};
                const auto before=readBinaryFile(f.target.path), original=readBinaryFile(old), otherBytes=readBinaryFile(other);
                if(handpicked)
                    writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(game/L"Game.exe"),
                        {{slot,{{"file",toUtf8((p/L"missing.png").wstring())}}}}}}}});
                const auto refresh=[&]{return refreshExistingSteamArtwork(f.data,scan,f.target,output,true,{canonicalPathKey(game)});};
                if(latePeer||rollback) {
                    const auto external=p/L"external.jpg";
                    writeRepairArtwork(external,87,true);
                    const auto externalBytes=readBinaryFile(external);
                    bool injected=false;
                    selftestMoveFault=[&](const auto& source,const auto& destination)->DWORD {
                        if(!injected&&latePeer&&destination.ends_with(L"transaction.pending.json")){
                            injected=true;writeAtomic(correct,externalBytes);
                        }
                        if(!injected&&rollback&&destination==correct.wstring()&&source.find(L".yeman-stage-")!=std::wstring::npos){
                            injected=true;return ERROR_WRITE_FAULT;
                        }
                        return ERROR_SUCCESS;
                    };
                    std::string error;
                    try{refresh();}catch(const std::exception& e){error=e.what();}
                    selftestMoveFault={};
                    writeJsonAtomic(p/L"observed.json",{{"injected",injected},{"error",error},{"receipt",fs::exists(output)}});
                    check(injected&&!error.empty()&&!fs::exists(output)&&readBinaryFile(old)==original,
                        "Filename repair did not refuse the race or roll back the original image");
                    check(latePeer?readBinaryFile(correct)==externalBytes:mixed?readBinaryFile(correct)==oldPeer:!fs::exists(correct),
                        "Failed filename repair touched a competing image or left a new file");
                } else {
                    json result;
                    if(mode=="mixed-unreadable") withConfigReadDenied(correct,[&]{result=refresh();});
                    else result=refresh();
                    writeJsonAtomic(p/L"observed.json",result);
                    const auto& item=result.at("items").at(0);
                    if(unchanged){
                        check(result.at("artworkFilesActivated")==0&&item.at("status")=="no-artwork-change"&&
                            !item.contains("verifiedStaticArtworkWrites")&&readBinaryFile(old)==original,
                            "Verified required artwork was reported missing or rewritten");
                    } else if(handpicked||mode=="mixed-competing"||mode=="mixed-unreadable"){
                        check(result.at("artworkFilesActivated")==0&&readBinaryFile(old)==original&&
                            (mixed?readBinaryFile(correct)==oldPeer:!fs::exists(correct))&&
                            item.at("unresolvedRequiredArtworkSlots")==json::array({slot}),"Filename repair wrote an ambiguous, unreadable or handpicked slot");
                    } else {
                        const auto verified=json::array({{{"slot",slot},{"fileName",toUtf8(correct.filename().wstring())},{"sha256",sha256(original)}}});
                        check(result.at("artworkFilesActivated")==1&&item.at("recoveredArtworkSlots")==json::array({slot})&&
                            item.at("verifiedStaticArtworkWrites")==verified&&
                            !fs::exists(old)&&readBinaryFile(correct)==original,"Valid image with wrong suffix was not recovered byte for byte");
                        const auto backup=fs::path(toWide(result.at("backup").get<std::string>()))/L"grid-before"/old.filename();
                        check(readBinaryFile(backup)==original,"Wrong-suffix image was not backed up");
                        if (mixed) check(readBinaryFile(backup.parent_path()/correct.filename())==oldPeer,"Corrupt peer was not backed up");
                        const auto repeated=refresh();
                        check(repeated.at("artworkFilesActivated")==0&&repeated.at("items").at(0).at("status")=="no-artwork-change",
                            "Second repair wrote a verified image or reported it missing");
                    }
                }
                check(readBinaryFile(other)==otherBytes&&readBinaryFile(f.target.path)==before&&
                    !hasStages(p)&&pendingJournals(f.data).empty(),"Filename repair changed another slot or the original card, or left pending work");
            });
        for (const auto* variant : {"partial-current","current-priority","foreign-exe","different-store","deleted-and-handpicked",
                "recovered-scan","conflicting-cache-id","cleared-identity"})
            run((std::string("refresh-artwork-alias-")+variant).c_str(),[variant](const fs::path& p){fixtureSteamArtworkAliasRecovery(p,variant);});
        for (const auto* variant : {"replace","rollback","recover","above-backup-limit","oversize-new-source","case-replace","case-rollback","case-recover"})
            run((std::string("refresh-artwork-preimage-")+variant).c_str(),[variant](const fs::path& p){fixtureSteamArtworkPreimage(p,variant);});
        for (const auto* variant : {"portrait-with-hero", "linked-horizontal", "zero-writes"})
            run((std::string("refresh-artwork-preimage-isolation-")+variant).c_str(),[variant](const fs::path& p){
                ArtworkRepairFixture f(p);
                const std::string_view mode(variant);
                const auto grid=f.target.path.parent_path()/L"grid", game=p/L"Game", output=f.data/L"state"/L"refresh.json";
                const auto scan=fixtureScanState(f);
                const auto originalVdf=readBinaryFile(f.target.path);
                const bool horizontal=mode=="linked-horizontal", wantHero=mode!="zero-writes";
                const auto blocked=grid/(horizontal?L"13835058059610685440.jpg":L"3221225473p.jpg");
                const auto replacement=grid/(horizontal?L"13835058059610685440.png":L"3221225473p.png");
                const auto otherHorizontal=grid/L"3221225473.png", background=grid/L"3221225473_hero.png";
                const auto chosen=p/L"chosen.png", hero=p/L"chosen-hero.png";
                writeRepairArtwork(chosen,51);writeRepairArtwork(hero,82);textFile(blocked,"old unreadable artwork");
                if(horizontal)writeRepairArtwork(otherHorizontal,99);
                const auto otherBefore=horizontal?readBinaryFile(otherHorizontal):std::vector<unsigned char>{};
                const auto slot=horizontal?"long":"cover";
                json selection={{slot,{{"file",toUtf8(chosen.wstring())}}}};
                if(wantHero)selection["wallpaper"]={{"file",toUtf8(hero.wstring())}};
                writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(game/L"Game.exe"),selection}}}});
                const auto refresh=[&]{return refreshExistingSteamArtwork(f.data,scan,f.target,output,true,{canonicalPathKey(game)});};
                json first;
                withConfigReadDenied(blocked,[&]{
                    mustThrow([&]{readBinaryFile(blocked);});
                    first=refresh();
                });
                check(first["committed"]==true&&first["artworkFilesActivated"]==(wantHero?1:0)&&first["items"][0]["blockedArtworkSlots"]==json::array({slot}),"Unreadable slot did not return a truthful partial receipt");
                check(fileText(blocked)=="old unreadable artwork"&&!fs::exists(replacement),"Unreadable preimage was touched");
                if(horizontal)check(readBinaryFile(otherHorizontal)==otherBefore,"Blocked full ID writes changed the short ID copy");
                if(wantHero)check(readBinaryFile(background)==readBinaryFile(hero),"An unrelated background did not repair");
                const auto retry=refresh();
                check(!retry["items"][0].contains("blockedArtworkSlots")&&!fs::exists(blocked)&&readBinaryFile(replacement)==readBinaryFile(chosen),"Explicit retry did not recover the unblocked slot");
                if(horizontal)check(readBinaryFile(otherHorizontal)==readBinaryFile(chosen),"The horizontal pair did not recover together");
                check(readBinaryFile(f.target.path)==originalVdf&&!hasStages(p)&&pendingJournals(f.data).empty(),"Partial repair modified card identity or leaked staging debt");
                writeJsonAtomic(p/L"blocked-slot-observed.json",{{"first",first},{"retry",retry}});
            });
        run("refresh-automatic-source-falls-back-to-same-record",[](const fs::path& p) {
            ArtworkRepairFixture f(p);
            const auto game=p/L"Game", exe=game/L"Game.exe", scan=fixtureScanState(f);
            const auto manifest=f.data/L"artwork"/toWide(gameDataId(game))/L"manifest.json";
            const auto original=p/L"auto-original.png", portable=p/L"auto-broken.png", automaticHero=p/L"auto-hero.png";
            writeRepairArtwork(original);writeRepairArtwork(automaticHero,71);textFile(portable,"unreadable portable copy");
            writeJsonAtomic(manifest,{{"exe",toUtf8(exe.wstring())},{"artwork",json::array({
                {{"type","tall"},{"steamSuffix","p"},{"ok",true},{"portableFile",toUtf8(portable.wstring())},{"file",toUtf8(original.wstring())}},
                {{"type","hero"},{"steamSuffix","_hero"},{"ok",true},{"file",toUtf8(automaticHero.wstring())}}
            })}});
            const auto grid=f.target.path.parent_path()/L"grid", before=grid/L"3221225473p.jpg";
            textFile(before,"stale portrait");
            const auto originalVdf=readBinaryFile(f.target.path), originalManifest=readBinaryFile(manifest);
            const auto refreshed=refreshExistingSteamArtwork(f.data,scan,f.target,f.data/L"state"/L"refresh.json",true,{canonicalPathKey(game)});
            check(refreshed["artworkFilesActivated"]==2&&!fs::exists(before),"Existing bad portable copy stranded a usable automatic portrait");
            check(readBinaryFile(grid/L"3221225473p.png")==readBinaryFile(original)&&readBinaryFile(grid/L"3221225473_hero.png")==readBinaryFile(automaticHero),"Recovered wrong source or slot");
            check(readBinaryFile(f.target.path)==originalVdf&&readBinaryFile(manifest)==originalManifest&&fileText(portable)=="unreadable portable copy","Steam repair mutated the launch data or local source records");
        });
        run("refresh-bad-automatic-rows-do-not-clobber-manual-or-block-other-slots",[](const fs::path& p) {
            ArtworkRepairFixture f(p);
            const auto game=p/L"Game", exe=game/L"Game.exe", scan=fixtureScanState(f);
            const auto manifest=f.data/L"artwork"/toWide(gameDataId(game))/L"manifest.json";
            const auto valid=p/L"automatic.png", broken=p/L"bad.png", chosen=p/L"chosen.png";
            writeRepairArtwork(valid);textFile(broken,"bad image");writeRepairArtwork(chosen,70);
            const auto asset=[&](const char* type,const char* suffix,const fs::path& file) {
                return json{{"type",type},{"steamSuffix",suffix},{"ok",true},{"file",toUtf8(file.wstring())}};
            };
            writeJsonAtomic(manifest,{{"exe",toUtf8(exe.wstring())},{"artwork",json::array({
                asset("tall","p",valid), asset("logo","../escaped",valid), asset("hero","_hero",broken),
                asset("hero","_hero",valid), asset("long","",valid)
            })}});
            writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(exe),{
                {"cover",{{"file",toUtf8(chosen.wstring())}}},{"artworkProtection",{{"long","deleted"}}}
            }}}}});
            const auto refreshed=refreshExistingSteamArtwork(f.data,scan,f.target,f.data/L"state"/L"refresh.json",true,{canonicalPathKey(game)});
            const auto grid=f.target.path.parent_path()/L"grid";
            check(refreshed["artworkFilesActivated"]==2&&!fs::exists(grid/L"3221225473.png"),"Invalid row blocked valid slots or deleted horizontal art returned");
            check(readBinaryFile(grid/L"3221225473p.png")==readBinaryFile(chosen)&&readBinaryFile(grid/L"3221225473_hero.png")==readBinaryFile(valid),"Automatic recovery displaced the manual portrait or missed an eligible background");
            check(!fs::exists(grid/L"3221225473_logo.png")&&!fs::exists(grid/L"escaped.png"),"Corrupt suffix wrote outside its slot");
        });
        run("artwork-slot-add-manual-image-wins-across-extensions",[](const fs::path& p) {
            ArtworkRepairFixture f(p, false);const auto manual=p/L"manual.png", automatic=p/L"automatic.jpg", manifest=p/L"art.json";
            writeRepairArtwork(manual);writeRepairArtwork(automatic, 76);
            writeJsonAtomic(manifest,{{"artwork",json::array({{{"ok",true},{"type","tall"},{"steamSuffix","p"},{"file",toUtf8(automatic.wstring())}}})}});
            f.plan["items"][0]["manualCoverPath"]=toUtf8(manual.wstring());f.plan["items"][0]["artworkManifest"]=toUtf8(manifest.wstring());f.commit();
            const auto grid=f.target.path.parent_path()/L"grid";
            check(readBinaryFile(grid/L"3221225473p.png")==readBinaryFile(manual)&&!fs::exists(grid/L"3221225473p.jpg"),"A single Steam slot has competing extensions");
        });
        run("delete-success-keeps-backup-receipt",[](const fs::path& p){Fixture f(p);f.commit();const auto original=readBinaryFile(f.target.path);const auto result=fixtureDelete(f);check(result.value("committed",false)&&readShortcuts(f.target.path,f.target.accountId).empty(),"Deletion did not commit");const auto backup=fs::path(toWide(result.value("backup",std::string{})));check(readBinaryFile(backup)==original&&fs::is_regular_file(backup.parent_path()/L"transaction.complete.json"),"Deletion receipt missing");});
        run("recovery-does-not-touch-steam-while-running",[](const fs::path& p){Fixture f(p,true);textFile(f.local,validLocal);selftestMoveFault=[](const auto& from,const auto& to){return (to.ends_with(L"localconfig.vdf")&&from.find(L".yeman-stage-")!=std::wstring::npos)||(to.ends_with(L"shortcuts.vdf")&&from.find(L".yeman-stage-")==std::wstring::npos)?ERROR_WRITE_FAULT:0;};mustThrow([&]{f.commit();});selftestMoveFault={};selftestSteamRunning=true;const auto before=readBinaryFile(f.target.path);cleanupInterruptedSteamTransactions(f.data);check(readBinaryFile(f.target.path)==before&&!pendingJournals(f.data).empty(),"Recovery bypassed Steam guard");});
        run("sequential-account-failure-preserves-first-commit",[](const fs::path& p){Fixture first(p/L"a"),second(p/L"b");first.commit();const auto before=readBinaryFile(first.target.path);selftestMoveFault=[&](const auto& from,const auto& to){return to==second.target.path.wstring()&&from.find(L".yeman-stage-")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};mustThrow([&]{second.commit();});check(readBinaryFile(first.target.path)==before,"Failed account rewrote successful account");check(!fs::exists(second.target.path),"Failed account left shortcut file");});

        run("three-manual-artwork-slots-stage-independently",[](const fs::path& p){ArtworkRepairFixture f(p,false);writeRepairArtwork(p/L"cover.png",36);writeRepairArtwork(p/L"long.png",69);writeRepairArtwork(p/L"hero.png",97);f.plan["items"][0]["manualCoverPath"]=toUtf8((p/L"cover.png").wstring());f.plan["items"][0]["manualLongPath"]=toUtf8((p/L"long.png").wstring());f.plan["items"][0]["manualWallpaperPath"]=toUtf8((p/L"hero.png").wstring());f.commit();const auto grid=f.target.path.parent_path()/L"grid";check(readBinaryFile(grid/L"3221225473p.png")==readBinaryFile(p/L"cover.png"),"Vertical cover ignored");check(readBinaryFile(grid/L"3221225473.png")==readBinaryFile(p/L"long.png"),"Wallpaper overwrote horizontal cover");check(readBinaryFile(grid/L"3221225473_hero.png")==readBinaryFile(p/L"hero.png"),"Wallpaper did not populate hero slot");});
        run("artwork-add-malformed-cache-ordinary-card-survives",[](const fs::path& p){
            ArtworkRepairFixture f(p,false);const auto manifest=p/L"art.json";textFile(manifest,"invalid JSON");
            f.plan["items"][0]["artworkManifest"]=toUtf8(manifest.wstring());
            const auto result=f.commit();const auto cards=readShortcuts(f.target.path,f.target.accountId);
            check(result.value("committed",false)&&cards.size()==1&&cards[0].exe==quotedSteamPath(p/L"Game"/L"Game.exe")&&cards[0].launchOptions.empty()&&!hasStages(p),"Optional broken artwork blocked or changed ordinary import");
        });
        run("artwork-add-ignores-bad-rows-and-falls-back-within-slot",[](const fs::path& p){
            ArtworkRepairFixture f(p,false);const auto manifest=p/L"art.json", image=p/L"good.jpg", bad=p/L"bad.png", wrong=p/L"wrong.png";
            writeRepairArtwork(image);textFile(bad,"bad cache");writeRepairArtwork(wrong,149);
            const auto str=[](const fs::path& path){return toUtf8(path.wstring());};
            writeJsonAtomic(manifest,{{"artwork",json::array({
                {{"ok",true},{"type","tall"},{"steamSuffix","p"},{"portableFile",str(bad)},{"file",str(image)}},
                {{"ok",true},{"type","hero"},{"steamSuffix","_hero"},{"file",str(bad)}},
                {{"ok",true},{"type","long"},{"steamSuffix","_unexpected"},{"file",str(image)}},
                {{"ok",true},{"type","long"},{"steamSuffix",nullptr},{"file",str(wrong)}},
                {{"ok",true},{"type","long"},{"steamSuffix",""},{"file",str(image)}}})}});
            f.plan["items"][0]["artworkManifest"]=str(manifest);
            const auto result=f.commit();const auto grid=f.target.path.parent_path()/L"grid";
            check(result.value("committed",false)&&!fs::exists(grid/L"3221225473_hero.png")&&!fs::exists(grid/L"3221225473_unexpected.png"),"Invalid sources were not isolated");
            for(const auto& file:{L"3221225473p.png",L"3221225473.png",L"13835058059610685440.png"})
                check(readBinaryFile(grid/file)==readBinaryFile(image),"Valid static fallback bytes not staged under the correct name");
        });
        run("artwork-add-preserves-existing-grid-and-deleted-selection",[](const fs::path& p){
            ArtworkRepairFixture f(p,false);const auto exe=p/L"Game"/L"Game.exe",manifest=p/L"art.json",image=p/L"good.png";
            const auto grid=f.target.path.parent_path()/L"grid";textFile(grid/L"3221225473p.jpg","existing Steam portrait");writeRepairArtwork(image);
            f.plan["items"][0]["artworkManifest"]=toUtf8(manifest.wstring());
            f.plan["items"][0]["manualWallpaperPath"]=toUtf8(image.wstring());
            writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{
                {toUtf8(exe.wstring()),{{"wallpaper",{{"file",toUtf8(image.wstring())}}}}},
                {canonicalPathKey(exe),{{"artworkProtection",{{"wallpaper","deleted"}}}}}}}});
            writeJsonAtomic(manifest,{{"artwork",json::array({{{"ok",true},{"type","tall"},{"steamSuffix","p"},{"file",toUtf8(image.wstring())}},
                {{"ok",true},{"type","hero"},{"steamSuffix","_hero"},{"file",toUtf8(image.wstring())}}})}});
            f.commit();check(fileText(grid/L"3221225473p.jpg")=="existing Steam portrait"&&!fs::exists(grid/L"3221225473p.png")&&!fs::exists(grid/L"3221225473_hero.png"),"Import overwrote an existing slot or restored manually deleted art");
        });
        for (const auto* mode : {"split-record", "missing-choice"})
            run((std::string("artwork-add-manual-write-")+mode).c_str(),[mode](const fs::path& p){
                ArtworkRepairFixture f(p,false);
                const auto exe=p/L"Game"/L"Game.exe", source=p/L"manual.png", automatic=p/L"auto.png", manifest=p/L"art.json";
                writeRepairArtwork(source,10);writeRepairArtwork(automatic,90);
                const auto text=[](const auto& path){return toUtf8(path.wstring());};
                const auto key=canonicalPathKey(exe), alias=text(exe);
                check(key!=alias,"Fixture needs an alternate spelling of the same executable");
                const bool split=std::string_view(mode)=="split-record";
                json items={{key,{{"name","Current name"}}}};
                if(split) items[alias]={{"cover",{{"file",text(source)}}},{"artworkProtection",{{"wallpaper","deleted"}}}};
                else items[key]["artworkProtection"]={{"cover","manual"}};
                const auto config=f.data/L"config"/L"manual-overrides.json";
                writeJsonAtomic(config,{{"items",items}});
                const auto configBefore=readBinaryFile(config);
                writeJsonAtomic(manifest,{{"artwork",json::array({{{"ok",true},{"type","tall"},{"steamSuffix","p"},{"file",text(automatic)}},
                    {{"ok",true},{"type","hero"},{"steamSuffix","_hero"},{"file",text(automatic)}}})}});
                f.plan["items"][0]["artworkManifest"]=text(manifest);
                f.plan["items"][0][split?"manualWallpaperPath":"manualCoverPath"]=text(automatic);
                const auto result=f.commit();const auto grid=f.target.path.parent_path()/L"grid";
                const auto portrait=grid/L"3221225473p.png",hero=grid/L"3221225473_hero.png";
                const auto cards=readShortcuts(f.target.path,f.target.accountId);
                const bool coverCorrect=split?(fs::exists(portrait)&&readBinaryFile(portrait)==readBinaryFile(source)):!fs::exists(portrait);
                const bool heroCorrect=split?!fs::exists(hero):(fs::exists(hero)&&readBinaryFile(hero)==readBinaryFile(automatic));
                writeJsonAtomic(p/L"observed.json",{{"result",result},{"coverCorrect",coverCorrect},{"heroCorrect",heroCorrect}});
                check(result.value("committed",false)&&cards.size()==1&&cards[0].exe==quotedSteamPath(exe)&&cards[0].launchOptions.empty()&&
                    coverCorrect&&heroCorrect&&readBinaryFile(config)==configBefore,"Steam import ignored the saved static-slot choice or changed ordinary launch");
            });
        run("refresh-artwork-manual-write-recover",[](const fs::path& p){
            ArtworkRepairFixture f(p);
            const auto exe=p/L"Game"/L"Game.exe", source=p/L"manual.png", automatic=p/L"auto.png", state=fixtureScanState(f);
            writeRepairArtwork(source,10);writeRepairArtwork(automatic,90);
            const auto text=[](const auto& path){return toUtf8(path.wstring());};
            const auto manifest=f.data/L"artwork"/toWide(gameDataId(p/L"Game"))/L"manifest.json";
            writeJsonAtomic(manifest,{{"exe",text(exe)},{"artwork",json::array({
                {{"ok",true},{"type","tall"},{"steamSuffix","p"},{"file",text(automatic)}},
                {{"ok",true},{"type","hero"},{"steamSuffix","_hero"},{"file",text(automatic)}}})}});
            const auto grid=f.target.path.parent_path()/L"grid",portrait=grid/L"3221225473p.png",hero=grid/L"3221225473_hero.png";
            textFile(portrait,"old portrait bytes");
            const auto before=readBinaryFile(portrait),vdf=readBinaryFile(f.target.path);
            const auto config=f.data/L"config"/L"manual-overrides.json", output=f.data/L"state"/L"repair.json";
            const auto key=canonicalPathKey(exe);
            json entry={{"artworkProtection",{{"cover","manual"}}}};
            const auto select=[&]{writeJsonAtomic(config,{{"items",{{key,entry}}}});};
            const auto refresh=[&]{return refreshExistingSteamArtwork(f.data,state,f.target,output,true,{canonicalPathKey(p/L"Game")});};
            select();const auto result=refresh();const auto& row=result.at("items").at(0);
            const bool protectedSlot=readBinaryFile(portrait)==before&&row.value("unusableArtworkSlots",json::array())==json::array({"cover"});
            const bool peerRepaired=fs::exists(hero)&&readBinaryFile(hero)==readBinaryFile(automatic);
            writeJsonAtomic(p/L"broken-choice.json",{{"result",result},{"protectedSlot",protectedSlot},{"peerRepaired",peerRepaired}});
            check(protectedSlot&&peerRepaired&&readBinaryFile(f.target.path)==vdf,"Missing manual choice fell back to automatic art or blocked another slot");
            entry["cover"]={{"file",text(source)}};select();const auto recovered=refresh();
            check(readBinaryFile(portrait)==readBinaryFile(source)&&readBinaryFile(f.target.path)==vdf&&
                recovered.at("items").at(0).value("unusableArtworkSlots",json::array()).empty(),"Reselected manual image did not recover the original card");
        });
        if (artworkManualCache) for (const auto* mode : {"add-recovery", "add-cleared", "add-foreign-game", "repair-recovery",
                "repair-cleared", "repair-reselected", "repair-deleted", "repair-altered-bytes", "repair-foreign-cache",
                "repair-preview-only", "repair-live-original"})
            run((std::string("artwork-slot-manual-cache-")+mode).c_str(),[mode](const fs::path& p){verifyManualArtworkCacheWrite(p,mode);});
        if (artworkManualScrape) for (const auto* mode : {"missing-choice", "missing-legacy-file", "original-fallback", "corrupt-manual-file", "manual-deletion"})
            run((std::string("artwork-slot-manual-scrape-")+mode).c_str(),[mode](const fs::path& p){
                DiscoveryProducerFixture f(p);
                const auto exe=f.exe.parent_path()/L"pcsx2.exe",original=p/L"manual.png",copy=p/L"portable.png";
                textFile(exe,"owned fixture EXE, never executed");writeRepairArtwork(original,38);textFile(copy,"damaged portable copy");
                const auto text=[](const auto& path){return toUtf8(path.wstring());};
                const std::string_view variant(mode);
                const bool deletion=variant=="manual-deletion", fallback=variant=="original-fallback";
                json choice=json::object();
                if (variant!="missing-legacy-file") choice["artworkProtection"]={{"cover",deletion?"deleted":"manual"}};
                if (fallback) choice["cover"]={{"file",text(original)},{"portableFile",text(copy)}};
                if (variant=="corrupt-manual-file") choice["cover"]={{"file",text(copy)}};
                if (variant=="missing-legacy-file") choice["cover"]={{"file",text(p/L"missing.png")}};
                const auto config=f.data/L"config"/L"manual-overrides.json";
                writeJsonAtomic(config,{{"schemaVersion",1},{"items",{{canonicalPathKey(exe),choice}}}});
                const auto before=readBinaryFile(config);
                const auto vdf=f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf";
                const auto beforeVdf=readBinaryFile(vdf);
                const auto code=f.call({exe.wstring(),f.job.wstring(),L"--only-missing",L"--simulate-no-version"});
                json manifest;
                for(const auto& entry:fs::directory_iterator(f.job)) if(entry.is_directory()&&fs::is_regular_file(entry.path()/L"manifest.json")) {
                    check(manifest.is_null(),"More than one artwork manifest was produced");
                    manifest=loadJsonDocument(entry.path()/L"manifest.json");
                }
                if (!manifest.is_object()) throw std::runtime_error("Offline scraper produced no diagnostic manifest: "+f.error);
                const auto artwork=[&](const char* type){for(const auto& row:manifest.at("artwork"))if(jsonStringOr(row,"type")==type)return row;return json::object();};
                const auto tall=artwork("tall"),hero=artwork("hero"),horizontal=artwork("long");
                writeJsonAtomic(p/L"observed.json",{{"exitCode",code},{"tall",tall},{"hero",hero},{"long",horizontal},
                    {"failure",manifest.value("failure",json(nullptr))}});
                check(hero.value("ok",false)&&horizontal.value("ok",false)&&readBinaryFile(config)==before&&readBinaryFile(vdf)==beforeVdf,
                    "Protecting the portrait blocked other slots or changed local configuration/Steam card");
                if(fallback) check(code==0&&tall.value("ok",false)&&jsonStringOr(tall,"provider")=="manual-override"&&
                    jsonStringOr(tall,"sha256")==sha256(readBinaryFile(original)),"Damaged portable image blocked the user's usable original");
                else if(deletion) check(code==0&&!tall.value("ok",false)&&jsonStringOr(tall,"manualSkipReason")=="manual-deleted",
                    "Deleted portrait was restored or counted as a scrape failure");
                else if(variant=="missing-legacy-file") check(code==0&&tall.value("ok",false)&&tall.value("previewOnlyManualFallback",false)&&
                    fs::path(toWide(jsonStringOr(tall,"portableFile"))).stem()==L"tall-preview-fallback",
                    "Known missing file should use the established preview fallback in isolation");
                else check(code==3&&!tall.value("ok",false)&&tall.value("manualProtected",false)&&
                    !jsonStringOr(tall,"manualProtectionError").empty()&&tall.at("attempts").empty()&&
                    !manifest.at("failure").value("retryWhenNetworkReturns",true),"Unavailable manual portrait was replaced or queued for network retry");
            });
        run("artwork-add-rejects-foreign-cache-and-keeps-real-exe-icon",[](const fs::path& p){
            ArtworkRepairFixture f(p,false);fixtureAutomaticIcon(f);
            const auto manifest=fs::path(toWide(jsonStringOr(f.plan["items"][0],"artworkManifest")));auto art=loadJsonDocument(manifest);
            art["exe"]=toUtf8((p/L"Another Game.exe").wstring());writeJsonAtomic(manifest,art);
            const auto result=f.commit();
            check(result.value("committed",false)&&fixtureShortcutIcon(f)==toUtf8((p/L"Game"/L"Game.exe").wstring())&&fs::is_empty(f.target.path.parent_path()/L"grid"),"Foreign artwork cache overrode the real game icon");
        });
        run("manual-wallpaper-counts-as-hero-not-long",[](const fs::path& p){textFile(p/L"cover.png","cover");textFile(p/L"hero.png","hero");const json manual{{"cover",{{"file",toUtf8((p/L"cover.png").wstring())}}},{"wallpaper",{{"file",toUtf8((p/L"hero.png").wstring())}}}};const auto readiness=artworkReadiness(json::object(),manual);check(readiness["minimumComplete"]==true&&readiness["hero"]==true&&readiness["long"]==false,"Artwork readiness confused wallpaper with horizontal cover");});
        run("refresh-manual-wallpaper-keeps-horizontal-cover",[](const fs::path& p){ArtworkRepairFixture f(p);const auto state=fixtureScanState(f),hero=p/L"hero.png",grid=f.target.path.parent_path()/L"grid";writeRepairArtwork(hero);textFile(grid/L"3221225473.png","long before");writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(p/L"Game"/L"Game.exe"),{{"wallpaper",{{"file",toUtf8(hero.wstring())}}}}}}}});refreshExistingSteamArtwork(f.data,state,f.target,f.data/L"state"/L"refresh.json",true,{canonicalPathKey(p/L"Game")});check(fileText(grid/L"3221225473.png")=="long before"&&readBinaryFile(grid/L"3221225473_hero.png")==readBinaryFile(hero),"Wallpaper refresh clobbered horizontal cover");});
        run("retry-malformed-fields-are-quarantined",[](const fs::path& p){writeJsonAtomic(networkRetryQueuePath(p),{{"jobs",{{"bad-time",{{"nextAttemptAt",nullptr}}},{"bad-args",{{"commandArgs",json::array({7})}}},{"bad-count",{{"consecutiveFailures",-1}}},{"good",{{"status","pending"},{"consecutiveFailures",1}}}}}});const auto queue=loadNetworkRetryQueue(p);check(queue["jobs"].size()==1&&queue["jobs"].contains("good"),"Invalid retry fields survived normalization");check(queue["quarantinedJobs"].size()==3,"Rejected retry jobs lost audit evidence");});
        run("initialization-restores-missing-config-before-defaults",[](const fs::path& p){const auto data=p/L"data",configPath=data/L"config"/L"library-config.json";auto config=defaultLibraryConfig();config["roots"]=json::array({toUtf8((p/L"MyGames").wstring())});writeJsonAtomic(fs::path(configPath.wstring()+L".bak"),config);const auto previous=custom_steam_library::configuredDataRoot();SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",data.c_str());initializeSteamBigPictureDataRoot(data,p);SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",previous.empty()?nullptr:previous.c_str());check(loadLibraryConfig(configPath)["roots"]==config["roots"],"Startup overwrote recoverable missing config with defaults");});
        run("cleanup-does-not-race-active-data-transaction",[](const fs::path& p){const auto temp=p/L"active.json.yeman-tmp-1-1";textFile(temp,"active transaction");DataTransactionMutex held;std::thread worker([&]{cleanupInterruptedTemporaryFiles(p);});worker.join();check(fs::exists(temp),"Cleanup deleted a live data transaction temp file");});
        run("writer-mutex-blocks-concurrent-steam-operation",[](const fs::path&){SteamShortcutWriterMutex held;std::atomic<bool> refused{false};std::thread worker([&]{try{SteamShortcutWriterMutex competing(0);}catch(...){refused=true;}});worker.join();check(refused,"Concurrent Steam writer was not serialized");});
        run("malformed-artwork-manifest-cannot-crash-plan",[](const fs::path& p){Fixture f(p);const auto state=fixtureScanState(f);auto game=loadLibraryScanState(state)["games"][0];const auto manifest=f.data/L"artwork"/toWide(gameDataId(p/L"Game"))/L"manifest.json";writeJsonAtomic(manifest,{{"exe",toUtf8((p/L"Game"/L"Game.exe").wstring())},{"ok",true},{"match",{{"identityStatus",nullptr}}}});check(!portableArtworkManifest(f.data,game,p/L"Game"/L"Game.exe"),"Malformed identity reached the planner");});
        run("unsafe-artwork-suffix-is-rejected",[](const fs::path& p){check(!validSteamArtworkSuffix("hero","../escaped"),"Unsafe artwork suffix accepted");check(validSteamArtworkSuffix("long",""),"Long artwork intentionally empty suffix rejected");Fixture f(p);textFile(p/L"cover.png","image");const auto manifest=p/L"manifest.json";writeJsonAtomic(manifest,{{"artwork",json::array({{{"ok",true},{"file",toUtf8((p/L"cover.png").wstring())},{"type","hero"},{"steamSuffix","../escaped"}}})}});f.plan["items"][0]["artworkManifest"]=toUtf8(manifest.wstring());mustThrow([&]{f.commit();});check(!fs::exists(f.target.path)&&!hasStages(p),"Bad suffix changed Steam or leaked stages");});

        run("scan-executable-outside-game-cannot-be-destructively-matched",[](const fs::path& p){const auto state=validateLibraryScanState({{"games",json::array({{{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",toUtf8((p/L"Other"/L"Game.exe").wstring())},{"status","ready"}}})}});check(state["games"][0]["primaryExecutable"]==""&&state["games"][0]["status"]=="data-invalid-review-required","Unrelated executable accepted");});
        run("scan-incomplete-auto-primary-is-review-only",[](const fs::path& p){const auto state=validateLibraryScanState({{"games",json::array({{{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",toUtf8((p/L"Game"/L"Game.exe").wstring())},{"status","ready"},{"scanComplete",false},{"selectionSource","automatic"}}})}});check(state["games"][0]["status"]=="scan-incomplete-needs-review","Partial scan is auto-importable");});
        run("real-target-guard-does-not-trust-plan-flag",[](const fs::path& p){Fixture f(p);f.plan["realSteamTarget"]=false;selftestSteamRunning=true;mustThrow([&]{f.commit();});check(!fs::exists(f.target.path),"Forged plan bypassed Steam running guard");});
        run("config-unchanged-save-does-not-activate-while-steam-is-open",[](const fs::path& p){
            const auto target=p/L"data"/L"config"/L"library-config.json"; fs::create_directories(target.parent_path());
            auto config=defaultLibraryConfig(); config["roots"]=json::array({toUtf8((p/L"Games").wstring())}); writeJsonAtomic(target,config);
            selftestSteamRunning=true; size_t moveAttempts=0; selftestMoveFault=[&](const std::wstring&,const std::wstring&){++moveAttempts;return ERROR_ACCESS_DENIED;};
            auto unchanged=config; unchanged["updatedAt"]=unixTimeMs(); writeJsonWithBackup(target,p/L"data"/L"backups"/L"config","library-config",unchanged);
            check(moveAttempts==0,"Unchanged library config still attempted an atomic activation while another process could hold it");
        });
        run("config-save-retries-transient-access-denied",[](const fs::path& p){
            const auto target=p/L"data"/L"config"/L"library-config.json"; fs::create_directories(target.parent_path());
            auto config=defaultLibraryConfig(); writeJsonAtomic(target,config); size_t moveAttempts=0;
            selftestMoveFault=[&](const std::wstring&,const std::wstring& destination) -> DWORD {if(destination!=target.wstring())return DWORD{ERROR_SUCCESS};return ++moveAttempts < 4 ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;};
            auto changed=config; changed["enabled"]=true; writeJsonWithBackup(target,p/L"data"/L"backups"/L"config","library-config",changed);
            check(moveAttempts==4,"Transient config lock was not retried until activation succeeded");
            check(loadLibraryConfig(target)["enabled"]==true,"Config changed after transient lock recovery");
        });
        run("config-save-persistent-access-denied-retains-original",[](const fs::path& p){
            const auto target=p/L"data"/L"config"/L"library-config.json"; fs::create_directories(target.parent_path());
            auto config=defaultLibraryConfig(); writeJsonAtomic(target,config); const auto original=readBinaryFile(target);
            selftestMoveFault=[&](const std::wstring&,const std::wstring& destination){return destination==target.wstring()?DWORD{ERROR_ACCESS_DENIED}:DWORD{ERROR_SUCCESS};};
            auto changed=config; changed["enabled"]=true; mustThrow([&]{writeJsonWithBackup(target,p/L"data"/L"backups"/L"config","library-config",changed);});
            check(readBinaryFile(target)==original,"Persistent config lock changed the original bytes");
            for(const auto& entry:fs::directory_iterator(target.parent_path())) check(entry.path().filename().wstring().find(L".yeman-tmp-")==std::wstring::npos,"Config lock leaked an activation temp file");
        });

        run("same-exe-root-and-bin-duplicates-repaired",[](const fs::path& p){const auto exe=toUtf8((p/L"Game"/L"bin"/L"Game.exe").wstring());json a={{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",exe},{"status","ready"}};auto b=a;b["gameDirectory"]=toUtf8((p/L"Game"/L"bin").wstring());for(const auto& rows:{json::array({a,b}),json::array({b,a})}){const auto clean=validateLibraryScanState({{"games",rows},{"missingGames",json::array({b})},{"unscannedGames",json::array({b})}});check(clean["games"].size()==1&&clean["games"][0]["gameDirectory"]==a["gameDirectory"],"Same EXE survived or winner depends on order");check(clean["games"][0]["alternateGameDirectories"].size()==1,"Nested selection alias lost");check(clean["missingGames"].empty()&&clean["unscannedGames"].empty(),"Active duplicate survived as lifecycle ghost");}});
        run("same-exe-path-case-and-slashes-are-equivalent",[](const fs::path& p){auto exe=toUtf8((p/L"Game"/L"Game.exe").wstring());json a={{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",exe},{"status","ready"}};auto b=a;auto altered=asciiLower(exe);std::replace(altered.begin(),altered.end(),'\\','/');b["primaryExecutable"]=altered;b["gameDirectory"]=toUtf8((p/L"Game"/L"Sub").wstring());const auto rows=custom_steam_library::uniqueExecutableInventory(json::array({a,b}),[](const auto& value){return canonicalPathKey(fs::path(toWide(value)));});check(rows.size()==1,"Case or slash duplicate survived");});
        run("same-filename-different-full-paths-are-not-duplicates",[](const fs::path& p){json a={{"gameDirectory",toUtf8((p/L"GameA").wstring())},{"primaryExecutable",toUtf8((p/L"GameA"/L"Game.exe").wstring())},{"status","ready"}};auto b=a;b["gameDirectory"]=toUtf8((p/L"GameB").wstring());b["primaryExecutable"]=toUtf8((p/L"GameB"/L"Game.exe").wstring());const auto clean=validateLibraryScanState({{"games",json::array({a,b})}});check(clean["games"].size()==2,"Unrelated EXEs collapsed by filename");});
        run("empty-executables-do-not-collapse",[](const fs::path& p){const auto clean=validateLibraryScanState({{"games",json::array({{{"gameDirectory",toUtf8((p/L"A").wstring())},{"primaryExecutable",""}},{{"gameDirectory",toUtf8((p/L"B").wstring())},{"primaryExecutable",""}}})}});check(clean["games"].size()==2,"Different unresolved games collapsed");});
        run("dedup-preserves-manual-selection-and-native-priority",[](const fs::path& p){json a={{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",toUtf8((p/L"Game"/L"bin"/L"Game.exe").wstring())},{"status","ready"}};auto b=a;b["gameDirectory"]=toUtf8((p/L"Game"/L"bin").wstring());b["manualGameDirectoryLocked"]=true;auto rows=custom_steam_library::uniqueExecutableInventory(json::array({a,b}),[](const auto& v){return canonicalPathKey(fs::path(toWide(v)));});check(rows[0]["gameDirectory"]==b["gameDirectory"],"Manual directory lock lost");a["steamNative"]=true;rows=custom_steam_library::uniqueExecutableInventory(json::array({b,a}),[](const auto& v){return canonicalPathKey(fs::path(toWide(v)));});check(rows[0]["steamNative"].get<bool>(),"Native exclusion lost");});
        run("explicit-nested-alias-selection-matches-single-plan-row",[](const fs::path& p){Fixture f(p);const auto scan=fixtureScanState(f);auto state=loadJsonDocument(scan);auto game=state["games"][0];game["primaryExecutable"]=toUtf8((p/L"Game"/L"bin"/L"Game.exe").wstring());textFile(p/L"Game"/L"bin"/L"Game.exe","fixture");state["games"][0]=game;game["gameDirectory"]=toUtf8((p/L"Game"/L"bin").wstring());state["games"].push_back(game);writeJsonAtomic(scan,state);const auto plan=buildSteamLibraryAddPlan(f.data,scan,f.target,f.planFile,{canonicalPathKey(p/L"Game"/L"bin")},true);check(plan["items"].size()==1&&plan["unmatchedSelections"].empty(),"Nested alias selected nothing or selected duplicate EXEs");});


        run("builtin-steam-tool-id-is-strict",[](const auto&){
            for(const auto& id:std::vector<json>{228980,228980u,"228980"," 00228980 "})
                check(custom_steam_library::builtinExcludedSteamAppId(id),"Tool ID not excluded");
            for(const auto& id:std::vector<json>{nullptr,228980.5,-228980,228981,"228980bad","2289800","+228980","228980.0",18446744073709551615ull})
                check(!custom_steam_library::builtinExcludedSteamAppId(id),"Unrelated or malformed ID incorrectly excluded");
        });
        run("builtin-steam-tool-names-paths-are-exact",[](const auto&){
            for(const auto& name:{"Steamworks Common Redistributables","sTeAmWoRkS Shared","Steamworks_Common_Redistributables"})
                check(custom_steam_library::builtinExcludedSteamToolName(name),"Exact tool name not excluded");
            for(const auto& name:{"Steamworks Simulator","My Steamworks Shared Adventure","Steamworks Shared 2","Redistributables","\xe6\xb8\xb8\xe6\x88\x8f Steamworks Shared"})
                check(!custom_steam_library::builtinExcludedSteamToolName(name),"Unrelated title incorrectly excluded");
            check(custom_steam_library::builtinExcludedSteamToolPath("D:/Steam/steamapps/common/Steamworks Shared/vcredist/installer.exe"),"Descendant tool EXE not excluded");
            check(!custom_steam_library::builtinExcludedSteamToolPath("D:/Games/Steamworks Shared Adventure/Game.exe"),"Similar game directory excluded");
            check(custom_steam_library::builtinExcludedSteamToolPath("C:/Program Files/Epic Games/UE_5.4/Engine/Binaries/Win64/UnrealEditor.exe"),"Versioned Unreal Engine directory not excluded");
            check(!custom_steam_library::builtinExcludedSteamToolPath("D:/Games/UE Game/Game.exe"),"UE-like game directory excluded");
        });
        run("builtin-steam-tool-nested-record-and-artwork-id",[](const auto&){
            check(custom_steam_library::builtinExcludedSteamTool({{"nativeSteam",{{"appId","228980"}}}}),"Nested native ID leaked");
            check(!custom_steam_library::builtinExcludedSteamTool({{"artwork",{{"appId",228980}}},{"igdbId",228980}}),"Non-Steam artwork/IGDB ID excluded");
        });
        run("builtin-steam-tool-clears-all-lifecycle-inventories",[](const auto& p){
            json game={{"gameDirectory",toUtf8((p/L"Game").wstring())},{"primaryExecutable",""},{"status","already-in-steam"}};
            auto tool=game;tool["nativeSteamAppId"]=228980;
            const auto rows=json::array({tool,game});
            const auto state=validateLibraryScanState({{"games",rows},{"unscannedGames",rows},{"missingGames",rows}});
            check(state["games"].size()==1&&state["games"][0]==game,"Excluded row shadowed valid same-directory game");
            for(const auto* section:{"unscannedGames","missingGames"})check(state[section].size()==1&&state[section][0]==game,"Excluded tool survived lifecycle filtering");
        });
        run("builtin-steam-tool-legacy-name-and-native-no-exe",[](const auto& p){
            json good={{"gameDirectory",toUtf8((p/L"Portal 2").wstring())},{"primaryExecutable",""},{"steamNativeAppId",620}};
            auto byName=good;byName["gameDirectory"]=toUtf8((p/L"OldTool").wstring());byName["formalName"]="Steamworks Common Redistributables";
            auto byNative=good;byNative["gameDirectory"]=toUtf8((p/L"RenamedTool").wstring());byNative["nativeSteam"]={{"appId",228980}};
            const auto state=validateLibraryScanState({{"games",json::array({good,byName,byNative})}});
            check(state["games"].size()==1,"Legacy tool survived or real native game vanished");
        });
        run("builtin-steam-tool-manifests-even-renamed-directory",[](const auto& p){
            const auto steam=p/L"Steam",common=steam/L"steamapps"/L"common";
            for(const auto& name:{L"Steamworks Shared",L"RenamedRuntime",L"Portal 2"})fs::create_directories(common/name);
            textFile(steam/L"steamapps"/L"appmanifest_228980.acf",R"("AppState" {"appid" "228980" "name" "Steamworks Common Redistributables" "installdir" "RenamedRuntime"})");
            textFile(steam/L"steamapps"/L"appmanifest_620.acf",R"("AppState" {"appid" "620" "name" "Portal 2" "installdir" "Portal 2"})");
            auto config=defaultLibraryConfig();config["manualGameDirectories"]=json::array({{{"path",toUtf8((common/L"Steamworks Shared").wstring())}},{{"path",toUtf8((common/L"RenamedRuntime").wstring())}}});
            const auto report=scanLibraryRoots({common},{},2,config);
            check(report["games"].size()==1&&report["games"][0].value("nativeSteamAppId",0)==620,"Shared runtime detected as game or Portal removed");
            bool rejected=false;for(const auto& row:report["skippedDirectories"])if(row.value("reason",std::string{})=="builtin-steam-tool-excluded")rejected=true;
            check(rejected,"Builtin exclusion not recorded in scan audit");
        });
        run("builtin-steam-tool-manual-primary-cannot-override",[](const auto& p){
            const auto tool=p/L"Steamworks Shared",exe=tool/L"vcredist.exe";textFile(exe,"fixture only");
            auto config=defaultLibraryConfig();config["manualGameDirectories"]=json::array({{{"path",toUtf8(tool.wstring())}}});
            config["manualPrimary"][canonicalPathKey(tool)]=toUtf8(exe.wstring());
            const auto report=scanLibraryRoots({p},{},2,config);check(report["games"].empty(),"Manual primary bypassed builtin blacklist");
            const auto direct=scanLibraryRoots({tool},{},2,config);check(direct["games"].empty(),"Direct tool root bypassed blacklist");
        });
        run("builtin-steam-tool-old-plan-is-never-import-ready",[](const auto& p){
            Fixture f(p);const auto scan=fixtureScanState(f);auto state=loadJsonDocument(scan);state["games"][0]["nativeSteamAppId"]=228980;writeJsonAtomic(scan,state);
            const auto plan=buildSteamLibraryAddPlan(f.data,scan,f.target,f.planFile,{},true);
            check(plan["items"].empty(),"Old tool cache entered add plan");
        });
        run("automatic-icon-add-binds-vdf-and-grid-together",[](const auto& p){
            ArtworkRepairFixture f(p,false);const auto source=fixtureAutomaticIcon(f);const auto result=f.commit();
            const auto icon=f.target.path.parent_path()/L"grid"/L"3221225473_icon.png";
            check(fixtureShortcutIcon(f)==toUtf8(icon.wstring()),"Downloaded icon not bound to shortcut");
            check(readBinaryFile(icon)==readBinaryFile(source),"Bound icon file missing");
            check(result["items"][0]["automaticIconPath"]==toUtf8(icon.wstring()),"Icon binding absent from receipt");
        });
        run("automatic-icon-add-binds-the-fallback-actually-staged",[](const auto& p){
            ArtworkRepairFixture f(p,false);const auto source=fixtureAutomaticIcon(f);const auto bad=p/L"invalid.jpg";textFile(bad,"broken icon");
            const auto manifest=fs::path(toWide(jsonStringOr(f.plan["items"][0],"artworkManifest")));auto art=loadJsonDocument(manifest);
            art["artwork"][0]["portableFile"]=toUtf8(bad.wstring());writeJsonAtomic(manifest,art);
            const auto result=f.commit();const auto grid=f.target.path.parent_path()/L"grid", icon=grid/L"3221225473_icon.png";
            check(result.value("committed",false)&&fixtureShortcutIcon(f)==toUtf8(icon.wstring())&&!fs::exists(grid/L"3221225473_icon.jpg")&&readBinaryFile(icon)==readBinaryFile(source),"Bound icon did not come from the validated staged fallback");
        });
        run("automatic-icon-add-completion-failure-rolls-back-vdf-and-grid",[](const auto& p){
            ArtworkRepairFixture f(p,false);fixtureAutomaticIcon(f);
            selftestMoveFault=[](const auto&,const auto& to){return to.find(L"transaction.complete.json")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{f.commit();});selftestMoveFault={};
            check(!fs::exists(f.target.path),"Failed first icon import left shortcut");
            check(!fs::exists(f.target.path.parent_path()/L"grid"/L"3221225473_icon.png"),"Failed import left icon");
            check(!hasStages(p)&&pendingJournals(p).empty(),"Failed icon import left stages or unresolved journal");
        });
        run("automatic-icon-refresh-binds-empty-field-without-other-vdf-changes",[](const auto& p){
            ArtworkRepairFixture f(p);const auto source=fixtureAutomaticIcon(f);
            auto expected=BinaryVdfDocument::parse(readBinaryFile(f.target.path));
            const auto icon=f.target.path.parent_path()/L"grid"/L"3221225473_icon.png";
            check(bindAutomaticSteamShortcutIcon(expected,p/L"Game"/L"Game.exe",icon),"Expected empty icon");
            const auto result=fixtureRefresh(f);
            check(readBinaryFile(f.target.path)==expected.serialize(),"Refresh changed unrelated shortcut fields");
            check(readBinaryFile(icon)==readBinaryFile(source)&&result["automaticIconsApplied"]==1,"Icon not committed");
        });
        run("automatic-icon-refresh-preserves-manually-chosen-grid-icon",[](const auto& p){
            ArtworkRepairFixture f(p);fixtureAutomaticIcon(f);
            const auto icon=f.target.path.parent_path()/L"grid"/L"3221225473_icon.png";textFile(icon,"manual choice");
            auto doc=BinaryVdfDocument::parse(readBinaryFile(f.target.path));
            bindAutomaticSteamShortcutIcon(doc,p/L"Game"/L"Game.exe",icon);writeAtomic(f.target.path,doc.serialize());
            const auto before=readBinaryFile(f.target.path);const auto result=fixtureRefresh(f);
            check(readBinaryFile(f.target.path)==before&&fileText(icon)=="manual choice","Automatic icon overwrote manual choice");
            check(result["automaticIconsApplied"]==0&&!result["shortcutsVdfModified"].template get<bool>(),"Manual icon caused VDF rewrite");
        });
        run("automatic-icon-refresh-vdf-activation-failure-rolls-back-images",[](const auto& p){
            ArtworkRepairFixture f(p);fixtureAutomaticIcon(f);const auto before=readBinaryFile(f.target.path);
            selftestMoveFault=[&](const auto& from,const auto& to){return to==f.target.path.wstring()&&from.find(L".yeman-stage-")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{fixtureRefresh(f);});selftestMoveFault={};
            check(readBinaryFile(f.target.path)==before,"Failed icon binding changed VDF");
            check(!fs::exists(f.target.path.parent_path()/L"grid"/L"3221225473_icon.png"),"Failed icon binding left new icon");
            check(!hasStages(p)&&pendingJournals(p).empty(),"Failed icon refresh leaked journal/stages");
        });
        run("automatic-icon-refresh-completion-failure-restores-both-resources",[](const auto& p){
            ArtworkRepairFixture f(p);fixtureAutomaticIcon(f);const auto before=readBinaryFile(f.target.path);
            selftestMoveFault=[](const auto&,const auto& to){return to.find(L"transaction.complete.json")!=std::wstring::npos?ERROR_WRITE_FAULT:0;};
            mustThrow([&]{fixtureRefresh(f);});selftestMoveFault={};
            check(readBinaryFile(f.target.path)==before,"Completion failure left automatic icon reference");
            check(!fs::exists(f.target.path.parent_path()/L"grid"/L"3221225473_icon.png"),"Completion failure left icon file");
            check(pendingJournals(p).empty(),"Successful icon rollback kept journal");
        });
        run("automatic-icon-refresh-failed-rollback-is-recoverable",[](const auto& p){
            ArtworkRepairFixture f(p);fixtureAutomaticIcon(f);const auto before=readBinaryFile(f.target.path);
            selftestMoveFault=[&](const auto& from,const auto& to){
                return to.find(L"transaction.complete.json")!=std::wstring::npos ||
                    (to==f.target.path.wstring()&&from.find(L".yeman-stage-")==std::wstring::npos)?ERROR_WRITE_FAULT:0;
            };
            mustThrow([&]{fixtureRefresh(f);});selftestMoveFault={};
            check(!pendingJournals(p).empty(),"Failed icon rollback lost recovery journal");
            cleanupInterruptedSteamTransactions(f.data);
            check(readBinaryFile(f.target.path)==before&&pendingJournals(p).empty(),"Icon transaction recovery failed");
        });
        run("empty-root-first-run-scan-is-retryable",[](const auto& p){
            const auto path=p/L"config"/L"library-config.json";
            for(const auto& status:{"pending","running","completed","skipped-existing-roots"}) {
                auto config=defaultLibraryConfig();config["roots"]=json::array();config["firstRunDefaultScan"]["status"]=status;writeJsonAtomic(path,config);
                const auto loaded=loadLibraryConfig(path);
                check(loaded["roots"].empty(),"Empty-root config was populated unexpectedly");
                check(loaded["firstRunDefaultScan"].value("status",std::string{})==status || status==std::string("skipped-existing-roots"),"First-run status was corrupted");
            }
        });

        for (const auto* family : {"library-config", "manual-overrides"}) {
            const std::string kind=family;
            auto load=[kind](const fs::path& f){return kind=="library-config"?loadLibraryConfig(f):loadManualOverrides(f);};
            auto save=[kind](const fs::path& f,const json& v,const json* baseline=nullptr){return writeJsonWithBackup(f,f.parent_path().parent_path()/L"backups"/L"config",kind,v,baseline);};
            run(("storm-"+kind+"-damaged-edit-reread").c_str(),[=](const fs::path& p){
                const auto f=p/L"config"/toWide(kind+".json");
                for(const auto& bad:std::vector<std::string>{"", "{", "null", "[]", "true"}) {
                    textFile(f,bad);textFile(f.wstring()+L".bak","{");
                    auto v=load(f);check(v.is_object(),"Damaged config did not return defaults");
                    auto baseline=v;v["userNote"]="edited";save(f,v,&baseline);
                    check(load(f)["userNote"]=="edited","User edit did not heal config");
                    bool retained=false;for(const auto& e:fs::directory_iterator(p/L"backups"/L"corrupt"))if(fileText(e.path())==bad)retained=true;
                    check(retained,"Bad original was not retained");
                }
            });
            run(("storm-"+kind+"-future-schema-preserved").c_str(),[=](const fs::path& p){
                const auto f=p/L"config"/toWide(kind+".json");json future={{"schemaVersion",999},{"enabled",true},{"items",{{"fake.exe",{{"steamId","620"},{"identityStatus","verified"}}}}},{"futureUnknown",{{"secret",7}}}};
                textFile(f,future.dump());const auto original=fileText(f);auto v=load(f);
                check(v.value("schemaVersion",0)==1&&!v.value("enabled",false)&&(!v.contains("items")||v["items"].empty()),"Future schema supplied runtime authority");
                v["userNote"]="edit";mustThrow([&]{save(f,v);});check(fileText(f)==original,"Future schema overwritten");
            });
            run(("storm-"+kind+"-oversize-heals").c_str(),[=](const fs::path& p){
                const auto f=p/L"config"/toWide(kind+".json");const std::string bad((kind=="library-config"?4u:1u)*1024*1024+1,'x');textFile(f,bad);
                auto v=load(f);v["userNote"]="after-large";save(f,v);check(load(f)["userNote"]=="after-large","Oversize did not heal");
                bool retained=false;for(const auto& e:fs::directory_iterator(p/L"backups"/L"corrupt"))if(readBinaryFile(e.path()).size()==bad.size())retained=true;check(retained,"Oversize original lost");
            });
            run(("storm-"+kind+"-unreadable-is-not-absent").c_str(),[=](const fs::path& p){
                const auto f=p/L"config"/toWide(kind+".json");textFile(f,"{\"schemaVersion\":1}");const auto original=fileText(f);
                HANDLE h=CreateFileW(f.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(h!=INVALID_HANDLE_VALUE,"Cannot hold read lock");
                try{auto runtime=load(f);check(!runtime.value("enabled",false)&&(!runtime.contains("items")||runtime["items"].empty()),"Unreadable config supplied authority");mustThrow([&]{save(f,{{"schemaVersion",1},{"userNote","bad"}});});}catch(...){CloseHandle(h);throw;}CloseHandle(h);check(fileText(f)==original,"Unreadable config overwritten");
            });

            run(("storm-"+kind+"-short-write-flush-failure-and-retry").c_str(),[=](const fs::path& p){
                const auto f=p/L"config"/toWide(kind+".json");textFile(f,R"({"schemaVersion":1,"retained":7})");auto v=load(f);const auto original=fileText(f);v["userNote"]="after-retry";
                for(int mode:{1,2}) {
                    configIoTarget=toWide(kind+".json.yeman-tmp-");configIoMode=mode;configIoPartial=false;
                    mustThrow([&]{save(f,v);});configIoMode=0;configIoTarget.clear();
                    check(fileText(f)==original,"Interrupted write changed main file");
                    for(const auto& e:fs::directory_iterator(f.parent_path()))check(e.path().filename().wstring().find(L".yeman-tmp-")==std::wstring::npos,"Interrupted write leaked stage");
                }
                save(f,v);check(load(f)["userNote"]=="after-retry","Retry could not heal after write fault");
            });
            run(("storm-"+kind+"-activation-failure-and-retry").c_str(),[=](const fs::path& p){
                const auto f=p/L"config"/toWide(kind+".json");textFile(f,R"({"schemaVersion":1,"retained":7})");auto v=load(f);const auto original=fileText(f);v["userNote"]="after-retry";
                selftestMoveFault=[&](const auto&,const auto& target)->DWORD{return target==f.wstring()?ERROR_WRITE_FAULT:ERROR_SUCCESS;};mustThrow([&]{save(f,v);});check(fileText(f)==original,"Failed activation changed original");selftestMoveFault={};save(f,v);check(load(f)["userNote"]=="after-retry","Activation retry failed");
            });
            run(("storm-"+kind+"-same-field-conflict-refuses-overwrite").c_str(),[=](const fs::path& p){
                const auto f=p/L"config"/toWide(kind+".json");textFile(f,R"({"schemaVersion":1,"userNote":"original"})");auto stale=load(f),baseline=stale;
                std::thread newer([&]{auto v=load(f);v["userNote"]="newer";save(f,v);});newer.join();stale["userNote"]="stale";mustThrow([&]{save(f,stale,&baseline);});check(load(f)["userNote"]=="newer","Conflicting stale value overwrote newer edit");
            });
            run(("storm-"+kind+"-oversize-submission-refused").c_str(),[=](const fs::path& p){const auto f=p/L"config"/toWide(kind+".json");textFile(f,R"({"schemaVersion":1,"unknown":7})");auto v=load(f);const auto original=fileText(f);v["large"]=std::string((kind=="library-config"?4u:1u)*1024*1024,'z');mustThrow([&]{save(f,v);});check(fileText(f)==original,"Oversized submission changed original");});

            run(("storm-"+kind+"-real-access-denied-and-unlock-retry").c_str(),[=](const fs::path& p){
                const auto f=p/L"config"/toWide(kind+".json");textFile(f,R"({"schemaVersion":1,"retained":7})");const auto original=fileText(f);
                withConfigReadDenied(f,[&]{HANDLE h=CreateFileW(f.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);const auto error=GetLastError();if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);check(h==INVALID_HANDLE_VALUE&&error==ERROR_ACCESS_DENIED,"ACL did not deny the actual read");auto runtime=load(f);check(!runtime.value("enabled",false),"Denied file supplied runtime settings");runtime["userNote"]="blocked";mustThrow([&]{save(f,runtime);});});
                check(fileText(f)==original,"Access denied was treated as absence");auto v=load(f);v["userNote"]="after-unlock";save(f,v);check(load(f)["retained"]==7&&load(f)["userNote"]=="after-unlock","ACL unlock did not allow healing");
            });

            run(("storm-"+kind+"-partial-primary-recovery-preserves-fields").c_str(),[=](const fs::path& p){
                const auto f=p/L"config"/toWide(kind+".json");const std::string field=kind=="library-config"?"roots":"items";
                json primary={{"schemaVersion",1},{field,nullptr},{"vendorExtension",{{"latest",7}}}};json backup={{"schemaVersion",1},{"backupExtension",9}};
                backup[field]=kind=="library-config"?json::array({"Z:\\Retained"}):json{{"retained.exe",{{"name","Retained title"}}}};textFile(f,primary.dump());textFile(f.wstring()+L".bak",backup.dump());
                auto v=load(f);check(v[field]==backup[field]&&v["vendorExtension"]["latest"]==7&&v["backupExtension"]==9,"Recovery discarded valid or extension fields");v["userNote"]="healed";save(f,v);check(load(f)["vendorExtension"]["latest"]==7,"Recovery edit lost primary extension");
            });
            run(("storm-"+kind+"-future-main-never-uses-old-backup").c_str(),[=](const fs::path& p){const auto f=p/L"config"/toWide(kind+".json");textFile(f,R"({"schemaVersion":999,"unknown":7})");textFile(f.wstring()+L".bak",R"({"schemaVersion":1,"enabled":true,"items":{"fake.exe":{"steamId":620,"identityStatus":"verified"}}})");const auto main=fileText(f),backup=fileText(f.wstring()+L".bak");auto v=load(f);check(!v.value("enabled",false)&&(!v.contains("items")||v["items"].empty()),"Future main borrowed authority from old backup");check(fileText(f)==main&&fileText(f.wstring()+L".bak")==backup,"Future runtime read changed originals");});

            run(("storm-"+kind+"-failed-automatic-recovery-is-not-success").c_str(),[=](const fs::path& p){
                const auto f=p/L"config"/toWide(kind+".json");textFile(f,"{");const json backup={{"schemaVersion",1},{"enabled",true},{"recoveryOnly","backup"},{"items",{{"retained.exe",{{"name","Retained title"}}}}}};textFile(f.wstring()+L".bak",backup.dump());const auto original=fileText(f),backupBytes=fileText(f.wstring()+L".bak");
                selftestMoveFault=[&](const auto&,const auto& target)->DWORD{return target==f.wstring()?ERROR_WRITE_FAULT:ERROR_SUCCESS;};auto v=load(f);check(!v.value("enabled",false)&&(!v.contains("items")||v["items"].empty())&&!v.contains("recoveredFromBackup"),"Failed recovery was reported as success");check(fileText(f)==original&&fileText(f.wstring()+L".bak")==backupBytes,"Failed recovery changed main or valid backup");selftestMoveFault={};auto retry=load(f);check(retry["recoveryOnly"]=="backup","Recovery did not retry after fault removal");retry["userNote"]="after-recovery";save(f,retry);check(load(f)["userNote"]=="after-recovery","Recovered config could not accept later user edit");
            });
            run(("storm-"+kind+"-stale-snapshots-rebase").c_str(),[=](const fs::path& p){
                const auto f=p/L"config"/toWide(kind+".json");textFile(f,"{\"schemaVersion\":1,\"unknown\":{\"keep\":3}}");auto stale=load(f),baseline=stale;
                std::thread newer([&]{auto v=load(f);v["otherUserEdit"]=42;save(f,v);});newer.join();stale["userNote"]="mine";save(f,stale,&baseline);
                auto v=load(f);check(v["otherUserEdit"]==42&&v["unknown"]["keep"]==3&&v["userNote"]=="mine","Stale edit discarded newer fields");
            });
        }
        run("storm-library-nested-types-preserve-siblings",[](const fs::path& p){
            const auto f=p/L"config"/L"library-config.json";auto v=defaultLibraryConfig();v["roots"]=json::array({toUtf8((p/L"Games").wstring())});v["manualPrimary"]["bad"]={{"exe",42},{"unknown",9}};v["firstRunDefaultScan"]={{"status",nullptr},{"unknown",7}};v["vendorExtension"]={{"x",true}};textFile(f,v.dump());
            auto loaded=loadLibraryConfig(f);check(loaded["manualPrimary"]["bad"].value("exe",std::string{}).empty(),"Bad manual exe escaped normalization");check(loaded["firstRunDefaultScan"]["unknown"]==7,"Unknown nested field discarded");
            loaded["enabled"]=true;writeJsonWithBackup(f,p/L"backups"/L"config","library-config",loaded);auto reread=loadLibraryConfig(f);check(reread["vendorExtension"]==v["vendorExtension"]&&reread["roots"]==v["roots"],"Valid/unknown fields lost");
        });
        run("storm-manual-name-consumer-types",[](const fs::path& p){const auto f=p/L"config"/L"manual-overrides.json";textFile(f,R"({"items":{"game.exe":{"name":17,"steamId":[],"identityStatus":"verified","unknown":3}},"unknownTop":4})");auto v=loadManualOverrides(f);check(!persistedManualName(v,"game.exe"),"Bad name accepted");check(!v["items"]["game.exe"].contains("identityStatus"),"Broken identity retained authority");updateManualOverridesAtomically(f,p/L"backups"/L"config",[](json& x){x["items"]["game.exe"]["name"]="User title";});check(persistedManualName(loadManualOverrides(f),"game.exe")=="User title","Manual name edit failed");check(loadManualOverrides(f)["unknownTop"]==4,"Unknown top field lost");});

        run("storm-manual-valid-numeric-ids-and-tombstone",[](const fs::path& p){const auto f=p/L"config"/L"manual-overrides.json";textFile(f,R"({"items":{"good.exe":{"steamId":620,"storefrontSteamId":null,"igdbId":72,"identityStatus":"manual"},"bad.exe":{"steamId":620,"idCleared":null,"identityStatus":"verified"}}})");auto v=loadManualOverrides(f);check(v["items"]["good.exe"]["steamId"]==620&&v["items"]["good.exe"]["igdbId"]==72,"Valid persisted numeric IDs lost");check(v["items"]["bad.exe"]["idCleared"]==true&&!v["items"]["bad.exe"].contains("identityStatus"),"Bad tombstone granted identity authority");});
        run("storm-future-schema-migration-and-restore-refused",[](const fs::path& p){const auto data=p/L"data",f=data/L"config"/L"library-config.json";textFile(f,R"({"schemaVersion":99,"path":"https://steam-library-data.local/artwork-overrides/a"})");const auto original=fileText(f);normalizeMigratedJsonPaths(data,{});check(fileText(f)==original,"Migration rewrote incompatible config");const auto snap=createPortableBackupSnapshot(data,L"snapshots","storm-future");mustThrow([&]{restorePortableBackupSnapshot(data,snap);});check(fileText(f)==original,"Restore rewrote incompatible config");});
        run("storm-nested-json-depth-is-bounded",[](const fs::path& p){const auto f=p/L"config"/L"library-config.json";textFile(f,std::string(512,'[')+"0"+std::string(512,']'));auto v=loadLibraryConfig(f);check(v["enabled"]==false,"Deep damaged config supplied settings");v["enabled"]=true;writeJsonWithBackup(f,p/L"backups"/L"config","library-config",v);check(loadLibraryConfig(f)["enabled"]==true,"Deep config edit failed");});

        run("storm-library-real-scan-consumer-edit-reread",[](const fs::path& p){
            const auto game=p/L"StormGame",exe=game/L"StormGame.exe",f=p/L"config"/L"library-config.json";textFile(exe,"isolated game file, never executed");const auto key=canonicalPathKey(game);auto raw=defaultLibraryConfig();raw["manualPrimary"][key]={{"exe",42},{"extension",7}};raw["roots"]=json::array({toUtf8(p.wstring())});textFile(f,raw.dump());auto config=loadLibraryConfig(f);const auto before=scanOneGameDirectory(game,2,config,{});check(before["status"]=="manual-primary-missing","Bad primary bypassed confirmation");writeJsonAtomic(p/L"consumed-before.json",before);auto baseline=config;config["manualPrimary"][key]["exe"]=toUtf8(exe.wstring());writeJsonWithBackup(f,p/L"backups"/L"config","library-config",config,&baseline);const auto reread=loadLibraryConfig(f);const auto after=scanOneGameDirectory(game,2,reread,{});writeJsonAtomic(p/L"consumed-after.json",after);check(after["status"]=="ready"&&reread["manualPrimary"][key]["extension"]==7,"Valid user primary edit failed to recover real scan");
        });
        run("storm-manual-readiness-cannot-forge-download-or-ownership",[](const fs::path& p){const auto f=p/L"config"/L"manual-overrides.json";const json raw={{"items",{{"game.exe",{{"steamId",620},{"identityStatus","verified"},{"steamOwnership","owned"},{"downloadConfirmed",true},{"cover",{{"file",nullptr},{"available",true},{"ok",true}}},{"wallpaper",{{"file",toUtf8((p/L"missing.png").wstring())},{"available",true}}}}}}}};textFile(f,raw.dump());const auto loaded=loadManualOverrides(f);const auto readiness=artworkReadiness(json::object(),loaded["items"]["game.exe"]);check(readiness["minimumComplete"]==false&&readiness["tall"]==false&&readiness["hero"]==false,"Config forged downloaded artwork");check(!fs::exists(p/L"Steam"),"Readiness created a Steam target");writeJsonAtomic(p/L"readiness.json",readiness);});

        run("storm-library-concurrent-root-list-conflict-and-retry",[](const fs::path& p){const auto f=p/L"config"/L"library-config.json";writeJsonAtomic(f,defaultLibraryConfig());auto stale=loadLibraryConfig(f),baseline=stale;std::thread newer([&]{auto v=loadLibraryConfig(f);v["roots"].push_back("Z:\\Newer");writeJsonWithBackup(f,p/L"backups"/L"config","library-config",v);});newer.join();stale["roots"].push_back("Z:\\Mine");mustThrow([&]{writeJsonWithBackup(f,p/L"backups"/L"config","library-config",stale,&baseline);});auto retry=loadLibraryConfig(f);check(retry["roots"]==json::array({"Z:\\Newer"}),"Stale root list lost newer root");retry["roots"].push_back("Z:\\Mine");writeJsonWithBackup(f,p/L"backups"/L"config","library-config",retry);check(loadLibraryConfig(f)["roots"].size()==2,"Fresh root edit could not retry");});

        auto createDebt=[](Fixture& f){const auto exe=f.root/L"Game"/L"Game.exe";textFile(exe,"isolated executable marker");f.plan["items"][0]["primaryExecutable"]=toUtf8(exe.wstring());textFile(f.local,validLocal);selftestMoveFault=[](const auto& from,const auto& to)->DWORD{return (to.ends_with(L"localconfig.vdf")&&from.find(L".yeman-stage-")!=std::wstring::npos)||(to.ends_with(L"shortcuts.vdf")&&from.find(L".yeman-stage-")==std::wstring::npos)?ERROR_WRITE_FAULT:ERROR_SUCCESS;};mustThrow([&]{f.commit();});selftestMoveFault={};check(pendingJournals(f.data).size()==1,"Fixture failed to retain transaction debt");return pendingJournals(f.data)[0];};
        for(const auto* change:{"future-schema","missing-original-existence","account-mismatch","bad-backup-bytes","missing-preimage-hash","forged-completion"})run((std::string("txstorm-pending-")+change).c_str(),[=](const fs::path& p){Fixture f(p,true);const auto journal=createDebt(f);auto record=loadJsonDocument(journal);const std::string kind=change;
            if(kind=="future-schema")record["schemaVersion"]=999;
            if(kind=="missing-original-existence")record.erase("targetExisted");
            if(kind=="account-mismatch")record["accountId"]="99999";
            if(kind=="missing-preimage-hash")record.erase("targetBeforeSha256");
            if(kind=="bad-backup-bytes")textFile(journal.parent_path()/L"shortcuts.before.vdf","damaged preimage bytes");
            if(kind=="forged-completion")writeJsonAtomic(journal.parent_path()/L"transaction.complete.json",{{"schemaVersion",999},{"targetPath",toUtf8(f.target.path.wstring())},{"committed",true}});
            writeJsonAtomic(journal,record);const auto before=readBinaryFile(f.target.path);cleanupInterruptedSteamTransactions(f.data);check(fs::exists(journal),"Untrusted journal/marker erased persistent debt");check(readBinaryFile(f.target.path)==before,"Untrusted journal changed external target");mustThrow([&]{requireResolvedSteamTransactions(f.data,f.target.path);});
        });
        run("txstorm-unreadable-journal-is-not-absent",[=](const fs::path& p){Fixture f(p,true);const auto journal=createDebt(f);const auto old=fileText(journal);withConfigReadDenied(journal,[&]{cleanupInterruptedSteamTransactions(f.data);mustThrow([&]{requireResolvedSteamTransactions(f.data,f.target.path);});});check(fileText(journal)==old,"Unreadable debt changed");cleanupInterruptedSteamTransactions(f.data);check(!fs::exists(journal),"Valid debt could not retry after ACL unlock");});
        run("txstorm-blank-journal-blocks-but-valid-repair-retries",[=](const fs::path& p){Fixture f(p,true);const auto journal=createDebt(f);const auto valid=fileText(journal);textFile(journal,"");cleanupInterruptedSteamTransactions(f.data);check(fs::exists(journal)&&fileText(journal).empty(),"Empty debt erased");mustThrow([&]{requireResolvedSteamTransactions(f.data,f.target.path);});textFile(journal,valid);cleanupInterruptedSteamTransactions(f.data);check(!fs::exists(journal),"Explicit valid repaired debt did not recover");});
        run("txstorm-pending-external-newer-write-is-preserved",[=](const fs::path& p){Fixture f(p,true);const auto journal=createDebt(f);auto changed=BinaryVdfDocument::emptySteamShortcuts();auto* shortcuts=vdfFindObject(changed.root(),"shortcuts");shortcuts->objectValue.push_back({"0",steamShortcutNode(-1073741800,"New external game",quotedSteamPath(p/L"External.exe"),quotedSteamPath(p),0)});writeAtomic(f.target.path,changed.serialize());const auto current=readBinaryFile(f.target.path);cleanupInterruptedSteamTransactions(f.data);check(readBinaryFile(f.target.path)==current&&fs::exists(journal),"Recovery overwrote unrelated later Steam edit");});
        run("txstorm-snapshot-future-schema-rejected",[](const fs::path& p){textFile(p/L"data"/L"state"/L"note.json","old");const auto snapshot=createPortableBackupSnapshot(p/L"data",L"snapshots","storm");auto manifest=loadJsonDocument(snapshot/L"snapshot-manifest.json");manifest["schemaVersion"]=999;writeJsonAtomic(snapshot/L"snapshot-manifest.json",manifest);mustThrow([&]{verifyPortableBackupSnapshot(snapshot);});});
        run("txstorm-snapshot-restore-cannot-erase-pending-debt",[=](const fs::path& p){Fixture f(p,true);textFile(f.data/L"state"/L"note.json","old");const auto snapshot=createPortableBackupSnapshot(f.data,L"snapshots","before-debt");const auto journal=createDebt(f);const auto bytes=fileText(journal);mustThrow([&]{restorePortableBackupSnapshot(f.data,snapshot);});check(fileText(journal)==bytes,"Snapshot restore erased transaction debt");});
        for(const auto* change:{"future-schema","unlisted-file","wrong-game-id","foreign-override"})run((std::string("txstorm-deleted-recovery-")+change).c_str(),[=](const fs::path& p){const auto data=p/L"data",game=p/L"Games"/L"Game";textFile(data/L"games"/toWide(gameDataId(game))/L"meta.json","retained");const auto package=createDeletedGameRecoveryPackage(data,game,defaultLibraryConfig(),{{"items",json::object()}});auto manifest=loadJsonDocument(package/L"recovery-manifest.json");const std::string kind=change;if(kind=="future-schema")manifest["schemaVersion"]=999;if(kind=="wrong-game-id")manifest["gameDataId"]="../arbitrary";if(kind=="foreign-override")manifest["manualOverrides"]={{"Z:\\Other\\Game.exe",{{"steamId",620}}}};if(kind=="unlisted-file")textFile(package/L"games"/L"injected.json","not inventoried");mustThrow([&]{verifyRecoveryFiles(package,manifest);});});
        run("txstorm-membership-plan-publication-before-commit",[](const fs::path& p){fixturePlanCommitPublication(p,true);});
        run("txstorm-membership-plan-publication-after-commit",[](const fs::path& p){fixturePlanCommitPublication(p,false);});
        run("txstorm-membership-replayed-live-authority-is-not-resigned",[](const fs::path& p){namespace m=custom_steam_library::ymcc_whitelist;const auto data=p/L"data",whitelist=p/L"whitelist.txt",target=p/L"Steam"/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf",exe=p/L"Game"/L"Game.exe";textFile(p/L"Steam"/L"steam.exe","fixture");auto doc=BinaryVdfDocument::emptySteamShortcuts();vdfFindObject(doc.root(),"shortcuts")->objectValue.push_back({"0",steamShortcutNode(-1073741823,"Game",quotedSteamPath(exe),quotedSteamPath(exe.parent_path()),0)});writeAtomic(target,doc.serialize());json event={{"schemaVersion",1},{"mode","steam-library-add-plan"},{"realSteamTarget",true},{"accountId","19627"},{"shortcutsVdf",toUtf8(target.wstring())},{"shortcutsBeforeSha256",sha256(readBinaryFile(target))},{"items",json::array({{{"primaryExecutable",toUtf8(exe.wstring())},{"primarySelectionStatus","ready"},{"primaryRole","main-game"},{"status","already-in-steam"}}})}};publishYmccGameWhitelist(event,data,false,whitelist);const auto statePath=data/L"state"/L"ymcc-game-whitelist-membership.json";auto state=loadJsonDocument(statePath);state["accounts"]["19627"]["revision"]=state["accounts"]["19627"]["revision"].get<uint64_t>()+1;writeJsonAtomic(statePath,state);const auto before=fileText(statePath);publishYmccGameWhitelist(event,data,false,whitelist);check(fileText(statePath)==before&&event["ymccWhitelistPublication"]["status"]!="updated"&&event["ymccWhitelistPublication"]["status"]!="unchanged","Historical event was resigned as live");});

        run("txstorm-valid-other-account-can-proceed-while-peer-debt-remains",[=](const fs::path& p){Fixture a(p/L"A",true);const auto journal=createDebt(a);Fixture b(p/L"B");b.data=a.data;b.target.accountId="19627";const auto result=b.commit();check(result["committed"]==true&&fs::exists(journal),"Unrelated valid account target could not proceed without erasing peer debt");});
        run("txstorm-recovery-io-failure-retains-debt-and-retries",[=](const fs::path& p){Fixture f(p,true);const auto journal=createDebt(f);selftestMoveFault=[&](const auto&,const auto& to)->DWORD{return to==f.target.path.wstring()?ERROR_WRITE_FAULT:ERROR_SUCCESS;};cleanupInterruptedSteamTransactions(f.data);check(fs::exists(journal),"Failed recovery wrote a completion");selftestMoveFault={};cleanupInterruptedSteamTransactions(f.data);check(!fs::exists(journal)&&readBinaryFile(f.target.path)==BinaryVdfDocument::emptySteamShortcuts().serialize(),"Verified recovery could not retry after I/O failure");});
        run("txstorm-completion-marker-missing-fields-cannot-release-debt",[=](const fs::path& p){Fixture f(p,true);const auto journal=createDebt(f);writeJsonAtomic(journal.parent_path()/L"transaction.complete.json",{{"schemaVersion",2},{"targetPath",toUtf8(f.target.path.wstring())},{"completedAt",uint64_t{1}}});cleanupInterruptedSteamTransactions(f.data);check(fs::exists(journal),"Incomplete completion released debt");mustThrow([&]{requireResolvedSteamTransactions(f.data,f.target.path);});});
        run("txstorm-snapshot-old-membership-cannot-replace-current",[](const fs::path& p){const auto data=p/L"data",state=data/L"state"/L"ymcc-game-whitelist-membership.json";textFile(state,R"({"schemaVersion":1,"owner":"custom-steam-library","accounts":{}})");const auto snapshot=createPortableBackupSnapshot(data,L"snapshots","old-ownership");textFile(state,R"({"schemaVersion":1,"owner":"custom-steam-library","accounts":{},"currentEpoch":7})");const auto before=fileText(state);mustThrow([&]{restorePortableBackupSnapshot(data,snapshot);});check(fileText(state)==before,"Snapshot rolled ownership epoch backward");});
        run("txstorm-bad-recovery-state-does-not-claim-consumed",[](const fs::path& p){const auto data=p/L"data",game=p/L"Games"/L"Game";textFile(data/L"games"/toWide(gameDataId(game))/L"meta.json","owned metadata");const auto package=createDeletedGameRecoveryPackage(data,game,defaultLibraryConfig(),{{"items",json::object()}});textFile(package/L"recovery-state.json","");check(latestDeletedGameRecoveryPackage(data,game)==package,"Bad consumed marker permanently suppressed explicit undo");check(fileText(package/L"recovery-state.json").empty(),"Selection overwrote corrupt marker");});

        auto membershipEvent=[](const fs::path& p,const std::string& account,const std::string& game){const auto steam=p/L"Steam",target=steam/L"userdata"/toWide(account)/L"config"/L"shortcuts.vdf",exe=p/L"Games"/toWide(game)/(toWide(game)+L".exe");textFile(steam/L"steam.exe","fixture marker");textFile(exe,"never executed");auto doc=BinaryVdfDocument::emptySteamShortcuts();vdfFindObject(doc.root(),"shortcuts")->objectValue.push_back({"0",steamShortcutNode(-1073741823,game,quotedSteamPath(exe),quotedSteamPath(exe.parent_path()),0)});writeAtomic(target,doc.serialize());return json{{"schemaVersion",1},{"mode","steam-library-add-plan"},{"realSteamTarget",true},{"accountId",account},{"shortcutsVdf",toUtf8(target.wstring())},{"shortcutsBeforeSha256",sha256(readBinaryFile(target))},{"items",json::array({{{"primaryExecutable",toUtf8(exe.wstring())},{"primarySelectionStatus","ready"},{"primaryRole","main-game"},{"status","already-in-steam"}}})}};};
        run("txstorm-membership-two-accounts-and-one-account-removal",[=](const fs::path& p){const auto data=p/L"data",rules=p/L"rules.txt";textFile(rules,"manual-user-rule\n");auto a=membershipEvent(p,"1001","Alpha"),b=membershipEvent(p,"1002","Beta");publishYmccGameWhitelist(a,data,true,rules);publishYmccGameWhitelist(b,data,true,rules);check(fileText(rules).find("alpha")!=std::string::npos&&fileText(rules).find("beta")!=std::string::npos,"Two-account union lost an actual peer");const auto target=fs::path(toWide(a["shortcutsVdf"].get<std::string>()));writeAtomic(target,BinaryVdfDocument::emptySteamShortcuts().serialize());a.erase("ymccMembershipAuthority");a["shortcutsBeforeSha256"]=sha256(readBinaryFile(target));a["items"]=json::array();publishYmccGameWhitelist(a,data,true,rules);check(fileText(rules).find("alpha")==std::string::npos&&fileText(rules).find("beta")!=std::string::npos&&fileText(rules).find("manual-user-rule")!=std::string::npos,"Account refresh removed another account/manual rule");
            // Repeated real extensions use the shared exact literal codec.
            json literalObservations=json::array();
            for(const auto& label:std::vector<std::string>{
                    "Stray-Win64-Shipping.exe","Stray-Win64-Shipping.exe.exe","测试.exe","Été.ExE"}) {
                const auto literalRoot=p/L"Lit"/toWide(std::to_string(literalObservations.size()));
                const auto literalData=literalRoot/L"d",literalRules=literalRoot/L"r.txt";
                auto event=membershipEvent(literalRoot,"1001",label);
                const auto executable=fs::path(toWide(event["items"][0]["primaryExecutable"].get<std::string>()));
                const auto expected=std::string("@exe:")+toUtf8(unicodeLower(executable.filename().wstring()));
                const auto rule=normalizeGameRule(toUtf8(executable.wstring()));
                check(rule==expected&&normalizeGameRule(rule)==rule&&normalizeGameRule(normalizeGameRule(rule))==rule,
                    "Literal producer changed a real suffix or was not idempotent");
                textFile(literalRules,"manual-user-rule\n");
                const auto target=fs::path(toWide(event["shortcutsVdf"].get<std::string>()));
                const auto targetBefore=readBinaryFile(target);
                publishYmccGameWhitelist(event,literalData,true,literalRules);
                const auto statePath=literalData/L"state"/L"ymcc-game-whitelist-membership.json";
                const auto owned=loadJsonDocument(statePath);
                const auto key=custom_steam_library::ymcc_whitelist::executableKey(toUtf8(executable.wstring()));
                check(owned["accounts"]["1001"]["entries"].at(key)==expected &&
                    event["ymccWhitelistPublication"]["status"]!="warning" &&
                    readBinaryFile(target)==targetBefore,"Literal membership disappeared or changed the VDF");
                const auto bytes=readBinaryFile(literalRules);
                const auto parsed=gameRuleTextRules(bytes);
                check(std::find(parsed.begin(),parsed.end(),expected)!=parsed.end() &&
                    gameRuleFileBytes(parsed,&bytes)==bytes,"Literal managed/manual text roundtrip changed identity");
                const std::vector<LibraryExecutableExclusion> exact{{expected,"owned-literal"}};
                const auto nearby=executable.parent_path()/L"Stray-Win64-Shipping.exe";
                check(libraryExecutableExclusion(executable,exact).has_value() &&
                    (executable==nearby || !libraryExecutableExclusion(nearby,exact)),
                    "Literal blacklist matched a different actual basename");
                const std::vector<LibraryExecutableExclusion> glob{{"*","old-glob"}};
                check(libraryExecutableExclusion(executable,glob).has_value(),"Literal producer broke old glob semantics");
                literalObservations.push_back({{"path",toUtf8(executable.wstring())},{"rule",rule},
                    {"publication",event["ymccWhitelistPublication"]},{"membership",owned},{"roundtrip",true}});
            }
            namespace literalMember=custom_steam_library::ymcc_whitelist;
            check(!literalMember::validRule("bad.exe")&&!literalMember::validRule("bad*")&&
                literalMember::validRule("normal-game"),"Literal support weakened old managed rule guards");
            for(const auto* bad:{"@exe2:game.exe.exe","@exe:game.exe","@exe:../game.exe.exe",
                                "@exe:game*.exe.exe","@exe:game?.exe.exe","@exe:game.exe.exe:evil"}) {
                check(!literalMember::validRule(bad),"Malformed literal passed stored-state validation");
                mustThrow([&]{normalizeGameRule(bad);});
                mustThrow([&]{const std::string text=std::string(bad)+"\n";gameRuleTextRules(std::vector<unsigned char>(text.begin(),text.end()));});
            }
            check(normalizeGameRule("D:\\Games\\NORMAL.EXE")=="normal"&&normalizeGameRule("normal")=="normal"&&
                normalizeGameRule("qemu-system-*")=="qemu-system-*","Legacy ordinary/glob normalization changed");
            writeJsonAtomic(p/L"literal-rule-observations.json",literalObservations);
            // A native game has a real appmanifest but no shortcuts.vdf row.
            // Its main EXE can be nested several directories under the install.
            const auto nativeDir=p/L"Steam"/L"steamapps"/L"common"/L"NativeGame";
            const auto nativeExe=nativeDir/L"bin"/L"NativeGame.exe";
            const auto nativeManifest=p/L"Steam"/L"steamapps"/L"appmanifest_778899.acf";
            textFile(nativeExe,"owned inert marker, never executed");fs::resize_file(nativeExe,6u*1024u*1024u);
            const std::string manifest="\"AppState\" { \"appid\" \"778899\" \"name\" \"NativeGame\" \"installdir\" \"NativeGame\" }";
            textFile(nativeManifest,manifest);
            auto nativeRow=json{{"status","already-in-steam"},{"steamNative",true},{"contentType","steam-native"},{"nativeSteamAppId",778899},
                {"gameDirectory",toUtf8(nativeDir.wstring())},{"primaryExecutable",toUtf8(nativeExe.wstring())},{"primaryRole","main-game"},{"primarySelectionStatus","ready"}};
            auto mixed=b;mixed.erase("ymccMembershipAuthority");mixed["items"].push_back(nativeRow);
            // A built-in excluded prerequisite cannot own a rule or block
            // unrelated games merely because it has no launchable main EXE.
            mixed["items"].push_back({{"status","already-in-steam"},{"steamNative",true},{"nativeSteamAppId",228980},{"directoryName","Steamworks Common Redistributables"},{"primaryExecutable",nullptr}});
            publishYmccGameWhitelist(mixed,data,true,rules);
            writeJsonAtomic(p/L"native-publication-observation.json",{{"publication",mixed["ymccWhitelistPublication"]},{"rules",fileText(rules)}});
            check(mixed["ymccWhitelistPublication"]["status"]!="warning"&&fileText(rules).find("nativegame")!=std::string::npos&&fileText(rules).find("beta")!=std::string::npos,"Native Steam row blocked valid shortcut whitelist publication");
            a.erase("ymccMembershipAuthority");publishYmccGameWhitelist(a,data,true,rules);
            check(a["ymccWhitelistPublication"]["status"]!="warning"&&fileText(rules).find("nativegame")!=std::string::npos,"Nested native executable lost retained peer ownership");
            const auto state=data/L"state"/L"ymcc-game-whitelist-membership.json";
            const auto beforeState=fileText(state),beforeRules=fileText(rules);
            for(const auto* reason:{"wrong-appid","wrong-directory","helper-exe","missing-manifest"}) {
                auto forged=mixed;forged.erase("ymccMembershipAuthority");auto& row=forged["items"][1];const std::string kind=reason;
                if(kind=="wrong-appid")row["nativeSteamAppId"]=778900;
                if(kind=="wrong-directory")row["gameDirectory"]=toUtf8((p/L"OtherGame").wstring());
                if(kind=="helper-exe"){const auto helper=nativeDir/L"bin"/L"CrashLogsGenerator.exe";textFile(helper,"owned helper marker");row["primaryExecutable"]=toUtf8(helper.wstring());}
                if(kind=="missing-manifest")fs::remove(nativeManifest);
                publishYmccGameWhitelist(forged,data,true,rules);
                check(forged["ymccWhitelistPublication"]["status"]=="warning"&&fileText(state)==beforeState&&fileText(rules)==beforeRules,"Unverified native row changed account/manual ownership");
                if(kind=="missing-manifest")textFile(nativeManifest,manifest);
            }
            mixed.erase("ymccMembershipAuthority");publishYmccGameWhitelist(mixed,data,true,rules);
            check(mixed["ymccWhitelistPublication"]["status"]!="warning"&&fileText(rules)==beforeRules,"Explicit native proof recovery did not preserve rules");
            // An independently observed live addition must proceed even when
            // a retained peer no longer has one of its historical shortcuts.
            const auto partialRoot=p/L"Partial",partialData=partialRoot/L"data",partialRules=partialRoot/L"rules.txt";
            textFile(partialRules,"manual-user-rule\r\n");
            auto partialA=membershipEvent(partialRoot,"1001","Alpha"),partialB=membershipEvent(partialRoot,"1002","Beta");
            publishYmccGameWhitelist(partialA,partialData,true,partialRules);publishYmccGameWhitelist(partialB,partialData,true,partialRules);
            const auto partialState=partialData/L"state"/L"ymcc-game-whitelist-membership.json";
            auto retained=loadJsonDocument(partialState);const auto ghost=partialRoot/L"Games"/L"Ghost"/L"Ghost.exe";
            retained["accounts"]["1002"]["entries"][custom_steam_library::ymcc_whitelist::executableKey(toUtf8(ghost.wstring()))]="ghost";
            retained["vendorExtension"]={{"retained",7}};writeJsonAtomic(partialState,retained);
            const auto peer=fs::path(toWide(partialB["shortcutsVdf"].get<std::string>()));writeAtomic(peer,BinaryVdfDocument::emptySteamShortcuts().serialize());
            const auto current=fs::path(toWide(partialA["shortcutsVdf"].get<std::string>()));
            const auto alpha=fs::path(toWide(partialA["items"][0]["primaryExecutable"].get<std::string>()));
            const auto gamma=partialRoot/L"Games"/L"Gamma"/L"Gamma.exe";textFile(gamma,"owned inert marker, never executed");
            auto actual=BinaryVdfDocument::emptySteamShortcuts();auto* actualRows=vdfFindObject(actual.root(),"shortcuts");
            actualRows->objectValue.push_back({"0",steamShortcutNode(-1073741823,"Alpha",quotedSteamPath(alpha),quotedSteamPath(alpha.parent_path()),0)});
            actualRows->objectValue.push_back({"1",steamShortcutNode(-1073741822,"Gamma",quotedSteamPath(gamma),quotedSteamPath(gamma.parent_path()),0)});writeAtomic(current,actual.serialize());
            partialA.erase("ymccMembershipAuthority");partialA["shortcutsBeforeSha256"]=sha256(readBinaryFile(current));auto gammaRow=partialA["items"][0];gammaRow["primaryExecutable"]=toUtf8(gamma.wstring());partialA["items"].push_back(gammaRow);
            const auto retainedRules=fileText(partialRules);publishYmccGameWhitelist(partialA,partialData,false,partialRules);
            const auto published=fileText(partialRules);const bool newRule=published.find("gamma\r\n")!=std::string::npos;
            const bool oldRule=published.find("beta\r\n")!=std::string::npos,unknownWithheld=published.find("ghost")==std::string::npos;
            writeJsonAtomic(p/L"partial-publication-observations.json",{{"newRule",newRule},{"oldRule",oldRule},{"unknownWithheld",unknownWithheld},{"publication",partialA["ymccWhitelistPublication"]},{"rules",published}});
            check(newRule,"Unconfirmed peer debt blocked confirmed new game rules");
            check(oldRule&&unknownWithheld&&published.starts_with("manual-user-rule\r\n"),"Partial publication deleted prior/manual rules or manufactured unknown ownership");
            check(partialA["ymccWhitelistPublication"].value("partialPublication",false)&&partialA["ymccWhitelistPublication"]["unconfirmedEntryCount"]==2,"Partial publication hid unresolved membership");
            const auto detail = partialA["ymccWhitelistPublication"].find("unconfirmedMembership");
            check(detail != partialA["ymccWhitelistPublication"].end() && detail->is_object(),
                "MEMBERSHIP_WARNING_DETAILS_MISSING");
            const auto keyFor = [](const fs::path& exe) { return custom_steam_library::ymcc_whitelist::executableKey(toUtf8(exe.wstring())); };
            std::set<std::string> actualDetails;
            for (const auto& row : (*detail)["entries"]) {
                check(row["accountId"] == "1002" && row["reason"] == "current-membership-unconfirmed",
                    "Unconfirmed diagnostic changed account or asserted absence");
                actualDetails.insert(row["executable"].get<std::string>());
            }
            check((*detail)["complete"] == true && (*detail)["entryLimit"] == 32 &&
                actualDetails == std::set<std::string>{keyFor(ghost), keyFor(partialRoot/L"Games"/L"Beta"/L"Beta.exe")},
                "Unconfirmed diagnostic did not retain exact owned paths");
            const auto savedState = readBinaryFile(partialState);
            auto duplicate = loadJsonDocument(partialState);
            duplicate["accounts"]["1001"]["entries"][keyFor(ghost)] = "ghost";
            writeJsonAtomic(partialState, duplicate);
            partialA.erase("ymccMembershipAuthority");publishYmccGameWhitelist(partialA,partialData,false,partialRules);
            const auto crossAccount = partialA["ymccWhitelistPublication"];
            check(crossAccount["unconfirmedEntryCount"] == 3 &&
                crossAccount["unconfirmedMembership"]["entries"].size() == 3 &&
                crossAccount["unconfirmedMembership"]["complete"] == true,
                "Same EXE in different accounts was collapsed");
            writeJsonAtomic(p/L"cross-account-warning-observations.json", crossAccount);
            auto capped = loadJsonDocument(partialState);
            for (int i = 0; i < 35; ++i) {
                const auto name = L"Missing" + std::to_wstring(i);
                const auto exe = partialRoot/L"Games"/name/(name+L".exe");
                capped["accounts"]["1002"]["entries"][keyFor(exe)] = normalizeGameRule(toUtf8(exe.wstring()));
            }
            writeJsonAtomic(partialState, capped);
            const auto cappedBefore = readBinaryFile(partialState);
            partialA.erase("ymccMembershipAuthority");publishYmccGameWhitelist(partialA,partialData,false,partialRules);
            const auto cappedNotice = partialA["ymccWhitelistPublication"];
            check(cappedNotice["unconfirmedEntryCount"] == 38 &&
                cappedNotice["unconfirmedMembership"]["entries"].size() == 32 &&
                cappedNotice["unconfirmedMembership"]["complete"] == false &&
                fileText(partialRules) == published && readBinaryFile(partialState) == cappedBefore,
                "Bounded diagnostic lost count or changed rules/ownership");
            writeJsonAtomic(p/L"capped-warning-observations.json", cappedNotice);
            writeAtomic(partialState, savedState);
            partialA.erase("ymccMembershipAuthority");publishYmccGameWhitelist(partialA,partialData,false,partialRules);
            check(partialA["ymccWhitelistPublication"]["unconfirmedEntryCount"] == 2 &&
                partialA["ymccWhitelistPublication"]["unconfirmedMembership"]["entries"].size() == 2 &&
                partialA["ymccWhitelistPublication"]["unconfirmedMembership"]["complete"] == true &&
                fileText(partialRules) == published,
                "A prior truncated diagnostic survived a smaller observation");
            check(loadJsonDocument(partialState)["vendorExtension"]["retained"]==7,"Partial publication lost unknown membership fields");
            partialA.erase("ymccMembershipAuthority");publishYmccGameWhitelist(partialA,partialData,false,partialRules);check(fileText(partialRules)==published,"Repeated partial publication duplicated confirmed rules");
            withConfigReadDenied(peer,[&]{partialA.erase("ymccMembershipAuthority");publishYmccGameWhitelist(partialA,partialData,false,partialRules);check(fileText(partialRules)==published&&partialA["ymccWhitelistPublication"]["status"]=="warning","Unreadable peer bypassed original read protection");});
            textFile(partialRules,"manual\n# BEGIN YMCC custom-steam-library managed rules\n");const auto broken=fileText(partialRules);partialA.erase("ymccMembershipAuthority");publishYmccGameWhitelist(partialA,partialData,false,partialRules);check(fileText(partialRules)==broken&&partialA["ymccWhitelistPublication"]["status"]=="warning","Partial publication bypassed ambiguous marker refusal");textFile(partialRules,published);
            partialB.erase("ymccMembershipAuthority");partialB["shortcutsBeforeSha256"]=sha256(readBinaryFile(peer));partialB["items"]=json::array();publishYmccGameWhitelist(partialB,partialData,true,partialRules);
            check(partialB["ymccWhitelistPublication"]["status"]!="warning"&&fileText(partialRules).find("beta")==std::string::npos&&fileText(partialRules).find("gamma")!=std::string::npos&&fileText(partialRules).starts_with("manual-user-rule\r\n"),"Explicit complete peer recovery did not converge without losing current/manual rules");
            writeJsonAtomic(p/L"partial-publication-recovery.json",{{"publication",partialB["ymccWhitelistPublication"]},{"rules",fileText(partialRules)}});

        });
        for(const auto* change:{"wrong-account","wrong-executable","old-hash","future-schema"})run((std::string("txstorm-membership-reject-")+change).c_str(),[=](const fs::path& p){const auto data=p/L"data",rules=p/L"rules.txt";textFile(rules,"manual-user-rule\n");auto event=membershipEvent(p,"19627","Alpha");const std::string kind=change;if(kind=="wrong-account")event["accountId"]="99999";if(kind=="wrong-executable")event["items"][0]["primaryExecutable"]=toUtf8((p/L"NotInSteam.exe").wstring());if(kind=="old-hash")event["shortcutsBeforeSha256"]=std::string(64,'0');if(kind=="future-schema")event["schemaVersion"]=999;publishYmccGameWhitelist(event,data,true,rules);check(event["ymccWhitelistPublication"]["status"]=="warning"&&fileText(rules)=="manual-user-rule\n","Unverified membership event published rules");check(!fs::exists(data/L"state"/L"ymcc-game-whitelist-membership.json"),"Unverified event fabricated persisted ownership");});
        run("txstorm-membership-corrupt-state-retained-and-valid-repair-retries",[=](const fs::path& p){const auto data=p/L"data",rules=p/L"rules.txt",state=data/L"state"/L"ymcc-game-whitelist-membership.json";textFile(rules,"manual-user-rule\n");auto seed=membershipEvent(p,"19627","Alpha");publishYmccGameWhitelist(seed,data,true,rules);const auto trusted=fileText(state),priorRules=fileText(rules);const std::string bad="{";textFile(state,bad);auto event=membershipEvent(p,"19627","Alpha");publishYmccGameWhitelist(event,data,true,rules);check(fileText(state)==bad&&fileText(rules)==priorRules&&event["ymccWhitelistPublication"]["status"]=="warning","Corrupt membership became confirmed ownership");writeJsonAtomic(p/L"saved-bad-membership.json",{{"originalBytes",bad}});textFile(state,trusted);event.erase("ymccMembershipAuthority");publishYmccGameWhitelist(event,data,true,rules);check(fileText(rules).find("alpha")!=std::string::npos,"Explicit verified state repair could not retry");});
        run("txstorm-membership-rules-io-failure-is-warning-and-retry",[=](const fs::path& p){const auto data=p/L"data",rules=p/L"rules.txt";textFile(rules,"manual-user-rule\n");auto event=membershipEvent(p,"19627","Alpha");selftestMoveFault=[&](const auto&,const auto& to)->DWORD{return to==rules.wstring()?ERROR_WRITE_FAULT:ERROR_SUCCESS;};publishYmccGameWhitelist(event,data,true,rules);check(event["ymccWhitelistPublication"]["status"]=="warning"&&fileText(rules)=="manual-user-rule\n","Failed rules write claimed publication success");selftestMoveFault={};event.erase("ymccMembershipAuthority");publishYmccGameWhitelist(event,data,true,rules);check(fileText(rules).find("alpha")!=std::string::npos,"Durable membership did not retry the failed rules publication");});
        run("txstorm-membership-same-account-other-installation-is-unknown",[=](const fs::path& p){const auto data=p/L"data",rules=p/L"rules.txt";auto a=membershipEvent(p/L"A","19627","Alpha");publishYmccGameWhitelist(a,data,true,rules);const auto old=fileText(rules);auto b=membershipEvent(p/L"B","19627","Beta");publishYmccGameWhitelist(b,data,true,rules);check(b["ymccWhitelistPublication"]["status"]=="warning"&&fileText(rules)==old,"Same account ID from another installation replaced peer ownership");});

        run("txstorm-actual-undo-consumer-partial-copy-failure-and-retry",[](const fs::path& p){const auto data=p/L"data",game=p/L"Games"/L"Game";const auto id=gameDataId(game);const auto metadata=data/L"games"/toWide(id)/L"meta.json";textFile(metadata,"owned recovery metadata");const auto package=createDeletedGameRecoveryPackage(data,game,defaultLibraryConfig(),{{"items",json::object()}});check(fs::remove(metadata),"Cannot remove isolated metadata file");fs::create_directory(metadata);textFile(package/L"recovery-state.json","");mustThrow([&]{undoDeletedGameRecovery(data,game);});check(fileText(package/L"recovery-state.json").empty(),"Partial copy failure claimed successful recovery");check(fs::remove(metadata),"Cannot remove isolated directory conflict");const auto result=undoDeletedGameRecovery(data,game);check(fileText(metadata)=="owned recovery metadata"&&result["gameInstallFilesIncluded"]==false,"Actual undo did not restore owned metadata");const auto manifest=loadJsonDocument(package/L"recovery-manifest.json");check(deletedRecoveryConsumed(package,manifest),"Verified undo did not publish a bound completion");mustThrow([&]{latestDeletedGameRecoveryPackage(data,game);});bool original=false;for(const auto& entry:fs::directory_iterator(data/L"backups"/L"corrupt"))if(fileText(entry.path()).empty())original=true;check(original,"Corrupt consumed marker original was not preserved");});
        run("txstorm-recovery-state-wrong-type-cannot-claim-returned",[](const fs::path& p){const auto data=p/L"data",game=p/L"Games"/L"Game";const auto package=createDeletedGameRecoveryPackage(data,game,defaultLibraryConfig(),{{"items",json::object()}});const auto manifest=loadJsonDocument(package/L"recovery-manifest.json");writeJsonAtomic(package/L"recovery-state.json",{{"schemaVersion",1},{"gameDataId",gameDataId(game)},{"manifestSha256",sha256(readBinaryFile(package/L"recovery-manifest.json"))},{"restoredAt",uint64_t{1}},{"restoredFiles","success"},{"mergeDidNotOverwriteNewerFiles",true}});check(!deletedRecoveryConsumed(package,manifest)&&latestDeletedGameRecoveryPackage(data,game)==package,"Wrong-type return marker became consumed debt");});
        for(const auto* change:{"wrong-type-existence","escaped-stage","escaped-target"})run((std::string("txstorm-pending-")+change).c_str(),[=](const fs::path& p){Fixture f(p,true);const auto journal=createDebt(f);auto record=loadJsonDocument(journal);const std::string kind=change;if(kind=="wrong-type-existence")record["targetExisted"]="true";if(kind=="escaped-stage")record["stagedFiles"].push_back(toUtf8((p/L"outside.yeman-stage-file").wstring()));if(kind=="escaped-target")record["targetPath"]=toUtf8((p/L"unrelated.txt").wstring());writeJsonAtomic(journal,record);const auto before=readBinaryFile(f.target.path);cleanupInterruptedSteamTransactions(f.data);check(fs::exists(journal)&&readBinaryFile(f.target.path)==before,"Bad field/path erased debt or changed external state");mustThrow([&]{requireResolvedSteamTransactions(f.data,f.target.path);});});

        run("txstorm-oversize-and-deep-pending-stay-unresolved",[=](const fs::path& p){Fixture f(p,true);const auto journal=createDebt(f);const auto original=readBinaryFile(f.target.path);for(const auto& bad:std::vector<std::string>{std::string((8u<<20)+1,'x'),std::string(512,'[')+"0"+std::string(512,']')}){textFile(journal,bad);cleanupInterruptedSteamTransactions(f.data);mustThrow([&]{requireResolvedSteamTransactions(f.data,f.target.path);});check(fileText(journal)==bad&&readBinaryFile(f.target.path)==original,"Oversize/deep debt was erased or applied");}});

        // Same original suite, selected only by --data-root-storm; no new registered ID.
        for (const bool buildLayout : {false, true}) run(buildLayout ? "dataroot-startup-cli-build-layout" : "dataroot-startup-cli-packaged-layout", [&](const fs::path& p){
            const auto lab=p/L"CustomSteamLibrary",data=lab/L"data";
            auto debt=dataRootMigration(data,lab);debt["sources"]=json::array({toUtf8((lab/L"Steam大屏").wstring())});
            writeJsonAtomic(data/L"state"/L"data-migration.json",debt);
            auto config=defaultLibraryConfig();config["roots"]=json::array({toUtf8((p/L"MyGames").wstring())});writeJsonAtomic(data/L"config"/L"library-config.json",config);
            const auto before=fileText(data/L"config"/L"library-config.json"),debtBefore=fileText(data/L"state"/L"data-migration.json");
            const bool oldPorts=discoveryProducerPorts;discoveryProducerPorts=true;
            SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",data.c_str());
            std::vector<std::wstring> args{(buildLayout?lab/L"build"/L"SteamArtworkLab.exe":lab/L"SteamArtworkLab.exe").wstring(),L"--cleanup-interrupted-transactions",L"--expected-data-root",data.wstring()};
            std::vector<wchar_t*> argv;for(auto& arg:args)argv.push_back(arg.data());
            const auto code=workerMainNotUsedBySelftest(static_cast<int>(argv.size()),argv.data());discoveryProducerPorts=oldPorts;
            check(code==0&&validDataRootManifest(readDataRootJournal(data/L"data-root.json",1u<<20),data),"Original startup CLI did not initialize selected root");
            check(fileText(data/L"config"/L"library-config.json")==before&&fileText(data/L"state"/L"data-migration.json")==debtBefore,"Startup CLI changed existing settings/debt");
        });
        run("dataroot-startup-cli-selection-change-no-publication",[&](const fs::path& p){
            const auto data=p/L"selected",foreign=p/L"changed";SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",data.c_str());
            const bool oldPorts=discoveryProducerPorts;discoveryProducerPorts=true;
            std::vector<std::wstring> args{(p/L"SteamArtworkLab.exe").wstring(),L"--cleanup-interrupted-transactions",L"--expected-data-root",foreign.wstring()};
            std::vector<wchar_t*> argv;for(auto& arg:args)argv.push_back(arg.data());const auto code=workerMainNotUsedBySelftest(static_cast<int>(argv.size()),argv.data());discoveryProducerPorts=oldPorts;
            check(code!=0&&!fs::exists(data)&&!fs::exists(foreign),"Changed root was initialized before expected-root validation");
        });
        auto rootInit=[](const fs::path& data,const fs::path& lab){
            SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",data.c_str());
            return dataRootReady([&]{return initializeSteamBigPictureDataRoot(data,lab);});
        };
        run("dataroot-missing-manifest-saves-and-rereads",[&](const fs::path& p){const auto data=p/L"data";check(rootInit(data,p),"Fresh trusted selection unavailable");const auto doc=loadJsonDocument(data/L"data-root.json");check(doc["dataRoot"]==toUtf8(data.wstring())&&doc["schemaVersion"]==2,"Saved root identity mismatched");check(rootInit(data,p),"Saved manifest not reusable");});
        for(const auto* kind:{"empty","truncated","array","null","string","schema-type","schema-float","future","missing-root","wrong-root","root-type","root-relative","root-nul","layout-type","layout-future","created-type","portable-type","duplicate","escaped-duplicate","too-deep","oversize"}) {
            const auto label=std::string("dataroot-invalid-manifest-")+kind;
            run(label.c_str(),[&,kind](const fs::path& p){const auto data=p/L"data",path=data/L"data-root.json";auto doc=dataRootManifest(data);std::string raw;const std::string k=kind;
                if(k=="empty")raw="";else if(k=="truncated")raw="{\"schemaVersion\":2";else if(k=="array")raw="[]";else if(k=="null")raw="null";else if(k=="string")raw="\"other-root\"";
                else if(k=="duplicate")raw="{\"schemaVersion\":99,\"schemaVersion\":2,\"dataRoot\":"+doc["dataRoot"].dump()+"}";
                else if(k=="escaped-duplicate")raw="{\"schemaVersion\":99,\"schema\\u0056ersion\":2,\"dataRoot\":"+doc["dataRoot"].dump()+"}";
                else if(k=="too-deep")raw=std::string(100,'[')+"0"+std::string(100,']');else if(k=="oversize")raw=std::string((1u<<20)+1,'x');
                else {if(k=="schema-type")doc["schemaVersion"]="2";if(k=="schema-float")doc["schemaVersion"]=2.0;if(k=="future")doc["schemaVersion"]=99;if(k=="missing-root")doc.erase("dataRoot");if(k=="wrong-root")doc["dataRoot"]=toUtf8((p/L"foreign").wstring());if(k=="root-type")doc["dataRoot"]=false;if(k=="root-relative")doc["dataRoot"]="relative";if(k=="root-nul")doc["dataRoot"]=toUtf8(data.wstring())+std::string(1,'\0')+"other";if(k=="layout-type")doc["dataLayoutVersion"]="2";if(k=="layout-future")doc["dataLayoutVersion"]=99;if(k=="created-type")doc["createdAt"]="now";if(k=="portable-type")doc["portableReinstallData"]="true";raw=doc.dump();}
                textFile(path,raw);for(int n=0;n<8;++n){check(!rootInit(data,p),"Bad identity ACKed writable root");check(fileText(path)==raw,"Bad root original overwritten");check(!fs::exists(data/L"config"/L"library-config.json"),"Readonly fallback persisted defaults");}
                writeJsonAtomic(path,dataRootManifest(data));check(rootInit(data,p),"Trusted original repair was not retried");check(loadJsonDocument(path)["userAnnotation"]["keep"]==true,"Unknown valid fields lost");
            });
        }
        run("dataroot-legacy-manifest-keeps-valid-unknown-fields",[&](const fs::path& p){const auto data=p/L"data",path=data/L"data-root.json";writeJsonAtomic(path,dataRootManifest(data,1));check(rootInit(data,p),"Legal legacy manifest rejected");check(loadJsonDocument(path)["userAnnotation"]["keep"]==true,"Legacy annotation lost");});
        run("dataroot-manifest-lock-release-and-readonly",[&](const fs::path& p){const auto data=p/L"data",path=data/L"data-root.json";writeJsonAtomic(path,dataRootManifest(data));const auto raw=fileText(path);HANDLE held=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Cannot lock fixture");bool ready=true;try{ready=rootInit(data,p);}catch(...){CloseHandle(held);throw;}CloseHandle(held);check(!ready&&fileText(path)==raw&&!fs::exists(data/L"config"/L"library-config.json"),"IO became missing/default or ACK");check(rootInit(data,p),"Unlocked valid root not retried");});
        run("dataroot-manifest-directory-is-unknown",[&](const fs::path& p){const auto data=p/L"data",path=data/L"data-root.json";fs::create_directories(path);check(!rootInit(data,p)&&fs::is_directory(path),"Directory accepted or removed");fs::remove(path);check(rootInit(data,p),"Removed IO obstruction not retried");});
        run("dataroot-manifest-read-denied-keeps-original",[&](const fs::path& p){const auto data=p/L"data",path=data/L"data-root.json";writeJsonAtomic(path,dataRootManifest(data));const auto raw=fileText(path);withConfigReadDenied(path,[&]{check(!rootInit(data,p),"Unreadable manifest ACKed");});check(fileText(path)==raw&&rootInit(data,p),"ACL recovery lost original or retry");});
        for(int fault:{1,2,3})run(("dataroot-save-failure-no-ack-"+std::to_string(fault)).c_str(),[&,fault](const fs::path& p){const auto data=p/L"data",path=data/L"data-root.json";configIoTarget=L"data-root.json";configIoMode=fault==3?0:fault;if(fault==3)selftestMoveFault=[&](const auto&,const auto& to){return to==path.wstring()?ERROR_WRITE_FAULT:0;};mustThrow([&]{rootInit(data,p);});check(!fs::exists(path),"Failed save published identity");configIoMode=0;selftestMoveFault={};check(rootInit(data,p),"Write-fault release did not recover");});
        for(bool absent:{false,true})run((std::string("dataroot-concurrent-manifest-change-")+(absent?"created":"replaced")).c_str(),[&,absent](const fs::path& p){const auto data=p/L"data",path=data/L"data-root.json";if(!absent)writeJsonAtomic(path,dataRootManifest(data,1));bool changed=false;auto foreign=dataRootManifest(data);foreign["newerUserValue"]="must retain";selftestMoveFault=[&](const auto&,const auto& to){if(to==path.wstring()&&!changed){changed=true;writeJsonAtomic(path,foreign);return absent?DWORD{0}:DWORD{ERROR_SHARING_VIOLATION};}return DWORD{0};};mustThrow([&]{rootInit(data,p);});selftestMoveFault={};check(changed&&loadJsonDocument(path)==foreign,"Concurrent identity clobbered");check(rootInit(data,p)&&loadJsonDocument(path)["newerUserValue"]=="must retain","Next trusted reread unavailable");});
        for(const auto* kind:{"empty","truncated","array","schema-type","future","missing-target","wrong-target","target-type","sources-type","source-outside","source-relative","count-type","count-negative","count-overflow","missing-completion","completion-type","duplicate","oversize"}) {
            run((std::string("dataroot-invalid-migration-")+kind).c_str(),[&,kind](const fs::path& p){const auto data=p/L"data",lab=p/L"lab",path=data/L"state"/L"data-migration.json";auto doc=dataRootMigration(data,lab);const std::string k=kind;std::string raw;
                if(k=="empty")raw="";else if(k=="truncated")raw="{";else if(k=="array")raw="[]";else if(k=="oversize")raw=std::string((4u<<20)+1,'x');else if(k=="duplicate")raw="{\"schemaVersion\":1,\"schemaVersion\":2}";
                else {if(k=="schema-type")doc["schemaVersion"]="2";if(k=="future")doc["schemaVersion"]=99;if(k=="missing-target")doc.erase("targetDataRoot");if(k=="wrong-target")doc["targetDataRoot"]=toUtf8((p/L"foreign").wstring());if(k=="target-type")doc["targetDataRoot"]=7;if(k=="sources-type")doc["sources"]=true;if(k=="source-outside")doc["sources"]=json::array({toUtf8((p/L"foreign").wstring())});if(k=="source-relative")doc["sources"]=json::array({"relative"});if(k=="count-type")doc["filesCopied"]="1";if(k=="count-negative")doc["filesCopied"]=-1;if(k=="count-overflow")doc["filesCopied"]=1e100;if(k=="missing-completion")doc.erase("completedAt");if(k=="completion-type")doc["completedAt"]=false;raw=doc.dump();}
                textFile(path,raw);textFile(lab/L"Steam大屏"/L"state"/L"private.txt","source must not be adopted");for(int n=0;n<4;++n){check(migrateLegacyData(data,lab).empty(),"Unknown migration claimed repair/completion");check(fileText(path)==raw&&!fs::exists(data/L"state"/L"private.txt"),"Unknown migration mutated original/adopted source");}
                SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",nullptr);check(!dataRootReady([&]{return initializeSteamBigPictureDataRoot(data,lab);}),"Unknown migration admitted writable initialization");check(!fs::exists(data/L"data-root.json"),"Unknown migration issued root identity");
                auto repaired=dataRootMigration(data,lab);writeJsonAtomic(path,repaired);check(migrateLegacyData(data,lab).empty()&&loadJsonDocument(path)==repaired,"Trusted completed marker was not reread");
            });
        }
        run("dataroot-migration-lock-release-keeps-debt",[&](const fs::path& p){const auto data=p/L"data",lab=p/L"lab",path=data/L"state"/L"data-migration.json";writeJsonAtomic(path,dataRootMigration(data,lab));const auto raw=fileText(path);HANDLE held=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Cannot lock migration");try{check(migrateLegacyData(data,lab).empty(),"Locked debt adopted");}catch(...){CloseHandle(held);throw;}CloseHandle(held);check(fileText(path)==raw&&migrateLegacyData(data,lab).empty(),"Debt changed after lock release");});
        run("dataroot-migration-valid-legacy-normalization-preserves-unknown",[&](const fs::path& p){const auto data=p/L"data",lab=p/L"lab",path=data/L"state"/L"data-migration.json";writeJsonAtomic(path,dataRootMigration(data,lab,1));fs::create_directories(lab/L"Steam大屏");check(migrateLegacyData(data,lab)==path,"Legacy normalization unavailable");const auto doc=loadJsonDocument(path);check(doc["schemaVersion"]==2&&doc["note"]["retain"]=="legacy annotation","Legacy unknown fields lost");});
        run("dataroot-migration-failed-copy-keeps-pending-and-retries",[&](const fs::path& p){const auto data=p/L"data",lab=p/L"lab",path=data/L"state"/L"data-migration.json",source=lab/L"Steam大屏"/L"state"/L"owned.txt";textFile(source,"trusted legacy bytes");fs::create_directories(data);HANDLE held=CreateFileW(source.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Cannot lock legacy source");try{mustThrow([&]{migrateLegacyData(data,lab);});}catch(...){CloseHandle(held);throw;}CloseHandle(held);check(fs::is_regular_file(path),"Interrupted migration lost persistent debt");const auto pending=loadJsonDocument(path);check(pending.contains("phase")&&pending["phase"]!="committed"&&!pending.contains("completedAt"),"Interrupted migration claimed completion");check(migrateLegacyData(data,lab)==path&&fileText(data/L"state"/L"owned.txt")=="trusted legacy bytes","Released migration did not retry actual copy");check(loadJsonDocument(path)["phase"]=="committed","Retry did not readback/commit migration");});


        run("dataroot-current-manifest-is-readonly-idempotent",[&](const fs::path& p){const auto data=p/L"data",path=data/L"data-root.json";writeJsonAtomic(path,dataRootManifest(data));check(rootInit(data,p),"Initial root unavailable");const auto raw=fileText(path);int publications=0;selftestMoveFault=[&](const auto&,const auto& to){if(to==path.wstring())++publications;return DWORD{0};};for(int n=0;n<32;++n)check(rootInit(data,p),"Healthy explicit retry failed");selftestMoveFault={};check(publications==0&&fileText(path)==raw,"Healthy root record was rewritten repeatedly");});
        run("dataroot-manifest-valid-large-unknown-fields-survive",[&](const fs::path& p){const auto data=p/L"data",path=data/L"data-root.json";auto doc=dataRootManifest(data);doc["customNotes"]=std::string(700000,'n');textFile(path,doc.dump());const auto raw=fileText(path);check(rootInit(data,p)&&fileText(path)==raw,"Legal large record/unknown data lost");});
        run("dataroot-root-junction-is-unknown-without-following",[&](const fs::path& p){const auto target=p/L"foreign",data=p/L"data";fs::create_directories(target);check(createFixtureJunction(data,target),"Fixture junction unavailable");check(!rootInit(data,p),"Reparse root ACKed");check(fs::is_empty(target),"Reparse root followed for defaults");});
        for(int fault:{1,2})run(("dataroot-migration-file-staging-failure-"+std::to_string(fault)).c_str(),[&,fault](const fs::path& p){const auto data=p/L"data",lab=p/L"lab",source=lab/L"Steam大屏"/L"state"/L"owned.txt",target=data/L"state"/L"owned.txt",marker=data/L"state"/L"data-migration.json";textFile(source,"private verified original");configIoTarget=L"owned.txt";configIoMode=fault;mustThrow([&]{migrateLegacyData(data,lab);});check(fs::is_regular_file(marker)&&!loadJsonDocument(marker).contains("completedAt")&&!fs::exists(target),"Failed staging left plausible partial target or lost debt");configIoMode=0;check(migrateLegacyData(data,lab)==marker&&fileText(target)=="private verified original","Released staging failure not recoverable");});
        run("dataroot-migration-commit-write-failure-keeps-normalizing",[&](const fs::path& p){const auto data=p/L"data",lab=p/L"lab",marker=data/L"state"/L"data-migration.json";textFile(lab/L"Steam大屏"/L"state"/L"owned.txt","private original");selftestMoveFault=[&](const auto& from,const auto& to){return to==marker.wstring()&&loadJsonDocument(from).value("phase",std::string{})=="committed"?DWORD{ERROR_WRITE_FAULT}:DWORD{0};};mustThrow([&]{migrateLegacyData(data,lab);});selftestMoveFault={};const auto pending=loadJsonDocument(marker);check(pending["phase"]=="normalizing"&&!pending.contains("completedAt"),"Failed commit erased pending or ACKed success");check(migrateLegacyData(data,lab)==marker&&loadJsonDocument(marker)["phase"]=="committed","Commit retry did not complete");});
        run("dataroot-migration-normalization-failure-keeps-debt",[&](const fs::path& p){const auto data=p/L"data",lab=p/L"lab",legacy=lab/L"Steam大屏",note=data/L"state"/L"note.json",marker=data/L"state"/L"data-migration.json";fs::create_directories(legacy);writeJsonAtomic(note,{{"oldPath",toUtf8((legacy/L"old").wstring())}});const auto raw=fileText(note);selftestMoveFault=[&](const auto&,const auto& to){return to==note.wstring()?DWORD{ERROR_WRITE_FAULT}:DWORD{0};};mustThrow([&]{migrateLegacyData(data,lab);});selftestMoveFault={};check(fileText(note)==raw&&loadJsonDocument(marker)["phase"]=="normalizing"&&!loadJsonDocument(marker).contains("completedAt"),"Normalization failure claimed complete");check(migrateLegacyData(data,lab)==marker&&loadJsonDocument(note)["oldPath"]==toUtf8((data/L"old").wstring()),"Normalization retry did not actually rewrite");});
        run("dataroot-migration-pending-loose-source-loss-remains-unknown",[&](const fs::path& p){const auto data=p/L"data",lab=p/L"lab",source=lab/L"library-config.json",marker=data/L"state"/L"data-migration.json";writeJsonAtomic(source,{{"ownedOriginal",true}});selftestMoveFault=[&](const auto&,const auto& to){return to==(data/L"config"/L"library-config.json").wstring()?DWORD{ERROR_WRITE_FAULT}:DWORD{0};};mustThrow([&]{migrateLegacyData(data,lab);});selftestMoveFault={};check(fs::is_regular_file(marker),"No pending marker for loose source");fs::remove(source);const auto pending=fileText(marker);mustThrow([&]{migrateLegacyData(data,lab);});check(fileText(marker)==pending,"Missing recorded source discarded debt");writeJsonAtomic(source,{{"ownedOriginal",true}});check(migrateLegacyData(data,lab)==marker,"Repaired original loose source not retried");});
        run("dataroot-migration-concurrent-record-change-is-preserved",[&](const fs::path& p){const auto data=p/L"data",lab=p/L"lab",marker=data/L"state"/L"data-migration.json";fs::create_directories(lab/L"Steam大屏");auto foreign=dataRootMigration(data,lab);foreign["schemaVersion"]=99;bool changed=false;selftestMoveFault=[&](const auto& from,const auto& to){if(to==marker.wstring()&&!changed&&loadJsonDocument(from).value("phase",std::string{})=="committed"){changed=true;writeJsonAtomic(marker,foreign);return DWORD{ERROR_SHARING_VIOLATION};}return DWORD{0};};mustThrow([&]{migrateLegacyData(data,lab);});selftestMoveFault={};check(changed&&loadJsonDocument(marker)==foreign,"Concurrent migration record overwritten");check(migrateLegacyData(data,lab).empty()&&loadJsonDocument(marker)==foreign,"Future external record implicitly retried/normalized");});
        run("dataroot-explicit-root-pending-is-readonly-without-import",[&](const fs::path& p){const auto data=p/L"data",lab=p/L"lab",marker=data/L"state"/L"data-migration.json";auto pending=dataRootMigration(data,lab);pending.erase("completedAt");pending["phase"]="copying";pending["startedAt"]=uint64_t{1};pending["unknownDebt"]={{"keep",true}};writeJsonAtomic(marker,pending);const auto raw=fileText(marker);textFile(lab/L"Steam大屏"/L"state"/L"owned.txt","do not import implicitly");for(int n=0;n<8;++n)check(!rootInit(data,lab),"Pending explicit root activated");check(fileText(marker)==raw&&!fs::exists(data/L"state"/L"owned.txt"),"Explicit root auto-imported or lost debt");check(migrateLegacyData(data,lab)==marker&&loadJsonDocument(marker)["unknownDebt"]["keep"]==true&&rootInit(data,lab),"Explicit trusted migration retry lost annotations or did not recover");});


        // This is the original datachain's exact identity selection, not a new suite.

        // Original membership consumer: fake Steam VDF bytes, owned files only.
        auto membershipSeed=[&](const fs::path& p){const auto data=p/L"data",rules=p/L"rules.txt";textFile(rules,"manual-user-rule\n");auto event=membershipEvent(p,"19627","Alpha");publishYmccGameWhitelist(event,data,true,rules);check(event["ymccWhitelistPublication"]["status"]!="warning","Valid membership seed failed");return event;};
        for(const auto* defect:{"schema-float","schema-bool","schema-string","schema-future","accounts-array","account-array","empty-account","nondigit-account","revision-string","revision-negative","revision-float","revision-bool","entries-array","entry-empty-key","entry-relative-key","entry-rule-type","bindings-array","binding-row-array","binding-path-type","binding-relative","binding-wrong-account","binding-wrong-config","binding-orphan-array"})run((std::string("txstorm-membership-contract-")+defect).c_str(),[&,defect](const fs::path& p){
            namespace m=custom_steam_library::ymcc_whitelist;auto event=membershipSeed(p);const auto data=p/L"data",state=data/L"state"/L"ymcc-game-whitelist-membership.json",rules=p/L"rules.txt";
            const auto trusted=fileText(state),priorRules=fileText(rules);auto bad=loadJsonDocument(state);const std::string kind=defect;auto& account=bad["accounts"]["19627"];
            if(kind=="schema-float")bad["schemaVersion"]=1.25;if(kind=="schema-bool")bad["schemaVersion"]=true;if(kind=="schema-string")bad["schemaVersion"]="1";if(kind=="schema-future")bad["schemaVersion"]=99;
            if(kind=="accounts-array")bad["accounts"]=json::array();if(kind=="account-array")account=json::array();
            if(kind=="empty-account"||kind=="nondigit-account"){bad["accounts"][kind=="empty-account"?"":"account-unknown"]=account;}
            if(kind=="revision-string")account["revision"]="1";if(kind=="revision-negative")account["revision"]=-1;if(kind=="revision-float")account["revision"]=1.5;if(kind=="revision-bool")account["revision"]=true;
            if(kind=="entries-array")account["entries"]=json::array();if(kind=="entry-empty-key")account["entries"][""]="ghost";if(kind=="entry-relative-key")account["entries"]["relative.exe"]="ghost";if(kind=="entry-rule-type")account["entries"].begin().value()=false;
            if(kind=="bindings-array")bad["bindings"]=json::array();if(kind=="binding-row-array")bad["bindings"]["19627"]=json::array();if(kind=="binding-path-type")bad["bindings"]["19627"]["shortcutsVdf"]=7;
            if(kind=="binding-relative")bad["bindings"]["19627"]["shortcutsVdf"]="userdata/19627/config/shortcuts.vdf";
            if(kind=="binding-wrong-account")bad["bindings"]["19627"]["shortcutsVdf"]=toUtf8((p/L"Steam"/L"userdata"/L"999"/L"config"/L"shortcuts.vdf").wstring());
            if(kind=="binding-wrong-config")bad["bindings"]["19627"]["shortcutsVdf"]=toUtf8((p/L"Steam"/L"userdata"/L"19627"/L"wrong"/L"shortcuts.vdf").wstring());
            if(kind=="binding-orphan-array")bad["bindings"]["999"]=json::array();
            writeJsonAtomic(state,bad);const auto original=fileText(state);event.erase("ymccMembershipAuthority");publishYmccGameWhitelist(event,data,true,rules);
            check(event["ymccWhitelistPublication"]["status"]=="warning"&&fileText(state)==original&&fileText(rules)==priorRules,"Invalid membership must remain unknown without changing ownership or rules");
            textFile(p/L"preserved-original.json",original);textFile(state,trusted);event.erase("ymccMembershipAuthority");publishYmccGameWhitelist(event,data,true,rules);check(event["ymccWhitelistPublication"]["status"]!="warning"&&fileText(rules)==priorRules,"Trusted ownership repair did not explicitly retry");
        });
        for(const auto* defect:{"empty","truncated","array","duplicate-root","duplicate-revision","over-limit"})run((std::string("txstorm-membership-reader-")+defect).c_str(),[&,defect](const fs::path& p){
            auto event=membershipSeed(p);const auto data=p/L"data",state=data/L"state"/L"ymcc-game-whitelist-membership.json",rules=p/L"rules.txt";const auto trusted=fileText(state),priorRules=fileText(rules);const std::string kind=defect;std::string raw;
            if(kind=="empty")raw="";if(kind=="truncated")raw="{";if(kind=="array")raw="[]";
            if(kind=="duplicate-root"){auto value=loadJsonDocument(state);raw="{\"schemaVersion\":1,\"schemaVersion\":1,\"owner\":\"custom-steam-library\",\"accounts\":"+value["accounts"].dump()+",\"bindings\":"+value["bindings"].dump()+"}";}
            if(kind=="duplicate-revision"){auto value=loadJsonDocument(state);const auto row=value["accounts"]["19627"];raw="{\"schemaVersion\":1,\"owner\":\"custom-steam-library\",\"accounts\":{\"19627\":{\"revision\":1,\"revision\":1,\"entries\":"+row["entries"].dump()+"}},\"bindings\":"+value["bindings"].dump()+"}";}
            if(kind=="over-limit"){auto value=loadJsonDocument(state);value["userNote"]=std::string(1u<<20,'x');raw=value.dump();}
            textFile(state,raw);event.erase("ymccMembershipAuthority");publishYmccGameWhitelist(event,data,true,rules);check(event["ymccWhitelistPublication"]["status"]=="warning"&&fileText(state)==raw&&fileText(rules)==priorRules,"Bad raw membership was consumed or overwritten");textFile(p/L"preserved-original.bin",raw);textFile(state,trusted);event.erase("ymccMembershipAuthority");publishYmccGameWhitelist(event,data,true,rules);check(event["ymccWhitelistPublication"]["status"]!="warning","Raw-state trusted repair blocked explicit retry");
        });
        run("txstorm-membership-header-unknown-row-fields-survive",[&](const fs::path& p){namespace m=custom_steam_library::ymcc_whitelist;auto event=membershipSeed(p);auto state=loadJsonDocument(p/L"data"/L"state"/L"ymcc-game-whitelist-membership.json");state["accounts"]["19627"]["unknownDebt"]={{"keep",true}};state["rootNote"]="retain";state["bindings"]["19627"]["bindingNote"]="retain";event.erase("ymccMembershipAuthority");event["mode"]="steam-library-delete-commit";event["committed"]=true;event["items"][0]["status"]="ready-to-delete";m::prepareLiveEvent(event,state,false);const auto update=m::applyPrepared(state,event,normalizeGameRule);check(update.accepted&&update.membership["accounts"]["19627"].contains("unknownDebt")&&update.membership["accounts"]["19627"]["unknownDebt"]["keep"]==true&&update.membership["rootNote"]=="retain"&&update.membership["bindings"]["19627"]["bindingNote"]=="retain","Header update discarded unknown ownership/debt fields");});
        run("txstorm-membership-legacy-unbound-debt-is-inactive",[&](const fs::path& p){namespace m=custom_steam_library::ymcc_whitelist;membershipSeed(p);auto state=loadJsonDocument(p/L"data"/L"state"/L"ymcc-game-whitelist-membership.json");state.erase("bindings");const auto original=state;m::validateMembership(state);mustThrow([&]{m::unionRules(state);});check(state==original,"Legacy unknown debt was cleared instead of held");});
        run("txstorm-membership-read-lock-release-retry",[&](const fs::path& p){auto event=membershipSeed(p);const auto data=p/L"data",state=data/L"state"/L"ymcc-game-whitelist-membership.json",rules=p/L"rules.txt";const auto before=fileText(state),priorRules=fileText(rules);HANDLE handle=CreateFileW(custom_steam_library::ioPath(state).c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(handle!=INVALID_HANDLE_VALUE,"Owned membership lock setup failed");event.erase("ymccMembershipAuthority");publishYmccGameWhitelist(event,data,true,rules);CloseHandle(handle);check(event["ymccWhitelistPublication"]["status"]=="warning"&&fileText(state)==before&&fileText(rules)==priorRules,"Read lock became missing/empty ownership");event.erase("ymccMembershipAuthority");publishYmccGameWhitelist(event,data,true,rules);check(event["ymccWhitelistPublication"]["status"]!="warning","Released membership lock could not retry");});
        run("txstorm-membership-revision-overflow-retains-debt",[&](const fs::path& p){namespace m=custom_steam_library::ymcc_whitelist;auto event=membershipSeed(p);auto state=loadJsonDocument(p/L"data"/L"state"/L"ymcc-game-whitelist-membership.json");state["accounts"]["19627"]["revision"]=(std::numeric_limits<uint64_t>::max)();const auto original=state;event.erase("ymccMembershipAuthority");event["mode"]="steam-library-delete-commit";event["committed"]=true;event["items"][0]["status"]="ready-to-delete";m::prepareLiveEvent(event,state,false);mustThrow([&]{m::applyPrepared(state,event,normalizeGameRule);});check(state==original,"Overflow rewrote ownership/debt");});
        run("txstorm-membership-large-legal-unknown-fields-roundtrip",[&](const fs::path& p){auto event=membershipSeed(p);const auto data=p/L"data",state=data/L"state"/L"ymcc-game-whitelist-membership.json",rules=p/L"rules.txt",target=fs::path(toWide(event["shortcutsVdf"].get<std::string>()));auto value=loadJsonDocument(state);auto doc=BinaryVdfDocument::parse(readBinaryFile(target));auto* shortcuts=vdfFindObject(doc.root(),"shortcuts");for(int i=0;i<1800;++i){const auto name="Game"+std::to_string(i);const auto exe=p/L"Games"/toWide(name)/(toWide(name)+L".exe");shortcuts->objectValue.push_back({std::to_string(i+1),steamShortcutNode(-1073000000+i,name,quotedSteamPath(exe),quotedSteamPath(exe.parent_path()),0)});value["accounts"]["19627"]["entries"][custom_steam_library::ymcc_whitelist::executableKey(toUtf8(exe.wstring()))]=normalizeGameRule(toUtf8(exe.wstring()));}writeAtomic(target,doc.serialize());event["shortcutsBeforeSha256"]=sha256(readBinaryFile(target));value["userNote"]=std::string(150000,'n');value["accounts"]["19627"]["unknownDebt"]={{"keep",true}};value["bindings"]["19627"]["extra"]={{"keep",true}};writeJsonAtomic(state,value);check(fileText(state).size()>300000&&fileText(state).size()<(1u<<20),"Large legal fixture size outside intended reader budget");event.erase("ymccMembershipAuthority");publishYmccGameWhitelist(event,data,false,rules);check(event["ymccWhitelistPublication"]["status"]!="warning","Large legal membership rejected");auto saved=loadJsonDocument(state);check(saved["userNote"]==value["userNote"]&&saved["accounts"]["19627"]["unknownDebt"]["keep"]==true&&saved["bindings"]["19627"]["extra"]["keep"]==true&&saved["accounts"]["19627"]["entries"].size()==1801,"Large membership lost entries or unknown fields");});
        check(!memberships||(!reports.empty()&&identityCallerNetwork==0&&identityCallerProcesses==0),"Membership selection empty or external port invoked");
        run("identitycaller-valid-cache-through-header",[](const fs::path& p){
            IdentityCallerFixture f(p);f.save(f.record());const auto result=f.read();check(result&&(*result)["match"]["appId"]==555,"Valid cache did not reach actual caller");
            textFile(f.executable,"owned existing game marker, never launched");
            const auto directory=f.executable.parent_path(),statePath=f.data/L"state"/L"library-scan.json",planPath=f.data/L"state"/L"existing-plan.json";
            writeJsonAtomic(statePath,{{"schemaVersion",1},{"games",json::array({{{"gameDirectory",toUtf8(directory.wstring())},{"directoryName","Resident Evil 2"},{"primaryExecutable",f.exe},{"primaryRole","main-game"},{"status","ready"}}})},{"missingGames",json::array()},{"unscannedGames",json::array()}});
            const auto steam=p/L"Steam",vdf=steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf";textFile(steam/L"steam.exe","owned fake Steam marker, never launched");
            auto shortcut=BinaryVdfDocument::emptySteamShortcuts();vdfFindObject(shortcut.root(),"shortcuts")->objectValue.push_back({"0",steamShortcutNode(-1073741823,"Homeworld: Deserts of Kharak",quotedSteamPath(f.executable),quotedSteamPath(directory),0)});writeAtomic(vdf,shortcut.serialize());
            const auto originalVdf=readBinaryFile(vdf);const SteamShortcutTarget target{vdf,"19627"};
            auto match=IdentityCallerFixture::match(883710);match["formalName"]="Resident Evil 2";match["storefrontAppId"]=883710;match["score"]=0.99;
            auto record=f.record(match);record["metadata"]={{"releaseDate","January 25, 2019"}};f.observation(match);f.save(record);
            auto plan=[&](){return buildSteamLibraryAddPlan(f.data,statePath,target,planPath);};
            auto assertMembership=[&](const json& value){check(value["items"].size()==1&&value["items"][0]["status"]=="already-in-steam"&&value["summary"]["alreadyInSteam"]==1&&value["summary"]["readyToAdd"]==0,"Identity enrichment changed existing shortcut membership");const auto& item=value["items"][0];check(item["storedAppId"]==-1073741823&&item["shortAppId"]=="3221225473"&&readBinaryFile(vdf)==originalVdf,"Identity enrichment changed existing shortcut ID/name/bytes");};
            const auto cachedPlan=plan();writeJsonAtomic(p/L"existing-identity-cache-observed.json",cachedPlan);assertMembership(cachedPlan);
            check(cachedPlan["items"][0].value("steamStoreAppId",0)==883710&&cachedPlan["items"][0].value("formalName",std::string{})=="Resident Evil 2"&&cachedPlan["items"][0].value("steamVerificationStatus",std::string{})=="steam-verified","Already-in-Steam plan dropped verified cache identity");
            check(cachedPlan["items"][0]["steamShortcutName"]=="Homeworld: Deserts of Kharak"&&cachedPlan["items"][0]["releaseYear"]==2019,"Existing shortcut provenance or verified release metadata lost");
            const auto direct=f.data/L"artwork"/toWide(gameDataId(directory))/L"manifest.json";auto directMatch=match;directMatch["formalName"]="Verified manifest title";writeJsonAtomic(direct,{{"exe",f.exe},{"match",directMatch},{"artwork",json::array()}});
            const auto directPlan=plan();assertMembership(directPlan);check(directPlan["items"][0]["formalName"]=="Verified manifest title"&&directPlan["items"][0]["identitySource"]=="portable-artwork-manifest","Valid current manifest did not retain precedence over cache");
            const auto overrides=f.data/L"config"/L"manual-overrides.json";writeJsonAtomic(overrides,{{"items",{{canonicalPathKey(f.executable),{{"idCleared",true},{"name","User custom label"}}}}}});
            const auto cleared=plan();assertMembership(cleared);check(!cleared["items"][0].contains("steamStoreAppId")&&cleared["items"][0]["formalName"]=="Homeworld: Deserts of Kharak","Existing shortcut repopulated explicitly cleared Steam ID");
            writeJsonAtomic(overrides,{{"items",json::object()}});fs::remove(direct);
            for(const auto* kind:{"candidate","future","different-exe","missing"}){
                auto invalid=record;const std::string k=kind;if(k=="candidate"){invalid["match"]["identityStatus"]="candidate";invalid["match"]["steamVerificationStatus"]="candidate";}if(k=="future")invalid["schemaVersion"]=99;if(k=="different-exe")invalid["executable"]=toUtf8((p/L"Different.exe").wstring());
                if(k=="missing")fs::remove(custom_steam_library::ioPath(f.cache));else f.save(invalid);const auto beforeCache=custom_steam_library::identity_cache::readSnapshot(f.cache);
                const auto fallback=plan();assertMembership(fallback);check(!fallback["items"][0].contains("steamStoreAppId")&&fallback["items"][0]["formalName"]=="Homeworld: Deserts of Kharak","Unverified/mismatched cache minted existing game's store ID");
                const auto afterCache=custom_steam_library::identity_cache::readSnapshot(f.cache);
                writeJsonAtomic(p/toWide("identity-cache-preservation-"+k+".json"),{{"case",k},{"pathChars",f.cache.wstring().size()},{"plainExists",fs::exists(f.cache)},{"beforeExists",beforeCache.exists},{"afterExists",afterCache.exists},{"beforeBytes",beforeCache.bytes.size()},{"afterBytes",afterCache.bytes.size()},{"sameSnapshot",custom_steam_library::identity_cache::sameSnapshot(beforeCache,afterCache)}});
                check(custom_steam_library::identity_cache::sameSnapshot(beforeCache,afterCache),"Read-only enrichment rewrote a protected identity cache");
            }
            f.save(record);const auto recovered=plan();assertMembership(recovered);check(recovered["items"][0].value("steamStoreAppId",0)==883710,"Valid later identity could not recover an existing shortcut");
            writeJsonAtomic(p/L"existing-identity-recovered.json",recovered);
            // An explicit new ID supersedes conflicting manifests and caches.
            const auto emptyVdf=steam/L"userdata"/L"26347"/L"config"/L"shortcuts.vdf";writeAtomic(emptyVdf,BinaryVdfDocument::emptySteamShortcuts().serialize());
            const SteamShortcutTarget emptyTarget{emptyVdf,"26347"};json manualObserved=json::array();bool manualSafe=true;
            for(const auto* variant:{"both-conflict","cache-current","manifest-current","numeric-string","explicit-content","name-only"}){
                const std::string kind=variant;auto oldMatch=match;oldMatch["appId"]=883710;oldMatch["storefrontAppId"]=883710;
                auto newMatch=match;newMatch["appId"]=620;newMatch["storefrontAppId"]=620;newMatch["formalName"]="New selected identity";
                auto manifestMatch=(kind=="manifest-current"||kind=="numeric-string"||kind=="explicit-content")?newMatch:oldMatch;
                auto cacheMatch=kind=="cache-current"?newMatch:oldMatch;
                json manual={{"steamId",620},{"name","Custom user label"}};
                if(kind=="numeric-string")manual["steamId"]="620";
                if(kind=="explicit-content"){manual["steamId"]=700;manifestMatch["matchedContentAppId"]=700;manifestMatch["baseGameAppId"]=620;manifestMatch["contentRelation"]="explicit-dlc";manifestMatch["identityKind"]="steam-base-game-with-explicit-content";}
                if(kind=="name-only")manual.erase("steamId");
                const int expected=kind=="both-conflict"?0:kind=="name-only"?883710:620;
                writeJsonAtomic(overrides,{{"items",{{canonicalPathKey(f.executable),manual}}}});
                writeJsonAtomic(direct,{{"exe",f.exe},{"match",manifestMatch},{"metadata",{{"releaseDate","January 1, 1999"}}},{"artwork",json::array()}});
                f.observation(cacheMatch);f.save(f.record(cacheMatch));const auto overrideBefore=readBinaryFile(overrides),manifestBefore=readBinaryFile(direct);
                for(bool owned:{true,false}){
                    const auto value=buildSteamLibraryAddPlan(f.data,statePath,owned?target:emptyTarget,planPath);const auto& item=value["items"][0];
                    const bool correct=positiveJsonInt(item,"steamStoreAppId")==expected;
                    const bool preserved=readBinaryFile(vdf)==originalVdf&&readBinaryFile(overrides)==overrideBefore&&readBinaryFile(direct)==manifestBefore;
                    const bool membership=owned?(item["status"]=="already-in-steam"&&value["summary"]["readyToAdd"]==0):item["status"]!="already-in-steam";
                    manualObserved.push_back({{"variant",kind},{"owned",owned},{"expectedAppId",expected},{"item",item},{"identityCorrect",correct},{"inputsPreserved",preserved},{"membershipPreserved",membership}});manualSafe=manualSafe&&correct&&preserved&&membership;
                }
            }
            writeJsonAtomic(p/L"manual-selection-worker-observed.json",manualObserved);
            check(manualObserved.size()==12&&manualSafe,"Plan reused identity conflicting with selected Steam ID");

            json igdbObserved=json::array();bool igdbSafe=true;
            for(const auto* variant:{"conflicting-steam","matching-igdb-manifest","matching-steam-igdb","mismatching-igdb"}){
                const std::string kind=variant;auto old=match;old["appId"]=883710;old["storefrontAppId"]=883710;old.erase("igdbId");
                auto chosen=old;chosen["igdbId"]=kind=="mismatching-igdb"?73:72;chosen["formalName"]="Selected metadata title";
                if(kind!="matching-steam-igdb"){chosen["appId"]=0;chosen["storefrontAppId"]=nullptr;chosen["primaryProvider"]="playnite-igdb";chosen["identityStatus"]="metadata-verified";chosen["metadataVerificationStatus"]="metadata-verified";chosen["steamVerificationStatus"]="not-available";}
                const auto observed=kind=="conflicting-steam"?old:chosen;json manual={{"igdbId",72},{"name","Selected metadata title"}};
                writeJsonAtomic(overrides,{{"items",{{canonicalPathKey(f.executable),manual}}}});
                writeJsonAtomic(direct,{{"exe",f.exe},{"match",observed},{"artwork",json::array()}});f.observation(old);f.save(f.record(old));
                const auto sourceBefore=readBinaryFile(direct),manualBefore=readBinaryFile(overrides),cacheBefore=readBinaryFile(f.cache);
                for(bool owned:{true,false}){
                    const auto value=buildSteamLibraryAddPlan(f.data,statePath,owned?target:emptyTarget,planPath);const auto& item=value["items"][0];
                    const int expected=kind=="matching-steam-igdb"?883710:0;
                    const bool identity=positiveJsonInt(item,"steamStoreAppId")==expected;
                    const bool metadata=owned||kind!="matching-igdb-manifest"||(positiveJsonInt(item,"igdbId")==72&&jsonStringOr(item,"formalName")=="Selected metadata title");
                    const bool preserved=readBinaryFile(vdf)==originalVdf&&readBinaryFile(direct)==sourceBefore&&readBinaryFile(overrides)==manualBefore&&readBinaryFile(f.cache)==cacheBefore;
                    const bool membership=owned?item["status"]=="already-in-steam":item["status"]!="already-in-steam";
                    igdbObserved.push_back({{"variant",kind},{"owned",owned},{"item",item},{"identityCorrect",identity},{"metadataCorrect",metadata},{"inputsPreserved",preserved},{"membershipPreserved",membership},{"passed",identity&&metadata&&preserved&&membership}});igdbSafe=igdbSafe&&identity&&metadata&&preserved&&membership;
                }
            }
            json bindings=json::array();auto bind=[&](const char* name,json m,json selected,bool expected){const bool actual=custom_steam_library::steamIdentityMatchesManualSelection(m,selected);bindings.push_back({{"name",name},{"expected",expected},{"actual",actual},{"passed",actual==expected}});igdbSafe=igdbSafe&&(actual==expected);};
            bind("numeric-string",{{"igdbId",72}},{{"igdbId","72"}},true);
            bind("negative",{{"igdbId",72}},{{"igdbId",-1}},false);
            bind("boolean",{{"igdbId",72}},{{"igdbId",true}},false);
            bind("signed-string",{{"igdbId",72}},{{"igdbId","+72"}},false);
            bind("overflow-string",{{"igdbId",72}},{{"igdbId","18446744073709551688"}},false);
            bind("int64-max",{{"igdbId",uint64_t{9223372036854775807ULL}}},{{"igdbId","9223372036854775807"}},true);
            bind("bad-match-type",{{"igdbId",true}},{{"igdbId",72}},false);
            bind("steam-choice-precedence",{{"appId",620},{"igdbId",73}},{{"steamId",620},{"igdbId",72}},true);
            writeJsonAtomic(p/L"igdb-selection-worker-observed.json",{{"rows",igdbObserved},{"bindings",bindings}});
            check(igdbSafe,"Worker plan reused identity conflicting with selected IGDB ID");
            // Exercise the actual CLI writer, then its persisted reader after
            // a failed offline recognition. These are owned, never-run EXEs.
            struct ClearCliPorts { bool previous=discoveryProducerPorts; ClearCliPorts(){discoveryProducerPorts=true;} ~ClearCliPorts(){discoveryProducerPorts=previous;} } clearCliPorts;
            DiscoveryProducerFixture clear(p/L"clear-cli");
            const auto overridesPath=clear.data/L"config"/L"manual-overrides.json";
            const auto clearKey=canonicalPathKey(clear.exe);
            textFile(clear.exe.parent_path()/L"steam_appid.txt","4285170");
            json observations=json::array();bool durable=true;
            for(const auto* variant:{"absent","empty","ids-no-name","named","artwork-no-name"}){
                const std::string kind=variant;
                json item=json::object();
                if(kind=="ids-no-name"||kind=="named")item={{"steamId",4285170},{"storefrontSteamId",4285170},{"igdbId",72},{"formalName","Wrong historical identity"},{"identityStatus","verified"},{"retryable",true},{"lastError","old failure"},{"attempts",3}};
                if(kind=="named")item["name"]="User custom title";
                if(kind=="artwork-no-name")item={{"cover",{{"file","owned-cover.png"},{"provider","user"}}},{"wallpaper",{{"file","owned-video.mp4"},{"provider","user"}}},{"userExtension",{{"keep",true}}}};
                json items={{"unrelated.exe",{{"name","Other game"},{"steamId",620},{"userExtension","keep"}}}};
                if(kind!="absent")items[clearKey]=item;
                writeJsonAtomic(overridesPath,{{"schemaVersion",1},{"items",items},{"userRootExtension","keep"}});
                auto expected=item;
                for(const auto* field:{"steamId","storefrontSteamId","igdbId","formalName","identityStatus","retryable","lastError","attempts"})expected.erase(field);
                expected["idCleared"]=true;
                for(int step=0;step<3;++step){
                    const auto output=p/L"clear-cli"/toWide(kind+"-"+std::to_string(step));
                    std::vector<std::wstring> args{clear.exe.wstring(),L"--steam-identity-only",L"--fault-provider",L"steam",output.wstring()};
                    if(step<2)args.push_back(L"--clear-id");
                    const auto code=clear.call(args);const auto stored=loadManualOverrides(overridesPath);
                    auto actual=stored["items"].value(clearKey,json::object());actual.erase("updatedAt");
                    const auto failure=loadJsonDocument(output/L"rounds-failed.json");
                    const bool saved=actual==expected;
                    const bool suppressed=failure.contains("localSteamIdentity")&&failure["localSteamIdentity"]["selectedAppId"].is_null()&&clear.output.find("LOCAL_STEAM_APPID_CANDIDATE:")==std::string::npos;
                    const bool preserved=stored["items"]["unrelated.exe"]==items["unrelated.exe"]&&stored["userRootExtension"]=="keep";
                    const bool failedOffline=code!=0&&failure.contains("steam")&&!failure["steam"].empty()&&failure["steam"][0]["status"]=="injected-failure";
                    observations.push_back({{"variant",kind},{"step",step},{"operation",step<2?"clear-id":"subsequent-offline-read"},{"exitCode",code},{"actual",actual},{"expected",expected},{"durableClear",saved},{"localCandidateSuppressed",suppressed},{"unrelatedPreserved",preserved},{"offlineFailureObserved",failedOffline}});
                    durable=durable&&saved&&suppressed&&preserved&&failedOffline;
                }
            }
            writeJsonAtomic(p/L"clear-id-cli-observations.json",observations);
            check(observations.size()==15&&clear.callCount==15,"Actual clear-id CLI selection did not execute the exact fifteen operations");
            check(durable,"Worker clear-id lost durable opt-out or unrelated saved fields");
            {
                DiscoveryProducerFixture stored(p/L"persisted-choice-cli");
                const auto config=stored.data/L"config"/L"manual-overrides.json";const auto key=canonicalPathKey(stored.exe);
                json savedRows=json::array();bool savedSafe=true;
                for(const auto* variant:{"saved-igdb","saved-igdb-reopen","saved-igdb-string","saved-steam-priority","saved-steam-string","explicit-igdb","explicit-steam","ignore-saved","cleared-saved","igdb-int64-max","igdb-overflow-string","name-only"}){
                    const std::string kind=variant;json choice={{"igdbId",72},{"name","Saved metadata choice"},{"userNote","keep"}};
                    std::string expectedSteam,expectedIgdb="72",source="manual-id-persisted",injected="Playnite IGDB provider failure was injected";
                    if(kind=="saved-igdb-string")choice["igdbId"]="00072";
                    if(kind=="saved-steam-priority"||kind=="saved-steam-string"){choice["steamId"]=kind=="saved-steam-string"?json("00620"):json(620);expectedSteam="620";expectedIgdb.clear();injected="Steam provider failure was injected";}
                    if(kind=="explicit-igdb"){expectedIgdb="73";source="manual-id-one-shot";}
                    if(kind=="explicit-steam"){expectedSteam="620";expectedIgdb.clear();source="manual-id-one-shot";injected="Steam provider failure was injected";}
                    if(kind=="ignore-saved"||kind=="cleared-saved"||kind=="name-only"||kind=="igdb-overflow-string"){expectedIgdb.clear();source.clear();injected.clear();}
                    if(kind=="cleared-saved")choice["idCleared"]=true;
                    if(kind=="name-only")choice.erase("igdbId");
                    if(kind=="igdb-int64-max"){choice["igdbId"]=uint64_t{9223372036854775807ULL};expectedIgdb="9223372036854775807";}
                    if(kind=="igdb-overflow-string")choice["igdbId"]="18446744073709551688";
                    if(kind!="saved-igdb-reopen")writeJsonAtomic(config,{{"schemaVersion",1},{"items",{{key,choice},{"unrelated.exe",{{"steamId",620},{"note","preserve"}}}}},{"rootNote","preserve"}});
                    const auto configBefore=readBinaryFile(config),exeBefore=readBinaryFile(stored.exe),steamBefore=readBinaryFile(stored.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf");
                    const auto output=stored.root/toWide("output-"+kind);
                    std::vector<std::wstring> args{stored.exe.wstring(),L"--metadata-only",L"--fault-provider",L"steam",L"--fault-provider",L"playnite-igdb"};
                    if(kind=="explicit-igdb")args.insert(args.end(),{L"--igdb-id",L"73"});
                    if(kind=="explicit-steam")args.insert(args.end(),{L"--steam-id",L"620"});
                    if(kind=="ignore-saved")args.push_back(L"--ignore-saved-id");
                    args.push_back(output.wstring());const auto code=stored.call(args);
                    const auto expectedLine=[&](const char* prefix,const std::string& id){return std::string(prefix)+id+" ("+source+")";};
                    const bool steamCorrect=expectedSteam.empty()?stored.output.find("MANUAL_STEAM_ID:")==std::string::npos:stored.output.find(expectedLine("MANUAL_STEAM_ID: ",expectedSteam))!=std::string::npos;
                    const bool igdbCorrect=expectedIgdb.empty()?stored.output.find("MANUAL_IGDB_ID:")==std::string::npos:stored.output.find(expectedLine("MANUAL_IGDB_ID: ",expectedIgdb))!=std::string::npos;
                    const bool route=injected.empty()||stored.error.find(injected)!=std::string::npos||stored.output.find(injected)!=std::string::npos;
                    const bool preserved=readBinaryFile(config)==configBefore&&readBinaryFile(stored.exe)==exeBefore&&readBinaryFile(stored.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf")==steamBefore;
                    const bool isolated=identityCallerNetwork==0&&identityCallerProcesses==0&&discoveryProducerPriorityWrites==0&&discoveryProducerDriveDiscovery==0;
                    savedRows.push_back({{"variant",kind},{"expectedSteam",expectedSteam},{"expectedIgdb",expectedIgdb},{"source",source},{"exitCode",code},{"stdout",stored.output},{"stderr",stored.error},{"steamCorrect",steamCorrect},{"igdbCorrect",igdbCorrect},{"directProviderRoute",route},{"savedInputsPreserved",preserved},{"isolated",isolated},{"passed",steamCorrect&&igdbCorrect&&route&&preserved&&isolated}});
                    savedSafe=savedSafe&&steamCorrect&&igdbCorrect&&route&&preserved&&isolated;
                }
                writeJsonAtomic(p/L"persisted-choice-observed.json",{{"rows",savedRows},{"calls",stored.callCount}});
                check(savedRows.size()==12&&stored.callCount==12,"Saved identity selection did not execute exactly twelve original CLI operations");
                check(savedSafe,"Ordinary Worker recognition ignored saved explicit identity choice");
            }

        });
        run("identitycaller-legacy-complete-without-pointer",[](const fs::path& p){IdentityCallerFixture f(p);auto value=f.record();value.erase("manifestPath");value.erase("schemaVersion");f.save(value);check(f.read().has_value(),"Legacy complete verified cache lost");});
        run("identitycaller-future-cache-no-manifest-follow",[](const fs::path& p){IdentityCallerFixture f(p);auto value=f.record();value["schemaVersion"]=99;value["match"]=json::object();f.save(value);const auto raw=fileText(f.cache);check(!f.read()&&!f.observed(f.manifest),"Future cache followed authority pointer");check(fileText(f.cache)==raw,"Caller changed future original");});
        run("identitycaller-decoded-duplicate-cache-rejected",[](const fs::path& p){IdentityCallerFixture f(p);const auto record=f.record();auto raw=record.dump();raw.pop_back();raw+=",\"\\u006datc\\u0068\":"+IdentityCallerFixture::match().dump()+"}";textFile(f.cache,raw);check(!f.read()&&!f.observed(f.manifest),"Decoded duplicate authority promoted");check(fileText(f.cache)==raw,"Duplicate original changed");});
        run("identitycaller-unknown-cache-never-follows-pointer",[](const fs::path& p){IdentityCallerFixture f(p);auto value=f.record();value["match"]=json::object();f.save(value);check(!f.read()&&!f.observed(f.manifest),"Unknown cache borrowed manifest identity");});
        run("identitycaller-missing-cache-no-default-id",[](const fs::path& p){IdentityCallerFixture f(p);check(!f.read()&&!f.observed(f.manifest),"Missing cache invented identity");});
        run("identitycaller-wrong-root-and-field-types",[](const fs::path& p){IdentityCallerFixture f(p);for(const char* field:{"metadata","updatedAt","steamAppId","schemaVersion","manifestPath"}){auto value=f.record();value[field]=json::array();f.save(value);check(!f.read(),"Malformed root field reached identity");}});
        run("identitycaller-empty-truncated-oversize",[](const fs::path& p){IdentityCallerFixture f(p);for(const auto& raw:std::vector<std::string>{"","{","null",std::string(16*1024*1024+1,'x')}){textFile(f.cache,raw);check(!f.read()&&fileText(f.cache)==raw,"Bad/oversize identity not unknown or mutated");}});
        run("identitycaller-executable-binding-required",[](const fs::path& p){IdentityCallerFixture f(p);auto value=f.record();value["executable"]=f.exe+".other";f.save(value);check(!f.read(),"Other executable identity reused");});
        run("identitycaller-arbitrary-cache-filename-not-authority",[](const fs::path& p){IdentityCallerFixture f(p);const auto other=f.cache.parent_path()/L"arbitrary.json";writeJsonAtomic(other,f.record());check(!f.read(),"Arbitrary directory member became identity");f.fresh();check(f.read().has_value()&&fs::exists(other),"Canonical explicit observation did not recover");});
        run("identitycaller-manifest-path-ownership",[](const fs::path& p){IdentityCallerFixture f(p);const auto outside=p/L"outside"/L"manifest.json";writeJsonAtomic(outside,{{"exe",f.exe},{"match",IdentityCallerFixture::match()}});for(const auto& path:std::vector<std::string>{toUtf8(outside.wstring()),"relative/manifest.json",toUtf8((f.data/L"config"/L"manifest.json").wstring()),toUtf8((f.data/L"cache"/L"jobs"/L".."/L".."/L"config"/L"manifest.json").wstring())}){auto value=f.record();value["manifestPath"]=path;f.save(value);check(!f.read()&&!f.observed(outside),"Unowned/ambiguous manifest pointer accepted or followed");}});
        run("identitycaller-manifest-target-binding",[](const fs::path& p){IdentityCallerFixture f(p);f.save(f.record());writeJsonAtomic(f.manifest,{{"exe",f.exe+".other"},{"match",IdentityCallerFixture::match()}});check(!f.read(),"Owned manifest of another executable accepted");});
        run("identitycaller-manifest-match-binding",[](const fs::path& p){IdentityCallerFixture f(p);f.save(f.record());f.observation(IdentityCallerFixture::match(777));check(!f.read(),"Changed manifest identity borrowed");});
        run("identitycaller-future-manifest-not-consumed",[](const fs::path& p){IdentityCallerFixture f(p);f.save(f.record());auto value=loadJsonDocument(f.manifest);value["schemaVersion"]=99;writeJsonAtomic(f.manifest,value);const auto raw=fileText(f.manifest);check(!f.read()&&fileText(f.manifest)==raw,"Future manifest accepted or changed");});
        run("identitycaller-decoded-duplicate-manifest",[](const fs::path& p){IdentityCallerFixture f(p);f.save(f.record());auto raw=loadJsonDocument(f.manifest).dump();raw.pop_back();raw+=",\"\\u0065xe\":"+json(f.exe).dump()+"}";textFile(f.manifest,raw);check(!f.read(),"Decoded duplicate manifest binding accepted");});
        run("identitycaller-complete-cache-survives-cleaned-job",[](const fs::path& p){
            json rows=json::array();bool passed=true;
            const auto record=[&](const char* variant,bool expected,bool actual,bool unchanged){const bool ok=expected==actual&&unchanged;rows.push_back({{"variant",variant},{"expected",expected},{"actual",actual},{"unchanged",unchanged},{"passed",ok}});passed=passed&&ok;};
            for(const auto* variant:{"legacy","schema1","schema2","missing","future","negative","float","string","boolean","null","foreign-schema","cache-tagged-worker","cache-version2","wrong-target","wrong-match","wrong-metadata","unverified-match","unknown-cache"}){
                IdentityCallerFixture f(p/toWide(variant));auto cache=f.record();auto manifest=loadJsonDocument(f.manifest);const std::string mode=variant;bool expected=mode=="legacy"||mode=="schema1"||mode=="schema2"||mode=="missing";
                if(mode!="legacy"&&mode!="missing")manifest["schemaVersion"]=2;
                if(mode=="schema1")manifest["schemaVersion"]=1;
                if(mode=="future")manifest["schemaVersion"]=3;
                if(mode=="negative")manifest["schemaVersion"]=-1;
                if(mode=="float")manifest["schemaVersion"]=2.0;
                if(mode=="string")manifest["schemaVersion"]="2";
                if(mode=="boolean")manifest["schemaVersion"]=true;
                if(mode=="null")manifest["schemaVersion"]=nullptr;
                if(mode=="foreign-schema")manifest["schema"]="external.schema";
                if(mode=="cache-tagged-worker")manifest["schema"]="ymcc.library-identity-cache.v1";
                if(mode=="cache-version2")cache["schemaVersion"]=2;
                if(mode=="wrong-target")manifest["exe"]=f.exe+".other";
                if(mode=="wrong-match")manifest["match"]=IdentityCallerFixture::match(777);
                if(mode=="wrong-metadata")manifest["metadata"]=json::array();
                if(mode=="unverified-match"){manifest["match"]["steamVerificationStatus"]="not-available";cache["match"]=manifest["match"];}
                if(mode=="unknown-cache")cache["match"]=json::object();
                f.save(cache);writeJsonAtomic(f.manifest,manifest);if(mode=="missing")fs::remove(f.manifest);
                const auto beforeCache=fileText(f.cache);const auto beforeManifest=fs::exists(f.manifest)?fileText(f.manifest):std::string{};const bool actual=f.read().has_value();
                record(variant,expected,actual,fileText(f.cache)==beforeCache&&(!fs::exists(f.manifest)||fileText(f.manifest)==beforeManifest));
            }
            /* PRIVATE_REAL_CAPTURE */
            writeJsonAtomic(p/L"manifest-schema-observed.json",{{"variants",rows},{"networkCalls",identityCallerNetwork},{"processCalls",identityCallerProcesses}});
            check(passed,"Worker manifest schema2 is not reusable through the real cached identity caller");
        });
        run("identitycaller-read-denied-then-valid-observation",[](const fs::path& p){IdentityCallerFixture f(p);f.save(f.record());const auto raw=fileText(f.cache);withConfigReadDenied(f.cache,[&]{check(!f.read(),"Denied cache reached identity");});check(fileText(f.cache)==raw,"Denied cache original changed");f.fresh();check(f.read().has_value(),"Valid observation after denial did not recover");});
        run("identitycaller-corrupt-to-real-store-commit-to-caller",[](const fs::path& p){IdentityCallerFixture f(p);const std::string raw="{owned bad";textFile(f.cache,raw);check(!f.read(),"Corrupt cache reached caller");f.fresh();check(f.read().has_value(),"Header commit not consumed by actual caller");bool preserved=false;for(const auto& entry:fs::directory_iterator(f.cache.parent_path()))if(entry.is_regular_file()&&entry.path()!=f.cache&&fileText(entry.path())==raw)preserved=true;check(preserved,"Legal observation lost bad original");});
        run("identitycaller-future-explicit-quarantine-then-observation",[](const fs::path& p){IdentityCallerFixture f(p);auto value=f.record();value["schemaVersion"]=99;f.save(value);const auto raw=fileText(f.cache);check(!f.read(),"Future root consumed");const auto retained=p/L"future-original.json";fs::rename(custom_steam_library::ioPath(f.cache),custom_steam_library::ioPath(retained));f.fresh();check(f.read().has_value()&&fileText(retained)==raw,"Explicit valid repair did not preserve original");});
        run("identitycaller-path-alias-refused",[](const fs::path& p){IdentityCallerFixture f(p);const auto actual=p/L"linked-owned.json";writeJsonAtomic(actual,f.record());fs::create_directories(f.cache.parent_path());check(CreateHardLinkW(custom_steam_library::ioPath(f.cache).c_str(),custom_steam_library::ioPath(actual).c_str(),nullptr)!=FALSE,"Owned alias setup");check(!f.read(),"Linked cache identity accepted");});
        run("identitycaller-cache-changed-during-read",[](const fs::path& p){
            namespace cache=custom_steam_library::identity_cache;
            IdentityCallerFixture f(p);f.save(f.record());
            const auto initialCache=cache::readSnapshot(f.cache),initialManifest=cache::readSnapshot(f.manifest);
            const auto baseline=f.read();
            check(baseline&&(*baseline)["match"]["appId"]==555,"Initial cache/manifest were not admitted by caller");
            auto replacement=f.record(IdentityCallerFixture::match(777));
            bool started=false,completed=false;unsigned cacheReads=0,manifestReads=0;
            std::string replacementError;json events=json::array();
            identityCallerReadHook=[&](const std::wstring& file){
                const bool isCache=file==identityCallerPath(f.cache.wstring());
                const bool isManifest=file==identityCallerPath(f.manifest.wstring());
                if(isCache)++cacheReads;if(isManifest)++manifestReads;
                events.push_back({{"event","read"},{"path",toUtf8(file)},{"cacheReads",cacheReads},{"manifestReads",manifestReads}});
                // The first cache handle has closed and its record has passed the
                // real caller gates before provenance is read. Change only the
                // cache here so neither header's first read nor manifest rebinding
                // can reject the old record ahead of the final cache snapshot gate.
                if(!started&&isManifest){
                    started=true;events.push_back({{"event","cache-replacement-started"}});
                    try{writeJsonAtomic(f.cache,replacement);completed=true;events.push_back({{"event","cache-replacement-completed"}});}
                    catch(const std::exception& e){replacementError=e.what();events.push_back({{"event","cache-replacement-failed"},{"error",replacementError}});throw;}
                }
            };
            const auto result=f.read();identityCallerReadHook={};
            const auto finalCache=cache::readSnapshot(f.cache),finalManifest=cache::readSnapshot(f.manifest);
            const bool cacheChanged=!cache::sameSnapshot(initialCache,finalCache);
            const bool manifestUnchanged=cache::sameSnapshot(initialManifest,finalManifest);
            writeJsonAtomic(p/L"snapshot-gate-timing.json",{{"events",events},{"initialCallerAppId",(*baseline)["match"]["appId"]},
                {"replacementStarted",started},{"replacementCompleted",completed},{"replacementError",replacementError},
                {"cacheReads",cacheReads},{"manifestReads",manifestReads},{"cacheSnapshotChanged",cacheChanged},{"manifestSnapshotUnchanged",manifestUnchanged},
                {"finalCacheAppId",finalCache.value["match"]["appId"]},{"callerReturnedIdentity",result.has_value()},
                {"callerReturnedAppId",result?(*result)["match"]["appId"]:json(nullptr)}});
            check(started&&completed&&replacementError.empty(),"Concurrent cache replacement did not complete");
            check(cacheReads>=1&&manifestReads>=1&&cacheChanged&&finalCache.value==replacement&&manifestUnchanged,
                "Concurrent cache fault did not preserve valid initial provenance and change the owned cache");
            check(!result,"Changed canonical snapshot reached identity");
        });
        run("identitycaller-manifest-changed-during-read",[](const fs::path& p){IdentityCallerFixture f(p);f.save(f.record());bool changed=false;identityCallerReadHook=[&](const std::wstring& file){if(!changed&&file==identityCallerPath(f.manifest.wstring())){changed=true;f.observation(IdentityCallerFixture::match(777));}};check(!f.read()&&changed,"Changed manifest snapshot reached identity");identityCallerReadHook={};});
        run("identitycaller-wrong-key-newer-file-cannot-win",[](const fs::path& p){IdentityCallerFixture f(p);f.fresh();auto newer=f.record(IdentityCallerFixture::match(777));newer["updatedAt"]=unixTimeMs()+1000;writeJsonAtomic(f.cache.parent_path()/L"other-file.json",newer);const auto result=f.read();check(result&&(*result)["match"]["appId"]==555,"Newer arbitrary filename overrode exact binding");});
        run("identitycaller-bad-root-storm-and-explicit-recovery",[](const fs::path& p){IdentityCallerFixture f(p);for(int n=0;n<12;++n){textFile(f.cache,n%2?"{":"null");for(int k=0;k<6;++k)check(!f.read(),"Bad root storm minted identity");f.fresh();check(f.read().has_value(),"Explicit new observation did not recover caller");}});

        run("identitycaller-future-complete-cache-rejected",[](const fs::path& p){IdentityCallerFixture f(p);auto value=f.record();value["schemaVersion"]=99;f.save(value);const auto raw=fileText(f.cache);check(!f.read()&&fileText(f.cache)==raw,"Complete future cache consumed");});
        run("identitycaller-manifest-known-field-types",[](const fs::path& p){IdentityCallerFixture f(p);f.save(f.record());for(const char* key:{"exe","match","metadata"}){json observation={{"exe",f.exe},{"match",IdentityCallerFixture::match()},{"metadata",json::object()}};observation[key]=json::array();writeJsonAtomic(f.manifest,observation);check(!f.read(),"Wrong-type provenance field accepted");}});
        run("identitycaller-manifest-reparse-ancestor-refused",[](const fs::path& p){IdentityCallerFixture f(p);f.save(f.record());const auto target=p/L"owned-provenance-target";fs::create_directories(target);writeJsonAtomic(target/L"manifest.json",{{"exe",f.exe},{"match",IdentityCallerFixture::match()},{"metadata",json::object()}});fs::remove(f.manifest);fs::remove(f.manifest.parent_path());check(createFixtureJunction(f.manifest.parent_path(),target),"Owned provenance junction setup");check(!f.read(),"Reparse provenance target consumed");});
        check(!identities || (!reports.empty()&&identityCallerNetwork==0&&identityCallerProcesses==0),"Identity selection empty or real external port invoked");


        const std::string ruleBegin=custom_steam_library::ymcc_whitelist::beginMarker,ruleEnd=custom_steam_library::ymcc_whitelist::endMarker;
        auto ruleState=[&](const fs::path& p,const std::string& name){const auto exe=p/L"Games"/toWide(name+".exe");textFile(exe,"owned marker, never executed");return json{{"games",json::array({{{"status","ready"},{"primaryRole","main-game"},{"primaryExecutable",toUtf8(exe.wstring())},{"gameDirectory",toUtf8(exe.parent_path().wstring())}}})}};};
        run("ruletext-read-bom-chinese-comments",[&](const fs::path& p){const auto f=p/L"rules.txt";const auto chinese=toUtf8(L"\u4e2d\u6587\u6e38\u620f");textFile(f,std::string("\xEF\xBB\xBF",3)+"\t "+chinese+".EXE # note\r\nALPHA.EXE\r\nalpha\n# comment\n");check(readGameRuleFile(f)==std::vector<std::string>({chinese,"alpha"}),"Valid BOM/Chinese/comment normalization lost");});
        run("ruletext-read-empty-and-missing",[&](const fs::path& p){check(readGameRuleFile(p/L"missing.txt").empty(),"Truly missing file is not empty");textFile(p/L"empty.txt","");check(readGameRuleFile(p/L"empty.txt").empty(),"Legitimate empty file rejected");});
        const std::vector<std::pair<std::string,std::string>> ruleBadText={
            {"utf8-ff",std::string(1,char(0xff))+"alpha\n"},{"utf8-overlong",std::string("\xc0\xaf",2)},{"utf8-truncated",std::string("\xe4\xb8",2)},{"utf8-surrogate",std::string("\xed\xa0\x80",3)},{"utf8-range",std::string("\xf4\x90\x80\x80",4)},{"utf16",std::string("\xff\xfe\x61\x00",4)},{"nul",std::string("alpha\0beta",10)},{"control",std::string("alpha\x01",6)},{"vertical-tab","alpha\vbeta"}};
        for(const auto& [name,raw]:ruleBadText)run(("ruletext-read-reject-"+name).c_str(),[&,raw](const fs::path& p){const auto f=p/L"rules.txt";textFile(f,raw);mustThrow([&]{readGameRuleFile(f);});check(fileText(f)==raw,"Bad TXT original was changed");textFile(p/L"preserved-original.bin",raw);textFile(f,"alpha\n");check(readGameRuleFile(f)==std::vector<std::string>({"alpha"}),"Explicit valid TXT repair blocked retry");});
        run("ruletext-read-directory-is-unknown",[&](const fs::path& p){const auto f=p/L"rules.txt";fs::create_directory(f);mustThrow([&]{readGameRuleFile(f);});check(fs::is_directory(f),"Directory placeholder removed");});
        run("ruletext-read-hardlink-is-unknown",[&](const fs::path& p){const auto f=p/L"rules.txt",alias=p/L"alias.txt";textFile(f,"alpha\n");check(CreateHardLinkW(alias.c_str(),f.c_str(),nullptr)!=FALSE,"Owned hardlink setup failed");mustThrow([&]{readGameRuleFile(f);});check(fileText(f)=="alpha\n"&&fileText(alias)=="alpha\n","Hardlinked source changed");});
        run("ruletext-read-lock-release-retry",[&](const fs::path& p){const auto f=p/L"rules.txt";textFile(f,"alpha\n");HANDLE h=CreateFileW(f.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(h!=INVALID_HANDLE_VALUE,"Owned read-lock setup failed");bool refused=false;try{readGameRuleFile(f);}catch(const std::exception&){refused=true;}CloseHandle(h);check(refused&&fileText(f)=="alpha\n","Read lock treated as missing");check(readGameRuleFile(f)==std::vector<std::string>({"alpha"}),"Released lock could not retry");});
        run("ruletext-read-denied-is-unknown",[&](const fs::path& p){const auto f=p/L"rules.txt";textFile(f,"alpha\n");withConfigReadDenied(f,[&]{mustThrow([&]{readGameRuleFile(f);});});check(readGameRuleFile(f)==std::vector<std::string>({"alpha"}),"ACL release did not restore explicit read");});
        run("ruletext-read-limit-boundary",[&](const fs::path& p){const auto f=p/L"rules.txt";textFile(f,std::string(1u<<20,'#'));check(readGameRuleFile(f).empty(),"Exact1MiB valid comment rejected");const auto raw=std::string((1u<<20)+1,'#');textFile(f,raw);mustThrow([&]{readGameRuleFile(f);});check(fileText(f)==raw,"Over-limit original was changed");});
        run("ruletext-serialize-chinese-and-leading-feff",[&](const fs::path& p){const auto f=p/L"rules.txt";const std::vector<std::string> desired={std::string("\xEF\xBB\xBF",3)+"alpha",toUtf8(L"\u4e2d\u6587\u6e38\u620f")};const auto raw=gameRuleFileBytes(desired);writeAtomic(f,raw);check(readGameRuleFile(f)==desired,"Serializer lost leading literal U+FEFF or Chinese rule");});
        const std::vector<std::pair<std::string,std::string>> ruleBadEntries={{"empty",""},{"utf8",std::string(1,char(0xff))},{"nul",std::string("alpha\0beta",10)},{"control",std::string("alpha\x02",6)},{"newline","alpha\nbeta"},{"carriage","alpha\rbeta"},{"tab","alpha\tbeta"},{"comment","alpha#beta"}};
        for(const auto& [name,rule]:ruleBadEntries)run(("ruletext-serialize-reject-"+name).c_str(),[&,rule](const fs::path&){mustThrow([&]{gameRuleFileBytes({rule});});});
        run("ruletext-serialize-limit-boundary",[&](const fs::path&){check(gameRuleFileBytes({std::string((1u<<20)-1,'a')}).size()==(1u<<20),"Exact output limit rejected");mustThrow([&]{gameRuleFileBytes({std::string(1u<<20,'a')});});mustThrow([&]{gameRuleFileBytes({std::string(600000,'a'),std::string(600000,'b')});});});
        for(const auto* kind:{"same-visible","add-manual","manual-also-owned"})run((std::string("ruletext-legacy-publish-sync-remove-")+kind).c_str(),[&,kind](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt",black=p/L"black.txt";const std::string change=kind;textFile(white,change=="manual-also-owned"?"manual-user-rule\nalpha\n":"manual-user-rule\n");textFile(black,"");auto event=membershipEvent(p,"19627","Alpha");publishYmccGameWhitelist(event,data,true,white);check(event["ymccWhitelistPublication"]["status"]!="warning","Initial owned membership failed");const auto original=fileText(white);const auto state=change=="same-visible"?json{{"games",json::array()}}:ruleState(p,"Beta");const auto plan=planGameWhitelistSync(state,white,black);commitGameWhitelistSync(plan,data,white,black);const auto synced=fileText(white);check(synced.find(ruleBegin)!=std::string::npos&&synced.find(ruleEnd)!=std::string::npos,"Legacy sync flattened managed ownership");if(change=="same-visible")check(synced==original,"No-change legacy sync rewrote original block");const auto target=fs::path(toWide(event["shortcutsVdf"].get<std::string>()));writeAtomic(target,BinaryVdfDocument::emptySteamShortcuts().serialize());event.erase("ymccMembershipAuthority");event["shortcutsBeforeSha256"]=sha256(readBinaryFile(target));event["items"]=json::array();publishYmccGameWhitelist(event,data,true,white);check(event["ymccWhitelistPublication"]["status"]!="warning","Owned removal did not retry");const auto rules=readGameRuleFile(white);check(std::find(rules.begin(),rules.end(),"manual-user-rule")!=rules.end(),"Explicit manual rule lost");check((std::find(rules.begin(),rules.end(),"alpha")!=rules.end())==(change=="manual-also-owned"),"Managed deletion removed manual ownership or retained flattened Alpha");if(change!="same-visible")check(std::find(rules.begin(),rules.end(),"beta")!=rules.end(),"Legacy added manual rule was lost");});
        run("ruletext-legacy-markerless-current-text-is-manual",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt",black=p/L"black.txt";textFile(white,"manual-user-rule\n");textFile(black,"");auto event=membershipEvent(p,"19627","Alpha");publishYmccGameWhitelist(event,data,true,white);textFile(white,"manual-user-rule\nalpha\n");auto plan=planGameWhitelistSync(ruleState(p,"Beta"),white,black);commitGameWhitelistSync(plan,data,white,black);const auto target=fs::path(toWide(event["shortcutsVdf"].get<std::string>()));writeAtomic(target,BinaryVdfDocument::emptySteamShortcuts().serialize());event.erase("ymccMembershipAuthority");event["shortcutsBeforeSha256"]=sha256(readBinaryFile(target));event["items"]=json::array();publishYmccGameWhitelist(event,data,true,white);check(fileText(white).find("alpha")!=std::string::npos,"Historical membership stole current naked manual rule");});
        const std::vector<std::pair<std::string,std::string>> ruleBadMarkers={{"missing-end",ruleBegin+"\nalpha\n"},{"orphan-end",ruleEnd+"\nalpha\n"},{"nested",ruleBegin+"\n"+ruleBegin+"\nalpha\n"+ruleEnd+"\n"},{"two-blocks",ruleBegin+"\nalpha\n"+ruleEnd+"\n"+ruleBegin+"\nbeta\n"+ruleEnd+"\n"},{"future",ruleBegin+" v2\nalpha\n"+ruleEnd+" v2\n"},{"lowercase","# begin ymcc custom-steam-library managed rules\nalpha\n# end ymcc custom-steam-library managed rules\n"},{"compact","#BEGIN YMCC custom-steam-library managed rules\nalpha\n#END YMCC custom-steam-library managed rules\n"},{"indented","  "+ruleBegin+"\nalpha\n"+ruleEnd+"\n"},{"trailing-end",ruleBegin+"\nalpha\n"+ruleEnd+" extra\n"}};
        for(const auto& [name,raw]:ruleBadMarkers)run(("ruletext-legacy-reject-marker-"+name).c_str(),[&,raw](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt",black=p/L"black.txt";textFile(white,raw);textFile(black,"beta\n");auto plan=planGameWhitelistSync(ruleState(p,"Beta"),white,black);mustThrow([&]{commitGameWhitelistSync(plan,data,white,black);});check(fileText(white)==raw&&fileText(black)=="beta\n","Unknown/bad marker changed either original file");textFile(p/L"preserved-marker-original.txt",raw);textFile(white,"manual-user-rule\n");plan=planGameWhitelistSync(ruleState(p,"Beta"),white,black);commitGameWhitelistSync(plan,data,white,black);check(readGameRuleFile(white)==std::vector<std::string>({"manual-user-rule","beta"}),"Trusted marker repair blocked explicit sync");});
        run("ruletext-legacy-bom-crlf-unchanged-keeps-bytes",[&](const fs::path& p){const auto white=p/L"rules.txt",black=p/L"black.txt";const auto original=std::string("\xEF\xBB\xBF",3)+"# manual note\r\n"+toUtf8(L"\u4e2d\u6587")+"\r\n"+ruleBegin+"\r\nalpha\r\n"+ruleEnd+"\r\n";textFile(white,original);textFile(black,"# retained comment\r\n");const auto plan=planGameWhitelistSync({{"games",json::array()}},white,black);commitGameWhitelistSync(plan,p/L"data",white,black);check(fileText(white)==original&&fileText(black)=="# retained comment\r\n","Unchanged legacy commit flattened BOM/comments/CRLF");});
        for(const auto* targetName:{"white","black"})run((std::string("ruletext-legacy-write-failure-retry-")+targetName).c_str(),[&,targetName](const fs::path& p){const auto white=p/L"rules.txt",black=p/L"black.txt",data=p/L"data";textFile(white,"manual-user-rule\n");textFile(black,"beta\n");const auto plan=planGameWhitelistSync(ruleState(p,"Beta"),white,black);bool fired=false;const auto target=std::string(targetName)=="white"?white:black;selftestMoveFault=[&](const auto&,const auto& to)->DWORD{if(!fired&&to==target.wstring()){fired=true;return ERROR_WRITE_FAULT;}return ERROR_SUCCESS;};mustThrow([&]{commitGameWhitelistSync(plan,data,white,black);});selftestMoveFault={};check(fired&&fileText(white)=="manual-user-rule\n"&&fileText(black)=="beta\n","Failed TXT publication ACKed or lost original");commitGameWhitelistSync(plan,data,white,black);check(fileText(white).find("beta")!=std::string::npos&&readGameRuleFile(black).empty(),"Released writer failure could not retry");});
        run("ruletext-legacy-concurrent-white-change-retained",[&](const fs::path& p){const auto white=p/L"rules.txt",black=p/L"black.txt",data=p/L"data";textFile(white,"manual-user-rule\n");textFile(black,"beta\n");const auto plan=planGameWhitelistSync(ruleState(p,"Beta"),white,black);bool fired=false;selftestMoveFault=[&](const auto&,const auto& to)->DWORD{if(!fired&&to==black.wstring()){fired=true;textFile(white,"newer-manual-rule\n");}return ERROR_SUCCESS;};mustThrow([&]{commitGameWhitelistSync(plan,data,white,black);});selftestMoveFault={};check(fired&&fileText(white)=="newer-manual-rule\n"&&fileText(black)=="beta\n","Commit/rollback overwrote concurrent manual source");const auto retry=planGameWhitelistSync(ruleState(p,"Beta"),white,black);commitGameWhitelistSync(retry,data,white,black);check(fileText(white).find("newer-manual-rule")!=std::string::npos,"Explicit retry lost newer source");});
        run("ruletext-legacy-canonical-readback-change-no-ack",[&](const fs::path& p){const auto white=p/L"rules.txt",black=p/L"black.txt",data=p/L"data";textFile(white,"manual-user-rule\n");textFile(black,"beta\n");const auto plan=planGameWhitelistSync(ruleState(p,"Beta"),white,black);bool fired=false;ruleTextAfterMove=[&](const std::wstring& from,const std::wstring& to){if(!fired&&identityCallerPath(to)==identityCallerPath(white.wstring())&&from.find(L".sal.")!=std::wstring::npos){fired=true;const std::string external="newer-external-rule\n";writeAtomic(white,std::vector<unsigned char>(external.begin(),external.end()));}};mustThrow([&]{commitGameWhitelistSync(plan,data,white,black);});ruleTextAfterMove={};check(fired&&fileText(white)=="newer-external-rule\n"&&fileText(black)=="beta\n","Changed canonical readback was ACKed or overwritten");const auto retry=planGameWhitelistSync(ruleState(p,"Beta"),white,black);commitGameWhitelistSync(retry,data,white,black);check(fileText(white).find("newer-external-rule")!=std::string::npos,"Readback conflict release did not recover");});
        // Same publisher, membership and real owned filesystem; no Steam execution.
        auto publisherCall=[&](json& event,const fs::path& data,const fs::path& white){
            const auto target=fs::path(toWide(event["shortcutsVdf"].get<std::string>()));const auto bytes=readBinaryFile(target);event["status"]="steam-completed";
            publishYmccGameWhitelist(event,data,true,white);
            check(event["status"]=="steam-completed"&&readBinaryFile(target)==bytes,"Auxiliary TXT result changed completed Steam result/VDF");
        };
        auto publisherRetry=[&](json& event,const fs::path& data,const fs::path& white){event.erase("ymccMembershipAuthority");publisherCall(event,data,white);check(event["ymccWhitelistPublication"]["status"]=="updated"||event["ymccWhitelistPublication"]["status"]=="unchanged","Explicit publisher retry did not recover");};
        auto publisherWarning=[&](const json& event){check(event["ymccWhitelistPublication"]["status"]=="warning"&&event["ymccWhitelistPublication"]["steamResultPreserved"]==true,"Unsafe publication returned success instead of warning");};
        for(const auto& [name,raw]:ruleBadText)run(("publishertext-reject-"+name).c_str(),[&,raw](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");textFile(white,raw);publisherCall(event,data,white);publisherWarning(event);check(fileText(white)==raw,"Corrupt original was overwritten");textFile(white,"manual-user-rule\n");publisherRetry(event,data,white);check(readGameRuleFile(white)==std::vector<std::string>({"manual-user-rule","alpha"}),"Repaired TXT did not become readable/publish");});
        for(const auto& [name,raw]:ruleBadMarkers)run(("publishertext-marker-"+name).c_str(),[&,raw](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");textFile(white,raw);publisherCall(event,data,white);publisherWarning(event);check(fileText(white)==raw,"Unknown/bad managed markers overwritten");textFile(white,"manual-user-rule\n");publisherRetry(event,data,white);});
        run("publishertext-overlimit-original-retry",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");const std::string raw((1u<<20)+1,'#');textFile(white,raw);publisherCall(event,data,white);publisherWarning(event);check(fileText(white)==raw,"Overlimit original changed");textFile(white,"manual\n");publisherRetry(event,data,white);});
        run("publishertext-output-overlimit-retry",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");const std::string raw(1u<<20,'#');textFile(white,raw);publisherCall(event,data,white);publisherWarning(event);check(fileText(white)==raw,"Oversized generated document was committed");textFile(white,"manual\n");publisherRetry(event,data,white);});
        run("publishertext-directory-retry",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");fs::create_directory(white);publisherCall(event,data,white);publisherWarning(event);check(fs::is_directory(white),"Nonfile original deleted");fs::remove(white);publisherRetry(event,data,white);});
        run("publishertext-hardlink-retry",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt",alias=p/L"rules.alias";auto event=membershipEvent(p,"19627","Alpha");textFile(white,"manual\n");check(CreateHardLinkW(alias.c_str(),white.c_str(),nullptr)!=FALSE,"Owned hardlink setup failed");publisherCall(event,data,white);publisherWarning(event);check(fileText(white)=="manual\n"&&fileText(alias)=="manual\n","Hardlink source changed");fs::remove(alias);publisherRetry(event,data,white);});
        run("publishertext-lock-release-retry",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");textFile(white,"manual\n");HANDLE h=CreateFileW(white.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(h!=INVALID_HANDLE_VALUE,"Owned lock setup failed");publisherCall(event,data,white);CloseHandle(h);publisherWarning(event);check(fileText(white)=="manual\n","Locked original changed");publisherRetry(event,data,white);});
        run("publishertext-read-denied-retry",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");textFile(white,"manual\n");withConfigReadDenied(white,[&]{publisherCall(event,data,white);publisherWarning(event);});check(fileText(white)=="manual\n","Unreadable original changed");publisherRetry(event,data,white);});
        run("publishertext-missing-and-empty",[&](const fs::path& p){for(bool exists:{false,true}){const auto root=p/(exists?L"empty":L"missing"),data=root/L"data",white=root/L"rules.txt";auto event=membershipEvent(root,"19627","Alpha");if(exists)textFile(white,"");publisherRetry(event,data,white);check(readGameRuleFile(white)==std::vector<std::string>({"alpha"}),"Valid empty/missing publisher lost rule");}});
        run("publishertext-bom-chinese-manual-raw",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");const auto raw=std::string("\xef\xbb\xbf",3)+"# manual comment\r\n\t"+toUtf8(L"\u4e2d\u6587\u6e38\u620f")+".EXE # keep note\r\nalpha\r\n";textFile(white,raw);publisherRetry(event,data,white);const auto published=fileText(white);check(published.starts_with(raw)&&published.find(ruleBegin)!=std::string::npos,"Publisher normalized original manual/BOM bytes");check(readGameRuleFile(white)==std::vector<std::string>({toUtf8(L"\u4e2d\u6587\u6e38\u620f"),"alpha"}),"Published BOM/Chinese unreadable by current legacy reader");const auto target=fs::path(toWide(event["shortcutsVdf"].get<std::string>()));writeAtomic(target,BinaryVdfDocument::emptySteamShortcuts().serialize());event["shortcutsBeforeSha256"]=sha256(readBinaryFile(target));event["items"]=json::array();publisherRetry(event,data,white);check(fileText(white)==raw,"Managed removal deleted or reformatted explicit manual alpha");});
        run("publishertext-bom-first-managed-block-replace-remove",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");const auto bom=std::string("\xef\xbb\xbf",3),tail=std::string("# manual tail\r\n")+toUtf8(L"\u4e2d\u6587")+"\r\nalpha\r\n";textFile(white,bom+ruleBegin+"\r\nstale-owned\r\n"+ruleEnd+"\r\n"+tail);publisherRetry(event,data,white);check(fileText(white).starts_with(bom+ruleBegin)&&fileText(white).ends_with(tail)&&fileText(white).find("stale-owned")==std::string::npos,"Leading BOM managed range was not replaced exactly");const auto target=fs::path(toWide(event["shortcutsVdf"].get<std::string>()));writeAtomic(target,BinaryVdfDocument::emptySteamShortcuts().serialize());event["shortcutsBeforeSha256"]=sha256(readBinaryFile(target));event["items"]=json::array();publisherRetry(event,data,white);check(fileText(white)==bom+tail,"Leading BOM/manual alpha removed with managed block");});
        run("publishertext-publish-legacy-sync-remove-positive",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt",black=p/L"black.txt";auto event=membershipEvent(p,"19627","Alpha");textFile(white,"manual-user-rule\n");textFile(black,"");publisherRetry(event,data,white);const auto published=fileText(white);const auto plan=planGameWhitelistSync(ruleState(p,"Alpha"),white,black);commitGameWhitelistSync(plan,data,white,black);check(fileText(white)==published,"Current legacy save changed same-visible published bytes");const auto target=fs::path(toWide(event["shortcutsVdf"].get<std::string>()));writeAtomic(target,BinaryVdfDocument::emptySteamShortcuts().serialize());event["shortcutsBeforeSha256"]=sha256(readBinaryFile(target));event["items"]=json::array();publisherRetry(event,data,white);check(readGameRuleFile(white)==std::vector<std::string>({"manual-user-rule"}),"Managed removal lost ownership through current legacy consumer");});
        for(const auto* which:{"txt","membership"})run((std::string("publishertext-write-failure-retry-")+which).c_str(),[&,which](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt",state=data/L"state"/L"ymcc-game-whitelist-membership.json";auto event=membershipEvent(p,"19627","Alpha");textFile(white,"manual\n");const auto target=std::string(which)=="txt"?white:state;unsigned attempts=0;selftestMoveFault=[&](const auto&,const auto& to)->DWORD{if(identityCallerPath(to)==identityCallerPath(target.wstring())){++attempts;return ERROR_SHARING_VIOLATION;}return ERROR_SUCCESS;};for(int retry=0;retry<3;++retry){event.erase("ymccMembershipAuthority");publisherCall(event,data,white);publisherWarning(event);check(fileText(white)=="manual\n","Write failure changed TXT");}selftestMoveFault={};check(attempts==60,"Explicit retry escaped original20-attempt write budget");publisherRetry(event,data,white);});
        for(int mode:{1,2})run((std::string("publishertext-")+(mode==1?"partial-write":"flush-failure")+"-retry").c_str(),[&,mode](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");textFile(white,"manual\n");configIoTarget=white.wstring()+L".yeman-tmp-";configIoMode=mode;publisherCall(event,data,white);configIoMode=0;configIoTarget.clear();publisherWarning(event);check(fileText(white)=="manual\n","Failed staging write changed original");publisherRetry(event,data,white);});
        for(const auto* change:{"different-bytes","same-bytes-new-file","missing-now-empty","noop-changed"})run((std::string("publishertext-observed-source-")+change).c_str(),[&,change](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt",state=data/L"state"/L"ymcc-game-whitelist-membership.json";auto event=membershipEvent(p,"19627","Alpha");const std::string kind=change,raw=kind=="noop-changed"?custom_steam_library::ymcc_whitelist::renderManagedBlock("manual\n",{"alpha"}):"manual\n";if(kind!="missing-now-empty")textFile(white,raw);bool fired=false;std::string external;ruleTextAfterMove=[&](const std::wstring&,const std::wstring& to){if(!fired&&identityCallerPath(to)==identityCallerPath(state.wstring())){fired=true;external=kind=="same-bytes-new-file"?raw:(kind=="missing-now-empty"?"":"newer-manual\n");writeAtomic(white,std::vector<unsigned char>(external.begin(),external.end()));}};publisherCall(event,data,white);ruleTextAfterMove={};check(fired,"Source-change actor did not execute");publisherWarning(event);check(fileText(white)==external,"Observed source identity/bytes overwritten");publisherRetry(event,data,white);});
        run("publishertext-canonical-readback-change-warning-retry",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");textFile(white,"manual\n");bool fired=false;ruleTextAfterMove=[&](const std::wstring& from,const std::wstring& to){if(!fired&&identityCallerPath(to)==identityCallerPath(white.wstring())&&from.find(L".yeman-tmp-")!=std::wstring::npos){fired=true;const std::string raw="newer-external\n";writeAtomic(white,std::vector<unsigned char>(raw.begin(),raw.end()));}};publisherCall(event,data,white);ruleTextAfterMove={};check(fired,"Postcommit actor did not execute");publisherWarning(event);check(fileText(white)=="newer-external\n","Readback conflict overwrote newer file");publisherRetry(event,data,white);});
        run("publishertext-membership-readback-change-warning-retry",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt",state=data/L"state"/L"ymcc-game-whitelist-membership.json";auto event=membershipEvent(p,"19627","Alpha");textFile(white,"manual\n");bool fired=false;ruleTextAfterMove=[&](const std::wstring&,const std::wstring& to){if(!fired&&identityCallerPath(to)==identityCallerPath(state.wstring())){fired=true;auto latest=loadJsonDocument(state);latest["externalUnknown"]=true;writeJsonAtomic(state,latest);}};publisherCall(event,data,white);ruleTextAfterMove={};check(fired,"Membership readback actor did not execute");publisherWarning(event);check(fileText(white)=="manual\n"&&loadJsonDocument(state)["externalUnknown"]==true,"Failed membership readback lost peer/original state");publisherRetry(event,data,white);check(loadJsonDocument(state)["externalUnknown"]==true,"Explicit retry erased unknown ownership field");});
        run("publishertext-valid-output-exact-limit",[&](const fs::path& p){const auto data=p/L"data",white=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");const auto block=custom_steam_library::ymcc_whitelist::renderManagedBlock("",{"alpha"});const auto raw=std::string((1u<<20)-block.size()-1,'#')+"\n";textFile(white,raw);publisherRetry(event,data,white);check(fileText(white).size()==(1u<<20)&&fileText(white).starts_with(raw)&&readGameRuleFile(white)==std::vector<std::string>({"alpha"}),"Legal exact-limit publisher output lost bytes/rule");});
        run("publishertext-ownership-budget-preserves-base-retry",[&](const fs::path& p){namespace m=custom_steam_library::ymcc_whitelist;const auto data=p/L"data",white=p/L"rules.txt",state=data/L"state"/L"ymcc-game-whitelist-membership.json";auto event=membershipEvent(p,"19627","Alpha");textFile(white,"manual\n");auto base=m::emptyMembership();base["futureOpaque"]="";const auto overhead=base.dump(2).size();base["futureOpaque"]=std::string((1u<<20)-overhead-32,'x');writeJsonAtomic(state,base);const auto before=fileText(state);m::validateMembership(base);check(before.size()<(1u<<20),"Large valid ownership fixture invalid");publisherCall(event,data,white);publisherWarning(event);check(fileText(state)==before&&fileText(white)=="manual\n","Publisher exceeded reader budget and replaced valid ownership base");base["futureOpaque"]="explicitly-repaired";writeJsonAtomic(state,base);publisherRetry(event,data,white);check(loadJsonDocument(state)["futureOpaque"]=="explicitly-repaired","Explicit budget repair lost unknown field");});
        auto discoveryDoc=[&](Fixture& f){textFile(f.root/L"Game"/L"Game.exe","owned marker, never executed");return json{{"schemaVersion",1},{"futureUnknown",{{"keep",true}}},{"games",json::array({{{"gameDirectory",toUtf8((f.root/L"Game").wstring())},{"primaryExecutable",toUtf8((f.root/L"Game"/L"Game.exe").wstring())},{"status","ready"},{"primaryRole","main-game"},{"scanComplete",true},{"selectionSource","manual"},{"nativeSteamAppId",0}}})}};};
        auto discoveryInactive=[&](const json& state){check(state.is_object()&&state.contains("discoveryState")&&state["discoveryState"].is_object()&&state["discoveryState"]["active"]==false,"Unknown scan became active/implicit empty default");};
        auto discoveryRetry=[&](Fixture& f,const fs::path& path){writeJsonAtomic(path,discoveryDoc(f));const auto state=loadLibraryScanState(path);check(state["games"].size()==1&&state["games"][0]["status"]=="ready"&&state["futureUnknown"]["keep"]==true,"Explicit valid repair remained inactive/lost unknown fields");};
        const std::vector<std::pair<std::string,std::string>> discoveryRawBad={{"empty",""},{"truncated","{\"games\":["},{"utf8",std::string("{\"games\":[],\"x\":\"")+char(0xff)+"\"}"},{"nul",std::string("{\"games\":[]}\0",13)},{"duplicate-root","{\"schemaVersion\":1,\"games\":[],\"games\":[]}"},{"duplicate-decoded-key","{\"games\":[],\"g\\u0061mes\":[]}"},{"duplicate-nested","{\"games\":[],\"other\":{\"x\":1,\"x\":2}}"}};
        for(const auto& [name,bytes]:discoveryRawBad)run(("discovery-read-"+name).c_str(),[&,bytes](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";textFile(path,bytes);discoveryInactive(loadLibraryScanState(path));check(fileText(path)==bytes,"Bad scan original replaced without confirmed recovery");discoveryRetry(f,path);});
        for(const auto* variant:{"future","schema-string","schema-null","schema-float","wrong-root","games-null","games-object","missing-games","missingGames-type","unscannedGames-type","roots-type","summary-type"})run((std::string("discovery-format-")+variant).c_str(),[&,variant](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";auto doc=discoveryDoc(f);const std::string v=variant;if(v=="future")doc["schemaVersion"]=99;if(v=="schema-string")doc["schemaVersion"]="1";if(v=="schema-null")doc["schemaVersion"]=nullptr;if(v=="schema-float")doc["schemaVersion"]=1.5;if(v=="wrong-root")doc=json::array();if(v=="games-null")doc["games"]=nullptr;if(v=="games-object")doc["games"]=json::object();if(v=="missing-games")doc.erase("games");if(v=="missingGames-type")doc["missingGames"]=7;if(v=="unscannedGames-type")doc["unscannedGames"]="wrong";if(v=="roots-type")doc["roots"]=false;if(v=="summary-type")doc["summary"]=7;writeJsonAtomic(path,doc);const auto before=fileText(path);discoveryInactive(loadLibraryScanState(path));check(fileText(path)==before,"Unknown/future scan overwritten");discoveryRetry(f,path);});
        run("discovery-missing-is-inactive",[&](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";discoveryInactive(loadLibraryScanState(path));check(!fs::exists(path),"Missing scan fabricated as completed empty inventory");discoveryRetry(f,path);});
        for(const auto* field:{"nativeSteamAppId","alternateGameDirectories","primaryPathEvidence","primaryIdentityConfidence"})run((std::string("discovery-row-type-")+field).c_str(),[&,field](const fs::path& p){Fixture f(p,true);auto doc=discoveryDoc(f);doc["games"][0][field]="invalid";const auto path=f.data/L"state"/L"library-scan.json";writeJsonAtomic(path,doc);const auto state=loadLibraryScanState(path);check(state["games"].size()==1&&state["games"][0]["status"]=="data-invalid-review-required"&&state["games"][0]["primaryExecutable"]=="","Bad row retained destructive/import authority");const auto plan=buildSteamLibraryAddPlan(f.data,path,f.target,f.planFile);check(plan["summary"]["readyToAdd"]==0,"Bad row reached ready plan");});
        run("discovery-row-null-does-not-throw-delete-plan",[&](const fs::path& p){Fixture f(p,true);auto doc=discoveryDoc(f);doc["games"][0]["primaryExecutable"]=nullptr;doc["games"][0]["status"]=nullptr;const auto path=f.data/L"state"/L"library-scan.json";writeJsonAtomic(path,doc);const auto plan=buildSteamLibraryDeletePlan(f.data,path,f.target,f.planFile,{canonicalPathKey(f.root/L"Game")});check(plan["summary"]["readyToDelete"]==0,"Null primary became delete authority");});
        run("discovery-legacy-bom-unknown-kept",[&](const fs::path& p){Fixture f(p,true);auto doc=discoveryDoc(f);doc.erase("schemaVersion");const auto path=f.data/L"state"/L"library-scan.json";const auto bytes=std::string("\xef\xbb\xbf",3)+doc.dump();textFile(path,bytes);const auto state=loadLibraryScanState(path);check(state["games"].size()==1&&state["futureUnknown"]["keep"]==true&&fileText(path)==bytes,"Legacy/BOM/unknown fields lost");});
        run("discovery-directory-is-inactive-retry",[&](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";fs::create_directories(path);discoveryInactive(loadLibraryScanState(path));check(fs::is_directory(path),"Directory placeholder removed");fs::remove(path);discoveryRetry(f,path);});
        run("discovery-hardlink-is-inactive",[&](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json",alias=p/L"scan.alias";writeJsonAtomic(path,discoveryDoc(f));const auto bytes=fileText(path);check(CreateHardLinkW(alias.c_str(),path.c_str(),nullptr),"Owned hardlink setup failed");discoveryInactive(loadLibraryScanState(path));check(fileText(path)==bytes&&fileText(alias)==bytes,"Nonowned shared file changed");fs::remove(alias);check(loadLibraryScanState(path)["games"].size()==1,"Unlinked retry failed");});
        for(const auto* fault:{"lock","acl"})run((std::string("discovery-io-")+fault+"-no-backup-authority").c_str(),[&,fault](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";writeJsonAtomic(path,discoveryDoc(f));writeJsonAtomic(fs::path(path.wstring()+L".bak"),discoveryDoc(f));const auto bytes=fileText(path);if(std::string(fault)=="lock"){HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(h!=INVALID_HANDLE_VALUE,"Owned lock setup failed");try{discoveryInactive(loadLibraryScanState(path));}catch(...){CloseHandle(h);throw;}CloseHandle(h);}else withConfigReadDenied(path,[&]{discoveryInactive(loadLibraryScanState(path));});check(fileText(path)==bytes&&loadLibraryScanState(path)["games"].size()==1,"IO release did not retry intact source");});
        run("discovery-backup-repair-write-failure-not-active",[&](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json",backup=fs::path(path.wstring()+L".bak");textFile(path,"bad JSON");writeJsonAtomic(backup,discoveryDoc(f));const auto saved=fileText(backup);selftestMoveFault=[&](const auto&,const auto& to)->DWORD{return identityCallerPath(to)==identityCallerPath(path.wstring())?ERROR_WRITE_FAULT:ERROR_SUCCESS;};discoveryInactive(loadLibraryScanState(path));selftestMoveFault={};check(fileText(path)=="bad JSON"&&fileText(backup)==saved,"Failed repair lost original/recovery source");check(loadLibraryScanState(path)["games"].size()==1,"Released backup repair did not recover");check(fileText(backup)==saved,"Valid .bak replaced with corrupt source");});
        run("discovery-backup-readback-change-not-active",[&](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";textFile(path,"bad JSON");writeJsonAtomic(fs::path(path.wstring()+L".bak"),discoveryDoc(f));bool fired=false;ruleTextAfterMove=[&](const auto&,const auto& to){if(!fired&&identityCallerPath(to)==identityCallerPath(path.wstring())){fired=true;writeJsonAtomic(path,{{"schemaVersion",99},{"games",json::array()},{"keep","external"}});}};const auto result=loadLibraryScanState(path);ruleTextAfterMove={};check(fired,"Repair readback actor did not run");discoveryInactive(result);check(loadJsonDocument(path)["schemaVersion"]==99,"Newer external record overwritten");discoveryRetry(f,path);});
        run("discovery-future-no-stale-backup",[&](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";writeJsonAtomic(path,{{"schemaVersion",99},{"games",json::array()},{"keep","future"}});writeJsonAtomic(fs::path(path.wstring()+L".bak"),discoveryDoc(f));const auto bytes=fileText(path);discoveryInactive(loadLibraryScanState(path));check(fileText(path)==bytes,"Future format restored from old backup");});
        run("discovery-unknown-plan-preserves-membership-rules",[&](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json",rules=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");publishYmccGameWhitelist(event,f.data,true,rules);const auto member=f.data/L"state"/L"ymcc-game-whitelist-membership.json";const auto beforeMember=fileText(member),beforeRules=fileText(rules);writeJsonAtomic(path,{{"schemaVersion",99},{"games",json::array()}});textFile(f.planFile,"prior plan bytes");const auto add=buildSteamLibraryAddPlan(f.data,path,f.target,f.planFile);discoveryInactive(add);check(fileText(member)==beforeMember&&fileText(rules)==beforeRules&&fileText(f.planFile)=="prior plan bytes","Unknown scan cleared ownership or overwrote plan");const auto del=buildSteamLibraryDeletePlan(f.data,path,f.target,f.planFile,{canonicalPathKey(p/L"Games"/L"Alpha")});discoveryInactive(del);check(fileText(f.planFile)=="prior plan bytes","Unknown delete plan overwrote prior receipt");});
        for(const auto* kind:{"write-failure","future-output","readback-change"})run((std::string("discovery-plan-")+kind).c_str(),[&,kind](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";writeJsonAtomic(path,discoveryDoc(f));const std::string v=kind;const auto before=v=="future-output"?json{{"schemaVersion",99},{"keep","future"}}.dump():"old plan bytes";textFile(f.planFile,before);bool fired=false;if(v=="write-failure")selftestMoveFault=[&](const auto&,const auto& to)->DWORD{return identityCallerPath(to)==identityCallerPath(f.planFile.wstring())?ERROR_WRITE_FAULT:ERROR_SUCCESS;};if(v=="readback-change")ruleTextAfterMove=[&](const auto&,const auto& to){if(!fired&&identityCallerPath(to)==identityCallerPath(f.planFile.wstring())){fired=true;writeJsonAtomic(f.planFile,{{"schemaVersion",99},{"keep","external"}});}};const auto result=buildSteamLibraryAddPlan(f.data,path,f.target,f.planFile);selftestMoveFault={};ruleTextAfterMove={};discoveryInactive(result);check(!result.value("planWriteConfirmed",true),"Unconfirmed writer ACKed plan");if(v=="readback-change")check(fired&&loadJsonDocument(f.planFile)["keep"]=="external","Changed canonical plan not retained");else check(fileText(f.planFile)==before,"Protected/failed plan changed");writeJsonAtomic(f.planFile,{{"schemaVersion",1},{"items",json::array()}});const auto retry=buildSteamLibraryAddPlan(f.data,path,f.target,f.planFile);check(retry.value("planWriteConfirmed",false)&&retry["items"].size()==1,"Released plan writer failed explicit retry");});
        run("discovery-valid-add-delete-plan-readback",[&](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";writeJsonAtomic(path,discoveryDoc(f));auto doc=BinaryVdfDocument::emptySteamShortcuts();vdfFindObject(doc.root(),"shortcuts")->objectValue.push_back({"0",steamShortcutNode(-1073741823,"Game",quotedSteamPath(f.root/L"Game"/L"Game.exe"),quotedSteamPath(f.root/L"Game"),0)});writeAtomic(f.target.path,doc.serialize());const auto steamBefore=readBinaryFile(f.target.path);const auto add=buildSteamLibraryAddPlan(f.data,path,f.target,f.planFile);auto persistedAdd=add;persistedAdd.erase("planWriteConfirmed");check(add["items"].size()==1&&readLibraryTransactionDocument(f.planFile,64u<<20)==persistedAdd,"Healthy add plan not saved exactly");const auto del=buildSteamLibraryDeletePlan(f.data,path,f.target,f.data/L"state"/L"delete-plan.json",{canonicalPathKey(f.root/L"Game")});check(del["summary"]["readyToDelete"]==1&&readBinaryFile(f.target.path)==steamBefore,"Healthy delete plan lost match or wrote fake Steam");});
        for(const auto* kind:{"add","delete"})run((std::string("discovery-observed-scan-change-")+kind).c_str(),[&,kind](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";writeJsonAtomic(path,discoveryDoc(f));textFile(f.planFile,"prior plan bytes");bool fired=false;identityCallerReadHook=[&](const std::wstring& file){if(!fired&&file==identityCallerPath(f.target.path.wstring())){fired=true;writeJsonAtomic(path,{{"schemaVersion",99},{"games",json::array()},{"keep","newer source"}});}};const auto plan=std::string(kind)=="add"?buildSteamLibraryAddPlan(f.data,path,f.target,f.planFile):buildSteamLibraryDeletePlan(f.data,path,f.target,f.planFile,{canonicalPathKey(f.root/L"Game")});identityCallerReadHook={};check(fired,"Observed-scan actor did not execute");discoveryInactive(plan);check(fileText(f.planFile)=="prior plan bytes"&&loadJsonDocument(path)["keep"]=="newer source","Stale scan observation ACKed/overwrote plan or source");discoveryRetry(f,path);});
        run("discovery-oversize-is-inactive-retry",[&](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";fs::create_directories(path.parent_path());{std::ofstream file(path,std::ios::binary);file.seekp(64u<<20);file.put(' ');}check(fs::file_size(path)==(64u<<20)+1,"Oversize fixture size wrong");discoveryInactive(loadLibraryScanState(path));check(fs::file_size(path)==(64u<<20)+1,"Oversize original removed/truncated");discoveryRetry(f,path);});
        run("discovery-large-valid-unknown-and-unicode",[&](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json";auto doc=discoveryDoc(f);doc["futureUnknown"]["opaque"]=std::string(1u<<20,'x');doc["futureUnknown"]["unicode"]=toUtf8(L"\u4e2d\u6587\u5b58\u91cf");writeJsonAtomic(path,doc);const auto bytes=fileText(path);const auto state=loadLibraryScanState(path);check(state["futureUnknown"]==doc["futureUnknown"]&&state["games"].size()==1&&fileText(path)==bytes,"Large legal/Unicode/unknown fields lost");});
        run("discovery-restored-backup-not-complete-authority",[&](const fs::path& p){Fixture f(p,true);const auto path=f.data/L"state"/L"library-scan.json",rules=p/L"rules.txt";auto event=membershipEvent(p,"19627","Alpha");publishYmccGameWhitelist(event,f.data,true,rules);const auto member=f.data/L"state"/L"ymcc-game-whitelist-membership.json";const auto beforeEntries=loadJsonDocument(member)["accounts"]["19627"]["entries"];check(beforeEntries.size()==1,"Live ownership fixture not created");textFile(path,"damaged scan JSON");const json emptyComplete={{"schemaVersion",1},{"games",json::array()},{"roots",json::array()},{"missingGames",json::array()},{"unscannedGames",json::array()},{"futureUnknown",{{"keep",true}}}};writeJsonAtomic(fs::path(path.wstring()+L".bak"),emptyComplete);for(int attempt=0;attempt<2;++attempt){const auto plan=buildSteamLibraryAddPlan(f.data,path,f.target,f.planFile);check(plan.value("planWriteConfirmed",false),"Verified repaired discovery plan did not remain available");check(loadJsonDocument(member)["accounts"]["19627"]["entries"]==beforeEntries,"Recovered old empty inventory cleared current membership");}check(loadLibraryScanState(path)["futureUnknown"]["keep"]==true,"Backup unknown fields lost");writeJsonAtomic(path,emptyComplete);const auto fresh=buildSteamLibraryAddPlan(f.data,path,f.target,f.planFile);check(fresh.value("planWriteConfirmed",false)&&loadJsonDocument(member)["accounts"]["19627"]["entries"].empty(),"Fresh explicit complete inventory could not release owned membership");});
        for(const auto* kind:{"future","duplicate","bad-type","candidate-container","escaped-nul"})run((std::string("discovery-producer-mapping-")+kind).c_str(),[&,kind](const fs::path& p){DiscoveryProducerFixture f(p);auto doc=f.catalog();const std::string k=kind;std::string raw;if(k=="future")doc["schemaVersion"]=99;if(k=="bad-type")doc["games"][0]["nativeSteamAppId"]="wrong";if(k=="candidate-container")doc["games"][0]["candidates"]={{"alias",{{"path",toUtf8(f.exe.wstring())}}}};if(k=="escaped-nul")doc["games"][0]["gameDirectory"]=toUtf8(f.exe.parent_path().parent_path().wstring())+std::string(1,'\0')+"tail";raw=doc.dump();if(k=="duplicate")raw.insert(1,"\"schemaVersion\":99,");textFile(f.scan,raw);bool source=true;const auto mapped=persistentGameDirectoryForExecutable(f.data,f.exe,&source);check(!source&&canonicalPathKey(mapped)==canonicalPathKey(f.exe.parent_path()),"Unknown raw scan invented a verified game directory");check(fileText(f.scan)==raw,"Mapping rewrote unknown original");writeJsonAtomic(f.scan,f.catalog());source=false;check(canonicalPathKey(persistentGameDirectoryForExecutable(f.data,f.exe,&source))==canonicalPathKey(f.exe.parent_path().parent_path())&&source,"Valid explicit mapping repair failed");});
        run("discovery-producer-mapping-lock-release",[&](const fs::path& p){DiscoveryProducerFixture f(p);writeJsonAtomic(f.scan,f.catalog());writeJsonAtomic(fs::path(f.scan.wstring()+L".bak"),f.catalog());HANDLE h=CreateFileW(f.scan.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(h!=INVALID_HANDLE_VALUE,"Owned mapping lock failed");bool source=true;const auto mapped=persistentGameDirectoryForExecutable(f.data,f.exe,&source);CloseHandle(h);check(!source&&canonicalPathKey(mapped)==canonicalPathKey(f.exe.parent_path()),"IO fault became known cached mapping");check(!persistentGameDirectoryForExecutable(f.data,f.exe,&source).empty()&&source,"Mapping lock release not retried");});
        for(const auto* kind:{"future","schema-type","recovery-future"})run((std::string("discovery-producer-scan-protect-")+kind).c_str(),[&,kind](const fs::path& p){DiscoveryProducerFixture f(p);auto doc=f.catalog();const std::string k=kind;if(k=="future")doc["schemaVersion"]=99;if(k=="schema-type")doc["schemaVersion"]="1";if(k=="recovery-future")doc["libraryScanRecovery"]={{"schemaVersion",99},{"requiresFreshScan",true}};writeJsonAtomic(f.scan,doc);const auto raw=fileText(f.scan);const auto code=f.fresh();check(code==2&&!f.scanAck()&&f.error.find("ERROR_LIBRARY_SCAN_UNCONFIRMED:")!=std::string::npos&&f.output.find("LIBRARY_SCAN_STATE: unknown")!=std::string::npos&&fileText(f.scan)==raw,"Unconfirmed scan CLI returned success or lost protected original");writeJsonAtomic(f.scan,f.catalog());check(f.fresh()==0&&f.scanAck(),"Protected scan did not retry after valid repair");});
        for(const auto* kind:{"lock","acl","directory"})run((std::string("discovery-producer-scan-io-")+kind).c_str(),[&,kind](const fs::path& p){DiscoveryProducerFixture f(p);const std::string k=kind;if(k=="directory")fs::create_directories(f.scan);else writeJsonAtomic(f.scan,f.catalog());std::string raw=k=="directory"?"":fileText(f.scan);int code=-1;if(k=="lock"){HANDLE h=CreateFileW(f.scan.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(h!=INVALID_HANDLE_VALUE,"Owned scan lock failed");code=f.fresh();CloseHandle(h);}else if(k=="acl")withConfigReadDenied(f.scan,[&]{code=f.fresh();});else code=f.fresh();check(code==2&&!f.scanAck()&&f.error.find("ERROR_LIBRARY_SCAN_UNCONFIRMED:")!=std::string::npos&&f.output.find("LIBRARY_SCAN_STATE: unknown")!=std::string::npos,"Unconfirmed scan CLI returned success or ACKed unreadable previous state");if(k=="directory"){check(fs::is_directory(f.scan),"Nonfile original deleted");fs::remove(f.scan);}else check(fileText(f.scan)==raw,"Unreadable original changed");writeJsonAtomic(f.scan,f.catalog());check(f.fresh()==0&&f.scanAck(),"Released scan IO did not recover");});
        for(const auto* kind:{"truncated","duplicate"})run((std::string("discovery-producer-scan-damaged-")+kind).c_str(),[&,kind](const fs::path& p){DiscoveryProducerFixture f(p);const std::string raw=std::string(kind)=="truncated"?"{broken scan":"{\"games\":[],\"games\":[]}";textFile(f.scan,raw);check(f.fresh()==0&&f.scanAck(),"Explicit scan could not conservatively rebuild readable damage");const auto doc=loadJsonDocument(f.scan);check(doc["libraryScanRecovery"]["requiresFreshScan"]==true&&doc["libraryScanRecovery"]["unknownPreviousInventory"]==true,"Damaged previous inventory became complete empty permission");check(producerArchivedBytes(f.data/L"backups",raw),"Damaged scan original not preserved");check(f.fresh()==0&&f.scanAck()&&loadJsonDocument(f.scan)["libraryScanRecovery"]["unknownPreviousInventory"]==true,"Explicit retry erased unknown previous inventory debt");});
        for(const auto* kind:{"persistent-failure","persistent-readback","job-future","job-readback"})run((std::string("discovery-producer-scan-write-")+kind).c_str(),[&,kind](const fs::path& p){DiscoveryProducerFixture f(p);writeJsonAtomic(f.scan,f.catalog());const auto before=fileText(f.scan);const auto output=f.job/L"library-scan.json";const std::string k=kind;if(k=="job-future")writeJsonAtomic(output,{{"schemaVersion",99},{"keep","future job"}});bool fired=false;if(k=="persistent-failure")selftestMoveFault=[&](const auto&,const auto& to)->DWORD{return identityCallerPath(to)==identityCallerPath(f.scan.wstring())?ERROR_WRITE_FAULT:ERROR_SUCCESS;};if(k=="persistent-readback"||k=="job-readback")ruleTextAfterMove=[&](const auto&,const auto& to){const auto selected=k=="persistent-readback"?f.scan:output;if(!fired&&identityCallerPath(to)==identityCallerPath(selected.wstring())){fired=true;writeJsonAtomic(selected,{{"schemaVersion",99},{"keep","external"}});}};const int code=f.fresh();selftestMoveFault={};ruleTextAfterMove={};check(code==2&&!f.scanAck()&&f.error.find("ERROR_LIBRARY_SCAN_UNCONFIRMED:")!=std::string::npos,"Unconfirmed scan CLI returned success or ACKed failed publication");check(f.output.find(k=="job-readback"?"LIBRARY_STATE_COMMITTED: true":"LIBRARY_STATE_COMMITTED: false")!=std::string::npos,"Scan refusal lost actual partial-state publication receipt");if(k=="persistent-failure"||k=="job-future")check(fileText(f.scan)==before,"Failed/protected output changed persistent state");if(k=="persistent-readback")check(fired&&loadJsonDocument(f.scan)["keep"]=="external","Canonical scan readback conflict overwritten");if(k=="job-readback")check(fired&&loadJsonDocument(output)["keep"]=="external","Job readback conflict overwritten");writeJsonAtomic(f.scan,f.catalog());writeJsonAtomic(output,{{"schemaVersion",1},{"games",json::array()}});check(f.fresh()==0&&f.scanAck(),"Scan publication release did not recover");});
        run("discovery-producer-scan-healthy-lifecycle",[&](const fs::path& p){DiscoveryProducerFixture f(p);auto previous=f.catalog();previous["games"][0]["userMetadata"]={{"keep",true}};previous["missingGames"]=json::array({{{"gameDirectory",toUtf8((p/L"MissingOld").wstring())},{"primaryExecutable",toUtf8((p/L"MissingOld"/L"Old.exe").wstring())},{"status","ready"},{"dataRetained",true},{"opaqueDebt","keep"}}});previous["libraryScanRecovery"]={{"schemaVersion",1},{"requiresFreshScan",true}};writeJsonAtomic(f.scan,previous);check(f.fresh()==0&&f.scanAck(),"Healthy original producer did not commit");const auto stored=loadJsonDocument(f.scan),job=loadJsonDocument(f.job/L"library-scan.json");check(stored==job&&stored["unscannedGames"].size()==1&&stored["unscannedGames"][0]["userMetadata"]["keep"]==true&&stored["missingGames"][0]["opaqueDebt"]=="keep","Actual previous-state merge lost retained lifecycle entries");check(!stored.contains("libraryScanRecovery"),"Fresh healthy known inventory could not release backup-only provenance");});
        if(GetEnvironmentVariableW(L"YMCC_DISCOVERY_NATIVE_ROOT_CONTROL",nullptr,0))
        run("discovery-producer-scan-nested-steam",[&](const fs::path& p){
            DiscoveryProducerFixture f(p);
            const auto steam=f.games/L"Steam", apps=steam/L"steamapps", common=apps/L"common";
            auto seed=[&](const fs::path& appRoot,const wchar_t* name,int id) {
                const auto dir=appRoot/L"common"/name, exe=dir/(std::wstring(name)+L".exe");
                textFile(exe,"owned marker; never executed");
                if(id)textFile(appRoot/(L"appmanifest_"+std::to_wstring(id)+L".acf"),
                    "\"AppState\" { \"appid\" \""+std::to_string(id)+"\" \"name\" \""+toUtf8(name)+"\" \"installdir\" \""+toUtf8(name)+"\" }");
                return json{{"gameDirectory",toUtf8(dir.wstring())},{"primaryExecutable",toUtf8(exe.wstring())},
                    {"status","ready"},{"steamNative",true},{"discoverySource","steam-native"},{"nativeSteamAppId",id}};
            };
            const auto installed=seed(apps,L"Owned Game",778801), oldMissing=seed(apps,L"Restored Game",778802);
            seed(apps,L"Copied Game",0);
            seed(apps,L"Steamworks Shared",228980);
            seed(p/L"Unrelated"/L"steamapps",L"Unrelated Game",778803);
            auto outside=toUtf8((p/L"Unrelated").wstring());
            for(size_t i=0;(i=outside.find('\\',i))!=std::string::npos;i+=2)outside.insert(i,1,'\\');
            textFile(apps/L"libraryfolders.vdf","\"libraryfolders\" { \"1\" { \"path\" \""+outside+"\" } }");
            const auto deletedDir=common/L"Deleted Game";
            auto deleted=installed;deleted["gameDirectory"]=toUtf8(deletedDir.wstring());
            deleted["primaryExecutable"]=toUtf8((deletedDir/L"Deleted Game.exe").wstring());
            auto previous=f.catalog();previous["games"]=json::array({installed,deleted});
            previous["missingGames"]=json::array({oldMissing});writeJsonAtomic(f.scan,previous);
            struct RestoreFacts{bool available=selftestSteamFactsAvailable;~RestoreFacts(){selftestSteamFactsAvailable=available;}} restoreFacts;
            selftestSteamFactsAvailable=false;
            check(f.fresh()==0&&f.scanAck(),"Nested Steam scan failed before publishing");
            const auto actual=loadJsonDocument(f.scan);
            writeJsonAtomic(p/L"nested-native-observed.json",actual);
            check(actual==loadJsonDocument(f.job/L"library-scan.json"),"Nested Steam scan reply does not match inventory");
            const auto has=[](const json& doc,int id){return std::any_of(doc["games"].begin(),doc["games"].end(),[&](const json& item){
                return jsonIntCaseInsensitive(item,"nativeSteamAppId").value_or(0)==id&&jsonBoolSafe(item,"steamNative",false);
            });};
            check(has(actual,778801)&&has(actual,778802),"Steam platform directory observed but both installed native games were not retained");
            check(actual["games"].size()==2,"Unrelated, unmanifested or built-in excluded directory entered active inventory");
            check(actual["missingGames"].size()==1&&actual["missingGames"][0]["gameDirectory"]==deleted["gameDirectory"]&&
                actual["missingGames"][0]["lifecycleStatus"]=="game-directory-missing","Real deletion was not distinguished from native discovery loss");
            check(actual["lifecycleTransitions"].size()==2&&std::any_of(actual["lifecycleTransitions"].begin(),actual["lifecycleTransitions"].end(),[&](const json& row){
                return row["type"]=="restored-or-reinstalled"&&row["gameDirectory"]==oldMissing["gameDirectory"];
            }),"Known native game did not recover from the old missing state");
            mustThrow([&]{(void)steamInstallPath(true);});
            check(f.call({L"--scan-library",f.job.wstring(),L"--root",common.wstring(),L"--trainer-root",f.trainers.wstring(),L"--max-depth",L"1"})==0&&f.scanAck(),"Explicit Steam root scan failed");
            const auto explicitScan=loadJsonDocument(f.scan);writeJsonAtomic(p/L"explicit-native-observed.json",explicitScan);
            check(has(explicitScan,778801)&&has(explicitScan,778803),"Explicit root no longer follows its existing Steam libraryfolders");
            const auto manifest=apps/L"appmanifest_778801.acf";
            const auto validManifest=fileText(manifest);
            const auto observe=[&] {
                check(f.call({L"--scan-library",f.job.wstring(),L"--root",common.wstring(),L"--trainer-root",f.trainers.wstring(),L"--max-depth",L"1"})==0&&f.scanAck(),
                    "Healthy neighboring appmanifest did not allow partial scan");
                const auto report=loadJsonDocument(f.scan);
                check(report==loadJsonDocument(f.job/L"library-scan.json"),"Manifest check scan reply did not match inventory");
                return report;
            };
            const std::pair<const wchar_t*,std::string> invalidManifests[]={
                {L"wrong-appid",R"("AppState" { "appid" "778899" "installdir" "Owned Game" })"},
                {L"duplicate-appid",R"("AppState" { "appid" "778801" "appid" "778899" "installdir" "Owned Game" })"},
                {L"truncated",R"("AppState" { "appid" "778801" "installdir" "Owned Game")"},
                {L"missing-appid",R"("AppState" { "installdir" "Owned Game" })"}
            };
            for(const auto& [label,text]:invalidManifests) {
                textFile(manifest,text);
                const auto partial=observe();
                writeJsonAtomic(p/(std::wstring(label)+L"-observed.json"),partial);
                check(!has(partial,778801)&&!has(partial,778899)&&has(partial,778802)&&has(partial,778803),
                    "Invalid appmanifest claimed a native game or removed a healthy neighbor");
                check(partial["nativeSteamInstallationUnavailable"]==true&&
                    std::any_of(partial["unscannedGames"].begin(),partial["unscannedGames"].end(),[](const json& row){
                        return row.value("nativeSteamAppId",0)==778801&&jsonBoolSafe(row,"nativeSteamScanUnconfirmed",false);
                    }),"Invalid appmanifest settled a previously visible card instead of retaining it unconfirmed");
            }
            textFile(manifest,validManifest);
            check(has(observe(),778801),"Corrected appmanifest did not restore native identity");

            const auto duplicate=apps/L"appmanifest_778899.acf";
            textFile(duplicate,R"("AppState" { "appid" "778899" "installdir" "OWNED GAME" })");
            const auto ambiguous=observe();
            const auto direct=steamNativeGameInfoForDirectory(common/L"Owned Game");
            writeJsonAtomic(p/L"shared-install-observed.json",{{"inventory",ambiguous},{"directAppId",direct?direct->appId:0}});
            check(!has(ambiguous,778801)&&!has(ambiguous,778899)&&!direct,
                "Two valid Steam AppIDs sharing a directory picked an arbitrary native identity");
            check(has(ambiguous,778802)&&has(ambiguous,778803)&&std::any_of(ambiguous["unscannedGames"].begin(),ambiguous["unscannedGames"].end(),[](const json& row){
                return row.value("nativeSteamAppId",0)==778801&&jsonBoolSafe(row,"nativeSteamScanUnconfirmed",false);
            }),
                "Conflicting manifests did not preserve healthy neighbors and the previous card as unconfirmed");
            fs::remove(duplicate);
            const auto repaired=steamNativeGameInfoForDirectory(common/L"Owned Game");
            check(has(observe(),778801)&&repaired&&repaired->appId==778801,"Removing the conflicting manifest did not restore identity");
        });
        if(GetEnvironmentVariableW(L"YMCC_DISCOVERY_NATIVE_ROOT_CONTROL",nullptr,0))
        run("discovery-producer-scan-native-installation-coverage",[&](const fs::path& p){
            DiscoveryProducerFixture f(p);
            const auto config=f.data/L"config"/L"library-config.json", lifecycle=f.data/L"state"/L"lifecycle-events.json";
            const auto result=f.job/L"library-scan.json", apps=f.steam/L"steamapps";
            const auto dir=apps/L"common"/L"Native Title", exe=dir/L"Native Title.exe";
            textFile(exe,"owned marker; never executed");
            textFile(apps/L"appmanifest_778811.acf","\"AppState\" { \"appid\" \"778811\" \"name\" \"Native Title\" \"installdir\" \"Native Title\" }");
            struct RestoreFacts {bool available=selftestSteamFactsAvailable;~RestoreFacts(){selftestSteamFactsAvailable=available;}} restore;
            const auto scan=[&](bool clear=false){
                std::vector<std::wstring> args{L"--scan-library",f.job.wstring(),L"--trainer-root",f.trainers.wstring(),L"--max-depth",L"1"};
                if(clear)args.push_back(L"--clear-roots");
                const auto reads=selftestSteamFactsReads;
                const int code=f.call(std::move(args));
                check(selftestSteamFactsReads-reads==1,"Scan did not use exactly one installation observation");
                return code;
            };
            const auto refused=[&](int code){
                check(code==2&&!f.scanAck()&&f.output.find("LIBRARY_SCAN_STATE: unknown")!=std::string::npos&&
                    f.output.find("LIBRARY_STATE_COMMITTED: false")!=std::string::npos&&
                    f.error.find("ERROR_LIBRARY_SCAN_UNCONFIRMED: [steam-information-unavailable]")!=std::string::npos,
                    "Unknown installation did not return a retryable uncommitted scan");
            };
            selftestSteamFactsAvailable=false;
            refused(scan(true));
            const auto cleared=loadJsonDocument(config);
            check(cleared["roots"].empty()&&cleared["scanRootsExplicitlyCleared"]==true&&cleared["userProducerNote"]=="keep",
                "Refused scan lost explicit clear-roots choice or unrelated config");
            check(!fs::exists(f.scan)&&!fs::exists(result)&&!fs::exists(lifecycle),"Unconfirmed first scan created inventory or lifecycle state");

            auto prior=f.catalog();
            prior["games"][0].update({{"gameDirectory",toUtf8(dir.wstring())},{"primaryExecutable",toUtf8(exe.wstring())},
                {"steamNative",true},{"nativeSteamAppId",778811},{"discoverySource","steam-native"}});
            writeJsonAtomic(f.scan,prior);writeJsonAtomic(result,prior);
            const auto stateBytes=fileText(f.scan), replyBytes=fileText(result);
            refused(scan());
            check(fileText(f.scan)==stateBytes&&fileText(result)==replyBytes&&!fs::exists(lifecycle),
                "Unconfirmed rescan replaced inventory, job output or lifecycle log");

            auto older = prior["games"][0];
            older["gameDirectory"] = toUtf8((apps/L"common"/L"Historical").wstring());
            older["primaryExecutable"] = toUtf8((apps/L"common"/L"Historical"/L"Historical.exe").wstring());
            older["nativeSteamAppId"] = 778812;
            prior["unscannedGames"].push_back(older);
            writeJsonAtomic(f.scan,prior);
            for(int repeat=0; repeat<2; ++repeat) {
                check(f.fresh()==0&&f.scanAck(),"Independent configured-root scan could not publish");
                const auto partial=loadJsonDocument(f.scan);
                const auto pending=std::count_if(partial["unscannedGames"].begin(),partial["unscannedGames"].end(),[](const json& row){
                    return jsonBoolSafe(row,"nativeSteamScanUnconfirmed",false)&&row.value("nativeSteamAppId",0)==778811;
                });
                const auto marked=std::count_if(partial["unscannedGames"].begin(),partial["unscannedGames"].end(),[](const json& row){
                    return jsonBoolSafe(row,"nativeSteamScanUnconfirmed",false);
                });
                check(partial["nativeSteamInstallationUnavailable"]==true&&partial["games"].empty()&&partial["missingGames"].empty()&&
                    partial["unscannedGames"].size()==2&&pending==1&&marked==1,"Partial scan elevated history, lost previous visible native row, or invented a deletion");
            }

            selftestSteamFactsAvailable=true;
            check(scan(true)==0&&f.scanAck(),"Verified installed Steam with zero custom roots did not publish");
            const auto active=loadJsonDocument(f.scan);
            check(!active.contains("nativeSteamInstallationUnavailable"),"Recovered scan retained the unavailable signal");
            check(active["games"].size()==1&&active["games"][0]["nativeSteamAppId"]==778811&&
                active["unscannedGames"].size()==1&&!active["unscannedGames"][0].contains("nativeSteamScanUnconfirmed")&&
                active==loadJsonDocument(result),"Verified scan did not replace the pending row, or elevated history");

            selftestSteamInstallationRoot.clear();
            check(scan()==0&&f.scanAck()&&loadJsonDocument(f.scan)["games"].empty(),"Confirmed absence of a Steam installation was not publishable");
            selftestSteamFactsAvailable=false;
            check(f.fresh()==0&&f.scanAck()&&loadJsonDocument(f.scan)["roots"].size()==1,
                "Independent configured root could not publish under unknown Steam coverage");
        });
        run("discovery-producer-accounts-healthy-readback",[&](const fs::path& p){DiscoveryProducerFixture f(p);check(f.list()==0&&f.accountAck(),"Healthy accounts producer unconfirmed");const auto report=loadJsonDocument(f.accounts);check(report["accounts"].size()==1&&report["accounts"][0]["accountId"]=="19627"&&report["accounts"][0]["readError"].is_null(),"Actual account observation not preserved");
            json rows=json::array();bool safe=true;
            for(const auto* label:{"before-healthy","after-healthy","locked","first-add","two-historical"}){
                const std::string mode=label;DiscoveryProducerFixture owned(p/toWide(mode));check(owned.list()==0&&owned.accountAck(),"Partial account seed failed");
                const auto valid=BinaryVdfDocument::emptySteamShortcuts().serialize();
                const auto bad=owned.steam/L"userdata"/toWide(mode=="before-healthy"?"00012":"19628")/L"config"/L"shortcuts.vdf";
                if(mode=="first-add")fs::remove(owned.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf");
                HANDLE held=INVALID_HANDLE_VALUE;if(mode=="locked"){writeAtomic(bad,valid);held=CreateFileW(bad.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Partial owned lock failed");}else textFile(bad,"malformed owned VDF");
                const auto second=owned.steam/L"userdata"/L"19629"/L"config"/L"shortcuts.vdf";if(mode=="two-historical")textFile(second,"malformed second owned VDF");
                const auto code=owned.list();const bool ack=owned.accountAck();const auto current=loadJsonDocument(owned.accounts);const size_t expected=mode=="two-historical"?2:1;
                const bool isolated=code==0&&ack&&current.value("partialAccounts",false)&&current["accounts"].size()==1&&current["accounts"][0]["accountId"]=="19627"&&current.value("unavailableAccounts",json::array()).size()==expected;
                if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
                const bool preserved=mode=="locked"?fileText(bad)==std::string(valid.begin(),valid.end()):fileText(bad)=="malformed owned VDF";
                writeAtomic(bad,valid);if(mode=="two-historical")writeAtomic(second,valid);
                const bool recovered=owned.list()==0&&owned.accountAck()&&!loadJsonDocument(owned.accounts).value("partialAccounts",false)&&loadJsonDocument(owned.accounts)["accounts"].size()==expected+1;
                rows.push_back({{"variant",mode},{"isolated",isolated},{"preserved",preserved},{"recovered",recovered}});safe=safe&&isolated&&preserved&&recovered;
            }
            writeJsonAtomic(p/L"partial-accounts-observed.json",{{"rows",rows}});check(safe,"Unreadable historical account blocked healthy discovery");});
        for(const auto* kind:{"future","schema-type","lock","write-failure","readback-change"})run((std::string("discovery-producer-accounts-")+kind).c_str(),[&,kind](const fs::path& p){DiscoveryProducerFixture f(p);const std::string k=kind;const auto prior=json{{"schemaVersion",k=="future"?json(99):k=="schema-type"?json("1"):json(1)},{"accounts",json::array()},{"keep","original"}}.dump();textFile(f.accounts,prior);bool fired=false;HANDLE held=INVALID_HANDLE_VALUE;if(k=="lock"){held=CreateFileW(f.accounts.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Owned account-output lock failed");}if(k=="write-failure")selftestMoveFault=[&](const auto&,const auto& to)->DWORD{return identityCallerPath(to)==identityCallerPath(f.accounts.wstring())?ERROR_WRITE_FAULT:ERROR_SUCCESS;};if(k=="readback-change")ruleTextAfterMove=[&](const auto&,const auto& to){if(!fired&&identityCallerPath(to)==identityCallerPath(f.accounts.wstring())){fired=true;writeJsonAtomic(f.accounts,{{"schemaVersion",99},{"accounts",json::array()},{"keep","external"}});}};const auto code=f.list();selftestMoveFault={};ruleTextAfterMove={};if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);check(code==0&&!f.accountAck()&&f.output.find("STEAM_ACCOUNTS_STATUS: unknown")!=std::string::npos,"Unknown account writer crashed/ACKed output");if(k=="readback-change")check(fired&&loadJsonDocument(f.accounts)["keep"]=="external","Account readback conflict overwritten");else check(fileText(f.accounts)==prior,"Unknown account cache replaced");writeJsonAtomic(f.accounts,{{"schemaVersion",1},{"accounts",json::array()}});check(f.list()==0&&f.accountAck(),"Account explicit repair/retry failed");});
        run("discovery-producer-accounts-reply-payload",[&](const fs::path& p){
            DiscoveryProducerFixture f(p);
            const auto reply=[&](){std::istringstream lines(f.output);std::string line,payload;unsigned count=0;while(std::getline(lines,line))if(line.starts_with("STEAM_ACCOUNTS_JSON: ")){++count;payload=line.substr(20);}check(count==1,"Actual Worker did not return exactly one account JSON payload");return json::parse(payload);};
            const auto beforeLocal=selftestSteamFactsReads;
            check(f.list()==0&&f.accountAck(),"Actual Worker account observation failed");const auto report=reply();check(report==loadJsonDocument(f.accounts)&&report["accounts"].size()==1,"Worker reply differs from its confirmed account observation");
            check(report["accounts"][0].value("realSteamTarget",false)&&selftestSteamFactsReads==beforeLocal,"Explicit local account did not use its own Steam marker");
            textFile(f.accounts,"{changed after worker reply");check(reply()==report,"Later shared cache changed immutable Worker reply");
            textFile(f.accounts,R"({"schemaVersion":1,"accounts":[]})");fs::remove(f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf");fs::remove(f.steam/L"userdata"/L"19627"/L"config");fs::remove(f.steam/L"userdata"/L"19627");
            check(f.list()==0&&f.accountAck()&&reply()["steamStatus"]=="installed-no-account"&&reply()["accounts"].empty(),"Actual healthy-empty Worker payload lost");
            textFile(f.accounts,R"({"schemaVersion":99,"accounts":[]})");const auto before=fileText(f.accounts);check(f.list()==0&&!f.accountAck()&&f.output.find("STEAM_ACCOUNTS_JSON:")==std::string::npos&&fileText(f.accounts)==before,"Unknown Worker publication emitted fresh payload");

            DiscoveryProducerFixture observed(p/L"inst");
            auto list=[&](const fs::path& explicitRoot={}) {
                std::vector<std::wstring> args{L"--list-steam-accounts",L"--output",observed.accounts.wstring()};
                if(!explicitRoot.empty())args.insert(args.end(),{L"--steam-root",explicitRoot.wstring()});
                return observed.call(std::move(args));
            };
            auto reads=selftestSteamFactsReads;
            check(list()==0&&observed.accountAck()&&selftestSteamFactsReads-reads==1,"Automatic installed account report did not read once");
            const auto installed=fileText(observed.accounts);
            selftestSteamFactsAvailable=false;
            reads=selftestSteamFactsReads;
            const auto unknownCode=list();
            selftestSteamFactsAvailable=true;
            check(unknownCode==0&&!observed.accountAck()&&fileText(observed.accounts)==installed&&selftestSteamFactsReads-reads==1&&
                observed.error.find("[steam-information-unavailable]")!=std::string::npos,"Unavailable installation replaced the account report");
            selftestSteamInstallationRoot.clear();
            reads=selftestSteamFactsReads;
            check(list()==0&&observed.accountAck()&&selftestSteamFactsReads-reads==1,"Known absent account report did not read once");
            const auto absent=loadJsonDocument(observed.accounts);
            check(absent.value("steamStatus",std::string{})=="not-installed"&&absent["accounts"].empty(),"Known absence changed account report shape");

            const auto alternate=observed.root/L"alternate";
            const auto emptyVdf=BinaryVdfDocument::emptySteamShortcuts().serialize();
            for(const auto* id:{L"42",L"43"})writeAtomic(alternate/L"userdata"/id/L"config"/L"shortcuts.vdf",emptyVdf);
            selftestSteamInstallationRoot=observed.steam.wstring();
            reads=selftestSteamFactsReads;
            check(list(alternate)==0&&observed.accountAck()&&selftestSteamFactsReads-reads==1,"Explicit fallback requested installation per account");
            const auto alternateReport=loadJsonDocument(observed.accounts);
            check(alternateReport["accounts"].size()==2&&std::none_of(alternateReport["accounts"].begin(),alternateReport["accounts"].end(),[](const auto& row){
                return row.value("realSteamTarget",true);
            }),"Other Steam root incorrectly classified as real");
            reads=selftestSteamFactsReads;
            check(list()==0&&observed.accountAck()&&selftestSteamFactsReads-reads==1&&fileText(observed.accounts)==installed,"Later account transaction did not observe a fresh installation");
        });
        run("discovery-producer-add-cli-refuses-unconfirmed-plan",[&](const fs::path& p){
            // A collision for a different card remains visible, but it must
            // not reject an explicitly selected independent game transaction.
            {
                Fixture selected(p/L"sel",true);const auto scan=fixtureScanState(selected);
                const auto ordinary=selected.root/L"Game"/L"Game.exe",collisionDir=selected.root/L"C",collisionExe=collisionDir/L"bin"/L"cod.exe",retainedExe=collisionDir/L"sp26"/L"sp26-cod.exe";
                textFile(collisionExe,"owned inert primary, never executed");textFile(retainedExe,"owned inert prior shortcut executable, never executed");
                auto inventory=loadJsonDocument(scan);auto& healthy=inventory["games"][0];healthy["status"]="ready";healthy["directoryName"]="Independent Game";healthy["primaryRole"]="main-game";
                inventory["games"].push_back({{"gameDirectory",toUtf8(collisionDir.wstring())},{"directoryName","Conflict"},{"primaryExecutable",toUtf8(collisionExe.wstring())},{"primaryRole","main-game"},{"status","ready"},{"alternateGameDirectories",json::array({toUtf8((collisionDir/L"bin").wstring())})}});writeJsonAtomic(scan,inventory);
                const auto image=selected.root/L"slot.png";textFile(image,"owned isolated artwork bytes");
                const auto manifest=selected.data/L"artwork"/toWide(gameDataId(collisionDir))/L"manifest.json";
                writeJsonAtomic(manifest,{{"exe",toUtf8(collisionExe.wstring())},{"match",{{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion",STEAM_RESOLVER_VERSION},{"appId",620},{"formalName","Conflict"},{"score",1.0}}},
                    {"artwork",json::array({{{"type","tall"},{"steamSuffix","p"},{"file",toUtf8(image.wstring())},{"ok",true}},{{"type","hero"},{"steamSuffix","_hero"},{"file",toUtf8(image.wstring())},{"ok",true}}})}});
                auto vdf=BinaryVdfDocument::emptySteamShortcuts();auto* rows=vdfFindObject(vdf.root(),"shortcuts");const auto retainedId=steamShortcutAppId(quotedSteamPath(collisionExe),"Conflict");
                rows->objectValue.push_back({"0",steamShortcutNode(retainedId,"Conflict",quotedSteamPath(retainedExe),quotedSteamPath(retainedExe.parent_path()),0)});writeAtomic(selected.target.path,vdf.serialize());
                const auto original=readBinaryFile(selected.target.path);const auto originalNode=BinaryVdfReader(original).parse()["shortcuts"]["0"];
                const auto plan=buildSteamLibraryAddPlan(selected.data,scan,selected.target,selected.planFile,{canonicalPathKey(ordinary.parent_path())},true);
                bool collisionVisible=false,collisionSelected=false;for(const auto& item:plan["items"])if(jsonStringOr(item,"status")=="shortcut-id-collision"){collisionVisible=true;collisionSelected=jsonBoolSafe(item,"selectedForAdd",true);}
                json result;std::string failure;try{result=commitSteamLibraryAddPlan(selected.data,scan,selected.target,selected.planFile,true,{canonicalPathKey(ordinary.parent_path())},true);}catch(const std::exception& e){failure=e.what();}
                writeJsonAtomic(p/L"selected-collision-observations.json",{{"visible",collisionVisible},{"selected",collisionSelected},{"planConflicts",plan["summary"]["shortcutIdConflicts"]},{"committed",jsonBoolSafe(result,"committed",false)},{"failure",failure},{"summary",plan["summary"]}});
                check(failure.empty()&&jsonBoolSafe(result,"committed",false),"Unselected shortcut collision blocked an independent selected game");
                check(collisionVisible&&!collisionSelected&&plan["summary"]["shortcutIdConflicts"]==0&&plan["summary"]["readyToAdd"]==1,"Selected plan hid another card conflict or miscounted the requested write");
                const auto after=BinaryVdfReader(readBinaryFile(selected.target.path)).parse();check(after["shortcuts"].size()==2&&after["shortcuts"]["0"]==originalNode,"Independent add rewrote prior conflicting shortcut fields");
                const auto shortcuts=readShortcuts(selected.target.path,selected.target.accountId);check(std::count_if(shortcuts.begin(),shortcuts.end(),[&](const auto& item){return canonicalPathKey(expandEnvironmentPath(unquote(item.exe)))==canonicalPathKey(ordinary);})==1,"Independent add wrote the wrong executable or duplicated it");
                const auto committedBytes=readBinaryFile(selected.target.path);
                for(const auto& selection:std::vector<std::set<std::string>>{{canonicalPathKey(collisionDir)},{canonicalPathKey(collisionDir/L"bin")},{}}){
                    bool refused=false;try{commitSteamLibraryAddPlan(selected.data,scan,selected.target,selected.planFile,true,selection,!selection.empty());}catch(const std::exception& e){refused=std::string(e.what()).find("AppID")!=std::string::npos;}
                    check(refused&&readBinaryFile(selected.target.path)==committedBytes&&!hasStages(selected.root),"Selected, alias or all-ready collision escaped original refusal");
                }
                fs::remove(manifest);mustThrow([&]{commitSteamLibraryAddPlan(selected.data,scan,selected.target,selected.planFile,true,{canonicalPathKey(collisionDir)},true);});check(readBinaryFile(selected.target.path)==committedBytes&&!hasStages(selected.root),"Explicit no-identity collision fallback changed old shortcuts");
                writeJsonAtomic(p/L"selected-collision-controls.json",{{"retainedNodeUnchanged",true},{"selectedAliasAndBulkRefused",true},{"noIdentityCollisionRefused",true},{"shortcutCount",shortcuts.size()},{"pendingJournals",pendingJournals(selected.data).size()}});
            }

            json observations=json::array();
            for(const bool commit:{false,true})for(const auto* fault:{"future-scan","future-output","write-failure"}){
                const auto root=p/(commit?L"commit":L"plan")/toWide(fault);DiscoveryProducerFixture f(root);auto state=f.catalog();state["games"]=json::array();
                const std::string kind=fault;if(kind=="future-scan")state["schemaVersion"]=99;writeJsonAtomic(f.scan,state);
                const auto output=f.data/L"state"/L"plan.json",receipt=f.data/L"state"/L"last-steam-add.json",vdf=f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf";
                const json old={{"schemaVersion",kind=="future-output"?99:1},{"mode","steam-library-add-commit"},{"committed",true},{"items",json::array()},{"keep","old successful observation"}};
                textFile(output,old.dump());textFile(receipt,"retained previous receipt");const auto originalVdf=fileText(vdf);
                if(kind=="write-failure")selftestMoveFault=[&](const auto&,const auto& to)->DWORD{return identityCallerPath(to)==identityCallerPath(output.wstring())?ERROR_WRITE_FAULT:0;};
                const auto invoke=[&](){std::vector<std::wstring> args={commit?L"--commit-add-library-to-steam":L"--plan-add-library-to-steam",L"--state",f.scan.wstring(),L"--steam-root",f.steam.wstring(),L"--account",L"19627",L"--output",output.wstring()};if(commit)args.push_back(L"--confirm-steam-closed");return f.call(args);};
                const auto code=invoke();selftestMoveFault={};
                check(code!=0,"Unconfirmed Steam add plan returned success");
                check(f.error.find("ERROR_STEAM_ADD_PLAN_UNCONFIRMED:")!=std::string::npos,"CLI omitted the actual unconfirmed-plan diagnostic");
                check(fileText(output)==old.dump()&&fileText(receipt)=="retained previous receipt"&&fileText(vdf)==originalVdf,"Unconfirmed CLI changed old result, receipt or shortcut data");
                check(f.output.find("STEAM_PLAN_RECEIPT:")==std::string::npos,"Unconfirmed plan emitted a content receipt");
                const auto rejectedOutput=f.output,rejectedError=f.error;
                state["schemaVersion"]=1;writeJsonAtomic(f.scan,state);textFile(output,R"({"schemaVersion":1,"items":[]})");
                check(invoke()==0,"Released plan publication did not recover");
                if(!commit){
                    const auto saved=readLibraryTransactionDocument(output,64u<<20);
                    const auto expected="STEAM_PLAN_RECEIPT: sha256-v1:"+custom_steam_library::plan_receipt::digest(saved)+"\n";
                    const auto first=f.output.find(expected);
                    check(first!=std::string::npos&&f.output.find("STEAM_PLAN_RECEIPT:",first+expected.size())==std::string::npos,"Actual plan-only Worker reply did not match its confirmed saved document");
                    auto later=saved;later["laterWriter"]=true;textFile(output,later.dump());
                    check(f.output.find(expected)!=std::string::npos&&custom_steam_library::plan_receipt::digest(later)!=custom_steam_library::plan_receipt::digest(saved),"Original plan receipt borrowed later cache bytes");
                }
                check(fileText(vdf)==originalVdf,"Healthy empty plan unexpectedly changed shortcut data");
                if(commit){const auto current=loadJsonDocument(receipt);check(current.value("reason",std::string{})=="nothing-ready-to-add"&&!current.value("committed",true),"Confirmed zero-item commit lost normal no-op semantics");const auto published=loadJsonDocument(output);check(jsonStringOr(published,"mode")=="steam-library-add-commit","No-op left requested output as a read-only plan");auto content=current;content.erase("resultPublicationWarnings");const auto expected="STEAM_ADD_RESULT_RECEIPT: sha256-v1:"+custom_steam_library::plan_receipt::digest(content)+"\n";check(f.output.find(expected)!=std::string::npos,"Commit CLI omitted its actual completion content receipt");}
                else check(fileText(receipt)=="retained previous receipt","Read-only plan changed the commit receipt");
                observations.push_back({{"commit",commit},{"fault",kind},{"rejectedExit",code},{"stderr",rejectedError},{"stdout",rejectedOutput},{"recoveredExit",0},{"scope","actual Worker CLI in-process; fake explicit Steam target; no actual client/network"}});
            }
            {
                Fixture f(p/L"completed-publication");
                selftestMoveFault=[&](const auto&,const auto& to)->DWORD{return to.ends_with(L"last-steam-add.json")||identityCallerPath(to)==identityCallerPath(f.planFile.wstring())?ERROR_WRITE_FAULT:0;};
                const auto result=f.commit();selftestMoveFault={};
                const auto saved=loadJsonDocument(f.data/L"state"/L"last-steam-add-19627.json");
                check(result.value("committed",false)&&readShortcuts(f.target.path,f.target.accountId).size()==1,"Completed write was lost on result publication failure");
                check(result["resultPublicationWarnings"].size()==2&&saved["resultPublicationWarnings"].size()==1,"Publication warnings did not evolve across original destinations");
                const auto digest=custom_steam_library::plan_receipt::addCompletionDigest(result);
                check(!digest.empty()&&digest==custom_steam_library::plan_receipt::addCompletionDigest(saved),"Diagnostic growth invalidated an original completed receipt");
                auto changed=saved;changed["items"][0]["status"]="ready-to-add";
                check(digest!=custom_steam_library::plan_receipt::addCompletionDigest(changed),"Completion digest omitted actual item state");
                observations.push_back({{"commit",true},{"fault","completed-result-publication"},{"committed",true},{"publicationWarnings",2},{"originalAccountReceiptWarnings",1},{"shortcuts",1}});
            }
            writeJsonAtomic(p/L"add-cli-observations.json",{{"observations",observations}});
        });
        run("discovery-producer-joined-artwork-repair-account-replay", [](const fs::path& p) {
            DiscoveryProducerFixture f(p);
            const auto restoredGame = f.games / L"Restored Game", restoredExe = restoredGame / L"bin" / L"Game.exe";
            const auto repairGame = f.games / L"Repair Game", repairExe = repairGame / L"Game.exe";
            const auto script = libLaunch::wrapperPath(restoredGame);
            textFile(restoredExe, "owned fixture executable, never run");
            textFile(repairExe, "owned fixture executable, never run");
            textFile(script, "' Owned legacy wrapper, never run\r\n");
            auto state = f.catalog();
            auto row = state["games"].at(0);
            state["games"] = json::array();
            for (const auto& [directory, primary] : {std::pair{restoredGame, restoredExe}, std::pair{repairGame, repairExe}}) {
                row["gameDirectory"] = toUtf8(directory.wstring());
                row["primaryExecutable"] = toUtf8(primary.wstring());
                state["games"].push_back(row);
            }
            writeJsonAtomic(f.scan, state);
            const auto cover = f.data / L"artwork" / L"saved-cover.jpg";
            fs::create_directories(cover.parent_path());
            writeRepairArtwork(cover, 71, true);
            writeJsonAtomic(f.data / L"config" / L"manual-overrides.json", {{"items", {{canonicalPathKey(repairExe),
                {{"cover", {{"file", toUtf8((p / L"retired-download.jpg").wstring())}, {"portableFile", toUtf8(cover.wstring())}}}}}}}});
            const auto video = f.data / L"media" / L"preview.mp4";
            textFile(video, "preview fixture untouched by static repair");

            struct Account { const char* id; int32_t stored; const char* shortId; const char* longId; };
            const std::array<Account, 2> accounts{{
                {"19627", -646717066, "3648250230", "15669115425508032512"},
                {"19628", -1522958974, "2772008322", "11905685087263391744"},
            }};
            const auto configPath = [&](const Account& account) { return f.steam / L"userdata" / toWide(account.id) / L"config"; };
            for (const auto& account : accounts) {
                const auto config = configPath(account), grid = config / L"grid";
                auto document = BinaryVdfDocument::emptySteamShortcuts();
                auto& rows = vdfFindObject(document.root(), "shortcuts")->objectValue;
                rows.push_back({"0", steamShortcutNode(-1789429860, "Restored Game", quotedSteamPath(libLaunch::systemFile(L"wscript.exe")),
                    quotedSteamPath(restoredGame), 11, {}, toUtf8(libLaunch::steamOptions(script)))});
                rows.push_back({"1", steamShortcutNode(account.stored, "Repair Game", quotedSteamPath(repairExe),
                    quotedSteamPath(repairGame), 12, {}, "--keep-original-options")});
                writeAtomic(config / L"shortcuts.vdf", document.serialize());
                fs::create_directories(grid);
                writeRepairArtwork(grid / L"2505537436p.jpg", 31, true);
                writeRepairArtwork(grid / L"2505537436_hero.jpg", 39, true);
            }
            const auto staleGrid = configPath(accounts[0]) / L"grid";
            textFile(staleGrid / L"3648250230p.jpg", "owned stale image contents");
            writeRepairArtwork(staleGrid / L"3648250230_hero.jpg", 44, true);

            json observed = json::array();
            bool verifiedStaticReceipts = true;
            for (const auto& account : accounts) {
                const auto config = configPath(account), grid = config / L"grid", vdf = config / L"shortcuts.vdf";
                const auto selected = grid / toWide(std::string(account.shortId) + "p.jpg");
                const auto plan = f.job / toWide(std::string(account.id) + "-plan.json");
                const auto receipt = f.job / toWide(std::string(account.id) + "-repair.json");
                const auto original = readBinaryFile(vdf), unaffectedCover = readBinaryFile(grid / L"2505537436p.jpg"),
                    unaffectedHero = readBinaryFile(grid / L"2505537436_hero.jpg");
                const auto inspect = [&] {
                    check(f.call({L"--plan-add-library-to-steam", L"--state", f.scan.wstring(), L"--steam-root", f.steam.wstring(),
                        L"--account", toWide(account.id), L"--select-game", repairGame.wstring(), L"--output", plan.wstring()}) == 0,
                        "Account-scoped plan rejected");
                    auto result = loadJsonDocument(plan);
                    check(result.at("accountId") == account.id && result.at("items").size() == 2, "Plan mixed account results");
                    for (const auto& item : result.at("items")) {
                        const bool repairing = item.at("gameDirectory") == toUtf8(repairGame.wstring());
                        check(item.at("status") == "already-in-steam" && item.at("primaryExecutable") == toUtf8((repairing ? repairExe : restoredExe).wstring()) &&
                            item.at("shortAppId") == (repairing ? account.shortId : "2505537436") &&
                            item.at("longAppId") == (repairing ? account.longId : "10761201346557247488"), "Wrong primary or original Steam identity after plan readback");
                    }
                    return result;
                };
                const auto before = inspect();
                check(f.call({L"--commit-refresh-steam-artwork", L"--state", f.scan.wstring(), L"--steam-root", f.steam.wstring(),
                    L"--account", toWide(account.id), L"--select-game", repairGame.wstring(), L"--confirm-steam-closed", L"--output", receipt.wstring()}) == 0,
                    "Isolated Steam artwork CLI did not complete");
                const auto result = loadJsonDocument(receipt);
                check(result.at("accountId") == account.id && result.at("artworkFilesActivated") == 1 && result.at("items").at(0).at("shortAppId") == account.shortId &&
                    f.output.find("STEAM_REFRESH_RESULT_RECEIPT: sha256-v1:" + custom_steam_library::plan_receipt::addCompletionDigest(result)) != std::string::npos,
                    "Repair did not acknowledge the verified account/card write");
                check(readBinaryFile(selected) == readBinaryFile(cover) && readBinaryFile(vdf) == original &&
                    readBinaryFile(grid / L"2505537436p.jpg") == unaffectedCover && readBinaryFile(grid / L"2505537436_hero.jpg") == unaffectedHero,
                    "Repair did not preserve original shortcut fields and untouched control artwork");
                const auto unresolved = result["items"][0].value("unresolvedRequiredArtworkSlots", json::array());
                check(unresolved == (std::string_view(account.id) == "19627" ? json::array() : json::array({"wallpaper"})),
                    "Absent background was not reported or borrowed another account's image");
                const auto& repaired = result.at("items").at(0);
                const json expectedWrites = json::array({{{"slot", "cover"},
                    {"fileName", toUtf8(selected.filename().wstring())}, {"sha256", sha256(readBinaryFile(selected))}}});
                verifiedStaticReceipts &= jsonStringOr(repaired, "longAppId") == account.longId &&
                    repaired.value("verifiedStaticArtworkWrites", json::array()) == expectedWrites;
                observed.push_back({{"beforePlan", before}, {"repair", result}, {"readbackPlan", inspect()}});
            }
            for (size_t i = 0; i < accounts.size(); ++i)
                check(!fs::exists(configPath(accounts[i]) / L"grid" / toWide(std::string(accounts[1-i].shortId) + "p.jpg")),
                    "One account's ID appeared in another account's grid");
            check(fileText(video) == "preview fixture untouched by static repair" && !hasStages(p) && pendingJournals(f.data).empty(),
                "Static repair touched dynamic media or left transaction debt");
            writeJsonAtomic(p / L"replay.json", observed);
            check(verifiedStaticReceipts, "Receipt omitted the exact card and verified portrait write or claimed an unrelated slot");
        });
        run("discovery-producer-delete-cli-refuses-unconfirmed-plan",[&](const fs::path& p){
            wchar_t mode[32]{};const auto modeSize=GetEnvironmentVariableW(L"YMCC_DELETE_RESUME_VARIANT",mode,32);const bool raceOnly=modeSize&&modeSize<32&&std::wstring(mode)==L"source-race";
            if(!raceOnly){
            json observations=json::array();
            for(const bool commit:{false,true})for(const auto* fault:{"future-scan","future-output","write-failure"}){
                const auto root=p/(commit?L"commit":L"plan")/toWide(fault);DiscoveryProducerFixture f(root);auto state=f.catalog();const auto directory=state["games"][0]["gameDirectory"].get<std::string>();
                const std::string kind=fault;if(kind=="future-scan")state["schemaVersion"]=99;writeJsonAtomic(f.scan,state);
                const auto output=f.data/L"state"/L"delete-plan.json",receipt=f.data/L"state"/L"last-steam-delete.json",vdf=f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf",other=f.root/L"Other"/L"Other.exe";
                auto doc=BinaryVdfDocument::emptySteamShortcuts();auto* shortcuts=vdfFindObject(doc.root(),"shortcuts");
                shortcuts->objectValue.push_back({"0",steamShortcutNode(-1073741823,"Selected",quotedSteamPath(f.exe),quotedSteamPath(f.exe.parent_path()),0)});
                shortcuts->objectValue.push_back({"1",steamShortcutNode(-1073741822,"Other",quotedSteamPath(other),quotedSteamPath(other.parent_path()),0)});writeAtomic(vdf,doc.serialize());
                const json old={{"schemaVersion",kind=="future-output"?99:1},{"mode","steam-library-delete-commit"},{"committed",true},{"items",json::array()},{"keep","old successful observation"}};
                textFile(output,old.dump());textFile(receipt,"retained previous receipt");const auto originalVdf=fileText(vdf);
                if(kind=="write-failure")selftestMoveFault=[&](const auto&,const auto& to)->DWORD{return identityCallerPath(to)==identityCallerPath(output.wstring())?ERROR_WRITE_FAULT:0;};
                const auto invoke=[&](){std::vector<std::wstring> args={commit?L"--commit-delete-library-from-steam":L"--plan-delete-library-from-steam",L"--state",f.scan.wstring(),L"--steam-root",f.steam.wstring(),L"--account",L"19627",L"--select-game",toWide(directory),L"--output",output.wstring()};if(commit)args.push_back(L"--confirm-steam-closed");return f.call(args);};
                const auto code=invoke();selftestMoveFault={};
                check(code!=0,"Unconfirmed Steam delete plan returned success");
                check(f.error.find("ERROR_STEAM_DELETE_PLAN_UNCONFIRMED:")!=std::string::npos,"Delete CLI omitted unconfirmed-plan diagnostic");
                check(fileText(output)==old.dump()&&fileText(receipt)=="retained previous receipt"&&fileText(vdf)==originalVdf,"Unconfirmed delete changed original results or shortcuts");
                const auto rejectedOutput=f.output,rejectedError=f.error;
                state["schemaVersion"]=1;writeJsonAtomic(f.scan,state);textFile(output,R"({"schemaVersion":1,"items":[]})");
                check(invoke()==0,"Released delete publication did not recover");
                if(commit){
                    const auto remaining=readShortcuts(vdf,"19627");check(remaining.size()==1&&canonicalPathKey(expandEnvironmentPath(unquote(remaining[0].exe)))==canonicalPathKey(other),"Confirmed delete changed an unrelated shortcut or failed to delete selected game");
                    const auto saved=loadJsonDocument(receipt);check(saved.value("committed",false),"Confirmed delete lost its success receipt");const auto retained=fileText(receipt),afterVdf=fileText(vdf);
                    check(invoke()!=0&&f.error.find("ERROR_STEAM_DELETE_PLAN_UNCONFIRMED:")==std::string::npos,"Confirmed not-found delete was accepted or mislabeled as an unconfirmed plan");
                    check(fileText(receipt)==retained&&fileText(vdf)==afterVdf,"Rejected not-found delete changed prior successful data");
                }else check(fileText(vdf)==originalVdf&&fileText(receipt)=="retained previous receipt","Read-only delete plan changed original data");
                observations.push_back({{"commit",commit},{"fault",kind},{"rejectedExit",code},{"stderr",rejectedError},{"stdout",rejectedOutput},{"recoveredExit",0},{"notFoundControl",commit},{"scope","actual Worker CLI in-process; explicit fake Steam directory only"}});
            }
            writeJsonAtomic(p/L"delete-cli-observations.json",{{"observations",observations}});
            json actions=json::array();bool safe=true;unsigned serial=0;
            for(const auto* operation:{"refresh","delete"})for(const auto* fault:{"healthy","output-fails","global-fails","both-fail"}){
                DiscoveryProducerFixture f(p/std::to_wstring(serial++));const std::string action=operation,kind=fault;auto state=f.catalog();writeJsonAtomic(f.scan,state);const auto directory=state["games"][0]["gameDirectory"].get<std::string>();
                const auto vdf=f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf",output=f.data/L"state"/L"action.json",global=f.data/L"state"/(action=="refresh"?L"last-steam-artwork-refresh.json":L"last-steam-delete.json");
                auto doc=BinaryVdfDocument::emptySteamShortcuts();vdfFindObject(doc.root(),"shortcuts")->objectValue.push_back({"0",steamShortcutNode(-1073741823,"Selected",quotedSteamPath(f.exe),quotedSteamPath(f.exe.parent_path()),0)});writeAtomic(vdf,doc.serialize());
                const auto image=f.root/L"manual.png";writeRepairArtwork(image);writeJsonAtomic(f.data/L"config"/L"manual-overrides.json",{{"items",{{canonicalPathKey(f.exe),{{"long",{{"file",toUtf8(image.wstring())}}}}}}}});
                const std::string oldOutput=json{{"schemaVersion",1},{"mode","steam-library-delete-plan"},{"items",json::array()},{"summary",json::object()},{"keep","retained requested output"}}.dump();textFile(output,oldOutput);textFile(global,"retained global result");json captured;unsigned captures=0;
                selftestMoveFault=[&](const std::wstring& from,const std::wstring& to)->DWORD{
                    const bool chosen=identityCallerPath(to)==identityCallerPath(output.wstring())||identityCallerPath(to)==identityCallerPath(global.wstring());if(!chosen)return 0;
                    auto value=loadJsonDocument(fs::path(from));if(jsonStringOr(value,"mode")!=(action=="refresh"?"steam-artwork-refresh":"steam-library-delete-commit"))return 0;
                    captured=value;++captures;const bool blocked=(identityCallerPath(to)==identityCallerPath(output.wstring())&&(kind=="output-fails"||kind=="both-fail"))||(identityCallerPath(to)==identityCallerPath(global.wstring())&&(kind=="global-fails"||kind=="both-fail"));return blocked?ERROR_WRITE_FAULT:ERROR_SUCCESS;
                };
                const int code=f.call({action=="refresh"?L"--commit-refresh-steam-artwork":L"--commit-delete-library-from-steam",L"--state",f.scan.wstring(),L"--steam-root",f.steam.wstring(),L"--account",L"19627",L"--select-game",toWide(directory),L"--confirm-steam-closed",L"--output",output.wstring()});selftestMoveFault={};
                const auto prefix=action=="refresh"?"STEAM_REFRESH_RESULT_RECEIPT: sha256-v1:":"STEAM_DELETE_RESULT_RECEIPT: sha256-v1:";const auto digest=captured.is_object()?custom_steam_library::plan_receipt::addCompletionDigest(captured):std::string{};
                const bool receipt=!digest.empty()&&f.output.find(std::string(prefix)+digest+"\n")!=std::string::npos&&f.output.find(prefix,f.output.find(prefix)+1)==std::string::npos;
                const bool completed=code==0&&captures==2&&(action=="delete"?readShortcuts(vdf,"19627").empty():captured.value("artworkFilesActivated",0)>0);
                const bool retained=kind!="both-fail"||(fileText(global)=="retained global result"&&(action=="refresh"?fileText(output)==oldOutput:jsonStringOr(loadJsonDocument(output),"mode")=="steam-library-delete-plan"));
                const bool passed=completed&&receipt&&retained;actions.push_back({{"operation",action},{"fault",kind},{"passed",passed},{"completed",completed},{"receipt",receipt},{"retainedOnBothFailures",retained},{"exitCode",code},{"captures",captures},{"stderr",f.error}});safe=safe&&passed;
            }
            writeJsonAtomic(p/L"action-cli-observed.json",{{"rows",actions}});check(safe,"Steam refresh/delete completion CLI did not bind actual result");

            }
            json resume=json::array();bool resumeSafe=true;
            for(const auto* label:{"empty","missing-vdf","missing-config","mixed","duplicate","unknown-primary","malformed-row","locked","future-scan","race"}){
                const std::string kind=label;if(raceOnly&&kind!="race")continue;DiscoveryProducerFixture f(p/toWide("resume-"+kind));auto state=f.catalog();
                const auto vdf=f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf",output=f.data/L"state"/L"resume.json",other=f.root/L"Other"/L"Other.exe";
                const auto directory=state["games"][0]["gameDirectory"].get<std::string>();auto doc=BinaryVdfDocument::emptySteamShortcuts();auto* shortcuts=vdfFindObject(doc.root(),"shortcuts");
                if(kind=="mixed"||kind=="duplicate"||kind=="race")shortcuts->objectValue.push_back({"0",steamShortcutNode(-1073741823,"Selected",quotedSteamPath(f.exe),quotedSteamPath(f.exe.parent_path()),0)});
                if(kind=="duplicate")shortcuts->objectValue.push_back({"1",steamShortcutNode(-1073741822,"Duplicate",quotedSteamPath(f.exe),quotedSteamPath(f.exe.parent_path()),0)});
                if(kind=="malformed-row")shortcuts->objectValue.push_back({"0",steamShortcutNode(-1073741823,"",quotedSteamPath(f.exe),quotedSteamPath(f.exe.parent_path()),0)});
                if(kind=="mixed"){textFile(other,"unrelated");shortcuts->objectValue.push_back({"1",steamShortcutNode(-1073741822,"Other",quotedSteamPath(other),quotedSteamPath(other.parent_path()),0)});const auto second=f.root/L"Absent"/L"Absent.exe";textFile(second,"absent selected exe");auto row=state["games"][0];row["gameDirectory"]=toUtf8(second.parent_path().wstring());row["primaryExecutable"]=toUtf8(second.wstring());state["games"].push_back(row);}
                if(kind=="unknown-primary")state["games"][0]["primaryExecutable"]="";if(kind=="future-scan")state["schemaVersion"]=99;
                writeJsonAtomic(f.scan,state);writeAtomic(vdf,doc.serialize());
                if(kind=="missing-vdf"||kind=="missing-config")fs::remove(vdf);if(kind=="missing-config")fs::remove(vdf.parent_path());
                const auto snapshot=[&](){json value=json::object();for(const auto& e:fs::recursive_directory_iterator(custom_steam_library::ioPath(f.steam,true)))value[toUtf8(e.path().lexically_relative(custom_steam_library::ioPath(f.steam,true)).wstring())]=e.is_directory()?"directory":sha256(readBinaryFile(e.path(),16u<<20));return value;};
                const auto before=snapshot();HANDLE held=INVALID_HANDLE_VALUE;if(kind=="locked"){held=CreateFileW(vdf.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Owned delete lock failed");}
                bool fired=false;std::vector<unsigned char> replacement;
                if(kind=="race"){auto next=BinaryVdfDocument::emptySteamShortcuts();vdfFindObject(next.root(),"shortcuts")->objectValue.push_back({"0",steamShortcutNode(-1073741823,"Unrelated replacement",quotedSteamPath(other),quotedSteamPath(other.parent_path()),0)});replacement=next.serialize();identityCallerCloseHook=[&](const std::wstring& file){if(!fired&&file==identityCallerPath(vdf.wstring())){fired=true;writeAtomic(vdf,replacement);}};}
                std::vector<std::wstring> args={L"--commit-delete-library-from-steam",L"--state",f.scan.wstring(),L"--steam-root",f.steam.wstring(),L"--account",L"19627",L"--select-game",toWide(directory),L"--confirm-steam-closed",L"--output",output.wstring()};
                if(kind!="race")args.push_back(L"--allow-already-absent");if(kind=="mixed"){args.push_back(L"--select-game");args.push_back(toWide(state["games"][1]["gameDirectory"].get<std::string>()));}
                const int code=f.call(args);identityCallerCloseHook={};if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
                const bool reject=kind=="duplicate"||kind=="unknown-primary"||kind=="malformed-row"||kind=="locked"||kind=="future-scan"||kind=="race";
                bool passed=reject?code!=0:code==0;
                if(kind=="race")passed=passed&&fired&&readBinaryFile(vdf,16u<<20)==replacement;
                else if(reject||kind!="mixed")passed=passed&&snapshot()==before;
                if(!reject&&code==0){const auto result=loadJsonDocument(output);passed=passed&&result.value("committed",false)&&result.value("alreadyAbsentCount",0)==1&&f.output.find("STEAM_DELETE_RESULT_RECEIPT: sha256-v1:")!=std::string::npos;if(kind=="mixed"){const auto rows=readShortcuts(vdf,"19627");passed=passed&&rows.size()==1&&canonicalPathKey(expandEnvironmentPath(unquote(rows[0].exe)))==canonicalPathKey(other);}else passed=passed&&result.value("deletedCount",-1)==0&&!result.value("shortcutsVdfModified",true);}
                resume.push_back({{"variant",kind},{"passed",passed},{"exitCode",code},{"stderr",f.error},{"raceFired",fired}});resumeSafe=resumeSafe&&passed;
            }
            writeJsonAtomic(p/L"delete-resume-observed.json",{{"rows",resume}});check(resumeSafe,"Batch deletion resume accepted unknown absence or lost a confirmed no-op");

        });
        run("discovery-producer-accounts-corrupt-nonempty-repair",[&](const fs::path& p){DiscoveryProducerFixture f(p);const std::string raw="{broken accounts";textFile(f.accounts,raw);check(f.list()==0&&f.accountAck()&&loadJsonDocument(f.accounts)["accounts"].size()==1,"Live nonempty accounts could not repair generated cache");check(producerArchivedBytes(f.data/L"backups",raw),"Corrupt account original lost");});
        run("discovery-producer-accounts-corrupt-empty-not-authority",[&](const fs::path& p){DiscoveryProducerFixture f(p);const std::string raw="{unknown previous accounts";textFile(f.accounts,raw);fs::remove(f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf");fs::remove(f.steam/L"userdata"/L"19627"/L"config");fs::remove(f.steam/L"userdata"/L"19627");check(f.list()==0&&!f.accountAck()&&fileText(f.accounts)==raw&&f.output.find("STEAM_ACCOUNTS_STATUS: unknown")!=std::string::npos,"Bad account cache became writable default empty account list");});
        run("discovery-producer-accounts-live-vdf-locked",[&](const fs::path& p){DiscoveryProducerFixture f(p);writeJsonAtomic(f.accounts,{{"schemaVersion",1},{"accounts",json::array()},{"keep","old"}});const auto raw=fileText(f.accounts);const auto target=f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf";HANDLE h=CreateFileW(target.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(h!=INVALID_HANDLE_VALUE,"Owned live VDF lock failed");const int code=f.list();CloseHandle(h);check(code==0&&!f.accountAck()&&fileText(f.accounts)==raw,"Unreadable live account became confirmed empty/default cache");check(f.list()==0&&f.accountAck(),"Live account lock release not retried");});
        run("discovery-producer-scan-observed-source-change",[&](const fs::path& p){DiscoveryProducerFixture f(p);writeJsonAtomic(f.scan,f.catalog());bool fired=false;ruleTextAfterMove=[&](const std::wstring&,const std::wstring& to){if(!fired&&(identityCallerPath(to)==identityCallerPath((f.job/L"library-scan.json").wstring())||fs::path(to).filename()==L"original.json")){fired=true;writeJsonAtomic(f.scan,{{"schemaVersion",99},{"games",json::array()},{"keep","external observed scan"}});}};const auto code=f.fresh();ruleTextAfterMove={};check(fired&&code==2&&!f.scanAck()&&f.error.find("ERROR_LIBRARY_SCAN_UNCONFIRMED:")!=std::string::npos&&loadJsonDocument(f.scan)["keep"]=="external observed scan","Unconfirmed scan CLI returned success or replaced changed source");writeJsonAtomic(f.scan,f.catalog());check(f.fresh()==0&&f.scanAck(),"Observed scan repair did not retry");});
        run("discovery-producer-job-observed-source-change",[&](const fs::path& p){DiscoveryProducerFixture f(p);writeJsonAtomic(f.scan,f.catalog());const auto job=f.job/L"library-scan.json";writeJsonAtomic(job,{{"schemaVersion",1},{"games",json::array()}});bool fired=false;ruleTextAfterMove=[&](const std::wstring&,const std::wstring& to){if(!fired&&identityCallerPath(to)==identityCallerPath(f.scan.wstring())){fired=true;writeJsonAtomic(job,{{"schemaVersion",99},{"keep","external job"}});}};const auto code=f.fresh();ruleTextAfterMove={};check(fired&&code==2&&!f.scanAck()&&f.error.find("ERROR_LIBRARY_SCAN_UNCONFIRMED:")!=std::string::npos&&f.output.find("LIBRARY_SCAN_STATE: unknown")!=std::string::npos&&loadJsonDocument(job)["keep"]=="external job","Unconfirmed scan CLI returned success or replaced changed job");check(f.output.find("LIBRARY_STATE_COMMITTED: true")!=std::string::npos,"Changed job refusal lost confirmed persistent-state publication receipt");writeJsonAtomic(job,{{"schemaVersion",1},{"games",json::array()}});check(f.fresh()==0&&f.scanAck(),"Job observation repair did not retry");});
        run("discovery-producer-accounts-observed-output-change",[&](const fs::path& p){DiscoveryProducerFixture f(p);writeJsonAtomic(f.accounts,{{"schemaVersion",1},{"accounts",json::array()}});const auto target=f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf";bool fired=false;identityCallerReadHook=[&](const std::wstring& file){if(!fired&&file==identityCallerPath(target.wstring())){fired=true;writeJsonAtomic(f.accounts,{{"schemaVersion",99},{"accounts",json::array()},{"keep","external accounts"}});}};const auto code=f.list();identityCallerReadHook={};check(fired&&code==0&&!f.accountAck()&&f.output.find("STEAM_ACCOUNTS_STATUS: unknown")!=std::string::npos&&loadJsonDocument(f.accounts)["keep"]=="external accounts","Changed account output source was overwritten/ACKed");writeJsonAtomic(f.accounts,{{"schemaVersion",1},{"accounts",json::array()}});check(f.list()==0&&f.accountAck(),"Account observation repair failed");});
        run("discovery-producer-accounts-userdata-denied",[&](const fs::path& p){DiscoveryProducerFixture f(p);writeJsonAtomic(f.accounts,listSteamAccountTargets({f.steam}));const auto raw=fileText(f.accounts);int code=-1;withConfigReadDenied(f.steam/L"userdata",[&]{code=f.list();});check(code==0&&!f.accountAck()&&fileText(f.accounts)==raw&&f.output.find("STEAM_ACCOUNTS_STATUS: unknown")!=std::string::npos,"Unreadable userdata became confirmed empty accounts");check(f.list()==0&&f.accountAck(),"Userdata ACL release failed");});
        run("discovery-producer-accounts-healthy-empty-first-observation",[&](const fs::path& p){DiscoveryProducerFixture f(p);fs::remove(f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf");fs::remove(f.steam/L"userdata"/L"19627"/L"config");fs::remove(f.steam/L"userdata"/L"19627");check(!fs::exists(f.accounts)&&f.list()==0&&f.accountAck(),"Legitimate first empty account observation was blocked");const auto report=loadJsonDocument(f.accounts);check(report["accounts"].empty()&&report["steamStatus"]=="installed-no-account","Known empty discovery fabricated account identity");});

        for(const auto* kind:{"future","duplicate","bad-type","current"})run((std::string("discovery-producer-audit-collection-")+kind).c_str(),[&,kind](const fs::path& p){
            DiscoveryProducerFixture f(p);const auto collection=f.games/L"Collection",alpha=collection/L"AlphaGame",beta=collection/L"BetaGame",ae=alpha/L"AlphaGame.exe",be=beta/L"BetaGame.exe";textFile(ae,"owned fake alpha, not launched");textFile(be,"owned fake beta, not launched");auto config=loadLibraryConfig(f.data/L"config"/L"library-config.json");config["manualPrimary"][canonicalPathKey(alpha)]={{"exe",toUtf8(ae.wstring())}};config["manualPrimary"][canonicalPathKey(beta)]={{"exe",toUtf8(be.wstring())}};
            auto prior=f.catalog();prior["games"][0]["gameDirectory"]=toUtf8(collection.wstring());prior["games"][0]["primaryExecutable"]=toUtf8(ae.wstring());const std::string k=kind;if(k=="future")prior["schemaVersion"]=99;if(k=="bad-type")prior["games"][0]["nativeSteamAppId"]="invalid";std::string bytes=prior.dump();if(k=="duplicate")bytes.insert(1,"\"schemaVersion\":99,");textFile(f.scan,bytes);
            auto report=scanLibraryRoots({f.games},{f.trainers},4,config);writeJsonAtomic(p/L"consumed.json",report);check(report["games"].size()==2&&!report["collectionContainers"].empty(),"Owned collection fixture did not reach actual split consumer");bool alias=false;for(const auto& game:report["games"])for(const auto& a:game.value("alternateGameDirectories",json::array()))if(a==toUtf8(collection.wstring()))alias=true;check(alias==(k=="current"),"Raw invalid scan granted legacy container ownership alias");check(fileText(f.scan)==bytes,"Collection reader changed scan original");
        });
        for(const auto* kind:{"future","schema-type","truncated","duplicate","container","utf8","missing","locked","write-failure","readback-change"})run((std::string("discovery-producer-audit-remove-protect-")+kind).c_str(),[&,kind](const fs::path& p){
            DiscoveryProducerFixture f(p);const std::string k=kind;const auto directory=f.exe.parent_path().parent_path();
            auto doc=f.catalog();const auto other=f.root/L"Other"/L"Other.exe";textFile(other,"unrelated install");
            auto unrelated=doc["games"][0];unrelated["gameDirectory"]=toUtf8(other.parent_path().wstring());unrelated["primaryExecutable"]=toUtf8(other.wstring());doc["games"].push_back(unrelated);
            if(k=="future")doc["schemaVersion"]=99;if(k=="schema-type")doc["schemaVersion"]="1";if(k=="container")doc["missingGames"]=json::object();
            std::string bytes=doc.dump();if(k=="truncated")bytes="{truncated scan";if(k=="duplicate")bytes.insert(1,"\"schemaVersion\":99,");if(k=="utf8")bytes=std::string("{\"unknown\":\"")+char(0xc0)+char(0xaf)+"\"}";
            if(k!="missing")textFile(f.scan,bytes);
            const auto config=f.data/L"config"/L"library-config.json",overrides=f.data/L"config"/L"manual-overrides.json";
            textFile(overrides,json{{"schemaVersion",1},{"items",{{canonicalPathKey(f.exe),{{"removeProbe","keep"}}},{canonicalPathKey(other),{{"removeProbe","unrelated"}}}}}}.dump());
            const auto saved=fileText(config),savedOverrides=fileText(overrides),install=fileText(f.exe);std::vector<fs::path> retained;
            for(const auto* section:{L"games",L"artwork",L"cache"}){const auto path=f.data/section/toWide(gameDataId(directory))/L"owned.txt";textFile(path,"retained owned metadata");retained.push_back(path);}
            HANDLE held=INVALID_HANDLE_VALUE;struct Close {HANDLE& h;~Close(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}} close{held};
            if(k=="locked"){held=CreateFileW(f.scan.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Owned removal scan lock failed");}
            if(k=="write-failure")selftestMoveFault=[&](const auto&,const auto& to)->DWORD{return identityCallerPath(to)==identityCallerPath(f.scan.wstring())?ERROR_WRITE_FAULT:0;};
            bool changed=false;std::string externalBytes;
            if(k=="readback-change")ruleTextAfterMove=[&](const auto&,const auto& to){if(!changed&&identityCallerPath(to)==identityCallerPath(f.scan.wstring())){changed=true;writeJsonAtomic(f.scan,{{"schemaVersion",99},{"keep","new external inventory"}});externalBytes=fileText(f.scan);}};
            const auto code=f.call({L"--remove-library-game",L"--game-dir",directory.wstring()});selftestMoveFault={};ruleTextAfterMove={};if(held!=INVALID_HANDLE_VALUE){CloseHandle(held);held=INVALID_HANDLE_VALUE;}
            check(code!=0,"Unconfirmed library removal returned success");
            check(f.output.find("LIBRARY_GAME_REMOVED:")==std::string::npos&&f.output.find("LIBRARY_SCAN_REMOVE_CONFIRMED: false")!=std::string::npos&&f.error.find("ERROR_LIBRARY_REMOVE_UNCONFIRMED:")!=std::string::npos,"Unconfirmed removal omitted its current failure receipt");
            if(k=="readback-change")check(changed&&fileText(f.scan)==externalBytes,"Changed inventory was overwritten");
            else check(k=="missing"?!fs::exists(f.scan):fileText(f.scan)==bytes,"Unconfirmed removal changed inventory original");
            check(fileText(config)==saved&&fileText(overrides)==savedOverrides&&fileText(f.exe)==install&&fileText(other)=="unrelated install","Unconfirmed removal changed configuration, overrides or installed games");
            for(const auto& file:retained)check(fileText(file)=="retained owned metadata","Unconfirmed removal deleted persistent metadata");
            check(!fs::exists(f.data/L"backups"/L"deleted-games"),"Unconfirmed removal entered deleted-game recovery workflow");
            doc=f.catalog();doc["games"].push_back(unrelated);writeJsonAtomic(f.scan,doc);
            check(f.call({L"--remove-library-game",L"--game-dir",directory.wstring()})==0&&f.output.find("LIBRARY_GAME_REMOVED:")!=std::string::npos,"Trusted scan repair did not unlock explicit removal");
            const auto after=loadJsonDocument(f.scan);check(after["games"].size()==1&&after["games"][0]==unrelated,"Confirmed removal changed an unrelated inventory row");
            check(fileText(f.exe)==install&&fileText(other)=="unrelated install","Confirmed library removal deleted installed game files");
            for(const auto& file:retained)check(!fs::exists(file)&&producerArchivedBytes(f.data/L"backups"/L"deleted-games","retained owned metadata"),"Confirmed removal lost original recovery data");
            check(loadManualOverrides(overrides)["items"].contains(canonicalPathKey(other)),"Confirmed removal deleted unrelated manual override");
            if(k=="missing")check(f.call({L"--restore-library-game",L"--game-dir",directory.wstring()})==0&&f.output.find("LIBRARY_GAME_RESTORED_FOR_DISCOVERY:")!=std::string::npos,"Explicit restore no longer clears original removal tombstone");
        });
        for(const auto* kind:{"lock","directory"})run((std::string("discovery-producer-audit-remove-io-")+kind).c_str(),[&,kind](const fs::path& p){
            DiscoveryProducerFixture f(p);const std::string k=kind;HANDLE held=INVALID_HANDLE_VALUE;if(k=="directory")fs::create_directories(f.scan);else{writeJsonAtomic(f.scan,f.catalog());held=CreateFileW(f.scan.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(held!=INVALID_HANDLE_VALUE,"Owned scan lock failed");}const auto config=f.data/L"config"/L"library-config.json";const auto saved=fileText(config);const int code=f.call({L"--remove-library-game",L"--game-dir",f.exe.parent_path().parent_path().wstring()});if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);check(code==0&&f.output.find("LIBRARY_GAME_REMOVED:")==std::string::npos&&fileText(config)==saved,"Scan IO fault became successful removal");if(k=="directory")fs::remove(f.scan);writeJsonAtomic(f.scan,f.catalog());check(f.call({L"--remove-library-game",L"--game-dir",f.exe.parent_path().parent_path().wstring()})==0&&f.output.find("LIBRARY_GAME_REMOVED:")!=std::string::npos,"Released scan IO did not retry");
        });
        for(const auto* kind:{"write-failure","readback-change","observed-source-change"})run((std::string("discovery-producer-audit-remove-")+kind).c_str(),[&,kind](const fs::path& p){
            DiscoveryProducerFixture f(p);writeJsonAtomic(f.scan,f.catalog());const auto raw=fileText(f.scan);const auto config=f.data/L"config"/L"library-config.json";const auto saved=fileText(config);const std::string k=kind;bool fired=false;if(k=="write-failure")selftestMoveFault=[&](const std::wstring&,const std::wstring& to)->DWORD{return identityCallerPath(to)==identityCallerPath(f.scan.wstring())?ERROR_ACCESS_DENIED:0;};else ruleTextAfterMove=[&](const std::wstring&,const std::wstring& to){const bool trigger=k=="readback-change"?identityCallerPath(to)==identityCallerPath(f.scan.wstring()):(fs::path(to).filename()==L"original.json"||identityCallerPath(to)==identityCallerPath(config.wstring()));if(!fired&&trigger){fired=true;writeJsonAtomic(f.scan,{{"schemaVersion",99},{"games",json::array()},{"keep","external change"}});}};
            const auto code=f.call({L"--remove-library-game",L"--game-dir",f.exe.parent_path().parent_path().wstring()});selftestMoveFault={};ruleTextAfterMove={};check(code==0&&f.output.find("LIBRARY_GAME_REMOVED:")==std::string::npos,"Unconfirmed removal scan write was ACKed/threw");check(fileText(config)==saved,"Failed scan submission continued destructive config phase");if(k=="write-failure")check(fs::is_regular_file(f.scan)&&fileText(f.scan)==raw,"Failed scan write deleted original");else check(fired&&loadJsonDocument(f.scan)["keep"]=="external change","Concurrent scan change overwritten/deleted");writeJsonAtomic(f.scan,f.catalog());check(f.call({L"--remove-library-game",L"--game-dir",f.exe.parent_path().parent_path().wstring()})==0&&f.output.find("LIBRARY_GAME_REMOVED:")!=std::string::npos,"Removal write repair did not retry");
        });
        run("discovery-producer-audit-remove-valid-unknown-preservation",[&](const fs::path& p){
            DiscoveryProducerFixture f(p);auto doc=f.catalog();const json opaque={{"gameDirectory",7},{"opaqueDebt",{{"owner","unknown"}}}};const auto other=f.root/L"Other";doc["games"].push_back(opaque);doc["unscannedGames"]=json::array({{{"gameDirectory",toUtf8(other.wstring())},{"opaque","manual remains"}}});doc["libraryScanRecovery"]={{"schemaVersion",1},{"requiresFreshScan",true},{"unknownPreviousInventory",true}};writeJsonAtomic(f.scan,doc);check(f.call({L"--remove-library-game",L"--game-dir",f.exe.parent_path().parent_path().wstring()})==0&&f.output.find("LIBRARY_GAME_REMOVED:")!=std::string::npos,"Valid explicit removal failed");const auto actual=loadJsonDocument(f.scan);check(actual["games"]==json::array({opaque})&&actual["unscannedGames"]==doc["unscannedGames"]&&actual["futureUnknown"]==doc["futureUnknown"]&&actual["libraryScanRecovery"]==doc["libraryScanRecovery"],"Removal persisted sanitized projection or lost unknown debt");
            json legacyRows=json::array(),metadataRows=json::array(),primaryRows=json::array();bool allCorrect=true,metadataSafe=true;
            for(const auto* mode:{"string","string-slashes","object","mixed-duplicates","object-extension"}){
                const std::string kind=mode;DiscoveryProducerFixture scenario(p/L"legacy-manual"/toWide(kind));const auto directory=scenario.root/L"StandaloneGame",exe=directory/L"StandaloneGame.exe";
                textFile(exe,"owned legacy manual game, never executed");fs::resize_file(exe,20u<<20);const auto installBefore=fs::file_size(exe);
                auto path=toUtf8(directory.wstring());if(kind=="string-slashes")std::replace(path.begin(),path.end(),'\\','/');
                const json currentObject={{"path",toUtf8(directory.wstring())},{"source","user"},{"locked",true},{"tags",{"manual-game-directory","custom-directory-tag"}},{"futureAnnotation",{{"keep",true}}}};
                const auto other=scenario.root/L"OtherManual";const json otherEntry={{"path",toUtf8(other.wstring())},{"futureOwner",{{"keep",true}}}};
                auto config=loadLibraryConfig(scenario.data/L"config"/L"library-config.json");config["manualGameDirectories"]=json::array({otherEntry});
                if(kind=="object"||kind=="object-extension")config["manualGameDirectories"].push_back(currentObject);
                else{config["manualGameDirectories"].push_back(path);if(kind=="mixed-duplicates")config["manualGameDirectories"].push_back(currentObject);}
                config["manualPrimary"][canonicalPathKey(directory)]={{"exe",toUtf8(exe.wstring())},{"locked",true},{"tags",{"custom-primary-tag"}},{"futurePrimary",{{"keep",true}}}};config["futureRoot"]={{"keep",true}};const auto configPath=scenario.data/L"config"/L"library-config.json";writeJsonAtomic(configPath,config);
                const auto metadata=scenario.data/L"games"/toWide(gameDataId(directory))/L"meta.json";textFile(metadata,"owned legacy metadata");const auto overridesPath=scenario.data/L"config"/L"manual-overrides.json";
                const json chosen={{"name","Retained manual title"},{"steamId",620},{"wallpaper",{{"path","keep-wallpaper"}}},{"futureChoice",{{"keep",true}}}};writeJsonAtomic(overridesPath,{{"schemaVersion",1},{"items",{{canonicalPathKey(exe),chosen}}}});
                const auto countGame=[&](){const auto state=loadJsonDocument(scenario.scan);return std::count_if(state["games"].begin(),state["games"].end(),[&](const auto& game){return canonicalPathKey(fs::path(toWide(jsonStringOr(game,"gameDirectory"))))==canonicalPathKey(directory)&&jsonBoolSafe(game,"manualGameDirectoryLocked",false)&&canonicalPathKey(fs::path(toWide(jsonStringOr(game,"primaryExecutable"))))==canonicalPathKey(exe);});};
                const auto initial=scenario.fresh();const bool discovered=initial==0&&scenario.scanAck()&&countGame()==1;
                const auto removed=scenario.call({L"--remove-library-game",L"--game-dir",directory.wstring()});const auto package=latestDeletedGameRecoveryPackage(scenario.data,directory);const auto manifest=loadJsonDocument(package/L"recovery-manifest.json");const auto savedDirectory=manifest.value("manualGameDirectory",json());
                const bool originSaved=savedDirectory.is_object()&&canonicalPathKey(fs::path(toWide(jsonStringOr(savedDirectory,"path"))))==canonicalPathKey(directory)&&((kind=="object"||kind=="object-extension"||kind=="mixed-duplicates")?savedDirectory==currentObject:true);
                bool foreignRejected=false;try{auto foreign=manifest;foreign["manualGameDirectory"]={{"path",toUtf8(other.wstring())}};verifyRecoveryFiles(package,foreign,directory);}catch(...){foreignRejected=true;}
                const bool removedWithoutInstall=removed==0&&scenario.output.find("LIBRARY_GAME_REMOVED:")!=std::string::npos&&fs::is_regular_file(exe)&&fs::file_size(exe)==installBefore&&configuredDirectory(loadLibraryConfig(configPath)["excludedGameDirectories"],directory);
                const auto undone=scenario.call({L"--undo-remove-library-game",L"--game-dir",directory.wstring()});const auto restored=loadLibraryConfig(configPath);
                const bool originRestored=undone==0&&configuredDirectory(restored["manualGameDirectories"],directory)&&!configuredDirectory(restored["excludedGameDirectories"],directory)&&loadManualOverrides(overridesPath)["items"][canonicalPathKey(exe)]==chosen&&fileText(metadata)=="owned legacy metadata";
                // Reintroduce the exact legacy shape to test the add writer's de-duplication.
                auto readdConfig=restored;readdConfig["manualGameDirectories"]=config["manualGameDirectories"];writeJsonAtomic(configPath,readdConfig);
                const auto readded=scenario.call({L"--add-library-game",L"--game-dir",directory.wstring()});const auto finalConfig=loadLibraryConfig(configPath);
                const auto matching=std::count_if(finalConfig["manualGameDirectories"].begin(),finalConfig["manualGameDirectories"].end(),[&](const auto& row){return configuredDirectory(json::array({row}),directory);});
                const auto rescanned=scenario.fresh();const bool stable=readded==0&&matching==1&&rescanned==0&&scenario.scanAck()&&countGame()==1;
                const bool retained=finalConfig["futureRoot"]==config["futureRoot"]&&std::find(finalConfig["manualGameDirectories"].begin(),finalConfig["manualGameDirectories"].end(),otherEntry)!=finalConfig["manualGameDirectories"].end()&&fs::file_size(exe)==installBefore;
                json retainedDirectory;for(const auto& row:finalConfig["manualGameDirectories"])if(configuredDirectory(json::array({row}),directory))retainedDirectory=row;
                const bool hadObject=kind=="object"||kind=="object-extension"||kind=="mixed-duplicates";
                const bool directoryMetadata=!hadObject||(retainedDirectory.value("futureAnnotation",json())==currentObject["futureAnnotation"]&&retainedDirectory.contains("tags")&&std::find(retainedDirectory["tags"].begin(),retainedDirectory["tags"].end(),json("custom-directory-tag"))!=retainedDirectory["tags"].end());
                metadataSafe=metadataSafe&&directoryMetadata;metadataRows.push_back({{"variant",kind},{"passed",directoryMetadata},{"directory",retainedDirectory}});
                if(kind=="object-extension"){
                    const auto alternate=directory/L"StandaloneAlternate.exe",outside=scenario.root/L"Outside"/L"Foreign.exe";textFile(alternate,"owned alternate, never executed");fs::resize_file(alternate,20u<<20);textFile(outside,"owned invalid selection, never executed");
                    const auto savedInventory=loadJsonDocument(scenario.scan);const auto wanted=canonicalPathKey(directory);const auto originalPrimary=restored["manualPrimary"][wanted];
                    for(const auto* route:{"add-executable","legacy-primary","editor-primary"}){
                        auto before=loadLibraryConfig(configPath);before["manualPrimary"][wanted]=originalPrimary;writeJsonAtomic(configPath,before);writeJsonAtomic(scenario.scan,savedInventory);
                        std::vector<std::wstring> args={std::string(route)=="add-executable"?L"--add-library-game":L"--set-library-primary",L"--game-dir",directory.wstring(),L"--exe",alternate.wstring()};
                        if(std::string(route)=="editor-primary")args.insert(args.end(),{L"--publish-primary-state",L"--expected-primary",exe.wstring()});
                        const auto code=scenario.call(args);const auto after=loadLibraryConfig(configPath),primary=after["manualPrimary"][wanted];
                        const bool owned=code==0&&canonicalPathKey(fs::path(toWide(jsonStringOr(primary,"exe"))))==canonicalPathKey(alternate)&&jsonStringOr(primary,"source")=="user"&&jsonBoolSafe(primary,"locked",false)&&jsonBoolSafe(primary,"protectedFromAutomaticOverwrite",false);
                        const bool preserved=primary.value("futurePrimary",json())==originalPrimary["futurePrimary"]&&primary.contains("tags")&&std::find(primary["tags"].begin(),primary["tags"].end(),json("custom-primary-tag"))!=primary["tags"].end()&&std::find(primary["tags"].begin(),primary["tags"].end(),json("manual-primary"))!=primary["tags"].end()&&std::find(primary["tags"].begin(),primary["tags"].end(),json("locked"))!=primary["tags"].end();
                        const auto acceptedBytes=fileText(configPath);auto invalid=args;for(size_t i=0;i+1<invalid.size();++i)if(invalid[i]==L"--exe")invalid[i+1]=outside.wstring();
                        const auto bad=scenario.call(invalid);const bool rejected=bad!=0&&fileText(configPath)==acceptedBytes;
                        const bool published=std::string(route)!="editor-primary"||canonicalPathKey(fs::path(toWide(jsonStringOr(loadJsonDocument(scenario.scan)["games"][0],"primaryExecutable"))))==canonicalPathKey(alternate);
                        const bool safe=owned&&preserved&&rejected&&published;metadataSafe=metadataSafe&&safe;primaryRows.push_back({{"route",route},{"exitCode",code},{"ownedFieldsUpdated",owned},{"metadataPreserved",preserved},{"invalidOutsideDirectoryRejected",rejected},{"targetedInventoryPublished",published},{"primary",primary},{"passed",safe}});
                    }
                }
                const bool passed=discovered&&originSaved&&foreignRejected&&removedWithoutInstall&&originRestored&&stable&&retained;allCorrect=allCorrect&&passed;
                legacyRows.push_back({{"variant",kind},{"initialExitCode",initial},{"discoveredExactlyOnce",discovered},{"recoveryOriginSaved",originSaved},{"savedDirectory",savedDirectory},{"foreignRecoveryRejected",foreignRejected},{"removedWithoutInstallChange",removedWithoutInstall},{"undoExitCode",undone},{"originAndDataRestored",originRestored},{"matchingAfterReadd",matching},{"stableAfterReaddAndScan",stable},{"unrelatedAndInstallRetained",retained},{"passed",passed}});
            }
            writeJsonAtomic(p/L"legacy-manual-observed.json",legacyRows);
            check(legacyRows.size()==5&&allCorrect,"Legacy manual directory lost scan, recovery origin or add uniqueness");
            writeJsonAtomic(p/L"manual-record-metadata-observed.json",{{"directories",metadataRows},{"primaryRoutes",primaryRows}});
            check(metadataRows.size()==5&&primaryRows.size()==3&&metadataSafe,"Manual selection discarded retained directory or primary metadata");
            json nestedRows=json::array();bool nestedSafe=true;
            for(const auto* mode:{"manual-object","manual-string","primary-only","active-inventory","missing-inventory","unscanned-inventory","unregistered-subdirectory","similar-prefix"}){
                const std::string kind=mode;const bool independent=kind!="unregistered-subdirectory"&&kind!="similar-prefix";
                DiscoveryProducerFixture scenario(p/L"nested-removal"/toWide(kind));const auto directory=scenario.exe.parent_path().parent_path();
                const auto child=directory/L"ChildGame",childExe=(kind=="similar-prefix"?directory/L"ChildGameCopy":child)/L"ChildGame.exe",ownAlternate=directory/L"bin"/L"Alternate.exe",unrelated=scenario.root/L"OtherGame"/L"Other.exe";
                textFile(childExe,"owned independent install, never executed");textFile(ownAlternate,"owned selected alternate, never executed");textFile(unrelated,"owned unrelated install, never executed");
                auto state=scenario.catalog();const json childRow={{"gameDirectory",toUtf8(child.wstring())},{"primaryExecutable",toUtf8((child/L"ChildGame.exe").wstring())},{"status","ready"},{"ownerNote","independent child"}};
                if(kind=="active-inventory")state["games"].push_back(childRow);
                if(kind=="missing-inventory")state["missingGames"].push_back(childRow);
                if(kind=="unscanned-inventory")state["unscannedGames"]=json::array({childRow});
                writeJsonAtomic(scenario.scan,state);auto config=loadLibraryConfig(scenario.data/L"config"/L"library-config.json");
                config["manualGameDirectories"].push_back({{"path",toUtf8(directory.wstring())},{"source","user"}});
                if(kind=="manual-object"||kind=="similar-prefix")config["manualGameDirectories"].push_back({{"path",toUtf8(child.wstring())},{"source","user"}});
                if(kind=="manual-string")config["manualGameDirectories"].push_back(toUtf8(child.wstring()));
                config["manualPrimary"][canonicalPathKey(directory)]={{"exe",toUtf8(scenario.exe.wstring())}};
                if(kind=="primary-only")config["manualPrimary"][canonicalPathKey(child)]={{"exe",toUtf8(childExe.wstring())}};
                const auto configPath=scenario.data/L"config"/L"library-config.json",overridesPath=scenario.data/L"config"/L"manual-overrides.json";writeJsonAtomic(configPath,config);
                const json parentChoice={{"name","Parent chosen name"},{"wallpaper",{{"path","keep-parent"}}}},childChoice={{"name","Child chosen name"},{"cover",{{"path","keep-child-cover"}}},{"future",{{"keep",true}}}},alternateChoice={{"name","Parent alternate"}},unrelatedChoice={{"name","Unrelated chosen name"}};
                const auto parentKey=canonicalPathKey(scenario.exe),childKey=canonicalPathKey(childExe),alternateKey=canonicalPathKey(ownAlternate),otherKey=canonicalPathKey(unrelated);
                writeJsonAtomic(overridesPath,{{"schemaVersion",1},{"items",{{parentKey,parentChoice},{childKey,childChoice},{alternateKey,alternateChoice},{otherKey,unrelatedChoice}}}});
                const auto parentMeta=scenario.data/L"games"/toWide(gameDataId(directory))/L"meta.json",childMeta=scenario.data/L"games"/toWide(gameDataId(child))/L"meta.json";
                textFile(parentMeta,"selected parent metadata");textFile(childMeta,"retained child metadata");
                const auto removed=scenario.call({L"--remove-library-game",L"--game-dir",directory.wstring()});const auto current=loadManualOverrides(overridesPath);const auto afterState=loadJsonDocument(scenario.scan);
                const bool removedOnlyOwn=removed==0&&!current["items"].contains(parentKey)&&!current["items"].contains(alternateKey)&&current["items"][otherKey]==unrelatedChoice&&
                    (independent?(current["items"].contains(childKey)&&current["items"][childKey]==childChoice):!current["items"].contains(childKey));
                const bool childInventoryRetained=(kind=="active-inventory"?afterState["games"].size()==1&&afterState["games"][0]==childRow:
                    kind=="missing-inventory"?afterState["missingGames"]==state["missingGames"]:kind=="unscanned-inventory"?afterState["unscannedGames"]==state["unscannedGames"]:true);
                const auto package=latestDeletedGameRecoveryPackage(scenario.data,directory);const auto manifestPath=package/L"recovery-manifest.json";auto manifest=loadJsonDocument(manifestPath);
                const bool archiveOwnsCorrectly=manifest["manualOverrides"].contains(parentKey)&&manifest["manualOverrides"].contains(alternateKey)&&
                    manifest["manualOverrides"].contains(childKey)==!independent&&!manifest["manualOverrides"].contains(otherKey);
                // Legacy packages could already contain descendant overrides.
                // Their valid same-parent data must not resurrect a now independent child.
                manifest["manualOverrides"][childKey]=childChoice;writeJsonAtomic(manifestPath,manifest);auto beforeUndo=current;beforeUndo["items"].erase(childKey);writeJsonAtomic(overridesPath,beforeUndo);
                const auto undone=scenario.call({L"--undo-remove-library-game",L"--game-dir",directory.wstring()});const auto restored=loadManualOverrides(overridesPath);
                const bool undoOwnsCorrectly=undone==0&&restored["items"][parentKey]==parentChoice&&restored["items"][alternateKey]==alternateChoice&&restored["items"][otherKey]==unrelatedChoice&&
                    (independent?!restored["items"].contains(childKey):restored["items"][childKey]==childChoice);
                const bool filesRetained=fileText(scenario.exe)=="owned fake exe, never launched"&&fileText(childExe)=="owned independent install, never executed"&&fileText(unrelated)=="owned unrelated install, never executed"&&fileText(childMeta)=="retained child metadata"&&fileText(parentMeta)=="selected parent metadata";
                const bool passed=removedOnlyOwn&&childInventoryRetained&&archiveOwnsCorrectly&&undoOwnsCorrectly&&filesRetained;nestedSafe=nestedSafe&&passed;
                nestedRows.push_back({{"variant",kind},{"independent",independent},{"removeExitCode",removed},{"removedOnlyOwnOverrides",removedOnlyOwn},{"childInventoryRetained",childInventoryRetained},{"archiveOwnsCorrectly",archiveOwnsCorrectly},{"undoExitCode",undone},{"legacyUndoOwnsCorrectly",undoOwnsCorrectly},{"installationAndMetadataRetained",filesRetained},{"passed",passed}});
            }
            writeJsonAtomic(p/L"nested-removal-observed.json",nestedRows);
            check(nestedRows.size()==8&&nestedSafe,"Parent removal or undo changed an independent nested game's overrides");


        });

        for(const auto* kind:{"add-inactive","delete-inactive","add-plan-write-failure","healthy-noop"})run((std::string("discovery-producer-audit-commit-")+kind).c_str(),[&,kind](const fs::path& p){
            DiscoveryProducerFixture f(p);const std::string k=kind;auto state=f.catalog();state["games"]=json::array();if(k=="add-inactive"||k=="delete-inactive")state["schemaVersion"]=99;writeJsonAtomic(f.scan,state);const auto plan=f.data/L"state"/L"plan.json",receipt=f.data/L"state"/L"last-steam-add.json";writeJsonAtomic(receipt,{{"schemaVersion",99},{"keep","unknown prior commit"}});const auto old=fileText(receipt),vdf=fileText(f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf");if(k=="add-plan-write-failure")selftestMoveFault=[&](const std::wstring&,const std::wstring& to)->DWORD{return identityCallerPath(to)==identityCallerPath(plan.wstring())?ERROR_ACCESS_DENIED:0;};json result;bool threw=false;try{const SteamShortcutTarget target{f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf","19627"};result=k=="delete-inactive"?commitSteamLibraryDeletePlan(f.data,f.scan,target,plan,true,{canonicalPathKey(f.exe.parent_path().parent_path())}):commitSteamLibraryAddPlan(f.data,f.scan,target,plan,true);}catch(...){threw=true;}selftestMoveFault={};check(!threw,"Inactive/unconfirmed plan escaped as commit exception");check(!result.value("committed",false)&&fileText(f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf")==vdf,"Inactive/empty plan wrote fake Steam data");if(k=="healthy-noop")check(result.value("reason",std::string{})=="nothing-ready-to-add"&&loadJsonDocument(receipt).value("committed",true)==false,"Confirmed healthy zero-item plan lost compatibility");else check(fileText(receipt)==old&&!result.value("planWriteConfirmed",false)&&result["discoveryState"]["active"]==false,"Inactive/unconfirmed plan entered commit and rewrote prior receipt");
        });

        for (const auto* variant : {"add", "update-vbs", "delete"})
            run((std::string("discovery-producer-audit-commit-scan-race-") + variant).c_str(), [variant](const fs::path& p) {
                const std::string_view operation = variant;
                const bool adding = operation == "add", deleting = operation == "delete";
                ArtworkRepairFixture f(p, !adding);
                const auto game = p / L"Game", primary = game / L"Game.exe", scan = fixtureScanState(f);
                auto inventory = loadJsonDocument(scan);
                inventory["games"][0]["status"] = "ready";
                writeJsonAtomic(scan, inventory);
                auto newer = inventory;
                const auto other = game / L"Other.exe";
                textFile(other, "replacement primary, never launched");
                newer["games"][0]["primaryExecutable"] = toUtf8(other.wstring());
                const auto script = libLaunch::wrapperPath(game), bindingRoot = libLaunch::bindingRoot(f.data);
                if (!adding && !deleting)
                    writeJsonAtomic(f.data / L"config" / L"manual-overrides.json", {{"items", {{canonicalPathKey(primary),
                        {{"launchConfiguration", {{"mode", "auto"}}}}}}}});
                const auto gridFile = f.target.path.parent_path() / L"grid" / L"3221225473p.png";
                fs::create_directories(gridFile.parent_path());
                writeRepairArtwork(gridFile);
                const auto artwork = readBinaryFile(gridFile);
                const auto original = adding ? std::vector<unsigned char>{} : readBinaryFile(f.target.path);
                const auto receipt = f.data / L"state" / (deleting ? L"last-steam-delete.json" : L"last-steam-add.json");
                textFile(receipt, "previous receipt");
                const std::set<std::string> selection{canonicalPathKey(game)};
                const auto commit = [&] {
                    if (deleting) return commitSteamLibraryDeletePlan(f.data, scan, f.target, f.planFile, true, selection);
                    return commitSteamLibraryAddPlan(f.data, scan, f.target, f.planFile, true, selection, true, !adding);
                };
                bool replaced = false;
                ruleTextAfterMove = [&](const auto&, const auto& destination) {
                    const bool boundary = deleting ? fs::path(destination).filename() == L"transaction.pending.json" :
                        identityCallerPath(destination) == identityCallerPath(f.planFile.wstring());
                    if (!replaced && boundary) {
                        replaced = true;
                        writeJsonAtomic(scan, newer);
                    }
                };
                std::string failure;
                json result;
                try { result = commit(); } catch (const std::exception& error) { failure = error.what(); }
                ruleTextAfterMove = {};
                const bool originalKept = adding ? !fs::exists(f.target.path) : readBinaryFile(f.target.path) == original;
                const bool launchKept = !fs::exists(script) && !fs::exists(bindingRoot);
                const bool clean = pendingJournals(f.data).empty() && !hasStages(p);
                writeJsonAtomic(p / L"observed.json", {{"injected", replaced}, {"committed", result.is_object() && result.value("committed", false)},
                    {"error", failure}, {"originalKept", originalKept}, {"launchKept", launchKept}, {"clean", clean}});
                check(replaced && loadJsonDocument(scan) == newer, "Scan replacement actor did not run at the requested boundary");
                check(failure.find("Library scan changed before Steam commit") != std::string::npos,
                    "Stale inventory did not cause a specific Steam commit refusal");
                check(originalKept && launchKept && clean && fileText(receipt) == "previous receipt" && readBinaryFile(gridFile) == artwork,
                    "Rejected commit changed card, launch files, artwork or prior receipt, or left pending work");

                writeJsonAtomic(scan, inventory);
                const auto retried = commit();
                check(retried.value("committed", false), "Valid current inventory did not allow retry");
                const auto rows = readShortcuts(f.target.path, f.target.accountId);
                if (deleting) check(rows.empty(), "Retry did not delete the original card");
                else if (adding) check(rows.size() == 1 && rows[0].exe == quotedSteamPath(primary) && !fs::exists(script),
                    "Retry did not add a single ordinary shortcut by default");
                else check(rows.size() == 1 && rows[0].storedAppId == -1073741823 &&
                    libLaunch::steamScriptOperand(toWide(rows[0].exe), toWide(rows[0].launchOptions)) == script,
                    "Retry did not update the original card in place");
                check(pendingJournals(f.data).empty() && !hasStages(p), "Retry left pending work");
            });

        auto primaryFixture=[&](const fs::path& p,const std::function<void(DiscoveryProducerFixture&,const fs::path&,const fs::path&)>& fn){
            DiscoveryProducerFixture f(p);const auto dir=f.exe.parent_path().parent_path(),next=dir/L"Alternate.exe";textFile(next,"owned alternate, never launched");auto catalog=f.catalog();catalog["games"][0]["ownerNote"]="keep";catalog["games"].push_back({{"gameDirectory",toUtf8((p/L"OtherGame").wstring())},{"primaryExecutable",toUtf8((p/L"OtherGame"/L"Other.exe").wstring())},{"status","ready"},{"ownerNote","unrelated"}});writeJsonAtomic(f.scan,catalog);fn(f,dir,next);
        };
        auto primaryCall=[&](DiscoveryProducerFixture& f,const fs::path& dir,const fs::path& next,const fs::path& expected){return f.call({L"--set-library-primary",L"--game-dir",dir.wstring(),L"--exe",next.wstring(),L"--publish-primary-state",L"--expected-primary",expected.wstring()});};
        run("discovery-producer-primary-publishes-exact-game-before-dependent-write",[&](const fs::path& p){primaryFixture(p,[&](auto& f,const auto& dir,const auto& next){
            bootstrapPeIdentityBoundary(p);
            {
            // Reproduce the actual Win5 crash helpers without executing them.
            const auto crashField=json::parse(R"FIELD([{"file":"CrashLogsGenerator.exe","version":{"FileDescription":"Crash Logs Generator","InternalName":"CrashLogsGenerator.dll","LegalCopyright":"Copyright (C) 2021 Activision Publishing. Inc.","OriginalFilename":"CrashLogsGenerator.dll","ProductName":"Call of Duty®"},"directory":"Call of Duty HQ","oldRole":"main-game","oldSource":"pe-product-name"},{"file":"codCrashHandler.exe","version":{"CompanyName":"Activision","FileDescription":"Call of Duty� External Crash Handler","InternalName":"codCrashHandler.exe","LegalCopyright":"� 2026 Activision Publishing, Inc.","OriginalFilename":"codCrashHandler.exe","ProductName":"External Crash Handler"},"directory":"Call of Duty HQ","oldRole":"main-game","oldSource":"pe-product-name"},{"file":"codCrashHandler.exe","version":{"CompanyName":"Activision","FileDescription":"Call of Duty� External Crash Handler","InternalName":"codCrashHandler.exe","LegalCopyright":"� 2026 Activision Publishing, Inc.","OriginalFilename":"codCrashHandler.exe","ProductName":"External Crash Handler"},"directory":"Call of Duty HQ","oldRole":"main-game","oldSource":"pe-product-name"}])FIELD");
            json crashRows=json::array();bool crashSafe=true;
            for(const auto& row:crashField){
                const auto exe=p/L"crash-field"/toWide(jsonStringOr(row,"file"));
                const auto version=row["version"].get<std::map<std::string,std::string>>();
                const auto value=assessPeIdentity(version,classifyLibraryExecutable(exe),jsonStringOr(row,"directory"));
                const bool passed=value.role=="crash-handler"&&value.primaryEvidenceSource=="pe-tool-classification";
                crashRows.push_back({{"field",row},{"role",value.role},{"source",value.primaryEvidenceSource},{"confidence",value.identityConfidence},{"passed",passed}});crashSafe=crashSafe&&passed;
            }
            const auto describedCrash=assessPeIdentity({{"ProductName","Owned Game"},{"FileDescription","Owned Game"}},"crash-handler","Owned Game");
            const bool descriptionSafe=describedCrash.role=="crash-handler"&&describedCrash.primaryEvidenceSource=="pe-tool-classification";
            json crashScans=json::array();bool scanSafe=true,manualSafe=true;
            for(const auto* filename:{L"CrashLogsGenerator.exe",L"codCrashHandler.exe"}){
                const auto directory=p/L"crash-only"/fs::path(filename).stem(),exe=directory/filename;
                textFile(exe,"owned inert crash utility, never executed");fs::resize_file(exe,20u*1024u*1024u);
                const auto automatic=scanOneGameDirectory(directory,1,defaultLibraryConfig(),{});
                const bool automaticSafe=jsonStringOr(automatic,"primaryExecutable").empty()&&jsonStringOr(automatic,"status")=="unrecognized-tool"&&!automatic["pidCoordination"].value("eligibleForGameTarget",true)&&automatic["pidCoordination"]["suggestedWhitelist"].empty()&&jsonStringOr(automatic["candidates"][0],"role")=="crash-handler";
                auto manualConfig=defaultLibraryConfig();manualConfig["manualPrimary"][canonicalPathKey(directory)]={{"exe",toUtf8(exe.wstring())}};
                const auto manual=scanOneGameDirectory(directory,1,manualConfig,{});
                const bool manualRetained=manual.value("selectionLocked",false)&&jsonStringOr(manual,"primaryExecutable")==toUtf8(exe.wstring())&&jsonStringOr(manual,"selectionSource")=="manual";
                crashScans.push_back({{"file",toUtf8(std::wstring(filename))},{"automatic",automatic},{"automaticSafe",automaticSafe},{"manual",manual},{"manualRetained",manualRetained}});scanSafe=scanSafe&&automaticSafe;manualSafe=manualSafe&&manualRetained;
            }
            json crashControls=json::array();bool normalSafe=true;
            for(const auto* title:{"Crash Bandicoot 4","Crashlands","Crash Handler Adventure","CrashLogsGenerator Game"}){
                const auto exe=p/L"normal-crash-titles"/toWide(title)/(toWide(title)+L".exe");
                const auto value=assessPeIdentity({{"ProductName",title},{"FileDescription",title}},classifyLibraryExecutable(exe),title);
                const bool passed=value.role=="main-game"&&value.primaryEvidenceSource=="pe-product-name";
                crashControls.push_back({{"title",title},{"role",value.role},{"source",value.primaryEvidenceSource},{"passed",passed}});normalSafe=normalSafe&&passed;
            }
            const auto helperDescription=assessPeIdentity({{"ProductName","Application"},{"FileDescription","Hearts of Iron IV"}},"helper");
            normalSafe=normalSafe&&helperDescription.role=="main-game"&&helperDescription.primaryEvidenceSource=="pe-file-description";
            writeJsonAtomic(p/L"crash-helper-observations.json",{{"field",crashRows},{"describedCrashRole",describedCrash.role},{"descriptionSafe",descriptionSafe},{"scans",crashScans},{"controls",crashControls},{"manualSafe",manualSafe},{"normalSafe",normalSafe},{"crashSafe",crashSafe},{"scanSafe",scanSafe}});
            check(manualSafe&&normalSafe,"Crash helper exclusion changed ordinary titles or explicit manual selection");
            check(crashSafe&&descriptionSafe&&scanSafe,"Crash helper executables became automatic game identities");
            }
            // A capped candidate set cannot prove an automatic primary.
            const auto cappedDirectory=p/L"cap-check"/L"CapAdventure";
            for(unsigned i=0;i<251;++i){const auto exe=cappedDirectory/toWide("part-"+std::to_string(i))/L"CapAdventure.exe";textFile(exe,"owned inert marker, never executed");fs::resize_file(exe,6u*1024u*1024u);}
            const auto capped=scanOneGameDirectory(cappedDirectory,2,defaultLibraryConfig(),{});
            const auto selectedPath=jsonStringOr(capped,"primaryExecutable");check(!selectedPath.empty(),"Cap scenario did not retain a primary candidate");
            auto cappedConfig=defaultLibraryConfig();cappedConfig["manualPrimary"][canonicalPathKey(cappedDirectory)]={{"exe",selectedPath}};
            const auto manualCap=scanOneGameDirectory(cappedDirectory,2,cappedConfig,{});
            auto legacyCap=capped;legacyCap["scanComplete"]=true;legacyCap["status"]="ready";legacyCap["pidCoordination"]["eligibleForGameTarget"]=true;legacyCap["pidCoordination"]["whitelistSuggested"]=true;legacyCap["pidCoordination"]["suggestedWhitelist"]=json::array({"capadventure"});
            const auto cachedCap=validateLibraryScanState({{"games",json::array({legacyCap})}})["games"][0];
            const bool automaticCapSafe=capped.value("candidateLimitReached",false)&&capped.value("executableCount",0)==251&&!capped.value("scanComplete",true)&&jsonStringOr(capped,"status")=="scan-incomplete-needs-review"&&!capped["pidCoordination"].value("eligibleForGameTarget",true)&&capped["pidCoordination"]["suggestedWhitelist"].empty();
            const bool manualCapSafe=manualCap.value("candidateLimitReached",false)&&jsonStringOr(manualCap,"status")=="ready"&&manualCap.value("selectionLocked",false)&&jsonStringOr(manualCap,"primaryExecutable")==selectedPath&&manualCap["pidCoordination"].value("eligibleForGameTarget",false);
            const bool cachedCapSafe=!cachedCap.value("scanComplete",true)&&jsonStringOr(cachedCap,"status")=="scan-incomplete-needs-review"&&!cachedCap["pidCoordination"].value("eligibleForGameTarget",true)&&cachedCap["pidCoordination"]["suggestedWhitelist"].empty();
            writeJsonAtomic(p/L"candidate-cap-observations.json",{{"automatic",capped},{"manual",manualCap},{"cached",cachedCap},{"automaticSafe",automaticCapSafe},{"manualSafe",manualCapSafe},{"cachedSafe",cachedCapSafe}});
            check(manualCapSafe,"Candidate cap changed explicit manual primary choice");
            check(automaticCapSafe&&cachedCapSafe,"Candidate cap advertised a complete automatic scan");
            // The actual iterator must finalize scan status before PID metadata.
            const auto incompleteDirectory=p/L"scan-completeness"/L"OwnedAdventure",incompleteExe=incompleteDirectory/L"OwnedAdventure.exe",deniedDirectory=incompleteDirectory/L"zzClosed";
            textFile(incompleteExe,"owned inert executable, never launched");fs::resize_file(incompleteExe,6u*1024u*1024u);
            textFile(deniedDirectory/L"marker.txt","owned directory read-denial marker");
            const auto completeBefore=scanOneGameDirectory(incompleteDirectory,3,defaultLibraryConfig(),{});
            auto incompleteManualConfig=defaultLibraryConfig();incompleteManualConfig["manualPrimary"][canonicalPathKey(incompleteDirectory)]={{"exe",toUtf8(incompleteExe.wstring())}};
            json incompleteAutomatic,incompleteManual;bool actualDirectoryDenied=false;
            withConfigReadDenied(deniedDirectory,[&]{
                WIN32_FIND_DATAW found{};const auto h=FindFirstFileW((deniedDirectory/L"*").c_str(),&found);const auto error=GetLastError();
                if(h!=INVALID_HANDLE_VALUE)FindClose(h);actualDirectoryDenied=h==INVALID_HANDLE_VALUE&&error==ERROR_ACCESS_DENIED;
                check(actualDirectoryDenied,"Owned directory ACL did not deny actual enumeration");
                incompleteAutomatic=scanOneGameDirectory(incompleteDirectory,3,defaultLibraryConfig(),{});
                incompleteManual=scanOneGameDirectory(incompleteDirectory,3,incompleteManualConfig,{});
            });
            const auto completeAfter=scanOneGameDirectory(incompleteDirectory,3,defaultLibraryConfig(),{});
            const auto completeSafe=[&](const json& value){return value.value("scanComplete",false)&&jsonStringOr(value,"status")=="ready"&&jsonStringOr(value,"primaryExecutable")==toUtf8(incompleteExe.wstring())&&value["pidCoordination"].value("eligibleForGameTarget",false)&&value["pidCoordination"].value("whitelistSuggested",false)&&value["pidCoordination"]["suggestedWhitelist"].size()==1;};
            bool candidateSafe=false;unsigned selectedCandidates=0;
            for(const auto& candidate:incompleteAutomatic["candidates"])if(candidate.value("selected",false)){
                ++selectedCandidates;const auto& pid=candidate["pidCoordination"];const auto tags=pid["tags"].get<std::vector<std::string>>();
                candidateSafe=!pid.value("eligibleForGameTarget",true)&&!pid.value("whitelistSuggested",true)&&std::find(tags.begin(),tags.end(),"game")==tags.end()&&std::find(tags.begin(),tags.end(),"pid-whitelist-candidate")==tags.end();
            }
            const auto& incompletePid=incompleteAutomatic["pidCoordination"];const auto incompleteTags=incompletePid["tags"].get<std::vector<std::string>>();
            const bool automaticSafe=!incompleteAutomatic.value("scanComplete",true)&&jsonStringOr(incompleteAutomatic,"status")=="scan-incomplete-needs-review"&&jsonStringOr(incompleteAutomatic,"primaryExecutable")==toUtf8(incompleteExe.wstring())&&!incompletePid.value("eligibleForGameTarget",true)&&!incompletePid.value("whitelistSuggested",true)&&incompletePid["suggestedWhitelist"].empty()&&std::find(incompleteTags.begin(),incompleteTags.end(),"pid-target-ineligible")!=incompleteTags.end()&&std::find(incompleteTags.begin(),incompleteTags.end(),"game")==incompleteTags.end()&&std::find(incompleteTags.begin(),incompleteTags.end(),"pid-whitelist-candidate")==incompleteTags.end()&&selectedCandidates==1&&candidateSafe;
            const bool manualSafe=!incompleteManual.value("scanComplete",true)&&jsonStringOr(incompleteManual,"status")=="ready"&&incompleteManual.value("selectionLocked",false)&&jsonStringOr(incompleteManual,"selectionSource")=="manual"&&jsonStringOr(incompleteManual,"primaryExecutable")==toUtf8(incompleteExe.wstring())&&incompleteManual["pidCoordination"].value("eligibleForGameTarget",false)&&incompleteManual["pidCoordination"]["suggestedWhitelist"].size()==1;
            writeJsonAtomic(p/L"scan-completeness-observations.json",{{"actualDirectoryDenied",actualDirectoryDenied},{"automatic",incompleteAutomatic},{"manual",incompleteManual},{"completeBefore",completeBefore},{"completeAfter",completeAfter},{"automaticSafe",automaticSafe},{"manualSafe",manualSafe},{"completeBeforeSafe",completeSafe(completeBefore)},{"completeAfterSafe",completeSafe(completeAfter)},{"selectedCandidates",selectedCandidates},{"candidateSafe",candidateSafe}});
            check(completeSafe(completeBefore)&&completeSafe(completeAfter)&&manualSafe,"Scan completeness repair changed complete/manual choices or ACL recovery");
            check(automaticSafe,"Incomplete automatic scan advertised a PID whitelist target");
            // Old cache views must not retain authority after status validation.
            json cacheRows=json::array();bool cacheSafe=true;
            for(const auto* variant:{"incomplete-ready","incomplete-review","outside-primary","manual-incomplete","complete"}){
                const std::string mode=variant;auto legacy=completeBefore;
                legacy["futureGame"]={{"keep",true}};legacy["pidCoordination"]["futurePid"]={{"keep",true}};legacy["pidCoordination"]["tags"].push_back("custom-pid-tag");
                for(auto& candidate:legacy["candidates"]){candidate["futureCandidate"]=42;candidate["pidCoordination"]["tags"].push_back("custom-candidate-tag");}
                if(mode!="complete")legacy["scanComplete"]=false;
                if(mode=="incomplete-review")legacy["status"]="scan-incomplete-needs-review";
                if(mode=="outside-primary")legacy["primaryExecutable"]=toUtf8((p/L"Unrelated"/L"Other.exe").wstring());
                if(mode=="manual-incomplete"){legacy["selectionSource"]="manual";legacy["selectionLocked"]=true;}
                const auto stateFile=p/L"cache-views"/toWide(mode)/L"library-scan.json",outputFile=stateFile.parent_path()/L"add-plan.json";
                const json original={{"schemaVersion",1},{"games",json::array({legacy})},{"futureTop",{{"keep",true}}}};writeJsonAtomic(stateFile,original);const auto originalBytes=readBinaryFile(stateFile);
                LibraryScanObservation observation;const auto state=loadLibraryScanState(stateFile,&observation);const auto& row=state["games"][0];
                const SteamShortcutTarget target{f.steam/L"userdata"/L"19627"/L"config"/L"shortcuts.vdf","19627"};const auto shortcutBytes=readBinaryFile(target.path);
                const bool quarantined=mode!="manual-incomplete"&&mode!="complete";
                json addPlan=json::object();bool addGate=true;
                if(quarantined){addPlan=buildSteamLibraryAddPlan(f.data,stateFile,target,outputFile);addGate=addPlan["summary"].value("readyToAdd",size_t{1})==0&&addPlan["items"].size()==1&&jsonStringOr(addPlan["items"][0],"primarySelectionStatus")!= "ready";}
                bool suggestionsSafe=true;
                if(quarantined){
                    const auto& pid=row["pidCoordination"];suggestionsSafe=!pid.value("eligibleForGameTarget",true)&&!pid.value("whitelistSuggested",true)&&pid["suggestedWhitelist"].empty();
                    for(const auto& tag:pid["tags"])if(tag=="game"||tag=="pid-whitelist-candidate")suggestionsSafe=false;
                    for(const auto& candidate:row["candidates"]){const auto& cp=candidate["pidCoordination"];suggestionsSafe=suggestionsSafe&&!cp.value("eligibleForGameTarget",true)&&!cp.value("whitelistSuggested",true);for(const auto& tag:cp["tags"])if(tag=="game"||tag=="pid-whitelist-candidate")suggestionsSafe=false;}
                }else suggestionsSafe=jsonStringOr(row,"status")=="ready"&&row["pidCoordination"].value("eligibleForGameTarget",false)&&row["pidCoordination"]["suggestedWhitelist"].size()==1;
                bool preserved=state["futureTop"]==original["futureTop"]&&row["futureGame"]==legacy["futureGame"]&&row["pidCoordination"]["futurePid"]==legacy["pidCoordination"]["futurePid"];
                preserved=preserved&&std::find(row["pidCoordination"]["tags"].begin(),row["pidCoordination"]["tags"].end(),json("custom-pid-tag"))!=row["pidCoordination"]["tags"].end();
                for(const auto& candidate:row["candidates"])preserved=preserved&&candidate["futureCandidate"]==42&&std::find(candidate["pidCoordination"]["tags"].begin(),candidate["pidCoordination"]["tags"].end(),json("custom-candidate-tag"))!=candidate["pidCoordination"]["tags"].end();
                const bool diskPreserved=readBinaryFile(stateFile)==originalBytes&&readBinaryFile(target.path)==shortcutBytes,passed=observation.active&&suggestionsSafe&&preserved&&diskPreserved&&addGate;
                cacheRows.push_back({{"variant",mode},{"readState",state},{"addPlan",addPlan},{"active",observation.active},{"suggestionsSafe",suggestionsSafe},{"unknownFieldsPreserved",preserved},{"diskPreserved",diskPreserved},{"addGate",addGate},{"passed",passed}});cacheSafe=cacheSafe&&passed;
            }
            writeJsonAtomic(p/L"cached-scan-observations.json",cacheRows);
            check(cacheSafe,"Validated cached scan retained stale PID authority");
            // Exact system-utility labels must not supply a game identity.
            const json utilityEvidence={{{"variant","leomoon-pe"},{"file","LeoMoon CPU-V.exe"},{"product","LeoMoon CPU-V"},{"description","LeoMoon CPU-V"}},{{"variant","inspectre-publisher-pe"},{"file","InSpectre.exe"},{"product","\"InSpectre\", by Steve Gibson"},{"description","InSpectre: Meltdown and Spectre?"}},{{"variant","inspectre-description-pe"},{"file","InSpectre.exe"},{"product","Application"},{"description","InSpectre: Meltdown and Spectre?"}},{{"variant","leomoon-uppercase-pe"},{"file","LEOMOON CPU-V.EXE"},{"product","LEOMOON CPU-V"},{"description","LEOMOON CPU-V"}},{{"variant","inspectre-publisher-only"},{"file","InSpectre.exe"},{"product","\"InSpectre\", by Steve Gibson"},{"description","Application"}}};
            json utilityRows=json::array();bool utilitiesSafe=true;
            for(const auto& evidence:utilityEvidence){
                const auto path=p/L"local-cases"/toWide(jsonStringOr(evidence,"variant"))/toWide(jsonStringOr(evidence,"file"));
                const auto value=assessPeIdentity({{"ProductName",jsonStringOr(evidence,"product")},{"FileDescription",jsonStringOr(evidence,"description")}},classifyLibraryExecutable(path),"Owned Game");
                const bool passed=value.role=="tool"&&value.primaryEvidenceSource=="pe-tool-classification";
                utilityRows.push_back({{"evidence",evidence},{"role",value.role},{"source",value.primaryEvidenceSource},{"confidence",value.identityConfidence},{"passed",passed}});utilitiesSafe=utilitiesSafe&&passed;
            }
            const auto utilityDirectory=p/L"local-only",utilityExe=utilityDirectory/L"LeoMoon CPU-V.exe";textFile(utilityExe,"owned inert utility, never executed");fs::resize_file(utilityExe,6u*1024u*1024u);
            const auto utilityScan=scanOneGameDirectory(utilityDirectory,1,defaultLibraryConfig(),{});
            const bool scanSafe=jsonStringOr(utilityScan,"status")=="needs-primary-exe-confirmation"&&jsonStringOr(utilityScan,"primaryExecutable").empty()&&!utilityScan["pidCoordination"].value("eligibleForGameTarget",true)&&utilityScan["pidCoordination"]["suggestedWhitelist"].empty()&&jsonStringOr(utilityScan["candidates"][0],"role")=="tool"&&!utilityScan["candidates"][0]["pidCoordination"].value("canBecomeGameTargetIfSelected",true);
            auto utilityManualConfig=defaultLibraryConfig();utilityManualConfig["manualPrimary"][canonicalPathKey(utilityDirectory)]={{"exe",toUtf8(utilityExe.wstring())}};
            const auto utilityManual=scanOneGameDirectory(utilityDirectory,1,utilityManualConfig,{});const bool manualRetained=utilityManual.value("selectionLocked",false)&&jsonStringOr(utilityManual,"selectionSource")=="manual"&&jsonStringOr(utilityManual,"primaryExecutable")==toUtf8(utilityExe.wstring());
            json utilityControls=json::array();
            for(const auto* title:{"InSpectre","Spectre","Leo Moon"}){
                const auto value=assessPeIdentity({{"ProductName",title},{"FileDescription",title}},"main-game",title);utilityControls.push_back({{"title",title},{"role",value.role},{"source",value.primaryEvidenceSource},{"passed",value.role=="main-game"&&value.primaryEvidenceSource=="pe-product-name"}});
            }
            for(const auto* file:{"InSpectre.exe","LeoMoonGame.exe"}){
                const auto folder=p/L"ordinary-labels"/fs::path(toWide(file)).stem(),exe=folder/toWide(file);textFile(exe,"owned inert game, never executed");fs::resize_file(exe,6u*1024u*1024u);const auto value=scanOneGameDirectory(folder,1,defaultLibraryConfig(),{});
                utilityControls.push_back({{"file",file},{"scan",value},{"passed",jsonStringOr(value,"primaryExecutable")==toUtf8(exe.wstring())&&jsonStringOr(value,"primaryRole")=="main-game"}});
            }
            writeJsonAtomic(p/L"utility-label-observations.json",{{"rows",utilityRows},{"scanner",utilityScan},{"scanSafe",scanSafe},{"manual",utilityManual},{"manualRetained",manualRetained},{"controls",utilityControls}});
            check(manualRetained&&utilityControls.size()==5&&std::all_of(utilityControls.begin(),utilityControls.end(),[](const json& x){return x["passed"].get<bool>();}),"Exact utility labels changed ordinary game or manual selection");
            check(utilitiesSafe&&scanSafe,"System utility labels became automatic game identities");
            // A real Win5 prerequisite installer was promoted by FileDescription.
            const json installerEvidence={{{"variant","logged-ue-prerequisites"},{"file","UEPrereqSetup_x64.exe"},{"product","UE Prerequisites (x64)"},{"description","UE Prerequisites (x64)"},{"directory","Install"}},{{"variant","generic-product-setup"},{"file","setup.exe"},{"product","Application"},{"description","Owned Game"},{"directory","Owned Game"}},{{"variant","game-product-setup"},{"file","GameSetup.exe"},{"product","Owned Game"},{"description","Owned Game"},{"directory","Owned Game"}}};
            json installerRows=json::array();bool installersSafe=true;
            for(const auto& evidence:installerEvidence){
                const auto path=p/L"installer-cases"/toWide(jsonStringOr(evidence,"directory"))/toWide(jsonStringOr(evidence,"file"));
                const auto pathRole=classifyLibraryExecutable(path);
                const auto value=assessPeIdentity({{"ProductName",jsonStringOr(evidence,"product")},{"FileDescription",jsonStringOr(evidence,"description")}},pathRole,jsonStringOr(evidence,"directory"));
                const bool passed=pathRole=="installer"&&value.role=="installer"&&value.primaryEvidenceSource=="pe-tool-classification"&&value.identityConfidence==0;
                installerRows.push_back({{"evidence",evidence},{"pathRole",pathRole},{"role",value.role},{"source",value.primaryEvidenceSource},{"confidence",value.identityConfidence},{"passed",passed}});installersSafe=installersSafe&&passed;
            }
            json descriptionControls=json::array();
            for(const auto* pathRole:{"main-game","helper"}){
                const auto value=assessPeIdentity({{"ProductName","Application"},{"FileDescription","Hearts of Iron IV"}},pathRole);
                descriptionControls.push_back({{"pathRole",pathRole},{"role",value.role},{"source",value.primaryEvidenceSource},{"passed",value.role=="main-game"&&value.primaryEvidenceSource=="pe-file-description"&&value.identityConfidence==0.94}});
            }
            writeJsonAtomic(p/L"installer-description-observations.json",{{"rows",installerRows},{"controls",descriptionControls}});
            check(descriptionControls.size()==2&&std::all_of(descriptionControls.begin(),descriptionControls.end(),[](const json& x){return x["passed"].get<bool>();}),"Installer fence changed legitimate description fallback");
            check(installersSafe,"Installer path role was overridden by file description");
            // Valid path components must not concatenate into a tool marker.
            const json pathBoundaryCases={{{"variant","exe-proxy-boundary"},{"relative","ProxyAdventure.exe"},{"file","ProxyAdventure.exe"}},{{"variant","mod-manager-boundary"},{"relative","mod/manager/OrdinaryGame"},{"file","OrdinaryGame.exe"}},{{"variant","train-er-boundary"},{"relative","train/er/OrdinaryGame"},{"file","OrdinaryGame.exe"}}};
            json boundaryRows=json::array();bool boundarySafe=true;
            for(const auto& evidence:pathBoundaryCases){
                const auto folder=p/L"boundary"/toWide(jsonStringOr(evidence,"relative")),exe=folder/toWide(jsonStringOr(evidence,"file"));
                textFile(exe,"owned inert game binary, never executed");fs::resize_file(exe,6u*1024u*1024u);
                const auto value=scanOneGameDirectory(folder,1,defaultLibraryConfig(),{});
                const auto winRole=classifyLibraryIdentityText(toUtf8(exe.wstring())),slashRole=classifyLibraryIdentityText(toUtf8(exe.generic_wstring()));
                const bool passed=jsonStringOr(value,"status")=="ready"&&jsonStringOr(value,"primaryExecutable")==toUtf8(exe.wstring())&&jsonStringOr(value,"primaryRole")=="main-game"&&winRole=="main-game"&&slashRole=="main-game";
                boundaryRows.push_back({{"variant",evidence["variant"]},{"scan",value},{"winRole",winRole},{"slashRole",slashRole},{"passed",passed}});boundarySafe=boundarySafe&&passed;
            }
            json boundaryControls=json::array();
            for(const auto& item:std::vector<std::pair<std::string,std::string>>{{"exeproxy.exe","mod-manager"},{"Mod_Manager","mod-manager"},{"trainer.exe","modifier"},{"Ｍｏｄ　Ｍａｎａｇｅｒ","mod-manager"},{"Tcl/Tk","helper"},{"Tcl\\Tk","helper"}}){
                const auto role=classifyLibraryIdentityText(item.first);boundaryControls.push_back({{"identity",item.first},{"expected",item.second},{"role",role},{"passed",role==item.second}});
            }
            writeJsonAtomic(p/L"path-boundary-observations.json",{{"rows",boundaryRows},{"controls",boundaryControls}});
            check(boundaryControls.size()==6&&std::all_of(boundaryControls.begin(),boundaryControls.end(),[](const json& x){return x["passed"].get<bool>();}),"Path boundary repair lost actual tool or Unicode labels");
            check(boundarySafe,"Path label matching joined separate components into a tool");
            // Exact filename-only runtime evidence from Win5's MO directory.
            const json runtimeExecutables={{{"file","usvfs_proxy_x64.exe"},{"size",885760}},{{"file","usvfs_proxy_x86.exe"},{"size",697856}},{{"file","nxmhandler.exe"},{"size",126464}}};
            json runtimeRows=json::array();bool runtimeSafe=true;unsigned runtimeIndex=0;
            fs::path manualRuntimeDirectory,manualRuntimeExe;
            for(const auto& evidence:runtimeExecutables){
                const auto folder=p/L"bundle"/toWide("case-"+std::to_string(runtimeIndex++)),exe=folder/toWide(jsonStringOr(evidence,"file"));
                textFile(exe,"owned inert mod runtime, never executed");fs::resize_file(exe,evidence["size"].get<uintmax_t>());
                const auto value=scanOneGameDirectory(folder,1,defaultLibraryConfig(),{});const auto& candidate=value["candidates"][0];
                const bool passed=jsonStringOr(value,"status")=="unrecognized-tool"&&jsonStringOr(value,"primaryExecutable").empty()&&jsonStringOr(candidate,"role")=="helper"&&!value["pidCoordination"].value("eligibleForGameTarget",true)&&!candidate["pidCoordination"].value("canBecomeGameTargetIfSelected",true);
                runtimeRows.push_back({{"file",evidence["file"]},{"size",evidence["size"]},{"scan",value},{"passed",passed}});runtimeSafe=runtimeSafe&&passed;
                if(manualRuntimeExe.empty()){manualRuntimeDirectory=folder;manualRuntimeExe=exe;}
            }
            json runtimeGameControls=json::array();
            for(const auto* name:{"ProxyAdventure.exe","nxmhandler_game.exe"}){
                const auto folder=p/L"ordinary"/fs::path(toWide(name)).stem(),exe=folder/toWide(name);textFile(exe,"owned inert game file, never executed");fs::resize_file(exe,6u*1024u*1024u);
                const auto value=scanOneGameDirectory(folder,1,defaultLibraryConfig(),{});const bool passed=jsonStringOr(value,"status")=="ready"&&jsonStringOr(value,"primaryExecutable")==toUtf8(exe.wstring())&&jsonStringOr(value,"primaryRole")=="main-game";
                runtimeGameControls.push_back({{"name",name},{"scan",value},{"passed",passed}});
            }
            auto runtimeManualConfig=defaultLibraryConfig();runtimeManualConfig["manualPrimary"][canonicalPathKey(manualRuntimeDirectory)]={{"exe",toUtf8(manualRuntimeExe.wstring())}};
            const auto runtimeManual=scanOneGameDirectory(manualRuntimeDirectory,1,runtimeManualConfig,{});
            const bool runtimeManualRetained=runtimeManual.value("selectionLocked",false)&&jsonStringOr(runtimeManual,"selectionSource")=="manual"&&jsonStringOr(runtimeManual,"primaryExecutable")==toUtf8(manualRuntimeExe.wstring())&&jsonStringOr(runtimeManual,"status")=="ready";
            writeJsonAtomic(p/L"runtime-executable-observations.json",{{"rows",runtimeRows},{"gameControls",runtimeGameControls},{"manual",runtimeManual},{"manualRetained",runtimeManualRetained}});
            check(runtimeGameControls.size()==2&&runtimeGameControls[0]["passed"].get<bool>()&&runtimeGameControls[1]["passed"].get<bool>()&&runtimeManualRetained,"Exact runtime exclusion changed ordinary game or explicit manual selection");
            check(runtimeSafe,"Runtime executables were promoted into the game lane");
            // Replay the actual PE evidence from Win5, not a title-only model.
            const std::vector<json> auxiliaryEvidence={
                {{"file","CreationKit.exe"},{"product","Bethesda Softworks: Creation Kit 2.0"},{"description","Creation Kit 2.0"},{"directory","F4.CE.v1.10.984"},{"role","tool"}},
                {{"file","QtWebEngineProcess.exe"},{"product","Qt6"},{"description","Qt QtWebEngineProcess"},{"directory","MO"},{"role","helper"}},
                {{"file","resources/app.asar.unpacked/node_modules/grpc-tools/bin/protoc.exe"},{"product","Protocol Buffers - Google's Data Interchange Format"},{"description","Compiled with MSVC 19.42.34436.0"},{"directory","RSI Launcher"},{"role","helper"}},
                {{"file","FrostyModManager/ThirdParty/fifaconfig.exe"},{"product","fifaconfig"},{"description","fifaconfig"},{"directory","SW2MOD"},{"role","helper"}},
                {{"file","MO2/explorer++/Explorer++.exe"},{"product","Explorer++"},{"description","Explorer++"},{"directory","SkyrimAE"},{"role","tool"}},
                {{"file","MO2/loot/lootcli.exe"},{"product","lootcli"},{"description","LOOT Command line interface"},{"directory","SkyrimAE"},{"role","tool"}}
            };
            json auxiliaryObservations=json::array();
            for(const auto& row:auxiliaryEvidence){
                const auto path=p/toWide(jsonStringOr(row,"directory"))/toWide(jsonStringOr(row,"file"));
                const auto pe=assessPeIdentity({{"ProductName",jsonStringOr(row,"product")},{"FileDescription",jsonStringOr(row,"description")}},classifyLibraryExecutable(path),jsonStringOr(row,"directory"));
                auxiliaryObservations.push_back({{"evidence",row},{"role",pe.role},{"source",pe.primaryEvidenceSource},{"confidence",pe.identityConfidence}});
            }
            writeJsonAtomic(p/L"auxiliary-identity-observations.json",auxiliaryObservations);
            json modUtilityControls=json::array();
            for(const auto* title:{"Explorer","Loot Hero","LOOT"}){
                const auto pe=assessPeIdentity({{"ProductName",title},{"FileDescription",title}},"main-game",title);
                modUtilityControls.push_back({{"title",title},{"role",pe.role},{"source",pe.primaryEvidenceSource},{"passed",pe.role=="main-game"&&pe.primaryEvidenceSource=="pe-product-name"}});
            }
            writeJsonAtomic(p/L"mod-utility-title-controls.json",modUtilityControls);
            for(const auto& row:modUtilityControls)check(row["passed"].get<bool>(),"Utility exclusion swallowed an ordinary Explorer/Loot title");
            for(const auto& row:auxiliaryObservations)check(row["role"]==row["evidence"]["role"]&&row["source"]=="pe-tool-classification",("Auxiliary PE evidence became a game: "+jsonStringOr(row["evidence"],"file")).c_str());
            // Replay the four supplemental executable records from Win5.
            const auto supplementalEvidence=json::parse(R"FIELD([{"relative":"ARMORED.CORE.VI.FIRES.OF.RUBICON/ArtbookOST/ACVI_FOR Digital Artbook and Soundtrack.exe","directory":"ArtbookOST","size":653824,"versionEvidence":{"LegalCopyright":"(c) 2005-2025 Unity Technologies. All rights reserved."},"oldRole":"main-game","oldSelected":true},{"relative":"ELDEN RING/AdvGuide/ELDEN RING Adventure Guide.exe","directory":"AdvGuide","size":653824,"versionEvidence":{"LegalCopyright":"(c) 2005-2025 Unity Technologies. All rights reserved."},"oldRole":"main-game","oldSelected":true},{"relative":"ELDEN RING/ArtbookOST/ELDEN RING Digital Artbook & Soundtrack.exe","directory":"ArtbookOST","size":653824,"versionEvidence":{"LegalCopyright":"(c) 2005-2025 Unity Technologies. All rights reserved."},"oldRole":"main-game","oldSelected":true},{"relative":"Sekiro/Artwork_MiniSoundtrack/DigitalArtwork_MiniSoundtrack.exe","directory":"Artwork_MiniSoundtrack","size":24645512,"versionEvidence":{"CompanyName":"DIGITAL ARTWORK & MINI SOUNDTRACK","FileDescription":"DIGITAL ARTWORK & MINI SOUNDTRACK","InternalName":"DIGITAL ARTWORK & MINI SOUNDTRACK","LegalCopyright":"©2019 FromSoftware, Inc. / ©2019 Activision Publishing, Inc.","ProductName":"DIGITAL ARTWORK & MINI SOUNDTRACK"},"oldRole":"main-game","oldSelected":false}])FIELD");
            json supplementalObserved=json::array();bool supplementalSafe=true;
            auto retainedScan=loadJsonDocument(f.scan);
            for(const auto& row:supplementalEvidence){
                const auto exe=f.games/fs::path(toWide(jsonStringOr(row,"relative"))),folder=exe.parent_path();
                textFile(exe,"owned supplemental executable marker, never launched");fs::resize_file(exe,row["size"].get<uintmax_t>());
                const auto pe=assessPeIdentity(row["versionEvidence"].get<std::map<std::string,std::string>>(),classifyLibraryExecutable(exe),jsonStringOr(row,"directory"));
                const auto candidate=scoreLibraryExecutable(folder,exe,0);
                const auto automatic=scanOneGameDirectory(folder,1,defaultLibraryConfig(),{});
                auto manualConfig=defaultLibraryConfig();manualConfig["manualPrimary"][canonicalPathKey(folder)]={{"exe",toUtf8(exe.wstring())}};
                const auto manual=scanOneGameDirectory(folder,1,manualConfig,{});
                const bool classified=pe.role=="tool"&&candidate.role=="tool";
                const bool rejected=jsonStringOr(automatic,"status")=="unrecognized-tool"&&jsonStringOr(automatic,"primaryExecutable").empty()&&automatic["pidCoordination"]["suggestedWhitelist"].empty();
                const bool retained=manual["selectionLocked"]==true&&jsonStringOr(manual,"selectionSource")=="manual"&&canonicalPathKey(fs::path(toWide(jsonStringOr(manual,"primaryExecutable"))))==canonicalPathKey(exe);
                supplementalObserved.push_back({{"evidence",row},{"peRole",pe.role},{"scoredRole",candidate.role},{"automatic",automatic},{"manual",manual},{"classifiedAsTool",classified},{"notAutomaticGame",rejected},{"manualChoiceRetained",retained}});
                supplementalSafe=supplementalSafe&&classified&&rejected&&retained;
                retainedScan["games"].push_back({{"gameDirectory",toUtf8(folder.wstring())},{"directoryName",toUtf8(folder.filename().wstring())},{"primaryExecutable",toUtf8(exe.wstring())},{"primaryRole","main-game"},{"status","ready"},{"selectionSource","automatic-high-confidence"},{"scanComplete",true}});
            }
            writeJsonAtomic(p/L"supplemental-content-observed.json",supplementalObserved);
            check(supplementalObserved.size()==4&&supplementalSafe,"Supplemental content became an automatic game or lost manual choice");
            check(classifyLibraryExecutable(L"C:/Games/Adventure Guide/Adventure Guide.exe")=="main-game","A standalone Adventure Guide title was excluded");
            check(classifyLibraryExecutable(L"C:/Games/Soundtrack/Soundtrack.exe")=="main-game","A bare soundtrack game title was excluded");
            check(classifyLibraryExecutable(L"C:/Games/Digital Artbook and Soundtrack/Game.exe")=="main-game","Parent folder companion words excluded a normal main executable");
            check(assessPeIdentity({{"ProductName","Adventure Guide"},{"FileDescription","Adventure Guide"}}).role=="main-game","An ordinary guide game PE title was excluded");
            // Retain stale automatic rows through the original fresh CLI scan.
            writeJsonAtomic(f.scan,retainedScan);
            for(const auto* name:{L"ELDEN RING",L"ARMORED.CORE.VI.FIRES.OF.RUBICON"}){
                const auto folder=f.games/name/L"Game";const auto exe=folder/(std::wstring(name)==L"ELDEN RING"?L"eldenring.exe":L"armoredcore6.exe");textFile(exe,"owned actual main marker, never launched");fs::resize_file(exe,std::wstring(name)==L"ELDEN RING"?(20u<<20):111413272u);
            }
            textFile(f.games/L"Sekiro"/L"sekiro.exe","owned actual main marker, never launched");fs::resize_file(f.games/L"Sekiro"/L"sekiro.exe",20u<<20);
            const auto fallout=p/L"F4.CE.v1.10.984",gameExe=fallout/L"Fallout4.exe",editorExe=fallout/L"CreationKit.exe";
            textFile(gameExe,"owned fake game, never launched");fs::resize_file(gameExe,20u<<20);
            textFile(editorExe,"owned fake editor, never launched");fs::resize_file(editorExe,20u<<20);
            const auto classified=scanOneGameDirectory(fallout,1,defaultLibraryConfig(),{});
            check(jsonStringOr(classified,"primaryExecutable")==toUtf8(gameExe.wstring()),"Creation Kit displaced Fallout4 automatic primary");
            for(const auto* name:{L"QtWebEngineProcess.exe",L"protoc.exe",L"fifaconfig.exe"}){
                const auto folder=p/L"OnlyAuxiliary"/fs::path(name).stem(),aux=folder/name;textFile(aux,"owned fake auxiliary, never launched");fs::resize_file(aux,20u<<20);
                const auto scannedAux=scanOneGameDirectory(folder,1,defaultLibraryConfig(),{});
                check(jsonStringOr(scannedAux,"primaryExecutable").empty()&&jsonStringOr(scannedAux,"status")=="unrecognized-tool","Auxiliary-only directory became a ready game");
            }

            bool localAutomaticSafe=true,localManualSafe=true,localNormalSafe=true;json localObserved=json::array();
            for(const auto* name:{L"PPSSPPWindows64.exe",L"snes9x.exe",L"Mesen-S.exe",L"NsEmuTools.exe",L"Ryujinx.exe"}) {
                const auto folder=p/L"LocalPrograms"/fs::path(name).stem(),exe=folder/name;textFile(exe,"owned inert application marker, never executed");fs::resize_file(exe,20u<<20);
                const auto observed=scanOneGameDirectory(folder,1,defaultLibraryConfig(),{});
                const bool noAutomatic=jsonStringOr(observed,"primaryExecutable").empty()&&jsonStringOr(observed,"status")=="unrecognized-tool"&&observed["pidCoordination"]["suggestedWhitelist"].empty();
                auto chosen=defaultLibraryConfig();chosen["manualPrimary"][canonicalPathKey(folder)]={{"exe",toUtf8(exe.wstring())}};
                const auto manual=scanOneGameDirectory(folder,1,chosen,{});const bool selected=jsonStringOr(manual,"primaryExecutable")==toUtf8(exe.wstring())&&manual["selectionLocked"]==true;
                localAutomaticSafe=localAutomaticSafe&&noAutomatic;localManualSafe=localManualSafe&&selected;
                localObserved.push_back({{"name",toUtf8(fs::path(name).wstring())},{"role",scoreLibraryExecutable(folder,exe,0).role},{"automaticSafe",noAutomatic},{"manualSafe",selected}});
            }
            for(const auto* name:{L"Emulator Adventure.exe",L"PSP Quest.exe",L"Switch.exe",L"Ryujinx Adventure.exe"})localNormalSafe=localNormalSafe&&classifyLibraryExecutable(fs::path(L"C:/Games/Ordinary")/name)=="main-game";
            writeJsonAtomic(p/L"local-program-observations.json",{{"automaticSafe",localAutomaticSafe},{"manualSafe",localManualSafe},{"normalSafe",localNormalSafe},{"rows",localObserved}});
            check(localAutomaticSafe,"Emulator applications became automatic game identities");check(localManualSafe&&localNormalSafe,"Local-program classification lost explicit choice or ordinary game titles");
            check(assessPeIdentity({{"ProductName","Fallout 4"},{"FileDescription","Fallout 4"}}).role=="main-game","Normal Fallout identity was excluded");
            check(assessPeIdentity({{"ProductName","Application"},{"FileDescription","Hearts of Iron IV"}},"helper").primaryEvidenceSource=="pe-file-description","Legitimate description fallback was lost");
            check(assessPeIdentity({{"ProductName","Total War: SHOGUN 2"},{"FileDescription","Total War: SHOGUN 2"}},"launcher","Total War - SHOGUN 2").role=="main-game","Legitimate game launcher identity was lost");
            auto manualTool=defaultLibraryConfig();manualTool["manualPrimary"][canonicalPathKey(fallout)]={{"exe",toUtf8(editorExe.wstring())}};
            const auto explicitTool=scanOneGameDirectory(fallout,1,manualTool,{});
            check(jsonStringOr(explicitTool,"primaryExecutable")==toUtf8(editorExe.wstring())&&explicitTool["selectionLocked"]==true,"Explicit user primary selection was overwritten");
            const auto old=loadJsonDocument(f.scan);check(primaryCall(f,dir,next,f.exe)==0,"Primary CLI failed to publish selected EXE");const auto config=loadLibraryConfig(f.data/L"config"/L"library-config.json"),state=loadJsonDocument(f.scan);
            check(config["manualPrimary"][canonicalPathKey(dir)]["exe"]==toUtf8(next.wstring())&&state["games"][0]["primaryExecutable"]==toUtf8(next.wstring()),"Config and current scan disagree about selected primary");
            check(state["games"][0]["selectionLocked"]==true&&state["games"][0]["selectionSource"]=="manual"&&state["games"][0]["ownerNote"]=="keep","Selected primary did not retain manual lock/unknown fields");check(state["games"][1]==old["games"][1]&&state["futureUnknown"]==old["futureUnknown"],"Targeted primary save rewrote other games or unknown fields");
            const auto zelda=f.games/L"Zelda",main=zelda/L"totk.exe",sidecar=zelda/L"totk_xlate.exe";
            textFile(main,"owned main, never executed");fs::resize_file(main,20u<<20);textFile(sidecar,"owned translation companion, never executed");
            check(translationSidecarForExecutable(sidecar,"totk_xlate","totk_xlate"),"Repeated translation metadata hid a sibling helper");
            check(!translationSidecarForExecutable(sidecar,"Actual Game Title","Actual Game Title"),"A distinct formal PE identity was demoted by filename alone");
            check(scoreLibraryExecutable(zelda,sidecar,0).role=="helper","Translation companion remained the automatic main game");
            const auto automatic=scanOneGameDirectory(zelda,1,defaultLibraryConfig(),{});
            check(jsonStringOr(automatic,"primaryExecutable")==toUtf8(main.wstring()),"Automatic scan chose translation companion instead of main EXE");
            check(f.call({L"--add-library-game",L"--game-dir",zelda.wstring(),L"--exe",main.wstring()})==0,"Manual add rejected the selected EXE");
            const auto manual=loadLibraryConfig(f.data/L"config"/L"library-config.json");
            check(manual["manualPrimary"][canonicalPathKey(zelda)]["exe"]==toUtf8(main.wstring()),"Manual add dropped the user's selected EXE");
            const auto common=f.steam/L"steamapps"/L"common"/L"NativeFixture";
            textFile(common/L"NativeFixture.exe","owned native marker, never executed");
            textFile(f.steam/L"steamapps"/L"appmanifest_620.acf",R"VDF("AppState" { "appid" "620" "name" "Native Fixture" "installdir" "NativeFixture" })VDF");
            check(f.fresh()==0&&f.scanAck(),"Explicit scan did not publish manual/native inventory");
            const auto scanned=loadLibraryScanState(f.scan);size_t manualCount=0,nativeCount=0;
            for(const auto& row:scanned["games"]){
                if(jsonStringOr(row,"gameDirectory")==toUtf8(zelda.wstring())){++manualCount;check(row["selectionLocked"]==true&&row["discoverySource"]=="manual"&&jsonStringOr(row,"primaryExecutable")==toUtf8(main.wstring()),"Overlapping automatic root lost manual choice/source");}
                if(jsonStringOr(row,"gameDirectory")==toUtf8(common.wstring())){++nativeCount;check(row["steamNative"]==true&&row["nativeSteamAppId"]==620,"Registered native game lost native identity");}
            }
            check(manualCount==1&&nativeCount==1,"Manual/native games were absent or duplicated outside custom root");
            const auto supplementalFinal=loadLibraryScanState(f.scan);size_t normalMainCount=0;
            for(const auto& row:supplementalFinal["games"]){
                const auto exe=fs::path(toWide(jsonStringOr(row,"primaryExecutable")));
                check(!std::any_of(supplementalEvidence.begin(),supplementalEvidence.end(),[&](const json& evidence){return canonicalPathKey(exe)==canonicalPathKey(f.games/fs::path(toWide(jsonStringOr(evidence,"relative"))));}),"Fresh scan retained an automatic supplemental game target");
                const auto stem=asciiLower(toUtf8(exe.stem().wstring()));if(stem=="eldenring"||stem=="armoredcore6"||stem=="sekiro")++normalMainCount;
            }
            check(normalMainCount==3,"Fresh scan lost or duplicated a real main game next to supplemental content");
            writeJsonAtomic(p/L"supplemental-content-fresh-scan.json",supplementalFinal);


        });});
        run("discovery-producer-primary-stale-editor-keeps-newer-choice",[&](const fs::path& p){primaryFixture(p,[&](auto& f,const auto& dir,const auto& next){
            auto state=loadJsonDocument(f.scan);state["games"][0]["primaryExecutable"]=toUtf8(next.wstring());writeJsonAtomic(f.scan,state);const auto cfg=f.data/L"config"/L"library-config.json";const auto before=readBinaryFile(cfg),prior=readBinaryFile(f.scan);check(primaryCall(f,dir,f.exe,f.exe)!=0,"Stale editor overwrote a changed primary");check(readBinaryFile(cfg)==before&&readBinaryFile(f.scan)==prior,"Stale primary attempt changed user bytes");
        });});
        run("discovery-producer-primary-rejects-outside-exe-and-native-record",[&](const fs::path& p){primaryFixture(p,[&](auto& f,const auto& dir,const auto& next){
            const auto outside=p/L"OutsideOther"/L"Other.exe";textFile(outside,"owned outside exe");const auto config=f.data/L"config"/L"library-config.json";const auto before=readBinaryFile(config);check(primaryCall(f,dir,outside,f.exe)!=0&&readBinaryFile(config)==before,"Outside EXE changed primary configuration");
            auto state=loadJsonDocument(f.scan);state["games"][0]["steamNative"]=true;state["games"][0]["contentType"]="steam-native";state["games"][0]["nativeSteamAppId"]=620;writeJsonAtomic(f.scan,state);const auto prior=readBinaryFile(f.scan);check(primaryCall(f,dir,next,f.exe)!=0&&readBinaryFile(config)==before&&readBinaryFile(f.scan)==prior,"Native Steam ownership was reclassified by editor primary save");
        });});
        run("discovery-producer-primary-rejects-blacklisted-and-protected-inventory",[&](const fs::path& p){primaryFixture(p,[&](auto& f,const auto& dir,const auto& next){
            const auto config=f.data/L"config"/L"library-config.json",blocked=dir/L"Steam.exe";textFile(blocked,"owned blocked exe");const auto before=readBinaryFile(config),prior=readBinaryFile(f.scan);check(primaryCall(f,dir,blocked,f.exe)!=0&&readBinaryFile(config)==before&&readBinaryFile(f.scan)==prior,"Explicit primary bypassed executable exclusion");
            auto future=f.catalog(99);writeJsonAtomic(f.scan,future);const auto raw=readBinaryFile(f.scan);check(primaryCall(f,dir,next,f.exe)!=0&&readBinaryFile(config)==before&&readBinaryFile(f.scan)==raw,"Future scan was overwritten by primary save");
            const auto add=[&](const fs::path& folder,const fs::path& executable){return f.call({L"--add-library-game",L"--game-dir",folder.wstring(),L"--exe",executable.wstring()});};
            check(add(dir,blocked)!=0&&readBinaryFile(config)==before&&readBinaryFile(f.scan)==raw,"Manual add accepted an excluded selected EXE");
            const auto native=f.steam/L"steamapps"/L"common"/L"NativeChoice",nativeExe=native/L"NativeChoice.exe";textFile(nativeExe,"owned native executable, never run");
            textFile(f.steam/L"steamapps"/L"appmanifest_620.acf",R"VDF("AppState" { "appid" "620" "name" "Native Choice" "installdir" "NativeChoice" })VDF");
            check(add(native,nativeExe)!=0&&readBinaryFile(config)==before&&readBinaryFile(f.scan)==raw,"Manual add overwrote manifest-managed native primary selection");
            writeJsonAtomic(f.scan,f.catalog());
            check(add(dir,next)==0,"Healthy manual EXE selection failed after rejected choices");
            const auto accepted=loadLibraryConfig(config);check(accepted["manualPrimary"][canonicalPathKey(dir)]["exe"]==toUtf8(next.wstring()),"Healthy manual EXE choice was not retained");
            auto retained=accepted;retained["futureAdmission"]={{"keep",true}};retained["manualPrimary"][canonicalPathKey(dir)]["futurePrimary"]={{"keep",true}};
            const auto wildcard=dir/L"UnityCrashHandler64.exe";textFile(wildcard,"owned excluded wildcard marker, never run");
            json rows=json::array();bool safe=true;
            for(const auto* kind:{"excluded-steam","excluded-wildcard","native-manifest"}){
                const std::string mode=kind;writeJsonAtomic(config,retained);writeJsonAtomic(f.scan,f.catalog());const auto originalConfig=readBinaryFile(config),originalScan=readBinaryFile(f.scan);
                const auto folder=mode=="native-manifest"?native:dir,selected=mode=="native-manifest"?nativeExe:mode=="excluded-wildcard"?wildcard:blocked;
                const auto code=f.call({L"--set-library-primary",L"--game-dir",folder.wstring(),L"--exe",selected.wstring()});
                const bool rejected=code!=0&&f.output.find("LIBRARY_PRIMARY_SAVED:")==std::string::npos,configPreserved=readBinaryFile(config)==originalConfig,inventoryPreserved=readBinaryFile(f.scan)==originalScan;
                const bool passed=rejected&&configPreserved&&inventoryPreserved;safe=safe&&passed;rows.push_back({{"variant",mode},{"exitCode",code},{"rejectedWithoutSuccessAck",rejected},{"configPreserved",configPreserved},{"inventoryPreserved",inventoryPreserved},{"passed",passed}});
            }
            writeJsonAtomic(config,retained);writeJsonAtomic(f.scan,f.catalog());const auto originalInventory=readBinaryFile(f.scan);
            const auto valid=f.call({L"--set-library-primary",L"--game-dir",dir.wstring(),L"--exe",next.wstring()});const auto final=loadLibraryConfig(config);
            const bool validPreserved=valid==0&&f.output.find("LIBRARY_PRIMARY_SAVED:")!=std::string::npos&&final["manualPrimary"][canonicalPathKey(dir)]["exe"]==toUtf8(next.wstring())&&final["manualPrimary"][canonicalPathKey(dir)]["futurePrimary"]==retained["manualPrimary"][canonicalPathKey(dir)]["futurePrimary"]&&final["futureAdmission"]==retained["futureAdmission"]&&readBinaryFile(f.scan)==originalInventory;
            writeJsonAtomic(p/L"legacy-primary-admission-observed.json",{{"negativeCases",rows},{"validLegacySelectionPreserved",validPreserved},{"cliCalls",f.callCount}});
            check(rows.size()==3&&safe&&validPreserved,"Legacy primary selection bypassed exclusion or native ownership");


        });});

        run("discovery-producer-primary-state-write-failure-reports-and-retains-choice-for-retry",[&](const fs::path& p){primaryFixture(p,[&](auto& f,const auto& dir,const auto& next){
            const auto prior=readBinaryFile(f.scan);selftestMoveFault=[&](const auto&,const auto& to){return identityCallerPath(to)==identityCallerPath(f.scan.wstring())?DWORD{ERROR_ACCESS_DENIED}:DWORD{0};};
            const auto code=primaryCall(f,dir,next,f.exe);selftestMoveFault={};check(code!=0&&readBinaryFile(f.scan)==prior,"Failed primary state publication falsely succeeded or lost old inventory");
            check(loadLibraryConfig(f.data/L"config"/L"library-config.json")["manualPrimary"][canonicalPathKey(dir)]["exe"]==toUtf8(next.wstring()),"Failure discarded the user's durable primary choice");check(primaryCall(f,dir,next,f.exe)==0&&loadJsonDocument(f.scan)["games"][0]["primaryExecutable"]==toUtf8(next.wstring()),"Explicit retry could not finish a retained primary choice");
        });});
        run("discovery-producer-primary-old-full-scan-cannot-replace-new-choice",[&](const fs::path& p){primaryFixture(p,[&](auto& f,const auto& dir,const auto& next){
            LibraryScanObservation observed;const auto report=loadLibraryScanState(f.scan,&observed);const auto configPath=f.data/L"config"/L"library-config.json";auto config=loadLibraryConfig(configPath);const auto captured=config["manualPrimary"];
            config["manualPrimary"][canonicalPathKey(dir)]={{"exe",toUtf8(next.wstring())}};writeJsonAtomic(configPath,config);const auto prior=readBinaryFile(f.scan);bool committed=false;std::string reason;
            check(!publishFreshLibraryScan(f.data,f.scan,f.scan,observed,report,committed,reason,&captured)&&!committed&&readBinaryFile(f.scan)==prior,"Full scan using old primary configuration overwrote the user's new choice");
            json configurationRows=json::array();bool safe=true;
            for(const auto* mode:{"roots","trainer-roots","manual-add","exclude","cleared-roots","manual-primary","timestamp","bucket","extension","first-run-diagnostic","explicit-unsaved-roots"}) {
                const std::string kind=mode;const bool relevant=kind=="roots"||kind=="trainer-roots"||kind=="manual-add"||kind=="exclude"||kind=="cleared-roots"||kind=="manual-primary";
                DiscoveryProducerFixture scenario(p/L"configuration-races"/toWide(kind));const auto configFile=scenario.data/L"config"/L"library-config.json";
                const auto currentGame=scenario.games/L"CurrentGame",currentExe=currentGame/L"CurrentGame.exe",newRoot=scenario.root/L"NewRoot",manualGame=scenario.root/L"ManualAdded",manualExe=manualGame/L"ManualAdded.exe";
                textFile(currentExe,"owned current game marker, never executed");fs::resize_file(currentExe,20u<<20);textFile(manualExe,"owned manual game marker, never executed");fs::resize_file(manualExe,20u<<20);fs::create_directories(newRoot);
                auto initial=loadLibraryConfig(configFile);if(kind=="explicit-unsaved-roots"){initial["roots"]=json::array();initial["scanRootsExplicitlyCleared"]=true;writeJsonAtomic(configFile,initial);}
                writeJsonAtomic(scenario.scan,scenario.catalog());const auto jobFile=scenario.job/L"library-scan.json";writeJsonAtomic(jobFile,{{"schemaVersion",1},{"games",json::array()},{"ownerNote","keep-job"}});
                const auto previousState=readBinaryFile(scenario.scan),previousJob=readBinaryFile(jobFile);bool changed=false;std::vector<uint8_t> latestConfiguration;
                struct ResetHook{~ResetHook(){identityCallerReadHook={};}} resetHook;
                identityCallerReadHook=[&](const std::wstring& file){
                    if(changed||file!=identityCallerPath(scenario.scan.wstring()))return;changed=true;auto concurrent=loadLibraryConfig(configFile);
                    if(kind=="roots")concurrent["roots"]=json::array({toUtf8(newRoot.wstring())});
                    else if(kind=="trainer-roots")concurrent["trainerRoots"]=json::array({toUtf8((scenario.root/L"NewTrainers").wstring())});
                    else if(kind=="manual-add")concurrent["manualGameDirectories"].push_back({{"path",toUtf8(manualGame.wstring())},{"source","user"},{"locked",true}});
                    else if(kind=="exclude")concurrent["excludedGameDirectories"].push_back(toUtf8(currentGame.wstring()));
                    else if(kind=="cleared-roots"){concurrent["roots"]=json::array();concurrent["scanRootsExplicitlyCleared"]=true;}
                    else if(kind=="manual-primary")concurrent["manualPrimary"][canonicalPathKey(currentGame)]={{"exe",toUtf8(currentExe.wstring())}};
                    else if(kind=="timestamp")concurrent["updatedAt"]=unixTimeMs()+1;
                    else if(kind=="bucket")concurrent["manualBuckets"][canonicalPathKey(currentGame)]={{"bucket","non-game"}};
                    else if(kind=="extension")concurrent["futureOwner"]={{"keep",true}};
                    else if(kind=="first-run-diagnostic")concurrent["firstRunDefaultScan"]["finishedAt"]=unixTimeMs();
                    writeJsonAtomic(configFile,concurrent);latestConfiguration=readBinaryFile(configFile);
                };
                const auto code=scenario.fresh();identityCallerReadHook={};const bool stateRetained=readBinaryFile(scenario.scan)==previousState,jobRetained=readBinaryFile(jobFile)==previousJob,configRetained=readBinaryFile(configFile)==latestConfiguration;
                const bool refused=code==2&&!scenario.scanAck()&&scenario.error.find("ERROR_LIBRARY_SCAN_UNCONFIRMED:")!=std::string::npos;
                const bool initialCorrect=changed&&(relevant?(configRetained&&refused&&stateRetained&&jobRetained):(code==0&&scenario.scanAck()&&(kind=="explicit-unsaved-roots"||configRetained)));
                json row={{"variant",kind},{"relevant",relevant},{"changeObserved",changed},{"exitCode",code},{"acknowledged",scenario.scanAck()},{"error",scenario.error},{"stateRetained",stateRetained},{"jobRetained",jobRetained},{"configurationRetained",configRetained},{"initialCorrect",initialCorrect}};
                if(relevant){
                    const auto retry=scenario.call({L"--scan-library",(scenario.root/L"retry-job").wstring(),L"--max-depth",L"1"});const auto fresh=loadJsonDocument(scenario.scan);bool latestVisible=true;
                    if(kind=="manual-add")latestVisible=std::any_of(fresh["games"].begin(),fresh["games"].end(),[&](const auto& game){return canonicalPathKey(fs::path(toWide(jsonStringOr(game,"gameDirectory"))))==canonicalPathKey(manualGame);});
                    if(kind=="exclude"||kind=="cleared-roots"||kind=="roots")latestVisible=std::none_of(fresh["games"].begin(),fresh["games"].end(),[&](const auto& game){return canonicalPathKey(fs::path(toWide(jsonStringOr(game,"gameDirectory"))))==canonicalPathKey(currentGame);});
                    row["retryExitCode"]=retry;row["retryAcknowledged"]=scenario.scanAck();row["latestChoicesVisible"]=latestVisible;row["retryCorrect"]=retry==0&&scenario.scanAck()&&latestVisible;
                    safe=safe&&initialCorrect&&row["retryCorrect"].get<bool>();
                }else safe=safe&&initialCorrect;
                configurationRows.push_back(std::move(row));
            }
            writeJsonAtomic(p/L"configuration-race-observed.json",configurationRows);
            check(configurationRows.size()==11&&safe,"Full scan published stale discovery configuration or rejected unrelated metadata");

        });});
        run("discovery-producer-primary-partial-inventory-does-not-drop-original-rows",[&](const fs::path& p){primaryFixture(p,[&](auto& f,const auto& dir,const auto& next){
            auto state=loadJsonDocument(f.scan);state["games"].push_back({{"gameDirectory",false},{"primaryExecutable",42}});writeJsonAtomic(f.scan,state);const auto prior=readBinaryFile(f.scan),config=readBinaryFile(f.data/L"config"/L"library-config.json");
            check(primaryCall(f,dir,next,f.exe)!=0&&readBinaryFile(f.scan)==prior&&readBinaryFile(f.data/L"config"/L"library-config.json")==config,"Targeted save dropped a malformed unrelated inventory row");
        });});

        run("discovery-producer-primary-resolves-pending-row-and-refreshes-counts",[&](const fs::path& p){primaryFixture(p,[&](auto& f,const auto& dir,const auto& next){
            auto state=loadJsonDocument(f.scan);state["games"][0]["status"]="needs-primary-exe-confirmation";state["games"][0]["primaryExecutable"]=nullptr;state["summary"]={{"games",2},{"ready",1},{"needsPrimaryExeConfirmation",1},{"manualPrimaryMissing",0},{"nonGames",0}};writeJsonAtomic(f.scan,state);
            check(primaryCall(f,dir,next,fs::path{})==0,"A pending primary could not be selected");const auto saved=loadJsonDocument(f.scan);check(saved["summary"]["ready"]==2&&saved["summary"]["needsPrimaryExeConfirmation"]==0&&saved["games"][0]["primaryExecutable"]==toUtf8(next.wstring()),"Primary save left pending counts or selected row stale");
        });});

        run("discovery-producer-primary-keeps-scan-exclusions-in-summary",[&](const fs::path& p){primaryFixture(p,[&](auto& f,const auto& dir,const auto& next){
            auto state=loadJsonDocument(f.scan);
            state["games"][0]["status"]="needs-primary-exe-confirmation";
            state["games"][0]["primaryExecutable"]=nullptr;
            state["roots"]=json::array({{{"path",toUtf8((p/L"Libraries").wstring())},{"status","scanned"}},
                {{"path",toUtf8((p/L"Tools").wstring())},{"status","non-game-root-excluded"}},
                {{"path",toUtf8((p/L"Steam").wstring())},{"status","platform-library-root-excluded"}}});
            state["skippedDirectories"]=json::array({
                {{"path",toUtf8((p/L"Pending").wstring())},{"reason","no-confident-game-executable"},{"status","needs-primary-exe-confirmation"}},
                {{"path",toUtf8((p/L"Utility").wstring())},{"reason","non-game-directory-excluded"},{"status","unrecognized-tool"}},
                {{"path",toUtf8((p/L"Redistributable").wstring())},{"reason","builtin-steam-tool-excluded"},{"steamAppId",228980}},
                {{"path",toUtf8((p/L"NoExe").wstring())},{"reason","no-executable"}}});
            state["summary"]={{"games",2},{"ready",1},{"needsPrimaryExeConfirmation",2},{"manualPrimaryMissing",0},
                {"nonGames",3},{"noExecutableSkipped",1}};
            writeJsonAtomic(f.scan,state);
            check(primaryCall(f,dir,next,fs::path{})==0,"Targeted primary save failed");
            const auto saved=loadJsonDocument(f.scan);
            writeJsonAtomic(p/L"primary-summary-observed.json",saved.at("summary"));
            check(saved["summary"]["games"]==2&&saved["summary"]["ready"]==2&&saved["games"][0]["primaryExecutable"]==toUtf8(next.wstring()),"Updated primary row not counted");
            check(saved["summary"]["needsPrimaryExeConfirmation"]==1&&saved["summary"]["nonGames"]==3,"Targeted primary save dropped unrelated scan counts");
            check(saved["roots"]==state["roots"]&&saved["skippedDirectories"]==state["skippedDirectories"]&&
                saved["summary"]["noExecutableSkipped"]==1,"Targeted primary save changed scan evidence");
        });});

        run("discovery-producer-primary-pending-newer-config-cannot-be-overwritten",[&](const fs::path& p){primaryFixture(p,[&](auto& f,const auto& dir,const auto& next){
            const auto third=dir/L"Newer.exe";textFile(third,"owned newer choice");const auto configPath=f.data/L"config"/L"library-config.json";auto config=loadLibraryConfig(configPath);config["manualPrimary"][canonicalPathKey(dir)]={{"exe",toUtf8(third.wstring())},{"source","user"},{"locked",true}};writeJsonAtomic(configPath,config);const auto prior=readBinaryFile(configPath),state=readBinaryFile(f.scan);
            check(primaryCall(f,dir,next,f.exe)!=0&&readBinaryFile(configPath)==prior&&readBinaryFile(f.scan)==state,"Stale editor replaced a newer durable primary whose publication was pending");
            check(primaryCall(f,dir,third,f.exe)==0&&loadJsonDocument(f.scan)["games"][0]["primaryExecutable"]==toUtf8(third.wstring()),"Retry of the same retained newer primary was not allowed");
        });});
        check(!producerOnly||(!reports.empty()&&identityCallerNetwork==0&&identityCallerProcesses==0&&discoveryProducerPriorityWrites==0&&discoveryProducerDriveDiscovery==0),"Producer selection escaped inert scope");
        check(!discoveries||(!reports.empty()&&identityCallerNetwork==0&&identityCallerProcesses==0),"Discovery selection empty or external port invoked");
        check(!publisherTexts||(!reports.empty()&&identityCallerNetwork==0&&identityCallerProcesses==0),"Publisher TXT selection empty or external port invoked");
        check(!ruleTexts||(!reports.empty()&&identityCallerNetwork==0&&identityCallerProcesses==0),"TXT selection empty or external port invoked");
        check(!dataRoots || !reports.empty(),"No data-root consumer cases selected");
        check(!artworkRepair || !reports.empty(),"No artwork repair cases selected");
        check(!snapshots || (snapshotDebt ? reports.size()==1 : !reports.empty()),"No requested snapshot cases selected");
        check(!factsOnly||(reports.size()==1&&reports[0]["name"]==factsSelected),"Exact Steam facts selection did not run one case");
        const json summary{{"allPassed",failures==0},{"caseCount",reports.size()},{"failedCount",failures},{"cases",reports},{"fixtures",toUtf8(root.wstring())},{"realSteamFilesModified",false},{"producerSelection",producerOnly},{"producerPriorityWriteAttempts",discoveryProducerPriorityWrites},{"producerAutomaticDriveDiscovery",discoveryProducerDriveDiscovery},{"discoverySelection",discoveries},{"publisherTextSelection",publisherTexts},{"ruleTextSelection",ruleTexts},{"membershipSelection",memberships},{"identitySelection",identities},{"identityNetworkCalls",identityCallerNetwork},{"identityProcessCalls",identityCallerProcesses}};
        writeJsonAtomic(root/L"summary.json",summary);std::cout<<summary.dump(2)<<"\n";return failures?1:0;
    }catch(const std::exception& e){std::cerr<<"DATA_CHAIN_TEST_FAILED: "<<e.what()<<"\n";return 1;}
}
