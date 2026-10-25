@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Manage-GamingHome.ps1" -Action Enable
if errorlevel 1 echo YMCC home operation failed. See the message above.
pause
