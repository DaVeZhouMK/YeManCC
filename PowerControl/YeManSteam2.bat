@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion


:: 要校验的任务（含文件夹路径）
set "TASK_NAME=\野蛮优化整合系统\野蛮控制中心-开机启动"
:: 命中后要启动的程序
set "EXE_PATH=C:\SOFT\YeMan\YeManCC\YeManCC.exe"

echo 正在检查任务计划中的启动项：%TASK_NAME%
schtasks /query /tn "%TASK_NAME%" >nul 2>&1

if %errorlevel%==0 (
    echo 检测到启动任务「%TASK_NAME%」，正在启动 YeManCC...
    if exist "%EXE_PATH%" (
        start "" "%EXE_PATH%"
        echo 已发起启动：%EXE_PATH%
    ) else (
        echo 错误：未找到程序文件 %EXE_PATH%
    )
) else (
    echo 未找到启动任务「%TASK_NAME%」，放弃启动。
)

endlocal

:: 关闭Xbox APP
taskkill /F /IM XboxPcApp.exe >nul 2>&1

:: 联动启动：依次读取 YeManSteam 文件夹内的全部 .txt（内容为 exe 路径）并直接启动
:: 已在运行的进程自动跳过，避免重复开启（按 exe 文件名匹配进程）
for %%f in ("C:\SOFT\YeMan\PowerControl\YeManSteam\*.txt") do (
    for /f "usebackq delims=" %%l in ("%%f") do (
        if exist "%%l" (
            tasklist /FI "IMAGENAME eq %%~nxl" 2>nul | find /I "%%~nxl" >nul
            if errorlevel 1 (
                start "" "%%l"
            ) else (
                echo [跳过] %%~nxl 已在运行
            )
        )
    )
)

:: ===== 检查 Steam 是否已在运行 =====
tasklist /FI "IMAGENAME eq Steam.exe" | find /I "Steam.exe" >nul
if %errorlevel%==0 (
    :: 已运行：直接唤起全屏游戏 UI，不重复启动
    start "" "steam://open/gamepadui"
) else (
    :: 未运行：读注册表路径并快速离线启动
    for /f "tokens=2*" %%a in ('reg query "HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul') do (
        start "" /b "%%b\Steam.exe" -bigpicture -offline -noverifyfiles -nobootstrapupdate -skipinitialbootstrap -no-browser -nofriendsui >nul 2>&1
    )
)

:: 等待1秒，判断Steam是否已启动
timeout /t 1 /nobreak >nul


tasklist /FI "IMAGENAME eq Steam.exe" | find /I "Steam.exe" >nul
if %errorlevel%==0 (
    timeout /t 10 /nobreak >nul

powershell -NoProfile -Command ^
  "Get-Process Steam++ -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"

powershell -NoProfile -Command ^
  "Get-Process Steam++.Accelerator -ErrorAction Stop | ForEach-Object { $_.ProcessorAffinity = 0xA0 }"

    taskkill /F /IM XboxPcApp.exe >nul 2>&1
    exit /b 0
)

:: 阶段2：尝试启动Steam大屏模式
start "" "steam://open/gamepadui"

:: 启动后等待2秒
timeout /t 2 /nobreak >nul


:: 再次检查Steam进程是否存在
tasklist /FI "IMAGENAME eq Steam.exe" | find /I "Steam.exe" >nul
if %errorlevel%==0 (
    echo 【Steam已成功启动】
    echo 【关闭Xbox应用】
    taskkill /F /IM XboxPcApp.exe >nul 2>&1
    exit /b 0
)

:: 阶段3：未检测到Steam进程，提示安装
if "!steamFound!"=="0" (
    echo 【未检测到Steam安装路径】
)

echo 【未检测到Steam进程】
echo 【请安装Steam：https://store.steampowered.com/about/】
timeout /t 15 
exit /b 1