@echo off
setlocal
REM FAN-926R execution order 6.1/6.2 test-only build (E1b native dispatch/handler-to-disk
REM integration gate + faultinject 12 scenarios). The production build carries none of these
REM switches; the artifact lands in an independent output directory.
REM Switches: -DYMCC_FAULT_INJECT (existing injection fixtures) + -DYMCC_FAN_LOG_TEST
REM (E1b entry and its mutation harness).
REM Output: %WORKSPACE_ROOT%\Build\Validation\FanLogDispatch\YeManCC-fanlog-selftest.exe
where cl.exe >nul 2>nul
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 goto :fail
cd /d "%~dp0"

if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
set "WINDOWSAPPLIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%\um\x64\WindowsApp.lib"
if not exist "%CPPWINRT_INCLUDE%\winrt\base.h" goto :fail
if not exist "%WINDOWSAPPLIB%" goto :fail

if defined YEMAN_WORKSPACE_ROOT (
  set "WORKSPACE_ROOT=%YEMAN_WORKSPACE_ROOT%"
) else (
  set "WORKSPACE_ROOT=%~dp0..\..\.."
)
for %%I in ("%WORKSPACE_ROOT%") do set "WORKSPACE_ROOT=%%~fI"
set "OUTDIR=%WORKSPACE_ROOT%\Build\Validation\FanLogDispatch"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /DYMCC_FAULT_INJECT /DYMCC_FAN_LOG_TEST /c main.cpp /I"..\deps\webview2\build\native\include" /I"..\deps\json" /I"%CPPWINRT_INCLUDE%" /Fo"%OUTDIR%\main-fi-test.obj"
if errorlevel 1 goto :fail
if not exist "%OUTDIR%\dsu_server.obj" (
  cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /c dsu_server.cpp /I"..\deps\webview2\build\native\include" /I"..\deps\json" /I"%CPPWINRT_INCLUDE%" /Fo"%OUTDIR%\dsu_server.obj"
  if errorlevel 1 goto :fail
)
if not exist "%OUTDIR%\app.res" (
  rc /nologo /fo "%OUTDIR%\app.res" app.rc
  if errorlevel 1 goto :fail
)
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE "%OUTDIR%\main-fi-test.obj" "%OUTDIR%\dsu_server.obj" "%OUTDIR%\app.res" /Fe"%OUTDIR%\YeManCC-fanlog-selftest.exe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 "..\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" "%WINDOWSAPPLIB%" user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib
if errorlevel 1 goto :fail
REM Same as the faultinject build: no embedded admin manifest, so the test entries can run
REM from a non-elevated shell.
echo FANLOG_TEST_BUILD_OK %OUTDIR%\YeManCC-fanlog-selftest.exe
goto :eof

:fail
echo FANLOG_TEST_BUILD_FAILED
exit /b 1
