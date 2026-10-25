[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[string]$NativeRoot='',[string]$MainSource='')
$ErrorActionPreference='Stop'
$taskWatch=[Diagnostics.Stopwatch]::StartNew()
function Get-TaskManualHash([string]$Path){
 $taskStream=[IO.File]::OpenRead($Path);$taskAlgorithm=[Security.Cryptography.SHA256]::Create()
 try{return ([BitConverter]::ToString($taskAlgorithm.ComputeHash($taskStream))).Replace('-','').ToLowerInvariant()}
 finally{$taskAlgorithm.Dispose();$taskStream.Dispose()}
}
$repo=Split-Path -Parent $PSScriptRoot
if(!$NativeRoot){$NativeRoot=Join-Path $repo 'native'}
if(!$MainSource){$MainSource=Join-Path $repo 'native\main.cpp'}
$taskCaller=[IO.Path]::GetFullPath($Out)
[IO.Directory]::CreateDirectory($taskCaller)|Out-Null
# Existing native consumer creates long-named private Steam/CSS images. Keep its
# actual Win32 work short; preserve the caller's canonical evidence unchanged.
$taskParent='G:\YeManCC-Work\Testing\YMCC\Workspace\Cm'
[IO.Directory]::CreateDirectory($taskParent)|Out-Null
for($taskAncestor=Get-Item -LiteralPath $taskParent;$taskAncestor;$taskAncestor=$taskAncestor.Parent){
 if($taskAncestor.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Manual CSS workspace has an unapproved reparse ancestor'}
}
$taskShortWork=Join-Path $taskParent ('m'+[guid]::NewGuid().ToString('N').Substring(0,8))
if(Test-Path -LiteralPath $taskShortWork){throw 'Fresh manual CSS workspace required'}
[IO.Directory]::CreateDirectory($taskShortWork)|Out-Null
$taskTemp=Join-Path $taskShortWork 'tmp';[IO.Directory]::CreateDirectory($taskTemp)|Out-Null
$taskOldTemp=$env:TEMP;$taskOldTmp=$env:TMP
$taskSources=@($PSCommandPath,$MainSource,(Join-Path $PSScriptRoot 'steam_cssloader_use_selftest.ps1'),(Join-Path $PSScriptRoot 'steam_cssloader_manual_observation_selftest.cpp'),(Join-Path $PSScriptRoot 'steam_cssloader_manual_observation_extract.mjs'))
# Pin the actual local include closure used by this unchanged C++ consumer.
$taskIncludeSources=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
function Add-TaskManualSource([string]$File){
 $taskFull=[IO.Path]::GetFullPath($File)
 if(!$taskIncludeSources.Add($taskFull)){return}
 $taskText=[IO.File]::ReadAllText($taskFull)
 foreach($taskMatch in [regex]::Matches($taskText,'(?m)^\s*#include\s+["<]([^">\r\n]+)[">]')){
  $taskName=$taskMatch.Groups[1].Value
  foreach($taskCandidate in @((Join-Path ([IO.Path]::GetDirectoryName($taskFull)) $taskName),(Join-Path $NativeRoot $taskName),(Join-Path (Join-Path $repo 'native') $taskName),(Join-Path (Join-Path $repo 'deps\json') $taskName))){
   if([IO.File]::Exists($taskCandidate)){Add-TaskManualSource $taskCandidate;break}
  }
 }
}
Add-TaskManualSource (Join-Path $PSScriptRoot 'steam_cssloader_manual_observation_selftest.cpp')
$taskSources+=@($taskIncludeSources)
$taskPins=@($taskSources | Select-Object -Unique | ForEach-Object {@{path=$_;sha256=(Get-TaskManualHash $_)}})
$taskOwner=@{kind='manual-css-original-consumer';callerEvidence=$taskCaller;work=$taskShortWork;parentPid=$PID}
[IO.File]::WriteAllText((Join-Path $taskShortWork 'OWNER.json'),($taskOwner|ConvertTo-Json -Depth 4),(New-Object Text.UTF8Encoding($false)))
$node='G:\YeManCC-Work\Environment\Toolchains\nodejs\node-v22.14.0-win-x64\node.exe'
$taskError=$null;$taskExit=$null;$taskReceipt=$null;$taskCmd=$null;$taskCopies=@()
try{
 $env:TEMP=$taskTemp;$env:TMP=$taskTemp
 & $node (Join-Path $PSScriptRoot 'steam_cssloader_manual_observation_extract.mjs') $taskShortWork $MainSource
 if($LASTEXITCODE -ne 0){throw 'Production manual CSS extraction failed'}
 $recipe=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'steam_cssloader_use_selftest.ps1') -Raw
 $recipe=$recipe.Substring($recipe.IndexOf('@"')+3);$recipe=$recipe.Substring(0,$recipe.IndexOf('"@'))
 $recipe=$recipe.Replace('$root',$repo).Replace('$OutputDirectory',$taskShortWork).Replace('steam_cssloader_use_selftest.cpp','steam_cssloader_manual_observation_selftest.cpp').Replace(' /I"'+$repo+'\native"',' /I"'+$taskShortWork+'" /I"'+$NativeRoot+'" /I"'+$repo+'\native" /I"'+$repo+'\deps\json"')
 $taskCmd=Join-Path $taskShortWork 'compile.cmd';[IO.File]::WriteAllText($taskCmd,$recipe,[Text.Encoding]::ASCII)
 Push-Location -LiteralPath $taskShortWork
 try{& cmd.exe /d /c $taskCmd *> (Join-Path $taskShortWork 'result.log');$taskExit=$LASTEXITCODE}finally{Pop-Location}
 if($taskExit -ne 0){throw ('CSS manual observer regression failed: '+(Get-Content -LiteralPath (Join-Path $taskShortWork 'result.log') -Tail 6|Out-String))}
 $taskReceipt=Get-Content -LiteralPath (Join-Path $taskShortWork 'result.json') -Raw | ConvertFrom-Json
 if(!$taskReceipt.ok -or $taskReceipt.ownedCreated -ne $taskReceipt.ownedClosed){throw 'Original manual CSS consumer or owned retirement did not complete'}
 Get-Content -LiteralPath (Join-Path $taskShortWork 'result.log')
}catch{$taskError=$_.Exception.Message}
finally{
 $env:TEMP=$taskOldTemp;$env:TMP=$taskOldTmp
 $taskChanges=@($taskPins | Where-Object {(Get-TaskManualHash $_.path) -cne $_.sha256})
 foreach($taskFile in Get-ChildItem -LiteralPath $taskShortWork -File | Where-Object {$_.Extension -notin @('.obj','.pdb')}){
  $taskTo=Join-Path $taskCaller $taskFile.Name
  [IO.File]::Copy($taskFile.FullName,$taskTo,$false)
  $taskDigest=Get-TaskManualHash $taskFile.FullName
  if((Get-TaskManualHash $taskTo) -cne $taskDigest){throw 'Manual CSS evidence copy changed'}
  $taskCopies+=@{source=$taskFile.FullName;destination=$taskTo;sha256=$taskDigest}
 }
 $taskMap=[ordered]@{schemaVersion=1;scope='Original production manual-use/guard consumer and exact main callbacks; private inert processes only';callerEvidence=$taskCaller;work=$taskShortWork;temp=$taskTemp;sources=$taskPins;sourceChanged=$taskChanges;copies=$taskCopies;compilerRecipe=if($taskCmd -and (Test-Path -LiteralPath $taskCmd)){@{path=$taskCmd;sha256=(Get-TaskManualHash $taskCmd)}}else{$null};exitCode=$taskExit;originalParentBudgetMs=90000;elapsedMs=$taskWatch.Elapsed.TotalMilliseconds;native=$taskReceipt;error=$taskError;passed=(!$taskError -and $taskExit -eq 0 -and !$taskChanges.Count -and $taskWatch.Elapsed.TotalMilliseconds -le 90000)}
 [IO.File]::WriteAllText((Join-Path $taskCaller 'PATH-MAP.json'),($taskMap|ConvertTo-Json -Depth 10),(New-Object Text.UTF8Encoding($false)))
}
if($taskError){throw $taskError}
if($taskChanges.Count){throw 'Manual CSS source changed during execution'}
if($taskWatch.Elapsed.TotalMilliseconds -gt 90000){throw 'Original manual CSS 90000ms budget exceeded'}
