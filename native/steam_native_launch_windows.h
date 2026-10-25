#pragma once
#include "steam_native_launch.h"
#include "json.hpp"
#include <windows.h>
#include <tlhelp32.h>
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
namespace pathfs=std::filesystem;
inline std::wstring lower(std::wstring value){std::replace(value.begin(),value.end(),L'/',L'\\');for(auto& c:value)c=std::towlower(c);return value;}
inline std::wstring trim(std::wstring value){const auto first=value.find_first_not_of(L" \r\n\t\ufeff");if(first==std::wstring::npos)return {};return value.substr(first,value.find_last_not_of(L" \r\n\t")-first+1);}
inline std::wstring userDirectory(int kind){wchar_t value[MAX_PATH]{};return SUCCEEDED(SHGetFolderPathW(nullptr,kind,nullptr,SHGFP_TYPE_CURRENT,value))?value:L"";}
inline std::wstring executablePath(){std::vector<wchar_t> value(32768);const auto size=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));return size&&size<value.size()?std::wstring(value.data(),size):L"";}
inline bool readText(const pathfs::path& file,std::wstring& value,bool missingAllowed=false){
    const auto attr=GetFileAttributesW(file.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();value.clear();return missingAllowed&&(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND);}
    if(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return false;
    HANDLE h=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER length{};std::array<char,8192> bytes{};DWORD read=0;
    const bool ok=GetFileSizeEx(h,&length)&&length.QuadPart>=0&&length.QuadPart<=bytes.size()&&ReadFile(h,bytes.data(),static_cast<DWORD>(length.QuadPart),&read,nullptr)&&read==length.QuadPart;CloseHandle(h);
    if(!ok)return false;if(!read){value.clear();return true;}
    if(read>=2 && static_cast<unsigned char>(bytes[0])==0xff && static_cast<unsigned char>(bytes[1])==0xfe){if(read%2)return false;value.resize((read-2)/2);memcpy(value.data(),bytes.data()+2,read-2);return value.find(L'\0')==std::wstring::npos;}
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),read,nullptr,0);if(count<=0)return false;value.assign(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),read,value.data(),count);return value.find(L'\0')==std::wstring::npos;
}
inline bool writeText(const pathfs::path& file,const std::wstring& value){
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(size<=0&&!value.empty())return false;std::string bytes(size,'\0');if(size)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),bytes.data(),size,nullptr,nullptr);
    const auto temporary=file.wstring()+L".ymcc-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HANDLE h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool ok=WriteFile(h,bytes.data(),size,&written,nullptr)&&written==size&&FlushFileBuffers(h);CloseHandle(h);
    if(ok&&MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;DeleteFileW(temporary.c_str());return false;
}
inline bool validExe(const std::wstring& value){
    if(value.size()<7||value.find_first_of(L"\"\r\n\0",0,4)!=std::wstring::npos)return false;
    const pathfs::path path(value);if(!path.is_absolute()||lower(path.extension().wstring())!=L".exe")return false;
    const auto attr=GetFileAttributesW(path.c_str());return attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_DIRECTORY);
}
// Advanced logon startup preserves the user's YeManSteam.bat contract: that
// script also starts the complete YMCC backend. Manual Steam launch remains native.
// Migrate only recognized prior entries; normal app startup never executes them.
struct Startup {
    pathfs::path file;std::wstring command;std::vector<std::wstring> legacy;
    Startup(const std::wstring& powerControl,const std::wstring& image=executablePath(),const std::wstring& profile=userDirectory(CSIDL_PROFILE)){
        if(!profile.empty())file=pathfs::path(profile)/L".earlystart";
        if(!powerControl.empty())command=L"\""+(pathfs::path(powerControl)/L"YeManSteam.bat").wstring()+L"\"";
        if(!image.empty())legacy.push_back(lower(L"\""+image+L"\" --steam-logon-start"));
        for(const auto& root:{powerControl,std::wstring(LR"(C:\SOFT\YeMan\PowerControl)")})for(const auto* name:{L"YeManSteam.bat",L"YeManSteam.vbs"}){auto p=(pathfs::path(root)/name).wstring();legacy.push_back(lower(p));legacy.push_back(lower(L"\""+p+L"\""));}
    }
    bool recognized(const std::wstring& text)const{const auto value=lower(trim(text));return !command.empty()&&(value==lower(command)||std::find(legacy.begin(),legacy.end(),value)!=legacy.end());}
    bool read(std::wstring& text)const{return !file.empty()&&readText(file,text,true);}
    bool authorized()const{std::wstring text;return read(text)&&!command.empty()&&lower(trim(text))==lower(command);}
    bool migrate()const{std::wstring text;if(!read(text))return false;if(trim(text).empty()||!recognized(text)||authorized())return true;return writeText(file,command+L"\r\n");}
    bool set(bool on,bool repairCommand=false)const{
        std::wstring text;if(command.empty()||!read(text))return false;
        if(!trim(text).empty()&&!recognized(text)){
            if(!repairCommand)return false;
            static volatile LONG backupSequence=0;
            const auto backup=file.wstring()+L".ymcc-before-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(InterlockedIncrement(&backupSequence));
            if(!CopyFileW(file.c_str(),backup.c_str(),TRUE))return false;
        }
        const auto expected=on?command+L"\r\n":std::wstring{};
        if(!writeText(file,expected))return false;
        std::wstring observed;return read(observed)&&observed==expected;
    }
};
inline std::wstring registryText(HKEY key,const wchar_t* sub,const wchar_t* name,DWORD view=0){std::array<wchar_t,32768> value{};DWORD size=sizeof(value);return RegGetValueW(key,sub,name,RRF_RT_REG_SZ|view,nullptr,value.data(),&size)==ERROR_SUCCESS?trim(value.data()):L"";}
class WindowsPorts {
    std::wstring powerControl_,receiptRoot_;Startup startup_;HANDLE receipt_=INVALID_HANDLE_VALUE;std::string logon_;DWORD session_=0;deckylaunch::Handle caller_;
    std::string steamPrimaryStage_="not-attempted",steamPrimaryMode_,steamFallbackStage_="not-attempted";
    DWORD steamPrimaryError_=0,steamCleanupError_=0,steamFallbackError_=0;
    bool steamCleanupConfirmed_=false,steamFallbackAttempted_=false;
public:
    std::string steamLaunchFailure()const{
        return "primary="+steamPrimaryStage_+"/"+std::to_string(steamPrimaryError_)+
            ";cleanup="+(steamCleanupConfirmed_?std::string("confirmed"):std::string("none-or-unconfirmed"))+"/"+std::to_string(steamCleanupError_)+
            ";fallback="+steamFallbackStage_+"/"+std::to_string(steamFallbackError_);
    }
    nlohmann::json steamLaunchDiagnostics()const{return {{"primaryStage",steamPrimaryStage_},{"primaryMode",steamPrimaryMode_},
        {"primaryError",steamPrimaryError_},{"cleanupConfirmed",steamCleanupConfirmed_},{"cleanupError",steamCleanupError_},
        {"fallbackAttempted",steamFallbackAttempted_},{"fallbackStage",steamFallbackStage_},{"fallbackError",steamFallbackError_}};}
    explicit WindowsPorts(const std::wstring& root,const std::wstring& receiptRoot=userDirectory(CSIDL_LOCAL_APPDATA)):powerControl_(root),receiptRoot_(receiptRoot),startup_(root){}
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
    std::wstring steamExecutable(){
        auto value=registryText(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamExe");std::replace(value.begin(),value.end(),L'/',L'\\');if(validExe(value)&&lower(pathfs::path(value).filename().wstring())==L"steam.exe")return value;
        for(const auto& root:{registryText(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath"),registryText(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Valve\\Steam",L"InstallPath",RRF_SUBKEY_WOW6432KEY),registryText(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Valve\\Steam",L"InstallPath",RRF_SUBKEY_WOW6464KEY)}){if(root.empty())continue;value=(pathfs::path(root)/L"steam.exe").wstring();if(validExe(value))return value;}return {};
    }
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
        Presence result;HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(h==INVALID_HANDLE_VALUE)return result;
        PROCESSENTRY32W entry{sizeof(entry)};bool next=Process32FirstW(h,&entry)!=FALSE;if(!next){if(GetLastError()==ERROR_NO_MORE_FILES)result.steam=0;CloseHandle(h);return result;}
        result.steam=0;do{
            const auto basename=lower(entry.szExeFile);if(basename==L"steam.exe"){result.steam=1;continue;}
            const auto found=wanted.find(basename);if(found==wanted.end())continue;const auto& matches=found->second;
            deckylaunch::Handle process,token;process.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
            if(!process.value||!OpenProcessToken(process.value,TOKEN_QUERY,&token.value)){result.uncertain.insert(matches.begin(),matches.end());continue;}
            if(!deckylaunch::equalUser(caller_.value,token.value)||!deckylaunch::sameSession(caller_.value,token.value))continue;
            std::array<wchar_t,32768> path{};DWORD count=static_cast<DWORD>(path.size());if(!QueryFullProcessImageNameW(process.value,0,path.data(),&count)){result.uncertain.insert(matches.begin(),matches.end());continue;}
            const auto actual=lower(std::wstring(path.data(),count));for(const auto& addon:matches)if(addon==actual)result.active.insert(addon);
        }while(Process32NextW(h,&entry));if(GetLastError()!=ERROR_NO_MORE_FILES)result.steam=-1;CloseHandle(h);return result;
    }
    static bool absentExcept(DWORD ownChild){
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return false;
        PROCESSENTRY32W item{sizeof(item)};bool absent=true;
        if(Process32FirstW(snapshot,&item)){do{if(item.th32ProcessID!=ownChild&&_wcsicmp(item.szExeFile,L"steam.exe")==0){absent=false;break;}}while(Process32NextW(snapshot,&item));if(absent&&GetLastError()!=ERROR_NO_MORE_FILES)absent=false;}else absent=GetLastError()==ERROR_NO_MORE_FILES;
        CloseHandle(snapshot);return absent;
    }
    SteamSpawnOutcome spawnPrimary(const std::wstring& executable,const std::wstring& arguments,bool steam){
        std::wstring command=L"\""+executable+L"\""+arguments;STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=steam?SW_SHOWNORMAL:SW_HIDE;PROCESS_INFORMATION child{};
        const auto created=deckylaunch::create(executable,command,nullptr,pathfs::path(executable).parent_path().wstring(),startup,child);
        if(steam){steamPrimaryStage_=created.stage;steamPrimaryMode_=created.mode;steamPrimaryError_=created.error;}
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
        const bool admitted=!steam||absentExcept(child.dwProcessId);
        const bool resumed=admitted&&ResumeThread(child.hThread)!=static_cast<DWORD>(-1);
        if(steam){
            if(!admitted){steamPrimaryStage_="steam-absence-unverified";steamPrimaryError_=ERROR_BUSY;}
            else if(!resumed){steamPrimaryStage_="resume-thread";steamPrimaryError_=GetLastError();}
            else {steamPrimaryStage_="resumed";steamPrimaryError_=ERROR_SUCCESS;}
        }
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
    SteamSpawnOutcome spawnSteamPrimary(const std::wstring& executable,const std::wstring& arguments){return spawnPrimary(executable,arguments,true);}
    bool manualSteamFallbackAllowed(const std::wstring& executable){
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
        if(arguments!=startupArguments||!manualSteamFallbackAllowed(executable))return false;
        const auto directory=pathfs::path(executable).parent_path().wstring();
        SHELLEXECUTEINFOW request{sizeof(request)};
        request.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
        request.lpVerb=L"open";request.lpFile=executable.c_str();request.lpParameters=arguments.c_str();request.lpDirectory=directory.c_str();request.nShow=SW_SHOWNORMAL;
        steamFallbackAttempted_=true;
        const bool dispatched=ShellExecuteExW(&request)!=FALSE;
        steamFallbackStage_=dispatched?"shell-dispatched":"shell-execute";
        steamFallbackError_=dispatched?ERROR_SUCCESS:GetLastError();
        if(request.hProcess)CloseHandle(request.hProcess);
        // Shell acceptance is dispatch evidence only, never Steam/UI ready.
        return dispatched;
    }
    bool spawnManualSteam(const std::wstring& executable,const std::wstring& arguments){return dispatchManualSteam(*this,executable,arguments);}
};
inline nlohmann::json launch(const std::wstring& root,const nlohmann::json& args){
    if(!args.is_object()||args.size()!=1||!args.contains("userInitiated")||!args["userInitiated"].is_boolean()||!args["userInitiated"].get<bool>())throw std::runtime_error("Steam launch requires an explicit user action");
    WindowsPorts io(root);const auto result=run(io,Origin::Manual);return {{"ok",result.ok},{"dispatched",result.dispatched},{"alreadyRunning",result.alreadyRunning},{"reason",result.reason},{"addonsStarted",result.addonsStarted},{"addonsFailed",result.addonsFailed},{"spawnDiagnostic",io.steamLaunchDiagnostics()}};
}
inline bool tryLogonCommand(const std::wstring& root,const wchar_t* line,int& exitCode){
    int count=0;auto args=CommandLineToArgvW(line,&count);if(!args)return false;bool present=false;for(int i=1;i<count;++i)if(wcscmp(args[i],L"--steam-logon-start")==0)present=true;
    const bool exact=present&&count==2;LocalFree(args);if(!present)return false;exitCode=2;if(!exact)return true;
    WindowsPorts io(root);const auto result=run(io,Origin::Logon);exitCode=result.ok?0:3;return true;
}
}
