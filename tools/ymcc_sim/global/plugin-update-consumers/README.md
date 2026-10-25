# 独立 YMCC 整包更新场景

新增 plugins.ymcc-package-update-consumer，复用原 tools/decky_ymcc_update_selftest.mjs 的 9 项检查，真实编译生产 LoaderUpdateRow/YmccPackageUpdateRow、updater client/protocol 与 supportsOfficialUpdaterConsumer。不复制测试框架，也不改原断言脚本。

它沿现有 runCase、esbuild 观察器和五成员工具身份门执行，保留原 90000ms 总预算及每个原子生产器 30000ms。声明闭包包含真实四个生产 TS/TSX 模块、原脚本、package.json 和原脚本直接读取的锁文件；实际 metafile 与声明须精确一致，来源/工具执行前后须不变。

两个私有、各仅修改一个生产来源的语义反例分别断开 YMCC 选择器和接受预发布 API。原测试在正确的命名案例处抛出原 AssertionError 后，新增 uncaughtExceptionMonitor 只记录异常，不捕获、不修改、不压制退出。必须核原脚本 SHA、案例源码行、错误码及 actual/expected/operator，并核原程序实际 exit1 和编译图；编译失败、超时或无关错误不能通过。原 9 项无聚合失败 JSON，因此负例保留原 stderr 与原异常记录，不制造旧报告。

新 ID 的 result.json 独立 PASS/FAIL；旧 plugins.dual-update-consumer 保持原 profiles、原 20 项/反例、历史文件和 BLOCKED。新 9 项不替代旧 20 项，也不代表完整安装或旧独立原生更新能力。新场景不执行真实网络、安装、Steam/Loader、用户存储或设备。

# 插件更新消费者与已退役的独立原生通道

`plugins.updater-service-consumer` 继续执行原 `decky_updater_service_selftest.mjs`，保留原 26 个场景、哈希准入反例和正式 service/client。此轮未改变该服务消费者的业务脚本或断言。

`plugins.dual-update-consumer` 的旧 `deckyNativeUpdate.ts` 已从产品删除。当前生产 `deckyLoaderUpdate.tsx` 的 `LoaderUpdateRow` 始终选择 `YmccPackageUpdateRow`，使用现有 `deckyUpdaterClient.ts` 和协议；旧 `decky_dual_update_selftest.mjs` 也已成为 `decky_ymcc_update_selftest.mjs` 的历史别名。原 Linux 原生安装、未确认安装后的 fallback 等 20 项合同无法等价接线。

因此该 ID 保留登记并明确返回 `BLOCKED` / exit 10，原因是 `PLUGIN_UPDATE_NATIVE_CHANNEL_RETIRED_NO_EQUIVALENT_CONSUMER`。不重建旧控制器、不提供 stub，不将当前 9 项 YMCC-only 结果计为原 20 项通过。原 `source-inputs.json` 原字节另存 `source-inputs.historical.json`，原路径、20 个场景和故障锚点均保留；当前清单中的 `retirement` 记录历史哈希及实际替代来源。

直接执行该消费者时，`retired-channel.mjs` 仍通过原别名运行实际当前生产行选择及官方消费 API 准入的 9 项检查，保存原报告、两个真实 esbuild 编译图、精确输入与工具哈希、自然退出回执。这个后续结果只证明当前 YMCC-only 软件范围，整体旧合同仍为 BLOCKED。声明集变化、历史合同变化、原脚本变化或编译闭包不符均拒绝。

输出继续要求绝对、新鲜、源码库外的证据目录。原 D:/Codex 与 G:/YeManCC-Work/Build 兼容入口保留，同时接受正式核心测试区 Testing/YMCC/Evidence 与 Workspace。无下载、安装、Steam/Loader、真实睡眠、设备或用户现场应用操作。
