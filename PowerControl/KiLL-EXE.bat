

@echo off
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

set "psScriptPath=C:\SOFT\YeMan\PowerControl\KiLL-EXE.ps1"
for /f "delims=" %%i in ('powershell -ExecutionPolicy Bypass -File "%psScriptPath%"') do set "ProcessPid=%%i"

if defined ProcessPid (
    taskkill /F /T /PID "%ProcessPid%"
)

