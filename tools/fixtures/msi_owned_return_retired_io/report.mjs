import fs from 'node:fs';
import path from 'node:path';
export function writeReport(run, receipt) {
  const lines = ['# MSI retired-slot sidecar：R108 复现 / current gate 验证', '',
    '结果：**' + (receipt.passed ? 'PASS（所选双源/单源预期均满足）' : '验证未完成或失败') + '**。', '',
    '旧 R108 把 `SlotBusyRetired` 的当前 `!attempted` 判为 retry-safe，旧 I/O 未收割时可先消费唯一 retry，再以忙槽返回，本次新增 WriteFile=0。current 的真实 CommandReady 在打开新 command、claimRetry 之前非阻塞收割旧槽；在途/锁竞争时保留预算及观察，终态后才实际写一次。', '',
    '此报告是独立验证 sidecar；未修改 native 或父 runtime selftest，未调用 R109 新 Mode2Outcome / ObserveOff / Resume 分支。', '',
    '## 固定输入与边界', '',
    '- 本次 run：`' + run + '`。',
    '- 原旧证据仍在 `G:/YeManCC-Work/Build/Validation/MSI-PostExport-R109-20261016/retired-slot-review/REPORT.md`，未修改。',
    '- R108 固定 runtime SHA-256：`abb44867f56184b32efaedb98bb0309c1c557b437a69f1ed6f6606f5e1bb3d8f`；runner 强制核对。',
    '- 所有真实 runtime/recovery/journal/identity Slot 直接 include 对应冻结源。真实 command slot、writer、reaper、free、modewriter、debt helper 与 current CommandReady 从 main.cpp 定义按原字节抽取，行号和 SHA-256 见各 source-binding.json。',
    '- Ready 没有高层桩，也没有替换为手写逻辑。runtime gate 顺序有源码绑定断言；测试同时验证 busy 时无新 open/claim/write。',
    '- 复用原 R108 selftest 的底层边界副本，独立于父的 current general suite；HANDLE 表示调整和接入 actual writer 的改动逐项记录。',
    '- 时钟/进程/board/power/Host、USB 根/location、日志、持久化 gate、open/close 和 Win32 I/O 都是内存桩。没有 Windows SDK headers、真实设备、Steam、OEM、CtrlAlt、部署或睡眠。唯一测试线程仅用于制造 C++ mutex 竞争，结束时 join。',
    '- `msiCrashRecoveryBeforeModeWrite` 为显式 inert 成功桩，结论不涵盖完整 durable 实现；真正物理完成/输入/Steam 消费与 native 链接由父另行验证。', '',
    '## 构建与执行', '',
    '| 源 | 配置 | 专项 case / assertions | 原 baseline case / assertions | 状态 |',
    '|---|---|---:|---:|---|'];
  for (const [name, variant] of Object.entries(receipt.variants)) {
    for (const [profile, p] of Object.entries(variant.profiles)) lines.push('| ' + name + ' | ' + profile + ' | ' + (p.cases ?? 0) + ' / ' + (p.assertions ?? 0) + ' | ' + (p.baselineCases ?? 0) + ' / ' + (p.baselineAssertions ?? 0) + ' | ' + (p.passed ? 'PASS' : 'FAIL') + ' |');
  }
  lines.push('', 'MSVC C++20 x64，`/W4 /WX /permissive- /sdl`；Release `/O2 /MT`；Checked `/Od /MTd /RTC1 /Zi`。', '');
  for (const [name, variant] of Object.entries(receipt.variants)) {
    const binding = JSON.parse(fs.readFileSync(path.join(run, name, 'source-binding.json'), 'utf8'));
    lines.push('## ' + name + ' 来源和 case', '', '源根：`' + variant.root + '`。', '',
      'runtime header SHA-256：`' + variant.sourceHashes['native/msi_owned_return_runtime.h'] + '`。',
      'recovery header SHA-256：`' + variant.sourceHashes['native/msi_owned_return_recovery.h'] + '`。',
      '执行期间相关源变化：`' + (variant.relevantSourceChangedDuringRun.join(', ') || 'none') + '`。', '',
      '| 函数 | 原文件行号 | SHA-256 |', '|---|---|---|');
    for (const b of binding.extractedDefinitions.filter(b => ['msiOwnedReturnCommandReady', 'msiClawIoSlotReapLocked', 'msiClawCommandWriteBounded', 'msiOwnedReturnRetryable', 'msiOwnedReturnMaintenance', 'claimRetry'].includes(b.symbol)))
      lines.push('| `' + b.symbol + '` | `' + b.file + ':' + b.startLine + '–' + b.endLine + '` | `' + b.sha256 + '` |');
    lines.push('', '| case | Release | Checked | assertions |', '|---|---|---|---:|');
    const reports = {};
    for (const profile of ['Release', 'Checked']) {
      const file = path.join(run, name, profile, 'results.json');
      if (fs.existsSync(file)) reports[profile] = JSON.parse(fs.readFileSync(file, 'utf8'));
    }
    for (const c of reports.Release?.cases ?? reports.Checked?.cases ?? []) lines.push('| `' + c.name + '` | ' + (reports.Release?.cases.find(x => x.name === c.name)?.passed ? 'PASS' : 'FAIL') + ' | ' + (reports.Checked?.cases.find(x => x.name === c.name)?.passed ? 'PASS' : 'FAIL') + ' | ' + c.assertions + ' |');
    lines.push('');
  }
  lines.push('## 可证结论及限制', '',
    '- R108 原 13 个专项 case 仍确定性复现；Release/Checked 都复跑原 135 case/963 assertions，确认旧边界副本语义保留。专项 PASS 表示旧缺陷成功复现。',
    '- current 忙槽 gate 在 timeout/signaled/immediate 三类真实退休来源下均未消费 retry、未打开新 handle，未新增 Wait/Cancel/WriteFile，旧 handle/event/buffer 仍归原槽。',
    '- current 旧终态成功、取消终止、以及终态成功但 0 bytes，均可由真实非阻塞 reaper 收割；随后同 episode 发出且只发出一次实际 inert WriteFile。当前 busy 的 written=0 本身仍不构成终态证明。',
    '- 初次成功、后来另有旧退休槽也被当前 gate 挡住；try_lock 竞争时不会查询旧 I/O 或消费预算。',
    '- unknown/retained/timeout/partial/unknown-start-error 的初次结果不授予 retry；观察任务保留。两个新的同 owned root/location 物理读窗口仍可用真实 discharge helper 清债，即使旧槽仍在途，或初次 I/O 不适合重试。',
    '- ROG 冷入口/观察期间 ROG 上下文不触碰 MSI slot；20 次有界观察耗尽后不因纯时间经过而主动轮询，既有 topology 事件后仍可使用尚未消费的 retry。',
    '- 本地 sidecar 验证的是指定快照下的串行执行和特定槽锁竞争，不证明所有产品 writer 都遵守同一 persona/lifecycle 串行化；Ready 返回后到新提交间的完整并发锁序仍属于父集成边界。未证明现场 bug 曾发生、发生率、设备实际完成、真实持久化或 Steam 输入消费。',
    '- 本轮不执行/评价父的新 Mode2Outcome/ObserveOff/Resume 接线，也不替代父 native 编译/链接验收。', '',
    '## 路径与复跑', '',
    '- runner：`tools/msi_owned_return_retired_io_selftest.mjs`。',
    '- fixture：`tools/fixtures/msi_owned_return_retired_io/`。',
    '- run 内 `RESULT.json` 汇总；各源 `source-binding.json`；各配置 `results.json` / `retired-slot-selftest.exe`；`logs/` 保留编译命令和执行回执。', '',
    '```powershell',
    'node tools/msi_owned_return_retired_io_selftest.mjs --variant both --current-root "' + (receipt.options.currentRoot ?? '') + '"',
    '```', '',
    '支持 `--variant r108|current|both`、`--r108-root`、`--current-root`、`--out`；生成物仅允许在 `retired-slot-fixed` 下，时间戳 run 不覆盖旧证据。', '');
  if (!receipt.passed) lines.push('**有构建或 case 失败；上列预期结论仅在相应配置 PASS 时成立，详细失败见 RESULT/logs。**', '');
  fs.writeFileSync(path.join(run, 'REPORT.md'), lines.join('\n'));
}