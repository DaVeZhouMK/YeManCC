#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_cssloader_use.h"
#include <json.hpp>
#include <atomic>
#include <fstream>
#include <iostream>
#include <memory>
using namespace ymcc;using Session=steamsession::Session;using json=nlohmann::json;
static unsigned checks=0;static void check(bool yes,const char*t){if(!yes)throw std::runtime_error(t);++checks;std::cout<<"PASS "<<t<<std::endl;}
template<class F>static bool until(F f,DWORD ms=5000){const auto due=GetTickCount64()+ms;while(!f()){if(GetTickCount64()>=due)return false;Sleep(3);}return true;}
static std::wstring self(){wchar_t p[32768]{};GetModuleFileNameW(nullptr,p,32768);return p;}
static bool live(HANDLE p){return p&&WaitForSingleObject(p,0)==WAIT_TIMEOUT;}
static HANDLE copy(HANDLE p){HANDLE h=nullptr;if(!DuplicateHandle(GetCurrentProcess(),p,GetCurrentProcess(),&h,SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,0))throw std::runtime_error("owned duplicate");return h;}
static ULONGLONG birth(HANDLE p){FILETIME b,e,k,u;GetProcessTimes(p,&b,&e,&k,&u);return(ULONGLONG(b.dwHighDateTime)<<32)+b.dwLowDateTime;}
static ULONGLONG cpu(){FILETIME b,e,k,u;GetProcessTimes(GetCurrentProcess(),&b,&e,&k,&u);return(ULONGLONG(k.dwHighDateTime)<<32)+k.dwLowDateTime+(ULONGLONG(u.dwHighDateTime)<<32)+u.dwLowDateTime;}
static unsigned ownedCreated=0,ownedClosed=0;
struct Child{HANDLE p=nullptr;~Child(){if(p){if(live(p))TerminateProcess(p,0);WaitForSingleObject(p,2000);if(!live(p))++ownedClosed;CloseHandle(p);}}};
static HANDLE launch(const std::wstring&exe){auto cmd=L"\""+exe+L"\" --inert";STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))throw std::runtime_error("owned child launch");CloseHandle(pi.hThread);++ownedCreated;return pi.hProcess;}
static std::atomic<bool>g_exitRequested=false;static struct{bool isolated=false;}g_aiFanMockSession;
enum class PowerLifecycle{Ready,Suspended};static std::atomic<PowerLifecycle>g_powerLifecycle=PowerLifecycle::Ready;
static struct{Session value;Session sessionSnapshot(){return value;}}g_steamSettingsObserver;
static struct{std::function<HANDLE(const Session&)>port;HANDLE confirmExisting(const Session&s){return port?port(s):nullptr;}}g_steamCssUse;
static struct{std::function<bool(const Session&,ULONGLONG)>port;bool requestReadOnlyRebind(const Session&s,ULONGLONG g){return port&&port(s,g);}}g_steamCssGuard;
static std::string W2U(const std::wstring&s){const auto count=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string text(count,0);if(count)WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),text.data(),count,nullptr,nullptr);return text;}
static void deckySidebarDiagnostic(const std::string&,const json&,bool){}static void deckySidebarPost(const std::string&,const json&){}
#include "production-css-manual.h"
static const char*helper=R"PS(param([string]$Action,[uint32]$SessionPid,[string]$SessionBirth,[string]$BackendPath,[string]$ReadyEvent,[string]$IdleEvent,[int]$GateTimeoutMs,[switch]$StartBackend,[switch]$VerifyOnly)
$ErrorActionPreference='Stop'
# ResidentBackendPolicy=3 AutomaticResultProtocol=2 ManualObservationProtocol=1
$elevated=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if($Action -eq 'Ensure'){
 if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'permit-initialize'))){throw 'unexpected initialization'}
 [IO.File]::AppendAllText((Join-Path $PSScriptRoot 'token-order.txt'),('Ensure:'+([int]$elevated))+[Environment]::NewLine)
 Start-Sleep -Milliseconds 700
 if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'fail-initialize')){exit 1}
 [IO.File]::AppendAllText((Join-Path $PSScriptRoot 'token-order.txt'),'EnsureComplete'+[Environment]::NewLine);exit 0
}
if($Action -ne 'Automatic' -or $StartBackend){throw 'owned helper must not start a backend'}
[IO.File]::AppendAllText((Join-Path $PSScriptRoot 'token-order.txt'),('Automatic:'+([int]$elevated))+[Environment]::NewLine)
[IO.File]::AppendAllText((Join-Path $PSScriptRoot 'helper-calls.txt'),([int][bool]$VerifyOnly).ToString()+[Environment]::NewLine)
$base=$ReadyEvent.Substring(0,$ReadyEvent.Length-6)
$ready=[Threading.EventWaitHandle]::OpenExisting($ReadyEvent);$idle=[Threading.EventWaitHandle]::OpenExisting($IdleEvent)
$map=[IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting($base+'.result');$view=$map.CreateViewAccessor(0,32)
try{
 if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'slow')){Start-Sleep -Milliseconds 500}
 $ready.Set()|Out-Null
 if(!$idle.WaitOne(5000)){throw 'owned idle gate timeout'}
 if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'slow')){Start-Sleep -Milliseconds 1700}
 $bad=Test-Path -LiteralPath (Join-Path $PSScriptRoot 'fail')
 $values=@(0x594D4353,2,8,0,31,1,0,0);if($bad){$values=@(0x594D4353,2,5,6,3,1,0,0)}
 for($n=1;$n -lt 8;$n++){$view.Write($n*4,[int]$values[$n])};$view.Write(0,[int]$values[0]);$view.Flush()
 if($bad){exit 1}
}finally{$view.Dispose();$map.Dispose();$idle.Dispose();$ready.Dispose()}
)PS";
static void write(const std::filesystem::path&p,const std::string&s){std::ofstream(p,std::ios::binary)<<s;}
struct Scene{
 std::filesystem::path dir,link,entry;Session session;Child steam,css;steamcssuse::Runtime use;steamcss::Runtime guard;deckylaunch::Handle lastCompletion;
 std::atomic<unsigned>helpers=0,manualEnds=0,readonly=0,ensures=0,initialUses=0,rebinds=0,readySignals=0;
 std::atomic<bool>editor=false,blocked=false,permitConfirmation=true;std::atomic<steamcss::UiMode>mode=steamcss::UiMode::BigPicture;
 std::atomic<unsigned>epoch=1;bool guardStarted=false;
 Scene(const std::filesystem::path&root,const wchar_t*name,DWORD lifetime=5000,DWORD minimumHold=0):dir(root/name),link(dir/L"steam-appearance/backend.path"),entry(dir/L"steam-appearance.ps1"),use(false,1,[this]{return editor?steamcsseditor::State::Active:steamcsseditor::State::Absent;}){
  std::filesystem::create_directories(link.parent_path());const auto steamImage=dir/L"steam.exe",backend=dir/L"CssLoader-Standalone-Headless.exe";std::filesystem::copy_file(self(),steamImage);std::filesystem::copy_file(self(),backend);steam.p=launch(steamImage.wstring());css.p=launch(backend.wstring());session={dir.wstring(),GetProcessId(steam.p),0,birth(steam.p)};
  write(link,backend.string());write(entry,helper);write(dir/L"steam-appearance/resident.ps1","function Start-ResidentAppearanceBackend");write(dir/L"steam-appearance/idle.ps1","AppearanceRenderProtocol=4 function Get-AppearanceVariableDefaults");
  g_steamSettingsObserver.value=session;g_powerLifecycle=PowerLifecycle::Ready;g_exitRequested=false;g_aiFanMockSession.isolated=false;
  g_steamCssGuard.port=[this](const auto&s,auto g){++manualEnds;return guardStarted?guard.requestReadOnlyRebind(s,g):true;};
  g_steamCssUse.port=[this](const auto&s){++readonly;auto h=permitConfirmation?use.confirmExisting(s):nullptr;if(h){if(lastCompletion.value)CloseHandle(lastCompletion.value);lastCompletion.value=nullptr;if(!DuplicateHandle(GetCurrentProcess(),h,GetCurrentProcess(),&lastCompletion.value,0,FALSE,DUPLICATE_SAME_ACCESS))throw std::runtime_error("owned completion duplicate");}return h;};
  check(use.initialize(entry.wstring(),link.wstring(),lifetime,0,[this](const auto&stage,const auto&s,const auto&b,const auto&r){if(stage=="automatic-helper-start")++helpers;if(stage=="automatic-helper-ready")++readySignals;productionManualChanged(stage,s,b,r);},minimumHold),"owned use runtime initializes");use.observeSteam(session);
 }
 ~Scene(){guard.shutdown();guardStarted=false;g_steamCssGuard.port={};g_steamCssUse.port={};use.shutdown();}
 steamcss::ModeSample sample(){return{mode.load(),"doc/owned:mode/"+std::to_string(epoch.load())};}
 void startGuard(){steamcss::Config c;c.linkFile=link.wstring();c.delays={2,5,9};c.modeDelays={2,5,15};c.crashDelayMs=60;
  c.ensure=[this](const auto&,const auto&,const auto&){++ensures;return steamcss::Result{copy(css.p),GetProcessId(css.p),"running",{},false};};c.probeExisting=[this]{++rebinds;return steamcss::Result{copy(css.p),GetProcessId(css.p),"running",{},false};};
  c.confirmIdle=[this](const auto&s,bool start,bool renew){++initialUses;if(start)throw std::runtime_error("existing owned CSS received a create grant");return use.confirmAndIdle(s,false,renew);};c.invalidateBackend=[this](const auto&s){return use.invalidateForResume(s);};c.confirmExisting=productionManualConfirm;c.readMode=[this](const auto&){return sample();};c.isResidentConfirmed=[this](const auto&s){return use.residentConfirmed(s);};c.mayLaunch=[this]{return !blocked&&!g_exitRequested&&g_powerLifecycle==PowerLifecycle::Ready;};
  check(guard.initialize(c),"owned guard initializes");guardStarted=true;guard.observeSteam(session);check(until([this]{return guard.snapshot().phase=="ready";}),"initial owned automatic confirmation completes");
 }
 void manual(){auto ticket=use.begin(session);auto result=use.release(ticket.token);check(result.ok()||(result.editorProtected&&!result.error),"manual completion retains the existing backend");}
 void deliver(steamcss::UiMode value){mode=value;guard.observeMode(session,sample());}
};
int wmain(int argc,wchar_t**argv){try{
 if(argc==2&&std::wstring(argv[1])==L"--inert"){Sleep(INFINITE);return 0;}if(argc!=2)throw std::runtime_error("evidence root required");const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
 {
  Scene s(out,L"release");auto a=s.use.begin(s.session),b=s.use.begin(s.session);check(s.use.release(a.token).stale&&s.manualEnds==0,"superseded token cannot complete the current click");check(s.use.release(b.token).ok()&&s.manualEnds==1,"only the accepted final token emits one real production handoff");s.use.release(b.token);check(s.manualEnds==1,"duplicate release does not repeat the handoff");
  auto c=s.use.begin(s.session);s.use.observeSteam({});s.use.release(c.token);check(s.manualEnds==1,"old Steam identity cannot submit a completion");s.use.observeSteam(s.session);auto d=s.use.begin(s.session);s.editor=true;s.use.release(d.token);check(s.manualEnds==1&&live(s.css.p),"editor takeover suppresses verification and retains CSS");s.editor=false;auto e=s.use.begin(s.session);s.use.shutdown();check(s.manualEnds==1&&live(s.css.p),"shutdown cannot emit a late completion or terminate CSS");
 }
 {
  Scene s(out,L"handoff");s.startGuard();const auto initial=s.initialUses.load(),checksBefore=s.ensures.load();s.mode=steamcss::UiMode::Unknown;s.manual();check(until([&]{return s.readonly==1&&s.guard.snapshot().phase=="verifying";}),"unknown cached mode does not block read-only confirmation of an existing backend");s.deliver(steamcss::UiMode::BigPicture);check(until([&]{return s.guard.snapshot().phase=="ready"&&s.rebinds>=2;}),"exact manual/native/PowerShell handoff rebinds the real process exit HANDLE");check(s.initialUses==initial&&s.ensures==checksBefore,"duplicate mode during handoff never invokes Ensure or the automatic apply path");
  const auto reads=s.rebinds.load(),calls=s.helpers.load();const auto before=cpu();Sleep(200);const auto delta=cpu()-before;check(s.rebinds==reads&&s.helpers==calls&&delta<700000,"healthy rebound monitor is an event wait with no repeated probes or helpers");
  check(!s.guard.requestReadOnlyRebind(s.session,0)&&!s.guard.requestReadOnlyRebind(s.session,1),"zero and old native generations cannot reopen verification");
  s.permitConfirmation=false;s.manual();check(until([&]{return s.readonly==2;}),"a newer busy use is observed once");check(s.ensures==checksBefore,"refused confirmation cannot manufacture an apply attempt");
  s.permitConfirmation=true;write(s.dir/L"fail","");s.manual();check(until([&]{return s.readonly==3&&s.guard.snapshot().reason=="appearance-unconfirmed";}),"failed read-only DOM confirmation cannot earn Ready");check(s.initialUses==initial&&s.ensures==checksBefore,"failed read-only verification never falls through to Ensure");std::filesystem::remove(s.dir/L"fail");
  s.deliver(steamcss::UiMode::Desktop);Sleep(30);++s.epoch;s.deliver(steamcss::UiMode::BigPicture);check(until([&]{return s.initialUses==initial+1&&s.guard.snapshot().phase=="ready";}),"a later genuine mode entry remains observable after a failed handoff");
  s.editor=true;s.manual();check(s.readonly==3,"editor-owned CSS never launches a verification helper");s.editor=false;
  auto wrong=s.session;++wrong.created;check(!s.guard.requestReadOnlyRebind(wrong,100000),"foreign Steam identity cannot enqueue a read-only handoff");s.guard.shutdown();check(live(s.css.p),"guard shutdown retains the resident process");
 }
 {
  Scene s(out,L"deferred-shutdown",5000,500);const auto ticket=s.use.begin(s.session);const auto release=s.use.release(ticket.token);check(release.deferred&&s.manualEnds==0,"deferred manual completion does not emit early");s.use.shutdown();check(s.manualEnds==0&&live(s.css.p),"shutdown suppresses even an accepted pending release completion");
 }
 {
  Scene s(out,L"off");s.startGuard();write(s.dir/L"slow","");s.manual();check(until([&]{return s.readySignals>=2&&s.guard.snapshot().phase=="verifying";}),"owned verification is in flight before OFF");std::filesystem::remove(s.link);check(until([&]{return s.guard.snapshot().phase=="disabled";}),"link OFF is delivered by its directory event");check(s.lastCompletion.value&&WaitForSingleObject(s.lastCompletion.value,600)==WAIT_OBJECT_0&&!s.use.residentConfirmed(s.session),"OFF cancels the owned helper before its late success can publish");check(live(s.css.p),"OFF preserves the independent resident backend");
 }
 {
  Scene s(out,L"late-editor");write(s.dir/L"slow","");auto event=s.use.confirmExisting(s.session);check(event&&until([&]{return s.readySignals==1;},1800),"owned helper reaches its final read before editor takeover");s.editor=true;check(WaitForSingleObject(event,3500)==WAIT_OBJECT_0&&!s.use.residentConfirmed(s.session),"late editor takeover vetoes a successful helper receipt");CloseHandle(event);check(live(s.css.p),"late editor takeover never stops CSS");
 }
 {
  Scene s(out,L"bounded",1800);write(s.dir/L"slow","");auto event=s.use.confirmExisting(s.session);check(event!=nullptr,"read-only helper has a bounded completion event");const auto began=GetTickCount64();check(until([&]{return s.readySignals==1;},1700),"slow owned helper reaches Ready before its original deadline");check(WaitForSingleObject(event,2100)==WAIT_OBJECT_0,"read-only Ready cannot extend the original hard deadline");check(GetTickCount64()-began<2200&&!s.use.residentConfirmed(s.session),"expired verification cannot publish an eventual late success");CloseHandle(event);check(live(s.css.p),"verification deadline terminates only the owned helper");
 }
 {
  Scene s(out,L"initialization-order");write(s.dir/L"permit-initialize","");auto event=s.use.confirmAndIdle(s.session,true);check(event!=nullptr,"missing-backend request has one bounded completion");
  check(until([&]{return std::filesystem::exists(s.dir/L"token-order.txt");}),"owned initialization is running");check(s.helpers==0,"automatic resource and DOM work waits for initialization completion");
  check(WaitForSingleObject(event,4000)==WAIT_OBJECT_0&&s.use.residentConfirmed(s.session),"successful initialization continues into exactly one verified automatic helper");CloseHandle(event);
  std::ifstream file(s.dir/L"token-order.txt");std::string text((std::istreambuf_iterator<char>(file)),{});deckylaunch::Handle token;TOKEN_ELEVATION own{};DWORD size=0;check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value)&&GetTokenInformation(token.value,TokenElevation,&own,sizeof(own),&size),"test host token is readable");
  check(text.find(std::string("Ensure:")+(own.TokenIsElevated?"1":"0"))!=std::string::npos&&text.find("EnsureComplete")<text.find("Automatic:")&&text.find(std::string("Automatic:")+(own.TokenIsElevated?"1":"0"))!=std::string::npos,"Ensure and later trusted automatic work retain the verified host token");check(s.helpers==1&&live(s.css.p),"initialization sequence retains the sole owned backend");
 }
 {
  Scene s(out,L"initialization-failure");write(s.dir/L"permit-initialize","");write(s.dir/L"fail-initialize","");auto event=s.use.confirmAndIdle(s.session,true);check(event&&WaitForSingleObject(event,4000)==WAIT_OBJECT_0,"failed initialization closes its finite request");CloseHandle(event);check(s.helpers==0&&!s.use.residentConfirmed(s.session)&&live(s.css.p),"failed initialization never falls through to confirmation or stops CSS");
 }
 {
  Scene s(out,L"initialization-manual-takeover");write(s.dir/L"permit-initialize","");auto event=s.use.confirmAndIdle(s.session,true);check(event&&until([&]{return std::filesystem::exists(s.dir/L"token-order.txt");}),"initialization is in flight before a manual takeover");auto ticket=s.use.begin(s.session);check(WaitForSingleObject(event,1000)==WAIT_OBJECT_0,"manual takeover retires the previous automatic completion");CloseHandle(event);s.use.release(ticket.token);Sleep(800);check(s.helpers==0&&live(s.css.p),"cancelled initialization cannot later launch an automatic helper");
 }
 check(ownedCreated==ownedClosed,"all owned inert Steam/CSS processes exited; no fixture process remains");std::ofstream(out/L"result.json")<<json{{"ok",true},{"passed",checks},{"ownedCreated",ownedCreated},{"ownedClosed",ownedClosed},{"realSteamOperations",0},{"realCssOperations",0},{"scope","Production use/guard and exact main callbacks with private inert Steam/CSS and handshake helper"}}.dump(2);return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
