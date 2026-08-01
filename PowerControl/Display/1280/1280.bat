@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

:: 获取当前分辨率
for /f "tokens=1,2 delims=x, " %%A in ('C:\SOFT\Ryzenadj\QRes.exe /S 2^>^&1 ^| find "x"') do (
    set current_width=%%A
    set current_height=%%B
)

:: 判断横屏或竖屏
if %current_width% gtr %current_height% (
    echo 当前为横屏模式
    set orientation=landscape
) else (
    echo 当前为竖屏模式
    set orientation=portrait
)

:: 计算宽高比(16:10=1.6, 16:9≈1.77)
set /a ratio=%current_width%*1000/%current_height%

:: 分辨率切换逻辑
if "%orientation%"=="landscape" (
    if %ratio% geq 1770 (
        echo 检测到16:9横屏显示器，正在切换到1280x720...
        C:\SOFT\Ryzenadj\QRes.exe /x:1280 /y:720
    ) else if %ratio% geq 1590 (
        echo 检测到16:10横屏显示器，正在切换到1280x800...
        C:\SOFT\Ryzenadj\QRes.exe /x:1280 /y:800
    ) else (
        echo 无法确定显示器比例，保持当前分辨率: %current_width%x%current_height%
    )
) else (
    if %ratio% geq 1770 (
        echo 检测到16:9竖屏显示器，保持当前分辨率: %current_width%x%current_height%
    ) else if %ratio% geq 1590 (
        echo 检测到16:10竖屏显示器，保持当前分辨率: %current_width%x%current_height%
    ) else (
        echo 无法确定显示器比例，保持当前分辨率: %current_width%x%current_height%
    )
)
