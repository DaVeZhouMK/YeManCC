@echo off
setlocal
where cl.exe >nul 2>nul
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 goto :fail
cd /d "%~dp0"

REM C++/WinRT is required for HC-equivalent Windows default sensor selection.
REM vcvars normally supplies both variables; keep the fallback explicit so this
REM native recipe never relies on a developer's ambient include/link paths.
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
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

REM compile resources (icon + version)
rc /nologo /fo "%OUTDIR%\app.res" app.rc
if errorlevel 1 goto :fail

REM compile shell: /MT static CRT + WebView2 static loader. All outputs stay in Build.
REM dsu_server.cpp (7.50 i2, DSU/ShockEmu UDP server) compiled alongside main.cpp;
REM each translation unit keeps its own .obj, then link produces one exe (/Fo per source).
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /c main.cpp /I"..\deps\webview2\build\native\include" /I"..\deps\json" /I"%CPPWINRT_INCLUDE%" /Fo"%OUTDIR%\main.obj"
if errorlevel 1 goto :fail
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE /c dsu_server.cpp /I"..\deps\webview2\build\native\include" /I"..\deps\json" /I"%CPPWINRT_INCLUDE%" /Fo"%OUTDIR%\dsu_server.obj"
if errorlevel 1 goto :fail
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE "%OUTDIR%\main.obj" "%OUTDIR%\dsu_server.obj" "%OUTDIR%\app.res" /Fe"%OUTDIR%\YeManCC.exe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 "..\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" "%WINDOWSAPPLIB%" user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib
if errorlevel 1 goto :fail

REM Independent recovery service. It must remain alive when YeManCC's UI thread
REM or WebView2 renderer is wedged, so it is a separate executable.
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE recovery_service.cpp /Fo"%OUTDIR%\recovery_service.obj" /Fe"%OUTDIR%\YeManRecoveryService.exe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 user32.lib shell32.lib ole32.lib powrprof.lib
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
