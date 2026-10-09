[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'custom_steam_library_build_guard.ps1')
$build = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\Build')).TrimEnd('\')
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $root.StartsWith($build + '\', [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $root)) {
  throw 'Use a fresh isolated Mainline Build root for compiler guard fixtures'
}
New-Item -ItemType Directory -Path $root | Out-Null
[IO.File]::WriteAllText((Join-Path $root 'fixture.exe'), 'Fixture bytes only; never executed')
$cases = @()
foreach ($case in @(
  @{name='zero-exit-fatal-internal-compiler-error';exit=0;line='host.cpp(2162) : fatal error C1001: Internal compiler error';exists=$true;reject=$true},
  @{name='zero-exit-normal-compiler-error';exit=0;line='host.cpp(1): error C2065: undefined symbol';exists=$true;reject=$true},
  @{name='zero-exit-linker-error';exit=0;line='LINK : fatal error LNK1120: unresolved symbols';exists=$true;reject=$true},
  @{name='zero-exit-resource-error';exit=0;line='resources.rc(2) : error RC2135: missing resource';exists=$true;reject=$true},
  @{name='nonzero-exit-without-diagnostic';exit=2;line='compile failed';exists=$true;reject=$true},
  @{name='missing-fresh-executable';exit=0;line='OK';exists=$false;reject=$true},
  @{name='normal-compile-with-warning';exit=0;line='host.cpp(1): warning C4100: unused parameter';exists=$true;reject=$false}
)) {
  $rejected=$false
  try {
    Assert-CustomSteamLibraryCompilation -ExitCode $case.exit -OutputLines @($case.line) -CompilationDirectory $root -ExpectedExecutables @(if($case.exists){'fixture.exe'}else{'missing.exe'})
  } catch { $rejected=$true }
  $cases += [ordered]@{name=$case.name;passed=($rejected -eq $case.reject)}
}
$failed=@($cases|Where-Object {-not $_.passed})
$report=[ordered]@{allPassed=($failed.Count -eq 0);caseCount=$cases.Count;failedCount=$failed.Count;cases=$cases;realSteamFilesModified=$false}
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root 'summary.json') -Encoding UTF8
$report|ConvertTo-Json -Depth 8
if($failed.Count){throw 'Compiler publication guard regression failed'}
