[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ProjectRoot, [Parameter(Mandatory=$true)][string]$RuntimeRoot, [Parameter(Mandatory=$true)][string]$ScratchRoot)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath
$ScratchRoot = [IO.Path]::GetFullPath($ScratchRoot).TrimEnd('\')
if (Test-Path -LiteralPath $ScratchRoot) { throw 'Require a new scratch directory.' }
$sourceRoot = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
if ($ScratchRoot.StartsWith($sourceRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Scratch cannot be under product source.' }
. (Join-Path $ProjectRoot 'tools\hc-device-runtime\verify-runtime.ps1')
$profileDest = Join-Path $ScratchRoot 'deps\ymcc-device-core-runtime'
New-Item -ItemType Directory -Path $profileDest -Force | Out-Null
foreach ($name in @('profile.json','upstream.runtime.json')) { Copy-Item -LiteralPath (Join-Path $ProjectRoot ('deps\ymcc-device-core-runtime\'+$name)) -Destination (Join-Path $profileDest $name) }
$profile = Get-Content -LiteralPath (Join-Path $profileDest 'profile.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($profile.PSObject.Properties['sharedDependencies']) {
  Copy-Item -LiteralPath (Join-Path $ProjectRoot 'deps\ymcc-shared-runtime') -Destination (Join-Path $ScratchRoot 'deps\ymcc-shared-runtime') -Recurse
}
$payload = Join-Path $ScratchRoot 'payload'
Copy-Item -LiteralPath $RuntimeRoot -Destination $payload -Recurse
$receipt = Assert-YmccDeviceCoreRuntime -ProjectRoot $ScratchRoot -RuntimeRoot $payload
$script:checks = [Collections.Generic.List[object]]::new()
$script:checks.Add([pscustomobject]@{ name='Exact approved runtime accepted'; status='PASS' })
function Check-Reject([string]$Name,[scriptblock]$Mutate,[scriptblock]$Restore,[string]$ExpectedReason = '') {
  $failure = ''
  try { & $Mutate; try { Assert-YmccDeviceCoreRuntime -ProjectRoot $ScratchRoot -RuntimeRoot $payload | Out-Null } catch { $failure = $_.Exception.Message } } finally { & $Restore }
  if (-not $failure) { throw "Expected rejection failed: $Name" }
  if ($ExpectedReason -and $failure -notmatch $ExpectedReason) { throw "Wrong rejection for ${Name}: $failure" }
  Assert-YmccDeviceCoreRuntime -ProjectRoot $ScratchRoot -RuntimeRoot $payload | Out-Null
  $script:checks.Add([pscustomobject]@{ name=$Name; status='PASS'; rejection=$failure })
}
$core = Join-Path $payload 'HandheldCompanion.dll'; $coreBytes = [IO.File]::ReadAllBytes($core)
Check-Reject 'Changed HC core bytes rejected' { $bytes=[byte[]]$coreBytes.Clone();$bytes[$bytes.Length-1]=$bytes[$bytes.Length-1] -bxor 1;[IO.File]::WriteAllBytes($core,$bytes) } { [IO.File]::WriteAllBytes($core,$coreBytes) }
$inpout = Join-Path $payload 'Resources\inpoutx64.dll'; $inpoutBytes = [IO.File]::ReadAllBytes($inpout)
Check-Reject 'Missing retained EC transport rejected' { [IO.File]::Delete($inpout) } { [IO.File]::WriteAllBytes($inpout,$inpoutBytes) }
$extra = Join-Path $payload 'unapproved.dll'
Check-Reject 'Unexpected binary rejected' { [IO.File]::WriteAllBytes($extra,[byte[]]@(1,2,3)) } { [IO.File]::Delete($extra) }
$excluded = Join-Path $payload 'PresentationCore.dll'
Check-Reject 'Removed framework duplicate cannot return' { [IO.File]::WriteAllBytes($excluded,[byte[]]@(1,2,3)) } { [IO.File]::Delete($excluded) }
$manifest = Join-Path $payload 'HandheldCompanion.runtime.json'; $manifestBytes = [IO.File]::ReadAllBytes($manifest)
function Write-Manifest($Object) { [IO.File]::WriteAllText($manifest,($Object | ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false)) }
Check-Reject 'Omitted manifest file rejected' { $j=Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json;$j.files=@($j.files | Select-Object -Skip 1);Write-Manifest $j } { [IO.File]::WriteAllBytes($manifest,$manifestBytes) }
Check-Reject 'Duplicate manifest file rejected' { $j=Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json;$j.files=@($j.files)+@($j.files[0]);Write-Manifest $j } { [IO.File]::WriteAllBytes($manifest,$manifestBytes) }
Check-Reject 'Manifest path escape rejected' { $j=Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json;$j.files[0].path='../outside.dll';Write-Manifest $j } { [IO.File]::WriteAllBytes($manifest,$manifestBytes) }
Check-Reject 'Wrong derivative profile rejected' { $j=Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json;$j.deviceCoreProfileSha256=('0'*64);Write-Manifest $j } { [IO.File]::WriteAllBytes($manifest,$manifestBytes) }
# Identity-only fixture: a private copy with a harmless appended marker is never loaded.
# This checks profile authorization, not functional/ABI acceptance of a trimmed library.
$profileFile = Join-Path $profileDest 'profile.json'
$profileFileBytes = [IO.File]::ReadAllBytes($profileFile)
$callbackDll = Join-Path $payload 'WindowsInput.dll'
$callbackOriginalBytes = [IO.File]::ReadAllBytes($callbackDll)
$callbackFixtureBytes = [byte[]]($callbackOriginalBytes + [Text.Encoding]::UTF8.GetBytes('YMCC profile identity fixture; never execute'))
Check-Reject 'Unlisted DLL replacement rejected' {
  [IO.File]::WriteAllBytes($callbackDll,$callbackFixtureBytes)
} { [IO.File]::WriteAllBytes($callbackDll,$callbackOriginalBytes) }
try {
  [IO.File]::WriteAllBytes($callbackDll,$callbackFixtureBytes)
  $fixtureHash = (Get-FileHash -LiteralPath $callbackDll -Algorithm SHA256).Hash.ToLowerInvariant()
  $candidateProfile = Get-Content -LiteralPath $profileFile -Raw -Encoding UTF8 | ConvertFrom-Json
  $candidateProfile.replacements = @($candidateProfile.replacements | Where-Object {$_.path -cne 'WindowsInput.dll'}) + @([pscustomobject]@{path='WindowsInput.dll';generatedSource='identity-fixture-only';sha256=$fixtureHash;bytes=$callbackFixtureBytes.Length;modifiedByYmcc=$true})
  [IO.File]::WriteAllText($profileFile,($candidateProfile|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
  $candidateManifest = Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json
  $candidateManifest.deviceCoreProfileSha256 = (Get-FileHash -LiteralPath $profileFile -Algorithm SHA256).Hash.ToLowerInvariant()
  @($candidateManifest.files|Where-Object {$_.path -ceq 'WindowsInput.dll'})[0].sha256 = $fixtureHash
  Write-Manifest $candidateManifest
  $fixtureReceipt = Assert-YmccDeviceCoreRuntime -ProjectRoot $ScratchRoot -RuntimeRoot $payload
  if (@($fixtureReceipt.declaredDerivativeDlls | Where-Object {$_ -ceq 'WindowsInput.dll'}).Count -ne 1) { throw 'Declared DLL derivative omitted from receipt' }
  $script:checks.Add([pscustomobject]@{name='Profile-pinned ordinary DLL derivative accepted';status='PASS';identityOnly=$true;targetCodeExecuted=$false})
  $candidateProfileBytes = [IO.File]::ReadAllBytes($profileFile)
  $candidateManifestBytes = [IO.File]::ReadAllBytes($manifest)
  Check-Reject 'Declared DLL replacement hash mismatch rejected' {
    $bytes=[byte[]]$callbackFixtureBytes.Clone();$bytes[$bytes.Length-1]=$bytes[$bytes.Length-1] -bxor 1
    [IO.File]::WriteAllBytes($callbackDll,$bytes)
  } { [IO.File]::WriteAllBytes($callbackDll,$callbackFixtureBytes) }
  Check-Reject 'Declared DLL replacement size mismatch rejected' {
    $wrong = Get-Content -LiteralPath $profileFile -Raw -Encoding UTF8 | ConvertFrom-Json
    @($wrong.replacements | Where-Object {$_.path -ceq 'WindowsInput.dll'})[0].bytes = $callbackFixtureBytes.Length + 1
    [IO.File]::WriteAllText($profileFile,($wrong|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
    $wrongManifest = Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json
    $wrongManifest.deviceCoreProfileSha256 = (Get-FileHash -LiteralPath $profileFile -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Manifest $wrongManifest
  } {
    [IO.File]::WriteAllBytes($profileFile,$candidateProfileBytes)
    [IO.File]::WriteAllBytes($manifest,$candidateManifestBytes)
  }
} finally {
  [IO.File]::WriteAllBytes($profileFile,$profileFileBytes)
  [IO.File]::WriteAllBytes($callbackDll,$callbackOriginalBytes)
  [IO.File]::WriteAllBytes($manifest,$manifestBytes)
}
Assert-YmccDeviceCoreRuntime -ProjectRoot $ScratchRoot -RuntimeRoot $payload | Out-Null
$index = Join-Path $profileDest 'upstream.runtime.json';$indexBytes=[IO.File]::ReadAllBytes($index)
Check-Reject 'Changed frozen upstream index rejected' { [IO.File]::WriteAllBytes($index,[byte[]]@(1,2,3)) } { [IO.File]::WriteAllBytes($index,$indexBytes) }
if ($profile.PSObject.Properties['sharedDependencies']) {
  $sharedBinary = Join-Path $ScratchRoot 'deps\ymcc-shared-runtime\bin\Newtonsoft.Json.dll'; $sharedBytes = [IO.File]::ReadAllBytes($sharedBinary)
  Check-Reject 'Missing shared device dependency rejected' { [IO.File]::Delete($sharedBinary) } { [IO.File]::WriteAllBytes($sharedBinary,$sharedBytes) }
  Check-Reject 'Changed shared device dependency rejected' { $bytes=[byte[]]$sharedBytes.Clone();$bytes[$bytes.Length-1]=$bytes[$bytes.Length-1] -bxor 1;[IO.File]::WriteAllBytes($sharedBinary,$bytes) } { [IO.File]::WriteAllBytes($sharedBinary,$sharedBytes) }
  Check-Reject 'Omitted shared device contract rejected' { $j=Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json;$j.sharedDependencies.files=@($j.sharedDependencies.files | Select-Object -Skip 1);Write-Manifest $j } { [IO.File]::WriteAllBytes($manifest,$manifestBytes) }
}
$result=[ordered]@{status='PASS';checks=@($script:checks);runtime=$receipt;hardwareIo=$false;installedDeploymentPerformed=$false}
$result|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $ScratchRoot 'verification.json') -Encoding UTF8
Write-Output ('DEVICECORE_EXPORT_SELFTEST_OK checks='+$script:checks.Count)
