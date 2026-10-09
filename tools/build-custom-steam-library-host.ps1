[CmdletBinding()]
param([string]$OutputDirectory='', [switch]$RunSelfTests, [switch]$Publish)
$ErrorActionPreference='Stop'
# Windows PowerShell launched by pnpm/pwsh must resolve its own modules first.
$ownModules = Join-Path $PSHOME 'Modules'
$firstModulePath = ($env:PSModulePath -split ';')[0]
if (-not $firstModulePath -or $firstModulePath.TrimEnd('\') -ine $ownModules.TrimEnd('\')) {
  $env:PSModulePath = $ownModules + ';' + $env:PSModulePath
}
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $projectRoot '..\..\Build\CustomSteamLibrary' }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$builder=Join-Path $projectRoot 'native\custom-steam-library\build_host.bat'
$testArgument=if($RunSelfTests){'selftest'}else{''}
. (Join-Path $PSScriptRoot 'custom_steam_library_build_guard.ps1')
$sourceIndex = @(Get-CustomSteamLibrarySourceIndex -ProjectRoot $projectRoot -Kind 'host')
$compileDirectory = Join-Path $OutputDirectory ('compile-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $compileDirectory | Out-Null
& $env:ComSpec /d /c "`"$builder`" `"$compileDirectory`" $testArgument" 2>&1 | Tee-Object -Variable compilerOutput
$buildExitCode = $LASTEXITCODE
$buildLines = @($compilerOutput | ForEach-Object { [string]$_ })
$buildLines | Set-Content -LiteralPath (Join-Path $compileDirectory 'compiler-output.log') -Encoding UTF8
$expectedExecutables = @('CustomSteamLibrary.exe')
if ($RunSelfTests) { $expectedExecutables += @('custom_steam_library_host_artwork_selftest.exe','custom_steam_library_host_lifecycle_selftest.exe') }
Assert-CustomSteamLibraryCompilation -ExitCode $buildExitCode -OutputLines $buildLines -CompilationDirectory $compileDirectory -ExpectedExecutables $expectedExecutables
Assert-CustomSteamLibrarySourceIndex -ProjectRoot $projectRoot -Kind 'host' -SourceIndex $sourceIndex
foreach ($exe in $expectedExecutables) { Copy-Item -LiteralPath (Join-Path $compileDirectory $exe) -Destination (Join-Path $OutputDirectory $exe) -Force }
if($RunSelfTests){
  $fixtures=Join-Path $OutputDirectory ('host-artwork-'+[guid]::NewGuid().ToString('N').Substring(0,8))
  & (Join-Path $OutputDirectory 'custom_steam_library_host_artwork_selftest.exe') $fixtures
  if($LASTEXITCODE -ne 0){throw 'Host artwork fixture regression failed'}
  $lifecycle=Join-Path $OutputDirectory ('host-lifecycle-'+[guid]::NewGuid().ToString('N').Substring(0,8))
  & (Join-Path $OutputDirectory 'custom_steam_library_host_lifecycle_selftest.exe') $lifecycle
  if($LASTEXITCODE -ne 0){throw 'Host lifecycle regression failed'}
}
if($Publish){
  Assert-CustomSteamLibraryMainlineIdentity -ProjectRoot $projectRoot
  Assert-CustomSteamLibrarySourceIndex -ProjectRoot $projectRoot -Kind 'host' -SourceIndex $sourceIndex
  $packageRoot=Join-Path $projectRoot 'CustomSteamLibrary'
  $manifestPath=Join-Path $packageRoot 'package-manifest.json'
  $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  # Fresh child builds must inherit the active mainline release, not a stale source manifest.
  $releaseVersion = Get-Content -LiteralPath (Join-Path $projectRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  if ([string]$releaseVersion.version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { throw 'Invalid mainline version for child package' }
  $manifest.packageVersion = [string]$releaseVersion.version
  $entry=@($manifest.fileIndex | Where-Object {$_.path -eq 'CustomSteamLibrary.exe'})
  if($entry.Count -ne 1){throw 'Host manifest entry must be unique'}
  $built=Join-Path $OutputDirectory 'CustomSteamLibrary.exe'
  $hash=(Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash.ToLowerInvariant()
  Copy-Item -LiteralPath $built -Destination (Join-Path $packageRoot 'CustomSteamLibrary.exe') -Force
  foreach($file in @($manifest.fileIndex)){
    $path=[IO.Path]::GetFullPath((Join-Path $packageRoot ([string]$file.path)))
    if(-not $path.StartsWith($packageRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Host manifest path escaped child package'}
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing managed child file: $path"}
    $file.bytes=[int64](Get-Item -LiteralPath $path).Length
    $file.sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  }
  if(-not $manifest.sourceBuild){$manifest | Add-Member -NotePropertyName sourceBuild -NotePropertyValue ([pscustomobject]@{}) -Force}
  $manifest.sourceBuild.host=$hash
  $manifest.sourceBuild | Add-Member -NotePropertyName hostSources -NotePropertyValue $sourceIndex -Force
  $manifest.generatedAt=[DateTimeOffset]::Now.ToString('o')
  $manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
  Write-Output "Published host to $packageRoot ($hash)"
}
Write-Output "HOST_BUILD_OK $OutputDirectory\CustomSteamLibrary.exe"


