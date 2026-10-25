[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ConfigPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$taskConfig=Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$taskCase=[IO.Path]::GetFullPath([string]$taskConfig.caseRoot)
$taskRoot=[IO.Path]::GetFullPath([string]$taskConfig.root)
function Test-TaskWithin([string]$value) { $taskPrefix=$taskCase.TrimEnd('\')+'\'; return $value.StartsWith($taskPrefix,[StringComparison]::OrdinalIgnoreCase) }
if(-not(Test-TaskWithin $taskRoot)){throw 'LEDGER_RUN_ROOT_OUTSIDE_CASE'}
foreach($taskEntry in @(@('TEMP','private-temp'),@('TMP','private-temp'),@('APPDATA','private-appdata'),@('LOCALAPPDATA','private-localappdata'),@('PROGRAMDATA','private-programdata'))){
 $taskPath=[IO.Path]::GetFullPath((Join-Path $taskRoot $taskEntry[1]))
 if(-not(Test-TaskWithin $taskPath)){throw 'LEDGER_ENVIRONMENT_ROOT_OUTSIDE_CASE'}
 New-Item -ItemType Directory -Path $taskPath -Force | Out-Null
 if(((Get-Item -LiteralPath $taskPath).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'LEDGER_ENVIRONMENT_ROOT_LINK'}
 [Environment]::SetEnvironmentVariable($taskEntry[0],$taskPath,'Process')
}
Add-Type -TypeDefinition @"
using System;using System.Text;using System.Runtime.InteropServices;
public static class YmccLedgerTempProbe {[DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern uint GetTempPathW(uint size,StringBuilder buffer);}
"@
$taskBuffer=[Text.StringBuilder]::new(4096)
$taskLength=[YmccLedgerTempProbe]::GetTempPathW(4096,$taskBuffer)
if($taskLength -eq 0 -or $taskLength -ge 4096){throw 'NATIVE_TEMP_PATH_PROBE_FAILED'}
$taskNativeTemp=[IO.Path]::GetFullPath($taskBuffer.ToString()).TrimEnd('\')
if($taskNativeTemp -ne ([IO.Path]::GetFullPath($env:TEMP)).TrimEnd('\') -or -not(Test-TaskWithin $taskNativeTemp)){throw 'ACTUAL_LEDGER_TEMP_ROOT_NOT_OWNED'}
$taskOutput=[IO.Path]::GetFullPath((Join-Path $taskRoot 'artifacts'))
if(-not(Test-TaskWithin $taskOutput)){throw 'ORIGINAL_LEDGER_OUTPUT_NOT_OWNED'}
@{caseRoot=$taskCase;root=$taskRoot;nativeTempPath=$taskNativeTemp;outputRoot=$taskOutput;appData=$env:APPDATA;localAppData=$env:LOCALAPPDATA;programData=$env:PROGRAMDATA;beforeTempFiles=@(Get-ChildItem -LiteralPath $taskNativeTemp -Force | Select-Object -ExpandProperty FullName)} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskRoot 'path-proof-before.json')
$env:CL='/showIncludes'
$env:_CL_=''
$env:VSLANG='1033'
$taskScript=Join-Path $taskConfig.sourceRoot 'tools/external_controller_priority_ledger_selftest.ps1'
try {
 & $taskScript -OutputRoot $taskOutput -VcVarsPath $taskConfig.toolchainCmd
 if(-not $?){exit 2}
 if($taskConfig.boundary){
  $taskBoundary=Join-Path $taskRoot 'observed-ledger'
  New-Item -ItemType Directory -Path $taskBoundary | Out-Null
  [IO.File]::WriteAllText((Join-Path $taskBoundary 'case.marker'),'ymcc-ledger-boundary-owned-v1',[Text.UTF8Encoding]::new($false))
  $taskCompileLog=Join-Path $taskBoundary 'compile.log'
  $taskExe=Join-Path $taskBoundary 'boundary.exe'
  $taskCpp=Join-Path $taskConfig.sourceRoot 'tools/ymcc_sim/global/controller-priority-ledger/ledger-boundary.cpp'
  Push-Location -LiteralPath $taskBoundary
  try {
   & cl.exe /nologo /std:c++17 /W4 /WX /EHsc /utf-8 /MT $taskCpp ("/I"+(Join-Path $taskConfig.sourceRoot 'deps/json')) ("/Fe"+$taskExe) /Fo'boundary.obj' *> $taskCompileLog
   if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $taskCompileLog -Tail 20;throw 'BOUNDARY_HELPER_COMPILATION_FAILED'}
   & $taskExe $taskCase $taskBoundary
   if($LASTEXITCODE -ne 0){throw 'BOUNDARY_HELPER_ACTUAL_IO_FAILED'}
  } finally {Pop-Location}
 }
} finally {
 @{nativeTempPath=$taskNativeTemp;afterTempFiles=@(Get-ChildItem -LiteralPath $taskNativeTemp -Force | Select-Object -ExpandProperty FullName);compiler=(Get-Command cl.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskRoot 'path-proof-after.json')
}
