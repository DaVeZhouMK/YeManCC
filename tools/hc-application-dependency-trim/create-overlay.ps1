[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$SnapshotRuntime,
 [Parameter(Mandatory=$true)][string]$CandidateRecord,
 [Parameter(Mandatory=$true)][string]$LinkedDirectory,
 [Parameter(Mandatory=$true)][string]$OutputRuntime,
 [Parameter(Mandatory=$true)][string]$OutputRecord
)
$ErrorActionPreference='Stop'
$source=[IO.Path]::GetFullPath($SnapshotRuntime).TrimEnd('\')
$link=[IO.Path]::GetFullPath($LinkedDirectory).TrimEnd('\')
$out=[IO.Path]::GetFullPath($OutputRuntime).TrimEnd('\')
if(Test-Path -LiteralPath $out){throw 'Output must be a new directory; existing evidence is never replaced'}
foreach($p in @($source,$link)){if($out.Equals($p,[StringComparison]::OrdinalIgnoreCase) -or $out.StartsWith($p+'\',[StringComparison]::OrdinalIgnoreCase) -or $p.StartsWith($out+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output overlaps an input directory'}}
$c=Get-Content -LiteralPath $CandidateRecord -Raw -Encoding UTF8|ConvertFrom-Json
if($c.mode -ne 'leaf' -or $c.linkerExitCode -ne 0){throw 'Only a successful leaf record is accepted'}
$manifest=Get-Content -LiteralPath (Join-Path $source 'HandheldCompanion.runtime.json') -Raw -Encoding UTF8|ConvertFrom-Json
$expected=@{};foreach($f in $manifest.files){$expected[$f.path]=$f.sha256}
$manifestHash=(Get-FileHash -LiteralPath (Join-Path $source 'HandheldCompanion.runtime.json') -Algorithm SHA256).Hash.ToLowerInvariant()
if($manifestHash -ne $c.inputManifestSha256){throw 'Snapshot identity differs from candidate input'}
foreach($f in $manifest.files){if((Get-FileHash -LiteralPath (Join-Path $source $f.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $f.sha256){throw "Snapshot changed: $($f.path)"}}
# Recompute the gate from the full original snapshot; candidate JSON is not authority.
# Build the inspector in the evidence directory, without loading inspected assemblies.
$gate=[IO.Path]::GetFullPath($OutputRecord)+'.com-gate'
if(Test-Path -LiteralPath $gate){throw 'COM gate evidence must be new'}
foreach($inputPath in @($source,$link,$out)){if($gate.Equals($inputPath,[StringComparison]::OrdinalIgnoreCase) -or $gate.StartsWith($inputPath+'\',[StringComparison]::OrdinalIgnoreCase) -or $inputPath.StartsWith($gate+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'COM gate evidence overlaps payload/input'}}
New-Item -ItemType Directory -Path $gate | Out-Null
$artifacts=Join-Path $gate 'tool-build'
& dotnet build (Join-Path $PSScriptRoot 'ApplicationTrim.csproj') -c Release --artifacts-path $artifacts -v quiet
if($LASTEXITCODE -ne 0){throw 'COM inspector build failed'}
$tool=Join-Path $artifacts 'bin\ApplicationTrim\release\ApplicationTrim.dll'
$comAudit=Join-Path $gate 'audit.json'
& dotnet $tool auditcom $source $comAudit
if($LASTEXITCODE -ne 0){throw 'Original COM inventory failed'}
$com=Get-Content -LiteralPath $comAudit -Raw -Encoding UTF8 | ConvertFrom-Json
$protected=@($com.protectedAssemblies | ForEach-Object {$_.name})
foreach($name in $c.targetAssemblies){
 if($name -ne [IO.Path]::GetFileName($name) -or $name.Contains(':')){throw 'Invalid target simple name'}
 if($name -notin $protected){continue}
 $original=@($com.protectedAssemblies | Where-Object name -eq $name)[0]
 $after=Join-Path $link ($name+'.dll')
 if(!(Test-Path -LiteralPath $after)){throw "Protected ABI assembly missing: $name"}
 & dotnet $tool verifycom (Join-Path $source ($name+'.dll')) $after (Join-Path $gate ($name+'.pre-overlay.json'))
 if($LASTEXITCODE -ne 0){throw "Protected ABI contract changed: $name"}
}
New-Item -ItemType Directory -Path $out | Out-Null
foreach($f in Get-ChildItem -LiteralPath $source -File -Recurse){$rel=[IO.Path]::GetRelativePath($source,$f.FullName);$p=Join-Path $out $rel;New-Item -ItemType Directory -Path (Split-Path -Parent $p) -Force|Out-Null;Copy-Item -LiteralPath $f.FullName -Destination $p}
$rows=@()
foreach($name in $c.targetAssemblies){
 if($name -ne [IO.Path]::GetFileName($name) -or $name.Contains(':')){throw 'Invalid target simple name'}
 $filename=$name+'.dll';$before=Join-Path $source $filename;$after=Join-Path $link $filename;$destination=Join-Path $out $filename
 if(!$expected.ContainsKey($filename)){throw "Target outside snapshot manifest: $filename"}
 $beforeHash=(Get-FileHash -LiteralPath $before -Algorithm SHA256).Hash.ToLowerInvariant();$afterHash=$null;$afterBytes=0
 if(Test-Path -LiteralPath $after){
  $pinned=@($c.files|Where-Object name -eq $filename);if($pinned.Count -ne 1){throw 'Candidate pin missing'}
  $afterHash=(Get-FileHash -LiteralPath $after -Algorithm SHA256).Hash.ToLowerInvariant();if($afterHash -ne $pinned[0].sha256){throw 'Candidate changed after link'}
  Copy-Item -LiteralPath $after -Destination $destination -Force;$afterBytes=(Get-Item -LiteralPath $after).Length
 }else{Remove-Item -LiteralPath $destination}
 $rows += [ordered]@{path=$filename;beforeBytes=(Get-Item -LiteralPath $before).Length;afterBytes=$afterBytes;beforeSha256=$beforeHash;afterSha256=$afterHash;action=if($afterBytes -eq 0){'remove-candidate'}else{'replace-candidate'}}
}
$comVerify=Join-Path $gate 'overlay-verify.json'
& dotnet $tool verifycom $source $out $comVerify
if($LASTEXITCODE -ne 0){throw 'Overlay COM/interop compatibility verification failed; no success record emitted'}
$total=0;foreach($r in $rows){$total+=$r.beforeBytes-$r.afterBytes}
$record=[ordered]@{schemaVersion=1;status='candidate-not-promoted';inputManifestSha256=$manifestHash;mode=$c.mode;profile=$c.profile;grossCandidateSaving=$total;rows=$rows;allOtherSnapshotFilesPreserved=$true;onlyTheseFilesEligibleForPromotion=$true;productionModified=$false;standaloneHandheldPackage=$false;comVerificationPassed=$true;comProtectedAssemblies=$protected;comVerificationSha256=(Get-FileHash -LiteralPath $comVerify -Algorithm SHA256).Hash.ToLowerInvariant();comExternalDependencies=@($com.unavailableDependencies)}
[IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputRecord),($record|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
$targetsFile=[IO.Path]::ChangeExtension([IO.Path]::GetFullPath($OutputRecord),'targets.json')
[IO.File]::WriteAllText($targetsFile,(@($c.targetAssemblies)|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
Write-Output "CANDIDATE_OVERLAY: $($rows.Count) targets; $total bytes gross saving"
