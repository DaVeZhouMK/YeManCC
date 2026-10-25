[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$RuntimeRoot,
 [Parameter(Mandatory=$true)][string]$UpstreamRuntimeRoot,
 [Parameter(Mandatory=$true)][string]$KnownBadWasapi,
 [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Test evidence must be new'}
New-Item -ItemType Directory -Path $out | Out-Null
$manifest=Join-Path $RuntimeRoot 'HandheldCompanion.runtime.json'
$manifestHash=(Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()
$artifacts=Join-Path $out 'tool-build'
& dotnet build (Join-Path $PSScriptRoot 'ApplicationTrim.csproj') -c Release --artifacts-path $artifacts -v quiet
if($LASTEXITCODE -ne 0){throw 'Inspector build failed'}
$tool=Join-Path $artifacts 'bin\ApplicationTrim\release\ApplicationTrim.dll'
& dotnet $tool selftestcom (Join-Path $out 'synthetic')
if($LASTEXITCODE -ne 0){throw 'Synthetic ABI regression failed'}
$profilePath=Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'deps\ymcc-device-core-runtime\profile.json'
$repair=Get-Content -LiteralPath $profilePath -Raw -Encoding UTF8|ConvertFrom-Json
$replacementNames=@($repair.replacements|ForEach-Object {[string]$_.path})
$restored=@($repair.interopAbiRepair.restoredFiles|Where-Object {$_})
if( @($restored|Select-Object -Unique).Count -ne $restored.Count -or @($restored|Where-Object {$_ -notmatch '^[A-Za-z0-9.]+\.dll$'}).Count){throw 'Invalid restored-original ABI library contract'}
$names=@($restored|ForEach-Object {[IO.Path]::GetFileNameWithoutExtension($_)})
$checks=[ordered]@{synthetic=$true}
# Historical restorations do not override a subsequently declared replacement.
# Protected COM/callback verification and the known-bad audio controls remain independent.
foreach($name in $names){
 & dotnet $tool verifycom (Join-Path $UpstreamRuntimeRoot ($name+'.dll')) (Join-Path $RuntimeRoot ($name+'.dll')) (Join-Path $out ($name+'.json'))
 if($LASTEXITCODE -ne 0){throw "Current library ABI differs from reference: $name"}
 # Inventory identity comes from the current profile, not a forced upstream copy.
 $relative=$name+'.dll'
 $replacement=@($repair.replacements|Where-Object {$_.path -ceq $relative})
 $upstream=Get-Content -LiteralPath (Join-Path $UpstreamRuntimeRoot 'HandheldCompanion.runtime.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $entry=if($replacement.Count -eq 1){$replacement[0]}else{@($upstream.files|Where-Object {$_.path -ceq $relative})[0]}
 if(!$entry -or (Get-FileHash -LiteralPath (Join-Path $RuntimeRoot $relative) -Algorithm SHA256).Hash.ToLowerInvariant() -cne [string]$entry.sha256){throw "Profile-pinned library hash mismatch: $name"}
 $checks[$name]=$true
}
& dotnet $tool verifycom (Join-Path $UpstreamRuntimeRoot 'NAudio.Wasapi.dll') $KnownBadWasapi (Join-Path $out 'known-bad-wasapi.json')
if($LASTEXITCODE -eq 0){throw 'Known bad Wasapi was accepted'}
$checks.knownBadWasapiRejected=$true
$link=Join-Path $out 'linked-original';New-Item -ItemType Directory -Path $link | Out-Null
foreach($name in $names){Copy-Item -LiteralPath (Join-Path $RuntimeRoot ($name+'.dll')) -Destination $link}
$files=@(Get-ChildItem -LiteralPath $link -File | ForEach-Object {[ordered]@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})
$record=[ordered]@{mode='leaf';profile='current-restored-ABI-exact-copy';linkerExitCode=0;inputManifestSha256=$manifestHash;targetAssemblies=$names;files=$files}
$recordPath=Join-Path $out 'original-record.json';[IO.File]::WriteAllText($recordPath,($record|ConvertTo-Json -Depth 8))
& (Join-Path $PSScriptRoot 'create-overlay.ps1') -SnapshotRuntime $RuntimeRoot -CandidateRecord $recordPath -LinkedDirectory $link -OutputRuntime (Join-Path $out 'overlay-original') -OutputRecord (Join-Path $out 'overlay-original.json')
$checks.currentRuntimeExactCopyOverlayPassed=$true
# Forge a fresh valid-looking candidate pin around the real historical bad library.
# The overlay must recompute ABI policy, irrespective of record booleans and hashes.
$badLink=Join-Path $out 'linked-bad';New-Item -ItemType Directory -Path $badLink | Out-Null
Copy-Item -LiteralPath $KnownBadWasapi -Destination (Join-Path $badLink 'NAudio.Wasapi.dll')
$badRecord=[ordered]@{mode='leaf';profile='forged-positive';linkerExitCode=0;inputManifestSha256=$manifestHash;targetAssemblies=@('NAudio.Wasapi');files=@(@{name='NAudio.Wasapi.dll';sha256=(Get-FileHash -LiteralPath $KnownBadWasapi -Algorithm SHA256).Hash.ToLowerInvariant()});comVerificationPassed=$true}
$badRecordPath=Join-Path $out 'bad-record.json';[IO.File]::WriteAllText($badRecordPath,($badRecord|ConvertTo-Json -Depth 8))
$rejected=$false
try { & (Join-Path $PSScriptRoot 'create-overlay.ps1') -SnapshotRuntime $RuntimeRoot -CandidateRecord $badRecordPath -LinkedDirectory $badLink -OutputRuntime (Join-Path $out 'overlay-bad') -OutputRecord (Join-Path $out 'overlay-bad.json') }
catch {if($_.Exception.Message -notmatch 'Protected ABI contract changed: NAudio.Wasapi'){throw};$rejected=$true;[IO.File]::WriteAllText((Join-Path $out 'overlay-rejection.txt'),$_.Exception.Message)}
if(!$rejected -or (Test-Path -LiteralPath (Join-Path $out 'overlay-bad.json')) -or (Test-Path -LiteralPath (Join-Path $out 'overlay-bad'))){throw 'Bad overlay was accepted or published success/output'}
$checks.forgedCandidateRejectedBeforeOverlayPublication=$true
if((Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifestHash){throw 'Source runtime changed during test'}
$result=[ordered]@{status='passed';passed=$true;runtimeRoot=[IO.Path]::GetFullPath($RuntimeRoot);runtimeManifestSha256=$manifestHash;checks=$checks;reviewedLibraryCount=$names.Count;syntheticChecks=@((Get-Content -LiteralPath (Join-Path $out 'synthetic\result.json') -Raw | ConvertFrom-Json).checks.PSObject.Properties).Count;targetCodeExecuted=$false;hardwareAccess=$false;scope='Current runtime libraries match profile identity and actual reference ABI; changed bytes alone do not reject an audited derivative. Same-runtime overlay, structural mutations and real bad audio are checked; no hardware acceptance.'}
[IO.File]::WriteAllText((Join-Path $out 'result.json'),($result|ConvertTo-Json -Depth 8))
Write-Output "COM_GATE_REGRESSION: passed against current runtime manifest $manifestHash"
# Expected negative native exits must not escape after every assertion and result publication succeeds.
exit 0
