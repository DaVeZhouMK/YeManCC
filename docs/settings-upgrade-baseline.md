# YMCC 配置升级基准 v1

建立日期：2026-10-23。配置合同 ID 为 `ymcc-settings`，revision 为 `1`；它独立于产品版本号。当前正式产品版本仍为 0.0.35，036/037 仅用于隔离升级验证。

## 固定入口与默认快照

- `PowerControl/settings-baseline.json`：所有权、迁移、保留和发布规则。
- `PowerControl/settings-defaults-v1.json`：从实际 `normalizeSettings({})` 捕获的无用户数据默认值。构建门逐项核对实际默认值，禁止悄悄改快照来跳过检查。
- 原生 `settings.location` 确定有效 PowerControl 目录；统一文档为其中的 `yeman-settings.json`，备份为同路径加 `.bak`。
- `baselineId` 保留原来的用户默认方案含义。新增 `configurationBaseline` 保存独立的迁移回执：`{ "id": "ymcc-settings", "revision": 1, "legacyImported": true }`。

## 现有数据归属

| 范围 | 处理方式 |
| --- | --- |
| ui、background、music、gamepad、performanceSchedule、tdp、rtss、cpu、sleep、quickApps、startupDesired、power、tray、autoclose、fan、input、extensions | 18 个默认结构分区。保留用户值与未知字段；补齐缺项，修复已知对象/数组结构；input 保留 CAS、revision、主机回执与模块所有权规则。 |
| gameCustom | 属于上述 18 分区的兼容入口；当前游戏个性化配置以独立 `game-custom.json` 为准，不能从兼容分区恢复已删除的游戏项。 |
| radialMenu、screenTouchpads、steamMonitor | 模块自己管理结构与版本；共享仓库只保留，不强套其他模块默认值。 |
| domainLogs、steamDeckMouse | 原生拥有的日志开关与写入回执；保留原有原生保护和事务规则。 |
| 21 条旧配置导入路径 | 精确清单在合同 `legacyImports`。仅首次迁移时用于补齐缺失分区；不少路径仍有兼容读写者，暂时保留原文件。 |
| game-custom.json/.bak、ui-background/dynamic.json | 独立用户配置，保留，并从发布模板中排除。 |
| HIDM/InputHost、BUS、Fan Host/HC、Decky、Steam 外观受管资源 | 沿用各模块清单、共享维护锁、重启围栏及资源升级链。配置基准不会安装/卸载驱动或重置模块状态。 |
| Steam 用户目录、config_USER.json、OptiscalerClient/LS 设置、日志、运行租约、升级恢复事务与缓存 | 各自所有者继续管理；不是统一配置的清理对象。 |

`tools/settings-baseline-contract.mjs` 只读扫描当前源码中的共享配置分区消费者；新增命名分区需要登记归属。动态扩展和未知字段继续保留，扫描不构成整机所有配置的穷尽证明。

## 本次升级行为

1. 先读取原生目录和当前文档；schema 或配置基准超出支持范围时保留原字节、停止自动写入，显示现有恢复界面。
2. 没有基准回执的旧文档，仅在缺失分区时读取 21 条旧导入路径；现有统一文档里的用户设置优先。
3. 使用当前结构规范化，在原生锁内提交相对于原文档的实际差异，并保存迁移回执。整个迁移不依赖版本号伪装。
4. 回执提交完成后，后续缺失/新增分区使用本版默认值，不再重新读取旧镜像文件。首次 input CAS 也必须先完成这一步。
5. JSON 对象顺序不影响判断。健康文档不会因为启动、再次升级或字段排序变化而重复写盘。
6. 中断或写入失败不发布完成回执。下次显式读取可重试；原生的增量提交继续保护并发用户变更、input revision 和原生回执。
7. 可兼容的备份允许恢复。两份文件都损坏/为空时，前端和普通原生写者都拒绝覆盖。用户明确选择“备份并重置应用配置”后，先保存两份原始字节，再重建应用自有分区；保留 input、fan、gamepad、startupDesired 及未知字段，并关闭 CPU/TDP 自动应用以便检查。

旧版本无可靠基准时不实施整目录删除。新基准给未来有范围、有归属的迁移提供起点。当前已废弃的 `sleep.steamOverlayOffFix` 继续强制关闭；其他未知字段不按名称猜测删除。

## 发布约束

构建前核对合同、默认快照、原生版本上限和实际导入路径；全部输入纳入源码身份。打包复制当前不可变基准与默认快照，并在 `update-manifest.json` 的 `rules.configurationBaseline` 中绑定它们的 SHA256。

发布过滤器排除共享配置、已登记旧配置、独立用户配置及其 `.bak`、`.corrupt-*`、`.before-user-reset-*` 存档。独立的成品检查会再次拒绝任何已知用户配置进入包，拒绝基准/默认快照的字节漂移。未知文件仍按原模块发布规则处理，不能把这份有限清单当作任意文件的删除权限。

PowerShell 5.1 与 7 都必须能解析发布入口。原发布脚本缺少 UTF-8 BOM 导致 PS5.1 的 22 条解析错误，本轮仅为原文本补入 BOM；PS7 行为保持兼容。

## 后续配置变化的维护方式

- 调整默认结构/语义时，先比较 v1 的冻结默认值和真实消费者，决定是保留用户值、填缺项、转换已知字段，还是明确退休某个受管字段。
- 有持久化语义变化时递增合同 revision，保存对应版本默认快照，并同步原生支持上限、精确源码快照白名单和导出源码清单。不要把产品版本号用作迁移状态。
- 新字段优先填缺项。更改现有用户值必须有明确迁移规则；不要整文档覆盖或从旧文件重新导入。
- 退休文件须证明属于当前模块、没有活跃兼容消费者，并进入该模块现有的备份/回滚事务。合同 `managedResources` 和 `retiredManagedResources` 记录不可变基准资源；v1 尚无退休资源，当前没有额外的文件删除执行器。
- 不支持更高 revision 的旧程序必须拒绝写入；需要降级/重建时沿用用户明确触发的存档恢复入口。
- 自动下载只准备经过校验的包。真正安装仍走同一已确认安装链，不能在后台下载阶段迁移用户配置、关闭工具或操作驱动。

## 验证入口与边界

- `node tools/settings-baseline-contract.mjs`：实际默认值、分区所有权、旧导入路径与原生上限。
- `node tools/settings_baseline_selftest.mjs`：真实仓库/API/IPC，原生存储为内存夹具。
- `node tools/settings_baseline_native_selftest.mjs`：设置隔离 `YMCC_BASELINE_EVIDENCE`，需要时用 `YMCC_BASELINE_ENV_CMD` 选择已核验 SDK 环境；自动抽取并编译当前原生读写/恢复函数。文件、锁和硬件接纳为明确的惰性夹具。
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/settings_baseline_package_selftest.ps1 -EvidenceDir <隔离证据目录>`；同样在 PS7 执行。
- 既有 settings CAS、响应性、启动目录、浏览器恢复与完整导出身份回归继续保留。

这些证据能证明声明范围的软件行为；不代表完整 Build/Package/Verify、管理员保护事务、实际安装、驱动或实物设备验收。本轮证据位于 `Testing/YMCC/Evidence/U36P/ConfigBaseline`；前一候选的 Decky 持续生命周期门失败继续保留，不能被配置单项通过替代。
