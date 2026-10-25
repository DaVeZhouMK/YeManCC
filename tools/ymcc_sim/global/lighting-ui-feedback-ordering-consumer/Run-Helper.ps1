[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ConfigPath)
$ErrorActionPreference='Stop'
$taskConfig=Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$env:YMCC_LIGHTING_OBSERVATION=$ConfigPath
$env:NODE_OPTIONS='--require "'+($taskConfig.observer.Replace('\','/'))+'"'
$env:ESBUILD_BINARY_PATH=$taskConfig.esbuildBinary
Remove-Item Env:NODE_PATH -ErrorAction SilentlyContinue
Set-Location -LiteralPath $taskConfig.repo
$helperArguments=@($taskConfig.helperArguments)
& $taskConfig.node $taskConfig.helper @helperArguments
exit $LASTEXITCODE
