param([Parameter(Mandatory=$true)][string]$Out,[string]$HeaderDirectory='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Path $Out -Force|Out-Null
if(!$HeaderDirectory){$HeaderDirectory=Join-Path $repo 'native'}
$recipe=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'steam_cssloader_use_selftest.ps1') -Raw
$recipe=$recipe.Substring($recipe.IndexOf('@"')+3);$recipe=$recipe.Substring(0,$recipe.IndexOf('"@'))
$recipe=$recipe.Replace('$root',$repo).Replace('$OutputDirectory',$Out).Replace('steam_cssloader_use_selftest.cpp','steam_cssloader_job_hierarchy_selftest.cpp').Replace(' /I"'+$repo+'\native"',' /I"'+$HeaderDirectory+'" /I"'+$repo+'\native"')
$bat=Join-Path $Out 'compile.cmd';[IO.File]::WriteAllText($bat,$recipe,[Text.Encoding]::ASCII)
& cmd.exe /d /c $bat 2>&1|Tee-Object -FilePath (Join-Path $Out 'result.log')
if($LASTEXITCODE -ne 0){throw "CSS hierarchy regression failed: $LASTEXITCODE"}
