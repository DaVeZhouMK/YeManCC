@echo off
setlocal EnableExtensions
set "MODE=%~1"
if /i not "%MODE%"=="restart" set "MODE=start"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0YeManHWiNFO.ps1" -Mode %MODE%
set "RC=%ERRORLEVEL%"
endlocal & exit /b %RC%
