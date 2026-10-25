function Test-YmccLightingPortableContract {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$PlanPath,[Parameter(Mandatory=$true)][string]$RepoRoot,[Parameter(Mandatory=$true)][string]$ScratchRoot,[Parameter(Mandatory=$true)][string]$EvidencePath)
 $ErrorActionPreference='Stop'
 $base=Microsoft.PowerShell.Management\Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8|ConvertFrom-Json
 $checks=[Collections.Generic.List[object]]::new();$blocked=[Collections.Generic.List[string]]::new()
 function Check([bool]$ok,[string]$name){$checks.Add([pscustomobject]@{name=$name;passed=$ok});if(-not $ok){throw ('PORTABLE_CONTROL_FAILED '+$name)}}
 function Hash([string]$p){(Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
 $original=Microsoft.PowerShell.Management\Get-Content -LiteralPath $base.lightingEvidence.originalPlan.path -Raw -Encoding UTF8|ConvertFrom-Json
 $forbidden=Split-Path -Parent $original.evidenceDir
 function Guard([string]$p){if($p -and [IO.Path]::GetFullPath($p).StartsWith($forbidden+'\',[StringComparison]::OrdinalIgnoreCase)){$blocked.Add($p);throw 'ORIGINAL_WORKSPACE_READ_BLOCKED'}}
 function Get-Content { [CmdletBinding()]param([string]$LiteralPath,[switch]$Raw,[string]$Encoding) Guard $LiteralPath;Microsoft.PowerShell.Management\Get-Content @PSBoundParameters }
 function Get-FileHash { [CmdletBinding()]param([string]$LiteralPath,[string]$Algorithm) Guard $LiteralPath;Microsoft.PowerShell.Utility\Get-FileHash @PSBoundParameters }
 foreach($command in @('content','hash')){$caught=$false;try{if($command -eq 'content'){Get-Content -LiteralPath (Join-Path $forbidden 'guard-control.txt') -Raw -Encoding UTF8|Out-Null}else{Get-FileHash -LiteralPath (Join-Path $forbidden 'guard-control.txt') -Algorithm SHA256|Out-Null}}catch{$caught=$_.Exception.Message.Contains('ORIGINAL_WORKSPACE_READ_BLOCKED')};Check $caught ('original workspace read guard '+$command)}
 $relocated=Join-Path $ScratchRoot 'relocated/deps';$source=Join-Path $relocated 'lightsetter-source/candidate7b';$evidence=Join-Path $relocated 'lightsetter-admission/candidate7b-v1'
 foreach($pair in @(@{from=$base.sourceBundleRoot;to=$source},@{from=$base.lightingEvidence.bundleRoot;to=$evidence})){
  New-Item -ItemType Directory -Path $pair.to -Force|Out-Null
  $root=[IO.Path]::GetFullPath($pair.from).TrimEnd('\')
  foreach($file in Get-ChildItem -LiteralPath $root -Recurse -File){$relative=$file.FullName.Substring($root.Length+1);$dest=Join-Path $pair.to $relative;New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force|Out-Null;Copy-Item -LiteralPath $file.FullName -Destination $dest}
 }
 $planFile=Join-Path $ScratchRoot 'relocated-plan.json'
 $p=New-YmccLightingReviewedPlan -ReviewedContractPath (Join-Path $evidence 'REVIEWED-CONTRACT.json') -SourceBundleRoot $source -RepoRoot $RepoRoot -OutputPlanPath $planFile -EvidenceRoot (Join-Path $ScratchRoot 'relocated-case')
 $answer=Assert-YmccLightingCatalogEvidence -Plan $p
 Check ($answer.status -eq 'PASS' -and $answer.contract -eq 'lighting-catalog-v3' -and $answer.originalAbsoluteSourceFilesRequired -eq $false) 'complete admission after relocating both approved bundles'
 function Clone {return $p|ConvertTo-Json -Depth 60|ConvertFrom-Json}
 function Reject([string]$name,[scriptblock]$change,[string]$code,[scriptblock]$restore){
  $q=Clone;$caught=$null
  try{& $change $q;try{Assert-YmccLightingCatalogEvidence -Plan $q|Out-Null}catch{$caught=$_.Exception.Message}}
  finally{if($restore){& $restore}}
  Check ($null -ne $caught -and $caught.Contains($code)) $name
 }
 Reject 'v3 plan cannot downgrade to historical v2' {param($q)$q.kind='lighting-catalog-v2'} 'V3_REQUIRED' $null
 Reject 'old helper is not a new reviewed artifact' {param($q)$q.newHelperSha256='e5460cfcfd1e73ad70b48a0bf8be70176c2115e2f1c655b9105f9e4de33dbadf'} 'REVIEWED_ARTIFACT' $null
 Reject 'native tuple cannot be replaced' {param($q)$q.preservedNativeSha256='0'*64} 'REVIEWED_ARTIFACT' $null
 Reject 'portable preimage root is mandatory' {param($q)$q.testPreimageRoot=Join-Path $ScratchRoot 'unreviewed-preimages'} 'PREIMAGE_ROOT' $null
 Reject 'original absolute receipt path is not opened' {param($q)$q.lightingEvidence.catalog.path=$original.lightingEvidence.catalog.path} 'PROOF_REFERENCE' $null
 Reject 'source relative escape is refused' {param($q)$q.changes[0].sourceRelative='../outside.dll'} 'CHANGED_BINDING' $null
 $contractFile=Join-Path $evidence 'REVIEWED-CONTRACT.json';$contractBefore=[IO.File]::ReadAllBytes($contractFile)
 Reject 'new contract hash cannot self-approve a profile' {param($q)$j=Get-Content -LiteralPath $contractFile -Raw -Encoding UTF8|ConvertFrom-Json;$j.profile='unreviewed-profile';[IO.File]::WriteAllText($contractFile,($j|ConvertTo-Json -Depth 60),[Text.UTF8Encoding]::new($false));$q.reviewedContract.sha256=Hash $contractFile} 'REVIEWED_CONTRACT' {[IO.File]::WriteAllBytes($contractFile,$contractBefore)}
 $sourceFile=Join-Path $source 'source/FanLab/LightSetter/OneXLightingSession.cs';$sourceBefore=[IO.File]::ReadAllBytes($sourceFile)
 Reject 'changed source bytes refuse admission' {param($q)[IO.File]::WriteAllBytes($sourceFile,($sourceBefore+[byte[]](10,47,47,32,120)))} 'BUNDLE_MEMBER_BYTES' {[IO.File]::WriteAllBytes($sourceFile,$sourceBefore)}
 $sourceManifest=Join-Path $source 'LIGHTSETTER-SOURCE.json';$sourceManifestBefore=[IO.File]::ReadAllBytes($sourceManifest)
 Reject 'reindexed source manifest cannot self-approve' {param($q)[IO.File]::WriteAllBytes($sourceManifest,($sourceManifestBefore+[byte[]](32)))} 'BUNDLE_MANIFEST' {[IO.File]::WriteAllBytes($sourceManifest,$sourceManifestBefore)}
 $extra=Join-Path $source 'nested/LIGHTSETTER-SOURCE.json'
 Reject 'nested unlisted manifest is not excluded by filename' {param($q)New-Item -ItemType Directory -Path (Split-Path -Parent $extra) -Force|Out-Null;[IO.File]::WriteAllText($extra,'{}')} 'BUNDLE_MEMBER_SET' {if(Test-Path -LiteralPath $extra){Remove-Item -LiteralPath $extra}}
 $oldFile=Join-Path $evidence 'fixture/before/PowerControl/light-setter/YeManLightSetter.dll';$oldBefore=[IO.File]::ReadAllBytes($oldFile)
 Reject 'true original helper bytes are immutable' {param($q)[IO.File]::WriteAllBytes($oldFile,($oldBefore+[byte[]](0)))} 'BUNDLE_MEMBER_BYTES' {[IO.File]::WriteAllBytes($oldFile,$oldBefore)}
 $profileFile=Join-Path $evidence 'profile/profile.json';$profileBefore=[IO.File]::ReadAllBytes($profileFile)
 Reject 'profile source is part of approved archive closure' {param($q)[IO.File]::WriteAllBytes($profileFile,($profileBefore+[byte[]](32)))} 'BUNDLE_MEMBER_BYTES' {[IO.File]::WriteAllBytes($profileFile,$profileBefore)}
 $admissionManifest=Join-Path $evidence 'ADMISSION-BUNDLE.json';$admissionBefore=[IO.File]::ReadAllBytes($admissionManifest)
 Reject 'reindexed admission evidence cannot self-approve' {param($q)[IO.File]::WriteAllBytes($admissionManifest,($admissionBefore+[byte[]](32)));$q.lightingEvidence.manifest.sha256=Hash $admissionManifest} 'BUNDLE_MANIFEST' {[IO.File]::WriteAllBytes($admissionManifest,$admissionBefore)}
 $moduleFile=Join-Path $evidence 'modules/catalog-maintained/HcLightingCapabilitySelfTest.dll';$moduleBefore=[IO.File]::ReadAllBytes($moduleFile)
 Reject 'executed replay module is hash bound' {param($q)[IO.File]::WriteAllBytes($moduleFile,($moduleBefore+[byte[]](0)))} 'BUNDLE_MEMBER_BYTES' {[IO.File]::WriteAllBytes($moduleFile,$moduleBefore)}
 $answer=Assert-YmccLightingCatalogEvidence -Plan $p
 Check ($answer.status -eq 'PASS') 'restored portable archives pass without original workspace'
 Check ($blocked.Count -eq 2) 'no original-workspace reads after guard controls'
 $result=[pscustomobject]@{status='PASS';passed=$true;checks=$checks.Count;cases=@($checks);originalWorkspaceBlocked=$forbidden;expectedGuardControls=2;blockedReadCount=$blocked.Count;oldWorkspaceRequired=$false;actualCWritten=$false;hardwareIo=$false;sourceBundle=$source;admissionBundle=$evidence}
 [IO.File]::WriteAllText($EvidencePath,($result|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false));return $result
}