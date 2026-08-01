@echo off
setlocal
set "SG=C:\SOFT\YeMan\PowerControl\Sleep"
set "FALLBACK=%~dp0"

:: 1. If YeManCC.exe is still running, the in-app guard resumes games itself -> do nothing.
tasklist /fi "IMAGENAME eq YeManCC.exe" 2>nul | find /i "YeManCC.exe" >nul
if not errorlevel 1 exit /b 0

:: 2. No suspended markers -> nothing orphaned to recover.
if not exist "%SG%\suspended\*.txt" exit /b 0

:: 3. Show the 3-choice fallback dialog (hidden launcher -> mshta).
mshta.exe "%FALLBACK%SleepGuardFallback.hta"
endlocal
