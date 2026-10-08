# OptiScaler 自动导入：固定版本与兼容策略

## 入口与范围

顶部游戏快捷菜单、快捷应用页均保留 **FSR4.1/Xess-OPT自动导入**。
弹窗的三个主要选项为 **FSR4 → XeSS → OPT 客户端**，另有独立维护项“卸载并还原游戏文件”。

- FSR4 / XeSS：调用随 YMCC 打包的 `PowerControl/pawnio/YeManTdpCtl.exe`，将本地运行库和模板套用到所选游戏 EXE 的目录；不依赖客户端运行。
- OPT 客户端：只发送打开 `C:\SOFT\OptiscalerClient\OptiscalerClient.exe` 的请求；不检查运行状态、不结束客户端进程，也不需要游戏目标。
- 真正安装/卸载时仍要求运行中的目标游戏先退出，以免文件锁定；此确认与客户端打开快捷方式无关。
- 这是 x64 Windows 文件部署功能，不是无输入要求的万能游戏插帧器。

## 固定支持列表

| 组件 | 优先使用 | 可回退的旧版本 |
| --- | --- | --- |
| OptiScaler | `0.9.5-pre4` | `0.9.4` |
| FSR4 INT8 Extras | `FSR_4.1.1b` | `FSR_4.1.1`、`FSR_4.0.2d`、`FSR_4.0.2c`、`FSR_4.0.2b`、`FSR_4.0.0` |
| OptiPatcher | `0.41` | 无；缺失时提示并保持默认伪装 |

先在所有已知缓存根目录寻找固定新版本，缺失/校验不通过才尝试上述旧版本。
忽略未来版本、Nightly、Rolling、FP8 和无版本目录，不自动追随 GitHub 最新版本。
DLL/ASI 必须通过 x64 PE/DLL 特征、Optional Header 和 section 文件边界检查；OptiScaler 基础包还必须有可解析的 INI 和核心运行库。
受支持的 Extras 必须有真正的超分槽文件，只有 Loader/FG/Denoiser 等效果 DLL 不算 FSR4 超分升级；
任一已知可选 DLL 损坏、同名 DLL 内容冲突或缓存无法完整读取时，拒绝该候选版本并尝试固定旧版。
这不是官方签名验证，也不能识别所有内容损坏。

如果没有受支持的 INT8 Extras，FSR 选项回退到基础包 FSR3.1 并显示警告；
如果基础 OptiScaler 包缺失/不完整，则停止安装，不改游戏文件。

本次不把新的缓存 DLL 放进 YMCC 更新包，也不增加运行时联网下载。
使用者保留旧缓存时，升级 YMCC 后仍可使用旧版模板与旧库。

## 本地缓存布局

首选 `%APPDATA%\OptiscalerClient\Cache`，例如：

```text
Cache\
  OptiScaler\
    0.9.5-pre4\OptiScaler.dll, OptiScaler.ini, 其余完整解压文件...
    0.9.4\OptiScaler.dll, OptiScaler.ini, 其余完整解压文件...
  Extras\
    FSR_4.1.1b\amd_fidelityfx_upscaler_dx12.dll 或 amdxcffx64.dll
    FSR_4.1.1b\可选的 Loader / FrameGeneration / Denoiser / RadianceCache DLL
    FSR_4.1.1\amd_fidelityfx_upscaler_dx12.dll 或 amdxcffx64.dll
  OptiPatcher\
    0.41\OptiPatcher.asi
```

也接受 `C:\SOFT\OptiscalerClient\Cache`，以及 `PowerControl` 下的
`OptiScalerCache`、`OptiscalerClient\Cache`、`OptiScaler\Cache`。
基础包保持完整解压结构；不要把 `OptiScaler.dll` 单独放到版本目录。
复制时先部署基础包，再用 Extras 覆盖对应 FSR DLL，避免旧 SDK 覆盖新 INT8 库。
两个模式都部署完整 FSR / XeSS SR+FG 依赖，插帧输出不受超分模式的文件裁剪影响。
对照客户端 `Fsr4Int8DllHelper.KnownFileNames`，精确识别并逐文件覆盖下面六个名称：

```text
amd_fidelityfx_upscaler_dx12.dll
amdxcffx64.dll
amd_fidelityfx_radiancecache_dx12.dll
amd_fidelityfx_loader_dx12.dll
amd_fidelityfx_framegeneration_dx12.dll
amd_fidelityfx_denoiser_dx12.dll
```

不把 `amdxcffx64.dll` 改名成 legacy DLL，也不把任意脚本、未知插件、`amdxc64.dll` 复制进游戏。
`amdxc64.dll` 是自定义 RDNA2 辅助驱动；排除它不等于禁用系统 AMD 驱动自身的同名库。
因此保留已验证的 `Fsr4DoNotLoadAmdxc64=auto`，不随意设为 `true` 破坏普通 AMD 驱动升级路径。

## 固定人工下载地址

这些地址用于人工维护缓存，不是运行时自动下载接口：

