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
REM ===== AC lock: read FPS-ac.txt =====
set "SRC=C:\SOFT\YeMan\PowerControl\FPS-ac.txt"
set "FPS=0"
if exist "%SRC%" (
    set "RAW="
    for /f "usebackq eol=# delims=" %%a in ("%SRC%") do if not defined RAW set "RAW=%%a"
    if defined RAW (
        for /f "tokens=1 delims=abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ " %%n in ("!RAW!") do set "FPS=%%n"
    )
)

REM ===== call RTSS lock (Limit=0 means disabled) =====
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\SOFT\YeMan\PowerControl\RTSS-FPS.ps1" -Limit %FPS%
