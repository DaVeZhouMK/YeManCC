@echo off
setlocal
set "YMCC_OWNER_SELFTEST_CMD=%~f0"
set "YMCC_OWNER_EVIDENCE=%~1"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -Command "$text=[IO.File]::ReadAllText($env:YMCC_OWNER_SELFTEST_CMD); $marker='# POWERSHELL ENTRY'; & ([scriptblock]::Create($text.Substring($text.LastIndexOf($marker)+$marker.Length)))"
exit /b %errorlevel%
# POWERSHELL ENTRY
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path (Split-Path $env:YMCC_OWNER_SELFTEST_CMD) '..'))
$evidenceRoot = $env:YMCC_OWNER_EVIDENCE
if (-not $evidenceRoot) { $evidenceRoot = 'G:\YeManCC-Work\Testing\YMCC\Evidence\DeckyBindingOwner-20261023' }
$run = Join-Path $evidenceRoot ('run-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
[IO.Directory]::CreateDirectory($run) | Out-Null
$receipt = [ordered]@{ schemaVersion=1; run=$run; startedAt=(Get-Date -Format o); componentOnly=$true; runtimeIntegrated=$false; originalStalledGateFixed=$false; passed=$false; sources=@(); compiler=@(); guardian=$null }
function Get-OwnerFileHash([string]$LiteralPath,[string]$Algorithm) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($LiteralPath)
    try { return [pscustomobject]@{ Hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') } }
    finally { $stream.Dispose(); $sha.Dispose() }
}
function Save-Receipt { $receipt | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $run 'receipt.json') -Encoding UTF8 }
Save-Receipt
try {
    $files = @('native\decky_binding_process_owner.h','tools\decky_binding_process_owner_selftest.cpp','tools\decky_binding_process_owner_selftest.cmd','deps\json\json.hpp')
    foreach ($relative in $files) {
        $source = Join-Path $repo $relative
        $target = Join-Path $run ('source\' + $relative)
        [IO.Directory]::CreateDirectory((Split-Path $target)) | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
        $sourceHash = (Get-OwnerFileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        $frozenHash = (Get-OwnerFileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($sourceHash -ne $frozenHash) { throw ('source changed during freeze: ' + $relative) }
        $receipt.sources += [ordered]@{ path=$relative; original=$source; frozen=$target; sha256=$frozenHash }
    }
    $msvcRoot = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
    $msvc = Get-ChildItem -LiteralPath $msvcRoot -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $msvc) { throw 'MSVC x64 BuildTools missing' }
    $sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
    $sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'um\windows.h') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $sdkVersion) { throw 'Windows SDK missing' }
    $bin = Join-Path $msvc.FullName 'bin\Hostx64\x64'
    $compiler = Join-Path $bin 'cl.exe'
    $env:INCLUDE = (Join-Path $msvc.FullName 'include') + ';' + (Join-Path $sdkVersion.FullName 'um') + ';' + (Join-Path $sdkVersion.FullName 'shared') + ';' + (Join-Path $sdkVersion.FullName 'ucrt')
    $env:LIB = (Join-Path $msvc.FullName 'lib\x64') + ';' + (Join-Path $sdk ('Lib\' + $sdkVersion.Name + '\um\x64')) + ';' + (Join-Path $sdk ('Lib\' + $sdkVersion.Name + '\ucrt\x64'))
    $env:PATH = $bin + ';' + $env:PATH
    foreach ($name in @('cl.exe','c1xx.dll','c2.dll','link.exe')) {
        $path = Join-Path $bin $name
        $receipt.compiler += [ordered]@{ path=$path; version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion; sha256=(Get-OwnerFileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    $receipt.sdkVersion = $sdkVersion.Name
    $receipt.msvcVersion = $msvc.Name
    $exe = Join-Path $run 'decky-binding-owner-selftest.exe'
    $arguments = @('/nologo','/Bv','/std:c++20','/permissive-','/utf-8','/EHsc','/MT','/O2','/W4','/WX','/D_WIN32_WINNT=0x0A00','/DUNICODE','/D_UNICODE',('/I'+(Join-Path $run 'source\deps\json')),(Join-Path $run 'source\tools\decky_binding_process_owner_selftest.cpp'),('/Fo'+(Join-Path $run 'selftest.obj')),('/Fe'+$exe),'/link','/INCREMENTAL:NO','/DYNAMICBASE','/NXCOMPAT','shell32.lib')
    $receipt.compileArguments = $arguments
    Save-Receipt
    $ErrorActionPreference = 'Continue'
    & $compiler @arguments > (Join-Path $run 'compile.log') 2>&1
    $ErrorActionPreference = 'Stop'
    $receipt.compileExitCode = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw ('compile failed: ' + (Join-Path $run 'compile.log')) }
    $receipt.exeSha256 = (Get-OwnerFileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    # Distinct test-only image acts as the foreign IPC server. Never a product helper.
    $foreignFixture = Join-Path $run 'foreign-owner-fixture.exe'
    Copy-Item -LiteralPath $exe -Destination $foreignFixture
    $receipt.foreignOwnerFixture = [ordered]@{ path=$foreignFixture; sha256=(Get-OwnerFileHash -LiteralPath $foreignFixture -Algorithm SHA256).Hash.ToLowerInvariant(); role='negative source identity fixture only' }
    $headerProbe = Join-Path $run 'public-header-compile.cpp'
    @"
#include "source/native/decky_binding_process_owner.h"
int main() {
    ymcc::deckybinding::Owner owner;
    auto s = owner.snapshot();
    return owner.stop().fullyDestroyed && s.fullyDestroyed ? 0 : 1;
}
"@ | Set-Content -LiteralPath $headerProbe -Encoding UTF8
    $probeArgs = @('/nologo','/std:c++20','/permissive-','/utf-8','/EHsc','/MT','/O2','/W4','/WX','/D_WIN32_WINNT=0x0A00','/c',$headerProbe,('/Fo'+(Join-Path $run 'public-header.obj')))
    $ErrorActionPreference = 'Continue'
    & $compiler @probeArgs > (Join-Path $run 'public-header-compile.log') 2>&1
    $ErrorActionPreference = 'Stop'
    $receipt.publicHeaderCompileExitCode = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw 'public header standalone compile failed' }
    Save-Receipt
    $guardianSource = @"
using System;
using System.Runtime.InteropServices;
public static class BindingOwnerGuardian {
 [StructLayout(LayoutKind.Sequential)] public struct SI {
  public uint cb; public IntPtr reserved,desktop,title; public uint x,y,xsize,ysize,xchars,ychars,fill,flags;
  public ushort show,reserved2; public IntPtr reservedBytes,input,output,error;
 }
 [StructLayout(LayoutKind.Sequential)] public struct PI { public IntPtr process,thread; public uint pid,tid; }
 [StructLayout(LayoutKind.Sequential)] public struct BasicLimits {
  public long processTime,jobTime; public uint flags; public UIntPtr minWorking,maxWorking;
  public uint activeLimit; public UIntPtr affinity; public uint priority,scheduling;
 }
 [StructLayout(LayoutKind.Sequential)] public struct IoCounters { public ulong a,b,c,d,e,f; }
 [StructLayout(LayoutKind.Sequential)] public struct Limits {
  public BasicLimits basic; public IoCounters io; public UIntPtr processMemory,jobMemory,peakProcess,peakJob;
 }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr CreateJobObjectW(IntPtr security,string name);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool SetInformationJobObject(IntPtr job,int type,ref Limits info,uint length);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern bool CreateProcessW(string image,System.Text.StringBuilder command,IntPtr ps,IntPtr ts,bool inherit,uint flags,IntPtr env,string cwd,ref SI si,out PI pi);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool AssignProcessToJobObject(IntPtr job,IntPtr process);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern uint ResumeThread(IntPtr thread);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern uint WaitForSingleObject(IntPtr handle,uint ms);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool TerminateJobObject(IntPtr job,uint code);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool TerminateProcess(IntPtr process,uint code);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool GetExitCodeProcess(IntPtr process,out uint code);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool CloseHandle(IntPtr handle);
}
"@
    $guardianSource | Set-Content -LiteralPath (Join-Path $run 'guardian.cs') -Encoding UTF8
    Add-Type -TypeDefinition $guardianSource
    $job = [BindingOwnerGuardian]::CreateJobObjectW([IntPtr]::Zero,$null)
    if ($job -eq [IntPtr]::Zero) { throw 'guardian CreateJobObject failed' }
    $pi = New-Object BindingOwnerGuardian+PI
    $assigned = $false
    $guardian = [ordered]@{ createdSuspended=$false; assignedBeforeResume=$false; killOnClose=$true; breakaway=$false; timeoutMs=60000; timedOut=$false; childExited=$false; handlesClosed=$false }
    $receipt.guardian = $guardian
    try {
        $limits = New-Object BindingOwnerGuardian+Limits
        $limits.basic.flags = 0x2000
        if (-not [BindingOwnerGuardian]::SetInformationJobObject($job,9,[ref]$limits,[Runtime.InteropServices.Marshal]::SizeOf($limits))) { throw 'guardian limits failed' }
        $si = New-Object BindingOwnerGuardian+SI
        $si.cb = [Runtime.InteropServices.Marshal]::SizeOf($si); $si.flags = 1; $si.show = 0
        $report = Join-Path $run 'selftest-result.json'
        $command = New-Object Text.StringBuilder ('"' + $exe + '" --report "' + $report + '"')
        if (-not [BindingOwnerGuardian]::CreateProcessW($exe,$command,[IntPtr]::Zero,[IntPtr]::Zero,$false,0x08000004,[IntPtr]::Zero,$run,[ref]$si,[ref]$pi)) { throw 'guardian fixture CreateProcess failed' }
        $guardian.createdSuspended = $true; $guardian.fixturePid = $pi.pid
        if (-not [BindingOwnerGuardian]::AssignProcessToJobObject($job,$pi.process)) { throw 'guardian assign failed' }
        $assigned = $true; $guardian.assignedBeforeResume = $true
        if ([BindingOwnerGuardian]::ResumeThread($pi.thread) -eq [uint32]::MaxValue) { throw 'guardian resume failed' }
        $waited = [BindingOwnerGuardian]::WaitForSingleObject($pi.process,60000)
        $guardian.timedOut = $waited -eq 258
        $guardian.childExited = $waited -eq 0
        if ($waited -ne 0) { throw ('fixture guardian bounded wait failed: ' + $waited) }
        [uint32]$code = 259
        if (-not [BindingOwnerGuardian]::GetExitCodeProcess($pi.process,[ref]$code)) { throw 'fixture exit code unknown' }
        $guardian.exitCode = $code
        if ($code -ne 0) { throw ('selftest failed, exit=' + $code) }
        $testReport = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
        if (-not $testReport.passed) { throw 'selftest report is not PASS' }
        $receipt.caseCount = $testReport.caseCount
        $receipt.passed = $true
    } finally {
        if ($pi.process -ne [IntPtr]::Zero -and [BindingOwnerGuardian]::WaitForSingleObject($pi.process,0) -ne 0) {
            if ($assigned) { $guardian.emergencyTerminationAccepted = [BindingOwnerGuardian]::TerminateJobObject($job,92) }
            else { $guardian.emergencyTerminationAccepted = [BindingOwnerGuardian]::TerminateProcess($pi.process,92) }
            $guardian.childExited = [BindingOwnerGuardian]::WaitForSingleObject($pi.process,1500) -eq 0
        }
        $closed = $true
        if ($pi.thread -ne [IntPtr]::Zero) { $closed = [BindingOwnerGuardian]::CloseHandle($pi.thread) -and $closed }
        if ($pi.process -ne [IntPtr]::Zero) { $closed = [BindingOwnerGuardian]::CloseHandle($pi.process) -and $closed }
        $closed = [BindingOwnerGuardian]::CloseHandle($job) -and $closed
        $guardian.handlesClosed = $closed
        if (-not $closed -or -not $guardian.childExited) { $receipt.passed = $false }
        Save-Receipt
    }
    foreach ($source in $receipt.sources) {
        if ((Get-OwnerFileHash -LiteralPath $source.original -Algorithm SHA256).Hash.ToLowerInvariant() -ne $source.sha256) { throw ('source drift after run: ' + $source.path) }
    }
} catch {
    $receipt.passed = $false
    $receipt.failure = $_.ToString()
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $run 'failure.txt') -Encoding UTF8
} finally {
    $receipt.finishedAt = Get-Date -Format o
    Save-Receipt
    Write-Output ('EVIDENCE=' + $run)
    Write-Output ('PASS=' + $receipt.passed)
}
if ($receipt.passed) { exit 0 } else { exit 1 }