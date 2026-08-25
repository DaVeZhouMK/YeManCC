$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $repoRoot)
$payloadRoot = Join-Path $repoRoot 'PowerControl\fan-host'
$manifestPath = Join-Path $payloadRoot 'YeManFanHost.payload.json'
$hostSourcePath = Join-Path $workspaceRoot 'FanLab\real-host\Program.cs'
$bridgePath = Join-Path $repoRoot 'src\bridge\fanHost.ts'
$nativePath = Join-Path $repoRoot 'native\main.cpp'
$aclScriptPath = Join-Path $payloadRoot 'install-fan-host-payload.ps1'
$payloadBuilderPath = Join-Path $repoRoot 'tools\build-fan-host-payload.ps1'
$closureAuditPath = Join-Path $repoRoot 'tools\fan_hc_device_closure_selftest.ps1'

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try {
      return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '')
    } finally {
      $stream.Dispose()
    }
  } finally {
    $sha.Dispose()
  }
}

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Fan Host payload manifest missing: $manifestPath" }
if (-not (Test-Path -LiteralPath $aclScriptPath -PathType Leaf)) { throw "Fan Host ACL script missing: $aclScriptPath" }
if (-not (Test-Path -LiteralPath $closureAuditPath -PathType Leaf)) { throw "Fan Host device closure audit missing: $closureAuditPath" }

$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or $null -eq $manifest.files -or $manifest.files.Count -lt 1) {
  throw 'Fan Host payload manifest is invalid or empty'
}

$seen = @{}
foreach ($entry in $manifest.files) {
  $relative = [string]$entry.path
  $expected = [string]$entry.sha256
  if ([string]::IsNullOrWhiteSpace($relative) -or $relative -match '(^|[\\/])\.\.([\\/]|$)' -or
      [IO.Path]::IsPathRooted($relative) -or $expected -notmatch '^[0-9a-fA-F]{64}$') {
    throw "Unsafe payload manifest entry: $relative"
  }
  if ($seen.ContainsKey($relative.ToLowerInvariant())) { throw "Duplicate payload manifest entry: $relative" }
  $seen[$relative.ToLowerInvariant()] = $true
  $path = Join-Path $payloadRoot $relative.Replace('/', '\\')
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Payload manifest file missing: $relative" }
  $actual = Get-Sha256 $path
  if ($actual -ne $expected) { throw "Payload manifest hash mismatch: $relative" }
}

$required = @(
  'YeManFanHost.exe', 'YeManFanHost.dll', 'YeManFanHost.deps.json',
  'YeManFanHost.runtimeconfig.json', 'HandheldCompanion.dll', 'HandheldCompanion.deps.json', 'GamepadMotion.dll',
  'hidapi.net.dll', 'hidapi.dll',
  'WindowsInput.dll', 'GregsStack.InputSimulatorStandard.dll', 'Gma.System.MouseKeyHook.dll',
  'HidLibrary.dll', 'Nefarius.Utilities.DeviceManagement.dll', 'Nefarius.Utilities.Bluetooth.dll',
  'Nefarius.Vicius.Abstractions.dll', 'PInvoke.Kernel32.dll', 'PInvoke.Windows.Core.dll',
  'SharpDX.dll', 'SharpDX.Direct3D9.dll', 'SharpDX.DirectInput.dll', 'SharpDX.XInput.dll',
  'System.Management.dll', 'System.IO.Ports.dll', 'System.ServiceProcess.ServiceController.dll',
  'YeManFanHost.authorization.md', 'install-fan-host-payload.ps1'
)
foreach ($name in $required) {
  if (-not $seen.ContainsKey($name.ToLowerInvariant())) { throw "Payload manifest omits required file: $name" }
}