```text
OptiScaler 0.9.5-pre4:
https://github.com/Optiscaler-Client/Optiscaler-Betas/releases/download/0.9.5-pre4/Optiscaler_0.9.5-pre4.20260905.7z

FSR4 INT8 4.1.1b:
https://github.com/Optiscaler-Client/Optiscaler-Extras/releases/download/FSR_4.1.1b/FSR4_INT8_4.1.1b.7z

OPT 客户端发布页:
https://github.com/Optiscaler-Client/Optiscaler-Client/releases
```

## 两个模式与 Auto x2

- FSR 模式：DX12 用 `fsr31`；DX11/Vulkan 用 `fsr31_12` 转译路径。
  有 INT8 Extras 时开启 FSR4 升级并指定 INT8；不引入 FP8、RDNA2 自定义驱动 DLL 或未知配置键。
- XeSS 模式：DX12/Vulkan 用 `xess`；DX11 用 `xess_12`，避免把 Arc 专用原生 DX11 路径强套给普通核显。
  已部署 INT8 Extras 时保留 `Fsr4Update=true` / `Fsr4ForceEnableInt8=true`；超分输出仍由 `[Upscalers]` 的 XeSS 选择决定，不误把“选 XeSS”当成“禁止已装 FSR4 INT8 替换”。
- 两模式均保持游戏控制质量/比例，伪装项为 `auto`，不强改厂商/设备 ID。
- XeFG `InterpolationCount=1`，即一张插帧、x2；初始化检查不绕过。
  FSR FG 使用 `FGIndex=1` 的 FSR3.1 路径，不强启 FSR4 原生 FP8 插帧。

Auto 路由优先级：

1. 游戏原生 DLSS-G 且旧版 Nukem 依赖完整：`nukems` 输入/输出。
2. 原生 FSR3.0：`fsrfg30` 输入；原生 FSR3.1 分离 FG 库：`fsrfg` 输入。
3. 识别到 DX12 且没有已知原生 FG：实验性 `upscaler`（OptiFG）输入。
4. FSR 模式优先 `fsrfg` 输出；XeSS 模式优先 `xefg` 输出；依赖缺失时尝试另一种。
5. DX11/Vulkan/未知 API 或 FG 依赖缺失：关闭 FG，保留所选超分并告知安全降级。

旧版真正读取 HUD 修复的是 **`[OptiFG] HUDFix`**，不是客户端新示例中的 `[HUDFix]`。
OptiFG 路径启用 HUD 修复，但实际深度、运动矢量、HUD 捕获仍由游戏决定，可能需要在覆盖层/客户端调节或关闭。
这些是通用模板，不承诺所有 AMD/Intel 核显、驱动或游戏都能运行 FSR4 / XeFG。
上游明确提醒：Unreal 游戏的**原生 XeSS 输入**可能不提供深度，不能据此承诺 OptiFG 可用；
这与 YMCC 选择 **XeSS 超分输出**不是同一个概念。OptiFG 也可能影响 Steam Input/覆盖层，
RTSS 覆盖层可能加剧不稳定；安装结果会提示这些风险，保留用户在 OPT 客户端关闭/调整的入口。
发现已知反作弊文件时拒绝自动注入；文件名检查不能保证识别所有反作弊，也不代表在线游戏可安全注入。

## 备份与恢复

- 覆盖前完整备份到 `%APPDATA%\YeManCC\optiscaler_backups`，并校验哈希。
- 安装异常尽力回滚；回滚失败时保留 `.pending` 和原件备份，不覆盖未完成事务。
- YMCC 卸载路径先检查清单和全部原件备份，再还原/删除；原件缺失或哈希异常时拒绝操作。
- 已有 YMCC 安装记录的游戏，切换模式/版本应先卸载还原再重新安装。
- 没有可信清单时，即使游戏 DLL 与缓存哈希完全相同也拒绝删除，避免误删游戏自带 FidelityFX/XeSS 运行库。
- 官方客户端清单按 `FilesOverwritten`、`FilesCreated`、`PreInstallKeyFiles` 的真实结构读取：
  路径大小写去重、原件改名映射、pre/post SHA256 和存在性必须一致；缺少原件/安装哈希时保守拒绝。
  还原后保留客户端自己的备份存储，不擅自清理其状态。
- 兼容旧 YMCC committed 清单的 `source_sha256`，允许用户修改 INI 后卸载；DLL 被第三方修改时保留并报错。
- `.pending` 中断事务可经“卸载并还原”恢复，状态接口明确要求恢复；dry-run 也不假装中断状态可再次安装。
- 安装/还原均重新确认目标哈希，读文件失败不视为文件原本不存在。INI/manifest 原子落盘，避免截断写入。
- 目标和备份路径拒绝绝对/穿越/ADS/设备名、junction/link 祖先和 hard-link；系统、控制器、缓存与备份目录禁止作为游戏目标。
- 同一游戏的跨进程锁位于 `%APPDATA%\YeManCC\optiscaler_locks`，避免两个 YMCC 事务并发覆盖。
- 清理只删除清单明确记录为本次新建且仍为空的目录；已有空 `plugins` 等游戏目录保留。
- 缓存校验回退只是文件级容错，不是游戏启动失败后的自动 GPU 兼容回退。

