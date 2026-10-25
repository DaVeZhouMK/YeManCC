@echo off
setlocal EnableExtensions DisableDelayedExpansion
if "%~5"=="" exit /b 2
set "OUT=%~f1"
if not exist "%OUT%" mkdir "%OUT%"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSROOT="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if defined UniversalCRTSdkDir goto compile
set "SDKROOT=%ProgramFiles(x86)%\Windows Kits\10"
set "SDKVERSION="
for /f "delims=" %%V in ('dir /b /ad /on "%SDKROOT%\Include\10.*" 2^>nul') do if exist "%SDKROOT%\Include\%%V\ucrt\crtdbg.h" if exist "%SDKROOT%\Lib\%%V\ucrt\x64\libucrt.lib" set "SDKVERSION=%%V"
if not defined SDKVERSION exit /b 1
set "INCLUDE=%INCLUDE%;%SDKROOT%\Include\%SDKVERSION%\ucrt;%SDKROOT%\Include\%SDKVERSION%\shared;%SDKROOT%\Include\%SDKVERSION%\um"
set "LIB=%LIB%;%SDKROOT%\Lib\%SDKVERSION%\ucrt\x64;%SDKROOT%\Lib\%SDKVERSION%\um\x64"
:compile
cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /Zc:__cplusplus /utf-8 /O2 /I"%~dp0..\native\steam_original_recovery_engine\vendor" /Fo"%OUT%\read-contracts.obj" /Fe"%OUT%\read-contracts.exe" "%~dp0steam_compatibility_read_contracts_selftest.cpp" > "%OUT%\BUILD.log" 2>&1
if errorlevel 1 exit /b 1
"%OUT%\read-contracts.exe" "%~f2" "%~f3" "%~f4" "%~f5" > "%OUT%\VERIFY.json" 2> "%OUT%\TEST.stderr.log"
exit /b %errorlevel%
