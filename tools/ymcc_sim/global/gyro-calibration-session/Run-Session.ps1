param([Parameter(Mandatory=$true)][string]$Config)
$ErrorActionPreference='Stop'
$taskConfig=Get-Content -LiteralPath $Config -Raw | ConvertFrom-Json
$taskRoot=[IO.Path]::GetFullPath([string]$taskConfig.root)
$taskNode=[IO.Path]::GetFullPath([string]$taskConfig.node)
$taskWorker=[IO.Path]::GetFullPath([string]$taskConfig.worker)
$env:TEMP=Join-Path $taskRoot 'private-temp'
$env:TMP=$env:TEMP
New-Item -ItemType Directory -Path $env:TEMP -Force | Out-Null
Remove-Item Env:NODE_OPTIONS -ErrorAction SilentlyContinue
Remove-Item Env:NODE_PATH -ErrorAction SilentlyContinue
Remove-Item Env:ESBUILD_BINARY_PATH -ErrorAction SilentlyContinue
& $taskNode $taskWorker --config $Config
exit $LASTEXITCODE
