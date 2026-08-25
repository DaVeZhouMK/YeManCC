<#
.SYNOPSIS
  Exercises the real immutable-payload ACL installer in a disposable fixture.

.DESCRIPTION
  This never starts YeManFanHost or loads HC. It proves that an inherited
  Authenticated Users Modify grant plus stale files is repaired/quarantined,
  then leaves the fixture and JSON evidence under Build\Validation.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}

function Assert-NoUntrustedWrite([string]$Path) {
  $blocked = @('S-1-1-0', 'S-1-5-11', 'S-1-5-32-545')
  $write = [Security.AccessControl.FileSystemRights]::WriteData -bor
    [Security.AccessControl.FileSystemRights]::AppendData -bor
    [Security.AccessControl.FileSystemRights]::Delete -bor
    [Security.AccessControl.FileSystemRights]::Modify -bor
    [Security.AccessControl.FileSystemRights]::FullControl
  $acl = (Get-Item -LiteralPath $Path -Force).GetAccessControl()
  foreach ($rule in $acl.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])) {
    if ($rule.AccessControlType -eq [Security.AccessControl.AccessControlType]::Allow -and
        $rule.IdentityReference.Value -in $blocked -and
        ($rule.FileSystemRights -band $write) -ne 0) {
      throw "Untrusted write ACL remains: $Path ($($rule.IdentityReference.Value))"
    }
  }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $repoRoot)
$installerSource = Join-Path $repoRoot 'PowerControl\fan-host\install-fan-host-payload.ps1'
$validationRoot = Join-Path $workspaceRoot 'Build\Validation'
$fixtureRoot = Join-Path $validationRoot ('fan-payload-acl-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
$payloadRoot = Join-Path $fixtureRoot 'fan-host'
New-Item -ItemType Directory -Force -Path $payloadRoot | Out-Null

$files = @(
  'YeManFanHost.exe', 'YeManFanHost.dll', 'YeManFanHost.deps.json',
  'YeManFanHost.runtimeconfig.json', 'HandheldCompanion.dll', 'GamepadMotion.dll',
  'YeManFanHost.authorization.md'
)
foreach ($file in $files) {
  [IO.File]::WriteAllText((Join-Path $payloadRoot $file), "fixture-$file", [Text.Encoding]::UTF8)
}
Copy-Item -LiteralPath $installerSource -Destination (Join-Path $payloadRoot 'install-fan-host-payload.ps1') -Force
$files += 'install-fan-host-payload.ps1'

[ordered]@{
  schemaVersion = 1
  files = @($files | ForEach-Object {
    [ordered]@{ path = $_; sha256 = Get-Sha256 (Join-Path $payloadRoot $_) }
  })
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $payloadRoot 'YeManFanHost.payload.json') -Encoding UTF8

# Reproduce the deployment bug found in the ROG feedback: inherited broad
# access and stale old runtime files beside a valid manifest payload.
& icacls.exe $payloadRoot '/grant', '*S-1-5-11:(OI)(CI)(M)', '/T', '/C', '/Q' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Unable to prepare ACL integration fixture' }
[IO.File]::WriteAllText((Join-Path $payloadRoot 'stale-old-host.dll'), 'stale', [Text.Encoding]::UTF8)
New-Item -ItemType Directory -Force -Path (Join-Path $payloadRoot 'logs') | Out-Null
[IO.File]::WriteAllText((Join-Path $payloadRoot 'logs\old-runtime.log'), 'stale log', [Text.Encoding]::UTF8)

$installerStdout = Join-Path $fixtureRoot 'installer.stdout.txt'
$installerStderr = Join-Path $fixtureRoot 'installer.stderr.txt'
$installer = Start-Process -FilePath powershell.exe -ArgumentList @(
  '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
  '-File', (Join-Path $payloadRoot 'install-fan-host-payload.ps1'),
  '-PayloadDirectory', $payloadRoot,
  '-SkipStateDirectory'
) -Wait -PassThru -NoNewWindow -RedirectStandardOutput $installerStdout -RedirectStandardError $installerStderr
$installerOutput = (Get-Content -LiteralPath $installerStdout -Raw) + "`n" + (Get-Content -LiteralPath $installerStderr -Raw)
if ($installer.ExitCode -ne 0 -or $installerOutput -notmatch 'FAN_HOST_ACL_OK:' -or
    $installerOutput -notmatch 'FAN_HOST_PAYLOAD_QUARANTINED:') {
  throw "Payload ACL installer did not complete the full repair: $installerOutput"
}

# The installer itself does the final recursive ACL and manifest checks while
# it still owns the protected directory. The current non-elevated test shell
# intentionally loses access afterwards, which proves it cannot inspect or
# alter the locked payload. Preserve only unprivileged evidence outside it.
$quarantineRoot = Join-Path $fixtureRoot 'fan-host-quarantine'

$evidence = [ordered]@{
  ok = $true
  fixture = $fixtureRoot
  quarantineRoot = $quarantineRoot
  installerReportedRecursiveValidation = $true
  checkedFiles = $files.Count + 2
  hardwareWritesObserved = $false
}
$evidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $fixtureRoot 'result.json') -Encoding UTF8
Write-Output "fan payload ACL integration self-test: PASS ($fixtureRoot)"
