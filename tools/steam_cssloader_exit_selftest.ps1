param([Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$Out=[IO.Path]::GetFullPath($Out);New-Item -ItemType Directory -Path $Out -Force|Out-Null
$node='G:\YeManCC-Work\Environment\Toolchains\nodejs\node-v22.14.0-win-x64\node.exe'
& $node (Join-Path $PSScriptRoot 'steam_cssloader_exit_extract.mjs') $Out (Join-Path $repo 'native\main.cpp')
if($LASTEXITCODE -ne 0){throw 'Production backend exit extraction failed'}
$recipe=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'steam_cssloader_use_selftest.ps1') -Raw
$recipe=$recipe.Substring($recipe.IndexOf('@"')+3);$recipe=$recipe.Substring(0,$recipe.IndexOf('"@'))
$recipe=$recipe.Replace('$root',$repo).Replace('$OutputDirectory',$Out).Replace('steam_cssloader_use_selftest.cpp','steam_cssloader_exit_selftest.cpp').Replace(' /I"'+$repo+'\native"',' /I"'+$Out+'" /I"'+$repo+'\native" /I"'+$repo+'\deps\json"')
$cmd=Join-Path $Out 'compile.cmd';[IO.File]::WriteAllText($cmd,$recipe,[Text.Encoding]::ASCII)
& $cmd 2>&1|Tee-Object -FilePath (Join-Path $Out 'result.log')
if($LASTEXITCODE -ne 0){throw 'CSS abnormal exit recovery regression failed'}

# Semantic negative keeps the actual production state machine and callbacks but
# disables only the exit-to-delayed-recovery dispatch. It must compile, then fail
# at the real HANDLE-exit admission assertion, never at a source-text assertion.
$negativeInclude=Join-Path $Out 'dispatch-disabled';New-Item -ItemType Directory -Path $negativeInclude -Force|Out-Null
$guard=[IO.File]::ReadAllText((Join-Path $repo 'native\steam_cssloader_guard.h'))
$operand='const bool scheduled = eligible && recovery.admit(GetTickCount64());'
if($guard.Split(@($operand),[StringSplitOptions]::None).Count -ne 2){throw 'Unique actual recovery dispatch required'}
[IO.File]::WriteAllText((Join-Path $negativeInclude 'steam_cssloader_guard.h'),$guard.Replace($operand,'const bool scheduled = false;'),(New-Object Text.UTF8Encoding($false)))
$negativeOut=Join-Path $Out 'negative-run';New-Item -ItemType Directory -Path $negativeOut -Force|Out-Null
$negativeRecipe=$recipe.Replace(' /I"'+$Out+'"',' /I"'+$negativeInclude+'" /I"'+$Out+'"').Replace((Join-Path $Out 'use.obj'),(Join-Path $Out 'negative.obj')).Replace((Join-Path $Out 'use.exe'),(Join-Path $Out 'negative.exe'))
$negativeRun='"'+(Join-Path $Out 'negative.exe')+'" "'+$Out+'"'
if(!$negativeRecipe.Contains($negativeRun)){throw 'Negative compilation recipe boundary missing'}
$negativeRecipe=$negativeRecipe.Replace($negativeRun,'REM negative executes separately under owned output')
$negativeCmd=Join-Path $Out 'compile-negative.cmd';[IO.File]::WriteAllText($negativeCmd,$negativeRecipe,[Text.Encoding]::ASCII)
& $negativeCmd 2>&1|Tee-Object -FilePath (Join-Path $Out 'negative-build.log')
if($LASTEXITCODE -ne 0){throw 'Exit negative must compile before semantic detection'}
$taskPreviousPreference=$ErrorActionPreference;$ErrorActionPreference='Continue'
try{& (Join-Path $Out 'negative.exe') $negativeOut 2>&1|Tee-Object -FilePath (Join-Path $Out 'negative.log');$taskNegativeExit=$LASTEXITCODE}
finally{$ErrorActionPreference=$taskPreviousPreference}
if($taskNegativeExit -ne 1 -or [IO.File]::ReadAllText((Join-Path $Out 'negative.log')) -notmatch 'abnormal process HANDLE exit schedules bounded recovery'){throw 'Disabled exit dispatch was not caught at the actual runtime boundary'}
[pscustomobject]@{detected=$true;compiled=$true;exitCode=$taskNegativeExit;expectedAssertion='abnormal process HANDLE exit schedules bounded recovery';mutation='disable exact production delayed recovery dispatch';realSteamOperations=0;realCssOperations=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Out 'negative-result.json') -Encoding UTF8
Write-Output 'PASS: disabled exit recovery dispatch rejected by actual process HANDLE scenario'