$factoryBootstrapHashes = [ordered]@{
  'WindowsInput.dll' = '5567CEA4661389A7FDCC51EF222E67B13C2176C9BE46E61A88A100188A77C711'
  'GregsStack.InputSimulatorStandard.dll' = '453E8A4B4CF7241954E9AAD060409C24F076EE0C9F742345FC36A1EA8DD8C6EE'
  'Gma.System.MouseKeyHook.dll' = 'FA9FEC4DFC02C80D262E2E61ABCE31D9358CA84E36C9794BA5CB30F912940485'
  'HidLibrary.dll' = '00AD68889764A8BEA6377A01D738A3EBC1DD286691D2AC5BCF7B1D2B16BCD9FA'
  'Nefarius.Utilities.DeviceManagement.dll' = 'B5EAF086634438F2774F6B65DD14254AAA078BF1EBFEB004F997314B61272B7C'
  'Nefarius.Utilities.Bluetooth.dll' = '010B46997F2BEA44A9E95B063E106BE3E662A93A7A7FAB5E5D485644CC48B433'
  'Nefarius.Vicius.Abstractions.dll' = '51F380A12A82E925308E5D6255218DF283B692F37B9E991C6CE5E63F3E11D8FA'
  'PInvoke.Kernel32.dll' = '3122B9C2CCD89B0FF915F4669D60F9FFA1A4D4A8608F61F5DF1B29D6298C4C44'
  'PInvoke.Windows.Core.dll' = '28DC91C7027BA45B07BE564A4564CF9E4606B96B01F4B431056E7D77AB25B81C'
  'SharpDX.dll' = '518D45A5AAEC84CB37E83EE2CF58C503AB6A25FEBB8C48B53316340C967E84BD'
  'SharpDX.Direct3D9.dll' = '69701EDA7433AC0010ABA416B9D9C245CD78694770D4BB6B7541B83BACE41D55'
  'SharpDX.DirectInput.dll' = '35D9AE6B98C5B68FDC1FCAF6E03C95C82F9305C7355DD911F8841880B42E945F'
  'SharpDX.XInput.dll' = '350195201205840B38AEE094BCEAD4C78B1661F3570A7CAA5C36B86CE6D03FF3'
  'System.Management.dll' = '01F9360D110863F810431C4D29ADA0FCA89F267343D030E98AA823EA4C0C0EBB'
  'System.IO.Ports.dll' = 'BF486068A47B18358313791B78ACA74F4DE61D1D9E2E08B58E3BFBF68BF15A2B'
  'System.ServiceProcess.ServiceController.dll' = '3274C2553C736435064E398F879404E8944F39790CAEE6632E6966046B3440E8'
  'hidapi.net.dll' = 'ADC343B824405081A1B3EC69B06B4808734FC448EE757D0EA7B723ACDDCA3182'
  'hidapi.dll' = 'EBEB835E2B4530ED68843F19D6A2604C51772E3C26E7F542FDE194075F82D9B4'
}
foreach ($entry in $factoryBootstrapHashes.GetEnumerator()) {
  if (-not $seen.ContainsKey($entry.Key.ToLowerInvariant())) { throw "Payload manifest omits factory bootstrap: $($entry.Key)" }
  if ($seen[$entry.Key.ToLowerInvariant()] -and (Get-Sha256 (Join-Path $payloadRoot $entry.Key)) -ne $entry.Value) {
    throw "Factory bootstrap hash mismatch: $($entry.Key)"
  }
}

$hostSource = Get-Content -LiteralPath $hostSourcePath -Raw -Encoding UTF8
$bridge = Get-Content -LiteralPath $bridgePath -Raw -Encoding UTF8
$native = Get-Content -LiteralPath $nativePath -Raw -Encoding UTF8
$aclScript = Get-Content -LiteralPath $aclScriptPath -Raw -Encoding UTF8
$payloadBuilder = Get-Content -LiteralPath $payloadBuilderPath -Raw -Encoding UTF8

