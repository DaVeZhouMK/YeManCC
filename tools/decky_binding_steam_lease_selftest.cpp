// Actual SteamLease, real owned processes and a unique private registry scope.
#include <winsock2.h>
#include <windows.h>
#include <atomic>
static std::atomic<unsigned> accountFactRegistryReads{0};
static LSTATUS WINAPI observedFactRegGetValueW(HKEY a,LPCWSTR b,LPCWSTR c,DWORD d,LPDWORD e,PVOID f,LPDWORD g){++accountFactRegistryReads;return ::RegGetValueW(a,b,c,d,e,f,g);}
#define RegGetValueW observedFactRegGetValueW
namespace leaseboundary {
inline std::atomic<DWORD> denied{0};
#ifdef YMCC_STEAM_LEASE_DELEGATED_BOUNDARIES
inline std::atomic<DWORD> countedPid{0};
inline std::atomic<unsigned> countedOpens{0},tokenSessionReads{0};
inline std::atomic<bool> denyTokenSession{false};
inline BOOL WINAPI getTokenInformation(HANDLE token,TOKEN_INFORMATION_CLASS kind,LPVOID value,DWORD size,PDWORD got){
    if(kind==TokenSessionId){++tokenSessionReads;if(denyTokenSession){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}}
    return ::GetTokenInformation(token,kind,value,size,got);
}
#endif
inline HANDLE WINAPI openProcess(DWORD rights,BOOL inherit,DWORD pid){
#ifdef YMCC_STEAM_LEASE_DELEGATED_BOUNDARIES
    if(pid&&pid==countedPid.load())++countedOpens;
#endif
    if(pid==denied.load()&&pid){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
    return ::OpenProcess(rights,inherit,pid);
}
#ifdef YMCC_STEAM_LEASE_DELEGATED_BOUNDARIES
inline std::atomic<unsigned> sessionLookups{0},forcedEventWaitFailures{0};
inline std::atomic<bool> failEventWait{false};
inline BOOL WINAPI processIdToSessionId(DWORD pid,DWORD* session){++sessionLookups;return ::ProcessIdToSessionId(pid,session);}
// This terminal OS fault port leaves the real held-process wait intact. It
// also covers a product-owned duplicate of the supplied cancellation event.
inline DWORD WINAPI waitForSingleObject(HANDLE value,DWORD milliseconds){
    if(failEventWait.load()&&::GetProcessId(value)==0){++forcedEventWaitFailures;SetLastError(ERROR_INVALID_HANDLE);return WAIT_FAILED;}
    return ::WaitForSingleObject(value,milliseconds);
}
#endif
}
#define OpenProcess leaseboundary::openProcess
#ifdef YMCC_STEAM_LEASE_DELEGATED_BOUNDARIES
#define ProcessIdToSessionId leaseboundary::processIdToSessionId
#define WaitForSingleObject leaseboundary::waitForSingleObject
#define GetTokenInformation leaseboundary::getTokenInformation
#endif
#include "decky_binding_worker.h"
#undef OpenProcess
#undef RegGetValueW
#ifdef YMCC_STEAM_LEASE_DELEGATED_BOUNDARIES
#undef ProcessIdToSessionId
#undef WaitForSingleObject
#undef GetTokenInformation
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
using Json=nlohmann::json;
using namespace ymcc::deckybinding::worker;
using LegacySteamLeaseFixture=BasicSteamLease<true>;
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
static bool admitted(const Request& r,const Registry& registry,const std::string& expectedCheck={}){std::string check;try{LegacySteamLeaseFixture lease(r,registry.path,&check);return lease.current();}catch(const std::exception& e){need(std::string(e.what())=="mirror-bootstrap-session-changed","expected-identity-failure");if(!expectedCheck.empty())need(check==expectedCheck,"wrong-session-refusal-diagnostic");return false;}}
static void save(const fs::path& file,const Json& report){std::ofstream out(file,std::ios::binary);out<<report.dump(2);need(bool(out),"write-report");}
#ifdef YMCC_STEAM_LEASE_DELEGATED_BOUNDARIES
struct DelegatedCancelEvent : Handle {
 explicit DelegatedCancelEvent(bool signalled=false){h=CreateEventW(nullptr,TRUE,signalled?TRUE:FALSE,nullptr);need(h!=nullptr,"delegated-owned-event-create");}
};
template<class Lease=SteamLease>
static bool delegatedConstructorRejected(const Request& r,const Registry& registry,HANDLE event,const std::string& expectedCheck={}){
 std::string check;
 try{Lease lease(r,registry.path,&check,event);}
 catch(const std::exception& e){need(std::string(e.what())=="mirror-bootstrap-session-changed","delegated-expected-identity-failure");if(!expectedCheck.empty())need(check==expectedCheck,"delegated-wrong-initial-refusal");return true;}
 return false;
}
// Only the explicit directed selection enters this function. All identity and
// revocation decisions below belong to production SteamLease, not a test model.
template<class Test>
static void delegatedSteamLeaseBoundaries(const Request& r,const fs::path& root,Registry& registry,Children& children,DWORD other,DWORD unrelated,Test&& test,Json& report){
 report["selection"]="delegated-identity-only";
 test("delegated held lease consumes 1001 times without registry or session discovery",[&]{
     DelegatedCancelEvent event;auto delegated=r;delegated.delegatedIdentity=true;
     // A delegated consumer must ignore private registry noise even on entry.
     registry.number(L"pid",other);registry.number(L"ActiveUser",124);registry.setRoot((root/L"foreign-registry-root").wstring());
     const auto beforeRegistry=accountFactRegistryReads.load(),beforeSession=leaseboundary::sessionLookups.load();
     const auto beforeOpens=leaseboundary::countedOpens.load(),beforeTokenSessions=leaseboundary::tokenSessionReads.load();
     leaseboundary::countedPid=r.steam.pid;
     struct CountReset {~CountReset(){leaseboundary::countedPid=0;}} reset;
     SteamLease lease(delegated,registry.path,nullptr,event.h);
     const auto afterConstructionRegistry=accountFactRegistryReads.load(),consumeSession=leaseboundary::sessionLookups.load();
     const auto consumeOpens=leaseboundary::countedOpens.load(),consumeTokenSessions=leaseboundary::tokenSessionReads.load();
     need(afterConstructionRegistry==beforeRegistry,"delegated-constructor-zero-registry");
     for(unsigned i=0;i<1001;++i)need(lease.current(),"delegated-current-1001-live");
     report["delegatedCurrentCalls"]=1001;
     report["delegatedConstructorRegistryReads"]=afterConstructionRegistry-beforeRegistry;
     report["delegatedCurrentRegistryReads"]=accountFactRegistryReads.load()-afterConstructionRegistry;
     report["delegatedConstructorSessionLookups"]=consumeSession-beforeSession;
     report["delegatedCurrentSessionLookups"]=leaseboundary::sessionLookups.load()-consumeSession;
     report["delegatedConstructorTargetOpens"]=consumeOpens-beforeOpens;
     report["delegatedConstructorTokenSessionQueries"]=consumeTokenSessions-beforeTokenSessions;
     report["delegatedCurrentTargetOpens"]=leaseboundary::countedOpens.load()-consumeOpens;
     need(consumeOpens-beforeOpens==1&&consumeSession==beforeSession&&consumeTokenSessions-beforeTokenSessions==2,
          "delegated-constructor-verifies-the-single-held-object");
     need(leaseboundary::countedOpens.load()==consumeOpens&&leaseboundary::tokenSessionReads.load()==consumeTokenSessions,
          "delegated-current-1001-zero-target-opens-and-token-session-queries");
     need(accountFactRegistryReads.load()==beforeRegistry,"delegated-current-1001-zero-registry");
     need(leaseboundary::sessionLookups.load()==consumeSession,"delegated-current-1001-zero-session-discovery");
     return true;
 });
 test("delegated constructor retains actual PID birth root Windows session and nonzero account checks",[&]{
     DelegatedCancelEvent event;const auto beforeReads=accountFactRegistryReads.load();auto bad=r;bad.delegatedIdentity=true;
     bad.steam.pid=0;need(delegatedConstructorRejected(bad,registry,event.h,"steam-process-or-birth"),"delegated-zero-pid-refused");
     bad=r;bad.delegatedIdentity=true;bad.steam.pid=unrelated;need(delegatedConstructorRejected(bad,registry,event.h,"steam-process-or-birth"),"delegated-wrong-pid-image-refused");
     bad=r;bad.delegatedIdentity=true;++bad.steam.created;need(delegatedConstructorRejected(bad,registry,event.h,"steam-process-or-birth"),"delegated-wrong-birth-refused");
     bad=r;bad.delegatedIdentity=true;bad.steam.root=ymcc::steamlive::normalizedPath((root/L"changed").wstring());need(delegatedConstructorRejected(bad,registry,event.h,"steam-process-or-birth"),"delegated-wrong-root-refused");
     bad=r;bad.delegatedIdentity=true;++bad.windowsSession;need(delegatedConstructorRejected(bad,registry,event.h,"steam-windows-session"),"delegated-wrong-windows-session-refused");
     {
         const auto before=handles();
         leaseboundary::denyTokenSession=true;
         struct FaultReset {~FaultReset(){leaseboundary::denyTokenSession=false;}} reset;
         bad=r;bad.delegatedIdentity=true;
         need(delegatedConstructorRejected(bad,registry,event.h,"steam-user-or-session"),"delegated-token-session-query-failure-refused");
         need(handles()==before,"delegated-token-session-failure-releases-handles");
     }
     bad=r;bad.delegatedIdentity=true;bad.steam.account=0;need(delegatedConstructorRejected(bad,registry,event.h),"delegated-zero-account-refused-before-parse");
     need(accountFactRegistryReads.load()==beforeReads,"delegated-initial-identity-refusals-zero-registry");report["delegatedInitialIdentityRefusals"]=6;return true;
 });
 test("delegated mode requires a valid unsignalled cancellation event",[&]{
     auto delegated=r;delegated.delegatedIdentity=true;const auto beforeReads=accountFactRegistryReads.load();
     need(delegatedConstructorRejected(delegated,registry,nullptr),"delegated-missing-event-no-fallback");
     need(delegatedConstructorRejected(delegated,registry,INVALID_HANDLE_VALUE),"delegated-invalid-event-no-fallback");
     DelegatedCancelEvent signalled(true);need(delegatedConstructorRejected(delegated,registry,signalled.h),"delegated-signalled-event-no-fallback");
     DelegatedCancelEvent writable;Handle noWait;
     need(DuplicateHandle(GetCurrentProcess(),writable.h,GetCurrentProcess(),&noWait.h,EVENT_MODIFY_STATE,FALSE,0)!=FALSE,"delegated-event-without-wait-access-create");
     need(delegatedConstructorRejected(delegated,registry,noWait.h),"delegated-unwaitable-event-no-fallback");
     // The inherited source is the private typed event. This consumer is not
     // an untrusted generic HANDLE parser and does not inspect object types.
     need(accountFactRegistryReads.load()==beforeReads,"delegated-invalid-events-zero-registry");report["delegatedInvalidEventRefusals"]=4;return true;
 });
 test("cancellation event alone cannot enable delegated identity",[&]{
     registry.setRoot(root.wstring());registry.number(L"pid",r.steam.pid);registry.number(L"ActiveUser",124);
     auto ordinary=r;need(!ordinary.delegatedIdentity,"delegated-mode-default-remains-false");DelegatedCancelEvent cancellation;
     // Supplying the ordinary typed cancellation HANDLE is not an identity grant.
     const auto beforeReads=accountFactRegistryReads.load();
     need(delegatedConstructorRejected<LegacySteamLeaseFixture>(ordinary,registry,cancellation.h,"steam-account"),"ordinary-cancel-handle-cannot-bypass-account");
     need(accountFactRegistryReads.load()>beforeReads,"ordinary-cancel-retains-registry-branch");
     auto anonymous=ordinary;anonymous.steam.account=0;anonymous.liveUiAccount=true;registry.number(L"ActiveUser",0);
     need(delegatedConstructorRejected<LegacySteamLeaseFixture>(anonymous,registry,cancellation.h,"steam-account"),"ordinary-typed-ui-and-cancel-cannot-authorize-zero-account");return true;
 });
 test("production lease refuses an unmarked identity before process or registry discovery",[&]{
     DelegatedCancelEvent event;
     struct ResetCountedPid {~ResetCountedPid(){leaseboundary::countedPid=0;}} reset;
     leaseboundary::countedPid=r.steam.pid;
     const auto opens=leaseboundary::countedOpens.load(),reads=accountFactRegistryReads.load();
     need(delegatedConstructorRejected(r,registry,event.h,"steam-delegation-required"),"unmarked-production-lease-accepted");
     need(leaseboundary::countedOpens.load()==opens&&accountFactRegistryReads.load()==reads,"unmarked-production-lease-did-discovery");
     return true;
 });
 test("delegated cancellation remains latched after an observed signal is reset",[&]{
     DelegatedCancelEvent event;auto delegated=r;delegated.delegatedIdentity=true;SteamLease lease(delegated,registry.path,nullptr,event.h);
     const auto beforeReads=accountFactRegistryReads.load(),beforeSession=leaseboundary::sessionLookups.load();need(lease.current(),"delegated-before-cancel-current");
     need(SetEvent(event.h)!=FALSE,"delegated-cancel-signal");need(!lease.current(),"delegated-observed-cancel-refused");
     // Deliberate fixture-only contract violation: Observer never resets these
     // events. Only the SAME child instance's permanent revocation is asserted.
     need(ResetEvent(event.h)!=FALSE,"delegated-fixture-only-reset");need(WaitForSingleObject(event.h,0)==WAIT_TIMEOUT,"delegated-fixture-reset-actually-unsignalled");
     for(unsigned i=0;i<1001;++i)need(!lease.current(),"delegated-observed-cancel-cannot-rearm");
     need(accountFactRegistryReads.load()==beforeReads,"delegated-revoked-zero-registry");need(leaseboundary::sessionLookups.load()==beforeSession,"delegated-revoked-zero-session-discovery");
     report["delegatedResetRejections"]=1001;report["delegatedObservedCancellationLatched"]=true;return true;
 });
 test("delegated cancellation wait failure remains latched after the port recovers",[&]{
     DelegatedCancelEvent event;auto delegated=r;delegated.delegatedIdentity=true;SteamLease lease(delegated,registry.path,nullptr,event.h);
     const auto beforeReads=accountFactRegistryReads.load(),beforeSession=leaseboundary::sessionLookups.load(),beforeFailures=leaseboundary::forcedEventWaitFailures.load();need(lease.current(),"delegated-before-wait-fault-current");
     struct Fault {Fault(){leaseboundary::failEventWait=true;}~Fault(){leaseboundary::failEventWait=false;}};
     {Fault fault;need(!lease.current(),"delegated-event-wait-failure-refused");}
     need(leaseboundary::forcedEventWaitFailures.load()>beforeFailures,"delegated-fault-reached-actual-event-wait");
     need(WaitForSingleObject(event.h,0)==WAIT_TIMEOUT,"delegated-event-still-live-after-port-fault");
     for(unsigned i=0;i<1001;++i)need(!lease.current(),"delegated-wait-failure-cannot-rearm");
     need(accountFactRegistryReads.load()==beforeReads,"delegated-wait-failure-zero-registry");need(leaseboundary::sessionLookups.load()==beforeSession,"delegated-wait-failure-zero-session-discovery");
     report["delegatedWaitFailureRejections"]=1001;report["delegatedObservedWaitFailureLatched"]=true;return true;
 });
 test("delegated revocation survives owner event handle closure",[&]{
     DelegatedCancelEvent owner;Handle childEvent;
     need(DuplicateHandle(GetCurrentProcess(),owner.h,GetCurrentProcess(),&childEvent.h,SYNCHRONIZE,FALSE,0)!=FALSE,"delegated-child-event-reference-create");
     auto delegated=r;delegated.delegatedIdentity=true;SteamLease lease(delegated,registry.path,nullptr,childEvent.h);need(lease.current(),"delegated-child-reference-current");
     const auto beforeReads=accountFactRegistryReads.load(),beforeSession=leaseboundary::sessionLookups.load();
     need(SetEvent(owner.h)!=FALSE,"delegated-owner-final-revoke");need(CloseHandle(owner.h)!=FALSE,"delegated-owner-event-close");owner.h=nullptr;
     need(!lease.current()&&!lease.current(),"delegated-owner-close-keeps-child-revoked");
     need(accountFactRegistryReads.load()==beforeReads,"delegated-owner-close-zero-registry");need(leaseboundary::sessionLookups.load()==beforeSession,"delegated-owner-close-zero-session-discovery");return true;
 });
 test("legacy registry admission remains strict beside delegated mode",[&]{
     need(!r.delegatedIdentity,"legacy-request-remains-nondelegated");registry.setRoot(root.wstring());registry.number(L"pid",r.steam.pid);registry.number(L"ActiveUser",123);
     const auto beforeReads=accountFactRegistryReads.load();need(admitted(r,registry),"legacy-exact-identity-admitted");need(accountFactRegistryReads.load()>beforeReads,"legacy-still-consumes-registry");
     registry.number(L"ActiveUser",124);need(!admitted(r,registry,"steam-account"),"legacy-account-switch-still-refuses");registry.number(L"ActiveUser",123);
     registry.setRoot((root/L"changed").wstring());need(!admitted(r,registry,"steam-root"),"legacy-root-switch-still-refuses");registry.setRoot(root.wstring());
     registry.number(L"pid",other);need(!admitted(r,registry,"steam-registered-pid"),"legacy-live-pid-switch-still-refuses");registry.number(L"pid",r.steam.pid);
     auto wrongSession=r;++wrongSession.windowsSession;need(!admitted(wrongSession,registry,"steam-windows-session"),"legacy-windows-session-still-refuses");return true;
 });
 test("delegated held process exit refuses despite an unsignalled cancellation event",[&]{
     DelegatedCancelEvent event;auto delegated=r;delegated.delegatedIdentity=true;SteamLease lease(delegated,registry.path,nullptr,event.h);need(lease.current(),"delegated-before-owned-process-exit-current");
     const auto beforeReads=accountFactRegistryReads.load(),beforeSession=leaseboundary::sessionLookups.load();report["ownedChildrenAtExit"]=children.stop();
     need(WaitForSingleObject(event.h,0)==WAIT_TIMEOUT,"delegated-process-exit-does-not-need-cancel");need(!lease.current(),"delegated-held-process-exit-refuses");
     need(accountFactRegistryReads.load()==beforeReads,"delegated-exited-zero-registry");need(leaseboundary::sessionLookups.load()==beforeSession,"delegated-exited-zero-session-discovery");return true;
 });
}
#endif
int wmain(int argc,wchar_t** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 if(argc==2&&std::wstring_view(argv[1])==L"--lease-child"){Sleep(INFINITE);return 0;}
 if(argc==4&&std::wstring_view(argv[1])==L"--live-readonly"){
  Json out={{"scope","Read-only legacy fixture lease; no bootstrap, CDP, registry writes or product launches"},{"realSteamActions",0},{"installedWrites",0}};
  Request r;r.steam.pid=static_cast<DWORD>(std::stoul(argv[2]));const std::wstring key=L"Software\\Valve\\Steam";r.steam.root=ymcc::steamsession::steamRoot(key);r.steam.account=ymcc::steamsession::regNumber(key,L"ActiveUser");const auto registered=ymcc::steamsession::regNumber(key,L"pid");auto h=ymcc::steamsession::verifiedProcess(r.steam.pid,r.steam.root,&r.steam.created);if(h)CloseHandle(h);ProcessIdToSessionId(r.steam.pid,&r.windowsSession);out["actualPid"]=r.steam.pid;out["registeredPid"]=registered;out["originalPidRuleAccepted"]=!registered||registered==r.steam.pid;out["verifiedProcess"]=h!=nullptr;out["sameWindowsUserAndSession"]=ymcc::deckymirror::sameWindowsUserAndSession(r.steam.pid);out["newPidRuleAccepted"]=registeredPidCompatible(registered,r.steam.pid,r.steam.root);try{LegacySteamLeaseFixture lease(r);out["fullLeaseCurrent"]=lease.current();}catch(const std::exception& e){out["fullLeaseCurrent"]=false;out["reason"]=e.what();}save(argv[3],out);std::cout<<out.dump()<<std::endl;return 0;
 }
#ifdef YMCC_STEAM_LEASE_DELEGATED_BOUNDARIES
  const bool delegatedOnly=argc==4&&std::wstring_view(argv[3])==L"--delegated-identity-only";
#else
  constexpr bool delegatedOnly=false;
#endif
  if(argc!=3&&!delegatedOnly)return 3;std::wstring ownedRegistryPath;const auto root=fs::absolute(argv[1]);fs::create_directories(root);Json rows=Json::array(),report={{"officialLoaderExecuted",false},{"realSteamActions",0},{"installedWrites",0},{"registryScope","unique private HKCU Software/YMCC.Tests/DeckySteamLease; no official registry writes"}};
 try{
  wchar_t image[32768]{};need(GetModuleFileNameW(nullptr,image,32768)!=0,"self-image");const auto steam=root/L"steam.exe",helper=root/L"unrelated.exe";fs::copy_file(image,steam);fs::copy_file(image,helper);
  Registry registry(root);ownedRegistryPath=registry.path;Children children;const auto pid=children.start(steam),other=children.start(steam),unrelated=children.start(helper);auto r=request(pid,root);registry.number(L"pid",pid);
  const auto test=[&](const char* name,auto action){bool okay=false;std::string error;try{okay=action();}catch(const std::exception& e){error=e.what();}Json row={{"name",name},{"pass",okay}};if(!error.empty())row["error"]=error;rows.push_back(row);std::cout<<(okay?"PASS ":"FAIL ")<<name<<std::endl;};
#ifdef YMCC_STEAM_LEASE_DELEGATED_BOUNDARIES
  if(delegatedOnly){delegatedSteamLeaseBoundaries(r,root,registry,children,other,unrelated,test,report);if(!report.contains("ownedChildrenAtExit"))report["ownedChildrenAtExit"]=children.stop();}else
#endif
  {
  test("exact registered Steam identity is admitted",[&]{return admitted(r,registry);});
  registry.number(L"pid",0);test("missing registered PID preserves verified observer lease",[&]{return admitted(r,registry);});
  registry.number(L"pid",MAXDWORD);test("stale absent registered PID preserves verified observer lease",[&]{return admitted(r,registry);});
  registry.number(L"pid",unrelated);test("PID reused by verified unrelated image cannot block current Steam",[&]{return admitted(r,registry);});
  leaseboundary::denied=unrelated;test("unreadable registered process never grants stale PID recovery",[&]{return !admitted(r,registry);});leaseboundary::denied=0;
  registry.number(L"pid",other);test("different live registered Steam remains a session conflict",[&]{return !admitted(r,registry,"steam-registered-pid");});
  registry.number(L"pid",MAXDWORD);registry.number(L"ActiveUser",124);test("stale PID cannot bypass changed Steam account",[&]{return !admitted(r,registry,"steam-account");});registry.number(L"ActiveUser",123);
  registry.number(L"ActiveUser",0);
  test("registry zero alone never authorizes an anonymous account",[&]{return !admitted(r,registry,"steam-account");});
  auto offline=r;offline.liveUiAccount=true;
  test("typed UI provenance permits only process observation before independent bootstrap guard",[&]{return admitted(offline,registry);});
  registry.number(L"ActiveUser",124);
  test("current UI provenance ignores a stale explicit registry account",[&]{return admitted(offline,registry);});
  registry.number(L"ActiveUser",123);
  test("offline to online same account retains the verified process lease",[&]{return admitted(offline,registry);});
  const Json mode{{"known",true},{"running",true},{"mode","bigpicture"},{"pid",r.steam.pid},{"birth",std::to_string(r.steam.created)},
      {"observedAccount",123u},{"deckyAccountWatchAvailable",true}};
  unsigned projectionChecks=0;
  const auto pc=[&](bool value,const char* label){++projectionChecks;need(value,label);};
  auto empty=r.steam;empty.account=0;
  test("offline account resolves only a verified same-process current UI proof",[&]{
      for(const DWORD cached:{0u,123u,124u}){auto selected=empty;selected.account=cached;
          const auto a=ymcc::deckysession::resolve(selected,mode);
          if(a.value!=123||a.fromUi!=(cached!=123)||!ymcc::deckysession::bigPicture(selected,mode))return false;
      }
      using namespace ymcc::steamaccount;
      const auto beforeReads=accountFactRegistryReads.load();
      auto facts=project(empty,mode);
      pc(!facts.identitySnapshotAvailable&&!facts.registryCandidate.readStatusAvailable,"legacy Session cannot invent readable registry evidence");
      pc(facts.ui.currentAccountKnown()&&facts.ui.samePidAndBirth&&facts.ui.accountID32==123,"actual current UI account projected separately");
      pc(facts.registryCandidate.accountID32==0&&facts.candidateUiConflict==Truth::Unknown,"zero registry candidate is Unknown conflict");
      pc(!facts.ui.cmSteamID64.has_value(),"no CM 64-bit proof is manufactured from account32");
      auto exact=r.steam;facts=project(exact,mode);
      pc(facts.candidateUiConflict==Truth::False&&!select(facts,Policy::UiLocal).fromUi,"matching accounts preserve original fromUi=false");
      exact.account=124;facts=project(exact,mode);
      pc(facts.candidateUiConflict==Truth::True&&facts.registryCandidate.accountID32==124&&facts.ui.accountID32==123,"conflict keeps both distinct original candidates");
      pc(select(facts,Policy::UiLocal).value==123&&select(facts,Policy::UiLocal).fromUi,"only explicit UiLocal policy chooses current UI over stale candidate");
      auto m=mode;m["modeEpoch"]="renderer:7";facts=project(exact,m);
      pc(facts.ui.modeEpochKnown&&facts.ui.documentKnown&&facts.ui.document=="renderer"&&facts.ui.documentEntry==7,"common original epoch parser projects document and entry");
      m["modeEpoch"]=std::string(97,'x');facts=project(exact,m);
      pc(!facts.ui.modeEpochKnown&&!facts.ui.documentKnown&&select(facts,Policy::UiLocal).value==123,"malformed epoch adds no new legacy Decky permission gate");
      for(const auto* key:{"steamid","steamID64","GameID","appID","overlayID"}){m=mode;m[key]="76561197960265851";pc(!project(exact,m).ui.cmSteamID64.has_value(),"aliases never become actual CM SteamID64 evidence");}
      ymcc::steamsession::IdentitySnapshot receipt;receipt.session=exact;receipt.presence=1;receipt.registeredPid=exact.pid;
      receipt.rootReadError=receipt.registeredPidReadError=receipt.accountReadError=ERROR_SUCCESS;
      receipt.revision=9;receipt.processEpoch=3;receipt.accountEpoch=4;receipt.observedAt=77;
      facts=project(receipt,mode);
      pc(facts.identitySnapshotAvailable&&facts.identityRevision==9&&facts.processEpoch==3&&facts.accountEpoch==4&&facts.observedAt==77,"coherent identity epochs stay attached to projection");
      pc(facts.registryCandidate.registryReadKnown&&facts.registryCandidate.pidMatchesSession==Truth::True&&facts.registryCandidate.processAssociation==Truth::True,"readable registry correlation remains distinct from UI proof");
      receipt.session.account=0;facts=project(receipt,mode);
      pc(facts.registryCandidate.registryReadKnown&&facts.registryCandidate.accountID32==0&&facts.candidateUiConflict==Truth::Unknown,"readable zero remains distinct from failed read and logout");
      receipt.accountReadError=ERROR_ACCESS_DENIED;facts=project(receipt,mode);
      pc(!facts.registryCandidate.registryReadKnown&&facts.registryCandidate.readError==ERROR_ACCESS_DENIED&&facts.ui.currentAccountKnown(),"registry denial does not erase independent same-process UI evidence");
      receipt.registeredPid++;facts=project(receipt,mode);
      pc(facts.registryCandidate.pidMatchesSession==Truth::False&&facts.registryCandidate.processAssociation==Truth::False,"foreign registered PID cannot gain process association");
      receipt.registeredPidReadError=ERROR_ACCESS_DENIED;facts=project(receipt,mode);
      pc(facts.registryCandidate.pidMatchesSession==Truth::Unknown&&facts.registryCandidate.processAssociation==Truth::Unknown,"PID read denial remains Unknown");
      receipt.session=exact;receipt.accountReadError=ERROR_ACCESS_DENIED;facts=project(receipt,Json::object());
      pc(select(facts,Policy::UiLocal).value==124&&!select(facts,Policy::UiLocal).fromUi&&!facts.registryCandidate.registryReadKnown,"legacy fallback preserves candidate without upgrading failed evidence");
      for(unsigned i=0;i<1001;++i){const auto held=project(receipt,mode);if(held.ui.accountID32!=123)return false;}
      pc(accountFactRegistryReads.load()==beforeReads,"1001 common projections perform zero registry discovery");
      report["accountFactsProjectionChecks"]=projectionChecks;report["accountFactsRegistryReads"]=accountFactRegistryReads.load()-beforeReads;
      return true;
  });
  test("missing raced malformed or foreign offline proofs never authorize",[&]{
      for(const auto& bad:std::vector<Json>{nullptr,true,0,-1,1.5,"123",4294967296ull}){
          auto m=mode;m["observedAccount"]=bad;if(ymcc::deckysession::resolve(empty,m).value)return false;
      }
      for(const auto* field:{"known","running","deckyAccountWatchAvailable"}){auto m=mode;m[field]=false;if(ymcc::deckysession::resolve(empty,m).value)return false;}
      auto m=mode;m["birth"]="1";if(ymcc::deckysession::resolve(empty,m).value)return false;
      m=mode;m["pid"]=r.steam.pid+1;if(ymcc::deckysession::resolve(empty,m).value)return false;
      return ymcc::deckysession::resolve(empty,Json::object()).value==0;
  });
  registry.setRoot((root/L"changed").wstring());test("stale PID cannot bypass changed Steam root",[&]{return !admitted(r,registry,"steam-root");});registry.setRoot(root.wstring());
  auto changed=r;++changed.steam.created;test("stale PID cannot bypass process birth mismatch",[&]{return !admitted(changed,registry,"steam-process-or-birth");});
  changed=r;++changed.windowsSession;test("stale PID cannot bypass Windows session mismatch",[&]{return !admitted(changed,registry,"steam-windows-session");});
  changed=r;changed.steam.root=ymcc::steamlive::normalizedPath((root/L"changed").wstring());test("stale PID cannot bypass request image mismatch",[&]{return !admitted(changed,registry);});
  test("held lease rejects a later real registered session switch",[&]{LegacySteamLeaseFixture lease(r,registry.path);registry.number(L"pid",other);const bool refused=!lease.current();registry.number(L"pid",MAXDWORD);return refused;});
  const auto before=handles();for(unsigned i=0;i<64;++i)admitted(changed,registry);test("rejected identities return their process handles",[&]{return handles()==before;});
  report["ownedChildrenAtExit"]=children.stop();test("terminated target cannot regain admission from stale registry",[&]{return !admitted(r,registry);});
  }
  std::string scopeName;for(const auto c:registry.path)scopeName.push_back(static_cast<char>(c));report["privateRegistryPath"]=scopeName;report["pass"]=std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.at("pass")==true;});
 }catch(const std::exception& e){report["pass"]=false;report["error"]=e.what();}
 HKEY retained=nullptr;const auto registryEnd=ownedRegistryPath.empty()?ERROR_FILE_NOT_FOUND:RegOpenKeyExW(HKEY_CURRENT_USER,ownedRegistryPath.c_str(),0,KEY_READ,&retained);if(retained)RegCloseKey(retained);report["privateRegistryRemoved"]=registryEnd==ERROR_FILE_NOT_FOUND;report["pass"]=report.value("pass",false)&&report.at("privateRegistryRemoved")==true;report["cases"]=rows;report["count"]=rows.size();save(argv[2],report);return report.value("pass",false)?0:2;
}
