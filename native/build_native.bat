@echo off
setlocal
where cl.exe >nul 2>nul
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 goto :fail
cd /d "%~dp0"
node "%~dp0..\tools\battery-charge-contract.mjs" --check
if errorlevel 1 goto :fail

REM Verify the exact embedded Decky package transaction before compiling YMCC.
node "%~dp0..\tools\module_package_maintenance_embed.mjs" --check
if errorlevel 1 goto :fail
node "%~dp0..\tools\decky_package_maintenance_embed.mjs" --check
if errorlevel 1 exit /b 1
node "%~dp0..\tools\update_install_recovery_embed.mjs" --check
if errorlevel 1 exit /b 1
node "%~dp0..\tools\updater_hc_runtime_embed.mjs" --check
if errorlevel 1 exit /b 1
node "%~dp0..\tools\updater_hook_tools_embed.mjs" --check
if errorlevel 1 goto :fail

REM C++/WinRT is required for HC-equivalent Windows default sensor selection.
REM vcvars normally supplies both variables; keep the fallback explicit so this
REM native recipe never relies on a developer's ambient include/link paths.
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if "%WindowsSDKVersion%"=="\" set "WindowsSDKVersion=10.0.26100.0\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
REM Keep SDK includes/libs explicit when vcvars cannot discover the installed SDK.
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%WindowsSdkDir%Include\%WindowsSDKVersion%winrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%LIB%"
set "PATH=%WindowsSdkDir%bin\%WindowsSDKVersion%x64;%PATH%"
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
set "WINDOWSAPPLIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64\WindowsApp.lib"
if not exist "%CPPWINRT_INCLUDE%\winrt\base.h" goto :fail
if not exist "%WINDOWSAPPLIB%" goto :fail

if defined YEMAN_WORKSPACE_ROOT (
  set "WORKSPACE_ROOT=%YEMAN_WORKSPACE_ROOT%"
) else (
  set "WORKSPACE_ROOT=%~dp0..\..\.."
)
for %%I in ("%WORKSPACE_ROOT%") do set "WORKSPACE_ROOT=%%~fI"
set "OUTDIR=%WORKSPACE_ROOT%\Build\App\Native"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

REM Opening YMCC must never replay a remembered Steam mode or linked-loader use.
REM Enforce the actual native callback boundaries before any native compilation.
REM Ordinary exports skip fixtures; retain source selftests for explicit full acceptance.
if "%YEMAN_FULL_ACCEPTANCE%"=="1" (
  node "%~dp0..\tools\steam_manual_startup_source_selftest.mjs" "%OUTDIR%\steam-startup-admission"
  if errorlevel 1 goto :fail
)

REM Native Touch/Pen input with mouse-transparent surfaces is the normal build.
REM Set YEMAN_SCREEN_NATIVE_INPUT_CANDIDATE=0 only for the explicit legacy fallback.
set "SCREEN_NATIVE_INPUT_DEFINE=/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1"
if "%YEMAN_SCREEN_NATIVE_INPUT_CANDIDATE%"=="0" set "SCREEN_NATIVE_INPUT_DEFINE="
if defined YEMAN_SCREEN_NATIVE_INPUT_CANDIDATE if not "%YEMAN_SCREEN_NATIVE_INPUT_CANDIDATE%"=="0" if not "%YEMAN_SCREEN_NATIVE_INPUT_CANDIDATE%"=="1" goto :fail

REM compile resources (icon + version)
rc /nologo /fo "%OUTDIR%\app.res" app.rc
if errorlevel 1 goto :fail

