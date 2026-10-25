@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Read-only source PEs; any mutations are owned test fixtures in OUT.
if "%~5"=="" (
    echo Usage: %~nx0 OUT LEGACY_CLIENT PUBLIC_CLIENT LEGACY_UI PUBLIC_UI [10^|20]
    exit /b 2
)
set "OUT=%~f1"
set "SNAP=%OUT%\build-source"
for %%D in ("%OUT%" "%SNAP%\native" "%SNAP%\tools" "%OUT%\source-before") do if not exist "%%~D" mkdir "%%~D"
if not exist "%SNAP%\native" exit /b 1
for %%H in ("%~dp0..\native\steam_compatibility*.h" "%~dp0..\native\steam_import_target_win32.h") do (
    copy /b /y "%%~fH" "%SNAP%\native\%%~nxH" >nul
    if errorlevel 1 exit /b 1
    copy /b /y "%SNAP%\native\%%~nxH" "%OUT%\source-before\%%~nxH" >nul
    if errorlevel 1 exit /b 1
)
copy /b /y "%~dp0..\native\steam_original_recovery_file_hash.h" "%SNAP%\native\steam_original_recovery_file_hash.h" >nul
if errorlevel 1 exit /b 1
copy /b /y "%SNAP%\native\steam_original_recovery_file_hash.h" "%OUT%\source-before\steam_original_recovery_file_hash.h" >nul
if errorlevel 1 exit /b 1
for %%T in (steam_compatibility_file_selftest.cpp steam_compatibility_file_selftest.cmd steam_compatibility_digest_selftest.cpp steam_compatibility_digest_selftest.cmd) do (
    copy /b /y "%~dp0%%T" "%SNAP%\tools\%%T" >nul
    if errorlevel 1 exit /b 1
    copy /b /y "%SNAP%\tools\%%T" "%OUT%\source-before\%%T" >nul
    if errorlevel 1 exit /b 1
)
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
for %%D in ("%OUT%\file-layer" "%OUT%\digest") do if not exist "%%~D" mkdir "%%~D"
cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /Zc:__cplusplus /utf-8 /O2 /showIncludes /Fo"%OUT%\file-layer\selftest.obj" /Fe"%OUT%\file-layer\selftest.exe" "%SNAP%\tools\steam_compatibility_file_selftest.cpp" bcrypt.lib > "%OUT%\file-layer\build.log" 2>&1
if errorlevel 1 (
    type "%OUT%\file-layer\build.log"
    exit /b 1
)
cl.exe /nologo /std:c++20 /EHsc /W4 /WX /permissive- /Zc:__cplusplus /utf-8 /O2 /showIncludes /Fo"%OUT%\digest\selftest.obj" /Fe"%OUT%\digest\selftest.exe" "%SNAP%\tools\steam_compatibility_digest_selftest.cpp" bcrypt.lib > "%OUT%\digest\build.log" 2>&1
if errorlevel 1 (
    type "%OUT%\digest\build.log"
    exit /b 1
)
"%OUT%\file-layer\selftest.exe" "%OUT%\file-layer" "%~f2" "%~f3" "%~f4" "%~f5" > "%OUT%\file-layer\run.json" 2> "%OUT%\file-layer\run-stderr.log"
set "FILEEXIT=%ERRORLEVEL%"
type "%OUT%\file-layer\run-stderr.log"
set "REPETITIONS=%~6"
if not defined REPETITIONS set "REPETITIONS=20"
"%OUT%\digest\selftest.exe" "%OUT%\digest" "%~f2" "%~f3" "%~f4" "%~f5" "%REPETITIONS%" > "%OUT%\digest\run.json" 2> "%OUT%\digest\run-stderr.log"
set "DIGESTEXIT=%ERRORLEVEL%"
type "%OUT%\digest\run-stderr.log"
for %%H in ("%SNAP%\native\*.h") do (
    "%SystemRoot%\System32\fc.exe" /b "%%~fH" "%OUT%\source-before\%%~nxH" >nul
    if errorlevel 1 exit /b 3
)
for %%T in (steam_compatibility_file_selftest.cpp steam_compatibility_file_selftest.cmd steam_compatibility_digest_selftest.cpp steam_compatibility_digest_selftest.cmd) do (
    "%SystemRoot%\System32\fc.exe" /b "%SNAP%\tools\%%T" "%OUT%\source-before\%%T" >nul
    if errorlevel 1 exit /b 3
)
echo FROZEN_SOURCE_BEFORE_AFTER=PASS > "%OUT%\source-stability.log"
echo CURRENT_SOURCE_RELEASE=NOT_ASSESSED_FROZEN_SNAPSHOT_ONLY >> "%OUT%\source-stability.log"
echo FILE_LAYER_EXIT=%FILEEXIT%
echo DIGEST_EXIT=%DIGESTEXIT%
if not "%DIGESTEXIT%"=="0" exit /b 1
if not "%FILEEXIT%"=="0" exit /b 1
exit /b 0