## 维护与验证

离线回归：`python tools/optiscaler_selftest.py`；前端：`pnpm run type-check`、`pnpm exec vite build`。
控制器修改后必须重新用 `PowerControl/pawnio/YeManTdpCtl.spec` 编译 EXE，
同步 `tools/release-assets.lock.json` 中对应 SHA-256，不能只交付 `.py`。
规格保持 `console=True` 以输出 JSON；YMCC 原生启动使用隐藏窗口标志。
`pawnio/_internal` 应与发布锁定清单一起校验，不能随意替换成其它 Python 运行时。

配置依据：OptiScaler v0.9.4 的 `Config.cpp`、本地 0.9.5-pre4 随包 INI、
OptiscalerClient-1.0.7.2 源码与上游 OptiFG 说明。不能把客户端/上游 main 的新键无条件写入这些固定旧版本。

## 本次源码审计与验证记录（2026-10-02）

官方 Release API 核验：`OptiscalerClient-1.0.7.2`，发布时间 `2026-09-23T18:24:28Z`；
源码固定 commit 为 `3f8159ea94ef6170c069b58575c381cadc636c3c`，避免把 main 的后续改动当成固定版本行为。
另外对照 OptiScaler v0.9.4 commit `7534ad00bf9e590eedb99e8dd9fd8c89dae3654f` 与本机 pre4 随包 INI。

```text
客户端 FG 路由源码：
https://github.com/Optiscaler-Client/Optiscaler-Client/blob/3f8159ea94ef6170c069b58575c381cadc636c3c/Services/FrameGenerationConfigurationService.cs
客户端安装/输出模板源码：
https://github.com/Optiscaler-Client/Optiscaler-Client/blob/3f8159ea94ef6170c069b58575c381cadc636c3c/Services/GameInstallationService.cs
客户端 FSR4 文件名：
https://github.com/Optiscaler-Client/Optiscaler-Client/blob/3f8159ea94ef6170c069b58575c381cadc636c3c/Helpers/Fsr4Int8DllHelper.cs
客户端恢复清单：
https://github.com/Optiscaler-Client/Optiscaler-Client/blob/3f8159ea94ef6170c069b58575c381cadc636c3c/Models/InstallationManifest.cs
固定 0.9.4 的实际配置读取：
https://github.com/optiscaler/OptiScaler/blob/7534ad00bf9e590eedb99e8dd9fd8c89dae3654f/OptiScaler/Config.cpp
实验性 OptiFG 限制：
https://github.com/optiscaler/OptiScaler/wiki/OptiFG
```

源码快照哈希：

| 对照文件 | SHA256 |
| --- | --- |
| 客户端 FrameGenerationConfigurationService.cs | `7e392d3768a62384ab55491adfae125a2ce46dd1af245d3a08208e69ffaad9d6` |
| 客户端 GameInstallationService.cs | `05ef5fa02facccf34af449bd6e2b047d0c719ae7c76591c594c6b1cbfd235c92` |
| 0.9.4 Config.cpp | `2a689100b5398060b4951918746799050d5442eb963676b4932433dcd3bf5ed3` |

关键区别：客户端通用逻辑会写 `Fsr4ForceModel`，新版本 HUD 配置也与旧包不同；
固定 0.9.4 `Config.cpp` 不读取 `Fsr4ForceModel` / `LoadCustomAmdxc64OnRdna2`，
因此 YMCC 不照抄未知键，而使用固定包支持的 `UpscalerIndex`、`Fsr4Update`、`Fsr4ForceEnableInt8`。
HUD 修复写在旧包真实读取的 `[OptiFG] HUDFix`；XeFG x2 为 `InterpolationCount=1`。

验证结果：

- `tools/optiscaler_selftest.py`：49 项离线回归通过。
- `tools/tdp_protocol_selftest.py`、Python 编译检查：通过。
- `pnpm run type-check`、`pnpm exec vite build`：通过（Vite 仍有主 chunk 大小提醒）。
- 重新编译的 EXE + **真实缓存只读副本**：
  `0.9.5-pre4 / FSR_4.1.1b` 和损坏新缓存后回退的 `0.9.4 / FSR_4.1.1`，
  FSR / XeSS 共四组 analyze、dry-run、安装、卸载均通过，原游戏夹具逐字节还原。
- EXE SHA256：`C3FC706E966895485308D0410A39D4B5E6810FC14FDF64C9906D0FC8195AC9D1`，
  已同步发布锁定清单；保留原有 `_internal` 发布运行时，未塞入新的 FSR/XeSS 缓存 DLL。

所有游戏写入测试仅限隔离夹具；真实缓存前后哈希未变。未启动真实游戏/OPT 客户端，
未进行 AMD/Intel 掌机实机画质、性能或驱动兼容验证，也不保证反作弊文件识别覆盖所有在线游戏。
