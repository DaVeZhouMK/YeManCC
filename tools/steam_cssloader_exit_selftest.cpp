// Real owned inert processes exercise the production exit HANDLE wait and Runtime.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_cssloader_guard.h"
#include <json.hpp>
#include <atomic>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace ymcc;using json=nlohmann::json;using Session=steamsession::Session;
static unsigned checks=0;static void check(bool yes,const char*text){if(!yes)throw std::runtime_error(text);++checks;std::cout<<"PASS "<<text<<std::endl;}
template<class F>static bool until(F test,DWORD ms=4000){const auto due=GetTickCount64()+ms;while(!test()){if(GetTickCount64()>=due)return false;Sleep(2);}return true;}
static std::wstring self(){wchar_t f[32768]{};GetModuleFileNameW(nullptr,f,32768);return f;}
static bool live(HANDLE p){return p&&WaitForSingleObject(p,0)==WAIT_TIMEOUT;}
static HANDLE copy(HANDLE p,bool process){HANDLE h=nullptr;if(!DuplicateHandle(GetCurrentProcess(),p,GetCurrentProcess(),&h,SYNCHRONIZE|(process?PROCESS_QUERY_LIMITED_INFORMATION:0),FALSE,0))throw std::runtime_error("fixture copy");return h;}
static ULONGLONG cpu(){FILETIME b{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&b,&e,&k,&u);return(ULONGLONG(k.dwHighDateTime)<<32)+k.dwLowDateTime+(ULONGLONG(u.dwHighDateTime)<<32)+u.dwLowDateTime;}
static std::atomic<unsigned>ownedCreated=0,ownedClosed=0,ownedRemaining=0;
struct Child {HANDLE p=nullptr;~Child(){if(p){if(live(p)){TerminateProcess(p,0);WaitForSingleObject(p,2000);}if(live(p))++ownedRemaining;else ++ownedClosed;CloseHandle(p);}}};
static std::atomic<bool>g_exitRequested=false;static struct{bool isolated=false;}g_aiFanMockSession;
enum class PowerLifecycle{Ready,Suspended};static std::atomic<PowerLifecycle>g_powerLifecycle=PowerLifecycle::Ready;
static struct{Session value;Session sessionSnapshot(){return value;}}g_steamSettingsObserver;
static struct{std::function<bool(const Session&)> port;bool invalidateForResume(const Session&s){return port&&port(s);}}g_steamCssUse;
static struct{std::function<bool(const Session&)> port;bool requestRefresh(const Session&s){return port&&port(s);}}g_steamModeObserver;
#include "production-css-exit.h"
struct Scene {
 std::filesystem::path dir,link,image;Session session{L"C:\\owned-fixture-steam",42,0,100};
 std::vector<std::unique_ptr<Child>> children;std::atomic<HANDLE>active=nullptr;deckylaunch::Handle helper,eventBackend;
 std::atomic<unsigned>uses=0,starts=0,reads=0,refreshes=0,invalidations=0,rebinds=0;
 std::atomic<bool>confirmed=false,blocked=false,editing=false,refreshAccepted=true,invalidateAccepted=true;
 std::atomic<steamcss::UiMode>mode=steamcss::UiMode::BigPicture;
 steamcss::Runtime runtime;DWORD delay;
 Scene(const std::filesystem::path&root,const wchar_t*name,DWORD ms=180):dir(root/name),link(dir/L"links/backend.path"),image(dir/L"CssLoader-Standalone-Headless.exe"),delay(ms){
  std::filesystem::create_directories(link.parent_path());std::filesystem::copy_file(self(),image);std::ofstream(link)<<"C:\\owned-css\\CssLoader-Standalone-Headless.exe";
  helper.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!helper.value)throw std::runtime_error("fixture helper event");
  g_steamSettingsObserver.value=session;g_powerLifecycle=PowerLifecycle::Ready;g_exitRequested=false;g_aiFanMockSession.isolated=false;
  g_steamCssUse.port=[this](const auto&){++invalidations;if(!invalidateAccepted)return false;confirmed=false;return true;};
  g_steamModeObserver.port=[this](const auto&){++refreshes;mode=steamcss::UiMode::Unknown;return refreshAccepted.load();};
 }
 ~Scene(){runtime.shutdown();g_steamCssUse.port={};g_steamModeObserver.port={};}
 HANDLE launch(){auto child=std::make_unique<Child>();auto command=L"\""+image.wstring()+L"\" --inert";STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};if(!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))throw std::runtime_error("owned inert launch");CloseHandle(pi.hThread);child->p=pi.hProcess;++ownedCreated;const auto p=child->p;children.push_back(std::move(child));active=p;return p;}
 steamcss::Result probe(){if(blocked)return{nullptr,0,"blocked","fixture-port-conflict"};const auto p=active.load();if(live(p))return{copy(p,true),GetProcessId(p),"running",{},false};return{nullptr,0,"missing",{},false};}
 void start(bool fakeHandle=false){
  if(fakeHandle){eventBackend.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);active=eventBackend.value;}else launch();
  steamcss::Config c;c.linkFile=link.wstring();c.delays={2,5,8};c.modeDelays={2,10,20};c.crashDelayMs=delay;
  c.mayLaunch=[] {return !g_exitRequested&&!g_aiFanMockSession.isolated&&g_powerLifecycle==PowerLifecycle::Ready;};
  c.readMode=[this](const auto&){return steamcss::ModeSample{mode.load(),"same-document:1"};};
  c.invalidateBackend=productionExitInvalidation;c.refreshMode=productionExitRefresh;
  c.ensure=[this,fakeHandle](const auto&,const auto&,const auto&){++reads;if(mode!=steamcss::UiMode::BigPicture||g_powerLifecycle!=PowerLifecycle::Ready)return steamcss::Result{nullptr,0,"waiting-steam-ui","unconfirmed-mode"};if(fakeHandle&&active==eventBackend.value)return steamcss::Result{copy(eventBackend.value,false),900,"running",{},false};return probe();};
  c.confirmIdle=[this](const auto&,bool missing,bool){++uses;ResetEvent(helper.value);if(editing){SetEvent(helper.value);return copy(helper.value,false);}if(missing){if(blocked||live(active))throw std::runtime_error("duplicate fixture start");++starts;launch();}confirmed=true;SetEvent(helper.value);return copy(helper.value,false);};
  c.probeExisting=[this,fakeHandle]{++rebinds;if(fakeHandle&&active==eventBackend.value)return steamcss::Result{copy(eventBackend.value,false),900,"running",{},false};return probe();};
  c.isResidentConfirmed=[this](const auto&){return confirmed.load();};
  check(runtime.initialize(c),"actual CSS Runtime initialized");runtime.observeSteam(session);check(until([&]{return runtime.snapshot().phase=="ready";}),"initial actual resident confirmation is ready");
 }
 void stop(DWORD code=0xc0000005UL){auto p=active.load();if(!p||!live(p)||!TerminateProcess(p,code)||WaitForSingleObject(p,2000)!=WAIT_OBJECT_0)throw std::runtime_error("owned inert exit");}
 void fresh(){mode=steamcss::UiMode::BigPicture;runtime.observeMode(session,{mode.load(),"same-document:1"});}
 void recover(unsigned count){check(until([&]{return refreshes>=count;}),"abnormal exit reaches one delayed fresh-mode request");check(!confirmed,"old successful receipt is revoked before recovery");const auto n=uses.load();Sleep(25);check(uses==n,"unknown fresh mode cannot create a recovery helper");fresh();check(until([&]{return runtime.snapshot().phase=="ready"&&starts==count;}),"one fresh Big Picture result restores one missing backend");}
};
int wmain(int argc,wchar_t**argv){try{
 if(argc>1&&std::wstring(argv[1])==L"--inert"){Sleep(INFINITE);return 0;}
 if(argc!=2)throw std::runtime_error("owned evidence root required");const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
 steamcss::ExitRecovery cap;check(!cap.pending()&&cap.timeout(0)==INFINITE,"no crash timer exists before an exit event");check(cap.admit(100)&&cap.timeout(30099)==1&&!cap.take(30099),"production recovery waits full thirty seconds");check(!cap.admit(200)&&cap.used()==1,"duplicate event cannot postpone or renew a pending recovery");check(cap.take(30100)&&cap.admit(40000)&&cap.take(70000)&&!cap.admit(80000),"two recovery waves exhaust the same Steam session budget");cap.reset();check(cap.admit(90000),"explicit reset renews the bounded budget");
 check(!steamcss::ExitRecovery::abnormal(false,1)&&!steamcss::ExitRecovery::abnormal(true,0)&&!steamcss::ExitRecovery::abnormal(true,STILL_ACTIVE)&&!steamcss::ExitRecovery::abnormal(true,0xc000013aUL),"unknown, clean, live and console-close codes never authorize restart");
 unsigned crashStarts=0;double idleCpu=0;
 {
  Scene f(root,L"capped");f.start();
  auto stale=f.session;++stale.created;check(!productionExitInvalidation(stale)&&f.invalidations==0,"exact invalidation rejects a reused Steam PID with the wrong birth");
  g_exitRequested=true;check(!productionExitInvalidation(f.session)&&f.invalidations==0,"exit fence rejects invalidation after shutdown admission closes");g_exitRequested=false;
  g_aiFanMockSession.isolated=true;check(!productionExitRefresh(f.session)&&f.refreshes==0,"isolation rejects actual recovery refresh before shared IO");g_aiFanMockSession.isolated=false;
  const auto first=f.active.load();f.stop();check(until([&]{return f.runtime.snapshot().phase=="recovering";}),"abnormal process HANDLE exit schedules bounded recovery");check(!f.confirmed&&f.invalidations==1,"exact production callback revokes the old confirmation");
  for(unsigned n=0;n<50000;++n)f.runtime.observeMode(f.session,{steamcss::UiMode::BigPicture,"same-document:1"});Sleep(35);check(f.starts==0&&f.reads==1,"50000 mode hints do not shorten the exit delay or search again");
  f.recover(1);check(!live(first)&&live(f.active)&&f.children.size()==2,"replacement starts only after previous owned process is actually gone");
  f.stop();f.recover(2);f.stop();check(until([&]{return f.runtime.snapshot().reason=="loader-crash-budget-exhausted";}),"third abnormal exit stops at the hard crash cap");
  for(unsigned n=0;n<1000;++n)f.runtime.observeMode(f.session,{steamcss::UiMode::BigPicture,"same-document:2"});check(f.runtime.requestResumeRecovery(f.session,1),"a real Ready event can be consumed without resetting exhausted budget");Sleep(300);check(f.starts==2&&f.refreshes==2&&f.runtime.snapshot().automaticRecoveries==2,"mode and Ready events cannot bypass crash exhaustion");const auto before=cpu();const auto r=f.reads.load();Sleep(200);idleCpu=(cpu()-before)/10000.0;check(f.reads==r,"exhausted backend waits without repeated process discovery");crashStarts=f.starts;
  f.runtime.requestManualRecovery();f.fresh();check(until([&]{return f.runtime.snapshot().phase=="ready"&&f.starts==3;}),"an explicit manual action can recover after the automatic cap");
 }
 {
  Scene f(root,L"clean");f.start();f.stop(0);check(until([&]{return f.runtime.snapshot().reason=="loader-exit-not-abnormal";}),"normal process exit stays closed");check(!f.confirmed,"normal exit also revokes stale success");f.fresh();f.runtime.requestResumeRecovery(f.session,1);Sleep(300);check(f.starts==0&&f.refreshes==0,"normal close is not undone by mode or Ready hints");
 }
 {
  Scene f(root,L"unknown");f.start(true);SetEvent(f.eventBackend.value);check(until([&]{return f.runtime.snapshot().reason=="loader-exit-not-abnormal";}),"nonprocess wait handle gives unknown exit rather than a crash grant");Sleep(250);check(f.starts==0&&f.refreshes==0,"unknown exit never starts a missing backend");
 }
 {
  Scene f(root,L"off");f.start();f.stop();check(until([&]{return f.runtime.snapshot().phase=="recovering";}),"OFF test owns a pending crash window");std::filesystem::remove(f.link);check(until([&]{return f.runtime.snapshot().phase=="disabled";}),"link OFF cancels pending recovery through directory event");Sleep(250);check(f.starts==0&&f.refreshes==0,"OFF cancels delayed CSS work with no new process");
 }
 {
  Scene f(root,L"foreign");f.start();f.stop();check(until([&]{return f.runtime.snapshot().phase=="recovering";}),"conflict test receives real exit");f.blocked=true;check(until([&]{return f.refreshes==1;}),"conflict recovery requests fresh mode once");f.fresh();check(until([&]{return f.runtime.snapshot().phase=="exhausted";}),"ambiguous existing owner uses only the finite probe window");check(f.starts==0&&f.uses==1,"foreign or unreadable existing backend never permits another start");f.runtime.requestResumeRecovery(f.session,1);Sleep(250);check(f.refreshes==1&&f.starts==0,"failed ambiguous recovery cannot replay itself on Ready");
 }
 {
  Scene f(root,L"survivor");f.start();const auto other=f.launch();f.active=f.children.front()->p;f.stop();f.active=other;check(until([&]{return f.refreshes==1;}),"surviving family still gets one bounded recheck");f.fresh();check(until([&]{return f.uses==2&&f.runtime.snapshot().phase=="ready";}),"already-running survivor is reused and reconfirmed");check(f.starts==0&&live(other),"surviving process prevents replacement and is never terminated");
 }
 {
  Scene f(root,L"sleep");f.start();g_powerLifecycle=PowerLifecycle::Suspended;f.stop();check(until([&]{return f.runtime.snapshot().reason=="loader-crash-awaiting-ready";}),"crash during suspension parks after the original delay");check(f.starts==0&&f.refreshes==0,"suspended power state cannot launch CSS or issue shared mode IO");g_powerLifecycle=PowerLifecycle::Ready;check(f.runtime.requestResumeRecovery(f.session,1),"new committed Ready resumes the already-budgeted recovery");f.recover(1);check(f.starts==1,"sleep recovery consumes one crash wave rather than resetting cap");
 }
 {
  Scene f(root,L"shutdown");f.start();f.stop();check(until([&]{return f.runtime.snapshot().phase=="recovering";}),"shutdown test owns pending delayed work");f.runtime.shutdown();Sleep(250);check(f.starts==0&&f.refreshes==0,"shutdown destroys pending delay and late events cannot revive it");
 }
 {
  Scene f(root,L"editor");f.start();f.stop();check(until([&]{return f.refreshes==1;}),"editor scenario reaches delayed mode confirmation");f.editing=true;f.fresh();check(until([&]{return f.uses==2&&f.runtime.snapshot().reason=="appearance-unconfirmed";}),"editor-owned backend denies recovery rather than asserting false success");check(f.starts==0&&!f.confirmed,"editor protection does not start a missing competing backend");f.editing=false;f.runtime.requestResumeRecovery(f.session,1);Sleep(250);check(f.starts==0&&f.refreshes==1,"an uncertain editor-protected use is not automatically replayed on Ready");
 }
 {
  Scene f(root,L"desktop");f.start();f.stop();check(until([&]{return f.refreshes==1;}),"desktop scenario reaches shared refresh");f.mode=steamcss::UiMode::Desktop;f.runtime.observeMode(f.session,{f.mode.load(),"same-document:1"});Sleep(250);check(f.starts==0&&f.uses==1,"desktop Steam never authorizes a CSS helper or a mode change");f.fresh();check(until([&]{return f.starts==1&&f.runtime.snapshot().phase=="ready";}),"later user-driven Big Picture confirmation may finish the pending finite recovery");
 }
 {
  Scene f(root,L"unverified");f.start();f.invalidateAccepted=false;f.stop();check(until([&]{return f.runtime.snapshot().reason=="loader-crash-recovery-unverified";}),"failed confirmation invalidation denies crash recovery");Sleep(250);check(f.starts==0&&f.refreshes==0,"unverified invalidation never grants a replacement");
 }
 {
  Scene f(root,L"mode-refresh-failure");f.start();f.refreshAccepted=false;f.stop();check(until([&]{return f.runtime.snapshot().reason=="loader-crash-mode-refresh-unavailable";}),"failed shared refresh parks instead of accepting old Big Picture cache");f.fresh();f.runtime.requestResumeRecovery(f.session,1);Sleep(250);check(f.starts==0&&f.refreshes==1,"uncertain mode refresh cannot replay on later hints");
 }
 check(ownedRemaining==0&&ownedCreated==ownedClosed,"all owned inert child processes are observed exited and their handles released");
 const json result={{"ok",true},{"checks",checks},{"automaticCrashStartsBeforeManual",crashStarts},{"stormHints",50000},{"idleCpuMs",idleCpu},{"idleSampleMs",200},{"realSteamOperations",0},{"realCssOperations",0},{"ownedInertProcesses",true},{"ownedCreated",ownedCreated.load()},{"ownedClosed",ownedClosed.load()},{"ownedRemaining",ownedRemaining.load()},{"physicalSleep",false},{"scope","Actual guard and exact production invalidation/refresh lambdas; real owned process HANDLE exits; external Steam/CSS process discovery and helper IO remain inert fixture boundaries"}};std::ofstream(root/L"result.json")<<result.dump(2);std::cout<<result.dump()<<std::endl;return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
