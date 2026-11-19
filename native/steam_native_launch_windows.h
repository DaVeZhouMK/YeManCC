#pragma once
#include "steam_native_launch.h"
#include "steam_offline_mutation_gate.h"
#include <optional>
#include "json.hpp"
#include <windows.h>
#include "steam_launch_observation.h"
#include "steam_launch_path.h"
#include "process_inventory.h"
#include <map>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <stdexcept>
#include <shellapi.h>
#include "decky_sidebar_launch.h"
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <array>
namespace ymcc::steamlaunch {
inline std::wstring userDirectory(int kind){wchar_t value[MAX_PATH]{};return SUCCEEDED(SHGetFolderPathW(nullptr,kind,nullptr,SHGFP_TYPE_CURRENT,value))?value:L"";}
inline std::wstring executablePath(){std::vector<wchar_t> value(32768);const auto size=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));return size&&size<value.size()?std::wstring(value.data(),size):L"";}
struct TextObservation { bool known=false,exists=false,utf8=false;DWORD bytes=0; };
inline bool readText(const pathfs::path& file,std::wstring& value,bool missingAllowed=false,TextObservation* observation=nullptr){
    if(observation)*observation={};
    const auto attr=GetFileAttributesW(file.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();value.clear();const bool absent=missingAllowed&&(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);if(observation&&absent)observation->known=true;return absent;}
    if(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return false;
    HANDLE h=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER length{};std::array<char,8192> bytes{};DWORD read=0;
    const bool ok=GetFileSizeEx(h,&length)&&length.QuadPart>=0&&length.QuadPart<=bytes.size()&&ReadFile(h,bytes.data(),static_cast<DWORD>(length.QuadPart),&read,nullptr)&&read==length.QuadPart;CloseHandle(h);
    const auto finish=[&](bool valid){if(valid&&observation){observation->known=true;observation->exists=true;observation->bytes=read;}return valid;};
    if(!ok)return false;if(!read){value.clear();if(observation)observation->utf8=true;return finish(true);}
    if(read>=2 && static_cast<unsigned char>(bytes[0])==0xff && static_cast<unsigned char>(bytes[1])==0xfe){if(read%2)return false;value.resize((read-2)/2);memcpy(value.data(),bytes.data()+2,read-2);return finish(value.find(L'\0')==std::wstring::npos);}
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),read,nullptr,0);if(count<=0)return false;value.assign(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),read,value.data(),count);if(observation)observation->utf8=true;return finish(value.find(L'\0')==std::wstring::npos);
}
inline bool writeText(const pathfs::path& file,const std::wstring& value){
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(size<=0&&!value.empty())return false;std::string bytes(size,'\0');if(size)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),bytes.data(),size,nullptr,nullptr);
    const auto temporary=file.wstring()+L".ymcc-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HANDLE h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool ok=WriteFile(h,bytes.data(),size,&written,nullptr)&&written==size&&FlushFileBuffers(h);CloseHandle(h);
    if(ok&&MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;DeleteFileW(temporary.c_str());return false;
}
// Preserve the installed BAT contract. The old native helper spelling can be
// migrated as a setting, but no longer grants a launch to a second process.
inline constexpr wchar_t legacyLogonFlag[]=L"--steam-logon-start";
// Migrate only recognized prior entries; normal app startup never executes them.
struct Startup {
    pathfs::path file;std::wstring command;std::vector<std::wstring> legacy;
    Startup(const std::wstring& powerControl,const std::wstring& image=executablePath(),const std::wstring& profile=userDirectory(CSIDL_PROFILE)){
        if(!profile.empty())file=pathfs::path(profile)/L".earlystart";
        if(!powerControl.empty())command=L"\""+(pathfs::path(powerControl)/L"YeManSteam.bat").wstring()+L"\"";
        if(!image.empty())legacy.push_back(lower(L"\""+image+L"\" "+legacyLogonFlag));
        for(const auto& root:{powerControl,std::wstring(LR"(C:\SOFT\YeMan\PowerControl)")})for(const auto* name:{L"YeManSteam.bat",L"YeManSteam.vbs"}){auto p=(pathfs::path(root)/name).wstring();legacy.push_back(lower(p));legacy.push_back(lower(L"\""+p+L"\""));}
    }
    bool recognized(const std::wstring& text)const{const auto value=lower(trim(text));return !command.empty()&&(value==lower(command)||std::find(legacy.begin(),legacy.end(),value)!=legacy.end());}
    bool read(std::wstring& text)const{return !file.empty()&&readText(file,text,true);}
    nlohmann::json observation()const{
        std::wstring text;TextObservation snapshot;
        const bool known=!file.empty()&&readText(file,text,true,&snapshot);
        const bool enabled=known&&recognized(text),canonical=enabled&&snapshot.exists&&snapshot.utf8&&text==command+L"\r\n";
        return {{"known",known},{"enabled",enabled},{"conflict",known&&!trim(text).empty()&&!enabled},
            {"file",{{"known",known},{"exists",known?nlohmann::json(snapshot.exists):nlohmann::json(nullptr)},
                {"bytes",known?nlohmann::json(snapshot.bytes):nlohmann::json(nullptr)},
                {"canonical",canonical},{"encoding",known&&snapshot.exists?(snapshot.utf8?"utf-8":"utf-16le"):"unknown"}}}};
    }
    bool authorized()const{std::wstring text;return read(text)&&!command.empty()&&lower(trim(text))==lower(command);}
    bool migrate()const{std::wstring text;if(!read(text))return false;if(trim(text).empty()||!recognized(text)||authorized())return true;return writeText(file,command+L"\r\n");}
    bool set(bool on,bool repairCommand=false)const{
        std::wstring text;if(command.empty()||file.empty())return false;
        const bool readable=read(text);
        if(!readable){
            // Explicit four-mode repair may replace malformed text only after
            // preserving its raw bytes. Access errors/reparse points still fail.
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if(!repairCommand||!GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&data)||
                (data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
                data.nFileSizeHigh||data.nFileSizeLow>1024u*1024u)return false;
        }
        if(!readable||(!trim(text).empty()&&!recognized(text))){
            if(!repairCommand)return false;
            static volatile LONG backupSequence=0;
            const auto backup=file.wstring()+L".ymcc-before-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(InterlockedIncrement(&backupSequence));
            if(!CopyFileW(file.c_str(),backup.c_str(),TRUE))return false;
        }
        if(!on){
            // Absence is the inverse of creating the advanced logon entry. Do
            // not leave an empty .earlystart for the shared service to consume.
            const auto attr=GetFileAttributesW(file.c_str());
            if(attr==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND;}
            if(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return false;
            if(!DeleteFileW(file.c_str()))return false;
            const auto observed=GetFileAttributesW(file.c_str());
            const auto error=GetLastError();
            return observed==INVALID_FILE_ATTRIBUTES&&(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);
        }
        const auto expected=command+L"\r\n";
        if(!writeText(file,expected))return false;
        std::wstring observed;return read(observed)&&observed==expected;
    }
};
class WindowsPorts {
    FinalDispatchGate finalDispatchGate_;
    std::function<bool()> finalDispatchCurrent_;
    LaunchFacts launchFacts_;
    std::wstring powerControl_,receiptRoot_;Startup startup_;HANDLE receipt_=INVALID_HANDLE_VALUE;std::string logon_;DWORD session_=0;deckylaunch::Handle caller_;
    std::string steamPrimaryStage_="not-attempted",steamPrimaryMode_,steamFallbackStage_="not-attempted";
    DWORD steamPrimaryError_=0,steamCleanupError_=0,steamFallbackError_=0;
    bool steamCleanupConfirmed_=false,steamFallbackAttempted_=false,manualSteamDispatched_=false;
public:
    bool hasLaunchFacts()const noexcept{return static_cast<bool>(launchFacts_);}
    bool manualSteamDispatched()const noexcept{return manualSteamDispatched_;}
    bool hasFinalDispatchGate()const noexcept{return static_cast<bool>(finalDispatchGate_)&&static_cast<bool>(finalDispatchCurrent_);}
    std::string steamLaunchFailure()const{
        return "primary="+steamPrimaryStage_+"/"+std::to_string(steamPrimaryError_)+
            ";cleanup="+(steamCleanupConfirmed_?std::string("confirmed"):std::string("none-or-unconfirmed"))+"/"+std::to_string(steamCleanupError_)+
            ";fallback="+steamFallbackStage_+"/"+std::to_string(steamFallbackError_);
    }
    nlohmann::json steamLaunchDiagnostics()const{return {{"primaryStage",steamPrimaryStage_},{"primaryMode",steamPrimaryMode_},
        {"primaryError",steamPrimaryError_},{"cleanupConfirmed",steamCleanupConfirmed_},{"cleanupError",steamCleanupError_},
        {"fallbackAttempted",steamFallbackAttempted_},{"fallbackStage",steamFallbackStage_},{"fallbackError",steamFallbackError_}};}
    explicit WindowsPorts(const std::wstring& root,const std::wstring& receiptRoot=userDirectory(CSIDL_LOCAL_APPDATA),const FinalDispatchGate& finalDispatchGate={},const std::function<bool()>& finalDispatchCurrent={},LaunchFacts launchFacts={}):finalDispatchGate_(finalDispatchGate),finalDispatchCurrent_(finalDispatchCurrent),launchFacts_(std::move(launchFacts)),powerControl_(root),receiptRoot_(receiptRoot),startup_(root){}
    ~WindowsPorts(){unlock();}
    bool bootAuthorized(){return startup_.authorized();}
    bool lock(){
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&caller_.value))return false;
        // Normalize split UAC tokens to the interactive logon's identity so a
        // medium/elevated caller shares the same receipt. Separate logons keep
        // separate files and cannot overwrite one another's consumed grant.
        deckylaunch::Handle linked;HANDLE identity=caller_.value;DWORD size=0;TOKEN_ELEVATION_TYPE type{};
        if(!GetTokenInformation(caller_.value,TokenElevationType,&type,sizeof(type),&size))return false;
        if(type==TokenElevationTypeFull){TOKEN_LINKED_TOKEN token{};if(!GetTokenInformation(caller_.value,TokenLinkedToken,&token,sizeof(token),&size))return false;linked.value=token.LinkedToken;identity=linked.value;}
        TOKEN_STATISTICS stats{};if(!GetTokenInformation(identity,TokenStatistics,&stats,sizeof(stats),&size)||!GetTokenInformation(identity,TokenSessionId,&session_,sizeof(session_),&size))return false;
        logon_=std::to_string(stats.AuthenticationId.HighPart)+"-"+std::to_string(stats.AuthenticationId.LowPart)+"-"+std::to_string(session_);
        const auto& local=receiptRoot_;if(local.empty())return false;
        const auto directory=pathfs::path(local)/L"YeManCC"/L"steam-launch";std::error_code ec;pathfs::create_directories(directory,ec);if(ec)return false;
        if(GetFileAttributesW(directory.c_str())&FILE_ATTRIBUTE_REPARSE_POINT)return false;
        receipt_=CreateFileW((directory/pathfs::path("launch-"+logon_+".receipt")).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(receipt_==INVALID_HANDLE_VALUE)return false;BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(receipt_,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){unlock();return false;}return true;
    }
    void unlock(){if(receipt_!=INVALID_HANDLE_VALUE){CloseHandle(receipt_);receipt_=INVALID_HANDLE_VALUE;}}
    bool readLedger(Ledger& state){
        LARGE_INTEGER zero{},size{};if(!SetFilePointerEx(receipt_,zero,nullptr,FILE_BEGIN)||!GetFileSizeEx(receipt_,&size)||size.QuadPart>1024)return false;if(!size.QuadPart)return true;
        std::array<char,1024> bytes{};DWORD read=0;if(!ReadFile(receipt_,bytes.data(),static_cast<DWORD>(size.QuadPart),&read,nullptr)||read!=size.QuadPart)return false;
        try{const auto data=nlohmann::json::parse(bytes.data(),bytes.data()+read);if(data.at("logon").get<std::string>()!=logon_)return true;state.bootConsumed=data.at("boot").get<bool>();state.dispatched=data.at("dispatched").get<bool>();state.dispatchedAt=data.at("at").get<uint64_t>();return true;}catch(...){return false;}
    }
    bool saveLedger(const Ledger& state){const auto data=nlohmann::json{{"logon",logon_},{"boot",state.bootConsumed},{"dispatched",state.dispatched},{"at",state.dispatchedAt}}.dump();LARGE_INTEGER zero{};DWORD written=0;return SetFilePointerEx(receipt_,zero,nullptr,FILE_BEGIN)&&WriteFile(receipt_,data.data(),static_cast<DWORD>(data.size()),&written,nullptr)&&written==data.size()&&SetEndOfFile(receipt_)&&FlushFileBuffers(receipt_);}
    uint64_t now(){return GetTickCount64();}
    std::wstring steamExecutable(){return hasLaunchFacts()?launchFacts_.executable():std::wstring{};}
    std::vector<std::wstring> addons(){
        std::set<std::wstring> unique;const auto directory=pathfs::path(powerControl_)/L"YeManSteam";std::error_code ec;
        const auto attributes=GetFileAttributesW(directory.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return {};
        unsigned visited=0;for(pathfs::directory_iterator it(directory,ec),end;!ec&&it!=end;it.increment(ec)){
            if(++visited>512)break;const auto path=it->path();if(lower(path.extension().wstring())!=L".txt")continue;std::wstring text;if(!readText(path,text))continue;
            auto value=trim(text.substr(0,text.find_first_of(L"\r\n")));const auto basename=lower(pathfs::path(value).filename().wstring());
            // CSS has one owner. A custom filename cannot bypass the exclusion.
            if(basename==L"cssloader-standalone-headless.exe"||basename==L"steam.exe")continue;if(validExe(value))unique.insert(lower(value));
        }return {unique.begin(),unique.end()};
    }
    Presence snapshot(const std::vector<std::wstring>& addons){
        std::map<std::wstring,std::vector<std::wstring>> wanted;
        for(const auto& addon:addons)wanted[lower(pathfs::path(addon).filename().wstring())].push_back(addon);
        Presence result;
        if(!hasLaunchFacts())return result;
        const auto observed=launchFacts_.observe(0,!addons.empty());
        if(observed.steam!=0&&observed.steam!=1)return result;
        if(!addons.empty()&&(!observed.addonRows||observed.addonRows->status!=processInventory::Status::Complete))return result;
        result.steam=observed.steam;
        if(addons.empty())return result;
        for(const auto& entry:observed.addonRows->rows){
            const auto basename=lower(entry.name);
            if(basename==L"steam.exe")continue;
            const auto found=wanted.find(basename);if(found==wanted.end())continue;const auto& matches=found->second;
            deckylaunch::Handle process,token;process.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.pid);
            if(!process.value||!OpenProcessToken(process.value,TOKEN_QUERY,&token.value)){result.uncertain.insert(matches.begin(),matches.end());continue;}
            if(!deckylaunch::equalUser(caller_.value,token.value)||!deckylaunch::sameSession(caller_.value,token.value))continue;
            std::array<wchar_t,32768> path{};DWORD count=static_cast<DWORD>(path.size());if(!QueryFullProcessImageNameW(process.value,0,path.data(),&count)){result.uncertain.insert(matches.begin(),matches.end());continue;}
            const auto actual=lower(std::wstring(path.data(),count));for(const auto& addon:matches)if(addon==actual)result.active.insert(addon);
        }return result;
    }
    bool absentExcept(DWORD ownChild)const{return hasLaunchFacts()&&launchFacts_.observe(ownChild,false).steam==0;}
    SteamSpawnOutcome spawnPrimary(const std::wstring& executable,const std::wstring& arguments,bool steam,bool manual=false){
        if(steam&&!hasLaunchFacts()){
            steamPrimaryStage_="steam-launch-source-unavailable";steamPrimaryError_=ERROR_NOT_READY;
            return SteamSpawnOutcome::NotCreated;
        }
        // Own the file-mutation boundary before any suspended Steam child
        // exists. The original receipt/current/power/census gates still apply.
        std::optional<ymcc::steamoffline::Guard> offline;
        if(steam){
            offline.emplace(0);
            if(!*offline){steamPrimaryStage_="steam-library-write-busy";steamPrimaryError_=offline->error();return SteamSpawnOutcome::NotCreated;}
        }
        std::wstring command=L"\""+executable+L"\""+arguments;STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=steam?SW_SHOWNORMAL:SW_HIDE;PROCESS_INFORMATION child{};
        const auto directory=pathfs::path(executable).parent_path().wstring();
        if(manual&&finalDispatchCurrent_&&!finalDispatchCurrent_()){
            steamPrimaryError_=ERROR_CANCELLED;
            try{steamPrimaryStage_="final-dispatch-current-refused";}catch(...){}
            return SteamSpawnOutcome::Uncertain;
        }
        const auto created=deckylaunch::create(executable,command,nullptr,directory,startup,child);
        if(steam){
            steamPrimaryError_=created.error;
            try{steamPrimaryStage_=created.stage;steamPrimaryMode_=created.mode;}catch(...){}
        }
        if(!created.created){
            // child-token-verification closes its handles after best-effort
            // cleanup inside the shared creator; this caller cannot prove exit.
            // Unknown/new failure stages also fail closed, never gain a retry.
            static constexpr const char* noChildStages[]={"caller-token-open","caller-token-elevation","caller-token-integrity","create-process",
                "caller-token-elevation-type","linked-token-query","linked-token-identity","shell-not-available","shell-process-open","shell-image-query","shell-windows-path",
                "shell-image-mismatch","shell-token-open","shell-token-type","shell-token-elevation","shell-token-elevated","shell-token-integrity","shell-token-identity","shell-token-duplicate","primary-token-identity","shell-process-changed","create-process-with-token"};
            const bool known=std::any_of(std::begin(noChildStages),std::end(noChildStages),[&](const char* stage){return created.stage==stage;});
            return known&&!child.hProcess&&!child.hThread&&!child.dwProcessId&&!child.dwThreadId?SteamSpawnOutcome::NotCreated:SteamSpawnOutcome::Uncertain;
        }
        // Only this still-suspended owned child can be terminated. Do not
        // replay Steam after an ambiguous creation or failed destruction.
        const char* finalStage=nullptr;
        DWORD finalError=ERROR_SUCCESS;
        const auto dispatch=dispatchFinalOnce(finalDispatchGate_,[&] {
            // The gate has acquired/validated the caller's settings/power scope
            // before this final fresh absence check. Do not move it outside.
            const bool admitted=!steam||absentExcept(child.dwProcessId);
            if(!admitted){finalStage="steam-absence-unverified";finalError=ERROR_BUSY;return false;}
            if(finalDispatchCurrent_) {
                finalStage="final-dispatch-current-refused";finalError=ERROR_CANCELLED;
                if(!finalDispatchCurrent_())return false;
            }
            const bool resumed=ResumeThread(child.hThread)!=static_cast<DWORD>(-1);
            // After the Kernel call, only nonthrowing scalar/pointer stores and
            // return may precede the helper's authoritative terminal capture.
            finalError=resumed?ERROR_SUCCESS:GetLastError();
            finalStage=resumed?"resumed":"resume-thread";
            return resumed;
        });
        const bool resumed=dispatch.resumed;
        if(!dispatch.invoked){finalStage=dispatch.threw?"final-dispatch-gate-exception":"final-dispatch-gate-refused";finalError=ERROR_CANCELLED;}
        // Both success and absence-failure diagnostics are outside the gate.
        // An allocation failure cannot change resume truth, erase its saved
        // Win32 error, skip owned cleanup, or terminate an already running child.
        if(steam&&finalStage){
            steamPrimaryError_=finalError;
            try {steamPrimaryStage_=finalStage;}catch(...) {}
        }
        // The synchronous gate and its locks have returned/unwound. Its bool
        // cannot erase a successful ResumeThread or authorize an uncalled leaf.
        bool destroyed=false;
        if(!resumed){
            destroyed=WaitForSingleObject(child.hProcess,0)==WAIT_OBJECT_0;
            if(!destroyed){
                const bool terminated=TerminateProcess(child.hProcess,ERROR_CANCELLED)!=FALSE;
                if(steam&&!terminated)steamCleanupError_=GetLastError();
                destroyed=WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0;
                if(steam&&!destroyed&&!steamCleanupError_)steamCleanupError_=ERROR_TIMEOUT;
            }
        }
        if(steam&&!resumed)steamCleanupConfirmed_=destroyed;
        CloseHandle(child.hThread);CloseHandle(child.hProcess);
        return resumed?SteamSpawnOutcome::Dispatched:destroyed?SteamSpawnOutcome::OwnedChildDestroyed:SteamSpawnOutcome::Uncertain;
    }
    bool spawn(const std::wstring& executable,const std::wstring& arguments,bool steam){return spawnPrimary(executable,arguments,steam)==SteamSpawnOutcome::Dispatched;}
    SteamSpawnOutcome spawnSteamPrimary(const std::wstring& executable,const std::wstring& arguments){return spawnPrimary(executable,arguments,true,true);}
    bool manualSteamFallbackAllowed(const std::wstring& executable){
        if(finalDispatchCurrent_&&!finalDispatchCurrent_()){
            steamFallbackStage_="final-dispatch-current-refused";steamFallbackError_=ERROR_CANCELLED;return false;
        }
        // Resolve and validate again; the fallback is neither an arbitrary
        // command line nor authority to reopen an already running Steam.
        if(!validExe(executable)||lower(pathfs::path(executable).filename().wstring())!=L"steam.exe"){
            steamFallbackStage_="steam-path-invalid";steamFallbackError_=ERROR_FILE_NOT_FOUND;return false;
        }
        if(lower(steamExecutable())!=lower(executable)){
            steamFallbackStage_="steam-path-changed";steamFallbackError_=ERROR_INVALID_DATA;return false;
        }
        if(!absentExcept(0)){
            steamFallbackStage_="steam-absence-unverified";steamFallbackError_=ERROR_BUSY;return false;
        }
        return true;
    }
    bool spawnSteamShell(const std::wstring& executable,const std::wstring& arguments){
        if(!manualSteamArgumentsAllowed(arguments))return false;
        ymcc::steamoffline::Guard offline;
        if(!offline){steamFallbackStage_="steam-library-write-busy";steamFallbackError_=offline.error();return false;}
        if(!manualSteamFallbackAllowed(executable))return false;
        const auto directory=pathfs::path(executable).parent_path().wstring();
        SHELLEXECUTEINFOW request{sizeof(request)};
        request.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
        request.lpVerb=L"open";request.lpFile=executable.c_str();request.lpParameters=arguments.c_str();request.lpDirectory=directory.c_str();request.nShow=SW_SHOWNORMAL;
        if(finalDispatchCurrent_&&!finalDispatchCurrent_()){
            steamFallbackStage_="final-dispatch-current-refused";steamFallbackError_=ERROR_CANCELLED;return false;
        }
        steamFallbackAttempted_=true;
        const bool dispatched=ShellExecuteExW(&request)!=FALSE;
        steamFallbackError_=dispatched?ERROR_SUCCESS:GetLastError();
        if(request.hProcess)CloseHandle(request.hProcess);
        try {steamFallbackStage_=dispatched?"shell-dispatched":"shell-execute";}catch(...) {}
        // Shell acceptance is dispatch evidence only, never Steam/UI ready.
        return dispatched;
    }
    bool spawnManualSteam(const std::wstring& executable,const std::wstring& arguments){
        const bool accepted=dispatchManualSteam(*this,executable,arguments);
        manualSteamDispatched_=manualSteamDispatched_||accepted;
        return accepted;
    }
};
inline std::wstring selectedManualExecutable(const nlohmann::json& args){
    if(!args.is_object()||!args.contains("userInitiated")||!args["userInitiated"].is_boolean()||!args["userInitiated"].get<bool>())throw std::runtime_error("Steam launch requires an explicit user action");
    for(auto it=args.begin();it!=args.end();++it)if(it.key()!="userInitiated"&&it.key()!="selectedExecutable")throw std::runtime_error("Invalid Steam launch argument");
    if(!args.contains("selectedExecutable"))return {};
    const auto& field=args["selectedExecutable"];
    if(!field.is_string())throw std::runtime_error("Invalid selected Steam executable");
    const auto text=field.get<std::string>();
    if(text.empty()||text.size()>131068||text.find_first_of(std::string("\0\r\n\"",4))!=std::string::npos)throw std::runtime_error("Invalid selected Steam executable");
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(count<=0||count>32767)throw std::runtime_error("Invalid selected Steam executable");
    std::wstring selected(count,L'\0');
    if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),selected.data(),count)!=count||
       !pathfs::path(selected).is_absolute()||lower(pathfs::path(selected).filename().wstring())!=L"steam.exe")throw std::runtime_error("Invalid selected Steam executable");
    return selected;
}
inline bool refuseLegacyLogonCommand(const wchar_t* line,int& exitCode) noexcept {
    int count=0;auto args=CommandLineToArgvW(line,&count);if(!args)return false;
    bool present=false;
    for(int i=1;i<count;++i)if(wcscmp(args[i],legacyLogonFlag)==0){present=true;break;}
    LocalFree(args);
    if(!present)return false;
    // Exact legacy calls used exit 3 when the current BAT authorization was
    // absent. Preserve that refusal even if another process just migrated it.
    exitCode=count==2?3:2;return true;
}
}
