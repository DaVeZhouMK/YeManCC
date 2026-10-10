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
  $out=Join-Path $build 'App\Native'
  New-Item -ItemType Directory -Path $out -Force | Out-Null
  $log=Join-Path $evidence 'native-link-mainline15.txt'
  "YMCC Decky 侧边栏 — task-local compile/link only, EXE not launched; $(Get-Date -Format o)" | Set-Content -LiteralPath $log -Encoding utf8
  Push-Location (Join-Path $workspace 'native')
  try{
    Invoke-Checked 'rc.exe' @('/nologo','/fo',(Join-Path $out 'app.res'),'app.rc') $log
    Invoke-Checked 'cl.exe' ($common+$includes+@('/c','main.cpp',('/Fo'+(Join-Path $out 'main.obj')))) $log
    Invoke-Checked 'cl.exe' ($common+$includes+@('/c','dsu_server.cpp',('/Fo'+(Join-Path $out 'dsu.obj')))) $log
    $exe=Join-Path $out 'YeManCC.exe'
    $libraries=@((Join-Path $mainline 'deps\webview2\build\native\x64\WebView2LoaderStatic.lib'),(Join-Path $sdk "Lib\$version\um\x64\WindowsApp.lib"),'user32.lib','gdi32.lib','shell32.lib','shlwapi.lib','ole32.lib','oleaut32.lib','dwmapi.lib','winhttp.lib','advapi32.lib','shcore.lib','version.lib','psapi.lib','PowrProf.lib','ws2_32.lib')
    Invoke-Checked 'cl.exe' ($common+@((Join-Path $out 'main.obj'),(Join-Path $out 'dsu.obj'),(Join-Path $out 'app.res'),"/Fe$exe",'/link','/SUBSYSTEM:WINDOWS','/MACHINE:x64')+$libraries) $log
    Invoke-Checked 'mt.exe' @('-manifest','app.manifest',"-outputresource:$exe;#1") $log
    'NATIVE_LINK_EXIT=0; CANDIDATE_EXECUTED=false' | Add-Content -LiteralPath $log -Encoding utf8;Write-Output 'NATIVE_LINK_EXIT=0; CANDIDATE_EXECUTED=false'
  }finally{Pop-Location}
}











