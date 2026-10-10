# YMCC 控制台 resource-missing — 现场修复与日志

日期：2026-10-10（香港时间）。

## 根因与实际处理

不是正常安装包的 Loader 缺失：`C:\SOFT\YeMan\PowerControl\decky` 的 pinned Loader 与 Mainline23 插件哈希正确。

用户级和当前进程级 `YEMAN_POWER_CONTROL_DIR` 都指向 `G:\YeManCC-Work\Mainline\Temp\ai-cpu-bundle-R11-rebuild\PowerControl`。原 native resolver 允许有效目录覆盖，控制台随原 PowerControl owner 检查该目录；该目录原先没有 decky 资源，而且最新业务设置仍保存在该目录。

已将 **6 个已校验资源文件** 补到该实际目录的 `decky` 子目录：Loader、Loader LICENSE、插件 JSON/包描述、插件 LICENSE、dist/index.js。只补资源，不覆盖不同哈希的已有资源，不启动 Loader，不重启 Steam/YMCC，不操作硬件。原 yeman-settings / game-custom 若存在均作前后哈希验证，保持不变。

随后执行精确生产 `deckysetup::inspect`（真实 SHA256 digest）的只读探针：`ready=true, reason=ready`。这证明缺文件问题已解决，不宣称 Loader 启动、native bootstrap 或真实 YMCC 接管已经通过。

没有直接清除用户级覆盖变量：当前设置还在旧临时根，切回正常安装根可能改变业务配置。统一数据目录需另做明确的配置保留/迁移，不把它混入控制台资源修复。

## 日志与错误提示改动

- 新低频日志：`%LOCALAPPDATA%\YeManCC\decky-sidebar.log`。
- 记录实际 PowerControl / Steam / Loader / plugin 目录、目录来源（生产默认/环境覆盖/无效覆盖）、缺失路径、运行阶段、加载器 PID、Windows 错误码、有限重试次数、显式启用请求和保存失败。
- 同状态重复查询去重；不增加轮询。单文件上限 1 MiB，仅保留一个 `.1` 轮转档；沿用原日志轮转函数。
- 字段允许名单排除 token、endpoint、配置内容、游戏身份、请求参数等；不记录 bootstrap 凭证。
- 日志失败 best-effort，不影响资源校验或原业务控制。原日志导出已包含该日志及轮转档。
- 环境检查返回实际路径与来源；Steam 页面设置失败会做一次只读回查，显示可读原因、具体路径和诊断日志位置，不再只显示裸 `resource-missing`。旧 native 不返回新字段时仍兼容。

## 验证

证据目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Resource24\validation`。

- RESOURCE-REPAIR.json：实际资源修复、原业务哈希、未启动进程/硬件。
- ACTIVE-RESOURCE-INSPECTION.json：修复后精确生产检查器返回 ready；只读且 0 settings/Steam writes。
- SETUP-SELFTEST.json：20 项隔离原生检查，包含缺失路径、哈希保护、Steam 标记保护、诊断敏感字段排除和大小上限。
- DIAGNOSTICS-SELFTEST.json：6 项可读提示、旧版兼容、根目录来源、日志去重/导出契约。
- WEB-BUILD.txt：类型检查、Vite 构建通过。

日志和提示代码在主线与新候选中，**没有热替换正在运行的安装版 EXE**。当前运行版能立即使用补齐的资源；新的原生日志必须部署新构建并正常重开 YMCC 后才生效。没有用这次资源检查冒充前轮错误 5 的启动闭环。
