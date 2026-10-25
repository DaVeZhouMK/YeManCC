[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$FunctionsPath,[Parameter(Mandatory=$true)][string]$ScratchRoot)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $ScratchRoot){throw 'Use a new scratch directory.'}
$functions=[IO.File]::ReadAllText($FunctionsPath)
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseInput($functions,[ref]$tokens,[ref]$errors)
if(@($errors).Count -or @($ast.EndBlock.Statements).Count -ne 2 -or @($ast.EndBlock.Statements|Where-Object {$_ -isnot [Management.Automation.Language.FunctionDefinitionAst]}).Count){throw 'Only the two actual validator function definitions may execute.'}
Invoke-Expression $functions
$standalonePath = Join-Path $ProjectRoot 'PowerControl\fan-host\install-fan-host-payload.ps1'
$standaloneTokens=$null;$standaloneErrors=$null
$standaloneAst=[Management.Automation.Language.Parser]::ParseFile($standalonePath,[ref]$standaloneTokens,[ref]$standaloneErrors)
if(@($standaloneErrors).Count){throw 'Standalone installer parse failed.'}
foreach($name in @('Get-Sha256','Assert-DeviceSharedFiles')) {
  $nodes=@($standaloneAst.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true))
  if($nodes.Count -ne 1){throw 'Standalone shared validator missing or ambiguous.'}
  Invoke-Expression $nodes[0].Extent.Text
}
New-Item -ItemType Directory -Path $ScratchRoot|Out-Null
$checks=[Collections.Generic.List[string]]::new()
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
function Set-Fixture([string]$Root,[string]$RuntimeSource){
  $script:staging=$Root
  $script:fanHostSource=Join-Path $Root 'PowerControl\fan-host-v2'
  $script:runtimeDir='handheldcompanion-runtime\YMCC-DEVICECORE-20261018-v1'
  $script:core=Join-Path (Join-Path $Root 'PowerControl') $script:runtimeDir
  New-Item -ItemType Directory -Path $script:fanHostSource,(Split-Path -Parent $script:core),(Join-Path $Root 'YeManCC') -Force|Out-Null
  Copy-Item -LiteralPath $RuntimeSource -Destination $script:core -Recurse
  $payload=Join-Path $ProjectRoot 'PowerControl\fan-host'
  $hostManifest=Get-Content -LiteralPath (Join-Path $payload 'YeManFanHost.payload.json') -Raw -Encoding UTF8|ConvertFrom-Json
  foreach($name in @('YeManFanHost.payload.json')+@($hostManifest.files|ForEach-Object path)){
    Copy-Item -LiteralPath (Join-Path $payload $name) -Destination (Join-Path $script:fanHostSource $name)
  }
  $script:layoutManifestPath=Join-Path $Root 'YeManCC\update-manifest.json'
  $binding=[ordered]@{directory='fan-host-v2';schemaVersion=2;manifestSha256=(Hash (Join-Path $script:fanHostSource 'YeManFanHost.payload.json'));fileCount=@($hostManifest.files).Count;runtimeDirectory=$script:runtimeDir;runtimeManifestSha256=(Hash (Join-Path $script:core 'HandheldCompanion.runtime.json'))}
  [IO.File]::WriteAllText($script:layoutManifestPath,([ordered]@{rules=[ordered]@{fanHostPayload=$binding}}|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
}
function Update-RuntimeBinding {
  $layout=Get-Content -LiteralPath $script:layoutManifestPath -Raw|ConvertFrom-Json
  $layout.rules.fanHostPayload.runtimeManifestSha256=Hash (Join-Path $script:core 'HandheldCompanion.runtime.json')
  [IO.File]::WriteAllText($script:layoutManifestPath,($layout|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
}
function Reject([string]$Name,[scriptblock]$Mutate,[scriptblock]$Restore,[string]$Expected){
  $failure=''
  try{& $Mutate;try{Assert-DeviceSharedFiles $script:core (Get-Content -LiteralPath (Join-Path $script:core 'HandheldCompanion.runtime.json') -Raw|ConvertFrom-Json);Assert-FanHostV2Payload}catch{$failure=$_.Exception.Message}}
  finally{& $Restore}
  if(-not $failure -or $failure -notlike ('*'+$Expected+'*')){throw "Expected rejection failed: $Name ($failure)"}
  Assert-FanHostV2Payload
  $checks.Add($Name)
}
$fixture=Join-Path $ScratchRoot 'current'
Set-Fixture $fixture (Join-Path $ProjectRoot 'PowerControl\handheldcompanion-runtime\YMCC-DEVICECORE-20261018-v1')
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'PowerControl\ymcc-shared-runtime') -Destination (Join-Path $fixture 'PowerControl\ymcc-shared-runtime') -Recurse
Assert-FanHostV2Payload;Assert-DeviceSharedFiles $script:core (Get-Content -LiteralPath (Join-Path $script:core 'HandheldCompanion.runtime.json') -Raw|ConvertFrom-Json);$checks.Add('Actual embedded and standalone installers accept current split shared runtime')
$sharedFile=Join-Path $fixture 'PowerControl\ymcc-shared-runtime\Newtonsoft.Json.dll';$sharedBytes=[IO.File]::ReadAllBytes($sharedFile)
Reject 'Missing shared library rejected' {[IO.File]::Delete($sharedFile)} {[IO.File]::WriteAllBytes($sharedFile,$sharedBytes)} 'shared dependency'
Reject 'Changed shared library rejected' {$b=[byte[]]$sharedBytes.Clone();$b[$b.Length-1]=$b[$b.Length-1] -bxor 1;[IO.File]::WriteAllBytes($sharedFile,$b)} {[IO.File]::WriteAllBytes($sharedFile,$sharedBytes)} 'shared dependency'
$runtimeManifest=Join-Path $script:core 'HandheldCompanion.runtime.json';$runtimeBytes=[IO.File]::ReadAllBytes($runtimeManifest);$layoutBytes=[IO.File]::ReadAllBytes($script:layoutManifestPath)
$restoreManifest={ [IO.File]::WriteAllBytes($runtimeManifest,$runtimeBytes);[IO.File]::WriteAllBytes($script:layoutManifestPath,$layoutBytes) }
function Write-Runtime($Object){[IO.File]::WriteAllText($runtimeManifest,($Object|ConvertTo-Json -Depth 40),[Text.UTF8Encoding]::new($false));Update-RuntimeBinding}
Reject 'Wrong shared directory rejected' {$m=Get-Content -LiteralPath $runtimeManifest -Raw|ConvertFrom-Json;$m.sharedDependencies.directory='PowerControl/other';Write-Runtime $m} $restoreManifest 'shared dependency contract'
Reject 'Duplicate shared library rejected' {$m=Get-Content -LiteralPath $runtimeManifest -Raw|ConvertFrom-Json;$m.sharedDependencies.files[1]=$m.sharedDependencies.files[0];Write-Runtime $m} $restoreManifest 'shared dependency'
Reject 'Shared path escape rejected' {$m=Get-Content -LiteralPath $runtimeManifest -Raw|ConvertFrom-Json;$m.sharedDependencies.files[0].path='..\escape.dll';Write-Runtime $m} $restoreManifest 'shared dependency path/hash'
Reject 'Wrong shared byte size rejected' {$m=Get-Content -LiteralPath $runtimeManifest -Raw|ConvertFrom-Json;$m.sharedDependencies.files[0].bytes++;Write-Runtime $m} $restoreManifest 'shared dependency'
Reject 'Wrong shared hash rejected' {$m=Get-Content -LiteralPath $runtimeManifest -Raw|ConvertFrom-Json;$m.sharedDependencies.files[0].sha256=('0'*64);Write-Runtime $m} $restoreManifest 'shared dependency'
$coreFile=Join-Path $script:core 'HandheldCompanion.dll';$coreBytes=[IO.File]::ReadAllBytes($coreFile)
Reject 'Missing HC library still rejected' {[IO.File]::Delete($coreFile)} {[IO.File]::WriteAllBytes($coreFile,$coreBytes)} 'HC runtime file missing'
$lhmFile=Join-Path $script:core 'LibreHardwareMonitorLib.dll';$lhmBytes=[IO.File]::ReadAllBytes($lhmFile)
Reject 'Missing canonical local LHM rejected' {[IO.File]::Delete($lhmFile)} {[IO.File]::WriteAllBytes($lhmFile,$lhmBytes)} 'HC runtime file missing'
Reject 'Changed canonical local LHM rejected' {$b=[byte[]]$lhmBytes.Clone();$b[$b.Length-1]=$b[$b.Length-1] -bxor 1;[IO.File]::WriteAllBytes($lhmFile,$b)} {[IO.File]::WriteAllBytes($lhmFile,$lhmBytes)} 'HC runtime hash mismatch'
$hostFile=Join-Path $script:fanHostSource 'YeManFanHost.dll';$hostBytes=[IO.File]::ReadAllBytes($hostFile)
Reject 'Changed Host payload still rejected' {$b=[byte[]]$hostBytes.Clone();$b[$b.Length-1]=$b[$b.Length-1] -bxor 1;[IO.File]::WriteAllBytes($hostFile,$b)} {[IO.File]::WriteAllBytes($hostFile,$hostBytes)} 'Fan Host V2 payload hash mismatch'
$extra=Join-Path $script:core 'Newtonsoft.Json.dll';$sharedDuplicateSource=Join-Path $fixture 'PowerControl\ymcc-shared-runtime\Newtonsoft.Json.dll'
Reject 'Undeclared local shared duplicate rejected' {[IO.File]::Copy($sharedDuplicateSource,$extra)} {[IO.File]::Delete($extra)} 'HC shared runtime has files outside'
$legacySource='D:\Codex\2026-10-18\kai-s\work\hc-followup-trim\preimages\runtime-live-before-promotion'
if(Test-Path -LiteralPath $legacySource){Set-Fixture (Join-Path $ScratchRoot 'legacy') $legacySource;Assert-FanHostV2Payload;$checks.Add('Original complete local runtime remains accepted')}
[ordered]@{status='passed';checks=$checks.Count;details=@($checks);functionsOnly=$true;productHostExecuted=$false;installedDeploymentPerformed=$false;hardwareExecuted=$false}|ConvertTo-Json -Depth 15|Set-Content -LiteralPath (Join-Path $ScratchRoot 'verification.json') -Encoding UTF8
Write-Output ('INSTALLER_SHARED_DEPENDENCY_GATE_OK checks='+$checks.Count)
