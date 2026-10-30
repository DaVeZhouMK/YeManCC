# YeManCC 0.0.37 正式发布环境准备

2026-10-30 用户已授权正式发布，当前进入最终源码冻结与发布验收；通过前不宣称已发布。版本唯一来源为根 `version.json`，由 `scripts/write-version.mjs` 生成前端和原生版本；根 package 与子库 packageVersion 必须一致。

## 固定环境与入口

- Node 22.14.0、pnpm 11.16.0、.NET SDK 10.0.400；沿用已核验的 Windows MSVC/SDK。
- 本地候选：`tools/publish.ps1 -WorkspaceRoot <独立目录> -TestExport`。
- 准备 CI：`.github/workflows/release-prepare.yml`，默认 EnvironmentCheck；手动 full_acceptance 才执行原 FullAcceptance。两者均无发布权限。
- 正式 CI：`.github/workflows/release.yml`，仅正式标签触发，仍须完整验收；不得用成功的输入捕获或本地测试包替代。

## 发布前保留门槛

- 最终冻结源码、Native/Web/Decky/模块与 ZIP 必须同源；不得使用旧测试包或旧源码 ZIP 充当正式附件。
- 保留 HC34、单份 HIDM、无 ymcc-input-payload.zip、固定灯光 Core、Fan Host v2 与配置基准校验。
- 重新核验 035→037 和本地036→037，自动/手动更新、运行中退出、失败恢复、游戏暂停及跨版本替代分别留证。
- 旧全量 CI 的全局注册与历史绝对路径阻塞仍须对最终源码解决并执行；环境准备不抹去旧 FAIL/UNKNOWN。
- 上线前在线复核官方 Decky 稳定版、SHA 与来源提交；用户授权正式发布后才推不可变标签，公开附件下载校验完成后才更新客户端清单。

准备证据保留于 `G:/YeManCC-Work/Build/GitRelease/20261029-v0.0.37-preparation`；正式轮证据位于 `G:/YeManCC-Work/Build/GitRelease/20261030-formal037`。按用户授权执行最终验收与发布，不部署或启停本机安装版。实际执行结果和未验收项分别记录。
