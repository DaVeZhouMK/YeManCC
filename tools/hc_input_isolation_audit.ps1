[CmdletBinding()]
param(
  [string]$HcSource = "deps/handheldcompanion-runtime/source",
  [string]$AssetManifest = "../../../Environment/Manifests/hc-gyro-virtual-assets-20260903.json",
  [string]$Output = "../../Build/Validation/HC-Parity/A1-input-isolation-audit.json"
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source = (Resolve-Path (Join-Path $repo $HcSource)).Path
$manifestPath = (Resolve-Path (Join-Path $repo $AssetManifest)).Path
$manifest = Get-Content -Raw $manifestPath | ConvertFrom-Json
$controller = Join-Path $source 'Managers/ControllerManager.cs'
$motion = Join-Path $source 'Managers/MotionManager.cs'
$factory = Join-Path $source 'Managers/ManagerFactory.cs'
$controllerText = Get-Content -Raw $controller
$motionText = Get-Content -Raw $motion
$factoryText = Get-Content -Raw $factory
$fanTokens = @('SetFanControl','SetFanDuty','ECRam','ACPI','WMI','FanControl')
$controllerFanTokens = @($fanTokens | Where-Object { $controllerText.Contains($_) })
$motionFanTokens = @($fanTokens | Where-Object { $motionText.Contains($_) })
$factoryManagers = [regex]::Matches($factoryText, 'public\s+static\s+(?!class\b|List\b)(?:[\w?<>]+)\s+(\w+)\s*;') | ForEach-Object { $_.Groups[1].Value }
$factoryManagerConstructors = [regex]::Matches($factoryText, '(?m)^\s*(\w+)\s*=\s*new\s*\(') | ForEach-Object { $_.Groups[1].Value }
$runtimeAsset = @($manifest.assets | Where-Object { $_.assetId -like 'HC-CANDIDATE*' })[0]
$runtimeManifestPath = $runtimeAsset.runtimeReference + '\' + $runtimeAsset.runtimeManifest
$runtimeHash = if (Test-Path $runtimeManifestPath) { (Get-FileHash $runtimeManifestPath -Algorithm SHA256).Hash } else { $null }
$result = [ordered]@{
  evidenceId = 'A1-HC-INPUT-ISOLATION-20260903'
  status = 'BLOCKED'
  staticChecks = [ordered]@{ runtimeManifestHashMatch = ($runtimeHash -eq $runtimeAsset.runtimeManifestSha256); controllerDirectFanTokenFree = ($controllerFanTokens.Count -eq 0); motionDirectFanTokenFree = ($motionFanTokens.Count -eq 0); managerFactoryIsolationProven = $false }
  assetLockId = $manifest.assetLockId
  hcAssetId = $runtimeAsset.assetId
  hcCommit = $runtimeAsset.gitCommit
  runtimeManifestSha256 = $runtimeHash
  runtimeManifestExpectedSha256 = $runtimeAsset.runtimeManifestSha256
  sourceEvidence = [ordered]@{
    controllerManager = $controller
    motionManager = $motion
    managerFactory = $factory
    controllerInputEntrypoints = @('SDL.Init(Gamepad)','XInput/XUsbDevice','HidDeviceArrived','ControllerState','VirtualManager.UpdateInputs')
    motionEntrypoints = @('MotionManager.UpdateReport','GamepadMotion.GetCalibratedGyro','GamepadMotion.GetGravity')
                           managerFactoryStaticManagers = @($factoryManagers)
                           managerFactoryConstructedManagers = @($factoryManagerConstructors)
    controllerFanTokens = @($controllerFanTokens)
    motionFanTokens = @($motionFanTokens)
  }
  conclusion = 'BLOCKED: full HC ManagerFactory graph initializes device/power/profile/platform managers; input-only loading is not proven isolated from fan/EC/ACPI/WMI. Keep InputHost capability hidden and use mock.'
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
$outPath = Join-Path $repo $Output
New-Item -ItemType Directory -Force (Split-Path $outPath) | Out-Null
$result | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $outPath
Write-Output "A1 HC INPUT ISOLATION: $($result.status)"
Write-Output "Evidence: $outPath"
