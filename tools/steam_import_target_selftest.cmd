@echo off
setlocal EnableExtensions DisableDelayedExpansion
if "%~2"=="" exit /b 2
set "OUT=%~f1"
set "SNAP=%OUT%\build-source"
if not exist "%SNAP%\native" mkdir "%SNAP%\native"
if not exist "%SNAP%\tools" mkdir "%SNAP%\tools"
if /i "%~2"=="fixed" (
  for %%H in ("%~dp0..\native\steam_compatibility*.h" "%~dp0..\native\steam_import_target_win32.h") do if exist "%%~fH" copy /b /y "%%~fH" "%SNAP%\native\%%~nxH" >nul
)
for %%T in (steam_import_target_selftest.cpp steam_import_target_fixture.cpp steam_import_target_selftest.cmd) do copy /b /y "%~dp0%%T" "%SNAP%\tools\%%T" >nul
where cl.exe >nul 2>nul
if not errorlevel 1 goto sdk
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
set "VSROOT="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
:sdk
if defined UniversalCRTSdkDir goto compile
set "SDKROOT=%ProgramFiles(x86)%\Windows Kits\10"
set "SDKVERSION="
for /f "delims=" %%V in ('dir /b /ad /on "%SDKROOT%\Include\10.*" 2^>nul') do if exist "%SDKROOT%\Include\%%V\ucrt\crtdbg.h" if exist "%SDKROOT%\Lib\%%V\ucrt\x64\libucrt.lib" set "SDKVERSION=%%V"
if not defined SDKVERSION exit /b 1
set "INCLUDE=%INCLUDE%;%SDKROOT%\Include\%SDKVERSION%\ucrt;%SDKROOT%\Include\%SDKVERSION%\shared;%SDKROOT%\Include\%SDKVERSION%\um"
set "LIB=%LIB%;%SDKROOT%\Lib\%SDKVERSION%\ucrt\x64;%SDKROOT%\Lib\%SDKVERSION%\um\x64"
:compile
set "BEFORE="
if /i "%~2"=="negative" set "BEFORE=/DYMCC_IMPORT_TARGET_BEFORE_FIX"
cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /O2 /LD /Fo"%OUT%\fixture.obj" /Fe"%OUT%\ymcc_import_fixture.dll" "%SNAP%\tools\steam_import_target_fixture.cpp" /link /IMPLIB:"%OUT%\ymcc_import_fixture.lib" > "%OUT%\fixture-build.log" 2>&1
if errorlevel 1 exit /b 2
cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /O2 %BEFORE% /Fo"%OUT%\selftest.obj" /Fe"%OUT%\import-selftest.exe" "%SNAP%\tools\steam_import_target_selftest.cpp" "%OUT%\ymcc_import_fixture.lib" bcrypt.lib > "%OUT%\build.log" 2>&1
if errorlevel 1 exit /b 2
"%OUT%\import-selftest.exe" > "%OUT%\RESULT.json" 2> "%OUT%\stderr.log"
set "TESTEXIT=%ERRORLEVEL%"
> "%OUT%\run.exit" echo %TESTEXIT%
exit /b %TESTEXIT%
