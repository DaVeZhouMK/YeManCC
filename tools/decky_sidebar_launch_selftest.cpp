// Memory API mode exercises the same production creator with no child/service/registry IO.
#ifdef YMCC_DECKY_TOKEN_MEMORY_TEST
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include "json.hpp"
#include "../native/process_efficiency.h"
namespace launchMemory {
struct Token {DWORD user=100,session=1,logon=50,integrity=SECURITY_MANDATORY_MEDIUM_RID;bool elevated=false;TOKEN_ELEVATION_TYPE type=TokenElevationTypeLimited;TOKEN_TYPE tokenType=TokenPrimary;};
struct State {std::map<uintptr_t,Token> tokens;std::set<uintptr_t> open;DWORD linkedError=ERROR_NO_SUCH_LOGON_SESSION,createError=0;bool typeQueryFails=false,shellMissing=false,shellChanged=false,badImage=false,shellExited=false,primaryWrongUser=false;int childFault=0;unsigned shellCalls=0,linkedCalls=0,created=0,direct=0,withToken=0,retired=0;};
inline State s;
inline HANDLE h(uintptr_t n){return reinterpret_cast<HANDLE>(n);}
inline uintptr_t id(HANDLE p){return reinterpret_cast<uintptr_t>(p);}
inline void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
inline void reset(bool full=false){s={};Token caller;caller.elevated=true;caller.integrity=SECURITY_MANDATORY_HIGH_RID;caller.type=full?TokenElevationTypeFull:TokenElevationTypeDefault;s.tokens[101]=caller;Token medium;medium.logon=full?49:50;s.tokens[102]=medium;s.tokens[103]=medium;if(full)s.linkedError=0;}
inline HANDLE current(){return h(201);}
inline HRESULT folder(HWND,int,HANDLE,DWORD,LPWSTR out){wcscpy_s(out,MAX_PATH,L"C:\\owned");return S_OK;}
inline DWORD module(HMODULE,LPWSTR out,DWORD size){const std::wstring value=L"C:\\owned\\fixture.exe";wcscpy_s(out,size,value.c_str());return static_cast<DWORD>(value.size());}
inline BOOL close(HANDLE p){need(s.open.erase(id(p))==1,"closed unknown/duplicate handle");return TRUE;}
inline BOOL openToken(HANDLE process,DWORD,PHANDLE out){uintptr_t token=id(process)==201?101:id(process)==203?103:id(process)==204?105:0;need(token&&out,"unknown process-token read");s.open.insert(token);*out=h(token);return TRUE;}
inline BOOL tokenInfo(HANDLE token,TOKEN_INFORMATION_CLASS kind,LPVOID data,DWORD bytes,PDWORD size){
 need(s.tokens.contains(id(token)),"unknown token read");const auto t=s.tokens.at(id(token));DWORD required=0;
 switch(kind){case TokenUser:required=sizeof(TOKEN_USER)+sizeof(SID);break;case TokenIntegrityLevel:required=sizeof(TOKEN_MANDATORY_LABEL)+sizeof(SID);break;case TokenElevation:required=sizeof(TOKEN_ELEVATION);break;case TokenElevationType:required=sizeof(TOKEN_ELEVATION_TYPE);break;case TokenSessionId:required=sizeof(DWORD);break;case TokenStatistics:required=sizeof(TOKEN_STATISTICS);break;case TokenType:required=sizeof(TOKEN_TYPE);break;case TokenLinkedToken:required=sizeof(TOKEN_LINKED_TOKEN);break;default:throw std::runtime_error("unexpected token information class");}
 *size=required;if(!data||bytes<required){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
 if(kind==TokenElevationType&&s.typeQueryFails){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 if(kind==TokenLinkedToken){++s.linkedCalls;if(s.linkedError){SetLastError(s.linkedError);return FALSE;}s.open.insert(102);static_cast<TOKEN_LINKED_TOKEN*>(data)->LinkedToken=h(102);return TRUE;}
 ZeroMemory(data,required);
 if(kind==TokenUser||kind==TokenIntegrityLevel){const bool integrity=kind==TokenIntegrityLevel;const auto offset=integrity?sizeof(TOKEN_MANDATORY_LABEL):sizeof(TOKEN_USER);auto sid=reinterpret_cast<SID*>(static_cast<unsigned char*>(data)+offset);sid->Revision=SID_REVISION;sid->SubAuthorityCount=1;SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY,mandatory=SECURITY_MANDATORY_LABEL_AUTHORITY;sid->IdentifierAuthority=integrity?mandatory:authority;sid->SubAuthority[0]=integrity?t.integrity:t.user;if(integrity)static_cast<TOKEN_MANDATORY_LABEL*>(data)->Label.Sid=sid;else static_cast<TOKEN_USER*>(data)->User.Sid=sid;}
 else if(kind==TokenElevation)static_cast<TOKEN_ELEVATION*>(data)->TokenIsElevated=t.elevated;
 else if(kind==TokenElevationType)*static_cast<TOKEN_ELEVATION_TYPE*>(data)=t.type;
 else if(kind==TokenSessionId)*static_cast<DWORD*>(data)=t.session;
 else if(kind==TokenStatistics)static_cast<TOKEN_STATISTICS*>(data)->AuthenticationId.LowPart=t.logon;
 else if(kind==TokenType)*static_cast<TOKEN_TYPE*>(data)=t.tokenType;
 return TRUE;
}
inline HWND shellWindow(){++s.shellCalls;return s.shellMissing?nullptr:reinterpret_cast<HWND>(static_cast<uintptr_t>(s.shellChanged&&s.shellCalls>1?302:301));}
inline DWORD windowProcess(HWND window,LPDWORD pid){need(window==reinterpret_cast<HWND>(301),"unknown shell window");*pid=77;return 1;}
inline HANDLE openProcess(DWORD,BOOL,DWORD pid){need(pid==77,"unknown shell pid");s.open.insert(203);return h(203);}
inline BOOL image(HANDLE process,DWORD,LPWSTR out,PDWORD size){need(id(process)==203,"unknown image query");const std::wstring value=s.badImage?L"C:\\untrusted\\explorer.exe":L"C:\\Windows\\explorer.exe";need(*size>value.size(),"image buffer");wcscpy_s(out,*size,value.c_str());*size=static_cast<DWORD>(value.size());return TRUE;}
inline UINT windowsDir(LPWSTR out,UINT size){wcscpy_s(out,size,L"C:\\Windows");return 10;}
inline BOOL duplicate(HANDLE source,DWORD,LPSECURITY_ATTRIBUTES,SECURITY_IMPERSONATION_LEVEL,TOKEN_TYPE,PHANDLE out){need(id(source)==103,"wrong duplication source");s.tokens[104]=s.tokens.at(103);if(s.primaryWrongUser)++s.tokens[104].user;s.open.insert(104);*out=h(104);return TRUE;}
inline BOOL makeChild(DWORD flags,BOOL inherit,PROCESS_INFORMATION* child,HANDLE token){
 need((flags&CREATE_SUSPENDED)&&(flags&CREATE_NO_WINDOW)&&!inherit,"creation must be suspended/hidden/no inherited handles");
 if(s.createError){SetLastError(s.createError);return FALSE;}++s.created;s.tokens[105]=s.tokens.at(id(token));auto& actual=s.tokens[105];if(s.childFault==1)actual.elevated=true;if(s.childFault==2)++actual.user;if(s.childFault==3)++actual.session;if(s.childFault==4)++actual.logon;if(s.childFault==5)actual.integrity=SECURITY_MANDATORY_HIGH_RID;
 child->hProcess=h(204);child->hThread=h(205);child->dwProcessId=88;child->dwThreadId=89;s.open.insert(204);s.open.insert(205);return TRUE;
}
inline BOOL withToken(HANDLE token,DWORD,LPCWSTR,LPWSTR,DWORD flags,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION child){need(id(token)==104,"child must use verified primary token");++s.withToken;return makeChild(flags,FALSE,child,token);}
inline DWORD wait(HANDLE process,DWORD){if(id(process)==203)return s.shellExited?WAIT_OBJECT_0:WAIT_TIMEOUT;need(id(process)==204,"wait outside owned handles");return s.retired?WAIT_OBJECT_0:WAIT_TIMEOUT;}
inline BOOL retire(HANDLE process,UINT){need(id(process)==204,"cannot retire shell or other process");++s.retired;return TRUE;}
}
namespace ymcc::processEfficiency {
inline BOOL memoryCreateProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL inherit,DWORD flags,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION child){++launchMemory::s.direct;return launchMemory::makeChild(flags,inherit,child,launchMemory::h(101));}
inline void memoryApplyOwnedChild(HANDLE){}
}
#define SHGetFolderPathW launchMemory::folder
#define GetModuleFileNameW launchMemory::module
#define GetCurrentProcess launchMemory::current
#define OpenProcessToken launchMemory::openToken
#define GetTokenInformation launchMemory::tokenInfo
#define CloseHandle launchMemory::close
#define GetShellWindow launchMemory::shellWindow
#define GetWindowThreadProcessId launchMemory::windowProcess
#define OpenProcess launchMemory::openProcess
#define QueryFullProcessImageNameW launchMemory::image
#define GetWindowsDirectoryW launchMemory::windowsDir
#define DuplicateTokenEx launchMemory::duplicate
#define CreateProcessWithTokenW launchMemory::withToken
#define WaitForSingleObject launchMemory::wait
#define TerminateProcess launchMemory::retire
#define createProcess memoryCreateProcess
#define applyOwnedChild memoryApplyOwnedChild
#include "../native/decky_sidebar_launch.h"
#include "../native/steam_native_launch_windows.h"
int main(int argc,char**argv){using namespace launchMemory;using nlohmann::json;json cases=json::array();
 try{
 auto run=[&]{std::wstring command=L"\"C:\\owned\\inert.exe\"";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};const auto result=ymcc::deckylaunch::create(L"C:\\owned\\inert.exe",command,nullptr,L"C:\\owned",startup,child);if(result.created){close(child.hThread);close(child.hProcess);}need(s.open.empty(),"all token/process handles must return");return result;};
 auto pass=[&](const char* name){cases.push_back(name);};
 reset();s.tokens[101].elevated=false;s.tokens[101].integrity=SECURITY_MANDATORY_MEDIUM_RID;s.tokens[101].type=TokenElevationTypeLimited;auto r=run();need(r.created&&s.direct==1&&!s.shellCalls&&!s.linkedCalls,"medium direct unchanged");pass("medium caller keeps direct suspended creation");
 reset(true);r=run();need(r.created&&r.mode=="verified-shell-primary"&&s.linkedCalls==1&&s.withToken==1&&!s.direct,"split UAC route unchanged");pass("split UAC keeps linked identity and shell primary");
 reset();r=run();need(r.created&&r.mode=="verified-shell-primary-default-token"&&!s.linkedCalls&&s.withToken==1&&!s.direct,"Default token must not require linked token");pass("Default elevated token uses only fully matching medium shell");
 reset(true);s.linkedError=ERROR_NO_SUCH_LOGON_SESSION;s.tokens[103].logon=50;r=run();need(r.created&&r.mode=="verified-shell-primary-linked-unavailable"&&s.linkedCalls==1&&!s.direct,"1312 may use only caller-authenticated medium shell");pass("1312 retains caller user session and authentication identity");
 reset(true);s.linkedError=ERROR_ACCESS_DENIED;r=run();need(!r.created&&r.stage=="linked-token-query"&&r.error==ERROR_ACCESS_DENIED&&!s.shellCalls&&!s.created,"unknown linked error must fail closed");pass("other linked query errors never gain fallback");
 reset();s.typeQueryFails=true;r=run();need(!r.created&&r.stage=="caller-token-elevation-type"&&!s.shellCalls,"unknown elevation type must fail");pass("unknown token type is refused");
 reset();s.tokens[101].type=TokenElevationTypeLimited;r=run();need(!r.created&&r.stage=="caller-token-elevation-type"&&!s.shellCalls,"inconsistent elevated Limited token must fail");pass("inconsistent elevation metadata is refused");
 for(int fault=0;fault<6;++fault){reset();auto& t=s.tokens[103];if(fault==0)t.elevated=true;if(fault==1)t.integrity=SECURITY_MANDATORY_HIGH_RID;if(fault==2)++t.user;if(fault==3)++t.session;if(fault==4)++t.logon;if(fault==5)t.tokenType=TokenImpersonation;r=run();const std::string expected=fault==0?"shell-token-elevated":fault==1?"shell-token-integrity":fault==5?"shell-token-type":"shell-token-identity";need(!r.created&&r.stage==expected&&!s.created&&!s.direct,"unsafe shell cannot become a fallback");cases.push_back("reject unsafe shell fact "+std::to_string(fault));}
 reset();s.shellMissing=true;r=run();need(!r.created&&r.stage=="shell-not-available"&&!s.created,"no interactive shell must fail");pass("missing shell is refused");
 reset();s.badImage=true;r=run();need(!r.created&&r.stage=="shell-image-mismatch"&&!s.created,"untrusted explorer path must fail");pass("fake Explorer is refused");
 reset();s.shellChanged=true;r=run();need(!r.created&&r.stage=="shell-process-changed"&&!s.created,"changed shell window must fail");pass("shell identity is rechecked before creation");
 reset();s.shellExited=true;r=run();need(!r.created&&r.stage=="shell-process-changed"&&!s.created,"exited shell must fail");pass("exited shell is refused");
 reset();s.primaryWrongUser=true;r=run();need(!r.created&&r.stage=="primary-token-identity"&&!s.created,"changed primary token must fail");pass("duplicate token identity is verified");
 for(int fault=0;fault<3;++fault){reset(true);if(fault==0)++s.tokens[102].user;if(fault==1)++s.tokens[102].session;if(fault==2)s.tokens[102].elevated=true;r=run();need(!r.created&&r.stage=="linked-token-identity"&&!s.shellCalls&&!s.created,"bad linked token must fail");cases.push_back("reject invalid linked identity "+std::to_string(fault));}
 reset();s.createError=ERROR_PRIVILEGE_NOT_HELD;r=run();need(!r.created&&r.stage=="create-process-with-token"&&r.error==ERROR_PRIVILEGE_NOT_HELD&&!s.direct,"privilege error must never launch elevated");pass("creation failure cannot use elevated caller");
 for(int fault=1;fault<=5;++fault){reset();s.childFault=fault;r=run();need(!r.created&&r.stage=="child-token-verification"&&s.created==1&&s.retired==1,"unsafe child must be retired while suspended");cases.push_back("retire changed child fact "+std::to_string(fault));}
 for(int fault=0;fault<4;++fault){reset();if(fault==0)s.typeQueryFails=true;if(fault==1)s.tokens[103].elevated=true;if(fault==2)s.tokens[103].integrity=SECURITY_MANDATORY_HIGH_RID;if(fault==3)s.tokens[103].tokenType=TokenImpersonation;ymcc::steamlaunch::WindowsPorts io(L"C:\\owned\\PowerControl",L"C:\\owned");const auto outcome=io.spawnSteamPrimary(L"C:\\owned\\steam.exe",ymcc::steamlaunch::startupArguments);need(outcome==ymcc::steamlaunch::SteamSpawnOutcome::NotCreated&&!s.created&&s.open.empty(),"Steam must classify token failures as known not created");cases.push_back("Steam preserves manual fallback for token failure "+std::to_string(fault));}
 reset();s.childFault=2;{ymcc::steamlaunch::WindowsPorts io(L"C:\\owned\\PowerControl",L"C:\\owned");const auto outcome=io.spawnSteamPrimary(L"C:\\owned\\steam.exe",ymcc::steamlaunch::startupArguments);need(outcome==ymcc::steamlaunch::SteamSpawnOutcome::Uncertain&&s.retired==1&&s.open.empty(),"Steam cannot replay ambiguous child cleanup");}pass("Steam still refuses fallback after unproven child retirement");
 json report={{"ok",true},{"cases",cases.size()},{"results",cases},{"productionCreator",true},{"memoryWin32Terminals",true},{"realProcessCreates",0},{"registryWrites",0},{"installedSettingsWrites",0}};if(argc==2){std::ofstream output(argv[1]);output<<report.dump(2);need(output.good(),"report write");}std::cout<<report.dump()<<std::endl;return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}
#else

// Source-built inert child only: actual normal/elevated-parent launch checks.
// No Steam, product YMCC, Loader, business settings, ACL edits or token elevation.
#define WIN32_LEAN_AND_MEAN
#include "../native/decky_sidebar_launch.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
using nlohmann::json;
using namespace ymcc::deckylaunch;
int wmain(int argc,wchar_t**argv){
 try {
    if(argc!=3)return 2;const std::filesystem::path directory=LR"(G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Launch25\validation)";
    const auto output=std::filesystem::absolute(argv[2]).lexically_normal();if(output.parent_path()!=directory)throw std::runtime_error("fixture output scope");
    Handle token;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value))throw std::runtime_error("fixture own token");
    TOKEN_ELEVATION elevation{};DWORD bytes=0;GetTokenInformation(token.value,TokenElevation,&elevation,sizeof(elevation),&bytes);
    if(std::wstring(argv[1])==L"--child"){
        const bool normal=unelevated(token.value);json report={{"elevated",elevation.TokenIsElevated!=0},{"mediumOrLower",normal},{"pid",GetCurrentProcessId()},{"sourceBuiltInert",true},{"settingsWrites",0}};
        std::ofstream file(output);file<<report.dump(2);return file.good()&&normal?0:5;
    }
    if(std::wstring(argv[1])!=L"--parent")return 3;
    wchar_t buffer[32768]{};GetModuleFileNameW(nullptr,buffer,static_cast<DWORD>(_countof(buffer)));const std::wstring executable=buffer;
    if(std::filesystem::path(executable).parent_path()!=directory.parent_path()/L"Build")throw std::runtime_error("fixture executable scope");
    const auto childOutput=output.wstring()+L".child.json";
    std::wstring command=L"\""+executable+L"\" --child \""+childOutput+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION child{};
    const auto result=create(executable,command,nullptr,std::filesystem::path(executable).parent_path().wstring(),startup,child);
    json report={{"parentElevated",elevation.TokenIsElevated!=0},{"childCreated",result.created},{"error",result.error},{"stage",result.stage},{"mode",result.mode},{"thirdPartyStarted",false},{"productStarted",false},{"steamTouched",false},{"settingsWrites",0}};
    if(result.created){
        Handle job;job.value=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        bool owned=job.value&&SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))&&AssignProcessToJobObject(job.value,child.hProcess);
        report["jobOwnedBeforeResume"]=owned;
        if(owned&&ResumeThread(child.hThread)!=static_cast<DWORD>(-1)){
            const auto wait=WaitForSingleObject(child.hProcess,10000);DWORD code=99;GetExitCodeProcess(child.hProcess,&code);report["childExit"]=code;report["childFinished"]=wait==WAIT_OBJECT_0;
            if(wait==WAIT_OBJECT_0){std::ifstream file(childOutput);json observation;file>>observation;report["childObservation"]=observation;}
        }else{report["ownershipError"]=GetLastError();TerminateProcess(child.hProcess,1);}
        CloseHandle(child.hThread);CloseHandle(child.hProcess);
    }
    const auto invalid=std::filesystem::path(executable).parent_path()/L"deliberately-absent.exe";std::wstring invalidCommand=L"\""+invalid.wstring()+L"\"";PROCESS_INFORMATION absent{};
    const auto denied=create(invalid.wstring(),invalidCommand,nullptr,invalid.parent_path().wstring(),startup,absent);
    report["missingExecutableRefused"]=!denied.created;report["missingExecutableError"]=denied.error;report["missingExecutableStage"]=denied.stage;
    report["callerIntegrityGate"]=unelevated(token.value)==!elevation.TokenIsElevated;report["sameTokenUser"]=equalUser(token.value,token.value);report["sameTokenSession"]=sameSession(token.value,token.value);report["sameTokenLogon"]=sameLogon(token.value,token.value);
    const bool pass=report.value("childCreated",false)&&report.value("jobOwnedBeforeResume",false)&&report.value("childFinished",false)&&report.value("childExit",99)==0&&report["childObservation"].value("mediumOrLower",false)&&!report["childObservation"].value("elevated",true)&&report.value("missingExecutableRefused",false)&&report.value("callerIntegrityGate",false);
    report["pass"]=pass;std::ofstream file(output);file<<report.dump(2);return pass?0:1;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
}

#endif
