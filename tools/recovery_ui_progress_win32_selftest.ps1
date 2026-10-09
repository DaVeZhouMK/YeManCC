<# Build/run the owned Win32 UI-stall fixture. No deployment, hardware or real process kill. #>
[CmdletBinding()]
param([string]$OutputDirectory = '../../Build/Validation/RecoveryUiProgress')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory = Join-Path $repo $OutputDirectory }
$out = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $out -Force | Out-Null
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw 'Visual Studio x64 BuildTools not found' }
$source = Join-Path $PSScriptRoot 'recovery_ui_progress_win32_selftest.cpp'
$exe = Join-Path $out 'recovery-ui-fixture.exe'
$object = Join-Path $out 'recovery-ui-fixture.obj'
$recipe = Join-Path $out 'compile-recovery-ui-fixture.cmd'
$lines = @('@echo off', "call `"$vcvars`" >nul", 'if errorlevel 1 exit /b 1',
  "cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 `"$source`" /Fo`"$object`" /Fe`"$exe`" /link user32.lib shell32.lib ole32.lib powrprof.lib")
Set-Content -LiteralPath $recipe -Value $lines -Encoding ascii
& cmd.exe /d /c $recipe
if ($LASTEXITCODE -ne 0) { throw 'Owned UI recovery fixture failed to compile' }
$stdout = Join-Path $out 'recovery-ui-fixture.stdout.log'
$stderr = Join-Path $out 'recovery-ui-fixture.stderr.log'
$process = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
# Retain the handle before Windows PowerShell 5 waits; otherwise ExitCode can be null.
$null = $process.Handle
if (-not $process.WaitForExit(60000)) {
  $process.Kill() # Only the exact fixture process created above, never a real YMCC/game.
  throw 'Owned UI recovery fixture exceeded its 60-second bound'
}
$process.WaitForExit()
Get-Content -LiteralPath $stdout
if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr }
if ($process.ExitCode -ne 0) { throw "Owned UI recovery fixture failed: $($process.ExitCode)" }
