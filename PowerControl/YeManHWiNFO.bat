@echo off
setlocal EnableExtensions DisableDelayedExpansion
REM Compatibility entry only. All configuration/recovery logic lives in YMCC.
set "MODE=start"
if /i "%~1"=="restart" set "MODE=restart"
set "INTERVAL=0"
if "%~2"=="" goto resolve
set "INTERVAL="
if "%~2"=="0" set "INTERVAL=0"
if "%~2"=="1000" set "INTERVAL=1000"
if "%~2"=="1500" set "INTERVAL=1500"
if "%~2"=="2000" set "INTERVAL=2000"
if "%~2"=="3000" set "INTERVAL=3000"
if not defined INTERVAL exit /b 8
:resolve
set "YMCC=%~dp0..\YeManCC\YeManCC.exe"
if exist "%YMCC%" goto run
set "YMCC=%~dp0..\YeManCC.exe"
if not exist "%YMCC%" exit /b 2
:run
start "" /b /wait "%YMCC%" --hwinfo-manage %MODE% --period %INTERVAL% --power-control "%~dp0."
set "RC=%ERRORLEVEL%"
endlocal & exit /b %RC%
