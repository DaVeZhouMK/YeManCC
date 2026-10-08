[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'

# Windows PowerShell launched by pnpm/pwsh must resolve its own built-in modules first.
$ownModules = Join-Path $PSHOME 'Modules'
if (($env:PSModulePath -split ';')[0].TrimEnd('\') -ine $ownModules.TrimEnd('\')) {
  $env:PSModulePath = $ownModules + ';' + $env:PSModulePath
}
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (!$OutputDirectory) {
  $version = (Get-Content -LiteralPath (Join-Path $root 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json).version
  $OutputDirectory = Join-Path $root ("..\..\Build\Validation\startup-readiness-v{0}-{1}" -f $version, (Get-Date -Format yyyyMMdd))
}
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $out -Force | Out-Null
$oldNodePath = $env:NODE_PATH
Push-Location $root
try {
  $env:NODE_PATH = Join-Path $root 'node_modules'
  & (Join-Path $root 'node_modules\.bin\esbuild.cmd') 'tools/startup_readiness_selftest.ts' --bundle --platform=node --format=cjs --external:esbuild "--outfile=$out\startup_readiness_selftest.cjs"
  if ($LASTEXITCODE -ne 0) { throw 'Startup frontend test compilation failed' }
  & node (Join-Path $out 'startup_readiness_selftest.cjs') | Tee-Object -FilePath (Join-Path $out 'frontend-tests.log')
  if ($LASTEXITCODE -ne 0) { throw 'Startup frontend tests failed' }
  & python -X utf8 'tools/startup_readiness_native_probe.py' $out
  if ($LASTEXITCODE -ne 0) { throw 'Startup observer source extraction failed' }
  $vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
  $commands = @('@echo off', 'where cl.exe >nul 2>nul', "if errorlevel 1 call `"$vcvars`" >nul", 'if errorlevel 1 exit /b 1')
  foreach ($test in @(
    @{ Name='startup_native_test'; Source=(Join-Path $root 'tools\startup_readiness_native_selftest.cpp') },
    @{ Name='startup_native_observer_probe'; Source=(Join-Path $out 'startup_native_observer_probe.cpp') }
  )) {
    $exe = Join-Path $out ($test.Name + '.exe')
    $obj = Join-Path $out ($test.Name + '.obj')
    $commands += "cl /nologo /std:c++17 /EHsc /utf-8 /I`"$(Join-Path $root 'deps/json')`" /Fe:`"$exe`" /Fo:`"$obj`" `"$($test.Source)`""
    $commands += 'if errorlevel 1 exit /b 1'
    $commands += "`"$exe`""
    $commands += 'if errorlevel 1 exit /b 1'
  }
  $commandFile = Join-Path $out 'run-native-tests.cmd'
  [IO.File]::WriteAllLines($commandFile, $commands, [Text.Encoding]::Default)
  & cmd.exe /d /c $commandFile | Tee-Object -FilePath (Join-Path $out 'native-tests.log')
  if ($LASTEXITCODE -ne 0) { throw 'Startup native tests failed' }
  Write-Output 'STARTUP_READINESS_PASS (production logic; no product launch or hardware operations)'
} finally {
  $env:NODE_PATH = $oldNodePath
  Pop-Location
}
