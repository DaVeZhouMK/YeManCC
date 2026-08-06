@echo off
@chcp 65001 >nul
setlocal enabledelayedexpansion

REM 旧 DC 锁帧计划脚本已停用；FPS 统一由 control-config.json 与 RTSS 前端流程管理。
REM 保留文件仅用于兼容旧任务，禁止新建/恢复对应任务。
exit /b 0
