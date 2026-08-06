@echo off
@chcp 65001 >nul
setlocal enabledelayedexpansion

REM ============================================================
REM  YeMan 开机 / 唤醒 整合脚本
REM  流程：切换电源方案 → 读取 control-config.json 的 tdpMax 并换算 AMD/Intel
REM        → 检测 RTSS，运行中则刷新覆盖层配置（游戏在跑时不重启监控/注入进程）
REM        → 复位一次电源方案
REM  TDP 配置（单位：瓦 W，可加 # 注释行）：
REM    C:\SOFT\YeMan\PowerControl\control-config.json 的 tdpMax  TDP 最大值（程序真相源）
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

REM ----- 2/3. 读取 TDP 并换算 AMD + Intel -----
call :SET_TDP

REM ----- 4. 检测 RTSS 是否运行，运行中则刷新覆盖层配置（不碰监控/注入进程）-----
tasklist /FI "IMAGENAME eq RTSS.exe" 2>NUL | find /I "RTSS.exe" >NUL
if not errorlevel 1 call :RTSS_RESET

REM ----- 5. 复位一次电源方案 -----
powercfg -setactive 1cb8b882-a900-4b9f-9bac-99d151e64441

goto :EOF


:SET_TDP
    REM 程序配置真相源：control-config.json 的 tdpMax
    set "SRC=C:\SOFT\YeMan\PowerControl\control-config.json"
    set "DEFW=75"

    REM 从 JSON 读取 tdpMax
    set "RAW="
    if exist "%SRC%" for /f "tokens=2 delims=:,}" %%a in ('findstr /i "tdpMax" "%SRC%"') do if not defined RAW set "RAW=%%a"
    set "TDPW=%RAW: =%"
    if not defined TDPW set "TDPW=%DEFW%"

    set /a "W=TDPW"

    echo [TDP] 源=%SRC% 目标=%W%W (经 PawnIO, 已避开 WinRing0)
    REM 统一交给 PawnIO CLI：自动识别 AMD/Intel 并换算/写入
    REM   AMD  = W*1000 mW 写 STAPM/PPT fast/slow（RyzenSMU.bin）
    REM   Intel= 写 MSR 0x610，PL1=W, PL2=W+5 保证 PL2>=PL1（IntelMSR.bin）
    "%TDPCTL%" set %W%
goto :EOF


:RTSS_RESET
    REM ── 唤醒时 RTSS 在跑（游戏很可能在运行）：只处理 RTSS 覆盖层配置 + 亲和性 ──
    REM HWiNFO 是强制底层数据源，由 autofloat/TopMonitor 独立自动恢复，唤醒不再碰它。
    set "LAYOUT=YeManOBS-W-1.ovl"

    REM 仅在开启 OSD 时覆盖 OverlayEditor 配置（RTSS 覆盖层布局文件，纯写盘，不重启 RTSS）
    set "file_path=C:\Program Files (x86)\RivaTuner Statistics Server\Profiles\Global"
    findstr /c:"EnableOSD=1" "%file_path%" >nul
    if %errorlevel% neq 0 goto RTSS_SKIP_CFG

    set "CFG_FILE=C:\Program Files (x86)\RivaTuner Statistics Server\Plugins\Client\OverlayEditor.cfg"
    (
        echo [Settings]
        echo Layout=%LAYOUT%
    ) > "%CFG_FILE%"

:RTSS_SKIP_CFG
    REM 固定相关进程 CPU 亲和性（不碰 HWiNFO）
    powershell -NoProfile -Command "Get-Process EncoderServer64 -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"
    powershell -NoProfile -Command "Get-Process RTSS -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"
    powershell -NoProfile -Command "Get-Process RTSSHooksLoader64 -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"

goto :EOF
