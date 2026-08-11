@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0RTSS-start.ps1"
set "RC=%ERRORLEVEL%"
endlocal & exit /b %RC%
