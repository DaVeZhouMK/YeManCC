@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cd /d "%~dp0"

REM backup current exe (manifest injected) for rollback
if exist YeManCC.exe copy /Y YeManCC.exe YeManCC.exe.bak_last >nul

REM compile resources (icon + version)
rc /nologo /fo app.res app.rc
if errorlevel 1 goto :fail

REM compile shell: /MT static CRT, WebView2 static link
cl /EHsc /O2 /MT /std:c++20 /utf-8 main.cpp ^
   /I"..\deps\webview2\build\native\include" ^
   /I"..\deps\json" ^
   /Fe:main.exe app.res ^
   /link /SUBSYSTEM:WINDOWS /MACHINE:x64 ^
   "..\deps\webview2\build\native\x64\WebView2LoaderStatic.lib" ^
   user32.lib gdi32.lib advapi32.lib shcore.lib version.lib
if errorlevel 1 goto :fail

REM MUST embed admin manifest (hard rule)
mt.exe -manifest app.manifest -outputresource:main.exe;#1
if errorlevel 1 goto :fail

copy /Y main.exe YeManCC.exe
echo BUILD_OK
goto :eof

:fail
echo BUILD_FAILED
exit /b 1
