# 野蛮系统控制中心 YeManCC

野蛮系统控制中心（YeManCC）是一款面向 Windows 游戏设备和高性能电脑的综合控制工具，帮助用户集中管理性能、功耗、CPU、游戏、Steam 大屏和系统启动项。

## 主要功能

- 性能调度：自动或手动切换多档性能配置。
- TDP 功耗控制：调节功耗上限、FPS 目标和自动应用策略。
- CPU 调度：管理 CPU 性能档位、睿频、核心和电源方案。
- 游戏监控：查看游戏、FPS、功耗、温度和 CPU/GPU 状态。
- 游戏暂停与恢复：睡眠前暂停游戏，唤醒后自动恢复。
- Windows 睡眠与现代待机：电源键触发的睡眠事务、S0 低功耗诊断和受控恢复。
- Steam 大屏：启动 Steam 大屏模式，并管理相关启动项。
- 模拟鼠标：支持 JoyXoff 和微软鼠标方案。
- 开机启动：管理 YeManCC、RTSS、JoyXoff 等常用程序。
- 快捷应用：音乐播放、显示设置、游戏加速、FSR 4.1 等功能。
- 支持自定义游戏配置，为不同游戏保存独立的性能方案。


<img width="1036" height="1029" alt="屏幕截图 2026-08-13 133255" src="https://github.com/user-attachments/assets/bd6294c8-3edc-44fe-bc93-8c44d97b63ee" />


## 下载

推荐从野蛮系统主页下载完整系统和手动优化包：


请优先使用主页提供的完整系统包，避免因缺少运行组件导致程序无法正常工作。

睡眠接口规范见：`G:\YeManCC-Work\Docs\Research\Sleep\YMCC-SLEEP-MODERN-STANDBY.md`。
该规范区分电源键意图、确认挂起、S0 低功耗会话和实际唤醒；不得仅凭屏幕关闭或风扇转速判断睡眠结果。

## 安装方法

1. 下载正式 Release 附件 **`YeManCC.zip`**；这是完整程序包，也是自动升级器使用的唯一包名。
2. 解压到 `C:\SOFT\YeMan`，保持以下目录结构，不要把所有文件展平：

   ```text
   C:\SOFT\YeMan\
   ├─ YeManCC\
   │  ├─ YeManCC.exe
   │  ├─ update-manifest.json
   │  └─ CustomSteamLibrary\
   │     ├─ CustomSteamLibrary.exe
   │     ├─ SteamArtworkLab.exe
   │     └─ package-manifest.json
   └─ PowerControl\
      ├─ fan-host-v2\
      ├─ handheldcompanion-runtime\
      └─ pawnio\
   ```

3. 运行 `C:\SOFT\YeMan\YeManCC\YeManCC.exe`。ZIP 根目录以包内 `YeManCC/update-manifest.json` 为准；不要手动改动已锁定风扇文件。

028 自动升级沿用 `YeManCC.zip`。033 使用 `PowerControl/fan-host-v2`，旧安装中的 `fan-host` 保留但不是新版本运行回退。更新过程保留用户配置、数据及清单之外的文件；仍建议更新前备份。

## 源码构建

需要 Windows x64、Node.js 22、pnpm 11.16.0、.NET 10 SDK，以及 Visual Studio C++ Build Tools 和 Windows SDK。InputHost 的批准依赖现位于仓库内，不再依赖开发机的 Archives 目录。

```powershell
pnpm install --frozen-lockfile
pnpm run build
pnpm run package
```

使用 Git 的 `main` checkout，保持仓库中的锁定载荷原始字节。发布 CI 使用 tag 的 detached checkout。独立源码 ZIP 用于审查/归档；发布前以批准的 Git checkout 及重新构建结果为准。构建/打包只产生工作区文件，不部署实际安装。

更新记录见 `docs/release-notes/v0.0.33.md`。
