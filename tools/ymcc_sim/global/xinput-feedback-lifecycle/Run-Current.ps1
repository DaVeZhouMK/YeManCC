param([Parameter(Mandatory=$true)][string]$ConfigPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$taskConfig=Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
if($taskConfig.mode -cne 'current'){throw 'PROTOTYPE_PATCH_MODE_NOT_ADMITTED'}
$taskRoot=[IO.Path]::GetFullPath([string]$taskConfig.root)
$taskCase=[IO.Path]::GetFullPath([string]$taskConfig.caseRoot)
$taskPrefix=$taskCase.TrimEnd('\')+'\'
if(-not $taskRoot.StartsWith($taskPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'XINPUT_WORKER_OUTSIDE_CASE'}
foreach($taskVariable in @('TEMP','TMP','APPDATA','LOCALAPPDATA','PROGRAMDATA')){
 $taskPath=[IO.Path]::GetFullPath((Join-Path $taskRoot ('private-'+$taskVariable.ToLowerInvariant())))
 New-Item -ItemType Directory -Path $taskPath -Force | Out-Null
 if(-not $taskPath.StartsWith($taskPrefix,[StringComparison]::OrdinalIgnoreCase) -or ((Get-Item -LiteralPath $taskPath).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'XINPUT_ENVIRONMENT_ROOT_OUTSIDE_CASE'}
 [Environment]::SetEnvironmentVariable($taskVariable,$taskPath,'Process')
}
$env:YMCC_VCVARS64=$taskConfig.toolchainCmd
$env:CL='/showIncludes'
$env:_CL_=''
$env:VSLANG='1033'
Remove-Item Env:NODE_OPTIONS -ErrorAction SilentlyContinue
Remove-Item Env:NODE_PATH -ErrorAction SilentlyContinue
$taskExit=2
try{
 & $taskConfig.node (Join-Path $taskConfig.sourceRoot 'tools/xinput_identity_feedback_lifecycle_selftest.mjs') --current (Join-Path $taskConfig.sourceRoot 'native/main.cpp') --out (Join-Path $taskRoot 'original')
 $taskExit=$LASTEXITCODE
} finally {
 @{mode='current';exitCode=$taskExit;arguments=@('--current',(Join-Path $taskConfig.sourceRoot 'native/main.cpp'),'--out',(Join-Path $taskRoot 'original'));TEMP=$env:TEMP;TMP=$env:TMP;APPDATA=$env:APPDATA;LOCALAPPDATA=$env:LOCALAPPDATA;PROGRAMDATA=$env:PROGRAMDATA} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskRoot 'original-process.json')
 $taskImages=@(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'original') -Recurse -File -Filter 'fixture.exe')
 foreach($taskImage in $taskImages){
  $taskTarget=[IO.Path]::GetFullPath($taskImage.FullName)
  if(-not $taskTarget.StartsWith($taskPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'XINPUT_COMPILED_IMAGE_OUTSIDE_CASE'}
  & $taskConfig.dumpbin /imports $taskTarget *> ($taskTarget+'.imports.log')
  if($LASTEXITCODE -ne 0){throw 'XINPUT_IMPORT_OBSERVATION_FAILED'}
 }
}
exit $taskExit
