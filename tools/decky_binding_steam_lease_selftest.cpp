// Actual SteamLease, real owned processes and a unique private registry scope.
#include <winsock2.h>
#include <windows.h>
#include <atomic>
namespace leaseboundary {
inline std::atomic<DWORD> denied{0};
inline HANDLE WINAPI openProcess(DWORD rights,BOOL inherit,DWORD pid){if(pid==denied.load()&&pid){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}return ::OpenProcess(rights,inherit,pid);}
}
#define OpenProcess leaseboundary::openProcess
#include "decky_binding_worker.h"
#undef OpenProcess
#include <filesystem>
#include <fstream>
#include <iostream>
using Json=nlohmann::json;
using namespace ymcc::deckybinding::worker;
namespace fs=std::filesystem;
static void need(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
static unsigned handles(){DWORD n=0;need(GetProcessHandleCount(GetCurrentProcess(),&n)!=FALSE,"handle-count");return n;}
struct Handle {HANDLE h=nullptr;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}Handle()=default;Handle(const Handle&)=delete;};
struct Registry {
 std::wstring path;HKEY root=nullptr,active=nullptr;
 Registry(const fs::path& home){path=L"Software\\YMCC.Tests\\DeckySteamLease\\case-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());need(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&root,nullptr)==ERROR_SUCCESS,"private-registry-root");need(RegCreateKeyExW(root,L"ActiveProcess",0,nullptr,0,KEY_ALL_ACCESS,nullptr,&active,nullptr)==ERROR_SUCCESS,"private-registry-active");setRoot(home.wstring());number(L"ActiveUser",123);}
 ~Registry(){if(active)RegCloseKey(active);if(root){RegDeleteKeyW(root,L"ActiveProcess");RegCloseKey(root);}RegDeleteKeyW(HKEY_CURRENT_USER,path.c_str());}
 void number(const wchar_t* name,DWORD value){need(RegSetValueExW(active,name,0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value))==ERROR_SUCCESS,"private-registry-number");}
 void setRoot(const std::wstring& value){need(RegSetValueExW(root,L"SteamPath",0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS,"private-registry-path");}
};
struct Children {
 Handle job;std::vector<HANDLE> owned;std::vector<DWORD> pids;
 Children(){job.h=CreateJobObjectW(nullptr,nullptr);need(job.h!=nullptr,"own-job");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;need(SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"own-job-kill-on-close");}
 DWORD start(const fs::path& exe){auto command=L"\""+exe.wstring()+L"\" --lease-child";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};need(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,exe.parent_path().c_str(),&startup,&process)!=FALSE,"own-child-create");if(!AssignProcessToJobObject(job.h,process.hProcess)){TerminateProcess(process.hProcess,99);CloseHandle(process.hThread);CloseHandle(process.hProcess);throw std::runtime_error("own-child-assign");}need(ResumeThread(process.hThread)!=DWORD(-1),"own-child-resume");CloseHandle(process.hThread);owned.push_back(process.hProcess);pids.push_back(process.dwProcessId);return process.dwProcessId;}
 Json stop(){need(TerminateJobObject(job.h,0)!=FALSE,"own-job-stop");for(const auto child:owned)need(WaitForSingleObject(child,1000)==WAIT_OBJECT_0,"own-child-exit");JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};need(QueryInformationJobObject(job.h,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr)!=FALSE,"own-job-query");need(info.ActiveProcesses==0,"own-job-empty");return {{"pids",pids},{"activeProcesses",info.ActiveProcesses}};}
 ~Children(){if(job.h)TerminateJobObject(job.h,0);for(const auto child:owned){WaitForSingleObject(child,1000);CloseHandle(child);}}
};
static Request request(DWORD pid,const fs::path& root){Request r;r.steam.root=ymcc::steamlive::normalizedPath(root.wstring());r.steam.pid=pid;r.steam.account=123;auto h=ymcc::steamsession::verifiedProcess(pid,r.steam.root,&r.steam.created);need(h!=nullptr,"real-owned-Steam-identity");CloseHandle(h);need(ProcessIdToSessionId(pid,&r.windowsSession)!=FALSE,"own-session");return r;}
static bool admitted(const Request& r,const Registry& registry,const std::string& expectedCheck={}){std::string check;try{SteamLease lease(r,registry.path,&check);return lease.current();}catch(const std::exception& e){need(std::string(e.what())=="mirror-bootstrap-session-changed","expected-identity-failure");if(!expectedCheck.empty())need(check==expectedCheck,"wrong-session-refusal-diagnostic");return false;}}
static void save(const fs::path& file,const Json& report){std::ofstream out(file,std::ios::binary);out<<report.dump(2);need(bool(out),"write-report");}
int wmain(int argc,wchar_t** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 if(argc==2&&std::wstring_view(argv[1])==L"--lease-child"){Sleep(INFINITE);return 0;}
 if(argc==4&&std::wstring_view(argv[1])==L"--live-readonly"){
  Json out={{"scope","Read-only compiled production SteamLease; no bootstrap, CDP, registry writes or product launches"},{"realSteamActions",0},{"installedWrites",0}};
  Request r;r.steam.pid=static_cast<DWORD>(std::stoul(argv[2]));const std::wstring key=L"Software\\Valve\\Steam";r.steam.root=ymcc::steamsession::steamRoot(key);r.steam.account=ymcc::steamsession::regNumber(key,L"ActiveUser");const auto registered=ymcc::steamsession::regNumber(key,L"pid");auto h=ymcc::steamsession::verifiedProcess(r.steam.pid,r.steam.root,&r.steam.created);if(h)CloseHandle(h);ProcessIdToSessionId(r.steam.pid,&r.windowsSession);out["actualPid"]=r.steam.pid;out["registeredPid"]=registered;out["originalPidRuleAccepted"]=!registered||registered==r.steam.pid;out["verifiedProcess"]=h!=nullptr;out["sameWindowsUserAndSession"]=ymcc::deckymirror::sameWindowsUserAndSession(r.steam.pid);out["newPidRuleAccepted"]=registeredPidCompatible(registered,r.steam.pid,r.steam.root);try{SteamLease lease(r);out["fullLeaseCurrent"]=lease.current();}catch(const std::exception& e){out["fullLeaseCurrent"]=false;out["reason"]=e.what();}save(argv[3],out);std::cout<<out.dump()<<std::endl;return 0;
 }
 if(argc!=3)return 3;std::wstring ownedRegistryPath;const auto root=fs::absolute(argv[1]);fs::create_directories(root);Json rows=Json::array(),report={{"officialLoaderExecuted",false},{"realSteamActions",0},{"installedWrites",0},{"registryScope","unique private HKCU Software/YMCC.Tests/DeckySteamLease; no official registry writes"}};
 try{
  wchar_t image[32768]{};need(GetModuleFileNameW(nullptr,image,32768)!=0,"self-image");const auto steam=root/L"steam.exe",helper=root/L"unrelated.exe";fs::copy_file(image,steam);fs::copy_file(image,helper);
  Registry registry(root);ownedRegistryPath=registry.path;Children children;const auto pid=children.start(steam),other=children.start(steam),unrelated=children.start(helper);auto r=request(pid,root);registry.number(L"pid",pid);
  const auto test=[&](const char* name,auto action){bool okay=false;std::string error;try{okay=action();}catch(const std::exception& e){error=e.what();}Json row={{"name",name},{"pass",okay}};if(!error.empty())row["error"]=error;rows.push_back(row);std::cout<<(okay?"PASS ":"FAIL ")<<name<<std::endl;};
  test("exact registered Steam identity is admitted",[&]{return admitted(r,registry);});
  registry.number(L"pid",0);test("missing registered PID preserves verified observer lease",[&]{return admitted(r,registry);});
  registry.number(L"pid",MAXDWORD);test("stale absent registered PID preserves verified observer lease",[&]{return admitted(r,registry);});
  registry.number(L"pid",unrelated);test("PID reused by verified unrelated image cannot block current Steam",[&]{return admitted(r,registry);});
  leaseboundary::denied=unrelated;test("unreadable registered process never grants stale PID recovery",[&]{return !admitted(r,registry);});leaseboundary::denied=0;
  registry.number(L"pid",other);test("different live registered Steam remains a session conflict",[&]{return !admitted(r,registry,"steam-registered-pid");});
  registry.number(L"pid",MAXDWORD);registry.number(L"ActiveUser",124);test("stale PID cannot bypass changed Steam account",[&]{return !admitted(r,registry,"steam-account");});registry.number(L"ActiveUser",123);
  registry.setRoot((root/L"changed").wstring());test("stale PID cannot bypass changed Steam root",[&]{return !admitted(r,registry,"steam-root");});registry.setRoot(root.wstring());
  auto changed=r;++changed.steam.created;test("stale PID cannot bypass process birth mismatch",[&]{return !admitted(changed,registry,"steam-process-or-birth");});
  changed=r;++changed.windowsSession;test("stale PID cannot bypass Windows session mismatch",[&]{return !admitted(changed,registry,"steam-windows-session");});
  changed=r;changed.steam.root=ymcc::steamlive::normalizedPath((root/L"changed").wstring());test("stale PID cannot bypass request image mismatch",[&]{return !admitted(changed,registry);});
  test("held lease rejects a later real registered session switch",[&]{SteamLease lease(r,registry.path);registry.number(L"pid",other);const bool refused=!lease.current();registry.number(L"pid",MAXDWORD);return refused;});
  const auto before=handles();for(unsigned i=0;i<64;++i)admitted(changed,registry);test("rejected identities return their process handles",[&]{return handles()==before;});
  report["ownedChildrenAtExit"]=children.stop();test("terminated target cannot regain admission from stale registry",[&]{return !admitted(r,registry);});
  std::string scopeName;for(const auto c:registry.path)scopeName.push_back(static_cast<char>(c));report["privateRegistryPath"]=scopeName;report["pass"]=std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.at("pass")==true;});
 }catch(const std::exception& e){report["pass"]=false;report["error"]=e.what();}
 HKEY retained=nullptr;const auto registryEnd=ownedRegistryPath.empty()?ERROR_FILE_NOT_FOUND:RegOpenKeyExW(HKEY_CURRENT_USER,ownedRegistryPath.c_str(),0,KEY_READ,&retained);if(retained)RegCloseKey(retained);report["privateRegistryRemoved"]=registryEnd==ERROR_FILE_NOT_FOUND;report["pass"]=report.value("pass",false)&&report.at("privateRegistryRemoved")==true;report["cases"]=rows;report["count"]=rows.size();save(argv[2],report);return report.value("pass",false)?0:2;
}
