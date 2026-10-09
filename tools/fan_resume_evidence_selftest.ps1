[CmdletBinding()]
param([string]$OutputRoot='')
$ErrorActionPreference='Stop'
$ownModules=Join-Path $PSHOME 'Modules'
if (($env:PSModulePath -split ';')[0].TrimEnd('\') -ine $ownModules.TrimEnd('\')) {$env:PSModulePath=$ownModules+';'+$env:PSModulePath}
$project=Split-Path -Parent $PSScriptRoot
if (!$OutputRoot) {$OutputRoot=Join-Path ([IO.Path]::GetFullPath((Join-Path $project '..\..'))) 'Build\Validation\FanResumeEvidence'}
$base=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
$run=Join-Path $base ([guid]::NewGuid().ToString('N'))
if (!$run.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'UNSAFE_TEST_ROOT'}
New-Item -ItemType Directory -Path $run -Force|Out-Null
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
if (!(Test-Path -LiteralPath $vcvars)){throw 'MSVC_TOOLCHAIN_UNAVAILABLE'}
$bat=Join-Path $run 'compile-and-test.bat'
@"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /MT /I"$project\deps\json" /I"$project\native" "$project\tools\fan_resume_evidence_selftest.cpp" /Fo"$run\selftest.obj" /Fe"$run\selftest.exe"
if errorlevel 1 exit /b 1
"$run\selftest.exe"
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat *> (Join-Path $run 'compile-and-test.log')
$code=$LASTEXITCODE
Get-Content -LiteralPath (Join-Path $run 'compile-and-test.log') -Tail 10
Write-Output ('FAN_RESUME_EVIDENCE_OUTPUT='+$run)
if($code){throw ('FAN_RESUME_EVIDENCE_TEST_FAILED '+$code)}