$checks = [ordered]@{
  hostPayloadManifestBeforeHcLoad = $hostSource.Contains('ValidateImmutablePayloadForHcLoad();')
  hostChecksPayloadAcl = $hostSource.Contains('IsImmutablePayloadAcl()') -and $hostSource.Contains('BuiltinAdministratorsSid') -and $hostSource.Contains('HasUnsafeWriteRights')
  hostDoesNotTreatRxAsWrite = $hostSource.Contains('acl-rx-is-not-write') -and -not $hostSource.Contains('FileSystemRights.TakeOwnership | FileSystemRights.Modify')
  hostChecksManifestAcl = $hostSource.Contains('Fan Host payload manifest ACL permits untrusted write access')
  hostRejectsUnexpectedPayloadFiles = $hostSource.Contains('Fan Host payload has an unexpected file') -and $hostSource.Contains('Fan Host payload has an unexpected directory')
  hostProtectsFormalSessionCapability = $hostSource.Contains('ValidateFormalSessionCapabilityAcl();') -and $hostSource.Contains('IsPrivateCapabilityAcl')
  hostHasNoStaticWritePassword = -not $hostSource.Contains('I-CONFIRM-YEMAN-REAL-FAN-TEST')
  hostBindsWriteConfirmationToSession = $hostSource.Contains('HasValidWriteConfirmation()') -and $hostSource.Contains('FixedTimeEquals')
  uiHasNoStaticWritePassword = -not $bridge.Contains('FAN_REAL_CONFIRMATION_TOKEN')
  uiMovesSessionOutsidePayload = $bridge.Contains('app.dataDir()') -and $bridge.Contains("'fan-host'")
  # The native emergency path must use the same title-independent capability
  # directory as the renderer. app_data_dir() is only a Windows fallback.
  nativeEmergencyUsesStableSession = $native.Contains('fan_host_state_dir() + L"\\YeManFanHost.session"') -and $native.Contains('FOLDERID_LocalAppData') -and $native.Contains('app.fanStateDir')
  aclScriptRequiresAdmin = $aclScript.Contains('WindowsBuiltInRole]::Administrator')
  aclScriptGrantsUsersReadExecuteOnly = $aclScript.Contains('*S-1-5-32-545:(RX)')
  aclScriptRecursesEveryPayloadFile = $aclScript.Contains("'/T', '/C', '/Q'")
  aclScriptQuarantinesUnexpectedPayloadItems = $aclScript.Contains('fan-host-quarantine') -and $aclScript.Contains('Move-Item')
  aclScriptProtectsPrivateState = $aclScript.Contains('SkipStateDirectory') -and $aclScript.Contains('FAN_HOST_STATE_ACL_OK')
  launcherRunsPayloadAclPreflight = $bridge.Contains('installAndVerifyPayload') -and $bridge.Contains('install-fan-host-payload.ps1')
  launcherRecoversBeforePayloadMutation = $bridge.IndexOf('recoverPreviousHostBeforePayloadMutation(config)') -ge 0 -and
    $bridge.IndexOf('installAndVerifyPayload(config, fanStateDirectory)') -gt $bridge.IndexOf('recoverPreviousHostBeforePayloadMutation(config)')
  legacyRecoveryBlocksUnverifiedResident = $bridge.Contains('Never mutate/quarantine the immutable payload') -and
    $bridge.Contains('if (ownerPid <= 0)')
  installerRejectsResidentHost = $aclScript.Contains('$payloadHostPath = Get-FullPath') -and $aclScript.Contains('Get-CimInstance Win32_Process') -and $aclScript.Contains('YeManFanHost.exe')
  updaterRunsPayloadAclPreflight = $native.Contains('Fan Host payload ACL installation failed')
  payloadUsesAuditedRuntimeClosure = $payloadBuilder.Contains('$hcRuntimeSources') -and $payloadBuilder.Contains('PRUNED_STALE_FAN_HOST_FILE')
  payloadPinsFactoryBootstrapClosure = $payloadBuilder.Contains('$hcFactoryBootstrapFiles = @(') -and $payloadBuilder.Contains("'WindowsInput.dll'") -and $payloadBuilder.Contains("'HidLibrary.dll'") -and $payloadBuilder.Contains("'SharpDX.Direct3D9.dll'") -and $payloadBuilder.Contains("'SharpDX.XInput.dll'") -and $payloadBuilder.Contains("'hidapi.net.dll'") -and $payloadBuilder.Contains("'hidapi.dll'")
  payloadDerivesFullHcRuntimeClosure = $payloadBuilder.Contains("'HandheldCompanion.deps.json'") -and $payloadBuilder.Contains('$hcRuntimeSources') -and $payloadBuilder.Contains('$hcDeviceNativeFiles')
  payloadPinsWindowsRuntimeTargets = $payloadBuilder.Contains('$hcWindowsRuntimeOverrides = [ordered]@{') -and $payloadBuilder.Contains('runtimes\win\lib\net10.0\System.Management.dll') -and $payloadBuilder.Contains('runtimes\win\lib\net10.0\System.IO.Ports.dll')
  payloadRunsHcDeviceClosureAudit = $payloadBuilder.Contains('fan_hc_device_closure_selftest.ps1') -and $payloadBuilder.Contains('Get-Command pwsh.exe')
  hostRequiresWindowsRuntimeTargets = $hostSource.Contains('"System.Management.dll"') -and $hostSource.Contains('"System.IO.Ports.dll"') -and $hostSource.Contains('"System.ServiceProcess.ServiceController.dll"')
  bridgeRequiresWindowsRuntimeTargets = $bridge.Contains("{ file: 'System.Management.dll'") -and $bridge.Contains("{ file: 'System.IO.Ports.dll'") -and $bridge.Contains("{ file: 'System.ServiceProcess.ServiceController.dll'")
  hostRequiresFactoryBootstrapClosure = $hostSource.Contains('"WindowsInput.dll"') -and $hostSource.Contains('"HidLibrary.dll"') -and $hostSource.Contains('"SharpDX.Direct3D9.dll"') -and $hostSource.Contains('"SharpDX.XInput.dll"') -and $hostSource.Contains('"hidapi.net.dll"') -and $hostSource.Contains('"hidapi.dll"')
  bridgeRequiresFactoryBootstrapClosure = $bridge.Contains("{ file: 'WindowsInput.dll'") -and $bridge.Contains("{ file: 'HidLibrary.dll'") -and $bridge.Contains("{ file: 'SharpDX.Direct3D9.dll'") -and $bridge.Contains("{ file: 'SharpDX.XInput.dll'") -and $bridge.Contains("{ file: 'hidapi.net.dll'") -and $bridge.Contains("{ file: 'hidapi.dll'")
  # HC ROGAlly dispatches CPU/GPU/Mid SetFanCurve calls. GetFanCurve is kept
  # as optional diagnostics only; HC has no cross-firmware ACK contract and
  # no direct vendor fallback belongs in this Host.
  hostHasHcAsusRestoreReadback = (
    -not $hostSource.Contains('IsHcAsusWriteAcknowledged') -and
    -not $hostSource.Contains('write was not acknowledged') -and
    -not $hostSource.Contains('WriteHcAsusCurve') -and
    -not $hostSource.Contains('CaptureAsusBaseline') -and
    -not $hostSource.Contains('ConfirmAppliedAsusCurve') -and
    -not $hostSource.Contains('AreAsusCurves') -and
    $hostSource.Contains('InvokeStaticMember(acpi, "GetFanCurve"') -and
    $hostSource.Contains('private bool ConfirmAsusOemReadback(') -and
    -not $hostSource.Contains('private bool TryWriteAsusDefaultsDirect(') -and
    $hostSource.Contains('private void MarkHcOemReleaseCallbackCompleted()') -and
    $hostSource.Contains('private void OpenHcDevice()') -and
    $hostSource.Contains('Invoke(device!, "OpenEvents")') -and
    $hostSource.Contains('private void WaitForHcDeviceOpenForRestore()') -and
    $hostSource.Contains('HC_DEVICE_NOT_OPEN_FOR_RESTORE') -and
    ($hostSource.Contains('private void CloseHcDevice()') -or $hostSource.Contains('private void CloseHcDevice(bool stopDeviceManager = true)')) -and
    $hostSource.Contains('ApplyPowerProfile(profile);') -and
    $hostSource.Contains('CaptureHcProfileTemplate();') -and
    ($hostSource.Contains('CloneHcPowerProfile(hcProfileTemplate)') -or
      $hostSource.Contains('CloneHcPowerProfilePreservingFanState(hcProfileTemplate)')) -and
    -not $hostSource.Contains('hcDeviceReportsOpen') -and
    -not $hostSource.Contains('OEM CPU default fallback')
  )
  hostHasNoVendorWriteReplica = -not $hostSource.Contains('WriteEcByte') -and -not $hostSource.Contains('ECRamDirectWriteByte') -and -not $hostSource.Contains('WriteMsiWmiFanTable') -and -not $hostSource.Contains('WriteMsiWmiData') -and -not $hostSource.Contains('ApplyMsiFanCurve') -and -not $hostSource.Contains('ApplyMsiHcDefaultRelease') -and -not $hostSource.Contains('ApplyLenovoFanCurve') -and -not $hostSource.Contains('ApplyLenovoHcDefaultTable') -and -not $hostSource.Contains('ApplyLegionGo2FanCurve') -and -not $hostSource.Contains('ApplyLegionGo2OemRelease') -and -not $hostSource.Contains('ApplySmartFanModeBaseline') -and -not $hostSource.Contains('InvokeSetFanControl')
  payloadBuildsReleaseHost = $payloadBuilder.Contains('dotnet build $hostProject -c Release --no-restore') -and $payloadBuilder.Contains('bin\Release\net10.0-windows10.0.19041.0\win-x64')
  # The product now includes HC's full deps closure, but must not accidentally
  # package HC's own executable, debug symbols or runtime logs.
  payloadOmitsHcExecutableAndDebugArtifacts = -not (@($manifest.files.path | Where-Object { $_ -match '^(?:HandheldCompanion\.exe|Batch11HCHost\.|.*\.pdb|logs/)' }).Count)
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value })
if ($failed.Count -gt 0) { throw "fan payload self-test failed: $($failed.Name -join ', ')" }

Write-Output ('fan payload self-test: PASS (' + ($checks.Keys -join ', ') + ')')
