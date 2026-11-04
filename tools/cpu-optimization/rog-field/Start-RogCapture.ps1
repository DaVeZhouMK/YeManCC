[CmdletBinding()]
param(
 [ValidateSet('Menu','Counters','Trace','Pack','Check')][string]$Action='Menu',
 [string]$ExpectedImagePath='',
 [ValidateSet('S0-VIRTUAL-OFF','S1-PAD-STILL','S2-IMU-STILL','S3-IMU-MOVE','S4-IMU-LOG')][string]$Scenario='S2-IMU-STILL',
 [ValidateRange(0,600)][int]$DurationSeconds=0,
 [string]$EvidenceRoot='',
 [switch]$NoPrompt
)
$ErrorActionPreference='Stop'
$script:lastCollectionExitCode=0
$kitRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if(-not $EvidenceRoot){$EvidenceRoot=Join-Path $kitRoot 'Results'}
$EvidenceRoot=[IO.Path]::GetFullPath($EvidenceRoot)
$null=[IO.Directory]::CreateDirectory($EvidenceRoot)
function Write-FieldJson($value,[string]$path){[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))}
function Get-ImagePath {
 if(-not $script:ExpectedImagePath){
  $default='C:\SOFT\YeMan\YeManCC\YeManCC.exe'
  if($NoPrompt){$script:ExpectedImagePath=$default}else{
   $typed=Read-Host ('YMCC exe full path [Enter = '+$default+']')
   $script:ExpectedImagePath=if($typed){$typed.Trim('"')}else{$default}
  }
 }
 $script:ExpectedImagePath=[IO.Path]::GetFullPath($script:ExpectedImagePath)
 if(-not [IO.File]::Exists($script:ExpectedImagePath) -or [IO.Path]::GetFileName($script:ExpectedImagePath) -ine 'YeManCC.exe'){throw 'ExpectedImagePath must be the existing YeManCC.exe being observed.'}
 return $script:ExpectedImagePath
}
function New-FieldRun([string]$kind){
 $path=Join-Path $EvidenceRoot ($kind+'-'+$Scenario+'-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
 $null=New-Item -ItemType Directory -Path $path
 return $path
}
function Get-BuildComparison([string]$image){
 $hash=(Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.ToLowerInvariant()
 $expected=$null;$buildPath=Join-Path $kitRoot 'CANDIDATE-IDENTITY.json'
 if(Test-Path -LiteralPath $buildPath){$expected=Get-Content -LiteralPath $buildPath -Raw -Encoding UTF8|ConvertFrom-Json}
 return [ordered]@{imagePath=$image;actualDiskSha256=$hash;expectedCandidateSha256=$(if($expected){$expected.nativeExeSha256}else{$null});nativeCandidateMatches=$(if($expected){$hash -ceq $expected.nativeExeSha256}else{$null});buildId=$(if($expected){$expected.buildId}else{$null});hostCandidateMatch='UNKNOWN; compare bound InputHost diskSha256 against candidate hostExeSha256/hostDllSha256';meaning='Actual running build must be bound by PID/birth/held handle in IDENTITY.json; copying source does not deploy a build.'}
}
function Read-Scenario {
 Write-Host 'S0 = virtual OFF, gyro OFF, detailed logs OFF, on table (120s)'
 Write-Host 'S1 = virtual ON, gyro OFF, detailed logs OFF, on table (120s)'
 Write-Host 'S2 = virtual ON, gyro ON, detailed logs OFF, on table (120s)'
 Write-Host 'S3 = same as S2, gently move handheld (30s)'
 Write-Host 'S4 = same as S2, detailed logging ON (Settings single switch), on table (60s)'
 $pick=Read-Host 'Scenario [0/1/2/3/4; Enter=2]'
 $script:Scenario=switch($pick){'0'{'S0-VIRTUAL-OFF'}'1'{'S1-PAD-STILL'}'3'{'S3-IMU-MOVE'}'4'{'S4-IMU-LOG'}default{'S2-IMU-STILL'}}
}
function Invoke-FieldCapture([bool]$trace){
 $script:lastCollectionExitCode=0
 $image=Get-ImagePath
 $run=New-FieldRun $(if($trace){'Trace'}else{'Counters'})
 $duration=if($DurationSeconds){$DurationSeconds}elseif($trace){60}elseif($Scenario -eq 'S3-IMU-MOVE'){30}elseif($Scenario -eq 'S4-IMU-LOG'){60}else{120}
 if($duration -lt 10){throw 'Capture duration must be at least 10 seconds.'}
 if($trace -and $duration -gt 120){throw 'Field trace limit is 120 seconds.'}
 $comparison=Get-BuildComparison $image
 Write-FieldJson $comparison (Join-Path $run 'BUILD-COMPARISON.json')
 if($comparison.nativeCandidateMatches -eq $false){Write-Host 'Current EXE differs from kit candidate. This run will describe your installed build.' -ForegroundColor Yellow}
 $isAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
 if($trace -and -not $isAdmin){throw 'Trace needs Administrator. Right-click Start-Capture.cmd > Run as administrator, then choose 2. No elevation was attempted.'}
 $note='NOT_PROVIDED'
 if(-not $NoPrompt){
  Write-Host ('Manually prepare '+$Scenario+'. Keep the same power mode/persona/background apps. Do not sleep during this run.')
  $note=Read-Host 'Optional note: persona / gyro mode / power / observed spike; Enter to continue'
  Write-Host 'Place handheld on the table now. Sampling starts in 8 seconds.'
  Start-Sleep -Seconds 8
 }
 Write-FieldJson ([ordered]@{startedUtc=[DateTime]::UtcNow.ToString('o');scenario=$Scenario;declaredByUser=$true;stateAutomaticallyVerified=$false;note=$note;requestedSeconds=$duration;trace=$trace;elevated=$isAdmin;automaticSettingsChanges=$false;imuSourceBinding='UNKNOWN until normal YMCC log export';keyboardOrMouseDuringStationaryScenario='Do not intentionally operate during sampling';wholeAppCpu='Only native root plus bound InputHost counters; WebView/other helpers excluded. ETL contains whole-system diagnostic data.'}) (Join-Path $run 'SCENARIO.json')
 $counterScript=Join-Path $PSScriptRoot 'Observe-RogCpu.ps1'
 if(-not $trace){
  $counterOutput=& $counterScript -ExpectedImagePath $image -EvidenceRoot $run -Scenario $Scenario -DurationSeconds $duration
  $script:lastCollectionExitCode=$LASTEXITCODE
  $counterResult=($counterOutput -join "`n")|ConvertFrom-Json
  Write-FieldJson $counterResult (Join-Path $run 'COUNTER-RESULT.json')
  $counterOutput|Write-Output
  if($script:lastCollectionExitCode -ne 0){Write-Host 'Counter capture is PARTIAL/ERROR. Keep all original evidence; missing values are not zero.' -ForegroundColor Yellow}
 }else{
  $rows=@(Get-CimInstance Win32_Process -Filter "Name='YeManCC.exe'"|Where-Object{$_.ExecutablePath -and [IO.Path]::GetFullPath([string]$_.ExecutablePath) -ieq $image})
  if($rows.Count -ne 1){throw 'Trace needs exactly one root matching the selected full image path.'}
  $target=[Diagnostics.Process]::GetProcessById([int]$rows[0].ProcessId)
  try{$birth=$target.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}finally{$target.Dispose()}
  $idPath=Join-Path $run 'EXPECTED-ACTUAL-EXE.json'
  Write-FieldJson ([ordered]@{exeSha256=$comparison.actualDiskSha256;meaning='Actual installed build selected for this run, candidate comparison kept separately.'}) $idPath
  foreach($pathArg in @($counterScript,$image,$run)){if($pathArg.Contains('"') -or $pathArg.Contains("`n")){throw 'Unsafe quoted argument'}}
  $hostExe=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
  $argsText='-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$counterScript+'" -ExpectedImagePath "'+$image+'" -EvidenceRoot "'+$run+'" -Scenario '+$Scenario+' -DurationSeconds '+$duration
  $observer=Start-Process -FilePath $hostExe -ArgumentList $argsText -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'COUNTER-CONSOLE.txt') -RedirectStandardError (Join-Path $run 'COUNTER-ERROR.txt')
  try{
   $captureScript=Join-Path (Split-Path -Parent $PSScriptRoot) 'Capture-YmccCpuStacks.ps1'
   & $captureScript -Mode Capture -ProfileMode Rog -FullDuration -FileMode -MaximumSeconds $duration -TargetProcessId ([int]$rows[0].ProcessId) -ExpectedBirthFileTime $birth -ExpectedIdentityPath $idPath -ExpectedImagePath $image -EvidenceRoot $run
  }finally{
   $wait=[Diagnostics.Stopwatch]::StartNew()
   while(-not $observer.HasExited -and $wait.Elapsed.TotalSeconds -lt ($duration+20)){[void]$observer.WaitForExit(1000)}
   $observer.Refresh()
   Write-FieldJson ([ordered]@{pid=$observer.Id;exited=$observer.HasExited;exitCode=$(if($observer.HasExited){$observer.ExitCode}else{$null});automaticTermination=$false;errors='COUNTER-ERROR.txt'}) (Join-Path $run 'COUNTER-CHILD.json')
   if($observer.HasExited -and $observer.ExitCode -ne 0){$script:lastCollectionExitCode=$observer.ExitCode;Write-Host 'Counter sidecar is partial/failed; inspect COUNTER-ERROR.txt and SUMMARY.json.' -ForegroundColor Yellow}
   $observer.Dispose()
  }
 }
 Write-Host ('Capture finished. Results: '+$run) -ForegroundColor $(if($script:lastCollectionExitCode -eq 0){'Green'}else{'Yellow'})
}
function Invoke-Pack {
 $entries=@(Get-ChildItem -LiteralPath $EvidenceRoot -Force)
 if(-not $entries.Count){throw 'No results yet.'}
 $zip=Join-Path $kitRoot ('ROG-CPU-RESULTS-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)+'.zip')
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 [IO.Compression.ZipFile]::CreateFromDirectory($EvidenceRoot,$zip,[IO.Compression.CompressionLevel]::Fastest,$true)
 Write-Host ('Send this ZIP plus the normal YMCC log export: '+$zip) -ForegroundColor Green
}
function Invoke-Check {
 $image=Get-ImagePath
 Get-BuildComparison $image|ConvertTo-Json -Depth 5|Write-Host
 $tokens=$null;$errors=$null
 foreach($file in @('Observe-RogCpu.ps1','Start-RogCapture.ps1')){[void][Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $file),[ref]$tokens,[ref]$errors);if($errors.Count){throw ($errors|Out-String)}}
 $wpr=Get-Command wpr.exe -ErrorAction SilentlyContinue
 Write-Host ('WPR available: '+[bool]$wpr+'. Counter collection requires no WPR or SDK.')
 if($wpr){& $wpr.Source -profiledetails ((Join-Path $PSScriptRoot 'RogCpu.wprp')+'!RogCpu.Verbose');if($LASTEXITCODE -ne 0){throw 'WPR profile rejected; counters remain independently available.'}}
}
try{
 if($Action -eq 'Menu'){
  while($true){
   Write-Host "`nYMCC ROG CPU field capture - manual app/device control"
   Write-Host '1 Counters  2 Scheduling + JIT/GC/wait trace (Admin)  3 Pack results  4 Check tools/build  0 Exit'
   $choice=Read-Host 'Choose'
   if($choice -eq '0'){break}
   try{switch($choice){'1'{Read-Scenario;Invoke-FieldCapture $false}'2'{Read-Scenario;Invoke-FieldCapture $true}'3'{Invoke-Pack}'4'{Invoke-Check}default{Write-Host 'Choose 0..4'}}}catch{Write-Host $_.Exception.Message -ForegroundColor Red}
  }
 }elseif($Action -eq 'Counters'){Invoke-FieldCapture $false}elseif($Action -eq 'Trace'){Invoke-FieldCapture $true}elseif($Action -eq 'Pack'){Invoke-Pack}else{Invoke-Check}
 if($Action -in @('Counters','Trace') -and $script:lastCollectionExitCode -ne 0){exit $script:lastCollectionExitCode}
}catch{
 $errorFile=Join-Path $EvidenceRoot ('FIELD-ERROR-'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.json')
 Write-FieldJson ([ordered]@{status='FAILED';atUtc=[DateTime]::UtcNow.ToString('o');error=$_.Exception.Message;stack=$_.ScriptStackTrace;appsControlled=$false}) $errorFile
 Write-Error $_
 exit 1
}
