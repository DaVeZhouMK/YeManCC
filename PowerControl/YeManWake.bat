@echo off
@chcp 65001 >nul
setlocal enabledelayedexpansion

REM ============================================================
REM  YeMan 开机 / 唤醒 整合脚本
REM  流程：切换电源方案 → 按电源状态读取 TDP 配置并换算 AMD/Intel
REM        → 检测 RTSS，运行中则重启 HWiNFO 解决12小时内存共享问题
REM        → 复位一次电源方案
REM  TDP 配置（单位：瓦 W，可加 # 注释行）：
REM    C:\SOFT\YeMan\PowerControl\tdp-ac.txt  接电时目标 TDP
REM    C:\SOFT\YeMan\PowerControl\tdp-dc.txt  电池时目标 TDP
REM  设置方式：统一经 PawnIO CLI（避开 WinRing0），自动识别 AMD/Intel：
REM    AMD  = 值*1000(mW) 写 STAPM/PPT fast/slow（RyzenSMU.bin）
REM    Intel= 写 MSR 0x610，PL1=值, PL2=值+5 保证 PL2>=PL1（IntelMSR.bin）
REM ============================================================

REM 运行文件路径（PawnIO 版，取代 WinRing0 的 ryzenadj/msr-cmd）
set "TDPCTL=C:\SOFT\YeMan\PowerControl\pawnio\YeManTdpCtl.exe"

REM ----- 0. 提权（msr-cmd / powercfg 需要管理员权限）-----
fltmc >nul 2>&1 || (
    PowerShell -Command "Start-Process '%~s0' -Verb RunAs -WindowStyle Hidden"
    exit /b
)

REM ----- 1. 首先切换电源方案（ELITE 精睿性能）-----
powercfg -setactive 1cb8b882-a900-4b9f-9bac-99d151e64441

REM ----- 2/3. 按电源状态读取 TDP 并换算 AMD + Intel -----
call :SET_TDP

REM ----- 4. 检测 RTSS 是否运行，运行则重启 HWiNFO（解决12小时内存共享问题）-----
tasklist /FI "IMAGENAME eq RTSS.exe" 2>NUL | find /I "RTSS.exe" >NUL
if not errorlevel 1 call :RTSS_RESET

REM ----- 5. 复位一次电源方案 -----
powercfg -setactive 1cb8b882-a900-4b9f-9bac-99d151e64441

goto :EOF


:SET_TDP
    REM 检测电源状态：BatteryStatus==1 为放电(DC)，否则 AC（无电池机器按 AC 处理）
    for /f %%i in ('powershell -nop -c "try{(Get-WmiObject -Class Win32_Battery).BatteryStatus}catch{2}"') do set "BATT=%%i"
    if "%BATT%"=="1" (
        set "SRC=C:\SOFT\YeMan\PowerControl\tdp-dc.txt"
        set "DEFW=45"
    ) else (
        set "SRC=C:\SOFT\YeMan\PowerControl\tdp-ac.txt"
        set "DEFW=75"
    )

    REM 读取首个非注释非空行
    set "RAW="
    if exist "%SRC%" (
        for /f "usebackq eol=# delims=" %%a in ("%SRC%") do if not defined RAW set "RAW=%%a"
    )
    REM 取行首数字（忽略单位字母如 W）
    set "TDPW="
    if defined RAW (
        for /f "tokens=1 delims=abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ " %%n in ("%RAW%") do set "TDPW=%%n"
    )
    if not defined TDPW set "TDPW=%DEFW%"

    set /a "W=TDPW"


    echo [TDP] 模式=%BATT% 源=%SRC% 目标=%W%W (经 PawnIO, 已避开 WinRing0)
    REM 统一交给 PawnIO CLI：自动识别 AMD/Intel 并换算/写入
    REM   AMD  = W*1000 mW 写 STAPM/PPT fast/slow（RyzenSMU.bin）
    REM   Intel= 写 MSR 0x610，PL1=W, PL2=W+5 保证 PL2>=PL1（IntelMSR.bin）
    "%TDPCTL%" set %W%
goto :EOF


:RTSS_RESET
    REM 关闭 HWiNFO
    taskkill /F /IM HWiNFO64.exe >NUL 2>&1
    wmic process where name="HWiNFO64.exe" delete >NUL 2>&1

    REM 覆盖 HWiNFO 配置文件（从 YeMan 配置目录同步）
    robocopy "C:\Program Files\HWiNFO64\YeMan" "C:\Program Files\HWiNFO64" /E /COPYALL /R:0 /W:0 >NUL

    REM 以低优先级重启 HWiNFO
    start "" /low "C:\Program Files\HWiNFO64\HWiNFO64.exe"
    timeout /t 2 /nobreak >nul

    REM 根据 HWiNFO 运行状态选择布局
    tasklist /FI "IMAGENAME eq HWiNFO64.exe" 2>NUL | find /I "HWiNFO64.exe" >NUL
    if "!ERRORLEVEL!"=="0" (
        set "LAYOUT=YeManOBS-W-1.ovl"
    ) else (
        set "LAYOUT=YeManOBS-W-2.ovl"
    )

    REM 仅在开启 OSD 时覆盖 OverlayEditor 配置
    set "file_path=C:\Program Files (x86)\RivaTuner Statistics Server\Profiles\Global"
    findstr /c:"EnableOSD=1" "%file_path%" >nul
    if %errorlevel% neq 0 goto RTSS_SKIP_CFG

    set "CFG_FILE=C:\Program Files (x86)\RivaTuner Statistics Server\Plugins\Client\OverlayEditor.cfg"
    (
        echo [Settings]
        echo Layout=%LAYOUT%
    ) > "%CFG_FILE%"

:RTSS_SKIP_CFG
    REM 固定相关进程 CPU 亲和性（RTSS 已在运行，无需再启动）
    powershell -NoProfile -Command "Get-Process EncoderServer64 -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"
    powershell -NoProfile -Command "Get-Process RTSS -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"
    powershell -NoProfile -Command "Get-Process RTSSHooksLoader64 -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"
    powershell -NoProfile -Command "Get-Process HWiNFO64 -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"

goto :EOF