REM compile shell: /MT static CRT + WebView2 static loader. All outputs stay in Build.
REM dsu_server.cpp (7.50 i2, DSU/ShockEmu UDP server) compiled alongside main.cpp;
REM each translation unit keeps its own .obj, then link produces one exe (/Fo per source).
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE %SCREEN_NATIVE_INPUT_DEFINE% /DYMCC_MSI_HOST_STAGE_DIAGNOSTIC /DYMCC_MSI_VISIBILITY_DIAGNOSTIC /DYMCC_MSI_USB_NAME_ORIGIN_DIAGNOSTIC /DYMCC_MSI_VISIBILITY_RAW_DIAGNOSTIC /DYMCC_MSI_VISIBILITY_WAKE_DIAGNOSTIC /c main.cpp /I"..\deps\webview2\build\native\include" /I"..\deps\json" /I"%CPPWINRT_INCLUDE%" /Fo"%OUTDIR%\main.obj"
if errorlevel 1 goto :fail
REM Original-instance Steam recovery is linked into YMCC; no helper executable.
REM Ordinary exports skip fixtures; retain source selftests for explicit full acceptance.
if "%YEMAN_FULL_ACCEPTANCE%"=="1" (
  node "%~dp0..\tools\steam_original_recovery_runtime_source_selftest.mjs"
  if errorlevel 1 goto :fail
)
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /c steam_original_recovery_engine.cpp /I"..\deps\json" /Fo"%OUTDIR%\steam_original_recovery_engine.obj"
if errorlevel 1 goto :fail
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /c dsu_server.cpp /I"..\deps\webview2\build\native\include" /I"..\deps\json" /I"%CPPWINRT_INCLUDE%" /Fo"%OUTDIR%\dsu_server.obj"
if errorlevel 1 goto :fail
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE "%OUTDIR%\main.obj" "%OUTDIR%\dsu_server.obj" "%OUTDIR%\steam_original_recovery_engine.obj" "%OUTDIR%\app.res" /Fe"%OUTDIR%\YeManCC.exe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 "..\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" "%WINDOWSAPPLIB%" user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib bcrypt.lib
if errorlevel 1 goto :fail

REM Shell API switching is compiled into YeManCC.exe's early worker mode.

REM Independent recovery service. It must remain alive when YeManCC's UI thread
REM or WebView2 renderer is wedged, so it is a separate executable.
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE recovery_service.cpp /Fo"%OUTDIR%\recovery_service.obj" /Fe"%OUTDIR%\YeManRecoveryService.exe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 user32.lib shell32.lib ole32.lib powrprof.lib
if errorlevel 1 goto :fail

REM Mainline external visibility evidence; runtime owns the MSI/logging gates
REM and launches this console helper hidden. No controller mode-query defines.
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE "..\tools\controller_visibility_probe.cpp" /I"..\deps\json" /I"%CPPWINRT_INCLUDE%" /Fo"%OUTDIR%\controller_visibility_probe.obj" /Fe"%OUTDIR%\YeManControllerVisibilityProbe.exe" /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "%WINDOWSAPPLIB%" user32.lib ole32.lib dinput8.lib dxguid.lib hid.lib setupapi.lib cfgmgr32.lib
if errorlevel 1 goto :fail

REM RTSSHooks stays in a disposable x64 helper, never in the WebView2 shell.
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE rtss\profile_helper.cpp /Fo"%OUTDIR%\profile_helper.obj" /Fe"%OUTDIR%\YMCCRtssProfileHelper.exe" /link /SUBSYSTEM:CONSOLE /MACHINE:x64
if errorlevel 1 goto :fail
REM RTSS's documented client-plugin host is x86 even on a 64-bit OS.
call rtss\build_bridge.bat
if errorlevel 1 goto :fail

REM MUST embed admin manifest (hard rule)
mt.exe -manifest app.manifest -outputresource:"%OUTDIR%\YeManCC.exe";#1
if errorlevel 1 goto :fail

REM Build output is intentionally kept in the workspace.  Deployment to
REM C:\SOFT\YeMan is a separate, explicitly authorized release step.
echo BUILD_OK %OUTDIR%\YeManCC.exe
goto :eof

:fail
echo BUILD_FAILED
exit /b 1
