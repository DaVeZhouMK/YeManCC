// Resident singleton regression. Only copied inert fixture processes are created.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_cssloader_use.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <json.hpp>
using namespace ymcc;using json=nlohmann::json;
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
function Invoke-LoaderRpc($Method,$Arguments){if($Method -eq 'get_themes'){return @()};throw 'ISOLATED_CSS_RPC_DENIED'}
function Get-NetTCPConnection {param($State,$LocalPort) @()}
function Get-CimInstance {
 param($ClassName,$Filter,$OperationTimeoutSec)
 $children=@(Get-BackendProcesses);$child=0
 if(Test-Path -LiteralPath (Join-Path (Split-Path -Parent $script:BackendExe) 'child.pid')){$child=[int]([IO.File]::ReadAllText((Join-Path (Split-Path -Parent $script:BackendExe) 'child.pid')))}
 $parent=@($children|Where-Object {$_.Id -ne $child}|Select-Object -First 1)
 foreach($p in $children){[pscustomobject]@{ProcessId=$p.Id;ParentProcessId=$(if($p.Id -eq $child -and $parent.Count){$parent[0].Id}else{0})}}
}
function Invoke-OwnedPresetResourceUpdate($Current){$Current}
function Test-CurrentUserProcess($p){$p.Path -eq $script:BackendExe}
function Get-BackendProcesses {@([Diagnostics.Process]::GetProcessesByName('CssLoader-Standalone-Headless')|Where-Object {$_.Path -eq $script:BackendExe})}
function Get-Process {param($Name,$Id) Get-BackendProcesses}
function Test-BackendOwner {@(Get-BackendProcesses).Count -gt 0}
function Get-AppearanceStatus {
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
function Get-AppearanceSteamSession {[pscustomobject]@{pid=__PID__;birth='__BIRTH__';root='__STEAM__'}}
function Get-AppearanceRenderedStatus($Session,$Status){
 $confirmed=Test-AppearanceRendered $Session $Status
 [pscustomobject]@{checked=$true;confirmed=[bool]$confirmed;reason='isolated-dom-receipt';mismatched=@()}
}
function Test-AppearanceRendered($Session,$Status){
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
 if(argc!=3)throw std::runtime_error("evidence root and repository required");
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
 for(const auto kind:{L"normal",L"shutdown",L"steam-change",L"timeout",L"editor-takeover",L"dom-failure",L"initial-dom-failure",L"resume-invalidation"}){
   auto dir=fixture(kind);if(std::wstring(kind)==L"initial-dom-failure")write(dir/L"fail-dom","1");std::atomic<unsigned>ready=0,completed=0,known=0;std::atomic<DWORD>reason=0,lastPhase=0,lastFlags=0;std::atomic<bool>editor=false;
   steamcssuse::Runtime runtime(false,35821,[&]{return editor?steamcsseditor::State::Active:steamcsseditor::State::Absent;});
   runtime.initialize((dir/L"steam-appearance.ps1").wstring(),(dir/L"link.txt").wstring(),std::wstring(kind)==L"timeout"?5000:std::wstring(kind)==L"initial-dom-failure"?40000:15000,100,[&](const auto&stage,const auto&,const auto&,const auto&r){std::ofstream(dir/L"native-events.jsonl",std::ios::app)<<json({{"stage",stage},{"error",r.error},{"phase",r.helperPhase},{"reason",r.helperFailureCode},{"exit",r.helperExitCode}}).dump()<<"\n";if(stage=="automatic-helper-ready")++ready;if(stage=="idle-helper-complete")++completed;if(r.helperResultKnown){++known;reason=r.helperFailureCode;lastPhase=r.helperPhase;lastFlags=r.helperFlags;}},700);
   deckylaunch::Handle lease;lease.value=runtime.confirmAndIdle(session,true);
   if(std::wstring(kind)==L"initial-dom-failure"){
     check(lease.value!=nullptr,"initial DOM failure starts one bounded automatic use");Child parent,child;pin(dir,parent,child);
     check(WaitForSingleObject(lease.value,45000)==WAIT_OBJECT_0&&ready==0,"initial DOM failure exhausts its finite window without a false ready signal");
     check(known>=1&&reason==6&&lastPhase==5&&(lastFlags&3)==3&&!(lastFlags&4),"actual native result distinguishes missing DOM from healthy backend/config and initialization");
     runtime.shutdown();check(live(parent.value)&&live(child.value),"initial injection failure retains the same resident backend without replacement");
     scenarios.push_back({{"case",u8(kind)},{"ready",ready.load()},{"known",known.load()},{"reason",reason.load()},{"phase",lastPhase.load()},{"flags",lastFlags.load()},{"residentParentSurvives",true},{"residentChildSurvives",true}});continue;
   }
   check(lease.value&&until([&]{return ready==1;}),"one automatic helper initialized CSS and signaled ready");Child parent,child,helper;pin(dir,parent,child);
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
