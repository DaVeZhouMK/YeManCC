@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not defined YEMAN_WORKSPACE_ROOT set "YEMAN_WORKSPACE_ROOT=%~dp0..\..\..\.."
for %%I in ("%YEMAN_WORKSPACE_ROOT%") do set "OUTDIR=%%~fI\Build\App\Native"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /LD overlay_bridge.cpp /Fo"%OUTDIR%\overlay_bridge.obj" /Fe"%OUTDIR%\YMCCOverlayBridge.dll" /link /DEF:overlay_bridge.def /IMPLIB:"%OUTDIR%\YMCCOverlayBridge.lib" /MACHINE:x86 user32.lib
set "RC=%errorlevel%"
popd
exit /b %RC%
