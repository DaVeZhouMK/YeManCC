param([Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$Out=[IO.Path]::GetFullPath($Out);New-Item -ItemType Directory -Path $Out -Force|Out-Null
$node='G:\YeManCC-Work\Environment\Toolchains\nodejs\node-v22.14.0-win-x64\node.exe'
& $node (Join-Path $PSScriptRoot 'steam_cssloader_resume_extract.mjs') $Out (Join-Path $repo 'native\main.cpp')
if($LASTEXITCODE -ne 0){throw 'Production Ready extraction failed'}
$recipe=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'steam_cssloader_use_selftest.ps1') -Raw
$recipe=$recipe.Substring($recipe.IndexOf('@"')+3);$recipe=$recipe.Substring(0,$recipe.IndexOf('"@'))
$recipe=$recipe.Replace('$root',$repo).Replace('$OutputDirectory',$Out).Replace('steam_cssloader_use_selftest.cpp','steam_cssloader_resume_selftest.cpp').Replace(' /I"'+$repo+'\native"',' /I"'+$Out+'" /I"'+$repo+'\native" /I"'+$repo+'\deps\json"')
$cmd=Join-Path $Out 'compile.cmd';[IO.File]::WriteAllText($cmd,$recipe,[Text.Encoding]::ASCII)
& $cmd 2>&1|Tee-Object -FilePath (Join-Path $Out 'result.log')
if($LASTEXITCODE -ne 0){throw 'CSS committed resume regression failed'}

# Semantic negative: keep the exact compiled callback but suppress its CSS
# recovery dispatch. It must fail at the named Ready admission assertion.
$header=Join-Path $Out 'production-css-resume.h'
$negativeHeader=Join-Path $Out 'production-css-resume-disabled.h'
$text=[IO.File]::ReadAllText($header)
$operand='const bool requested = g_steamCssGuard.requestResumeRecovery(session, generation);'
if(($text.Split(@($operand),[StringSplitOptions]::None)).Count -ne 2){throw 'Unique production resume dispatch operand required'}
[IO.File]::WriteAllText($negativeHeader,$text.Replace($operand,'const bool requested = false;'),(New-Object Text.UTF8Encoding($false)))
$negativeSource=Join-Path $Out 'negative.cpp'
[IO.File]::WriteAllText($negativeSource,([IO.File]::ReadAllText((Join-Path $repo 'tools\steam_cssloader_resume_selftest.cpp')).Replace('#include "production-css-resume.h"','#include "production-css-resume-disabled.h"')),(New-Object Text.UTF8Encoding($false)))
$negativeRecipe=$recipe.Replace((Join-Path $repo 'tools\steam_cssloader_resume_selftest.cpp'),$negativeSource).Replace((Join-Path $Out 'use.obj'),(Join-Path $Out 'negative.obj')).Replace((Join-Path $Out 'use.exe'),(Join-Path $Out 'negative.exe'))
$negativeRun='"'+(Join-Path $Out 'negative.exe')+'" "'+$Out+'"'
if(!$negativeRecipe.Contains($negativeRun)){throw 'Negative compilation recipe boundary missing'}
$negativeRecipe=$negativeRecipe.Replace($negativeRun,'REM semantic negative executes under a separate owned output directory')
$negativeCmd=Join-Path $Out 'compile-negative.cmd';[IO.File]::WriteAllText($negativeCmd,$negativeRecipe,[Text.Encoding]::ASCII)
& $negativeCmd 2>&1|Tee-Object -FilePath (Join-Path $Out 'negative-build.log')
if($LASTEXITCODE -ne 0){throw 'Negative must compile before semantic detection'}
$negativeOut=Join-Path $Out 'disabled-dispatch';New-Item -ItemType Directory -Path $negativeOut -Force|Out-Null
$taskPreviousPreference=$ErrorActionPreference;$ErrorActionPreference='Continue'
try{& (Join-Path $Out 'negative.exe') $negativeOut 2>&1|Tee-Object -FilePath (Join-Path $Out 'negative.log');$taskNegativeExit=$LASTEXITCODE}
finally{$ErrorActionPreference=$taskPreviousPreference}
if($taskNegativeExit -ne 1 -or [IO.File]::ReadAllText((Join-Path $Out 'negative.log')) -notmatch 'actual committed Ready admits one CSS-only event'){throw 'Disabled recovery was not detected at the actual Ready behavior'}
[pscustomobject]@{detected=$true;exitCode=$taskNegativeExit;compiled=$true;expectedAssertion='actual committed Ready admits one CSS-only event';mutation='suppress exact production recovery dispatch';realCssOperations=0;realSteamOperations=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Out 'negative-result.json') -Encoding UTF8
Write-Output 'PASS: disabled resume dispatch is rejected by the real worker scenario'
