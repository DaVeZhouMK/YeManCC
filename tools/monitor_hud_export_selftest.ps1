# Exercises the production export functions on copies of the actual task build.
# Never calls deploy-installed.ps1, starts product/game processes, or reads hardware.
[CmdletBinding()]
param([string]$CandidateRoot = 'G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\desktop-output-20261011\runtime-handoff-20261011')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'monitor-hud-export.ps1')
$out = Join-Path $CandidateRoot ('export-selftest-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $out | Out-Null
$buildWeb = Join-Path $CandidateRoot 'Build\App\Web'
$buildPlugin = Join-Path $CandidateRoot 'Build\App\Decky\plugins\ymcc-sidebar'
$script:cases = @()
function Assert-True([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message } }
function Check([string]$Name, [scriptblock]$Test) { & $Test; $script:cases += $Name }
function Reject([scriptblock]$Test, [string]$Reason) {
  $message = $null
  try { & $Test | Out-Null } catch { $message = $_.Exception.Message }
  Assert-True ([bool]$message) 'Expected export guard to reject mutation'
  Assert-True ($message -match $Reason) ("Wrong rejection: $message")
}
function Web-Case([string]$Name) {
  $target = Join-Path $out $Name
  Copy-Item -LiteralPath $buildWeb -Destination $target -Recurse
  return $target
}
function Plugin-Case([string]$Name) {
  $target = Join-Path $out $Name
  Copy-Item -LiteralPath $buildPlugin -Destination $target -Recurse
  return $target
}
Check 'Actual built main and independent HUD documents resolve all JS/CSS/preload entries' {
  $r = Assert-MonitorHudWebExport $buildWeb
  Assert-True ($r.pages.Count -eq 2 -and $r.assetCount -gt 0 -and $r.referencedAssets.Count -gt 1) 'Missing production web entries'
}
Check 'Complete isolated frontend copy has byte-identical pages and ALL chunks' {
  $web = Web-Case 'same-web'
  Assert-MonitorHudWebExport $web $buildWeb | Out-Null
}
Check 'Missing independent HUD HTML is rejected, not reported as a deployable runtime' {
  $web = Web-Case 'missing-hud'
  Remove-Item -LiteralPath (Get-MonitorHudSafeExportPath $web 'hud-output.html')
  Reject { Assert-MonitorHudWebExport $web } 'entry missing'
}
Check 'Missing independent HUD module is rejected before runtime startup' {
  $web = Web-Case 'missing-module'
  $html = Get-Content -LiteralPath (Join-Path $web 'hud-output.html') -Raw
  $relative = [regex]::Match($html,'<script\b[^>]*\bsrc="\./([^"]+)"').Groups[1].Value
  Remove-Item -LiteralPath (Get-MonitorHudSafeExportPath $web $relative)
  Reject { Assert-MonitorHudWebExport $web } 'reference missing'
}
Check 'Changed HUD HTML cannot silently bind a different generation' {
  $web = Web-Case 'changed-html'
  Add-Content -LiteralPath (Join-Path $web 'hud-output.html') -Value '<!-- mismatch -->'
  Reject { Assert-MonitorHudWebExport $web $buildWeb } 'HTML differs'
}
Check 'Shared HUD renderer chunk is hash checked, not only main entry.js' {
  $web = Web-Case 'changed-shared-chunk'
  $chunk = Get-ChildItem -LiteralPath (Join-Path $web 'assets') -File | Where-Object { $_.Name -like 'MonitorHudTemplate-*.js' } | Select-Object -First 1
  Assert-True ([bool]$chunk) 'Production shared renderer chunk not found'
  Add-Content -LiteralPath $chunk.FullName -Value '/* mixed generation */'
  Reject { Assert-MonitorHudWebExport $web $buildWeb } 'asset differs'
}
Check 'Changed HUD stylesheet is rejected just like changed JS' {
  $web = Web-Case 'changed-css'
  $css = Get-ChildItem -LiteralPath (Join-Path $web 'assets') -Filter 'hud-*.css' -File | Select-Object -First 1
  Add-Content -LiteralPath $css.FullName -Value 'body{opacity:0}'
  Reject { Assert-MonitorHudWebExport $web $buildWeb } 'asset differs'
}
Check 'Old extra hashed asset is rejected instead of kept after install' {
  $web = Web-Case 'stale-asset'
  Set-Content -LiteralPath (Join-Path $web 'assets\old-generation.js') -Value '// stale'
  Reject { Assert-MonitorHudWebExport $web $buildWeb } 'asset set differs'
}
Check 'HTML asset traversal cannot escape the explicitly named export root' {
  $web = Web-Case 'traversal'
  Add-Content -LiteralPath (Join-Path $web 'hud-output.html') -Value '<script src="./assets/../../outside.js"></script>'
  Reject { Assert-MonitorHudWebExport $web } 'escapes'
}
Check 'Reparse aliases are rejected by the same path checker used before recursive moves' {
  $alias = Join-Path $out 'web-alias'
  New-Item -ItemType Junction -Path $alias -Target $buildWeb | Out-Null
  Reject { Assert-MonitorHudWebExport $alias } 'reparse-point'
}
Check 'Actual formal plugin requires precisely five immutable files and correct identity' {
  $r = Assert-MonitorHudDeckyExport $buildPlugin
  Assert-True ($r.files.Count -eq 5 -and $r.name -eq 'ymcc-sidebar') 'Wrong plugin payload'
}
Check 'Old backend/extra plugin payload is not allowed to coexist with the formal build' {
  $plugin = Plugin-Case 'extra-backend'
  Set-Content -LiteralPath (Join-Path $plugin 'main.py') -Value '# never executed'
  Reject { Assert-MonitorHudDeckyExport $plugin } 'exact immutable'
}
Check 'Changed plugin JS cannot pass a matching-build check' {
  $plugin = Plugin-Case 'wrong-plugin-js'
  Add-Content -LiteralPath (Join-Path $plugin 'dist\index.js') -Value '/* old plugin */'
  Reject { Assert-MonitorHudDeckyExport $plugin $buildPlugin } 'differs from build'
}
Check 'Missing Vue license fails the immutable plugin allowlist' {
  $plugin = Plugin-Case 'missing-license'
  Remove-Item -LiteralPath (Get-MonitorHudSafeExportPath $plugin 'LICENSE.vue')
  Reject { Assert-MonitorHudDeckyExport $plugin } 'payload missing'
}
Check 'Foreign plugin manifest is rejected even with the correct filenames' {
  $plugin = Plugin-Case 'foreign-plugin'
  Set-Content -LiteralPath (Join-Path $plugin 'plugin.json') -Value '{"name":"another-plugin"}'
  Reject { Assert-MonitorHudDeckyExport $plugin } 'plugin identity'
}
Check 'Previous ACTUAL installed plugin is backed up before stale ordinary source assets overwrite it' {
  $pc = Join-Path $out 'install\PowerControl'
  $backup = Join-Path $out 'install\YeManCC\.deployment-backup-test'
  $oldPlugin = Join-Path $pc 'decky\plugins\ymcc-sidebar'
  New-Item -ItemType Directory -Force -Path (Join-Path $oldPlugin 'dist') | Out-Null
  Set-Content -LiteralPath (Join-Path $oldPlugin 'dist\index.js') -Value 'ACTUAL-OLD-INSTALLED'
  Set-Content -LiteralPath (Join-Path $oldPlugin 'main.py') -Value 'obsolete-backend'
  $oldHash = Get-MonitorHudExportHash (Join-Path $oldPlugin 'dist\index.js')
  Move-MonitorHudDeckyBackup $pc $backup
  Assert-True (-not (Test-Path -LiteralPath $oldPlugin)) 'Old plugin was not moved away'
  $saved = Join-Path $backup 'PowerControl\decky\plugins\ymcc-sidebar\dist\index.js'
  Assert-True ((Get-MonitorHudExportHash $saved) -eq $oldHash) 'Lost original installed plugin bytes'
  # Simulate the existing ordinary source-asset copy, which is not authoritative.
  Copy-Item -LiteralPath $buildPlugin -Destination $oldPlugin -Recurse
  Set-Content -LiteralPath (Join-Path $oldPlugin 'dist\index.js') -Value 'STALE-SOURCE-TREE-DIST'
  Copy-MonitorHudDeckyExport $buildPlugin $pc
  Assert-MonitorHudDeckyExport $oldPlugin $buildPlugin | Out-Null
  Assert-True ((Get-MonitorHudExportHash $saved) -eq $oldHash) 'Original backup was overwritten'
  Assert-True (-not (Test-Path -LiteralPath (Join-Path $oldPlugin 'main.py'))) 'Old backend leaked into new plugin'
}
Check 'Repeated plugin backup cannot merge into or overwrite the saved generation' {
  $pc = Join-Path $out 'install\PowerControl'
  $backup = Join-Path $out 'install\YeManCC\.deployment-backup-test'
  Reject { Move-MonitorHudDeckyBackup $pc $backup } 'backup already exists'
}
Check 'Deploy integration orders preflight, original backup, built plugin copy and post-copy byte proof' {
  $source = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'deploy-installed.ps1') -Raw
  $pre = $source.IndexOf('Assert-MonitorHudWebExport $WebBuild')
  $move = $source.IndexOf('Move-MonitorHudDeckyBackup $installedPowerControl $BackupRoot')
  $ordinary = $source.IndexOf('Copy-InstalledPowerControlMainAssets $sourcePowerControl $installedPowerControl')
  $copy = $source.IndexOf('Copy-MonitorHudDeckyExport $DeckyBuild $installedPowerControl')
  $post = $source.IndexOf('Assert-MonitorHudWebExport $InstallRoot $WebBuild')
  Assert-True ($pre -gt 0 -and $move -gt $pre -and $ordinary -gt $move -and $copy -gt $ordinary -and $post -gt $copy) 'Deploy proof is not integrated in the required order'
  Assert-True ($source.Contains('--out $DeckyBuild --check')) 'No compiled plugin/source byte check before install'
  $errors = $null; $tokens = $null
  [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'deploy-installed.ps1'),[ref]$tokens,[ref]$errors) | Out-Null
  Assert-True (-not $errors) 'Deployment script syntax invalid'
}
Check 'Build/package/deploy chains all enforce the same actual HUD export contract' {
  $build = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build-workspace.ps1') -Raw
  $package = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'package-release.ps1') -Raw
  $deploy = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'deploy-installed.ps1') -Raw
  Assert-True ($build.Contains('Assert-MonitorHudWebExport $WebBuild') -and $build.Contains('Assert-MonitorHudDeckyExport $DeckyBuild')) 'Missing build output contract'
  Assert-True ($package.Contains('Assert-MonitorHudWebExport $StagingYeManCC $BuildWeb') -and $package.Contains('-BuiltPluginRoot $BuildDecky')) 'Missing staging/final archive contract'
  Assert-True ($deploy.Contains('Assert-ExportBuildIdentity -ProjectRoot $ProjectRoot -WorkspaceRoot $WorkspaceRoot') -and $deploy.Contains('Assert-ExportStagedBuild (Split-Path -Parent $InstallRoot) $exportBuildIdentity')) 'Missing byte identity link from build to real installation'
  foreach ($name in @('build-workspace.ps1','package-release.ps1','deploy-installed.ps1')) {
    $errors = $null; $tokens = $null
    [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $name),[ref]$tokens,[ref]$errors) | Out-Null
    Assert-True (-not $errors) ('Export script syntax invalid: ' + $name)
  }
}
Check 'Actual ordinary install policy accepts all seven Decky files before generic dist/JSON rules' {
  $tokens=$null; $errors=$null
  $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'deploy-installed.ps1'),[ref]$tokens,[ref]$errors)
  foreach($name in @('Get-FullPath','Get-RelativePath','Test-InstalledPowerControlExcluded','Copy-InstalledPowerControlMainAssets')) {
    $nodes=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
    Assert-True ($nodes.Count -eq 1) ('Ambiguous install function: '+$name)
    . ([scriptblock]::Create($nodes[0].Extent.Text))
  }
  foreach($relative in @('decky\PluginLoader_noconsole.exe','decky\LICENSE.decky-loader','decky\plugins\ymcc-sidebar\dist\index.js','decky\plugins\ymcc-sidebar\package.json','decky\plugins\ymcc-sidebar\plugin.json','decky\plugins\ymcc-sidebar\LICENSE.decky-api','decky\plugins\ymcc-sidebar\LICENSE.vue')) {
    Assert-True (-not (Test-InstalledPowerControlExcluded $relative $false)) ('Formal Decky payload wrongly excluded: '+$relative)
  }
  foreach($relative in @('decky','decky\plugins','decky\plugins\ymcc-sidebar','decky\plugins\ymcc-sidebar\dist')) {
    Assert-True (-not (Test-InstalledPowerControlExcluded $relative $true)) ('Formal Decky directory wrongly excluded: '+$relative)
  }
}
Check 'Install policy excludes Decky runtime settings, foreign plugins, old backends and extra binaries' {
  $tokens=$null; $errors=$null; $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'deploy-installed.ps1'),[ref]$tokens,[ref]$errors)
  $node=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Test-InstalledPowerControlExcluded'},$true))[0]
  . ([scriptblock]::Create($node.Extent.Text))
  foreach($relative in @('decky\settings.json','decky\plugins\foreign\dist\index.js','decky\plugins\ymcc-sidebar\main.py','decky\plugins\ymcc-sidebar\dist\extra.dll','decky\plugins\ymcc-sidebar\runtime.json')) {
    Assert-True (Test-InstalledPowerControlExcluded $relative $false) ('Unexpected Decky file accepted: '+$relative)
  }
  Reject { Test-InstalledPowerControlExcluded 'unknown-business.json' $false } 'Unclassified'
}
Check 'Actual ordinary copy function carries the production Decky frontend and licenses, not only a mocked helper' {
  $tokens=$null; $errors=$null; $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'deploy-installed.ps1'),[ref]$tokens,[ref]$errors)
  foreach($name in @('Get-FullPath','Get-RelativePath','Test-InstalledPowerControlExcluded','Copy-InstalledPowerControlMainAssets')) {
    $node=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))[0]
    . ([scriptblock]::Create($node.Extent.Text))
  }
  $source=Join-Path $out 'ordinary-source';$destination=Join-Path $out 'ordinary-installed'
  $plugin=Join-Path $source 'decky\plugins\ymcc-sidebar';New-Item -ItemType Directory -Force -Path (Split-Path -Parent $plugin)|Out-Null
  Copy-Item -LiteralPath $buildPlugin -Destination $plugin -Recurse
  $locked=Join-Path $CandidateRoot 'Source\YeManCC\PowerControl\decky'
  foreach($name in @('PluginLoader_noconsole.exe','LICENSE.decky-loader')){Copy-Item -LiteralPath (Join-Path $locked $name) -Destination (Join-Path $source ('decky\'+$name))}
  Set-Content -LiteralPath (Join-Path $plugin 'runtime.json') -Value '{"secret":"fixture"}'
  Copy-InstalledPowerControlMainAssets $source $destination
  Assert-MonitorHudDeckyExport (Join-Path $destination 'decky\plugins\ymcc-sidebar') $buildPlugin|Out-Null
  Assert-True (Test-Path -LiteralPath (Join-Path $destination 'decky\PluginLoader_noconsole.exe')) 'Locked loader absent from actual ordinary copy'
  Assert-True (-not (Test-Path -LiteralPath (Join-Path $destination 'decky\plugins\ymcc-sidebar\runtime.json'))) 'Runtime settings copied as immutable payload'
}
Check 'Whole actual ordinary PowerControl material matches the formal immutable appearance policy' {
  $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'deploy-installed.ps1'),[ref]$tokens,[ref]$errors)
  foreach($name in @('Get-FullPath','Get-Sha256','Get-RelativePath','Test-InstalledPowerControlExcluded','Copy-InstalledPowerControlMainAssets')) {
    $node=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))[0]
    . ([scriptblock]::Create($node.Extent.Text))
  }
  $ProjectRoot=Join-Path $CandidateRoot 'Source\YeManCC';$destination=Join-Path $out 'whole-ordinary-source-copy'
  Copy-InstalledPowerControlMainAssets (Join-Path $ProjectRoot 'PowerControl') $destination
  Assert-MonitorHudDeckyExport (Join-Path $destination 'decky\plugins\ymcc-sidebar') $buildPlugin|Out-Null
  Assert-True (Test-Path -LiteralPath (Join-Path $destination 'steam-appearance\manifest.json')) 'Bundled immutable appearance manifest missing'
  Reject {Test-InstalledPowerControlExcluded 'steam-appearance\runtime-user.json' $false} 'Unclassified Steam appearance'
}
$report = [ordered]@{ passed = $script:cases.Count; cases = $script:cases; out = $out; cpuMeasured = $false; hardwareTouched = $false; actualInstallChanged = $false; productOrGameLaunched = $false; scope = 'Actual production export verification/copy/backup functions exercised against isolated copies of the real built frontend/plugin. Main deploy script integration is source/AST checked, NOT a full product deployment or game/input acceptance.' }
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'EXPORT-SELFTEST.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 6
