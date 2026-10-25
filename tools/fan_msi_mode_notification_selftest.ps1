# FAN-954: actual extracted native functions, fake owner/power/log sink. No device.
param([string]$ProjectRoot=(Split-Path -Parent $PSScriptRoot),[string]$EvidenceRoot)
$ErrorActionPreference='Stop'
if(-not $EvidenceRoot){$EvidenceRoot=Join-Path $env:TEMP ('fan-msi-mode-'+[guid]::NewGuid().ToString('N'))}
New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
$sourcePath=Join-Path $ProjectRoot 'native/main.cpp'
$source=[IO.File]::ReadAllText($sourcePath)
function Extract-Function([string]$Signature){
 $start=$source.IndexOf($Signature,[StringComparison]::Ordinal)
 if($start -lt 0){throw "Missing production function: $Signature"}
 $brace=$source.IndexOf('{',$start);$depth=0
 for($i=$brace;$i -lt $source.Length;$i++){
  if($source[$i] -eq '{'){$depth++};if($source[$i] -eq '}'){$depth--;if($depth -eq 0){return $source.Substring($start,$i-$start+1)}}
 }
 throw 'Unbalanced production function'
}
$notify=Extract-Function 'static void fanHostNoteMsiModeWrite(unsigned long long generation) noexcept {'
$payload=Extract-Function 'static void fanLeaseKeepaliveAddMsiMode(json& payload) {'
if($source -notmatch 'if \(step.first == 210\) fanModeMayHaveChanged = true;'){throw 'Mode-write boundary marker missing'}
if($source -notmatch 'if \(fanModeMayHaveChanged\) fanHostNoteMsiModeWrite\(generation\);'){throw 'Actual mode completion/unknown notification missing'}
if($source -notmatch 'payload\["leaseId"\] = leaseId;\s*fanLeaseKeepaliveAddMsiMode\(payload\);'){throw 'Authenticated keepalive payload no longer reaches helper'}
$fixture=@'
#include <atomic>
#include <stdexcept>
#include <iostream>
#include "json.hpp"
using json=nlohmann::json;using ULONGLONG=unsigned long long;
static std::atomic<bool> g_fanLeaseKeepaliveArmed{false},g_exitRequested{false};
static std::atomic<ULONGLONG> g_fanMsiModeRevision{0};
static std::atomic<unsigned long long> g_fanMsiModePowerGeneration{0};
static unsigned long long generation=7;static bool allowed=true,logThrows=false;static unsigned logs=0;
static unsigned long long currentPowerGeneration(){return generation;}
static unsigned long long fanGeneration=7;
static unsigned long long currentFanPowerGeneration(){return fanGeneration;}
static bool fanHostPowerMayWrite(){return allowed;}
static void appendFanLifecycleLog(const char*,const json&){++logs;if(logThrows)throw std::runtime_error("log-only");}
'@
$cases=@'
int main(){unsigned checks=0,fail=0;auto ck=[&](bool ok,const char*name){++checks;if(!ok)++fail;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
fanHostNoteMsiModeWrite(7);ck(g_fanMsiModeRevision==0,"disabled fan cannot queue replay");
g_fanLeaseKeepaliveArmed=true;fanHostNoteMsiModeWrite(6);ck(g_fanMsiModeRevision==0,"stale power generation cannot queue replay");
allowed=false;fanHostNoteMsiModeWrite(7);ck(g_fanMsiModeRevision==0,"suspend/closed admission cannot queue replay");
allowed=true;g_exitRequested=true;fanHostNoteMsiModeWrite(7);ck(g_fanMsiModeRevision==0,"exit cannot publish replay");
g_exitRequested=false;fanHostNoteMsiModeWrite(7);ck(g_fanMsiModeRevision==1&&logs==1,"current mode edge published once");
json first={{"leaseId","fixture"}};fanLeaseKeepaliveAddMsiMode(first);ck(first.value("msiFirmwareModeRevision",0)==1&&first.value("leaseId","")=="fixture","keepalive preserves lease and carries edge");
json repeated={{"leaseId","fixture"}};fanLeaseKeepaliveAddMsiMode(repeated);ck(repeated==first&&g_fanMsiModeRevision==1,"heartbeats cannot manufacture new edges");
generation=8;fanGeneration=8;json stale;fanLeaseKeepaliveAddMsiMode(stale);ck(!stale.contains("msiFirmwareModeRevision"),"old wake generation not sent");
logThrows=true;fanHostNoteMsiModeWrite(8);ck(g_fanMsiModeRevision==2,"fan log failure cannot break TDP result");
json newer;fanLeaseKeepaliveAddMsiMode(newer);ck(newer.value("msiFirmwareModeRevision",0)==2,"new mode edge uses new revision");
fanGeneration=9;fanHostNoteMsiModeWrite(8);ck(g_fanMsiModePowerGeneration==9&&g_fanMsiModeRevision==3,"shared TDP edge maps to current private Fan cycle");
json mapped;fanLeaseKeepaliveAddMsiMode(mapped);ck(mapped.value("msiFirmwareModeRevision",0)==3,"mapped Fan cycle retains the real mode replay notification");
std::cout<<"checks="<<checks<<" failures="<<fail<<"\n";return fail?1:0;}
'@
$fixturePath=Join-Path $EvidenceRoot 'actual-fan-mode-functions.cpp'
[IO.File]::WriteAllText($fixturePath,$fixture+"`r`n"+$notify+"`r`n"+$payload+"`r`n"+$cases,[Text.UTF8Encoding]::new($false))
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $lines){$eq=$line.IndexOf('=');if($eq -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$eq),$line.Substring($eq+1),'Process')}}
$exe=Join-Path $EvidenceRoot 'native-mode-notification.exe'
& cl.exe /nologo /std:c++17 /EHsc /utf-8 $fixturePath "/I$ProjectRoot\deps\json" "/Fo$EvidenceRoot\fixture.obj" "/Fe$exe" *> (Join-Path $EvidenceRoot 'compile.out')
if($LASTEXITCODE -ne 0){Get-Content -LiteralPath (Join-Path $EvidenceRoot 'compile.out');throw 'native mode gate compile failed'}
& $exe *> (Join-Path $EvidenceRoot 'gate.out')
$exit=$LASTEXITCODE
Get-Content -LiteralPath (Join-Path $EvidenceRoot 'gate.out')
@{sourceSha256=(Get-FileHash -LiteralPath $sourcePath).Hash;scope='actual-extracted-native-notification-functions-with-fake-owner-state-no-hardware';exit=$exit}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'result.json') -Encoding utf8
if($exit -ne 0){throw 'native mode gate failed'}