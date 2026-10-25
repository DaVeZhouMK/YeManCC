# 性能模拟器真实草稿与保存消费者

叶 ID：`performance.simulation-draft-consumer`。父端负责最终注册；本模块不写产品、registry、runner 或 audit。

## 权威与运行

复用实际根的 `bundled-node-tool-authority`、`snapshot-node-tools`、原 UI 声明及 source-snapshot 验证器，不以 B48 SHA 或文件数作为未来输入条件。

- 固定源码：调用者必须显式提供该根 binding 绝对路径及 expected SHA（API 的 `binding`，或原 `YMCC_BATTERY_UI_NODE_TOOLS_BINDING` / `..._SHA256` 环境变量）。验证 binding 所指真实 manifest、完整捕获、实际读取逐文件成员和完整 Node/UI 工具图，结束再次验证。
- 主线：按 `source-inputs.json` 的明确真实源码列表建立 before/after；保持原 sourcePath 权限与重解析边界、完整工具图和原 UI 目录成员校验。没有 manifest 的模式不会伪造完整捕获或 primary current 结论。原标准合同拒绝把 snapshot binding 转发给主线。
- 工具总预算仍为原 **3072**；当次 package/file/UI 数、manifest/binding SHA、creation/current 状态均从本次真实权威记录。工具缺失、错误 SHA、源漂移或未声明实际读取不能通过；描述符公开 `blockedReason`。

CLI：声明的 Node 执行 `run.mjs --repo-root <真实绝对根> --evidence-dir <不存在的绝对case子目录>`；固定输入可再传 `--binding <绝对路径> --binding-sha256 <expectedSHA>`。叶 API：`await runPerformanceSimulationConsumer({repoRoot,evidenceDir,binding})`。完整成功打印 `PASS performance.simulation-draft-consumer`。诊断参数 `--only` / `--skip-original` / `--skip-negative` 无权生成 READY 完成标记。

## 实际消费与原门

直接执行完整真实 `performanceSimulation.ts`、`performanceSchedule.ts`、`PerformanceSimulationWindow.vue`、`ContinuousPerformancePanel.vue`，iframe 使用真实 index/prototype/app 及既有模型。只将系统/配置边界换成内存端口，没有平行草稿状态机。14 组真实消费者在 1280×720 / 1280×800 形成 28 个用例，覆盖零写入临时编辑、显式 Save、供电/游戏/revision 旧草稿拒绝、64条缓存淘汰、非法草稿、重复保存、失败重试、关闭/重开、晚到读取/回执和退订。

原 pipeline 的 11 个报告组、原 split 的全部断言独立执行。原 split helper 缺少 `removeGameCustomProfile` / `restorePerformanceScheduleIfConfigured` 导出，原命令在编译阶段仍是 FAIL；不计作业务负例。`original-producer.mjs` 是需要长期保留的测试适配源：只在 D case 内逐字节复制 producer，修复 helper 的源码根绑定及两个未使用内存导出（若意外调用会抛错），不删改任何原断言。`unadapted.pass=false` 与独立的 repaired-helper PASS 一起保留；28 个新用例不能替代原断言门。

两个 D 单文件变异门保持不变：省略正式 Save writer、遗漏卸载退订；必须先编译并进入真实页面，再在指定业务断言失败，编译/缺依赖错误不算检出。

## 证据与交付范围

产物全部置于 case 的 `output-evidence/`，使用原公共 `sourceHashes` 通道；产品、仓库工具、外部工具、模块与输出分别列出。完整声明工具 before/after、actual 读取和 PID/server 关闭账本均保留，不过滤 node_modules，不放宽预算/runner/audit。浏览器仅 owned headless Edge，origin 白名单精确到本次自建端口，自建拒绝代理；没有用户浏览器或 native WebView 通道，系统桥完全内存化。

保持轻度2D、原25%晃动和现有算法/功能状态；global active、joint=false、CPU功耗收益=null。历史 B48 的旧固定 pins、28 用例与原正反门 PASS 保留在原回执及 module-source 副本，属于旧版本证据。可复用权威改造只做定向接口验收；当前版本的整套消费者由父端在新固定输入统一实跑，旧 PASS 不重签为当前版本 PASS。

同步注册建议（父端 global 模块，不改公共接口为异步）：

```js
import {getPerformanceSimulationConsumerAdapters} from './performance-simulation-consumer/adapter.mjs';
const added = getPerformanceSimulationConsumerAdapters(repoRoot, {binding});
// 父端检查 blockedReason 后，按现有注册方式纳入 added。
```

描述符生成由共享的同步 lockToolsSync 完成，直接返回数组，兼容 getExtraAdapters() 的同步 spread。它与异步 lockContext 共用同一标准 Node/UI 工具权威；完整捕获、实际 manifest 成员、selected before/after 仍在 run 的异步路径执行。共享 helper 使用永久布局的静态相对 import，D 验证使用完整复制源码视图及其标准新捕获，不靠绝对 G 动态 import。

