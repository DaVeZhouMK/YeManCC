[CmdletBinding()]
param(
  [string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($OutputPath)) { $OutputPath = Join-Path $scriptRoot '..\..\..\Build\Validation\HC-Parity\A1-HIDHIDE-READONLY-20260903.json' }
$cli = 'C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe'
if (!(Test-Path -LiteralPath $cli -PathType Leaf)) { throw "HidHideCLI missing: $cli" }
function Invoke-Cli([string[]]$cliArgs) { @(& $cli @cliArgs 2>&1 | ForEach-Object { [string]$_ }) }
$version = (Invoke-Cli @('--version') -join ' ').Trim()
$cloak = (Invoke-Cli @('--cloak-state') -join ' ').Trim()
$inverse = (Invoke-Cli @('--inv-state') -join ' ').Trim()
$apps = @(Invoke-Cli @('--app-list'))
$hidden = @(Invoke-Cli @('--dev-list'))
$present = @{}
foreach ($entry in $hidden) {
  if ($entry -match '^--dev-hide\s+"(.+)"$') {
    $id = $Matches[1]
    $device = Get-PnpDevice -InstanceId $id -ErrorAction SilentlyContinue
    $present[$id] = [bool]($device -and $device.Present)
  }
}
$registeredPaths = @($apps | ForEach-Object { if ($_ -match '^--app-reg\s+"(.+)"$') { $Matches[1] } })
$result = [ordered]@{
  evidenceId = 'A1-HIDHIDE-READONLY-20260903'
  status = 'LOCATED'
  mutated = $false
  binary = $cli
  version = $version
  readOnlyCommands = @('--version','--cloak-state','--inv-state','--app-list','--dev-list')
  observed = [ordered]@{
    cloak = $cloak
    inverseApplicationList = $inverse
    registeredApplications = $registeredPaths
    registeredApplicationExists = @($registeredPaths | ForEach-Object { [ordered]@{ path = $_; exists = [bool](Test-Path -LiteralPath $_ -PathType Leaf) } })
    hiddenDeviceEntries = $hidden
    hiddenDevicePresent = $present
  }
  conclusion = 'HidHide version and configuration were read without mutation. Hidden entries and application paths are recorded as baseline only; no cleanup or suppression claim is inferred.'
  generatedUtc = [DateTime]::UtcNow.ToString('o')
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Output "A1 HIDHIDE READONLY AUDIT: $($result.status)"; Write-Output "Evidence: $OutputPath"
