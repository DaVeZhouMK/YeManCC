[CmdletBinding()]
param([string]$OutputDirectory = '', [switch]$Headless, [switch]$Performance)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot '..\..\..\Testing\YMCC\Evidence\Build\OptiCatalogCost-20261023'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$compile = Join-Path $OutputDirectory ('opti-compile-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $compile | Out-Null
. (Join-Path $PSScriptRoot 'custom_steam_library_build_guard.ps1')
$sourceIndex = @(Get-CustomSteamLibrarySourceIndex -ProjectRoot $projectRoot -Kind host)
if ($Headless -and $Performance) { throw 'Select only one consumer mode' }
$testName = if ($Performance) { 'custom_steam_library_opti_performance_selftest.cpp' } elseif ($Headless) { 'custom_steam_library_opti_headless_selftest.cpp' } else { 'custom_steam_library_opti_catalog_selftest.cpp' }
$testSource = Join-Path $PSScriptRoot $testName
$testHash = (Get-FileHash -LiteralPath $testSource -Algorithm SHA256).Hash
$binaryName = if ($Headless) { 'YeManCC.exe' } else { 'opti_catalog_selftest.exe' }
$executable = Join-Path $compile $binaryName
$builder = Join-Path $compile 'build.cmd'
@"
@echo off
set "PATH=%SystemRoot%\System32;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O1 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /FC "$testSource" /I"$projectRoot\deps\json" /I"$projectRoot\deps\webview2\build\native\include" /Fo"$compile\opti.obj" /Fe"$executable" /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "$projectRoot\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib version.lib bcrypt.lib psapi.lib
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $builder -Encoding ASCII
$output = @(& $env:ComSpec /d /c "`"$builder`"" 2>&1 | ForEach-Object { [string]$_ })
$compileExit = $LASTEXITCODE
$output | Set-Content -LiteralPath (Join-Path $compile 'compiler-output.log') -Encoding UTF8
Assert-CustomSteamLibraryCompilation -ExitCode $compileExit -OutputLines $output -CompilationDirectory $compile -ExpectedExecutables @($binaryName)
Assert-CustomSteamLibrarySourceIndex -ProjectRoot $projectRoot -Kind host -SourceIndex $sourceIndex
if ((Get-FileHash -LiteralPath $testSource -Algorithm SHA256).Hash -ne $testHash) { throw 'Selftest source changed during compilation' }
if ($Performance) {
    foreach ($scenario in @('complete400','import40')) {
        $fixture = Join-Path $OutputDirectory ($scenario + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
        & $executable $fixture $scenario 2>&1 | Tee-Object -FilePath (Join-Path $compile ($scenario + '-output.log'))
        if ($LASTEXITCODE -ne 0) { throw "OPT performance consumer failed: $scenario" }
    }
} else {
    $fixture = Join-Path $OutputDirectory ('opti-fixture-' + [guid]::NewGuid().ToString('N').Substring(0,8))
    & $executable $fixture 2>&1 | Tee-Object -FilePath (Join-Path $compile 'test-output.log')
    if ($LASTEXITCODE -ne 0) { throw 'OPT catalog production-consumer selftest failed' }
}
$sourceIndex | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $compile 'source-index.json') -Encoding UTF8
Write-Output "Evidence: $fixture"
