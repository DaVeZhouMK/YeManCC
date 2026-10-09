@echo off
@chcp 65001 >nul
setlocal enabledelayedexpansion

REM 旧 AC/DC 计划脚本已停用；程序统一读取 control-config.json 的 tdpMax。
REM 保留文件仅用于兼容旧任务，禁止新建/恢复对应任务。
exit /b 0
