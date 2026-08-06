@echo off
@chcp 65001 >nul
setlocal enabledelayedexpansion

REM ===== elevation: test write to RTSS Profiles dir; if fail, relaunch as admin =====
if "%1"=="Elevated" goto :MainCode
set "PROF=%ProgramFiles(x86)%\RivaTuner Statistics Server\Profiles"
set "TESTF=%PROF%\.writetest"
(echo.>"%TESTF%") 2>nul
if exist "%TESTF%" (
    del "%TESTF%" >nul 2>&1
    goto :MainCode
)
PowerShell -Command "Start-Process -FilePath '%~f0' -ArgumentList 'Elevated' -Verb RunAs"
exit /b

:MainCode
REM ===== AC lock: read program control-config.json fpsLimit =====
set "SRC=C:\SOFT\YeMan\PowerControl\control-config.json"
set "FPS=0"
if exist "%SRC%" for /f "tokens=2 delims=:,}" %%a in ('findstr /i "fpsLimit" "%SRC%"') do if "%FPS%"=="0" set "FPS=%%a"
set "FPS=%FPS: =%"

REM ===== call RTSS lock (Limit=0 means disabled) =====
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\SOFT\YeMan\PowerControl\RTSS-FPS.ps1" -Limit %FPS%
