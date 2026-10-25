[CmdletBinding()]
param(
  [string]$ProjectRoot = '',
  [Parameter(Mandatory=$true)][string]$ScratchRoot,
  [Parameter(Mandatory=$true)][string]$BuiltInputHost,
  [Parameter(Mandatory=$true)][string]$BuiltBatteryHost
)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }
$productionRoot = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
$ScratchRoot = [IO.Path]::GetFullPath($ScratchRoot).TrimEnd('\')
if ((Test-Path -LiteralPath $ScratchRoot) -or $ScratchRoot.StartsWith($productionRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Tests require a new scratch directory outside product source.' }
New-Item -ItemType Directory -Path $ScratchRoot | Out-Null
. (Join-Path $PSScriptRoot 'shared-runtime.ps1')
. (Join-Path $productionRoot 'tools\hc-device-runtime\verify-runtime.ps1')
$script:checks = [Collections.Generic.List[object]]::new()
function Pass([string]$Name) { $script:checks.Add([pscustomobject]@{name=$Name;status='PASS'}) }
function Check([bool]$Value,[string]$Name) { if(-not $Value){throw $Name};Pass $Name }
function Reject([scriptblock]$Action,[string]$Message,[string]$Name) {
  $failure = ''
  try { & $Action | Out-Null } catch { $failure = $_.Exception.Message }
  if (-not $failure -or ($Message -and $failure -notlike ('*' + $Message + '*'))) { throw "Expected rejection failed: $Name ($failure)" }
  Pass $Name
}
function Change-File([string]$Path,[scriptblock]$Action,[bool]$Missing=$false) {
  $full = [IO.Path]::GetFullPath($Path)
  if (-not $full.StartsWith($ScratchRoot + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Mutation escaped scratch.' }
  $original = [IO.File]::ReadAllBytes($full)
  try {
    if($Missing){[IO.File]::Delete($full)}else{$changed=[byte[]]$original.Clone();$changed[$changed.Length-1]=$changed[$changed.Length-1] -bxor 1;[IO.File]::WriteAllBytes($full,$changed)}
    & $Action
  } finally { [IO.File]::WriteAllBytes($full,$original) }
}
function Parse-Source([string]$Relative) {
  $tokens=$null;$errors=$null
  $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $productionRoot $Relative),[ref]$tokens,[ref]$errors)
  if(@($errors).Count){throw "Parse failure: $Relative"}
  return $ast
}
function Function-Text($Ast,[string]$Name) {
  $nodes=@($Ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $Name},$true))
  if($nodes.Count -ne 1){throw "Ambiguous function: $Name"};return $nodes[0].Extent.Text
}
function Assignment-Text($Ast,[string]$Name) {
  $nodes=@($Ast.FindAll({param($n)$n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -ceq ('$'+$Name)},$true))
  if($nodes.Count -ne 1){throw "Ambiguous assignment: $Name"};return $nodes[0].Extent.Text
}
function Loop-Text($Ast,[string]$Marker) {
  $nodes=@($Ast.FindAll({param($n)$n -is [Management.Automation.Language.ForEachStatementAst] -and $n.Extent.Text.Contains($Marker)},$true))
  $nodes=@($nodes | Sort-Object { $_.Extent.Text.Length })
  if($nodes.Count -lt 1){throw "Missing loop: $Marker"};return $nodes[0].Extent.Text
}
$packageAst=Parse-Source 'tools\package-release.ps1'
$deployAst=Parse-Source 'tools\deploy-installed.ps1'
foreach($relative in @('tools\build-battery-host.ps1','tools\inputhost_shared_packaging_selftest.ps1','tools\ymcc-shared-runtime\shared-runtime.ps1')){[void](Parse-Source $relative);Pass ('parse '+$relative)}
foreach($name in @('Get-Sha256','Assert-HcSlimSourcePolicy','Assert-HcSlimStagedPolicy','Test-IsExcludedPowerControlPath')){Invoke-Expression (Function-Text $packageAst $name)}
foreach($name in @('hcRuntimeName','hcSlimSharedFallbackHashes','hcSlimSizeOrderRetainedHashes','hcSlimHidMaestroSha256','ymccHostFiles','sharedHostDlls','sharedRuntimeFallbackHashes','sharedRuntimeFallbackDlls')){Invoke-Expression (Assignment-Text $packageAst $name)}
$sourceFixture=Join-Path $ScratchRoot 'source'
New-Item -ItemType Directory -Path (Join-Path $sourceFixture 'deps'),(Join-Path $sourceFixture 'tools') | Out-Null
Copy-Item -LiteralPath (Join-Path $productionRoot 'deps\ymcc-shared-runtime') -Destination (Join-Path $sourceFixture 'deps\ymcc-shared-runtime') -Recurse
Copy-Item -LiteralPath (Join-Path $productionRoot 'tools\opt-baseline.json') -Destination (Join-Path $sourceFixture 'tools\opt-baseline.json')
$deviceDeps=Join-Path $sourceFixture 'deps\ymcc-device-core-runtime'
New-Item -ItemType Directory -Path $deviceDeps -Force | Out-Null
foreach($name in @('profile.json','upstream.runtime.json')){Copy-Item -LiteralPath (Join-Path $productionRoot ('deps\ymcc-device-core-runtime\'+$name)) -Destination (Join-Path $deviceDeps $name)}
# Require the actual approved closure before constructing an isolated source fixture.
$actualClosure=Assert-YmccDeviceCoreRuntime -ProjectRoot $productionRoot
Pass ('actual approved derivative closure '+$actualClosure.runtimeId)
$ProjectRoot=$sourceFixture
Assert-YmccSharedRuntimeSource $ProjectRoot;Pass 'independent source lock and all provenance verified'
$oldRuntime=Join-Path $sourceFixture ("PowerControl\handheldcompanion-runtime\"+$hcRuntimeName)
New-Item -ItemType Directory -Path $oldRuntime -Force | Out-Null
$realRuntime=Join-Path $productionRoot ('PowerControl\handheldcompanion-runtime\'+$hcRuntimeName)
$approvedManifest=Get-Content -LiteralPath (Join-Path $realRuntime 'HandheldCompanion.runtime.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach($entry in $approvedManifest.files){
  $destination=Assert-YmccSharedPath $oldRuntime ([string]$entry.path)
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
  Copy-Item -LiteralPath (Join-Path $realRuntime ([string]$entry.path)) -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $realRuntime 'HandheldCompanion.runtime.json') -Destination (Join-Path $oldRuntime 'HandheldCompanion.runtime.json')
$fixturePower=Join-Path $sourceFixture 'PowerControl'
Assert-HcSlimSourcePolicy $fixturePower;Pass 'actual source policy passes with exact approved derivative closure and independent shared authority'
Check (@(Get-ChildItem -LiteralPath $oldRuntime -File -Recurse).Count -eq (@($approvedManifest.files).Count + 1)) 'exact derivative file closure copied to source-policy fixture'
Change-File (Join-Path $sourceFixture 'deps\ymcc-shared-runtime\bin\Newtonsoft.Json.dll') {Reject {Assert-HcSlimSourcePolicy $fixturePower} 'Pinned shared DLL' 'actual source policy rejects tampered independent DLL'}
Change-File (Join-Path $sourceFixture 'deps\ymcc-shared-runtime\licenses\Newtonsoft.Json.txt') {Reject {Assert-YmccSharedRuntimeSource $ProjectRoot} 'license/provenance' 'source license tamper rejected'}
Change-File (Join-Path $oldRuntime 'HandheldCompanion.dll') {Reject {Assert-HcSlimSourcePolicy $fixturePower} 'Device-core unexpected, missing or changed file:' 'exact device-core gate rejects modified retained HC bytes'}
$unexpected=Join-Path $sourceFixture 'deps\ymcc-shared-runtime\unexpected.dll';[IO.File]::WriteAllText($unexpected,'unexpected')
try{Reject {Assert-YmccSharedRuntimeSource $ProjectRoot} 'inventory' 'unlisted source DLL rejected'}finally{[IO.File]::Delete($unexpected)}
Check (Test-IsExcludedPowerControlPath 'ymcc-shared-runtime\Newtonsoft.Json.dll' $false) 'package template cannot supply unverified shared DLL'
$stage=Join-Path $ScratchRoot 'stage'
$sharedRuntimeComplete=Join-Path $stage 'PowerControl\ymcc-shared-runtime'
# Invoke the exact staging call used by the production packager.
Invoke-Expression 'Copy-YmccSharedRuntime $ProjectRoot $sharedRuntimeComplete'
Assert-YmccSharedRuntimeStaged $ProjectRoot $sharedRuntimeComplete;Pass 'production shared stage call copies only locked small files'
Assert-YmccInputHostBuild $ProjectRoot $BuiltInputHost;Pass 'built InputHost support copies match independent authority'
$vgComplete=Join-Path $stage 'PowerControl\feature-assets\virtual-gamepad'
New-Item -ItemType Directory -Path $vgComplete -Force | Out-Null
foreach($file in Get-ChildItem -LiteralPath $BuiltInputHost -File | Where-Object {$_.Extension -ne '.pdb'}){Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $vgComplete $file.Name)}
$nativeInputHost=$BuiltInputHost
$copySupport=Loop-Text $packageAst 'Shared DLL missing from independent lock:'
Invoke-Expression $copySupport
$thin=Loop-Text $packageAst '$duplicate = Join-Path $vgComplete'
Invoke-Expression $thin
Check (@($sharedRuntimeFallbackDlls | Where-Object {Test-Path -LiteralPath (Join-Path $vgComplete $_)}).Count -eq 0) 'actual package thin loop removes exactly two local fallbacks'
foreach($name in $sharedHostDlls){Check (Test-Path -LiteralPath (Join-Path $vgComplete $name)) ('required InputHost local support retained '+$name)}
$battery=Join-Path $stage 'PowerControl\battery-host';New-Item -ItemType Directory -Path $battery | Out-Null
foreach($name in @('LibreHardwareMonitorLib.dll','RAMSPDToolkit-NDD.dll')){Copy-Item -LiteralPath (Join-Path $BuiltBatteryHost $name) -Destination (Join-Path $battery $name)}
Assert-YmccBatteryHostCopies $ProjectRoot $battery;Pass 'BatteryHost LHM and RAMSPD local copies preserved'
Change-File (Join-Path $battery 'LibreHardwareMonitorLib.dll') {Reject {Assert-YmccBatteryHostCopies $ProjectRoot $battery} 'BatteryHost local' 'BatteryHost local dependency tamper rejected'}
$packagedFallback=Loop-Text $packageAst 'Pinned shared fallback missing or changed:'
Invoke-Expression $packagedFallback;Pass 'actual package fallback hash gate passes independent location'
$hostDeps=Get-Content -LiteralPath (Join-Path $vgComplete 'YeManInputHost.deps.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$declaredRuntime=@()
foreach($target in @($hostDeps.targets.PSObject.Properties)){foreach($pkg in @($target.Value.PSObject.Properties)){if($null -ne $pkg.Value.runtime){$declaredRuntime+=@($pkg.Value.runtime.PSObject.Properties.Name)}}}
$declaredRuntime=@($declaredRuntime | Sort-Object -Unique)
$packageClosure=Loop-Text $packageAst 'Complete package YeManInputHost.deps.json runtime file is missing at its approved location:'
Invoke-Expression $packageClosure;Pass 'actual package deps.json closure passes'
Change-File (Join-Path $sharedRuntimeComplete 'Newtonsoft.Json.dll') {Reject {Invoke-Expression $packagedFallback} 'Pinned shared fallback' 'actual package gate rejects corrupt shared authority'}
Copy-Item -LiteralPath (Join-Path $sourceFixture 'deps\ymcc-shared-runtime\bin\Newtonsoft.Json.dll') -Destination (Join-Path $vgComplete 'Newtonsoft.Json.dll')
try{Reject {Invoke-Expression $packagedFallback} 'Unexpected local shared' 'actual package gate rejects thin-layout duplicates'}finally{[IO.File]::Delete((Join-Path $vgComplete 'Newtonsoft.Json.dll'))}
foreach($name in @('Get-Sha256','Test-InstalledPowerControlExcluded')){Invoke-Expression (Function-Text $deployAst $name)}
Check (Test-InstalledPowerControlExcluded 'ymcc-shared-runtime\Newtonsoft.Json.dll' $false) 'deploy template cannot supply unverified shared DLL'
Invoke-Expression (Assignment-Text $deployAst 'inputHostFiles')
$virtualGamepadAssets=$vgComplete;$installedSharedRuntime=$sharedRuntimeComplete;$NativeInputHost=$BuiltInputHost
$deployClosure=Loop-Text $deployAst 'Installed InputHost runtime closure missing:'
# The outer target loop is required; select the top-level production foreach.
$deployNodes=@($deployAst.EndBlock.Statements | Where-Object {$_ -is [Management.Automation.Language.ForEachStatementAst] -and $_.Extent.Text.Contains('Installed InputHost runtime closure missing:')})
if($deployNodes.Count -ne 1){throw 'Ambiguous deploy closure loop'};$deployClosure=$deployNodes[0].Extent.Text
Invoke-Expression $deployClosure;Pass 'actual deploy deps.json closure passes independent location'
Change-File (Join-Path $installedSharedRuntime 'Nefarius.Utilities.DeviceManagement.dll') {Reject {Invoke-Expression $deployClosure} 'Installed shared fallback hash' 'actual deploy gate rejects shared tamper'}
Change-File (Join-Path $installedSharedRuntime 'Newtonsoft.Json.dll') {Reject {Invoke-Expression $deployClosure} 'runtime closure missing' 'actual deploy closure rejects missing shared DLL even if HC is retained'} $true
$deployCopyNodes=@($deployAst.EndBlock.Statements | Where-Object {$_ -is [Management.Automation.Language.IfStatementAst] -and $_.Extent.Text.Contains('InputHost copy drift:')})
if($deployCopyNodes.Count -ne 1){throw 'Ambiguous deploy copy block'}
$nativeInputHostReady=$true;Invoke-Expression $deployCopyNodes[0].Extent.Text;Pass 'actual deploy support copy and thinning block passes'
$deployVerify=Loop-Text $deployAst 'Installed InputHost file differs from build:'
Invoke-Expression $deployVerify;Pass 'actual deploy local-support hash verification passes'
$copyBackup=Join-Path $ScratchRoot 'backup\PowerControl\ymcc-shared-runtime'
$corrupt=Join-Path $installedSharedRuntime 'Newtonsoft.Json.dll';$old=[IO.File]::ReadAllBytes($corrupt);$bad=[byte[]]$old.Clone();$bad[$bad.Length-1]=$bad[$bad.Length-1] -bxor 1;[IO.File]::WriteAllBytes($corrupt,$bad)
Copy-YmccSharedRuntime $ProjectRoot $installedSharedRuntime $copyBackup
Check ((Get-YmccSharedSha256 (Join-Path $copyBackup 'Newtonsoft.Json.dll')) -cne (Get-YmccSharedSha256 $corrupt)) 'shared deploy replaces pinned copy with backup of replaced bytes'
$extra=Join-Path $installedSharedRuntime 'unknown.dll';[IO.File]::WriteAllText($extra,'extra')
try{Reject {Copy-YmccSharedRuntime $ProjectRoot $installedSharedRuntime $copyBackup} 'unlisted files' 'shared deploy refuses unknown overlay files'}finally{[IO.File]::Delete($extra)}
& (Join-Path $productionRoot 'tools\inputhost_shared_packaging_selftest.ps1') -CandidateRoot $stage
Pass 'existing AST packaging selftest passes updated contract'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=Join-Path $ScratchRoot 'shared-runtime-fixture.zip';[IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip)
$zipFallback=Loop-Text $packageAst 'archive shared authority mismatch:'
function Check-Zip {
  $zipArchive=[IO.Compression.ZipFile]::OpenRead($zip)
  try{Assert-YmccSharedRuntimeArchive $ProjectRoot $zipArchive;Invoke-Expression $zipFallback}finally{$zipArchive.Dispose()}
}
foreach($name in @('Get-ZipEntrySha256')){Invoke-Expression (Function-Text $packageAst $name)}
Check-Zip;Pass 'actual archive shared-authority loop and complete shared/provenance gate pass'
function Set-ZipEntry([string]$Name,[byte[]]$Bytes,[bool]$Remove=$false) {
  $archive=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Update)
  try {
    $entries=@($archive.Entries | Where-Object { $_.FullName.Replace('\','/') -ceq $Name });foreach($entry in $entries){$entry.Delete()}
    if(-not $Remove){$entry=$archive.CreateEntry($Name);$stream=$entry.Open();try{$stream.Write($Bytes,0,$Bytes.Length)}finally{$stream.Dispose()}}
  }finally{$archive.Dispose()}
}
$zipName='PowerControl/ymcc-shared-runtime/Newtonsoft.Json.dll';$original=[IO.File]::ReadAllBytes((Join-Path $sharedRuntimeComplete 'Newtonsoft.Json.dll'))
$tampered=[byte[]]$original.Clone();$tampered[$tampered.Length-1]=$tampered[$tampered.Length-1] -bxor 1
Set-ZipEntry $zipName $tampered
Reject {Check-Zip} 'hash mismatch' 'archive shared DLL tamper rejected';Set-ZipEntry $zipName $original
Check-Zip
$licenseName='PowerControl/ymcc-shared-runtime/licenses/Newtonsoft.Json.txt';$licenseBytes=[IO.File]::ReadAllBytes((Join-Path $sharedRuntimeComplete 'licenses\Newtonsoft.Json.txt'))
Set-ZipEntry $licenseName @() $true;Reject {Check-Zip} 'inventory' 'archive missing license rejected';Set-ZipEntry $licenseName $licenseBytes
Check-Zip
$localName='PowerControl/feature-assets/virtual-gamepad/Newtonsoft.Json.dll';Set-ZipEntry $localName $original
Reject {Check-Zip} 'forbidden local duplicate' 'actual archive loop rejects local duplicate';Set-ZipEntry $localName @() $true
Check-Zip;Pass 'archive restored to valid independent layout'
# Preserve the old gate, and exercise its concrete pending patch without editing it.
$gateAst=Parse-Source 'tools\opt-inheritance-gate.ps1'
foreach($name in @('ConvertTo-HashtableDeep','Hash-File')){Invoke-Expression (Function-Text $gateAst $name)}
$Opt=ConvertTo-HashtableDeep (Get-Content -LiteralPath (Join-Path $productionRoot 'tools\opt-baseline.json') -Raw -Encoding UTF8 | ConvertFrom-Json)
$oldResolve=Function-Text $gateAst 'Resolve-Rule';Invoke-Expression $oldResolve
$stageDir=@{pc=$vgComplete;stage='package'}
$oldExpr='$full = Join-Path $power (''handheldcompanion-runtime\'' + [string]$routes[''runtimeId''] + ''\'' + $rel)'
$newExpr='if ([string]$routes[''directory''] -cne ''ymcc-shared-runtime'') { throw ''Shared sdk authority directory is invalid.'' }; $full = Join-Path (Join-Path $power ([string]$routes[''directory''])) $rel'
$inheritanceGatePatchPending=$oldResolve.Contains($oldExpr)
if($inheritanceGatePatchPending){
  $oldRows=@(Resolve-Rule $stageDir '@pc/Newtonsoft.Json.dll' 'sdk-dll')
  Check ($oldRows.Count -eq 1 -and $oldRows[0].size -eq -1) 'unmodified inheritance gate still needs explicit shared-directory patch'
  Invoke-Expression ($oldResolve.Replace($oldExpr,$newExpr))
}else{
  if(-not $oldResolve.Contains("[string]`$routes['directory']")){throw 'Unrecognized inheritance authority implementation'}
  Pass 'actual inheritance gate uses shared directory field'
}
$rows=@(Resolve-Rule $stageDir '@pc/Newtonsoft.Json.dll' 'sdk-dll')
Check ($rows.Count -eq 1 -and $rows[0].path -ceq (Join-Path $sharedRuntimeComplete 'Newtonsoft.Json.dll') -and $rows[0].sha256 -ceq $sharedRuntimeFallbackHashes['Newtonsoft.Json.dll']) 'actual or proposed inheritance route resolves exact independent hash'
Change-File (Join-Path $sharedRuntimeComplete 'Newtonsoft.Json.dll') {Reject {[void](Resolve-Rule $stageDir '@pc/Newtonsoft.Json.dll' 'sdk-dll')} 'Pinned shared sdk artifact hash' 'actual or proposed inheritance route preserves tamper gate'}
$sourceHashes=@{}
foreach($relative in @('tools/build-battery-host.ps1','tools/package-release.ps1','tools/deploy-installed.ps1','tools/opt-baseline.json','tools/opt-inheritance-gate.ps1','tools/inputhost_shared_packaging_selftest.ps1','tools/ymcc-shared-runtime/shared-runtime.ps1','tools/ymcc-shared-runtime/formal-integration-selftest.ps1','native/BatteryHost/runtime-contract.json','native/battery_host_contract.h','tools/hc-device-runtime/verify-runtime.ps1','deps/ymcc-device-core-runtime/profile.json','deps/ymcc-device-core-runtime/upstream.runtime.json')){$sourceHashes[$relative]=Get-YmccSharedSha256 (Join-Path $productionRoot $relative)}
$result=[ordered]@{schema=1;status='FORMAL_SHARED_RUNTIME_ISOLATED_CHECKS_PASS';actualDeviceCoreClosure=$actualClosure;selfTestSha256=(Get-YmccSharedSha256 $PSCommandPath);checks=$script:checks;sourceHashes=$sourceHashes;sourceRoot=$productionRoot;scratch=$ScratchRoot;archive=$zip;inheritanceGatePatchPending=$inheritanceGatePatchPending;productStarted=$false;deviceAccessed=$false;realDeployRun=$false;fullUiPackageBuilt=$false}
$result | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $ScratchRoot 'verification.json') -Encoding UTF8
Write-Output ("FORMAL_SHARED_RUNTIME_ISOLATED_PASS checks="+$script:checks.Count+" inheritance_gate_patch_pending="+$inheritanceGatePatchPending)
