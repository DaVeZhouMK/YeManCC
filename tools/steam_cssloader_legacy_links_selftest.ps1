param([Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference='Stop'
$taskWatch=[Diagnostics.Stopwatch]::StartNew()
function Get-TaskLegacyHash([string]$Path){
 $taskHashStream=[IO.File]::OpenRead($Path);$taskHashAlgorithm=[Security.Cryptography.SHA256]::Create()
 try{return ([BitConverter]::ToString($taskHashAlgorithm.ComputeHash($taskHashStream))).Replace('-','').ToLowerInvariant()}
 finally{$taskHashAlgorithm.Dispose();$taskHashStream.Dispose()}
}
$repo=Split-Path -Parent $PSScriptRoot
$Out=[IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Path $Out -Force|Out-Null
# This fixture creates timestamp-suffixed Win32 retirement files; keep its
# actual working tree short, while preserving the caller's original evidence.
$taskLegacyParent='G:\YeManCC-Work\Testing\YMCC\Workspace\Cr'
[IO.Directory]::CreateDirectory($taskLegacyParent)|Out-Null
for($taskAncestor=Get-Item -LiteralPath $taskLegacyParent;$taskAncestor;$taskAncestor=$taskAncestor.Parent){
 if($taskAncestor.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Legacy CSS workspace has an unapproved reparse ancestor'}
}
$taskLegacyWork=Join-Path $taskLegacyParent ('r'+[guid]::NewGuid().ToString('N').Substring(0,8))
if(Test-Path -LiteralPath $taskLegacyWork){throw 'Fresh legacy CSS workspace required'}
[IO.Directory]::CreateDirectory($taskLegacyWork)|Out-Null
$taskLegacyTemp=Join-Path $taskLegacyWork 'tmp'
[IO.Directory]::CreateDirectory($taskLegacyTemp)|Out-Null
$env:TEMP=$taskLegacyTemp;$env:TMP=$taskLegacyTemp
$taskSources=@($PSCommandPath,(Join-Path $PSScriptRoot 'steam_cssloader_use_selftest.ps1'),(Join-Path $PSScriptRoot 'steam_cssloader_legacy_links_selftest.cpp'),(Join-Path $repo 'native/steam_cssloader_legacy_links.h'),(Join-Path $repo 'native/steam_cssloader_guard.h'))
$taskPins=@($taskSources|ForEach-Object{@{path=$_;sha256=(Get-TaskLegacyHash $_)}})
$recipe=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'steam_cssloader_use_selftest.ps1') -Raw
$recipe=$recipe.Substring($recipe.IndexOf('@"')+3);$recipe=$recipe.Substring(0,$recipe.IndexOf('"@'))
$recipe=$recipe.Replace('$root',$repo).Replace('$OutputDirectory',$taskLegacyWork).Replace('steam_cssloader_use_selftest.cpp','steam_cssloader_legacy_links_selftest.cpp')
$file=Join-Path $taskLegacyWork 'compile.cmd';[IO.File]::WriteAllText($file,$recipe,[Text.Encoding]::ASCII)
$taskError=$null;$taskNativeExit=$null
try {
 Push-Location -LiteralPath $taskLegacyWork
 try {
  & cmd.exe /d /c $file *> (Join-Path $Out 'result.log')
  $taskNativeExit=$LASTEXITCODE
 } finally {Pop-Location}
 if($taskNativeExit -ne 0){throw ('Legacy CSS retirement failed: '+(Get-Content -LiteralPath (Join-Path $Out 'result.log') -Tail 8|Out-String))}
 if(!(Get-Content -LiteralPath (Join-Path $Out 'result.log') -Raw).Contains('STEAM_CSS_LINK_RETIRE_OK checks=9')){throw 'Legacy CSS original nine-case consumer did not complete'}
}catch{$taskError=$_.Exception.Message}
finally {
 $taskChanges=@($taskPins|Where-Object{(Get-TaskLegacyHash $_.path) -cne $_.sha256})
 $taskImage=Join-Path $taskLegacyWork 'use.exe'
 $taskMap=[ordered]@{schemaVersion=1;scope='Original nine-case native consumer, short owned fixture path; no Steam or CSS launch';callerEvidence=$Out;work=$taskLegacyWork;temp=$taskLegacyTemp;sources=$taskPins;sourceChanged=$taskChanges;compilerRecipe=@{path=$file;sha256=(Get-TaskLegacyHash $file)};executable=if(Test-Path -LiteralPath $taskImage){@{path=$taskImage;sha256=(Get-TaskLegacyHash $taskImage)}}else{$null};exitCode=$taskNativeExit;elapsedMs=$taskWatch.Elapsed.TotalMilliseconds;originalParentBudgetMs=30000;error=$taskError;passed=(!$taskError -and $taskNativeExit -eq 0 -and !$taskChanges.Count -and $taskWatch.Elapsed.TotalMilliseconds -le 30000)}
 [IO.File]::WriteAllText((Join-Path $Out 'PATH-MAP.json'),($taskMap|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
}
if($taskError){throw $taskError}
if($taskWatch.Elapsed.TotalMilliseconds -gt 30000){throw 'Legacy CSS original30000ms budget exceeded'}
if($taskChanges.Count){throw 'Legacy CSS source changed during execution'}
Write-Output (Get-Content -LiteralPath (Join-Path $Out 'result.log') -Tail 1)
