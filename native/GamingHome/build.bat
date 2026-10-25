@echo off
setlocal
where cl.exe >nul 2>nul
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
REM Pin the installed SDK: some hosts have stale latest-SDK registration.
set "GHSDK=C:\Program Files (x86)\Windows Kits\10"
set "GHSDKVER=10.0.26100.0"
if not exist "%GHSDK%\Include\%GHSDKVER%\um\windows.h" exit /b 3
set "INCLUDE=%GHSDK%\Include\%GHSDKVER%\ucrt;%GHSDK%\Include\%GHSDKVER%\shared;%GHSDK%\Include\%GHSDKVER%\um;%GHSDK%\Include\%GHSDKVER%\winrt;%INCLUDE%"
set "LIB=%GHSDK%\Lib\%GHSDKVER%\ucrt\x64;%GHSDK%\Lib\%GHSDKVER%\um\x64;%LIB%"
set "PATH=%GHSDK%\bin\%GHSDKVER%\x64;%PATH%"
if "%~1"=="" exit /b 2
if not exist "%~1" mkdir "%~1"
cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /DNDEBUG /DUNICODE /D_UNICODE "%~dp0main.cpp" /Fo"%~1\home.obj" /Fe"%~1\YeManGamingHome.exe" /link /SUBSYSTEM:WINDOWS /MACHINE:x64 user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib taskschd.lib
if errorlevel 1 exit /b 1
mt.exe -nologo -manifest "%~dp0app.manifest" -outputresource:"%~1\YeManGamingHome.exe";#1
if errorlevel 1 exit /b 1
if /i not "%~2"=="selftest" exit /b 0
cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /DUNICODE /D_UNICODE "%~dp0..\..\tools\gaming-home\flow-selftest.cpp" /I"%~dp0." /Fo"%~1\flow-selftest.obj" /Fe"%~1\flow-selftest.exe" /link /SUBSYSTEM:CONSOLE /MACHINE:x64
if errorlevel 1 exit /b 1
"%~1\flow-selftest.exe"
exit /b %errorlevel%
