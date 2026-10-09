@echo off
setlocal
REM Test-only build: native with -DYMCC_FAULT_INJECT (review 14 section 7 requires
REM fault injection in an isolated build boundary; the production exe must not
REM carry any resident fault switch).  Output: Build\App\Native\YeManCC-faultinject.exe
REM The injected hooks read %LOCALAPPDATA%\YeManCC\test-resolve-fault.flag and are
REM compiled out of the production binary entirely.
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
set "OUTDIR=%WORKSPACE_ROOT%\Build\App\Native"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /DYMCC_FAULT_INJECT /c main.cpp /I"..\deps\webview2\build\native\include" /I"..\deps\json" /I"%CPPWINRT_INCLUDE%" /Fo"%OUTDIR%\main-fi.obj"
if errorlevel 1 goto :fail
if not exist "%OUTDIR%\dsu_server.obj" (
  cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /c dsu_server.cpp /I"..\deps\webview2\build\native\include" /I"..\deps\json" /I"%CPPWINRT_INCLUDE%" /Fo"%OUTDIR%\dsu_server.obj"
  if errorlevel 1 goto :fail
)
if not exist "%OUTDIR%\app.res" (
  rc /nologo /fo "%OUTDIR%\app.res" app.rc
  if errorlevel 1 goto :fail
)
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE "%OUTDIR%\main-fi.obj" "%OUTDIR%\dsu_server.obj" "%OUTDIR%\app.res" /Fe"%OUTDIR%\YeManCC-faultinject.exe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 "..\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" "%WINDOWSAPPLIB%" user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib
if errorlevel 1 goto :fail
REM 2026-09-18 (exit-handoff six-case self-test): the fault-inject build does NOT
REM embed the admin manifest so --fan-exit-handoff-selftest can run from a
REM non-elevated shell (process enumeration / tcp table / per-user file writes
REM need no elevation). The production exe still requires the admin manifest
REM (build_native.bat hard rule).
echo FAULTINJECT_BUILD_OK %OUTDIR%\YeManCC-faultinject.exe
goto :eof

:fail
echo FAULTINJECT_BUILD_FAILED
exit /b 1