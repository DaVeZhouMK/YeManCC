@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" exit /b 64
if "%~2"=="" exit /b 64
set "OUTPUT=%~f1"
set "DISPATCH_LIBRARY=%~f2"
if not exist "%DISPATCH_LIBRARY%" exit /b 66
if not exist "%OUTPUT%" mkdir "%OUTPUT%"
if not exist "%OUTPUT%\tmp" mkdir "%OUTPUT%\tmp"
set "TMP=%OUTPUT%\tmp"
set "TEMP=%OUTPUT%\tmp"
set "VCTOOLS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVERSION=10.0.26100.0"
set "INCLUDE=%CD%\vendor;%VCTOOLS%\include;%SDK%\Include\%SDKVERSION%\um;%SDK%\Include\%SDKVERSION%\shared;%SDK%\Include\%SDKVERSION%\ucrt"
set "LIB=%VCTOOLS%\lib\x64;%SDK%\Lib\%SDKVERSION%\um\x64;%SDK%\Lib\%SDKVERSION%\ucrt\x64"
set "PATH=%VCTOOLS%\bin\Hostx64\x64;%PATH%"
rem The dispatch library uses the unchanged one_shot_dispatch.h ABI; it is a build input, not a runtime helper.
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /WX /bigobj /guard:cf /LD running-publish-trial.cpp "%DISPATCH_LIBRARY%" /Fo"%OUTPUT%\running-publish-trial.obj" /Fe"%OUTPUT%\wuxia-publish-trial.dll" /link bcrypt.lib /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /GUARD:CF
if errorlevel 1 exit /b 1
if not defined NODE_EXE set "NODE_EXE=node"
"%NODE_EXE%" "%~dp0embed-trial.mjs" "%OUTPUT%\wuxia-publish-trial.dll" "%~dp0embedded_trial.h" "%OUTPUT%\build-receipt.json" "%DISPATCH_LIBRARY%"
exit /b %errorlevel%
