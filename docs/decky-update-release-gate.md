# Decky 双通道更新：YMCC 发布必检

2026-10-18 用户要求：Decky 可先用自身更新；内部通道不可用或失败时，随 YMCC 本体整包更新。此节点是每次 YMCC 发布前必须通过的检查。

## 两个真实通道

- **Decky 自身更新**：受支持 Linux 平台调用原 Decky `updater/check_for_updates` / `get_version_info` / `do_update`；明确确认前重新核对当前版本、目标版本、来源和平台文件。请求返回不当作升级成功，服务重启后必须重新读取实际目标版本。断线/超时进入未确认状态，只读复核，不重复安装。
- **YMCC 兼容更新**：原生通道不可用、明确失败或没有新版本时，保留 YMCC 原 `updateManager` 的整包检查、下载/hash 校验、明确确认安装/重启和失败恢复。Windows 使用官方公开版原字节，通过此通道更新，不再重打包 Loader；禁止下载 Linux 产物。

两者独立显示实际状态。原生更新进行中或结果未确认时禁止并发执行 YMCC 安装；旧的 YMCC 确认弹窗也须重新核对原生通道状态。没有已发布的新包时如实显示状态，不伪造远端版本。

## 不可省略的三个阶段

| 阶段 | 入口 | 必检内容 | 失败结果 |
|---|---|---|---|
| Build | `tools/build-workspace.ps1` | 当前构建身份、原生通道与备用策略、商店旧入口、新更新服务/实际 TSX、真实认证 C++ 回环与实际更新管理器、下载续传、游戏生命周期、普通文件恢复 | 不输出 BUILD_OK |
| Package | `tools/package-release.ps1` | 发布候选 ZIP 中 Loader / 插件 / 主程序 / 网页逐件匹配本次构建；临时目录中完整安装、YMCC 启动握手失败恢复、子程序健康检查失败恢复；保留用户配置和完整事务 | 在发布候选前终止 |
| Verify | `tools/deploy-installed.ps1` | Build / Package 成功回执存在，当前源码指纹、主程序和 Release ZIP hash 均匹配 | 在安装目录写入前终止 |

门为 `tools/decky_update_release_gate.ps1`。检测脚本和安装链本身已纳入 `release-export-identity.ps1` 的源码身份；修改检测逻辑或使用旧回执不能放行。没有跳过开关。

回执写入任务工作区 `Build/Validation/DeckyUpdate/BUILD.json`、`PACKAGE.json`、`VERIFY.json`；其中保留对应源码、产物、测试日志哈希。发布候选 ZIP 安装链使用明确的 `PackagePath / ManifestPath`，不误用此前 Release 包。

## 证据边界

检测全程使用临时目录、隔离后端和原生下载 fixture。真实认证传输与正式管理器会执行，但测试不会操作当前 Steam、安装第三方插件、升级用户软件或重启游戏。实物手柄、SteamOS / Steam Beta 和新更新行实机验收必须单独记录。源码编译、进程回执、旧版本截图均不能代替这些验收。

## 0.0.36 开发交付补充

本地开发安装在最终成功前调用 `Write-ExportInstalledIdentityFromArchive`，确认已安装的 Native/Web/Decky 匹配后，从已验证的完整包同步 `export-identity.json` 并保留旧记录备份。59 项导出身份回归已加入 Build 强制检查，防止“二进制已更新，构建身份仍旧”的假象。此次补充需在 0.0.36 批次重新构建/打包并重新取得三阶段回执。用户已经应用的本地开发补丁不等于线上 0.0.35 包已经包含这些代码。

## 官方 Loader 依赖门（2026-10-19）

唯一身份为 decky-plugin/vendor/decky-loader.lock.json；生成 native/decky_loader_identity.h。升级流程和示例直接写在 tools/decky-loader-upgrade.ps1 中，Prepare / Apply 自动同步四个受控文件并保留备份；正式程序不新增更新器 EXE。各本地阶段增加官方字节/PE/许可证/生成头核验；Build 执行官方消费者、升级失败回滚和原生启动/环境回归。GitHub Publish 前新增 ReleaseGate，在线核对最新官方稳定版、Windows 资产 SHA256/id/size 和源码提交；网络失败、版本落后或身份变化均拒绝发布。CI 不改 tag 中的源码，先在主线准备并提交依赖，再打发布 tag。
