@echo off
setlocal
:: YeManRTSSone.bat — RTSS 独立一次性启动（HWiNFO 是强制底层数据源，由 autofloat/TopMonitor 独立维护，本脚本不碰）。
:: 与 YeManRTSS.bat 不同，本脚本不循环，专给 toggleRtss(true) 的手动开关/VBS 调用。

:GO1

set "LAYOUT=YeManOBS-W-1.ovl"

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

goto :EOF
