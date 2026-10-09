@echo off
set processName=Joyxoff.exe
set "processPath=C:\SOFT\Joyxoff\Joyxoff.exe"

cd /d "C:\SOFT\Joyxoff"

if not exist "%processPath%" (
    exit /b 1
)

tasklist /FI "IMAGENAME eq %processName%" | find /I "%processName%" >nul
if %ERRORLEVEL%==0 (
    taskkill /F /IM %processName%
) else (
    start "" "%processPath%"
)