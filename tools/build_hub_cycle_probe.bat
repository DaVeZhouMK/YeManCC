@echo off
setlocal
where cl.exe >nul 2>nul
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cd /d "%~dp0"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DUNICODE /D_UNICODE hub_cycle_probe.cpp /Fe:hub_cycle_probe.exe setupapi.lib cfgmgr32.lib
if errorlevel 1 exit /b 1
echo PROBE_BUILD_OK