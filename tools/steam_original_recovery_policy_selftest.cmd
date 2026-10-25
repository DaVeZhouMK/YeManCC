@echo off
setlocal
rem Builds and runs only the pure in-memory policy selftest. No product launch.
rem Usage: steam_original_recovery_policy_selftest.cmd [output-directory] [debug]
where cl.exe >nul 2>&1
if errorlevel 1 (
  if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    echo ERROR: cl.exe unavailable. Run from an x64 MSVC developer prompt.
    exit /b 2
  )
  call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
  if errorlevel 1 exit /b 2
)
rem Reuse the installed SDK recorded by the native host build recipe.
set "policySdk=%ProgramFiles(x86)%\Windows Kits\10"
set "policyArch=%VSCMD_ARG_TGT_ARCH%"
if not defined policyArch set "policyArch=x64"
if exist "%policySdk%\Include\10.0.26100.0\ucrt\crtdbg.h" (
  set "INCLUDE=%policySdk%\Include\10.0.26100.0\ucrt;%INCLUDE%"
  set "LIB=%policySdk%\Lib\10.0.26100.0\ucrt\%policyArch%;%policySdk%\Lib\10.0.26100.0\um\%policyArch%;%LIB%"
)
set "policyOut=%~1"
if not defined policyOut set "policyOut=%TEMP%\ymcc-steam-original-policy-%RANDOM%-%RANDOM%"
for %%I in ("%policyOut%") do set "policyOut=%%~fI"
if not exist "%policyOut%" mkdir "%policyOut%"
if errorlevel 1 exit /b 2
set "policyFlags=/O2 /DNDEBUG"
if /i "%~2"=="debug" set "policyFlags=/Od /Zi /RTC1"
cl.exe /nologo /std:c++20 /permissive- /EHsc /W4 /WX /utf-8 /MT %policyFlags% "%~dp0steam_original_recovery_policy_selftest.cpp" /Fo"%policyOut%\policy_selftest.obj" /Fe"%policyOut%\policy_selftest.exe" /Fd"%policyOut%\policy_selftest.pdb" /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
"%policyOut%\policy_selftest.exe"
set "policyResult=%ERRORLEVEL%"
echo Selftest artifacts: %policyOut%
exit /b %policyResult%
