[CmdletBinding()]
param([string]$ProjectRoot='', [Parameter(Mandatory=$true)][string]$ScratchRoot)
if([string]::IsNullOrWhiteSpace($ProjectRoot)){
 $scriptFolder=if($PSScriptRoot){$PSScriptRoot}else{Split-Path -Parent $MyInvocation.MyCommand.Path}
 $ProjectRoot=Split-Path -Parent $scriptFolder
}
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
if(Test-Path -LiteralPath $ScratchRoot){throw 'Require new scratch evidence'}
New-Item -ItemType Directory -Path $ScratchRoot|Out-Null
. (Join-Path $ProjectRoot 'tools/hc-export-baseline-guard.ps1')
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
function Json([string]$Path,$Value){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Text([string]$Path,[string]$Value){[IO.File]::WriteAllText($Path,$Value,[Text.UTF8Encoding]::new($false))}
function Row([string]$Path,[string]$Relative){[pscustomobject]@{path=$Relative;bytes=(Get-Item -LiteralPath $Path).Length;sha256=(Hash $Path)}}
$baseline=Join-Path $ScratchRoot 'baseline';$candidate=Join-Path $ScratchRoot 'candidate'
New-Item -ItemType Directory -Path $baseline,$candidate|Out-Null
$dllName='WindowsInput.dll';$manifestName='HandheldCompanion.runtime.json'
$dll=Join-Path $baseline $dllName;Text $dll 'compact identity fixture; never executed'
$profilePath=Join-Path $ScratchRoot 'profile.json'
Json $profilePath @{schemaVersion=1;runtimeId='HC-COMPACT-FIXTURE';replacements=@((Row $dll $dllName))}
$profileHash=Hash $profilePath
$mf=Join-Path $baseline $manifestName
Json $mf @{schemaVersion=1;runtimeId='HC-COMPACT-FIXTURE';deviceCoreProfileSha256=$profileHash;files=@(@{path=$dllName;sha256=(Hash $dll)})}
$manifestHash=Hash $mf
$inventoryPath=Join-Path $ScratchRoot 'inventory.json'
Json $inventoryPath @{schemaVersion=1;files=@((Row $dll $dllName),(Row $mf $manifestName))}
$params=@{BaselineProfilePath=$profilePath;BaselineProfileSha256=$profileHash;BaselineManifestPath=$mf;BaselineManifestSha256=$manifestHash;BaselineInventoryPath=$inventoryPath;BaselineInventorySha256=(Hash $inventoryPath)}
Copy-Item -LiteralPath $dll,$mf -Destination $candidate
$checks=[Collections.Generic.List[object]]::new()
function Accept([string]$Name,[scriptblock]$Run){$r=&$Run;if($r.status -ne 'PASS'){throw ('No PASS: '+$Name)};$checks.Add([pscustomobject]@{name=$Name;status='PASS';expected='accept'})}
function Reject([string]$Name,[scriptblock]$Run,[string]$Code){$failed=$false;try{&$Run|Out-Null}catch{if($_.Exception.Message -notlike ('*HC_EXPORT_BASELINE_'+$Code+'*')){throw};$failed=$true};if(-not $failed){throw ('Unexpected acceptance: '+$Name)};$checks.Add([pscustomobject]@{name=$Name;status='PASS';expected='reject';code=$Code})}
function Zip([string]$Path,$Files,[bool]$Duplicate=$false){
 $z=[IO.Compression.ZipFile]::Open($Path,[IO.Compression.ZipArchiveMode]::Create)
 try{foreach($file in $Files){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z,(Join-Path $candidate $file),('PowerControl/handheldcompanion-runtime/HC-COMPACT-FIXTURE/'+$file))|Out-Null};if($Duplicate){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($z,(Join-Path $candidate $dllName),('PowerControl/handheldcompanion-runtime/HC-COMPACT-FIXTURE/'+$dllName))|Out-Null}}finally{$z.Dispose()}
}
Accept 'Explicit pinned compact root accepted' {Assert-YmccHcExportBaseline @params -RuntimeRoot $candidate}
$zip=Join-Path $ScratchRoot 'valid.zip';Zip $zip @($dllName,$manifestName)
Accept 'Actual ZIP member bytes and source root accepted' {Assert-YmccHcExportBaseline @params -RuntimeRoot $candidate -ArchivePath $zip}
$bytes=[IO.File]::ReadAllBytes((Join-Path $candidate $dllName))
Text (Join-Path $candidate $dllName) ('expanded original '+('x'*200))
Reject 'Unreviewed larger DLL is rejected' {Assert-YmccHcExportBaseline @params -RuntimeRoot $candidate} 'CANDIDATE_DIFF'
# A self-consistent changed manifest/inventory does not redefine the explicitly pinned baseline.
$newProfile=Join-Path $ScratchRoot 'expanded-profile.json';Json $newProfile @{schemaVersion=1;runtimeId='HC-COMPACT-FIXTURE';replacements=@((Row (Join-Path $candidate $dllName) $dllName))}
$bad=@{};foreach($k in $params.Keys){$bad[$k]=$params[$k]};$bad.BaselineProfilePath=$newProfile
Reject 'Self-consistent expanded profile cannot replace accepted baseline' {Assert-YmccHcExportBaseline @bad -RuntimeRoot $candidate} 'PROFILE_HASH'
[IO.File]::WriteAllBytes((Join-Path $candidate $dllName),$bytes)
$changed=[byte[]]$bytes.Clone();$changed[0]=$changed[0] -bxor 1
[IO.File]::WriteAllBytes((Join-Path $candidate $dllName),$changed)
$badZip=Join-Path $ScratchRoot 'changed-same-length.zip';Zip $badZip @($dllName,$manifestName)
Reject 'ZIP same path and size but different SHA is rejected' {Assert-YmccHcExportBaseline @params -ArchivePath $badZip} 'CANDIDATE_DIFF'
[IO.File]::WriteAllBytes((Join-Path $candidate $dllName),$bytes)
$missingZip=Join-Path $ScratchRoot 'missing.zip';Zip $missingZip @($manifestName)
Reject 'ZIP missing baseline library is rejected' {Assert-YmccHcExportBaseline @params -ArchivePath $missingZip} 'CANDIDATE_DIFF'
$duplicateZip=Join-Path $ScratchRoot 'duplicate.zip';Zip $duplicateZip @($dllName,$manifestName) $true
Reject 'Duplicate ZIP path is rejected' {Assert-YmccHcExportBaseline @params -ArchivePath $duplicateZip} 'ZIP_DUPLICATE'
$extra=Join-Path $candidate 'retired.dll';Text $extra 'unreviewed extra'
Reject 'Reintroduced unlisted file is rejected' {Assert-YmccHcExportBaseline @params -RuntimeRoot $candidate} 'CANDIDATE_DIFF'
[IO.File]::Delete($extra)
# An explicitly hash-pinned review can authorize specific new bytes, including a repair that is larger.
$newDll=Join-Path $candidate $dllName;Text $newDll 'larger but explicitly reviewed compatibility repair; identity-only fixture'
$newManifest=Join-Path $candidate $manifestName
Json $newManifest @{schemaVersion=1;runtimeId='HC-COMPACT-FIXTURE';deviceCoreProfileSha256=$profileHash;files=@(@{path=$dllName;sha256=(Hash $newDll)})}
$review=Join-Path $ScratchRoot 'review.json'
$reviewData=@{schemaVersion=1;baselineProfileSha256=$profileHash;baselineManifestSha256=$manifestHash;baselineInventorySha256=$params.BaselineInventorySha256;changes=@(@{path=$dllName;before=(Row $dll $dllName);after=(Row $newDll $dllName);reason='Fixture explicitly reviewed repair'},@{path=$manifestName;before=(Row $mf $manifestName);after=(Row $newManifest $manifestName);reason='Rebind exact runtime manifest'})}
Json $review $reviewData
Accept 'Explicit fixed-source reviewed repair is allowed regardless of size' {Assert-YmccHcExportBaseline @params -RuntimeRoot $candidate -ApprovedChangesPath $review -ApprovedChangesSha256 (Hash $review)}
$reviewData.changes[0].before.sha256=('0'*64);Json $review $reviewData
Reject 'Review with wrong baseline preimage is rejected' {Assert-YmccHcExportBaseline @params -RuntimeRoot $candidate -ApprovedChangesPath $review -ApprovedChangesSha256 (Hash $review)} 'REVIEW_BEFORE'
Reject 'Review cannot be supplied without exact source hash' {Assert-YmccHcExportBaseline @params -RuntimeRoot $candidate -ApprovedChangesPath $review} 'REVIEW_PIN_PAIR'
$badPin=@{};foreach($k in $params.Keys){$badPin[$k]=$params[$k]};$badPin.BaselineManifestSha256=('0'*64)
Reject 'Manifest hash binding is mandatory' {Assert-YmccHcExportBaseline @badPin -RuntimeRoot $candidate} 'MANIFEST_HASH'
$result=@{status='PASS';checks=$checks.ToArray();count=$checks.Count;targetCodeExecuted=$false;hardwareAccess=$false;installedDeploymentPerformed=$false}
Json (Join-Path $ScratchRoot 'RESULT.json') $result
Write-Output ('HC_EXPORT_BASELINE_SELFTEST_OK checks='+$checks.Count)