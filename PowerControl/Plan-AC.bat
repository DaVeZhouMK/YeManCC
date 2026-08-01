@echo off
@chcp 65001 >nul
setlocal enabledelayedexpansion

REM ============================================================
REM  YeMan Plan-AC 脚本
REM  直接读取 tdp-ac.txt 设置 TDP（AMD + Intel），然后切换 ELITE 电源
REM  不做 AC/DC 判断，调用即按 AC 配置执行
REM  TDP 配置（单位：瓦 W，可加 # 注释行）：
REM    C:\SOFT\YeMan\PowerControl\tdp-ac.txt
REM  设置方式：统一经 PawnIO CLI（避开 WinRing0），自动识别 AMD/Intel：
REM    AMD  = 值*1000(mW) 写 STAPM/PPT fast/slow（RyzenSMU.bin）
REM    Intel= 写 MSR 0x610，PL1=值, PL2=值+5 保证 PL2>=PL1（IntelMSR.bin）
REM ============================================================

REM 运行文件路径（PawnIO 版，取代 WinRing0 的 ryzenadj/msr-cmd）
set "TDPCTL=C:\SOFT\YeMan\PowerControl\pawnio\YeManTdpCtl.exe"
set "SRC=C:\SOFT\YeMan\PowerControl\tdp-ac.txt"
set "DEFW=75"

REM ----- 0. 提权（msr-cmd / powercfg 需要管理员权限）-----
fltmc >nul 2>&1 || (
    PowerShell -Command "Start-Process '%~s0' -Verb RunAs -WindowStyle Hidden"
    exit /b
)

REM ----- 1. 读取 tdp-ac.txt 并换算 AMD + Intel -----
call :SET_TDP

REM ----- 2. 切换电源方案（ELITE 精睿性能）-----
powercfg -setactive 1cb8b882-a900-4b9f-9bac-99d151e64441

goto :EOF


:SET_TDP
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

    echo [TDP-AC] 源=%SRC% 目标=%W%W (经 PawnIO, 已避开 WinRing0)
    REM 统一交给 PawnIO CLI：自动识别 AMD/Intel 并换算/写入
    REM   AMD  = W*1000 mW 写 STAPM/PPT fast/slow（RyzenSMU.bin）
    REM   Intel= 写 MSR 0x610，PL1=W, PL2=W+5 保证 PL2>=PL1（IntelMSR.bin）
    "%TDPCTL%" set %W%
goto :EOF
