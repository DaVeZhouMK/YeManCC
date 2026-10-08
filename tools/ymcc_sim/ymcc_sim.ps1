# =============================================================================
# ymcc_sim.ps1 - YMCC global simulator front door (2026-09-22)
#
#   powershell -ExecutionPolicy Bypass -File tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep   -Mode forms
#   powershell -ExecutionPolicy Bypass -File tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep   -Mode live -StartApp
#   powershell -ExecutionPolicy Bypass -File tools\ymcc_sim\ymcc_sim.ps1 -Domain gamepad -Mode selfloop -Persona dualshock4
#   powershell -ExecutionPolicy Bypass -File tools\ymcc_sim\ymcc_sim.ps1 -Domain doctor
#
# The global tool lives at the YMCC mainline layer (repo tools\), NOT under the
# fan task folder: the sleep domain and the gamepad domain share one core, one
# evidence format and one lifecycle. Everything after -Domain is forwarded
# verbatim to that domain's script, so each domain stays directly runnable too.
#
# This front door itself touches nothing but the registry and the lib; the
# domain it routes to owns the actual behaviour and its exit code.
#
# Exit codes (shared by all ymcc_sim domains):
#   0  all checks passed
#   2  one or more checks FAILED
#   3  bad arguments / required file missing
#   4  a product process is already running (exclusive precondition)
#   5  NOT_IMPLEMENTED (domain mode/scenario not built yet)
#   6  requires an elevated session (the app manifest needs administrator)
#   7  sandbox host could not start (port taken / host missing)
# =============================================================================
[CmdletBinding()]
param(
  [ValidateSet('sleep', 'gamepad', 'full', 'msi', 'doctor')][string]$Domain = 'doctor',
  [switch]$Route,     # print the resolved domain command line and exit 0
  [switch]$Elevate,   # re-run this same invocation in an elevated session
  [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }

$simRoot = Split-Path -Parent $PSCommandPath
if (-not $simRoot) { $simRoot = Split-Path -Parent $MyInvocation.MyCommand.Path }
. (Join-Path $simRoot 'lib\sim_core.ps1')

$registryPath = Join-Path $simRoot 'registry.json'
$d = Get-SimDefaults

$domainMap = [ordered]@{
  sleep   = (Join-Path $simRoot 'domains\sleep\sleep_domain.ps1')
  gamepad = (Join-Path $simRoot 'domains\gamepad\gamepad_domain.ps1')
  full    = (Join-Path $simRoot 'domains\full\full_domain.ps1')
  msi     = (Join-Path $simRoot 'domains\msi\msi_domain.ps1')
}
$childArgs = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File')

function Get-ChildCommandLine([string]$DomainName) {
  $script = $domainMap[$DomainName]
  $flat = @()
  foreach ($r in @($Rest)) { if ($null -ne $r -and "$r" -ne '') { $flat += "$r" } }
  return [pscustomobject]@{ script = $script; args = $flat }
}

function Invoke-ElevatedSelf {
  # Re-run the exact same front-door invocation elevated. Used when a domain
  # must start the app (requireAdministrator manifest). One UAC prompt.
  $self = $PSCommandPath
  $argList = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$self`"", '-Domain', $Domain)
  foreach ($r in @($Rest)) { if ($null -ne $r -and "$r" -ne '') { $argList += "`"$r`"" } }
  Write-Host '提权重入：将在 UAC 确认后以管理员会话重新运行本次调用。' -ForegroundColor Yellow
  try {
    $p = Start-Process -FilePath 'powershell.exe' -ArgumentList ($argList -join ' ') -Verb RunAs -PassThru
  } catch {
    Write-Host ('提权被取消或失败：{0}' -f $_.Exception.Message) -ForegroundColor Red
    return 3
  }
  $p.WaitForExit(3600000) | Out-Null
  return [int]$p.ExitCode
}

function Show-Doctor {
  Write-Host '=== ymcc_sim doctor（全局模拟器自检，不启动 App、不起宿主）===' -ForegroundColor White
  $run = New-SimRun -Domain 'doctor' -Mode 'doctor' -ToolPath $PSCommandPath
  $ok = $true

  $core = Get-SimFileIdentity (Join-Path $simRoot 'lib\sim_core.ps1') 'core'
  $ok = (Add-SimCheck $run 'DOCTOR' 'core library present' $core.exists ("{0} bytes={1}" -f $core.sha256.Substring(0, 16), $core.bytes)) -and $ok
  $ok = (Add-SimCheck $run 'DOCTOR' 'registry.json present' (Test-Path -LiteralPath $registryPath) $registryPath) -and $ok

  $reg = $null
  if (Test-Path -LiteralPath $registryPath) {
    try { $reg = Get-Content -LiteralPath $registryPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $reg = $null }
  }
  $ok = (Add-SimCheck $run 'DOCTOR' 'registry.json parses' ($null -ne $reg) $(if ($reg) { "domains=" + @($reg.domains).Count } else { 'parse failed' })) -and $ok

  foreach ($k in $domainMap.Keys) {
    $id = Get-SimFileIdentity $domainMap[$k] $k
    $ok = (Add-SimCheck $run 'DOCTOR' ("domain script: {0}" -f $k) $id.exists ("{0} bytes={1}" -f $id.sha256.Substring(0, 16), $id.bytes)) -and $ok
  }

  # install-tree targets the simulator needs at runtime
  foreach ($name in @('appExe', 'hostExe', 'hostDll', 'mockFlag', 'settingsPath', 'sessionTokenPath')) {
    $p = $d[$name]
    $id = Get-SimFileIdentity $p $name
    $ok = (Add-SimCheck $run 'DOCTOR' ("target: {0}" -f $name) $id.exists ("{0} bytes={1}" -f $id.sha256.Substring(0, 16), $id.bytes)) -and $ok
  }

  # the gamepad facility we wrap (owned by the input line - never modified here)
  $sl = Get-SimFileIdentity $d.selfloopTool 'selfloop'
  $ok = (Add-SimCheck $run 'DOCTOR' 'wrapped gamepad tool present' $sl.exists ("{0} bytes={1}" -f $sl.sha256.Substring(0, 16), $sl.bytes)) -and $ok

  # sleep forms library
  $formsPath = Join-Path $simRoot 'domains\sleep\sleep_forms.json'
  $forms = $null
  if (Test-Path -LiteralPath $formsPath) {
    try { $forms = Get-Content -LiteralPath $formsPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $forms = $null }
  }
  $formCount = 0
  if ($forms -and $forms.forms) { $formCount = @($forms.forms).Count }
  $ok = (Add-SimCheck $run 'DOCTOR' 'sleep forms library parses (20 forms expected)' ($null -ne $forms -and $formCount -eq 20) ("forms=$formCount path=$formsPath")) -and $ok

  $procs = Get-SimProductProcesses
  Add-SimCheck $run 'DOCTOR' 'product process state (precondition for live/selfloop)' $true $(if ($procs.Count -eq 0) { 'none running' } else { 'running: ' + (@($procs | ForEach-Object { "$($_.name):$($_.pid)" }) -join ',') + ' (live/selfloop want a clean start)' }) | Out-Null

  foreach ($port in @($d.sandboxDefaultPort, $d.sandboxAltPort)) {
    $listening = Test-SimPort $port
    Add-SimCheck $run 'DOCTOR' ("sandbox port {0} free" -f $port) (-not $listening) $(if ($listening) { 'busy - domain must use -Port' } else { 'free' }) | Out-Null
  }

  Add-SimCheck $run 'DOCTOR' 'elevation state (live modes need an admin session)' $true $(if (Get-SimElevated) { 'administrator' } else { 'standard user - pass -Elevate for live/selfloop runs' }) | Out-Null

  if ($reg) {
    Write-Host ''
    Write-Host '--- 域注册表 ---' -ForegroundColor White
    foreach ($dom in @($reg.domains)) {
      Write-Host ("  {0,-8} status={1}" -f $dom.domain, $dom.status) -ForegroundColor Gray
      foreach ($m in @($dom.modes | Where-Object { $_ })) { Write-Host ("     mode {0,-10} {1}" -f $m.mode, $m.status) }
      foreach ($s in @($dom.scenarios | Where-Object { $_ })) { Write-Host ("     scenario {0,-12} {1}" -f $s.scenario, $s.status) }
    }
  }

  $sum = Get-SimRunSummary $run
  $run.extra['summary'] = $sum
  $run.extra['registry'] = $reg
  $run.extra['defaults'] = $d
  Save-SimEvidence -Run $run -Prefix 'ymcc-sim-doctor' | Out-Null
  Write-Host ''
  Write-Host ("doctor：{0} 项检查，{1} 失败。" -f $sum.checks, $sum.failed) -ForegroundColor $(if ($sum.failed -gt 0) { 'Red' } else { 'Green' })
  if ($sum.failed -gt 0) { return 2 }
  return 0
}

# ------------------------------------------------------------------- routing --
if ($Domain -eq 'doctor') { exit (Show-Doctor) }

if ($Elevate) {
  if (Get-SimElevated) { Write-Host '已在管理员会话中，-Elevate 无需重入。' -ForegroundColor DarkGray }
  else { exit (Invoke-ElevatedSelf) }
}

# NOTE: never name a local $route here - variable names are case-insensitive and
# the [switch]$Route parameter is strongly typed, so "$route = <object>" would be
# an assignment INTO the switch (ArgumentTransformationMetadataException).
$routing = Get-ChildCommandLine $Domain
if (-not (Test-Path -LiteralPath $routing.script)) {
  Write-Host ("域脚本缺失：{0}" -f $routing.script) -ForegroundColor Red
  exit 3
}
$line = (@($childArgs + $routing.script) + $routing.args) -join ' '
$line = 'powershell ' + $line
Write-Host '=== ymcc_sim（YMCC 全局模拟器）===' -ForegroundColor White
Write-Host ("domain={0}  route={1}" -f $Domain, $line) -ForegroundColor Gray

if ($Route) {
  Write-Output $line
  exit 0
}

# splat the domain args so each token reaches the child as its own argument
$childRest = @($routing.args)
& powershell.exe @childArgs $routing.script @childRest
exit $LASTEXITCODE