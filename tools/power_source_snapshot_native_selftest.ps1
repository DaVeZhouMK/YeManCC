param([string]$OutputDirectory='G:\YeManCC-Work\Mainline\Build\Validation\foreground-acdc-20261008\native-selftest')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$main=[IO.File]::ReadAllText((Join-Path $root 'native\main.cpp'))
$match=[regex]::Match($main,'ipc_on\("power.sourceSnapshot", \[\]\(const json&\) -> json \{([\s\S]*?)\n    \}\);')
if(-not $match.Success){throw 'Production power.sourceSnapshot handler not found'}
$accept=[regex]::Match($main,'static bool acceptAcDcSource\(HWND h, int ac, const char\* source\) \{[\s\S]*?\n\}')
$heartbeat=[regex]::Match($main,'static void reconcileAcDcSourceHeartbeat\(HWND h\) \{[\s\S]*?\n\}')
if(-not $accept.Success -or -not $heartbeat.Success){throw 'Shared source event/heartbeat handler missing'}
$fixture=@'
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <atomic>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
struct SYSTEM_POWER_STATUS { unsigned char ACLineStatus=255; };
static constexpr int FALSE=0;
static int reads=0;
static bool available=true;
static unsigned char acLine=1;
static int GetSystemPowerStatus(SYSTEM_POWER_STATUS* status) { ++reads; status->ACLineStatus=acLine; return available?1:0; }
using HWND=int;using UINT=unsigned;using UINT_PTR=unsigned long long;using DWORD=unsigned;
static int g_lastAcState=-1,notes=0,exits=0,timers=0;static DWORD ticks=10000;
static std::atomic<unsigned long long> g_acdcDebounceGeneration{1};
static std::vector<DWORD> g_acSwitchTicks;
static constexpr UINT_PTR TIMER_ID_ACDC=0xA100;static constexpr DWORD ACDC_DEBOUNCE_MS=5000,ACDC_BURST_MS=5000;static constexpr int ACDC_BURST_LIMIT=10,WM_APP_EXIT=0x406;
enum class PowerLifecycle{Ready,Suspending,Suspended,Resuming};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static json events=json::array();
using Timer=void(*)(HWND,UINT,UINT_PTR,DWORD);static Timer settle=nullptr;
static unsigned long long currentPowerGeneration(){return 7;}
static void sgRecordFact(const char*,const json&){}
static void sgNoteExternalDeviceAcDcChange(){++notes;}
static void ipc_emit(const char* event,const json& data){events.push_back({{"event",event},{"data",data}});}
static void appendNativeLifecycleLog(const char*,const json&){}
static bool KillTimer(HWND,UINT_PTR){return true;}
static UINT_PTR SetTimer(HWND,UINT_PTR id,UINT,Timer callback){++timers;settle=callback;return id;}
static bool PostMessageW(HWND,UINT_PTR,UINT_PTR,UINT_PTR){++exits;return true;}
static DWORD GetTickCount(){return ticks;}
//__SHARED_SOURCE__
static json readSnapshot() {
//__PRODUCTION_HANDLER__
}
static void expect(bool condition,const char* label) { if(!condition)throw std::runtime_error(label);std::cout<<"PASS "<<label<<"\n"; }
int main() { try {
 auto read=[&] {const auto before=reads;auto result=readSnapshot();expect(reads==before+1,"every foreground snapshot performs exactly one system-power query");return result;};
 available=true;acLine=1;auto ac=read();expect(ac["known"]==true&&ac["acLine"]==1,"AC supply is reliable AC");
 acLine=0;auto dc=read();expect(dc["known"]==true&&dc["acLine"]==0,"sleep AC then wake DC reads actual DC supply rather than a remembered AC event");
 acLine=255;auto unknown=read();expect(unknown["known"]==false&&unknown["acLine"]==255,"Windows unknown is not silently converted to reliable AC");
 acLine=2;auto invalid=read();expect(invalid["known"]==false&&invalid["acLine"]==255,"unexpected system source stays unknown");
 available=false;acLine=0;auto failed=read();expect(failed["known"]==false&&failed["acLine"]==255,"failed system-power query is unknown, never a fabricated source");
 const auto beforeEventReads=reads;events.clear();g_lastAcState=-1;available=true;acLine=1;
 expect(!acceptAcDcSource(1,1,"fixture-windows")&&events.empty(),"initial native subscription seeds one owner without fabricating a change");
 expect(!acceptAcDcSource(1,1,"fixture-windows")&&events.empty(),"same-side native events do no heavy work");
 expect(!acceptAcDcSource(1,0,"fixture-windows")&&events.size()==1&&events[0]["event"]=="power.sourceChanged"&&events[0]["data"]["ac"]==false&&reads==beforeEventReads,"native unplug emits immediately without probing");
 expect(settle!=nullptr&&timers==1,"existing tail debounce remains one owned timer");settle(1,0,TIMER_ID_ACDC,0);expect(events.back()["event"]=="power.acChanged"&&events.back()["data"]["ac"]==false,"existing debounce confirms latest source");
 events.clear();acLine=0;const auto sameNotes=notes,beforeHeartbeat=reads;reconcileAcDcSourceHeartbeat(1);
 expect(reads==beforeHeartbeat+1&&notes==sameNotes&&events.size()==1&&events[0]["event"]=="power.sourceHeartbeat","shared heartbeat republishes checked state without replaying unchanged source logic");
 events.clear();acLine=1;reconcileAcDcSourceHeartbeat(1);expect(events.size()==2&&events[0]["event"]=="power.sourceChanged"&&events[1]["event"]=="power.sourceHeartbeat"&&notes==sameNotes+1,"heartbeat repairs missed Windows source event through the original source owner");
 events.clear();acLine=255;reconcileAcDcSourceHeartbeat(1);expect(events.empty()&&g_lastAcState==1,"unknown heartbeat neither fabricates side nor revokes known owner");available=false;acLine=0;reconcileAcDcSourceHeartbeat(1);expect(events.empty()&&g_lastAcState==1,"failed heartbeat retains known source without event");
 g_powerLifecycle=PowerLifecycle::Suspended;available=true;const auto suspendedReads=reads;reconcileAcDcSourceHeartbeat(1);expect(reads==suspendedReads&&events.empty(),"suspended heartbeat does no source IO");g_powerLifecycle=PowerLifecycle::Ready;
 g_acSwitchTicks.clear();g_lastAcState=0;for(int n=0;n<11;n++){++ticks;acceptAcDcSource(1,(n+1)%2,"fixture-storm");}expect(exits==1,"original fast-source-storm exit protection remains intact");

 std::cout<<"FOREGROUND_POWER_SOURCE_NATIVE_OK (production handler, mock OS only)\n";return 0;
 }catch(const std::exception& error){std::cerr<<"FAILED "<<error.what()<<"\n";return 1;}}
'@
$fixture=$fixture.Replace('//__PRODUCTION_HANDLER__',$match.Groups[1].Value).Replace('//__SHARED_SOURCE__',$accept.Value+[Environment]::NewLine+$heartbeat.Value)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$source=Join-Path $OutputDirectory 'power-source-selftest.cpp'
[IO.File]::WriteAllText($source,$fixture,[Text.UTF8Encoding]::new($false))
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /I"$root\deps\json" "$source" /Fo"$OutputDirectory\power-source-selftest.obj" /Fe"$OutputDirectory\power-source-selftest.exe"
if errorlevel 1 exit /b 1
"$OutputDirectory\power-source-selftest.exe"
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat
if($LASTEXITCODE -ne 0){throw "Foreground power-source selftest failed: $LASTEXITCODE"}
