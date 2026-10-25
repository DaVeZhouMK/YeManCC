param([switch]$Native,[switch]$Selftests,[switch]$UiController)
$ErrorActionPreference='Stop'
$workspace=Split-Path -Parent $PSScriptRoot
$task=if($env:YMCC_DECKY_ARTIFACT_ROOT){$env:YMCC_DECKY_ARTIFACT_ROOT}else{'G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Sidebar'}
$build=Join-Path $task 'Build'
$evidence=Join-Path $task 'validation'
New-Item -ItemType Directory -Path $build,$evidence -Force | Out-Null
$mainline='G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC'
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$envLines=& $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $envLines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$sdk='C:\Program Files (x86)\Windows Kits\10'
$version='10.0.26100.0'
$includes=@(("/I"+(Join-Path $mainline 'deps\webview2\build\native\include')),("/I"+(Join-Path $mainline 'deps\json')),("/I"+(Join-Path $sdk "Include\$version\cppwinrt")))
$includes+=@(("/I"+(Join-Path $workspace 'native')))
$common=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj','/DNDEBUG','/DUNICODE','/D_UNICODE')
function Invoke-Checked([string]$program,[string[]]$arguments,[string]$log){
  & $program @arguments 2>&1 | ForEach-Object { $text=$_.ToString();Add-Content -LiteralPath $log -Value $text -Encoding utf8;Write-Output $text }
  $exit=$LASTEXITCODE
  "COMMAND_EXIT=$exit" | Add-Content -LiteralPath $log -Encoding utf8;Write-Output "COMMAND_EXIT=$exit"
  if($exit -ne 0){throw "$program failed ($exit)"}
}
if($Selftests){
  $log=Join-Path $evidence 'native-selftests-mainline15.txt'
  "YMCC Decky 侧边栏 — source-built no-deployment fixtures; $(Get-Date -Format o)" | Set-Content -LiteralPath $log -Encoding utf8
  foreach($test in @('startup_race','runtime','broker','bootstrap','bootstrap_watch','lifecycle','context')){
    $src=Join-Path $workspace "tools\decky_sidebar_${test}_selftest.cpp"
    $exe=Join-Path $build "decky-sidebar-$test-test.exe"
    $obj=Join-Path $build "decky-sidebar-$test-test.obj"
    Invoke-Checked 'cl.exe' ($common+@('/DNOMINMAX')+$includes+@($src,"/Fo$obj","/Fe$exe",'/link','/SUBSYSTEM:CONSOLE','/MACHINE:x64','advapi32.lib','ws2_32.lib','bcrypt.lib','winhttp.lib','user32.lib')) $log
    Invoke-Checked $exe @() $log
  }
}
if($UiController){
  $log=Join-Path $evidence 'native-ui-controller-build.txt'
  $exe=Join-Path $build 'decky-sidebar-ui-controller.exe'
  $obj=Join-Path $build 'decky-sidebar-ui-controller.obj'
  Invoke-Checked 'cl.exe' ($common+@('/DNOMINMAX')+$includes+@((Join-Path $workspace 'tools\decky_sidebar_real_ui_controller.cpp'),"/Fo$obj","/Fe$exe",'/link','/SUBSYSTEM:CONSOLE','/MACHINE:x64','advapi32.lib','ws2_32.lib','bcrypt.lib','winhttp.lib','user32.lib')) $log
}
if($Native){
  # Use the authoritative application recipe: do not duplicate its feature defines here.
  # This remains a task-only build and never launches or deploys the resulting program.
  $log=Join-Path $evidence 'native-link-official-loader.txt'
  $priorWorkspace=$env:YEMAN_WORKSPACE_ROOT
  try {
    $env:YEMAN_WORKSPACE_ROOT=$task
    Invoke-Checked $env:ComSpec @('/d','/c',(Join-Path $workspace 'native\build_native.bat')) $log
    'NATIVE_LINK_EXIT=0; CANDIDATE_EXECUTED=false' | Add-Content -LiteralPath $log -Encoding utf8
  } finally { $env:YEMAN_WORKSPACE_ROOT=$priorWorkspace }
}
