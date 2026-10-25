[CmdletBinding()]
param([string]$OutputRoot='',[string]$ProductSourceRoot='')
$ErrorActionPreference='Stop'

# Windows PowerShell launched by pnpm/pwsh must resolve its own built-in modules first.
$ownModules = Join-Path $PSHOME 'Modules'
if (($env:PSModulePath -split ';')[0].TrimEnd('\') -ine $ownModules.TrimEnd('\')) {
  $env:PSModulePath = $ownModules + ';' + $env:PSModulePath
}
$project=if($ProductSourceRoot){[IO.Path]::GetFullPath($ProductSourceRoot)}else{Split-Path -Parent $PSScriptRoot}
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
$fixture=$fixture.Replace('//__PRODUCTION_ACTIVITY_ADMISSION__',(Slice 'static bool updateBackgroundAdmitted() {' 'static std::mutex                        g_updateOperationMtx;'))
$declaration=Slice 'struct DownloadAttemptResult {' 'static bool downloadFile('
$fixture=$fixture.Replace('//__PRODUCTION_RESULT_DECLARATION__',$declaration)
$transport=Slice 'static std::string queryWinHttpHeader(' 'static bool downloadFile('
$fixture=$fixture.Replace('//__PRODUCTION_TRANSPORT__',$transport)
# A forward declaration exists earlier; the definition is the last occurrence.
$a=$main.LastIndexOf('static std::string sha256FileCancellable(');$b=$main.IndexOf('struct StrictUpdateVersion',$a)
if($a -lt 0 -or $b -le $a){throw 'PRODUCTION_SHA_BOUNDARY_MISSING'}
$fixture=$fixture.Replace('//__PRODUCTION_SHA256__',$main.Substring($a,$b-$a))
# Keep the actual version/repair-admission implementation. Only its protected
# journal IO is a scoped fixture provider; no ProgramData or installed root read.
$versionGate=Slice 'struct StrictUpdateVersion {' 'static std::string readPackagedUpdateVersion('
$journalCall='ymcc::updaterecovery::read('
if(([regex]::Matches($versionGate,[regex]::Escape($journalCall))).Count -ne 2){throw 'VERSION_GATE_JOURNAL_BOUNDARY_CHANGED'}
$versionGate=$versionGate.Replace($journalCall,'fixtureRecoveryRead(')
$fixture=$fixture.Replace('//__PRODUCTION_VERSION_ADMISSION__',$versionGate)

$start='ipc_on("app.downloadUpdate", [](const json& a) -> json {'
$a=$main.IndexOf($start);$b=$main.IndexOf('    // Install the complete release payload',$a)
if($a -lt 0 -or $b -le $a){throw 'PRODUCTION_HANDLER_BOUNDARY_MISSING'}
$handler=$main.Substring($a+$start.Length,$b-$a-$start.Length)
$handler=[regex]::Replace($handler,'\s*\}\);\s*$','')
$fixture=$fixture.Replace('//__PRODUCTION_UPDATE_HANDLER__',$handler)
$fixture=$fixture.Replace('//__PRODUCTION_PREDOWNLOAD_POLICY__',(Slice '// Dedicated updater policy;' 'static void reg_updater()'))
$fixture=$fixture.Replace('//__PRODUCTION_PREDOWNLOAD_REGISTRATION__',(Slice '    ipc_on("app.updatePredownloadActivity"' '    ipc_on("app.version"'))
$fixture=$fixture.Replace('//__PRODUCTION_CACHE_REGISTRATION__',(Slice '    ipc_on("app.retireUpdateDownload"' '    ipc_on("app.downloadUpdate"'))
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
$record['productSourceRoot']=$project;$record['harnessSha256']=(Get-FileHash -LiteralPath $PSCommandPath).Hash;$record['templateSha256']=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'updater_resume_native_fixture.cpp')).Hash
$record['versionAdmission']='Actual version/parser/repair-admission source with the two protected-journal reads supplied by a scoped inert fixture; actual authorizes/live functions and version.h compiled.'
$record|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $run 'run.json') -Encoding UTF8
Write-Output ('UPDATER_RESUME_TEST_EVIDENCE='+$run)
if($code){throw ('UPDATER_RESUME_NATIVE_TEST_FAILED '+$code)}
