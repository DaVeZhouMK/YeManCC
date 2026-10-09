# Read and exercise only the pure pin planners. No build, disk mutation or hardware access.
[CmdletBinding()]
param([string]$ProjectRoot = '')
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$tool = Join-Path $ProjectRoot 'tools\rebaseline-fan-host-payload.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($tool, [ref]$tokens, [ref]$errors)
if ($errors.Count -gt 0) { throw 'rebaseline tool parse failed' }
foreach ($name in @('Set-Pin', 'Set-NativeHcPin')) {
  $definitions = @($ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name }, $true))
  if ($definitions.Count -ne 1) { throw "expected one planner: $name" }
  . ([scriptblock]::Create($definitions[0].Extent.Text))
}
$source = [IO.File]::ReadAllText((Join-Path $ProjectRoot 'native\main.cpp'))
$hash = 'A' * 64
$checks = 0
$plan = Set-NativeHcPin $source $hash
if ($plan.Text -cne $source -or $plan.NewAudit -ne 'incoming-runtime-manifest (unchanged)') { throw 'incoming layout must remain byte-equivalent' }
$checks++
$guards = @(
  '(Get-FileHash -LiteralPath $runtimeManifestPath -Algorithm SHA256).Hash -ine ([string]$binding.runtimeManifestSha256)',
  '(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine ([string]$entry.sha256)',
  '$hc.Count -ne 1 -or ([string]$hc[0].sha256) -notmatch ''^[0-9A-Fa-f]{64}$'''
)
foreach ($guard in $guards) {
  $mutant = $source.Replace($guard, 'deliberately-removed-guard')
  $caught = $false
  try { $null = Set-NativeHcPin $mutant $hash } catch { $caught = $true }
  if (!$caught) { throw 'incomplete incoming runtime validation was accepted' }
  $checks++
}
$legacy = '([string]$hc[0].sha256) -ine ''' + ('B' * 64) + ''''
$legacyPlan = Set-NativeHcPin $legacy $hash
if ($legacyPlan.Text -cne ('([string]$hc[0].sha256) -ine ''' + $hash + '''')) { throw 'legacy pin was not updated' }
$checks++
foreach ($bad in @(($legacy + "`n" + $legacy), 'unrecognized native validation')) {
  $caught = $false
  try { $null = Set-NativeHcPin $bad $hash } catch { $caught = $true }
  if (!$caught) { throw 'ambiguous or unknown layout was accepted' }
  $checks++
}
Write-Output "FAN_REBASELINE_LAYOUT_SELFTEST_PASS: $checks checks"
