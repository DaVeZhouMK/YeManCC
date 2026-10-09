[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$CandidateRoot)
$ErrorActionPreference = 'Stop'
# Match the production scripts' PowerShell 5.1 module-path self-heal.
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath
# Execute only the actual read-only integrity statements extracted from the
# production packager AST. Never invoke packaging/release/deploy script bodies.
$project = Split-Path -Parent $PSScriptRoot
$root = [IO.Path]::GetFullPath($CandidateRoot)
$vgComplete = Join-Path $root 'PowerControl\feature-assets\virtual-gamepad'
$hcRuntimeComplete = Join-Path $root 'PowerControl\handheldcompanion-runtime\HC-CANDIDATE-0.32.4.0-06c0b954-20260902'
function Get-Sha256([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
$tokens=$null; $errors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'package-release.ps1'),[ref]$tokens,[ref]$errors)
if (@($errors).Count -ne 0) { throw 'Package parser errors.' }
foreach ($name in @('sharedRuntimeFallbackHashes','sharedRuntimeFallbackDlls','ymccHostFiles','sharedHostDlls')) {
  $nodes=@($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -eq ('$'+$name) },$true))
  if ($nodes.Count -ne 1) { throw "Ambiguous production assignment: $name" }
  Invoke-Expression $nodes[0].Extent.Text
}
$hostDepsPath = Join-Path $vgComplete 'YeManInputHost.deps.json'
$hostDeps = Get-Content -LiteralPath $hostDepsPath -Raw -Encoding UTF8 | ConvertFrom-Json
$declaredRuntime=@()
foreach ($target in @($hostDeps.targets.PSObject.Properties)) {
 foreach ($pkg in @($target.Value.PSObject.Properties)) {
  if ($null -ne $pkg.Value.runtime) { $declaredRuntime+=@($pkg.Value.runtime.PSObject.Properties.Name) }
 }
}
$declaredRuntime=@($declaredRuntime | Sort-Object -Unique)
$selectors=@('Pinned shared fallback missing or changed:','Complete package virtual-gamepad bundle is incomplete:','Complete package YeManInputHost.deps.json runtime file is missing at its approved location:')
foreach ($selector in $selectors) {
 $nodes=@($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.ForEachStatementAst] -and $n.Extent.Text.Contains($selector) },$true))
 if ($nodes.Count -ne 1) { throw "Ambiguous integrity statement: $selector" }
 Invoke-Expression $nodes[0].Extent.Text
}
# Verify the same filename/hash mapping remains in deploy/config/production C#.
foreach ($script in @('deploy-installed.ps1')) {
 $other=[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $script),[ref]$tokens,[ref]$errors)
 $node=@($other.FindAll({ param($n) $n -is [System.Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -eq '$sharedRuntimeFallbackHashes' },$true))
 $otherMap=Invoke-Expression $node[0].Right.Extent.Text
 foreach ($name in $sharedRuntimeFallbackDlls) { if ($otherMap[$name] -cne $sharedRuntimeFallbackHashes[$name]) { throw "Map drift: $name" } }
}
$config=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'opt-baseline.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$cs=Get-Content -LiteralPath (Join-Path $project 'InputHost\RuntimeSharedDependencies.cs') -Raw -Encoding UTF8
foreach ($name in $sharedRuntimeFallbackDlls) {
 if ($config.sharedRuntimeRoutes.sha256.$name -cne $sharedRuntimeFallbackHashes[$name] -or -not $cs.Contains($sharedRuntimeFallbackHashes[$name])) { throw "Resolver/config pin drift: $name" }
}
Write-Output "PACKAGING_SHARED_RUNTIME_PASS runtimeDeclarations=$($declaredRuntime.Count) NOT_DEVICE_PASS"
