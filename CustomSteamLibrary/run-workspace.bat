@echo off
setlocal
cd /d "%~dp0"

set "WORKSPACE_EXE=%~dp0CustomSteamLibrary.exe"
if not exist "%WORKSPACE_EXE%" set "WORKSPACE_EXE=%~dp0SteamLibraryWorkspace.exe"
if not exist "%WORKSPACE_EXE%" set "WORKSPACE_EXE=%~dp0build\SteamLibraryWorkspace.exe"
if not exist "%WORKSPACE_EXE%" (
    if not exist "%~dp0build.bat" (
        echo Custom Steam Library executable is missing.
        exit /b 1
    )
    call "%~dp0build.bat"
    if errorlevel 1 exit /b 1
    set "WORKSPACE_EXE=%~dp0build\SteamLibraryWorkspace.exe"
)

start "" "%WORKSPACE_EXE%" --input-owner=host
exit /b 0
