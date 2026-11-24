// Resident singleton regression. Only copied inert fixture processes are created.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
 #include <string>
#include <filesystem>
static std::atomic<unsigned> actualHelpers{0};
static BOOL WINAPI fixtureCreate(LPCWSTR app,LPWSTR command,LPSECURITY_ATTRIBUTES pa,LPSECURITY_ATTRIBUTES ta,
 BOOL inherit,DWORD flags,LPVOID env,LPCWSTR dir,LPSTARTUPINFOW si,LPPROCESS_INFORMATION pi){
 const auto result=::CreateProcessW(app,command,pa,ta,inherit,flags,env,dir,si,pi);
 if(result&&app&&std::filesystem::path(app).filename()==L"powershell.exe"&&command&&
    std::wstring(command).find(L" -Action ")!=std::wstring::npos)++actualHelpers;
 return result;
}
#define CreateProcessW fixtureCreate
#include "fixtures/steam_css_inventory_source_fixture.inc"
#include "steam_cssloader_use.h"
#include "sleep_power_state.h"
#undef CreateProcessW
#include <atomic>
#include <fstream>
#include <iostream>
#include <json.hpp>
using namespace ymcc;using json=nlohmann::json;
static std::mutex fixturePowerMutex;
static sleeppower::SharedPowerV1 fixturePower;
static bool fixturePowerKnown=true;
static unsigned fixturePowerReads=0;
static int __cdecl readFixtureSystemPower(sleeppower::SharedPowerV1* value,std::uint32_t bytes){
 std::lock_guard lock(fixturePowerMutex);++fixturePowerReads;
 if(!fixturePowerKnown||!value||bytes!=sizeof(*value))return 0;*value=fixturePower;return 1;
}
static void powerChange(const std::wstring& selected){
 std::lock_guard lock(fixturePowerMutex);
 if(selected==L"cold-public-serial"||selected==L"cold-public-cache")++fixturePower.suspendSerial;
 if(selected==L"cold-public-notification")++fixturePower.observedNotificationSequence;
 if(selected==L"cold-public-unknown")fixturePowerKnown=false;
 ++fixturePower.publicationRevision;
}
static std::vector<std::wstring> ownedBackendImages;
static std::filesystem::path ownedOutput;
struct OwnedFixtureCleanup {
 ~OwnedFixtureCleanup(){unsigned terminated=0,remaining=0;
  for(unsigned round=0;round<3;++round){remaining=0;deckylaunch::Handle snapshot;snapshot.value=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W row{sizeof(row)};
   if(Process32FirstW(snapshot.value,&row))do{if(_wcsicmp(row.szExeFile,L"CssLoader-Standalone-Headless.exe"))continue;deckylaunch::Handle p;p.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE|PROCESS_TERMINATE,FALSE,row.th32ProcessID);wchar_t image[32768]{};DWORD size=32768;
    if(!p.value||!steamcss::ownProcess(p.value)||!QueryFullProcessImageNameW(p.value,0,image,&size)||std::none_of(ownedBackendImages.begin(),ownedBackendImages.end(),[&](const auto& known){return !_wcsicmp(known.c_str(),image);}))continue;
    if(WaitForSingleObject(p.value,0)==WAIT_TIMEOUT){++remaining;if(round<2&&TerminateProcess(p.value,0)){++terminated;WaitForSingleObject(p.value,1000);}}
   }while(Process32NextW(snapshot.value,&row));
   if(!remaining)break;if(round<2)Sleep(20);
  }
  if(!ownedOutput.empty())std::ofstream(ownedOutput/L"fixture-cleanup.json")<<"{\"remaining\":"<<remaining<<",\"terminatedOwnedOrphans\":"<<terminated<<",\"realCssOperations\":0}";
 }
} ownedFixtureCleanup;
static unsigned checks=0;
static void check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);++checks;std::cout<<"PASS "<<text<<std::endl;}
template<class Runtime>
static std::optional<ULONGLONG> applyRevision(Runtime& runtime,const steamsession::Session& session){
 if constexpr(requires{runtime.readPolicy(session).automaticAppliedRevision;})return runtime.readPolicy(session).automaticAppliedRevision;
 return std::nullopt; // The launch regression can also compile a pre-receipt header.
}
static std::wstring self(){wchar_t f[32768]{};GetModuleFileNameW(nullptr,f,32768);return f;}
static std::string text(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void write(const std::filesystem::path&p,const std::string&s){std::ofstream f(p,std::ios::binary);f<<s;}
static std::string u8(const std::filesystem::path&p){auto w=p.wstring();auto n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);std::string v(n,'\0');WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),v.data(),n,nullptr,nullptr);return v;}
static std::string replace(std::string s,const std::string&a,const std::string&b){size_t at=0;while((at=s.find(a,at))!=s.npos){s.replace(at,a.size(),b);at+=b.size();}return s;}
static HANDLE launch(const std::wstring&exe,const std::wstring&args){auto cmd=L"\""+exe+L"\" "+args;STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))throw std::runtime_error("fixture create");CloseHandle(pi.hThread);return pi.hProcess;}
struct Child{HANDLE value=nullptr;~Child(){if(value){TerminateProcess(value,0);WaitForSingleObject(value,2000);CloseHandle(value);}}};
static bool live(HANDLE h){return h&&WaitForSingleObject(h,0)==WAIT_TIMEOUT;}
static unsigned countImage(const std::filesystem::path& wanted){unsigned n=0;deckylaunch::Handle snapshot;snapshot.value=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W row{sizeof(row)};if(Process32FirstW(snapshot.value,&row))do{deckylaunch::Handle p;p.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,row.th32ProcessID);wchar_t image[32768]{};DWORD size=32768;if(p.value&&QueryFullProcessImageNameW(p.value,0,image,&size)&&!_wcsicmp(image,wanted.c_str())&&live(p.value))++n;}while(Process32NextW(snapshot.value,&row));return n;}
static ULONGLONG birth(HANDLE h){FILETIME b{},e{},k{},u{};if(!GetProcessTimes(h,&b,&e,&k,&u))throw std::runtime_error("birth");return(ULONGLONG(b.dwHighDateTime)<<32)|b.dwLowDateTime;}
static ULONGLONG cpu(){FILETIME b{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&b,&e,&k,&u);return(ULONGLONG(k.dwHighDateTime)<<32)+k.dwLowDateTime+(ULONGLONG(u.dwHighDateTime)<<32)+u.dwLowDateTime;}
static bool until(const std::function<bool()>&fn,DWORD ms=15000){auto end=GetTickCount64()+ms;while(!fn()){if(GetTickCount64()>=end)return false;Sleep(10);}return true;}
static const char* preset=R"PS(
function Initialize-PresetTargetHint {param($KeepBackendPath)}
function Read-PresetState {$null}
function Get-PresetStateDir {Join-Path (Split-Path -Parent $script:BackendExe) 'fixture-state'}
function Assert-PresetStateTarget([string]$Path){
 if($Path -cne (Join-Path (Get-PresetStateDir) 'transaction.json')){throw 'ISOLATED_TRANSACTION_PATH_DENIED'}
 $Path
}
function Invoke-LoaderRpc($Method,$Arguments){if($Method -eq 'get_themes'){return @()};throw 'ISOLATED_CSS_RPC_DENIED'}
function Get-AppearanceTcpListeners {param($LocalPort) @()}
function Get-AppearanceBackendCandidates {Get-BackendProcesses}
function Get-CimInstance {
 param($ClassName,$Filter,$OperationTimeoutSec)
 $children=@(Get-BackendProcesses);$child=0
 if(Test-Path -LiteralPath (Join-Path (Split-Path -Parent $script:BackendExe) 'child.pid')){$child=[int]([IO.File]::ReadAllText((Join-Path (Split-Path -Parent $script:BackendExe) 'child.pid')))}
 $parent=@($children|Where-Object {$_.Id -ne $child}|Select-Object -First 1)
 foreach($p in $children){[pscustomobject]@{ProcessId=$p.Id;ParentProcessId=$(if($p.Id -eq $child -and $parent.Count){$parent[0].Id}else{0})}}
}
function Invoke-OwnedPresetResourceUpdate($Current){
 $fixtureRoot=Split-Path -Parent $script:BackendExe
 if(Test-Path -LiteralPath (Join-Path $fixtureRoot 'gate-manual')){
  [IO.File]::WriteAllText((Join-Path $fixtureRoot 'manual-waiting'),'1')
  $stopwatch=[Diagnostics.Stopwatch]::StartNew()
  while(!(Test-Path -LiteralPath (Join-Path $fixtureRoot 'allow-manual'))){
   if($stopwatch.ElapsedMilliseconds -ge 8000){throw 'ISOLATED_MANUAL_GATE_TIMEOUT'}
   Start-Sleep -Milliseconds 25
  }
 }
 if(Test-Path -LiteralPath (Join-Path (Split-Path -Parent $script:BackendExe) 'fail-initialization')){throw 'ISOLATED_INITIALIZATION_FAILURE'}
 $Current
}
function Test-CurrentUserProcess($p){$p.Path -eq $script:BackendExe}
function Get-BackendProcesses {@([Diagnostics.Process]::GetProcessesByName('CssLoader-Standalone-Headless')|Where-Object {$_.Path -eq $script:BackendExe})}
function Get-Process {param($Name,$Id) Get-BackendProcesses}
function Test-BackendOwner {@(Get-BackendProcesses).Count -gt 0}
function Get-AppearanceStatus($FinalIdleIdentity) {
 if($FinalIdleIdentity){
  $session=Get-AppearanceSteamSession
  if($session.pid -ne $FinalIdleIdentity.pid -or $session.birth -cne $FinalIdleIdentity.birth){throw 'ISOLATED_FINAL_STEAM_IDENTITY_MISMATCH'}
  $FinalIdleIdentity.session=$session
  $script:AppearanceHelperPhase=4
 }
 $count=@(Get-BackendProcesses).Count
 [pscustomobject]@{backendReady=($count -gt 0);configuredMatchesBackend=$true;packOwned=$true;packPrepared=$true;backendReloadRequired=$false;packError='';foreignActiveCount=0;revision=('a'*64);targetId=('b'*16);backendProcessCount=$count;themes=@([pscustomobject]@{installed=$true;configured=$true})}
}
function Get-ThemeRows {@()}
function Get-ThemeRevision($Rows){'a'*64}
function Get-ThemeTargetId {'b'*16}
)PS";
static const char* boundary=R"PS(
[IO.File]::WriteAllText('__DIR__\helper.pid',[string]$PID)
function Test-CssDesktopEditing {$false}
function Get-AppearanceBackendCandidates {Get-BackendProcesses}
function Get-AppearanceSteamSession {[pscustomobject]@{pid=__PID__;birth='__BIRTH__';root='__STEAM__'}}
function Get-AppearanceRenderedStatus($Session,$Status){
 $confirmed=Test-AppearanceRendered $Session $Status
 [pscustomobject]@{checked=$true;confirmed=[bool]$confirmed;reason='isolated-dom-receipt';mismatched=@()}
}
function Test-AppearanceRendered($Session,$Status){
 [IO.File]::WriteAllText('__DIR__\dom-started','1')
 $script:DomCalls++
 if($script:DomCalls -gt 1 -and (Test-Path -LiteralPath '__DIR__\hang-dom')){Start-Sleep -Seconds 30}
 !(Test-Path -LiteralPath '__DIR__\fail-dom')
}
)PS";
int wmain(int argc,wchar_t**argv){std::cout<<std::unitbuf;try{
 if(argc==1&&std::filesystem::path(self()).filename()==L"CssLoader-Standalone-Headless.exe"){
   auto dir=std::filesystem::path(self()).parent_path();auto h=launch(self(),L"--dummy");write(dir/L"child.pid",std::to_string(GetProcessId(h)));CloseHandle(h);Sleep(INFINITE);return 0;
 }
 if(argc==2&&std::wstring(argv[1])==L"--dummy"){Sleep(INFINITE);return 0;}
 if(argc!=3&&argc!=4)throw std::runtime_error("evidence root, repository and optional exact case required");
 const std::wstring selected=argc==4?argv[3]:L"all";
 const bool manualEvent=selected==L"manual-ensure-event";
 const bool applyRevisionCase=selected==L"cold-apply-revision";
 if(!manualEvent&&!applyRevisionCase&&selected!=L"all"&&selected!=L"cold-single"&&selected!=L"cold-cost"&&selected!=L"cold-editor"&&selected!=L"cold-power"&&selected!=L"cold-request"&&selected!=L"cold-identity"&&selected!=L"cold-initial-failure"&&selected!=L"cold-bad-init"&&selected!=L"cold-missing-protocol"&&selected!=L"cold-public-serial"&&selected!=L"cold-public-notification"&&selected!=L"cold-public-unknown"&&selected!=L"cold-public-revision"&&selected!=L"cold-public-cache")throw std::runtime_error("unknown exact case");
 const auto out=std::filesystem::absolute(argv[1]),repo=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(out);ownedOutput=out;
 const auto steamExe=out/L"steam.exe";std::filesystem::copy_file(self(),steamExe);Child steam;steam.value=launch(steamExe.wstring(),L"--dummy");
 const steamsession::Session session{out.wstring(),GetProcessId(steam.value),0,birth(steam.value)};
 auto fixture=[&](const std::wstring&name){auto dir=out/name;std::filesystem::create_directories(dir/L"steam-appearance");std::filesystem::copy_file(self(),dir/L"CssLoader-Standalone-Headless.exe");ownedBackendImages.push_back((dir/L"CssLoader-Standalone-Headless.exe").wstring());
 auto entry=text(repo/L"PowerControl/steam-appearance.ps1");
 // Exact production entry, with only its lock domain and external IO isolated.
 entry=replace(entry,"Local\\YMCC.CssLoader.","Local\\YMCC.CssFixture."+std::to_string(GetCurrentProcessId())+".");
 write(dir/L"steam-appearance.ps1",entry);write(dir/L"steam-appearance/preset.ps1",preset);
 auto idle=text(repo/L"PowerControl/steam-appearance/idle.ps1");auto limits=replace(boundary,"__PID__",std::to_string(session.pid));limits=replace(limits,"__BIRTH__",std::to_string(session.created));limits=replace(limits,"__STEAM__",u8(out));limits=replace(limits,"__DIR__",u8(dir));write(dir/L"steam-appearance/idle.ps1",idle+"\n"+limits);
 write(dir/L"steam-appearance/resident.ps1",text(repo/L"PowerControl/steam-appearance/resident.ps1"));write(dir/L"link.txt",u8(dir/L"CssLoader-Standalone-Headless.exe"));return dir;};
 auto pin=[&](const std::filesystem::path&dir,Child&parent,Child&child){check(until([&]{return std::filesystem::exists(dir/L"child.pid");}),"resident packaged child published");DWORD childId=std::stoul(text(dir/L"child.pid"));child.value=OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,childId);
   // Capture only this fixture's exact executable path, never a real CSS process.
   deckylaunch::Handle snap;snap.value=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W e{sizeof(e)};
   if(Process32FirstW(snap.value,&e))do{if(e.th32ProcessID==childId)parent.value=OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,e.th32ParentProcessID);}while(Process32NextW(snap.value,&e));
   check(live(parent.value)&&live(child.value),"one packaged backend family pinned");};
 json scenarios=json::array();
 const bool cold=selected!=L"all";
 for(const auto kind:{L"normal",L"shutdown",L"steam-change",L"timeout",L"editor-takeover",L"dom-failure",L"initial-dom-failure",L"resume-invalidation"}){
   if(cold&&std::wstring(kind)!=L"normal")continue;
   {std::lock_guard lock(fixturePowerMutex);fixturePower={};fixturePower.generation=1;fixturePower.suspendSerial=1;fixturePower.publicationRevision=1;fixturePower.flags=sleeppower::kKnown|sleeppower::kHardwareAllowed;fixturePowerKnown=true;fixturePowerReads=0;}
   bool publicPowerBound=false;
   actualHelpers=0;std::atomic<bool>admit=true,identityValid=true;std::atomic<ULONGLONG>power=1;
   auto identity=std::make_shared<steamsession::IdentitySnapshot>();identity->session=session;identity->presence=1;identity->processEpoch=1;identity->rootReadError=ERROR_SUCCESS;identity->windowsSessionKnown=true;
   auto dir=fixture(kind);if(std::wstring(kind)==L"initial-dom-failure")write(dir/L"fail-dom","1");std::atomic<unsigned>ready=0,completed=0,known=0;std::atomic<DWORD>reason=0,lastPhase=0,lastFlags=0;std::atomic<bool>editor=false;
   std::atomic<uint64_t> ensureCompletion=0,ensureCancellation=0;
   std::atomic<unsigned> ensureExits=0;
   if(selected==L"cold-initial-failure")write(dir/L"fail-initialization","1");
   if(selected==L"cold-bad-init")write(dir/L"steam-appearance.ps1",replace(text(dir/L"steam-appearance.ps1"),"Write-AppearanceHelperResult $true","Write-AppearanceHelperResult $false"));
   if(selected==L"cold-missing-protocol")write(dir/L"steam-appearance.ps1",replace(text(dir/L"steam-appearance.ps1"),"AutomaticInitializeGateProtocol=1","AutomaticInitializeGateProtocol=0"));
   steamcssuse::Runtime runtime(false,35821,[&]{return editor?steamcsseditor::State::Active:steamcsseditor::State::Absent;});
   const auto changed=[&](const auto&stage,const auto&,const auto&,const auto&r){std::ofstream(dir/L"native-events.jsonl",std::ios::app)<<json({{"stage",stage},{"error",r.error},{"phase",r.helperPhase},{"reason",r.helperFailureCode},{"exit",r.helperExitCode},{"useGeneration",r.useGeneration}}).dump()<<"\n";
   if(stage=="ensure-helper-complete"&&r.helperExitKnown){ensureCompletion=r.useGeneration;++ensureExits;}
   if(stage=="ensure-helper-canceled")ensureCancellation=r.useGeneration;
   if(stage=="automatic-initialization-ready"){
 if(selected==L"cold-editor")editor=true;
 if(selected==L"cold-power")++power;
 if(selected==L"cold-request")admit=false;
 if(selected==L"cold-identity")identityValid=false;
 if(selected==L"cold-public-serial"||selected==L"cold-public-notification"||selected==L"cold-public-unknown"||selected==L"cold-public-revision")powerChange(selected);
 }if(stage=="automatic-helper-ready")++ready;if(stage=="idle-helper-complete")++completed;if(r.helperResultKnown){++known;reason=r.helperFailureCode;lastPhase=r.helperPhase;lastFlags=r.helperFlags;}};
   // Adapt only the old optional-argument boundary. Runtime and public power
   // comparisons are the real product implementations in both arms.
   const auto initializeUse=[&]<class R>(R& target){
    if constexpr(requires{target.initialize((dir/L"steam-appearance.ps1").wstring(),(dir/L"link.txt").wstring(),std::wstring(kind)==L"timeout"?5000:std::wstring(kind)==L"initial-dom-failure"?40000:15000,100,changed,700,[&]{return admit.load();},[&]{return power.load();},[&]{return identity;},[&](const auto& expected){return identityValid&&expected==identity;},steamcssinventory::Bridge::SharedRead{},CssSourceFixture::defaultRead,&readFixtureSystemPower);}){
     publicPowerBound=true;return target.initialize((dir/L"steam-appearance.ps1").wstring(),(dir/L"link.txt").wstring(),std::wstring(kind)==L"timeout"?5000:std::wstring(kind)==L"initial-dom-failure"?40000:15000,100,changed,700,[&]{return admit.load();},[&]{return power.load();},[&]{return identity;},[&](const auto& expected){return identityValid&&expected==identity;},steamcssinventory::Bridge::SharedRead{},CssSourceFixture::defaultRead,&readFixtureSystemPower);
    }else{return target.initialize((dir/L"steam-appearance.ps1").wstring(),(dir/L"link.txt").wstring(),std::wstring(kind)==L"timeout"?5000:std::wstring(kind)==L"initial-dom-failure"?40000:15000,100,changed,700,[&]{return admit.load();},[&]{return power.load();},[&]{return identity;},[&](const auto& expected){return identityValid&&expected==identity;},steamcssinventory::Bridge::SharedRead{},CssSourceFixture::defaultRead);}
   };
   check(initializeUse(runtime),"owned use initializes with its supported public power port");
   if(manualEvent){
     write(dir/L"gate-manual","1");
     const auto first=runtime.begin(session);
     check(runtime.ensure(first.token)&&until([&]{return std::filesystem::exists(dir/L"manual-waiting");}),"manual Ensure reaches the real helper before any exit notice");
     check(ensureExits==0&&actualHelpers==1,"no completion before the helper exits");
     const auto second=runtime.begin(session);
     check(second.token!=first.token&&ensureCancellation==first.token,"manual takeover cancels only the displaced completion token");
     check(runtime.ensure(second.token)&&actualHelpers==1,"new manual request reuses the same in-flight helper");
     write(dir/L"allow-manual","1");
     check(until([&]{return ensureExits==1;})&&ensureCompletion==second.token,"native helper exit wakes only the current manual token");
     Child parent,child;pin(dir,parent,child);
     const auto boot=runtime.begin(session,[]{return true;});
     check(runtime.ensure(boot.token,[]{return true;})&&until([&]{return ensureExits==2;})&&ensureCompletion==0,"boot-context Ensure keeps its prior notification contract");
     check(ready==0&&completed==0&&!std::filesystem::exists(dir/L"dom-started"),"Ensure completion is never automatic DOM confirmation");
     runtime.shutdown();check(live(parent.value)&&live(child.value),"manual completion and shutdown preserve the resident family");
     scenarios.push_back({{"case","manual-ensure-event"},{"helpers",actualHelpers.load()},{"manualCompletions",1},{"displacedToken",first.token},{"completedToken",second.token}});
     continue;
   }
   const auto started=GetTickCount64();
   deckylaunch::Handle lease;lease.value=runtime.confirmAndIdle(session,true);
   if(selected==L"cold-missing-protocol"){
     check(!lease.value&&actualHelpers==0&&!std::filesystem::exists(dir/L"child.pid"),"mixed old script is rejected before any helper or backend creation");
     runtime.shutdown();scenarios.push_back({{"case",u8(selected)},{"helpers",actualHelpers.load()}});continue;
   }
   if(selected==L"cold-initial-failure"){
     check(lease.value&&WaitForSingleObject(lease.value,15000)==WAIT_OBJECT_0,"failed Ensure actually ends its bounded helper use");
     check(actualHelpers==1&&ready==0&&completed==0&&known>=1&&reason!=0&&!std::filesystem::exists(dir/L"child.pid")&&!std::filesystem::exists(dir/L"dom-started"),"initialization failure cannot signal ready, create another helper or enter DOM confirmation");
     if(const auto revision=applyRevision(runtime,session);revision.has_value())check(revision==0,"failed initialization cannot advance the automatic application stamp");
     runtime.shutdown();scenarios.push_back({{"case",u8(selected)},{"helpers",actualHelpers.load()},{"reason",reason.load()},{"ready",ready.load()}});continue;
   }
   if(selected==L"cold-editor"||selected==L"cold-power"||selected==L"cold-request"||selected==L"cold-identity"||selected==L"cold-bad-init"||selected==L"cold-public-serial"||selected==L"cold-public-notification"||selected==L"cold-public-unknown"){
     check(lease.value&&WaitForSingleObject(lease.value,15000)==WAIT_OBJECT_0,"native initialization gate cancels the actual helper on revoked admission");
     Child parent,child;pin(dir,parent,child);
     if(selected==L"cold-public-serial"||selected==L"cold-public-notification"||selected==L"cold-public-unknown"){
      write(dir/L"power-observation.json",json{{"publicPowerBound",publicPowerBound},{"reads",fixturePowerReads},{"serial",fixturePower.suspendSerial},{"notification",fixturePower.observedNotificationSequence},{"readerKnown",fixturePowerKnown},{"ready",ready.load()},{"complete",completed.load()},{"domStarted",std::filesystem::exists(dir/L"dom-started")},{"flags",lastFlags.load()},{"helpers",actualHelpers.load()}}.dump(2));
     }
     check(ready==0&&completed==0&&!std::filesystem::exists(dir/L"dom-started"),"post-Ensure editor or power/request revocation prevents DOM work and final ready");
     check(actualHelpers==1&&live(parent.value)&&live(child.value),"revoked initialization retains one CSS family and creates only one temporary helper");
     runtime.shutdown();scenarios.push_back({{"case",u8(selected)},{"helpers",actualHelpers.load()},{"domStarted",false},{"ready",ready.load()},{"publicPowerBound",publicPowerBound},{"publicPowerReads",fixturePowerReads}});continue;
   }
   if(std::wstring(kind)==L"initial-dom-failure"){
     check(lease.value!=nullptr,"initial DOM failure starts one bounded automatic use");Child parent,child;pin(dir,parent,child);
     check(WaitForSingleObject(lease.value,45000)==WAIT_OBJECT_0&&ready==0,"initial DOM failure exhausts its finite window without a false ready signal");
     check(known>=1&&reason==6&&lastPhase==5&&(lastFlags&3)==3&&!(lastFlags&4),"actual native result distinguishes missing DOM from healthy backend/config and initialization");
     runtime.shutdown();check(live(parent.value)&&live(child.value),"initial injection failure retains the same resident backend without replacement");
     scenarios.push_back({{"case",u8(kind)},{"ready",ready.load()},{"known",known.load()},{"reason",reason.load()},{"phase",lastPhase.load()},{"flags",lastFlags.load()},{"residentParentSurvives",true},{"residentChildSurvives",true}});continue;
   }
   check(lease.value&&until([&]{return ready==1;}),"one automatic helper initialized CSS and signaled ready");Child parent,child,helper;pin(dir,parent,child);
   if(selected==L"cold-single"||applyRevisionCase)check(actualHelpers==1,"cold initialization creates exactly one temporary helper");
   if(selected==L"cold-cost"||selected==L"cold-single"||selected==L"cold-public-revision"||selected==L"cold-public-cache"||applyRevisionCase){
     check(WaitForSingleObject(lease.value,15000)==WAIT_OBJECT_0&&completed==1&&known>=1&&reason==0,"complete cold confirmation retains the original result contract");
     const auto elapsed=GetTickCount64()-started;
     check(lastFlags==31&&runtime.residentConfirmed(session),"complete cold path confirms all five backend/config/DOM/singleton/resident flags");
     const auto applied=applyRevision(runtime,session);auto previous=session;++previous.created;
     if(applied.has_value()||applyRevisionCase){
      check(applied==1,"accepted automatic completion advances the stamp even after its helper exits");
      check(applyRevision(runtime,previous)==0,"the stamp cannot cross a Steam birth boundary");
     }
     if(applyRevisionCase){
      deckylaunch::Handle verify;verify.value=runtime.confirmExisting(session);
      check(verify.value&&WaitForSingleObject(verify.value,15000)==WAIT_OBJECT_0&&completed==2&&lastFlags==31&&runtime.residentConfirmed(session),"read-only helper fully confirms the same resident backend");
      check(applyRevision(runtime,session)==1,"successful VerifyOnly does not claim a new automatic apply");
      deckylaunch::Handle apply;apply.value=runtime.confirmAndIdle(session,false,true);
      check(apply.value&&WaitForSingleObject(apply.value,15000)==WAIT_OBJECT_0&&completed==3&&applyRevision(runtime,session)==2,"the next accepted automatic apply advances the same-session stamp again");
      check(countImage(dir/L"CssLoader-Standalone-Headless.exe")==2,"the three temporary uses retain exactly one resident packaged backend family");
     }
     if(selected==L"cold-public-cache"){
      const auto helperCount=actualHelpers.load();powerChange(selected);
      const bool retained=runtime.residentConfirmed(session);
      write(dir/L"power-observation.json",json{{"publicPowerBound",publicPowerBound},{"reads",fixturePowerReads},{"serial",fixturePower.suspendSerial},{"residentProofRetained",retained},{"helpers",actualHelpers.load()}}.dump(2));
      check(!retained&&actualHelpers==helperCount,"public same-generation suspend change invalidates the saved resident proof without creating a helper");
     }
     if(selected==L"cold-public-revision")check(publicPowerBound&&fixturePowerReads>0,"metadata-only public Ready publication preserves the same helper and final confirmation");
     runtime.shutdown();check(live(parent.value)&&live(child.value),"cold helper shutdown preserves its independently resident CSS family");
     scenarios.push_back({{"case",u8(selected)},{"confirmationWallMs",elapsed},{"helpers",actualHelpers.load()},{"flags",lastFlags.load()},{"ready",ready.load()},{"complete",completed.load()},{"publicPowerBound",publicPowerBound},{"publicPowerReads",fixturePowerReads}});continue;
   }
   helper.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE,FALSE,std::stoul(text(dir/L"helper.pid")));
   check(live(helper.value),"temporary helper has a pinned process identity");
   if(std::wstring(kind)==L"resume-invalidation"){
     check(runtime.invalidateForResume(session),"a verified resume invalidates the old request and its confirmation");
     check(WaitForSingleObject(lease.value,2000)==WAIT_OBJECT_0&&WaitForSingleObject(helper.value,2000)==WAIT_OBJECT_0,"pre-resume helper is actually canceled and its wait is completed");
     check(live(parent.value)&&live(child.value)&&!runtime.residentConfirmed(session),"old confirmation is revoked while CSS parent and child remain alive");
     deckylaunch::Handle resumed;resumed.value=runtime.confirmAndIdle(session,false,true);
     check(resumed.value&&WaitForSingleObject(resumed.value,15000)==WAIT_OBJECT_0&&runtime.residentConfirmed(session),"only a new post-resume helper can confirm the same resident backend");
     check(countImage(dir/L"CssLoader-Standalone-Headless.exe")==2,"resume cancellation and reconfirmation do not create a second packaged backend");
     runtime.shutdown();check(live(parent.value)&&live(child.value),"shutdown after resume retains the same CSS family");
     scenarios.push_back({{"case",u8(kind)},{"ready",ready.load()},{"known",known.load()},{"reason",reason.load()},{"residentParentSurvives",true},{"residentChildSurvives",true}});continue;
   }
   if(std::wstring(kind)==L"normal"){
     bool renewals=true;for(unsigned n=0;n<1000;++n){deckylaunch::Handle renewed;renewed.value=runtime.confirmAndIdle(session,false,true);renewals=renewals&&renewed.value!=nullptr;}check(renewals,"one thousand mode renewals reuse the active helper");
     check(WaitForSingleObject(lease.value,15000)==WAIT_OBJECT_0&&completed==1&&known>=1&&reason==0,"resident confirmation completes with the real result mapping");
     const auto token=runtime.begin(session);runtime.ensure(token.token);Sleep(1500);runtime.release(token.token);Sleep(200);
     check(live(parent.value)&&live(child.value)&&countImage(dir/L"CssLoader-Standalone-Headless.exe")==2,"another Ensure and release reuse exactly the same two-process backend family");
   }else if(std::wstring(kind)==L"shutdown")runtime.shutdown();
   else if(std::wstring(kind)==L"steam-change"){auto changed=session;++changed.created;runtime.observeSteam(changed);}
   else if(std::wstring(kind)==L"timeout"){write(dir/L"hang-dom","1");}
   else if(std::wstring(kind)==L"editor-takeover"){editor=true;}
   else write(dir/L"fail-dom","1");
   check(WaitForSingleObject(lease.value,15000)==WAIT_OBJECT_0,"bounded helper completes or is canceled");runtime.shutdown();
   check(WaitForSingleObject(helper.value,2000)==WAIT_OBJECT_0,"temporary helper is actually gone after request shutdown");
   check(live(parent.value)&&live(child.value),"CSS parent and child survive use completion and YMCC shutdown");
   if(std::wstring(kind)==L"dom-failure")check(reason==6,"failed DOM is reported separately while resident CSS survives");
   const auto idleBefore=cpu();Sleep(200);check(cpu()-idleBefore<1000000,"stopped helper runtime has no recurring discovery CPU loop");
   scenarios.push_back({{"case",u8(kind)},{"ready",ready.load()},{"known",known.load()},{"reason",reason.load()},{"residentParentSurvives",true},{"residentChildSurvives",true}});
 }
 write(out/L"single-helper.json",json({{"ok",true},{"checks",checks},{"scenarios",scenarios},{"realSteamOperations",0},{"realCssOperations",0},{"residentPolicy",true}}).dump(2));return 0;
 }catch(const std::exception&e){std::cerr<<"FAILED "<<e.what()<<std::endl;return 1;}}
