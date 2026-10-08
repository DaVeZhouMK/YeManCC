[CmdletBinding()]
param([string]$OutputRoot='')
$ErrorActionPreference='Stop'

# Windows PowerShell launched by pnpm/pwsh must resolve its own built-in modules first.
$ownModules = Join-Path $PSHOME 'Modules'
if (($env:PSModulePath -split ';')[0].TrimEnd('\') -ine $ownModules.TrimEnd('\')) {
  $env:PSModulePath = $ownModules + ';' + $env:PSModulePath
}
$project=Split-Path -Parent $PSScriptRoot
if (!$OutputRoot) {$OutputRoot=Join-Path ([IO.Path]::GetFullPath((Join-Path $project '..\..'))) 'Build\Validation\UpdaterResume'}
$base=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
$run=Join-Path $base ([guid]::NewGuid().ToString('N'))
if (!$run.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'UNSAFE_TEST_ROOT'}
# No aliases are allowed for the output root. Never clean up any existing path.
$probe=$base
while($probe -and $probe.Length -gt 3){if(Test-Path -LiteralPath $probe){if((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'REPARSE_TEST_ROOT'}};$probe=Split-Path -Parent $probe}
New-Item -ItemType Directory -Path $run -Force|Out-Null
$main=[IO.File]::ReadAllText((Join-Path $project 'native\main.cpp'))
$fixture=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'updater_resume_native_fixture.cpp'))
function Slice([string]$Start,[string]$End){$a=$main.IndexOf($Start);$b=$main.IndexOf($End,$a+1);if($a -lt 0 -or $b -le $a){throw ('PRODUCTION_BOUNDARY_MISSING '+$Start)};return $main.Substring($a,$b-$a)}
$declaration=Slice 'struct DownloadAttemptResult {' 'static bool downloadFile('
$fixture=$fixture.Replace('//__PRODUCTION_RESULT_DECLARATION__',$declaration)
$transport=Slice 'static std::string queryWinHttpHeader(' 'static bool downloadFile('
$fixture=$fixture.Replace('//__PRODUCTION_TRANSPORT__',$transport)
# A forward declaration exists earlier; the definition is the last occurrence.
$a=$main.LastIndexOf('static std::string sha256File(');$b=$main.IndexOf('struct StrictUpdateVersion',$a)
if($a -lt 0 -or $b -le $a){throw 'PRODUCTION_SHA_BOUNDARY_MISSING'}
$fixture=$fixture.Replace('//__PRODUCTION_SHA256__',$main.Substring($a,$b-$a))
$start='ipc_on("app.downloadUpdate", [](const json& a) -> json {'
$a=$main.IndexOf($start);$b=$main.IndexOf('    // Install the complete release payload',$a)
if($a -lt 0 -or $b -le $a){throw 'PRODUCTION_HANDLER_BOUNDARY_MISSING'}
$handler=$main.Substring($a+$start.Length,$b-$a-$start.Length)
$handler=[regex]::Replace($handler,'\s*\}\);\s*$','')
$fixture=$fixture.Replace('//__PRODUCTION_UPDATE_HANDLER__',$handler)
$source=Join-Path $run 'updater-resume-selftest.cpp'
[IO.File]::WriteAllText($source,$fixture,[Text.UTF8Encoding]::new($false))
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue) -and !(Test-Path -LiteralPath $vcvars)) { throw 'MSVC_TOOLCHAIN_UNAVAILABLE' }
$bat=Join-Path $run 'compile-and-test.bat'
@"
@echo off
where cl.exe >nul 2>nul
if errorlevel 1 call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /MT /I"$project\deps\json" /I"$project\native" "$source" /Fo"$run\updater-resume-selftest.obj" /Fe"$run\updater-resume-selftest.exe" /link advapi32.lib user32.lib
if errorlevel 1 exit /b 1
"$run\updater-resume-selftest.exe" "$run\fixtures"
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat *> (Join-Path $run 'compile-and-test.log')
$code=$LASTEXITCODE
Get-Content -LiteralPath (Join-Path $run 'compile-and-test.log') -Tail 12
$record=@{status=$(if($code -eq 0){'PASS'}else{'FAIL'});exit=$code;evidence=$run;mainSourceSha256=(Get-FileHash -LiteralPath (Join-Path $project 'native\main.cpp')).Hash;policySha256=(Get-FileHash -LiteralPath (Join-Path $project 'native\update_download_resume.h')).Hash;scope='No hardware or network/product operation; extracted production native logic with inert WinHTTP transport'}
$record|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $run 'run.json') -Encoding UTF8
Write-Output ('UPDATER_RESUME_TEST_EVIDENCE='+$run)
if($code){throw ('UPDATER_RESUME_NATIVE_TEST_FAILED '+$code)}
