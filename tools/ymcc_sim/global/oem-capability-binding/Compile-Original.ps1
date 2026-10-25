param([Parameter(Mandatory=$true)][string]$Config)
$ErrorActionPreference='Stop'
$taskConfig=Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
$taskRoot=[IO.Path]::GetFullPath([string]$taskConfig.directory)
$taskBuild=Join-Path $taskRoot 'build.cmd'
$taskExe=Join-Path $taskRoot 'oem-binding.exe'
if(-not (Test-Path -LiteralPath $taskBuild -PathType Leaf)){throw 'BUILD_COMMAND_MISSING'}
$env:TEMP=Join-Path $taskRoot 'private-temp'
$env:TMP=$env:TEMP
New-Item -ItemType Directory -Path $env:TEMP -Force | Out-Null
Push-Location -LiteralPath $taskRoot
try {
  & $env:ComSpec /d /c $taskBuild
  $taskCompileExit=$LASTEXITCODE
  @{schemaVersion=1;exitCode=$taskCompileExit;hardwareAccess=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskRoot 'compile-result.json')
  if($taskCompileExit -ne 0){exit $taskCompileExit}
  & $taskExe
  exit $LASTEXITCODE
} finally { Pop-Location }
