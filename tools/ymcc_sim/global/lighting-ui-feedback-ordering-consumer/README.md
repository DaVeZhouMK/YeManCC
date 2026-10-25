# Lighting UI feedback ordering consumer

独立 public leaf：`lighting.ui-feedback-ordering-consumer`。父入口登记后统一运行；本叶不修改共享登记。

正例默认运行维护中的 `../lighting-signals/ui-feedback.mjs`，不传 `--component-source`，直接编译当前 `src/components/ControllerFeedbackCapabilitiesCard.vue`。沿用 `tools/controller_lighting_operations_browser_selftest.mjs` 全部原期待，并执行原 4 个回执及 1280×720、1280×800 各 4 个乱序场景。实际控件、debounce、Vue 组件、DOM status 和 API 参数均参与消费；bridge/settings/focus 是原有显式内存夹具。

2026-10-20 当前合同升级为 lifecycle-aware-two-guard-ordering：正式组件SHA为 `173b40da1a0fc3c1301af26c1c1b8174c6963526ba5722583bb47e75c49b30b8`。私有反例仅移除成功/异常路径各一条 `if (request !== ledApplyRequest) return;`，私有源码SHA为 `d2cf176e8110cb3dd1b3035913d8285c1c23ab11bf261f47232a6a737c5a0acd`；请求计数声明、递增和全部退出生命周期保留，避免旧四行删除制造无关ReferenceError。当前私有源码不是历史发布组件，不将旧PASS改签为新源码验收。

旧合同保留在verify.mjs的HISTORICAL_CONTRACT：current `f1946ffc02c6cc6930378b8f0f1b8051b366a100254c12d0c0fc41370428707f`、before `b3c5c29961115196dee570d86dd05077aaaca95943df4e051ad65d03bfbdaa9c`，原四行历史字节与归档回执按该时代解释。公共 helper 仍锁定 `879a1a0feb9a3660b9803d83048b198fa16e2112f2bbd6e792d68a4c5e88bed7`，原浏览器基线仍为 `d74af5ef651cc3a611ad6294d8fc1b803081b7c29b0c011ff7ce54d0c2180721`，没有修改消费者期待来制造通过。

检出要求：原 4 回执仍 PASS，8 个乱序行全部产生各自历史旧文本，且命中公共 helper 原来的 `latest issued lighting request must retain its own receipt after older replies` 断言，原失败计数恰为 8。编译、启动、超时、其他断言或不完整终态都不会计为检出。正/反例生成的 probe 除组件路径外必须完全相同。

`source-inputs.json` 是实际组件/控件/图标/CSS闭包；`adapter.mjs` 另外声明公共 helper、原浏览器夹具、叶实现及公共运行器。`tool-inputs.json` 固定声明 Node、esbuild、Vue 编译器和浏览器客户端模块、浏览器运行时 ESM、Edge 当前版本顶层运行文件及全部 Locales、PowerShell 执行所需核心文件，总数 332，保持 `artifactInputs <= 640`。不将安装器、更新器或 ResiliencyLinks 备份作为浏览器运行文件。

`observe.cjs` 通过本叶 `Run-Helper.ps1` 的 `NODE_OPTIONS` 预加载，观察实际 helper/probe PID、真实 spawn/spawnSync ledger、源码读取、CJS模块、esbuild metafile 和输出。编译器服务在原程序已结束的 `beforeExit` 阶段关闭 stdin 并等待其真实 close，保留退出码；不 kill 或合成终态。Edge 必须由原 Playwright 夹具以 headless 和临时 profile 启动并自然退出。执行包由既有 `host-process.ps1` 的自有 Windows Job 约束，失败不清理其他进程。

每轮保存输入快照、输入/工具前后哈希、正反例原 stdout/stderr、公共 helper 的 `out/ui-feedback-cases.json`、`ordering-result.json`、`feedback-result.json`、浏览器原产物、编译输出、观察报告、真实 childLedger、私有前像证明及最终 `result.json`。新发现但未登记的源码/工具、哈希漂移或缺失闭包均拒绝放行；不在运行时自动扩大审定清单。

父登记所需：

```js
import {getLightingUiFeedbackOrderingConsumerAdapters} from './lighting-ui-feedback-ordering-consumer/adapter.mjs';
// 工厂表达式（沿父注册文件既有合并模式使用）
...getLightingUiFeedbackOrderingConsumerAdapters()
```

必要验证器单测（不运行业务）：

```powershell
$env:YMCC_LIGHTING_TEST_ROOT='D:/your-owned-evidence/lighting-validator-unit'
node --test tools/ymcc_sim/global/lighting-ui-feedback-ordering-consumer/runner.test.mjs
```

源码/工具发现仅做读取及模块装载，不启动产品、Host、硬件或用户 profile。完整 public 和 selectedAudit 由父代理唯一执行。导航由独立 `controller.ui-navigation` 门承担；本叶不证明实体灯效、Host集成或安装版验收。


2026-10-19 cleanup correction: the original browser.close finally also waits at most five seconds for this observer's already recorded compiler children to end through stdin EOF and actual close events. This applies identically to positive and private-fault runs, preserves the original AssertionError and exit status, and never invents child terminal facts or kills a compiler. The normal beforeExit EOF fallback remains.
