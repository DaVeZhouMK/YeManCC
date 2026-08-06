@echo off
setlocal
:: YeManHWiNFO.bat — HWiNFO 强制恢复入口；绝不重启/触碰 RTSS。
:: /restart：HWiNFO 进程存在但共享内存异常，停止后立即复制配置并重启。
:: /start：HWiNFO 进程不存在，立即复制配置并启动；已有进程则不重复启动。
:: 统一 PowerShell 事务负责全局互斥、文件缺失判断、robocopy 结果和启动结果。

set "MODE=%~1"
if /i not "%MODE%"=="restart" set "MODE=start"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\SOFT\YeMan\PowerControl\YeManHWiNFO.ps1" -Mode %MODE%
set "RC=%ERRORLEVEL%"
endlocal & exit /b %RC%
