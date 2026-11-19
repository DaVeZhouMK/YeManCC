#pragma once
// Included only by the YMCC main native executable. Child clients have neither
// the registry producer nor a fallback Steam observer/process inventory.
#include "steam_information_client.h"
#include "steam_client_peer_ownership.h"
#include "steam_source_owner.h"
#include "steam_account_facts.h"
#include "steam_launch_owner_observation.h"
#include "steam_launch_path.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace ymcc::steaminfo {
// A one-shot foreground routing hint for an already admitted resident caller.
// Never infer it from registry/process names alone or enumerate processes.
inline bool readSteamFamilyForeground(steamsession::Observer& observer) noexcept {
    try {
        const auto reading=observer.sourceRead();
        if(!reading||!reading.current())return false;
        const auto identity=observer.identitySnapshot();
        if(!identity||identity->presence!=1||identity->rootReadError!=ERROR_SUCCESS||
            !identity->windowsSessionKnown)return false;
        const auto& session=identity->session;
        if(!session.pid||!session.created||session.root.empty())return false;
        const HWND window=GetForegroundWindow();
        DWORD pid=0,windowSession=0;
        if(!window||!GetWindowThreadProcessId(window,&pid)||!pid||
            !ProcessIdToSessionId(pid,&windowSession)||windowSession!=identity->windowsSession)return false;
        // Hold a foreground helper until the final window check. Only the
        // observer checks the already-pinned primary process.
        Peer helper;
        if(pid!=session.pid){
            if(!helper.load(pid)||helper.session!=windowSession)return false;
            const auto path=pathKey(helper.path),prefix=pathKey(session.root)+L"\\";
            const auto leaf=path.find_last_of(L'\\');
            if(!path.starts_with(prefix)||leaf==std::wstring::npos||path.substr(leaf+1)!=L"steamwebhelper.exe")return false;
        }
        DWORD finalPid=0;
        return GetForegroundWindow()==window&&
            GetWindowThreadProcessId(window,&finalPid)&&finalPid==pid&&
            (pid==session.pid||helper.live())&&observer.liveIdentityHint(reading,identity);
    }catch(...){return false;}
}
namespace launchDetail {
using steamlaunch::trim;
using steamlaunch::lower;
using steamlaunch::validExe;
namespace pathfs=std::filesystem;
inline std::wstring registryText(HKEY key,const wchar_t* sub,const wchar_t* name,DWORD view=0){std::array<wchar_t,32768> value{};DWORD size=sizeof(value);return RegGetValueW(key,sub,name,RRF_RT_REG_SZ|view,nullptr,value.data(),&size)==ERROR_SUCCESS?trim(value.data()):L"";}
template<class ReadText>
inline std::wstring steamExecutable(const ReadText& read){
    auto value=read(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamExe",0);
    std::replace(value.begin(),value.end(),L'/',L'\\');
    if(validExe(value)&&lower(pathfs::path(value).filename().wstring())==L"steam.exe")return value;
    const auto fromRoot=[](std::wstring root)->std::wstring{
        if(root.empty())return {};
        auto image=(pathfs::path(root)/L"steam.exe").wstring();
        return validExe(image)?std::move(image):std::wstring{};
    };
    value=fromRoot(read(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",0));
    if(!value.empty())return value;
    value=fromRoot(read(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Valve\\Steam",L"InstallPath",RRF_SUBKEY_WOW6432KEY));
    if(!value.empty())return value;
    return fromRoot(read(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Valve\\Steam",L"InstallPath",RRF_SUBKEY_WOW6464KEY));
}
} // namespace launchDetail

// The caller must keep its read and the observer alive until the synchronous
// launch attempt returns. The request predicate is copied into both endpoints.
inline steamlaunch::LaunchFacts bindLaunchFacts(steamsession::Observer& observer,
    const steamsession::SourceOwner::Read& reading,std::function<bool()> requestCurrent) {
    const auto current=[&observer,&reading,request=std::move(requestCurrent)] {
        return request&&request()&&observer.sourceReadCurrent(reading);
    };
    return {
        [current](DWORD child,bool addons){
            return launchDetail::observe(child,addons,current,[]{
                return processInventory::read(processInventory::resolveQuery(),processInventory::currentTime,0);
            });
        },
        [current]() -> std::wstring {
            try {
                if(!current())return {};
                auto value=launchDetail::steamExecutable(launchDetail::registryText);
                return current()?std::move(value):std::wstring{};
            }catch(...){return {};}
        }
    };
}

namespace updateHookDetail {
// Keep the updater's old SteamPath-only lookup and raw REG_SZ limits. This
// selects shutdown candidates, not a launchable Steam image or a process lease.
inline std::wstring registryRoot(HKEY hive,const wchar_t* key=L"Software\\Valve\\Steam"){
    HKEY opened=nullptr;
    if(RegOpenKeyExW(hive,key,0,KEY_READ,&opened)!=ERROR_SUCCESS)return {};
    wchar_t value[520]{};
    DWORD bytes=sizeof(value)-sizeof(wchar_t),type=0;
    const auto status=RegQueryValueExW(opened,L"SteamPath",nullptr,&type,reinterpret_cast<BYTE*>(value),&bytes);
    RegCloseKey(opened);
    return status==ERROR_SUCCESS&&type==REG_SZ&&value[0]?std::wstring(value):std::wstring{};
}
template<class Current,class ReadRoot>
inline std::wstring selectRoot(const Current& current,const ReadRoot& read){
    if(!current())return {};
    auto root=read(HKEY_CURRENT_USER);
    if(root.empty()){
        if(!current())return {};
        root=read(HKEY_LOCAL_MACHINE);
    }
    return current()?std::move(root):std::wstring{};
}
} // namespace updateHookDetail

inline std::wstring readUpdateHookInstallRoot(steamsession::Observer& observer){
    try{
        const auto reading=observer.sourceRead();
        return updateHookDetail::selectRoot([&]{return observer.sourceReadCurrent(reading);},
            [](HKEY hive){return updateHookDetail::registryRoot(hive);});
    }catch(...){return {};}
}

inline std::vector<Installation> installationCandidates(const std::shared_ptr<const steamsession::IdentitySnapshot>& /*identity*/){
    std::vector<Installation> result;
    auto registry=[&](HKEY hive,const wchar_t* key,const wchar_t* name,const char* source){
        wchar_t value[32768]{};DWORD size=sizeof(value);const auto error=RegGetValueW(hive,key,name,RRF_RT_REG_SZ,nullptr,value,&size);
        std::wstring text=error==ERROR_SUCCESS?value:L"";
        if(text.size()>=2&&text.front()==L'"'&&text.back()==L'"')text=text.substr(1,text.size()-2);
        std::replace(text.begin(),text.end(),L'/',L'\\');
        return Installation{utf8(text),source,static_cast<std::uint32_t>(error)};
    };
    // Installation metadata is a fresh one-shot observation, including while
    // Steam is closed. A retained session root cannot select an offline export.
    auto primary=registry(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath","hkcu-steam-path");
    if(primary.error!=ERROR_SUCCESS||primary.path.empty()){
        const auto executable=registry(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamExe","hkcu-steam-exe");
        if(executable.error==ERROR_SUCCESS&&!executable.path.empty()){
            const auto image=std::filesystem::u8path(executable.path);
            if(image.is_absolute()&&_wcsicmp(image.filename().c_str(),L"steam.exe")==0)
                primary={utf8(image.parent_path().wstring()),executable.source,0};
        }
    }
    result.push_back(std::move(primary)); // Preserve the seven-entry wire bound.
    result.push_back(registry(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"InstallPath","hkcu-install-path"));
    auto machineInstallation=[&](const wchar_t* key,const char* installSource,const char* steamSource){
        auto candidate=registry(HKEY_LOCAL_MACHINE,key,L"InstallPath",installSource);
        if(candidate.error!=ERROR_SUCCESS||candidate.path.empty()){
            auto fallback=registry(HKEY_LOCAL_MACHINE,key,L"SteamPath",steamSource);
            if(fallback.error==ERROR_SUCCESS&&!fallback.path.empty())candidate=std::move(fallback);
        }
        return candidate; // Keep the original InstallPath diagnostic if both reads fail.
    };
    result.push_back(machineInstallation(L"Software\\WOW6432Node\\Valve\\Steam","hklm32-install-path","hklm32-steam-path"));
    result.push_back(machineInstallation(L"Software\\Valve\\Steam","hklm-install-path","hklm-steam-path"));
    auto program=[&](const wchar_t* name,const wchar_t* fallback,const char* source){
        wchar_t value[32768]{};const auto size=GetEnvironmentVariableW(name,value,static_cast<DWORD>(std::size(value)));
        const auto root=size&&size<std::size(value)?std::filesystem::path(value):std::filesystem::path(fallback);
        result.push_back({utf8((root/L"Steam").wstring()),source,0});
    };
    program(L"ProgramFiles(x86)",L"C:\\Program Files (x86)","programfiles-x86-default");program(L"ProgramFiles",L"C:\\Program Files","programfiles-default");
    result.push_back({"C:\\Steam","legacy-default",0});return result;
}
inline void collectPresence(Facts& result,const processInventory::Snapshot& table,const steamsession::Session& observed,DWORD activePid=0){
    if(table.status!=processInventory::Status::Complete)return;
    result.presence=0;
    // The sole publisher supplies a diagnostic priority hint. A retained hint
    // cannot prove presence/absence or authorize an action: those facts still
    // require this complete fresh table and the existing per-process verifier.
    const auto qualify=[&](DWORD pid,bool requireObservedBirth){
        Peer process;
        if(!process.load(pid)||pathKey(std::filesystem::path(process.path).filename().wstring())!=L"steam.exe")return false;
        if(requireObservedBirth&&(!observed.created||process.birth!=observed.created))return false;
        result.processPid=process.pid;result.processBirth=process.birth;result.processPath=utf8(process.path);
        return true;
    };
    // Defer only matching names behind the registered hint, in table order.
    // Absence and the usual single-candidate case need no candidate allocation.
    std::array<const processInventory::Entry*,2> first{};
    std::array<std::vector<const processInventory::Entry*>,2> remaining;
    for(const auto& row:table.rows){
        if(_wcsicmp(row.name.c_str(),L"steam.exe"))continue;
        result.presence=1;
        if(result.processPid)return;
        if(row.pid==activePid){
            if(qualify(row.pid,false))return;
            continue;
        }
        const auto rank=row.pid==observed.pid?0u:1u;
        if(!first[rank])first[rank]=&row;
        else remaining[rank].push_back(&row);
    }
    for(unsigned rank=0;rank<first.size();++rank){
        if(first[rank]&&qualify(first[rank]->pid,rank==0))return;
        for(const auto* row:remaining[rank])if(qualify(row->pid,rank==0))return;
    }
}
inline Facts collect(std::uint32_t mask,steamsession::Observer& observer,const std::function<Json(const steamsession::Session&)>& mode,std::shared_ptr<const processInventory::Snapshot> table={},const std::function<StartupWindowFacts(const std::shared_ptr<const steamsession::IdentitySnapshot>&)>& windowReader={},const steamsession::SourceOwner::Read* borrowed=nullptr){
    Facts result;result.requested=mask;
    // A synchronous ingress can lend its admission through collection and commit.
    const auto owned=borrowed?steamsession::SourceOwner::Read{}:observer.sourceRead();
    const auto& reading=borrowed?*borrowed:owned;
    if(borrowed?!observer.sourceReadCurrent(reading):!reading)return result;
    result.available=true;
    const auto identity=(mask&(Session|Presence|StartupWindow))?observer.identitySnapshot():nullptr;
    if(mask&Installations)result.installations=installationCandidates(identity);
    if(mask&Session){
        if(identity){
            const auto projected=steamaccount::project(*identity,mode(identity->session));
            if(observer.sameIdentityEpoch(identity)){
                result.root=utf8(identity->registryRoot);result.rootError=identity->rootReadError;
                result.registryAccount=identity->session.account;result.accountError=identity->accountReadError;
                result.uiAccountKnown=projected.ui.currentAccountKnown()&&projected.ui.deckyAccountWatchAvailable==steamaccount::Truth::True;if(result.uiAccountKnown)result.uiAccount=projected.ui.accountID32;
                result.identityRevision=identity->revision;result.processEpoch=identity->processEpoch;result.accountEpoch=identity->accountEpoch;
            }
        }else observer.requestIdentityRefresh();
    }
    if(mask&Presence){
        if(!table)table=processInventory::read(processInventory::resolveQuery(),processInventory::currentTime,0);
        if(table)collectPresence(result,*table,identity?identity->session:steamsession::Session{},
            identity&&identity->registeredPidReadError==ERROR_SUCCESS?identity->registeredPid:0);
    }
    if((mask&StartupWindow)&&identity&&windowReader){
        StartupWindowFacts window;
        if(identity->presence==0){
            // Only the sole observer's existing complete-table publication can
            // supply absence. An unavailable source never reaches this branch.
            window.completed=true;window.observedAt=GetTickCount64();
            window.processEpoch=identity->processEpoch;window.accountEpoch=identity->accountEpoch;
        }else window=windowReader(identity);
        if(observer.sameIdentityEpoch(identity)){
            result.startupWindow=std::move(window);
            result.identityRevision=identity->revision;result.processEpoch=identity->processEpoch;result.accountEpoch=identity->accountEpoch;
            if(identity->presence==0)result.presence=0;
            else if(identity->presence==1&&identity->session.pid&&identity->session.created&&
                !identity->session.root.empty()&&identity->rootReadError==ERROR_SUCCESS){
                result.presence=1;result.processPid=identity->session.pid;result.processBirth=identity->session.created;
                result.processPath=utf8(identity->session.root+L"\\steam.exe");
            }
        }
    }
    if(!reading.current()){Facts unavailable;unavailable.requested=mask;return unavailable;}
    result.observedAt=GetTickCount64();return result;
}
using AcquireRead=std::function<steamsession::SourceOwner::Read()>;
using Reader=std::function<Facts(std::uint32_t,const std::shared_ptr<const processInventory::Snapshot>&,const steamsession::SourceOwner::Read&)>;
inline LRESULT serve(HWND providerWindow,HWND sender,const COPYDATASTRUCT* data,const Reader& reader,const AcquireRead& acquireRead)noexcept{
    try{
        if(!data||data->dwData!=messageTag||data->cbData!=sizeof(Request)||!data->lpData||!sender)return FALSE;
        Request request;std::memcpy(&request,data->lpData,sizeof(request));
        if(request.version!=protocol||request.size!=sizeof(Request)||!request.mask||(request.mask&~Supported)||
            ((request.mask&StartupWindow)&&request.mask!=StartupWindow)||
            std::all_of(request.nonce.begin(),request.nonce.end(),[](auto n){return n==0;}))return FALSE;
        if(!acquireRead)return FALSE;
        const auto reading=acquireRead();if(!reading||!reading.current())return FALSE;
        DWORD callerPid=0,providerPid=0;GetWindowThreadProcessId(sender,&callerPid);GetWindowThreadProcessId(providerWindow,&providerPid);
        if(providerPid!=GetCurrentProcessId()||callerPid!=request.callerPid)return FALSE;
        Peer provider,caller;Ancestry ancestry;if(!provider.load(providerPid)||!caller.load(callerPid)||caller.birth!=request.callerBirth)return FALSE;
        std::shared_ptr<const processInventory::Snapshot> table;
        if(request.mask&StartupWindow){
            if(request.mask!=StartupWindow||!provider.sameUser(caller)||!gamingHomePackagePeer(caller))return FALSE;
        }else if(!authorizeLibraryCaller(provider,caller,ancestry,LibraryCaller::HostOrWorker,&table))return FALSE;
        Handle mapping(OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,mappingName(request).c_str()));if(!mapping.value)return FALSE;
        View view;view.value=MapViewOfFile(mapping.value,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Wire));if(!view.value)return FALSE;auto* wire=static_cast<Wire*>(view.value);
        if(!matches(wire->request,request)||wire->capacity!=payloadCapacity||wire->bytes||wire->providerPid||wire->providerBirth||wire->committed)return FALSE;
        if(!reading.current())return FALSE;
        const auto facts=reader(request.mask,table,reading);const auto body=encode(facts).dump();if(!reading.current()||!facts.available||body.empty()||body.size()>=payloadCapacity||!provider.live()||!caller.live()||!ancestry.live())return FALSE;
        DWORD finalPid=0;GetWindowThreadProcessId(sender,&finalPid);if(!reading.current()||finalPid!=caller.pid||!matches(wire->request,request)||wire->committed)return FALSE;
        std::memcpy(wire->payload,body.data(),body.size());wire->bytes=static_cast<std::uint32_t>(body.size());wire->providerPid=provider.pid;wire->providerBirth=provider.birth;
        MemoryBarrier();InterlockedExchange(&wire->committed,1);return protocol;
    }catch(...){return FALSE;}
}
} // namespace ymcc::steaminfo
