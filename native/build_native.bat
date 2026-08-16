@echo off
setlocal
where cl.exe >nul 2>nul
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 goto :fail
cd /d "%~dp0"

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
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE main.cpp /I"..\deps\webview2\build\native\include" /I"..\deps\json" /Fo"%OUTDIR%\main.obj" /Fe"%OUTDIR%\YeManCC.exe" "%OUTDIR%\app.res" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 "..\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib
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
