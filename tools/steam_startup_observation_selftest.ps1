param([Parameter(Mandatory=$true)][string]$Out,[string]$MainSource='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$Out=[IO.Path]::GetFullPath($Out)
if(!$MainSource){$MainSource=Join-Path $repo 'native\main.cpp'}
New-Item -ItemType Directory -Path $Out -Force | Out-Null
& node (Join-Path $PSScriptRoot 'steam_startup_observation_extract.mjs') $Out $MainSource
if($LASTEXITCODE -ne 0){throw 'Production callback extraction failed'}
$recipe=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'steam_cssloader_use_selftest.ps1') -Raw
$recipe=$recipe.Substring($recipe.IndexOf('@"')+3);$recipe=$recipe.Substring(0,$recipe.IndexOf('"@'))
$recipe=$recipe.Replace('$root',$repo).Replace('$OutputDirectory',$Out).Replace('steam_cssloader_use_selftest.cpp','steam_startup_observation_selftest.cpp').Replace(' /I"'+$repo+'\native"',' /I"'+$Out+'" /I"'+$repo+'\native" /I"'+$repo+'\deps\json"')
$bat=Join-Path $Out 'compile.cmd';[IO.File]::WriteAllText($bat,$recipe,[Text.Encoding]::ASCII)
& cmd.exe /d /c $bat 2>&1|Tee-Object -FilePath (Join-Path $Out 'result.log')
if($LASTEXITCODE -ne 0){throw "Production startup observation regression failed: $LASTEXITCODE"}