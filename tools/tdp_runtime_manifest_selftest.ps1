[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[ValidateSet('none','package-omit','deploy-omit','package-asset-omit')][string]$Mutation='none')
$ErrorActionPreference='Stop'
$realProject=Split-Path -Parent $PSScriptRoot
$out=[IO.Path]::GetFullPath($EvidenceDir)
if($out -match '^[A-Za-z]:\\SOFT\\YeMan(?:\\|$)' -or $out -eq [IO.Path]::GetPathRoot($out)){throw 'Fixture output must be an owned evidence directory'}
New-Item -ItemType Directory -Path $out -Force|Out-Null
$checks=[Collections.Generic.List[object]]::new()
$sourcePins=[Collections.Generic.List[object]]::new()
function Add-Check([string]$Name,[bool]$Passed){$checks.Add([pscustomobject]@{name=$Name;passed=$Passed})}
function File-Sha([string]$Path){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Get-Block([string]$Text,[string]$Name){$a='# BEGIN '+$Name;$b='# END '+$Name;$start=$Text.IndexOf($a,[StringComparison]::Ordinal);$end=$Text.IndexOf($b,[StringComparison]::Ordinal);if($start -lt 0 -or $end -le $start){throw ('Missing actual consumer block '+$Name)};return $Text.Substring($start,$end+$b.Length-$start)}
$packagePath=Join-Path $realProject 'tools\package-release.ps1'
$deployPath=Join-Path $realProject 'tools\deploy-installed.ps1'
$libraryPath=Join-Path $realProject 'tools\lib\tdp-runtime-manifest.ps1'
foreach($p in @($packagePath,$deployPath,$libraryPath)){$sourcePins.Add([pscustomobject]@{path=$p;sha256=(File-Sha $p)})}
$packageText=Get-Content -LiteralPath $packagePath -Raw -Encoding UTF8
$deployText=Get-Content -LiteralPath $deployPath -Raw -Encoding UTF8
$tokens=$null;$parseErrors=$null
$deployAst=[Management.Automation.Language.Parser]::ParseInput($deployText,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Actual deployment script parse error'}
foreach($name in @('Get-FullPath','Get-Sha256','Copy-WithBackup','Copy-InstalledLockedPowerControlAssets')){
  $found=@($deployAst.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
  if($found.Count -ne 1){throw ('Expected one original deployment function '+$name)}
  Invoke-Expression $found[0].Extent.Text
}
$ProjectRoot=Join-Path $out 'project'
$WorkspaceRoot=Join-Path $out 'workspace'
$sourcePowerControl=Join-Path $ProjectRoot 'PowerControl'
$AssetsRoot=Join-Path $WorkspaceRoot 'Assets'
New-Item -ItemType Directory -Path (Join-Path $ProjectRoot 'tools\lib') -Force|Out-Null
Copy-Item -LiteralPath $libraryPath -Destination (Join-Path $ProjectRoot 'tools\lib\tdp-runtime-manifest.ps1')
$fullLock=Get-Content -LiteralPath (Join-Path $realProject 'tools\release-assets.lock.json') -Raw -Encoding UTF8|ConvertFrom-Json
$tdpLock=[pscustomobject]@{files=@($fullLock.files|Where-Object component -eq 'PawnIO')}
if($tdpLock.files.Count -ne 21){throw 'Original TDP asset count changed'}
$tdpLock|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $ProjectRoot 'tools\release-assets.lock.json') -Encoding UTF8
foreach($entry in $tdpLock.files){$target=Join-Path $ProjectRoot $entry.fallbackPath;New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force|Out-Null;Copy-Item -LiteralPath (Join-Path $realProject $entry.fallbackPath) -Destination $target}
$manifestSource=Join-Path $sourcePowerControl 'pawnio\tdp-runtime-manifest.json'
foreach($name in @('YeManTdpCtl.py','YeManTdpCtl.spec')){Copy-Item -LiteralPath (Join-Path $realProject ('PowerControl\pawnio\'+$name)) -Destination (Join-Path (Join-Path $sourcePowerControl 'pawnio') $name)}
$sourceFile=Join-Path $sourcePowerControl 'pawnio\YeManTdpCtl.py'
$recipeFile=Join-Path $sourcePowerControl 'pawnio\YeManTdpCtl.spec'
Copy-Item -LiteralPath (Join-Path $realProject 'PowerControl\pawnio\tdp-runtime-manifest.json') -Destination $manifestSource
$StagingPowerControl=Join-Path $out 'package\PowerControl'
$assetLock=$tdpLock;$useWorkspaceAssets=$false;$assetSources=@{}
$loopStart=$packageText.IndexOf('foreach ($entry in $assetLock.files)',[StringComparison]::Ordinal)
$loopEnd=$packageText.IndexOf('# BEGIN TDP_RUNTIME_MANIFEST_PACKAGE',[StringComparison]::Ordinal)
if($loopStart -lt 0 -or $loopEnd -le $loopStart){throw 'Original package asset loop not found'}
$packageLoop=$packageText.Substring($loopStart,$loopEnd-$loopStart)
$packageBlock=Get-Block $packageText 'TDP_RUNTIME_MANIFEST_PACKAGE'
$packageCopy="Copy-Item -LiteralPath `$tdpRuntimeManifestPlan.ManifestPath -Destination (Join-Path `$StagingPowerControl 'pawnio\tdp-runtime-manifest.json') -Force"
if($Mutation -eq 'package-omit'){if(-not $packageBlock.Contains($packageCopy)){throw 'Package fault anchor changed'};$packageBlock=$packageBlock.Replace($packageCopy,'# original missing sidecar copy fault')}
$omittedPackageAsset=$null
if($Mutation -eq 'package-asset-omit'){
  $loopHeader='foreach ($entry in $assetLock.files) {'
  if(([regex]::Matches($packageLoop,[regex]::Escape($loopHeader))).Count -ne 1){throw 'Package asset fault anchor changed'}
  $omittedPackageAsset='pawnio/IntelMSR.bin'
  if(@($tdpLock.files|Where-Object {$_.releasePath.Replace('\','/') -ieq $omittedPackageAsset}).Count -ne 1){throw 'Package asset fault target changed'}
  $skipAsset="`n  if (`$entry.releasePath.Replace('\','/') -ieq 'pawnio/IntelMSR.bin') { continue }"
  $packageLoop=$packageLoop.Replace($loopHeader,$loopHeader+$skipAsset)
}
Invoke-Expression ($packageLoop+$packageBlock)
$packagedManifest=Join-Path $StagingPowerControl 'pawnio\tdp-runtime-manifest.json'
$packageManifestMatches=(Test-Path -LiteralPath $packagedManifest -PathType Leaf) -and (File-Sha $packagedManifest) -eq (File-Sha $manifestSource)
$packageAssetVerification=@(foreach($entry in $tdpLock.files){
  $target=Join-Path $StagingPowerControl $entry.releasePath
  $exists=Test-Path -LiteralPath $target -PathType Leaf
  $actualSha=if($exists){File-Sha $target}else{$null}
  [pscustomobject]@{releasePath=$entry.releasePath;destination=$target;expectedSha256=$entry.sha256;exists=$exists;actualSha256=$actualSha;verified=($exists -and $actualSha -ieq $entry.sha256)}
})
$packagedBinaryAssets=@($packageAssetVerification|Where-Object verified).Count
$packageMatches=$packageManifestMatches -and $packagedBinaryAssets -eq $tdpLock.files.Count
Add-Check 'actual-package-loop-and-sidecar-consumer-copy-all21-plus-manifest' $packageMatches
$deployedBinaryAssets=$null
# This mutation targets only the original package assertion; default and old mutation scopes retain all ten checks.
if($Mutation -ne 'package-asset-omit'){
$prefight=Get-Block $deployText 'TDP_RUNTIME_MANIFEST_DEPLOY_PREFLIGHT'
Invoke-Expression $prefight
$installedPowerControl=Join-Path $out 'installed\PowerControl'
$BackupRoot=Join-Path $out 'installed\backups\epoch'
$installedManifest=Join-Path $installedPowerControl 'pawnio\tdp-runtime-manifest.json'
New-Item -ItemType Directory -Path (Split-Path -Parent $installedManifest) -Force|Out-Null
[IO.File]::WriteAllText($installedManifest,'old-fixture-metadata',[Text.UTF8Encoding]::new($false))
$oldManifestHash=File-Sha $installedManifest
Copy-InstalledLockedPowerControlAssets $ProjectRoot $WorkspaceRoot $installedPowerControl $BackupRoot|Out-Null
$copy=Get-Block $deployText 'TDP_RUNTIME_MANIFEST_DEPLOY_COPY'
$copyLine="Copy-WithBackup `$tdpRuntimeManifestPlan.ManifestPath (Join-Path `$installedPowerControl 'pawnio\tdp-runtime-manifest.json') `$BackupRoot"
if($Mutation -eq 'deploy-omit'){if(-not $copy.Contains($copyLine)){throw 'Deploy fault anchor changed'};$copy=$copy.Replace($copyLine,'# original missing installed sidecar copy fault')}
Invoke-Expression $copy
$backupManifest=Join-Path $BackupRoot 'PowerControl\pawnio\tdp-runtime-manifest.json'
$deployMatches=(File-Sha $installedManifest) -eq (File-Sha $manifestSource) -and (Test-Path -LiteralPath $backupManifest) -and (File-Sha $backupManifest) -eq $oldManifestHash
$deployedBinaryAssets=0
foreach($entry in $tdpLock.files){if((File-Sha (Join-Path $installedPowerControl $entry.releasePath)) -ieq $entry.sha256){$deployedBinaryAssets++}else{$deployMatches=$false}}
Add-Check 'actual-deploy-preflight-copy-all21-and-sidecar-preserves-prior-metadata-backup' $deployMatches
$raw=Get-Content -LiteralPath $manifestSource -Raw -Encoding UTF8
foreach($case in @('wrong-helper','wrong-library','future-schema','path-traversal','wrong-source','wrong-recipe')){
 $value=$raw|ConvertFrom-Json
 switch($case){'wrong-source'{$value.source.sha256=('0'*64)}'wrong-recipe'{$value.recipe.sha256=('0'*64)}'wrong-helper'{$value.helperSha256=('0'*64)}'wrong-library'{$value.files[1].sha256=('0'*64)}'future-schema'{$value.schemaVersion=2}'path-traversal'{$value.files[0].name='../outside.exe'}}
 $bad=Join-Path $out ($case+'.json');$value|ConvertTo-Json -Depth 40|Set-Content -LiteralPath $bad -Encoding UTF8
 $rejected=$false;try{Assert-YmccTdpRuntimeManifestPayload $bad (Join-Path $sourcePowerControl 'pawnio') $sourceFile $recipeFile|Out-Null}catch{if($_.Exception.Message -notmatch '^TDP_RUNTIME_MANIFEST_'){throw};$rejected=$true}
 Add-Check ('actual-preflight-rejects-'+$case) $rejected
}
$rejected=$false;try{Assert-YmccTdpRuntimeManifestPayload (Join-Path $out 'missing.json') (Join-Path $sourcePowerControl 'pawnio') $sourceFile $recipeFile|Out-Null}catch{if($_.Exception.Message -notmatch '^TDP_RUNTIME_MANIFEST_'){throw};$rejected=$true};Add-Check 'actual-preflight-rejects-missing-sidecar' $rejected
$missingPayload=Join-Path $out 'missing-payload';New-Item -ItemType Directory -Path (Join-Path $missingPayload '_internal') -Force|Out-Null
foreach($name in @('YeManTdpCtl.exe','_internal/base_library.zip')){Copy-Item -LiteralPath (Join-Path (Join-Path $sourcePowerControl 'pawnio') $name) -Destination (Join-Path $missingPayload $name)}
$rejected=$false;try{Assert-YmccTdpRuntimeManifestPayload $manifestSource $missingPayload $sourceFile $recipeFile|Out-Null}catch{if($_.Exception.Message -notmatch '^TDP_RUNTIME_MANIFEST_'){throw};$rejected=$true};Add-Check 'actual-preflight-rejects-missing-python-library' $rejected
}
$allPassed=@($checks|Where-Object {-not $_.passed}).Count -eq 0
$result=[ordered]@{status=if($allPassed){'PASS'}else{'FAIL'};allPassed=$allPassed;checks=$checks.ToArray();mutation=$Mutation;sourcePins=$sourcePins.ToArray();exactOriginalFunctionsExecuted=if($Mutation -eq 'package-asset-omit'){1}else{4};copiedBinaryAssets=$packagedBinaryAssets;copiedBinaryAssetsScope='verified package destinations from original lock';expectedBinaryAssets=$tdpLock.files.Count;packagedBinaryAssets=$packagedBinaryAssets;deployedBinaryAssets=$deployedBinaryAssets;packageManifestMatches=$packageManifestMatches;packageAssetVerification=$packageAssetVerification;omittedPackageAsset=$omittedPackageAsset;checkScope=if($Mutation -eq 'package-asset-omit'){'targeted-original-package-check'}else{'original-ten-checks'};noBinaryOrDriverExecuted=$true;noInstalledDomainAccessed=$true;fixtureOutput=$out}
$result|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $out 'RESULT.json') -Encoding UTF8
$result|ConvertTo-Json -Depth 15 -Compress
if(-not $allPassed){exit 1}
