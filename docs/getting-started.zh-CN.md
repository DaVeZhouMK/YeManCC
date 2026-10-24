# 安装与源码构建

[返回中文首页](../README.md) · [English](getting-started.en.md)

## 安装

1. 从 [Releases](https://github.com/DaVeZhouMK/YeManCC/releases) 下载完整运行包 **YeManCC.zip**。
2. 解压到 `C:\SOFT\YeMan`，保持 `YeManCC` 与 `PowerControl` 同级，保留包内子目录。
3. 运行 `C:\SOFT\YeMan\YeManCC\YeManCC.exe`。

```text
C:\SOFT\YeMan\
├─ YeManCC\
│  ├─ YeManCC.exe
│  ├─ update-manifest.json
│  └─ CustomSteamLibrary\
└─ PowerControl\
   ├─ fan-host-v2\
   ├─ handheldcompanion-runtime\
   └─ pawnio\
```

自定义游戏库随完整包放在 `YeManCC/CustomSteamLibrary`。运行包用于安装，源码 ZIP 用于开发和审查。

自动更新按清单校验并保留用户数据；升级前建议备份配置。各版本所需组件以包内清单与对应发布说明为准。

## 从源码构建

当前主线使用 Windows x64、Node.js 22、pnpm 11.16.0、.NET 10 SDK，以及 Visual Studio C++ Build Tools 和 Windows SDK。具体版本以仓库配置和锁文件为准。

```powershell
pnpm install --frozen-lockfile
pnpm run build
pnpm run package
```

使用 Git 的 `main` checkout，保留锁文件和随库分发的依赖原始字节。正式发布从对应标签构建；源码 ZIP 用于审查与归档。构建和打包不等于安装或实机验证。

## 维护资料

Steam 相关维护在开发主线中从 `STEAM-UPGRADE.md`、`STEAM-RUNNING-LINK.md` 与 `docs/steam-integration-map.json` 开始；按仓库内的维护规则核对所需范围。

睡眠、游戏输入、风扇与监控需要分别验证。屏幕关闭、进程返回或接口成功，均不足以独立确认实际游戏恢复。
