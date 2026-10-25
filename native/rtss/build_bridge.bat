@echo off
setlocal
REM The main build already initialized the x64 toolchain. Running vcvars again
REM can overflow cmd's 8191-character limit in a long desktop/pnpm environment.
REM Use the installed x64-hosted x86 compiler and explicit x86 library paths.
if defined VCToolsInstallDir if exist "%VCToolsInstallDir%bin\Hostx64\x86\cl.exe" goto :toolchain_ready
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if errorlevel 1 exit /b 1
:toolchain_ready
if not exist "%VCToolsInstallDir%bin\Hostx64\x86\cl.exe" exit /b 1
if not defined WindowsSdkDir exit /b 1
if not defined WindowsSDKVersion exit /b 1
set "LIB=%VCToolsInstallDir%lib\x86;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x86;%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x86"
pushd "%~dp0"
if not defined YEMAN_WORKSPACE_ROOT set "YEMAN_WORKSPACE_ROOT=%~dp0..\..\..\.."
for %%I in ("%YEMAN_WORKSPACE_ROOT%") do set "OUTDIR=%%~fI\Build\App\Native"
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
"%VCToolsInstallDir%bin\Hostx64\x86\cl.exe" /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /LD overlay_bridge.cpp /Fo"%OUTDIR%\overlay_bridge.obj" /Fe"%OUTDIR%\YMCCOverlayBridge.dll" /link /DEF:overlay_bridge.def /IMPLIB:"%OUTDIR%\YMCCOverlayBridge.lib" /MACHINE:x86 user32.lib
set "RC=%errorlevel%"
popd
exit /b %RC%
