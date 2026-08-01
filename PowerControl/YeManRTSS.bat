@echo off
setlocal

:GO1


taskkill /F /IM HWiNFO64.exe >NUL 2>&1
wmic process where name="HWiNFO64.exe" delete >NUL 2>&1
robocopy "C:\Program Files\HWiNFO64\YeMan" "C:\Program Files\HWiNFO64" /E /COPYALL /R:0 /W:0 >NUL
start "" /low "C:\Program Files\HWiNFO64\HWiNFO64.exe"
timeout /t 2 /nobreak >nul

:: 检查 HWiNFO是否正在运行
tasklist /FI "IMAGENAME eq HWiNFO64.exe" 2>NUL | find /I /N "HWiNFO64.exe">NUL
if "%ERRORLEVEL%"=="0" (
    :: 如果 HWiNFO64.exe 正在运行，设置布局为 YeManOBS-W-1.ovl
    set "LAYOUT=YeManOBS-W-1.ovl"
) else (
    :: 如果 HWiNFO64.exe 没有运行，设置布局为 YeManOBS-W-2.ovl
    set "LAYOUT=YeManOBS-W-2.ovl"
)

:: 读取显示OSD设置如果开启OSD就继续覆盖配置，不然就直接启动RTSS
set "file_path=C:\Program Files (x86)\RivaTuner Statistics Server\Profiles\Global"
findstr /c:"EnableOSD=1" "%file_path%" >nul
if %errorlevel% neq 0 goto GO2


:: 设置配置文件路径
set "CFG_FILE=C:\Program Files (x86)\RivaTuner Statistics Server\Plugins\Client\OverlayEditor.cfg"

:: 生成新的配置文件内容
(
    echo [Settings]
    echo Layout=%LAYOUT%
) > "%CFG_FILE%"


:GO2

timeout /t 1 /nobreak >nul

start "" /B "C:\Program Files (x86)\RivaTuner Statistics Server\RTSS.exe"

timeout /t 10 /nobreak >nul

powershell -NoProfile -Command ^
  "Get-Process EncoderServer64 -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"

powershell -NoProfile -Command ^
  "Get-Process RTSS -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"

powershell -NoProfile -Command ^
  "Get-Process RTSSHooksLoader64 -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"

powershell -NoProfile -Command ^
  "Get-Process HWiNFO64 -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"

timeout /t 41400 /nobreak >nul

goto GO1
