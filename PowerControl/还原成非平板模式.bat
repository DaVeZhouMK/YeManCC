
REM --> Check for admin permissions
>nul 2>&1 net session
if %errorlevel% NEQ 0 (
    echo Requesting admin access...
    goto UACPrompt
) else (
    goto gotAdmin
)

:UACPrompt
    REM Create a temporary VBS to elevate the current script
    set "_elevateVbs=%temp%\getadmin.vbs"
    > "%_elevateVbs%" echo Set UAC = CreateObject("Shell.Application")
    >>"%_elevateVbs%" echo UAC.ShellExecute "%~s0", "", "", "runas", 1
    cscript //nologo "%_elevateVbs%"
    del /q "%_elevateVbs%" 2>nul
    exit /B

:gotAdmin
    pushd "%CD%"
    cd /D "%~dp0"

:: Set desktop mode
REG ADD "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\PriorityControl" /v ConvertibleSlateMode /t REG_DWORD /d 1 /f


echo  OK  QQ Group: 805978517

TIMEOUT /t 3


exit



