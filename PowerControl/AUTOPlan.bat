@echo off
@chcp 65001 >nul
setlocal enabledelayedexpansion

REM ============================================================
REM  YeMan AUTOPlan 脚本（精简版）
REM  仅做：切换电源方案 → 读取统一配置 tdp.tdpMax 并换算 AMD/Intel
REM        → 复位一次电源方案
REM  不含 RTSS 检测与 HWiNFO 修复
REM  TDP 配置（单位：瓦 W，可加 # 注释行）：
REM    C:\SOFT\YeMan\PowerControl\yeman-settings.json 的 tdp.tdpMax  TDP 最大值（程序真相源）
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

REM ----- 4. 复位一次电源方案 -----
powercfg -setactive 1cb8b882-a900-4b9f-9bac-99d151e64441

goto :EOF


:SET_TDP
    REM 程序配置真相源：统一配置 tdp.tdpMax
    set "SRC=C:\SOFT\YeMan\PowerControl\yeman-settings.json"
    set "DEFW=75"

    REM 用 JSON 解析读取精确的 tdp.tdpMax，不能用 findstr 扫描性能档位中的同名字段
    set "RAW="
    if exist "%SRC%" for /f "usebackq delims=" %%a in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$p='C:\SOFT\YeMan\PowerControl\yeman-settings.json'; if (Test-Path -LiteralPath $p) { $j=Get-Content -LiteralPath $p -Raw | ConvertFrom-Json; if ($null -ne $j.tdp.tdpMax) { [int][math]::Round([double]$j.tdp.tdpMax) } }"`) do if not defined RAW set "RAW=%%a"
    set "TDPW=%RAW: =%"
    if not defined TDPW set "TDPW=%DEFW%"

    set /a "W=TDPW"

    echo [TDP] 源=%SRC% 目标=%W%W (经 PawnIO, 已避开 WinRing0)
    REM 统一交给 PawnIO CLI：自动识别 AMD/Intel 并换算/写入
    REM   AMD  = W*1000 mW 写 STAPM/PPT fast/slow（RyzenSMU.bin）
    REM   Intel= 写 MSR 0x610，PL1=W, PL2=W+5 保证 PL2>=PL1（IntelMSR.bin）
    "%TDPCTL%" set %W%
goto :EOF
