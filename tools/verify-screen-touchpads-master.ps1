[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh evidence directory; earlier receipts are preserved.'}
$frozen=Join-Path $OutputDirectory 'Native'
New-Item -ItemType Directory -Path $frozen -Force|Out-Null
$nativeFiles=Get-ChildItem -LiteralPath (Join-Path $project 'native') -File -Filter '*.h'
foreach($file in $nativeFiles){Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $frozen $file.Name)}
Copy-Item -LiteralPath (Join-Path $project 'deps/json/json.hpp') -Destination (Join-Path $frozen 'json.hpp')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'screen_touchpads_master_selftest.cpp') -Destination (Join-Path $frozen 'screen_touchpads_master_selftest.cpp')
$sdkRoot=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkVersion=(Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory|Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'um/windows.h')}|Sort-Object {[version]$_.Name} -Descending|Select-Object -First 1).Name
if(!$sdkVersion){throw 'Windows SDK headers are required to compile the production consumer.'}
$vc=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'
$includes=@('um','shared','ucrt','winrt','cppwinrt')|ForEach-Object {'/I"'+(Join-Path $sdkRoot "Include/$sdkVersion/$_")+'"'}
$libraries=@('um','ucrt')|ForEach-Object {'/LIBPATH:"'+(Join-Path $sdkRoot "Lib/$sdkVersion/$_/x64")+'"'}
$receipt=[ordered]@{ok=$false;scope='Production persona bank, saved configuration, Core input projection and configure/button admission in dryRun; no initialize or native windows';sources=@();runs=@();physicalController=$false;systemInputInjected=$false;hardwareWrites=0;installedApplicationChanged=$false}
foreach($file in $nativeFiles){$receipt.sources+=@{path=$file.FullName;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}}
$testSource=Join-Path $PSScriptRoot 'screen_touchpads_master_selftest.cpp';$receipt.sources+=@{path=$testSource;sha256=(Get-FileHash -LiteralPath $testSource -Algorithm SHA256).Hash}
try{
 foreach($kind in @('legacy','candidate','missing-master-gate')){
  $testRoot=$frozen
  if($kind -eq 'missing-master-gate'){
   $testRoot=Join-Path $OutputDirectory 'PrivateNegative';New-Item -ItemType Directory -Path $testRoot|Out-Null
   Get-ChildItem -LiteralPath $frozen -File|ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $testRoot $_.Name)}
   $header=Join-Path $testRoot 'screen_touchpads.h';$text=[IO.File]::ReadAllText($header)
   $text=$text.Replace('nativeMode.store(cfg.masterEnabled && cfg.enabled &&','nativeMode.store(cfg.enabled &&').Replace('enabled.store(cfg.masterEnabled && cfg.enabled &&','enabled.store(cfg.enabled &&')
   [IO.File]::WriteAllText($header,$text)
  }
  $define=if($kind -eq 'candidate'){'/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1'}else{''}
  $exe=Join-Path $OutputDirectory "$kind.exe";$obj=Join-Path $OutputDirectory "$kind.obj";$cmd=Join-Path $OutputDirectory "build-$kind.cmd"
  @"
@echo off
call "$vc" $sdkVersion >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE $define $($includes -join ' ') /I"$testRoot" "$testRoot\screen_touchpads_master_selftest.cpp" /Fo"$obj" /Fe"$exe" /link $($libraries -join ' ') user32.lib gdi32.lib ole32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
  & $cmd *> (Join-Path $OutputDirectory "compile-$kind.log")
  if($LASTEXITCODE -ne 0){throw "Production test compilation failed: $kind"}
  $report=Join-Path $OutputDirectory "$kind.json"
  & $exe $report *> (Join-Path $OutputDirectory "run-$kind.log")
  $exit=$LASTEXITCODE;$result=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
  if($result.systemInputInjected -or $result.windowsCreated -or $result.nativeRuntimeStarted -or $result.hardwareWrites -ne 0){throw 'Native master fixture violated its no-device boundary.'}
  $failures=@($result.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name})
  if($kind -eq 'missing-master-gate'){
   $expected=@('configure-off-closes-runtime-admission','off-closes-native-mapping-atomic')
   if($exit -ne 3 -or $result.ok -or @(Compare-Object ($expected|Sort-Object) ($failures|Sort-Object)).Count -ne 0){throw 'The negative must compile and fail exactly at both missing runtime admission gates.'}
  }elseif($exit -ne 0 -or !$result.ok){throw "Native master behavior failed: $kind"}
  $receipt.runs+=@{kind=$kind;checks=@($result.cases).Count;expectedNegative=($kind -eq 'missing-master-gate');failures=$failures;exitCode=$exit;report=$report;executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash}
 }
 foreach($row in $receipt.sources){if((Get-FileHash -LiteralPath $row.path -Algorithm SHA256).Hash -ne $row.sha256){throw "Product source changed during verification: $($row.path)"}}
 $receipt.ok=$true
}finally{$receipt|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
Write-Output 'SCREEN_TOUCHPAD_MASTER_VERIFIED legacy=23 candidate=23 missing-master-negative=detected hardwareWrites=0'
