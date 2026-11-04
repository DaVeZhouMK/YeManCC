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

# Root-cause negative: restore process-only observation deduplication. The
# actual callback/worker must refuse the stale-account shared-mode request.
$accountGuard=Join-Path $Out 'steam_cssloader_guard-account-stale.h'
$accountGuardText=[IO.File]::ReadAllText((Join-Path $repo 'native\steam_cssloader_guard.h'))
$accountOperand='if (desired_ == session) return;'
if(($accountGuardText.Split(@($accountOperand),[StringSplitOptions]::None)).Count -ne 2){throw 'Account observation operand must be unique'}
[IO.File]::WriteAllText($accountGuard,$accountGuardText.Replace($accountOperand,'if (sameSession(desired_, session)) return;'),(New-Object Text.UTF8Encoding($false)))
$accountSource=Join-Path $Out 'account-stale.cpp'
[IO.File]::WriteAllText($accountSource,([IO.File]::ReadAllText((Join-Path $repo 'tools\steam_cssloader_resume_selftest.cpp')).Replace('#include "steam_cssloader_guard.h"','#include "steam_cssloader_guard-account-stale.h"')),(New-Object Text.UTF8Encoding($false)))
$accountRecipe=$recipe.Replace((Join-Path $repo 'tools\steam_cssloader_resume_selftest.cpp'),$accountSource).Replace((Join-Path $Out 'use.obj'),(Join-Path $Out 'account-stale.obj')).Replace((Join-Path $Out 'use.exe'),(Join-Path $Out 'account-stale.exe'))
$accountRun='"'+(Join-Path $Out 'account-stale.exe')+'" "'+$Out+'"'
if(!$accountRecipe.Contains($accountRun)){throw 'Stale-account compile boundary missing'}
$accountRecipe=$accountRecipe.Replace($accountRun,'REM stale-account negative runs in its own evidence directory')
$accountCmd=Join-Path $Out 'compile-account-stale.cmd';[IO.File]::WriteAllText($accountCmd,$accountRecipe,[Text.Encoding]::ASCII)
& $accountCmd 2>&1|Tee-Object -FilePath (Join-Path $Out 'account-stale-build.log')
if($LASTEXITCODE -ne 0){throw 'Stale-account negative must compile'}
$accountOut=Join-Path $Out 'account-stale';New-Item -ItemType Directory -Path $accountOut -Force|Out-Null
$taskAccountPreference=$ErrorActionPreference;$ErrorActionPreference='Continue'
try{& (Join-Path $Out 'account-stale.exe') $accountOut 2>&1|Tee-Object -FilePath (Join-Path $Out 'account-stale.log');$taskAccountExit=$LASTEXITCODE}
finally{$ErrorActionPreference=$taskAccountPreference}
if($taskAccountExit -ne 1 -or [IO.File]::ReadAllText((Join-Path $Out 'account-stale.log')) -notmatch 'resume invalidates old shared mode before any CSS ensure can apply'){throw 'Stale-account root cause was not rejected by actual resume behavior'}
[pscustomobject]@{detected=$true;exitCode=$taskAccountExit;compiled=$true;mutation='restore process-only observeSteam deduplication';expectedAssertion='resume invalidates old shared mode before any CSS ensure can apply';realCssOperations=0;realSteamOperations=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Out 'account-stale-result.json') -Encoding UTF8
Write-Output 'PASS: stale account reproduces the real wake refresh failure'
