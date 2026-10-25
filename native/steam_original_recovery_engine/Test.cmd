@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" exit /b 64
set "OUTPUT=%~f1"
if not exist "%OUTPUT%" mkdir "%OUTPUT%"
if not exist "%OUTPUT%\tmp" mkdir "%OUTPUT%\tmp"
set "TMP=%OUTPUT%\tmp"
set "TEMP=%OUTPUT%\tmp"
set "VCTOOLS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVERSION=10.0.26100.0"
set "INCLUDE=%CD%\vendor;%CD%\query-sdk;%VCTOOLS%\include;%SDK%\Include\%SDKVERSION%\um;%SDK%\Include\%SDKVERSION%\shared;%SDK%\Include\%SDKVERSION%\ucrt"
set "LIB=%VCTOOLS%\lib\x64;%SDK%\Lib\%SDKVERSION%\um\x64;%SDK%\Lib\%SDKVERSION%\ucrt\x64"
set "PATH=%VCTOOLS%\bin\Hostx64\x64;%PATH%"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /WX /bigobj /guard:cf /DYMCC_STEAM_ORIGINAL_ENGINE_TESTING /c ..\steam_original_recovery_engine.cpp /Fo"%OUTPUT%\engine_testing.obj"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /WX /guard:cf offline_tests.cpp "%OUTPUT%\engine_testing.obj" /Fo"%OUTPUT%\offline_tests.obj" /Fe"%OUTPUT%\engine_offline_tests.exe" /link bcrypt.lib /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /GUARD:CF
exit /b %errorlevel%
