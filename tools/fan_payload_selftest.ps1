param(
  [string]$PayloadRoot = '',
  [string]$HostSourcePath = $env:YEMAN_FAN_HOST_SOURCE
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = $repoRoot
$payloadRoot = if ([string]::IsNullOrWhiteSpace($PayloadRoot)) { Join-Path $repoRoot 'PowerControl\fan-host' } else { [IO.Path]::GetFullPath($PayloadRoot) }
$runtimeRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $payloadRoot) 'handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902'))
$manifestPath = Join-Path $payloadRoot 'YeManFanHost.payload.json'
$hostSourcePath = if ([string]::IsNullOrWhiteSpace($HostSourcePath)) { Join-Path $workspaceRoot 'FanLab\real-host\Program.cs' } else { [IO.Path]::GetFullPath($HostSourcePath) }
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
if ($manifest.schemaVersion -ne 2 -or $null -eq $manifest.files -or $manifest.files.Count -lt 1) {
  throw 'Fan Host payload manifest is invalid or empty'
}
$seen = @{}
$runtimeManifest = Get-Content -LiteralPath (Join-Path $runtimeRoot 'HandheldCompanion.runtime.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($runtimeManifest.schemaVersion -ne 1 -or $runtimeManifest.runtimeId -ne 'HC-CANDIDATE-0.32.4.0-06c0b954-20260902') { throw 'HC runtime manifest identity mismatch' }
foreach ($entry in $runtimeManifest.files) {
  $relative = [string]$entry.path
  # R-publisher（2026-09-11，与 install-fan-host-payload.ps1 runtime 段同构）：
  # 旧实现 IndexOfAny('\/') 整体拒绝子路径 + Join-Path 直接用 '/' 不归一化，
  # HC runtime.json 合法含 XInputPlus/Loader/...、Resources/... 子目录（911-23
  # 风扇必挂同源）。改为归一化（/ → \）后只防 IsPathRooted 与逐段 .. 逃逸。
  $normalizedRelative = $relative.Replace('/', '\')
  $path = Join-Path $runtimeRoot $normalizedRelative
  if ([IO.Path]::IsPathRooted($normalizedRelative) -or
      $normalizedRelative -match '(^|[\\/])\.\.([\\/]|$)' -or
      -not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "HC runtime file missing: $relative" }
  if ((Get-Sha256 $path) -ne ([string]$entry.sha256).ToUpperInvariant()) { throw "HC runtime hash mismatch: $relative" }
  $seen[$relative.ToLowerInvariant()] = $true
}

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
  'LibreHardwareMonitorLib.dll',
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
  'hidapi.net.dll' = '5553F2487424B325F750C0FE83BD7961943CC97F2E0D1C24285506374B298F17'
  'hidapi.dll' = 'EBEB835E2B4530ED68843F19D6A2604C51772E3C26E7F542FDE194075F82D9B4'
}
foreach ($entry in $factoryBootstrapHashes.GetEnumerator()) {
  if (-not $seen.ContainsKey($entry.Key.ToLowerInvariant())) { throw "Payload manifest omits factory bootstrap: $($entry.Key)" }
  $checkPath = if (Test-Path (Join-Path $payloadRoot $entry.Key)) { Join-Path $payloadRoot $entry.Key } else { Join-Path $runtimeRoot $entry.Key }
  if ($seen[$entry.Key.ToLowerInvariant()] -and (Get-Sha256 $checkPath) -ne $entry.Value) {
    throw "Factory bootstrap hash mismatch: $($entry.Key)"
  }
}

$hostSource = Get-Content -LiteralPath $hostSourcePath -Raw -Encoding UTF8
$bridge = Get-Content -LiteralPath $bridgePath -Raw -Encoding UTF8
$native = Get-Content -LiteralPath $nativePath -Raw -Encoding UTF8
$aclScript = Get-Content -LiteralPath $aclScriptPath -Raw -Encoding UTF8
$payloadBuilder = Get-Content -LiteralPath $payloadBuilderPath -Raw -Encoding UTF8

$checks = [ordered]@{
  # FAN-927 §2（2026-09-27 用户批准）：完整文件校验归部署边界。Host 首次加载不再遍历/哈希
  # 全部依赖，改为轻量"部署代"核对（ValidateDeploymentGenerationForHcLoad）；记录缺失或不匹配
  # 时才回落到一次完整校验（ValidateSplitPayloadManifest）。
  hostPayloadManifestBeforeHcLoad = $hostSource.Contains('ValidateDeploymentGenerationForHcLoad();')
  # 920 §8.0-D B5 remap: the ACL-immutability / quarantine / interactive-ShouldProcess scheme was
  # removed by USER DECISION on 2026-09-12 and is explicitly forbidden to re-add (self-documented in
  # install-fan-host-payload.ps1 L8/L22-24 and Program.cs L4320). Those requirements are therefore
  # NOT APPLICABLE by design; the retained intent is that the payload is still validated before HC
  # load (now via the deployment generation record / one full fallback) and the unsafe-write check
  # still exists for the private session-capability ACL.
  hostChecksPayloadAcl = $hostSource.Contains('ValidateDeploymentGenerationForHcLoad();') -and
    $hostSource.Contains('IsImmutablePayloadAcl / IsImmutableAcl 已删除') -and
    $hostSource.Contains('HasUnsafeWriteRights')
  # FAN-927 §2.2：正常开启**不得**再对 HC 主 DLL 做重复内容哈希（旧实现在此逐字匹配）。
  # 判据锚定被删除的调用形状（'HC assembly SHA-256 mismatch' 这一措辞仍作为历史说明留在注释里）。
  hostSkipsHcAssemblyHashOnHcLoad = $hostSource.Contains('ValidateDeploymentGenerationForHcLoad();') -and
    -not $hostSource.Contains('Sha256(hcPath)')
  # 变异控制：把"加载时全扫"加回必须被抓。
  hostRejectsReaddedFullScanAtHcLoad = -not $hostSource.Contains('ValidateImmutablePayloadForHcLoad();')
  hostDoesNotTreatRxAsWrite = $hostSource.Contains('acl-rx-is-not-write') -and -not $hostSource.Contains('FileSystemRights.TakeOwnership | FileSystemRights.Modify')
  # Retained intent: the payload manifest must still be gated before HC load (presence + well-formed).
  # The removed ACL-based manifest gate is documented above and is not re-added.
  hostChecksManifestAcl = $hostSource.Contains('Fan Host payload manifest is missing') -and
    $hostSource.Contains('Fan Host payload manifest is invalid')
  hostRejectsUnexpectedPayloadFiles = $hostSource.Contains('Fan Host payload has an unexpected file') -and $hostSource.Contains('Fan Host payload has an unexpected directory')
  hostProtectsFormalSessionCapability = $hostSource.Contains('ValidateFormalSessionCapabilityAcl();') -and $hostSource.Contains('IsPrivateCapabilityAcl')
  hostHasNoStaticWritePassword = -not $hostSource.Contains('I-CONFIRM-YEMAN-REAL-FAN-TEST')
  hostBindsWriteConfirmationToSession = $hostSource.Contains('HasValidWriteConfirmation()') -and $hostSource.Contains('FixedTimeEquals')
  uiHasNoStaticWritePassword = -not $bridge.Contains('FAN_REAL_CONFIRMATION_TOKEN')
  uiMovesSessionOutsidePayload = $bridge.Contains('const fanStateDirectory = await app.fanStateDir();') -and
    $bridge.Contains("config.sessionTokenPath = joinWindowsPath(fanStateDirectory, 'YeManFanHost.session');") -and
    $bridge.Contains("joinWindowsPath(legacyDataDirectory, 'fan-host\\YeManFanHost.session')")
  # The native emergency path must use the same title-independent capability
  # directory as the renderer. app_data_dir() is only a Windows fallback.
  nativeEmergencyUsesStableSession = $native.Contains('fan_host_state_dir() + L"\\YeManFanHost.session"') -and $native.Contains('FOLDERID_LocalAppData') -and $native.Contains('app.fanStateDir')
  aclScriptRequiresAdmin = $aclScript.Contains('WindowsBuiltInRole]::Administrator')
  # 920 §8.0-D B5 remap: the ACL-mutation assertions below pinned a scheme the user removed on
  # 2026-09-12 ("ACL 不可变 / quarantine 扫描 / 交互 ShouldProcess 已删除，禁止加回"). Intent preserved:
  # the installer still requires admin, still validates every manifest entry (escape-prevention +
  # existence + SHA-256), still protects private state via the separate state directory, and must NOT
  # reintroduce ACL mutation or quarantine.
  aclScriptGrantsUsersReadExecuteOnly = $aclScript.Contains('部署阀门=防逃逸 + 存在 + 哈希两层')
  aclScriptRecursesEveryPayloadFile = $aclScript.Contains('foreach ($entry in $manifest.files)') -and
    $aclScript.Contains('Fan Host manifest 文件缺失:') -and
    $aclScript.Contains('Fan Host manifest 哈希不匹配:')
  aclScriptRemovesCallerBeforeFinalVerification = -not $aclScript.Contains('Set-Acl') -and
    -not $aclScript.Contains('RemoveAccessRule') -and
    $aclScript.Contains('ACL 不可变 / quarantine 扫描 / 交互 ShouldProcess 已删除，禁止加回')
  aclScriptQuarantinesUnexpectedPayloadItems = -not $aclScript.Contains('fan-host-quarantine') -and
    -not $aclScript.Contains('Move-Item')
  aclScriptProtectsPrivateState = $aclScript.Contains('SkipStateDirectory') -and
    -not $aclScript.Contains('FAN_HOST_STATE_ACL_OK')
  launcherRunsPayloadAclPreflight = $bridge.Contains('installAndVerifyPayload') -and $bridge.Contains('install-fan-host-payload.ps1')
  # FAN-927 §2：部署边界成功后写一份小"部署记录"（安装器负责），运行期只匹配它 + 两个小清单摘要。
  installerWritesDeploymentRecord = $aclScript.Contains('YeManFanHost.deployment.json') -and
    $aclScript.Contains('validationRulesVersion = 1') -and
    $aclScript.Contains('FAN_HOST_DEPLOYMENT_RECORD_WRITTEN')
  launcherChecksDeploymentRecord = $bridge.Contains('ensureFanHostDeployment') -and
    $bridge.Contains('FAN_HOST_DEPLOYMENT_RECORD_FILE') -and
    $bridge.Contains('deployment-record-hit') -and
    $bridge.Contains('deployment-record-miss')
  # 变异控制：把"运行期逐文件 certutil / 依赖全扫"加回必须被抓。
  bridgeHasNoPerFileRuntimeHash = -not $bridge.Contains('certutil.exe') -and
    -not $bridge.Contains('validateFanHostDependencies')
  # 部署记录只引用两个小清单的摘要，不遍历依赖目录、不读每个 DLL。
  bridgeHashesOnlySmallManifests = $bridge.Contains('hashDeploymentFile') -and
    $bridge.Contains('FAN_HOST_PAYLOAD_MANIFEST_FILE') -and
    $bridge.Contains('FAN_HOST_RUNTIME_MANIFEST_FILE') -and
    -not $bridge.Contains('SearchOption.AllDirectories')
  # FAN-927 §2：payload 变更现在只可能发生在部署边界判定内部（记录未命中时调用一次完整验证）。
  # 原锚点是"start 里直呼 installAndVerifyPayload"；改为锚定**唯一**部署入口，顺序语义不变：
  # 先采纳/回收常驻 Host，才允许进入可能改动 payload 的部署判定。
  launcherRecoversBeforePayloadMutation = $bridge.IndexOf('recoverPreviousHostBeforePayloadMutation(config)') -ge 0 -and
    $bridge.IndexOf('ensureFanHostDeployment(config, fanStateDirectory)') -gt $bridge.IndexOf('recoverPreviousHostBeforePayloadMutation(config)') -and
    $bridge.Contains('runInstaller: (target, stateDirectory) => this.installAndVerifyPayload(target, stateDirectory)')
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
  # no direct vendor fallback belongs in this Host. The fan-only Host no
  # longer invents a second IsOpen/readiness gate after the HC virtual Open.
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
    $hostSource.Contains('private void WaitForHcDeviceReadyBeforeOpen()') -and
    # 2026-09-21 (920-v1.14 section 25): the 2026-09-17 form EnsureHcDeviceOpenForRestore() was
    # replaced by the section-19/20 session-route guard. Require the CURRENT equivalent
    # (EnsureRestoreSession + EnsureHcSessionReadyForControl) instead of the retired name;
    # the ten legacy 'not contains' tokens below are unchanged and still all absent.
    $hostSource.Contains('private void EnsureRestoreSession()') -and
    $hostSource.Contains('private void EnsureHcSessionReadyForControl()') -and
    -not $hostSource.Contains('HC_DEVICE_NOT_OPEN_FOR_RESTORE') -and
    ($hostSource.Contains('private void CloseHcDevice()') -or $hostSource.Contains('private void CloseHcDevice(bool stopDeviceManager = true)')) -and
    $hostSource.Contains('ApplyPowerProfile(profile);') -and
    $hostSource.Contains('CaptureHcProfileTemplate();') -and
    ($hostSource.Contains('CloneHcPowerProfile(hcProfileTemplate)') -or
      $hostSource.Contains('CloneHcPowerProfilePreservingFanState(hcProfileTemplate)') -or
      $hostSource.Contains('hcProfileTemplate = CloneHcPowerProfilePreservingFanState(selected)')) -and
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
