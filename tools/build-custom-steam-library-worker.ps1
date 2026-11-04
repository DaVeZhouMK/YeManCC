[CmdletBinding()]
param(
  [string]$OutputDirectory = '',
  [switch]$RunSelfTests,
  [switch]$Publish,
  [switch]$RecordPremainTools,
  [string]$CaptureCompilerTools = ''
)
$ErrorActionPreference = 'Stop'
# Windows PowerShell launched by pnpm/pwsh must resolve its own modules first.
$ownModules = Join-Path $PSHOME 'Modules'
$firstModulePath = ($env:PSModulePath -split ';')[0]
if (-not $firstModulePath -or $firstModulePath.TrimEnd('\') -ine $ownModules.TrimEnd('\')) {
  $env:PSModulePath = $ownModules + ';' + $env:PSModulePath
}
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'custom_steam_library_build_guard.ps1')
if($CaptureCompilerTools){Write-CslCompilerTools $CaptureCompilerTools;exit 0}
if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $projectRoot '..\..\Build\CustomSteamLibrary'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$builder = Join-Path $projectRoot 'native\custom-steam-library\build_worker.bat'
$testArgument = if ($RunSelfTests) { 'selftest' } else { '' }
. (Join-Path $PSScriptRoot 'custom_steam_library_build_guard.ps1')
$sourceIndex = @(Get-CustomSteamLibrarySourceIndex -ProjectRoot $projectRoot -Kind 'worker')
$compileDirectory = Join-Path $OutputDirectory ('compile-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $compileDirectory | Out-Null
$oldToolHost=$env:YMCC_CSL_TOOL_HOST;$oldToolScript=$env:YMCC_CSL_TOOL_SCRIPT
try {
  if($RecordPremainTools){$env:YMCC_CSL_TOOL_HOST=(Get-Process -Id $PID).MainModule.FileName;$env:YMCC_CSL_TOOL_SCRIPT=$PSCommandPath}
  & $env:ComSpec /d /c "`"$builder`" `"$compileDirectory`" $testArgument" 2>&1 | Tee-Object -Variable compilerOutput
  $buildExitCode = $LASTEXITCODE
} finally {$env:YMCC_CSL_TOOL_HOST=$oldToolHost;$env:YMCC_CSL_TOOL_SCRIPT=$oldToolScript}
$buildLines = @($compilerOutput | ForEach-Object { [string]$_ })
$buildLines | Set-Content -LiteralPath (Join-Path $compileDirectory 'compiler-output.log') -Encoding UTF8
$expectedExecutables = @('SteamArtworkLab.exe')
if ($RunSelfTests) { $expectedExecutables += @('custom_steam_library_localconfig_selftest.exe','custom_steam_library_datachain_selftest.exe','custom_steam_library_artwork_tier_selftest.exe','custom_steam_library_worker_lifecycle_selftest.exe','custom_steam_library_http_transport_selftest.exe') }
Assert-CustomSteamLibraryCompilation -ExitCode $buildExitCode -OutputLines $buildLines -CompilationDirectory $compileDirectory -ExpectedExecutables $expectedExecutables
Assert-CustomSteamLibrarySourceIndex -ProjectRoot $projectRoot -Kind 'worker' -SourceIndex $sourceIndex
foreach ($exe in $expectedExecutables) { Copy-Item -LiteralPath (Join-Path $compileDirectory $exe) -Destination (Join-Path $OutputDirectory $exe) -Force }
if($RecordPremainTools){
  $before=Get-Content -LiteralPath (Join-Path $compileDirectory 'compiler-tools-before.json') -Raw -Encoding UTF8|ConvertFrom-Json
  $after=Get-Content -LiteralPath (Join-Path $compileDirectory 'compiler-tools-after.json') -Raw -Encoding UTF8|ConvertFrom-Json
  if($before.schema -ne 1 -or $after.schema -ne 1 -or $before.kind -cne 'CSL_SELECTED_COMPILER_CONTEXT' -or $after.kind -cne $before.kind -or @($before.tools).Count -ne 5 -or ($before.tools|ConvertTo-Json -Compress) -cne ($after.tools|ConvertTo-Json -Compress)){throw 'CSL_STAGE_COMPILER_TOOLS_DRIFT'}
  foreach($p in @($before.tools)){Assert-CslStagePin $p}
  Write-CslStageJson (Join-Path $OutputDirectory 'PREMAIN-COMPILER-TOOLS.json') ([ordered]@{schema=1;tools=@($before.tools);before=(Get-CslStagePin (Join-Path $compileDirectory 'compiler-tools-before.json'));after=(Get-CslStagePin (Join-Path $compileDirectory 'compiler-tools-after.json'));compileLog=([IO.Path]::GetFileName($compileDirectory)+'/compiler-output.log')})
}
if ($RunSelfTests) {
  # Only the test executable virtualizes Steam process enumeration. The shipping
  # worker retains its actual process guard. All test writes stay in Build.
  $fixtures = Join-Path $OutputDirectory ('localconfig-selftest-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
  & (Join-Path $OutputDirectory 'custom_steam_library_localconfig_selftest.exe') $fixtures
  if ($LASTEXITCODE -ne 0) { throw 'Custom Steam Library localconfig regression failed' }
  $dataFixtures = Join-Path $OutputDirectory ('dc-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
  & (Join-Path $OutputDirectory 'custom_steam_library_datachain_selftest.exe') $dataFixtures
  if ($LASTEXITCODE -ne 0) { throw 'Custom Steam Library data-chain regression failed' }
  $artworkFixtures = Join-Path $OutputDirectory ('artwork-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
  & (Join-Path $OutputDirectory 'custom_steam_library_artwork_tier_selftest.exe') $artworkFixtures
  if ($LASTEXITCODE -ne 0) { throw 'Custom Steam Library artwork priority regression failed' }
  $lifecycleFixtures = Join-Path $OutputDirectory ('worker-lifecycle-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
  & (Join-Path $OutputDirectory 'custom_steam_library_worker_lifecycle_selftest.exe') $lifecycleFixtures
  if ($LASTEXITCODE -ne 0) { throw 'Custom Steam Library worker lifecycle regression failed' }
  $httpFixtures = Join-Path $OutputDirectory ('http-transport-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
  & (Join-Path $OutputDirectory 'custom_steam_library_http_transport_selftest.exe') $httpFixtures
  if ($LASTEXITCODE -ne 0) { throw 'Custom Steam Library raw HTTP transport regression failed' }
  & node (Join-Path $projectRoot 'tools\custom_steam_library_ui_datachain_selftest.mjs') (Join-Path $OutputDirectory 'ui-datachain-summary.json')
  if ($LASTEXITCODE -ne 0) { throw 'Custom Steam Library UI data-chain regression failed' }
  & (Join-Path $projectRoot 'tools\custom_steam_library_cli_datachain_selftest.ps1') -Worker (Join-Path $OutputDirectory 'SteamArtworkLab.exe') -OutputDirectory (Join-Path $OutputDirectory ('cli-' + [guid]::NewGuid().ToString('N').Substring(0, 8)))
  if ($LASTEXITCODE -ne 0) { throw 'Custom Steam Library CLI data-chain regression failed' }
  $worker = Join-Path $OutputDirectory 'SteamArtworkLab.exe'
  foreach ($test in @('--steam-recent-signal-self-test', '--artwork-quality-self-test', '--identity-evidence-self-test', '--steam-search-query-self-test', '--steam-base-consensus-self-test', '--release-year-self-test')) {
    & $worker $test
    if ($LASTEXITCODE -ne 0) { throw "Worker self-test failed: $test" }
  }
}
if ($Publish) {
  Assert-CustomSteamLibraryMainlineIdentity -ProjectRoot $projectRoot
  Assert-CustomSteamLibrarySourceIndex -ProjectRoot $projectRoot -Kind 'worker' -SourceIndex $sourceIndex
  # Publish the worker and refresh managed-file hashes in its child manifest
  # to the canonical source
  # package; never deploy, restart Steam, or modify runtime/user data here.
  $packageRoot = Join-Path $projectRoot 'CustomSteamLibrary'
  $manifestPath = Join-Path $packageRoot 'package-manifest.json'
  $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  # Fresh child builds must inherit the active mainline release, not a stale source manifest.
  $releaseVersion = Get-Content -LiteralPath (Join-Path $projectRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  if ([string]$releaseVersion.version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { throw 'Invalid mainline version for child package' }
  $manifest.packageVersion = [string]$releaseVersion.version
  $entry = @($manifest.fileIndex | Where-Object { $_.path -eq 'SteamArtworkLab.exe' })
  if ($entry.Count -ne 1) { throw 'Worker manifest entry must be unique' }
  $builtWorker = Join-Path $OutputDirectory 'SteamArtworkLab.exe'
  $hash = (Get-FileHash -LiteralPath $builtWorker -Algorithm SHA256).Hash.ToLowerInvariant()
  Copy-Item -LiteralPath $builtWorker -Destination (Join-Path $packageRoot 'SteamArtworkLab.exe') -Force
  $entry[0].bytes = [int64](Get-Item -LiteralPath $builtWorker).Length
  $entry[0].sha256 = $hash
  foreach ($file in @($manifest.fileIndex)) {
    $path = [IO.Path]::GetFullPath((Join-Path $packageRoot ([string]$file.path)))
    if (-not $path.StartsWith($packageRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escaped canonical child package' }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Managed child file missing: $path" }
    $file.bytes = [int64](Get-Item -LiteralPath $path).Length
    $file.sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  }
  $manifest.sourceBuild.worker = $hash
  $manifest.sourceBuild | Add-Member -NotePropertyName workerSources -NotePropertyValue $sourceIndex -Force
  $manifest.generatedAt = [DateTimeOffset]::Now.ToString('o')
  $manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
  Write-Output "Published worker to $packageRoot ($hash)"
}
Write-Output "WORKER_BUILD_OK $OutputDirectory\SteamArtworkLab.exe"
