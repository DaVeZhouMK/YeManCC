[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[Parameter(Mandatory=$true)][string]$FixtureContractPath)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if($PSVersionTable.PSEdition -cne 'Core'){throw 'LIGHTING_V4_GLOBAL_CORE_REQUIRED'}
if($env:YMCC_SIM_GLOBAL_RUN -cne '1'){throw 'LIGHTING_V4_GLOBAL_OWNED_RUN_REQUIRED'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..')).TrimEnd('\');$out=[IO.Path]::GetFullPath($EvidenceDir).TrimEnd('\')
if($out -ieq $repo -or $out.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase) -or $out -ieq 'C:\SOFT\YeMan' -or $out.StartsWith('C:\SOFT\YeMan\',[StringComparison]::OrdinalIgnoreCase)){throw 'LIGHTING_V4_GLOBAL_OUTPUT_SCOPE'}
if(Test-Path -LiteralPath (Join-Path $out 'result.json')){throw 'Fresh v4 global evidence required'}
New-Item -ItemType Directory -Path $out -Force|Out-Null
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save([string]$p,$v){[IO.File]::WriteAllText($p,($v|ConvertTo-Json -Depth 90),[Text.UTF8Encoding]::new($false))}
function Need([bool]$ok,[string]$code){if(-not$ok){throw $code}}
$contract=Get-Content -LiteralPath $FixtureContractPath -Raw -Encoding UTF8|ConvertFrom-Json
$started=[DateTime]::UtcNow.ToString('o');$processes=[Collections.Generic.List[object]]::new();$steps=[Collections.Generic.List[object]]::new()
$module='tools/ymcc_sim/global/lighting-component-continuity/v4/'
$sourceFiles=@('Run-BuildDelivery.ps1','Archive-IO.ps1','adapter.mjs','fixture.contract.json','README.md'|ForEach-Object {$module+$_})+@($contract.frozenTools.path)+@('tools/hc-device-runtime/verify-lighting-portable-evidence.ps1','tools/ymcc_sim/global/lighting-component-continuity/New-ReviewedPlan.ps1')
$before=@{};foreach($rel in $sourceFiles){$before[$rel]=Hash (Join-Path $repo $rel)}
function Expand-VerifiedArchive([string]$Zip,[string]$Destination,$Expected){
 Need ((Hash $Zip) -ceq $Expected.zip.sha256 -and (Get-Item -LiteralPath $Zip).Length -eq $Expected.zip.bytes) 'LIGHTING_V4_ZIP_BYTES'
 Need (-not(Test-Path -LiteralPath $Destination)) 'LIGHTING_V4_FRESH_EXTRACTION'
 $stream=[IO.File]::OpenRead($Zip);$archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Read);$names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);$rows=[Collections.Generic.List[object]]::new();$total=0L
 try{
  Need ($archive.Entries.Count -eq $Expected.zip.members -and $archive.Entries.Count -le 2048) 'LIGHTING_V4_ZIP_COUNT'
  foreach($entry in $archive.Entries){
   $name=$entry.FullName
   Need ($name -and $name.Length -le 512 -and -not[IO.Path]::IsPathRooted($name) -and $name -notmatch '(^|[\\/])\.\.?([\\/]|$)|:|\\|\x00' -and -not$name.EndsWith('/')) 'LIGHTING_V4_ZIP_PATH'
   Need ($names.Add($name)) 'LIGHTING_V4_ZIP_DUPLICATE'
   Need ((($entry.ExternalAttributes -shr 16) -band 0xf000) -ne 0xa000 -and ($entry.ExternalAttributes -band 0x400) -eq 0) 'LIGHTING_V4_ZIP_REPARSE'
   Need ($entry.Length -ge 0 -and $entry.Length -le 134217728) 'LIGHTING_V4_ZIP_MEMBER_SIZE';$total+=$entry.Length;Need ($total -le 536870912) 'LIGHTING_V4_ZIP_TOTAL_SIZE'
   $path=[IO.Path]::GetFullPath((Join-Path $Destination $name));Need ($path.StartsWith([IO.Path]::GetFullPath($Destination).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) 'LIGHTING_V4_ZIP_ESCAPE'
   $rows.Add([pscustomobject]@{name=$name;path=$path;entry=$entry})
  }
  Need ($total -eq $Expected.zip.uncompressedBytes) 'LIGHTING_V4_ZIP_TOTAL_BINDING'
  New-Item -ItemType Directory -Path $Destination|Out-Null
  foreach($row in $rows){New-Item -ItemType Directory -Path (Split-Path -Parent $row.path) -Force|Out-Null;$input=$row.entry.Open();$output=[IO.File]::Open($row.path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);try{$input.CopyTo($output)}finally{$output.Dispose();$input.Dispose()}}
 }finally{$archive.Dispose();$stream.Dispose()}
 $indexPath=Join-Path $Destination $Expected.index.name;Need ((Hash $indexPath) -ceq $Expected.index.sha256) 'LIGHTING_V4_INDEX_BYTES';$index=Get-Content -LiteralPath $indexPath -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($index.kind -ceq 'ymcc-lighting-build-delivery-fixture-v1' -and @($index.members).Count -eq $rows.Count-1) 'LIGHTING_V4_INDEX_MEMBERS'
 foreach($row in $index.members){$file=[IO.Path]::GetFullPath((Join-Path $Destination $row.path));Need ($file.StartsWith([IO.Path]::GetFullPath($Destination).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase) -and $names.Contains($row.path) -and (Hash $file) -ceq $row.sha256 -and (Get-Item -LiteralPath $file).Length -eq $row.bytes) 'LIGHTING_V4_EXTRACTED_BYTES'}
 return $index
}
function Run-Tool([string]$Name,[string]$Engine,[string]$Tool,[string[]]$Arguments,[string]$Phase,[int]$Timeout=240){
 $argsFile=Join-Path $out ($Name+'-arguments.json');Save $argsFile $Arguments;$trace=Join-Path $out ($Name+'-archive-io.json')
 $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$Engine;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true;$info.WorkingDirectory=$repo
 $info.Environment['PSModulePath']=(Join-Path (Split-Path -Parent $Engine) 'Modules')+';'+$env:PSModulePath
 foreach($value in @('-NoProfile','-ExecutionPolicy','RemoteSigned','-File',$script:bootstrap,'-CaseRoot',$out,'-FixtureRoot',$script:fixture,'-Phase',$Phase,'-Tool',$Tool,'-ArgumentsPath',$argsFile,'-TracePath',$trace)){ $info.ArgumentList.Add($value) }
 $p=[Diagnostics.Process]::new();$p.StartInfo=$info;$start=[DateTime]::UtcNow
 try{
  Need ($p.Start()) 'LIGHTING_V4_CHILD_START';$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();Save (Join-Path $out 'current-stage.json') @{stage=$Name;pid=$p.Id;startedUtc=$start.ToString('o');state='running'}
  $timedOut=$false;while(-not$p.WaitForExit(1000)){if(([DateTime]::UtcNow-$start).TotalSeconds -gt $Timeout){$timedOut=$true;$p.Kill($true);$p.WaitForExit();break}}
  $code=$p.ExitCode;[IO.File]::WriteAllText((Join-Path $out ($Name+'.stdout.log')),$stdout.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false));[IO.File]::WriteAllText((Join-Path $out ($Name+'.stderr.log')),$stderr.GetAwaiter().GetResult(),[Text.UTF8Encoding]::new($false))
  $r=[pscustomobject]@{name=$Name;pid=$p.Id;exitCode=$code;naturalExit=(-not$timedOut);timedOut=$timedOut;engine=$Engine;tool=$Tool;toolSha256=(Hash $Tool);phase=$Phase;startedUtc=$start.ToString('o');finishedUtc=[DateTime]::UtcNow.ToString('o');argumentsPath=$argsFile;trace=$trace};$processes.Add($r);Save (Join-Path $out 'current-stage.json') @{stage=$Name;pid=$p.Id;state='terminal';exitCode=$code}
  Need (-not$timedOut -and $code -eq 0) ('LIGHTING_V4_CHILD_FAILED:'+ $Name);Write-Output ('LIGHTING_V4_STAGE_PASS '+$Name)
 }finally{$p.Dispose()}
}
try{
 foreach($row in $contract.frozenTools){Need ((Hash (Join-Path $repo $row.path)) -ceq $row.sha256) 'LIGHTING_V4_FROZEN_TOOL_CHANGED'}
 $zip=Join-Path (Split-Path -Parent $FixtureContractPath) $contract.zip.name;$script:fixture=Join-Path $out 'fixture';$index=Expand-VerifiedArchive $zip $script:fixture $contract
 $steps.Add(@{name='archive-materialized';status='PASS';members=$contract.zip.members;memberHashesVerified=$true})
 $script:bootstrap=Join-Path $out 'Invoke-ArchiveMappedTool.ps1';Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Archive-IO.ps1') -Destination $script:bootstrap
 $core=Join-Path $PSHOME 'pwsh.exe';$win=Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe';Need ((Test-Path -LiteralPath $core) -and (Test-Path -LiteralPath $win)) 'LIGHTING_V4_ENGINES_REQUIRED'
 $install=Join-Path $out 'installation';New-Item -ItemType Directory -Path $install|Out-Null
 foreach($row in (Get-Content -LiteralPath (Join-Path $script:fixture 'v4/bundle/LIGHTING-BUILD-DELIVERY.json') -Raw -Encoding UTF8|ConvertFrom-Json).installInventory){$from=Join-Path $script:fixture ('fixture-before/'+$row.relative);Need ((Hash $from) -ceq $row.sha256) 'LIGHTING_V4_PREIMAGE_BYTES';$to=Join-Path $install $row.relative;New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force|Out-Null;Copy-Item -LiteralPath $from -Destination $to;Need ((Hash $to) -ceq $row.sha256) 'LIGHTING_V4_PREIMAGE_COPY'}
 $fresh=Join-Path $out 'new-delivery'
 Run-Tool 'generate-plan' $core (Join-Path $repo 'tools/hc-device-runtime/New-LightingBuildDelivery.ps1') @('-ProjectRoot',$index.originalProjectRoot,'-HelperCandidateRoot',$index.originalHelperCandidate,'-HelperWorkspaceRoot',$index.originalHelperWorkspace,'-HelperSnapshotRoot',$script:fixture,'-NativeWorkspaceRoot',$index.originalNativeWorkspace,'-InstallRoot',$install,'-OutputRoot',$fresh,'-InventoryTemplatePath',(Join-Path $script:fixture 'legacy/deps/lightsetter-admission/candidate7b-v1/proofs/original-plan.json')) 'helper' 180
 $plan=Join-Path $fresh 'plan.json';$admission=Get-Content -LiteralPath (Join-Path $fresh 'admission.json') -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($admission.kind -ceq 'lighting-build-v4' -and $admission.nativeSha256 -ceq $contract.fixedTuple.native -and $admission.helperSha256 -ceq $contract.fixedTuple.helper) 'LIGHTING_V4_NEW_PLAN_TUPLE'
 $steps.Add(@{name='actual-new-plan';status='PASS';helperSha256=$admission.helperSha256;nativeSha256=$admission.nativeSha256})
 foreach($engine in @(@{name='ps7';path=$core},@{name='ps51';path=$win})){
  $testing=Join-Path $out ('tests-'+$engine.name)
  Run-Tool ('original-tests-'+$engine.name) $engine.path (Join-Path $repo 'tools/hc-device-runtime/Test-LightingBuildDelivery.ps1') @('-PlanPath',$plan,'-EnginePath',$engine.path,'-EvidenceRoot',$testing,'-LegacyV2PlanPath',(Join-Path $script:fixture 'legacy/deps/lightsetter-admission/candidate7b-v1/proofs/original-plan.json'),'-ReviewedV3ContractPath',(Join-Path $script:fixture 'legacy/deps/lightsetter-admission/candidate7b-v1/REVIEWED-CONTRACT.json')) 'legacy' 300
  $raw=Get-Content -LiteralPath (Join-Path $testing 'result.json') -Raw -Encoding UTF8|ConvertFrom-Json
  Need ($raw.status -ceq 'PASS' -and $raw.checks -eq 37 -and @($raw.cases|Where-Object passed -NE $true).Count -eq 0 -and @($raw.executions).Count -eq 6) 'LIGHTING_V4_ORIGINAL_TESTS'
  $locked=@($raw.executions|Where-Object case -CEQ 'locked-export')[0];$lockReport=Get-Content -LiteralPath $locked.receipt -Raw -Encoding UTF8|ConvertFrom-Json
  Need ($lockReport.rollbackPerformed -eq $true -and $lockReport.passed -eq $false) 'LIGHTING_V4_REAL_LOCK_ROLLBACK'
  $steps.Add(@{name=('original-tests-'+$engine.name);status='PASS';checks=37;actualConnectorProcesses=6;realSecondFileLockRollback=$true;result=@{path=(Join-Path $testing 'result.json');sha256=(Hash (Join-Path $testing 'result.json'))}})
 }
 Run-Tool 'final-connection' $core (Join-Path $repo 'tools/hc-device-runtime/connect-installed-lighting.ps1') @('-PlanPath',$plan) 'none' 120
 $connectionPath=Join-Path $fresh 'connection/verification.json';$connection=Get-Content -LiteralPath $connectionPath -Raw -Encoding UTF8|ConvertFrom-Json
 Need ($connection.status -ceq 'PASS' -and $connection.nativeAlreadyMatched -eq $true -and @($connection.changedFiles).Count -eq 2 -and $connection.preservedFiles -eq 103) 'LIGHTING_V4_FINAL_TRANSACTION'
 $steps.Add(@{name='final-private-connection';status='PASS';changedFiles=$connection.changedFiles;preservedFiles=103;result=@{path=$connectionPath;sha256=(Hash $connectionPath)}})
 $changes=@($sourceFiles|Where-Object {(Hash (Join-Path $repo $_)) -cne $before[$_]});Need ($changes.Count -eq 0) 'LIGHTING_V4_SOURCE_CHANGED'
 $result=@{schemaVersion=1;kind='ymcc-lighting-build-delivery-global-v1';status='PASS';startedUtc=$started;finishedUtc=[DateTime]::UtcNow.ToString('o');steps=@($steps);processes=@($processes);sourceHashes=$before;sourceChanges=$changes;fixture=@{contractSha256=(Hash $FixtureContractPath);zipSha256=(Hash $zip);members=$contract.zip.members};generatedPlan=@{path=$plan;sha256=(Hash $plan)};tuple=$contract.fixedTuple;originalChecksPerEngine=37;actualConnectorTestProcesses=12;finalConnectionProcess=$connection.processId;helperMainInvoked=$false;nativeApplicationExecuted=$false;actualCWritten=$false;actualGCanonicalWritten=$false;hardwareIo=$false;physicalAcceptance=$false;fullProductAcceptance=$false;goalComplete=$false}
 Save (Join-Path $out 'result.json') $result;Write-Output ('LIGHTING_V4_GLOBAL PASS 2x37 actual-connector=13')
}catch{
 Save (Join-Path $out 'result.json') @{schemaVersion=1;status='FAIL';error=$_.Exception.Message;scriptStack=$_.ScriptStackTrace;steps=@($steps);processes=@($processes);sourceHashes=$before;actualCWritten=$false;actualGCanonicalWritten=$false;nativeApplicationExecuted=$false;hardwareIo=$false;goalComplete=$false};throw
}
