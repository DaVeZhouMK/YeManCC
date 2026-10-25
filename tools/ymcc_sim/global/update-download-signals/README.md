# Update predownload consumer gate

`adapter.mjs` exports pure static `getUpdateDownloadAdapters()`; `index.mjs` re-exports it and exports `runUpdateDownloadRegressions`. Parent owns shared global registration.

```js
const result=await runUpdateDownloadRegressions({repoRoot,evidenceDir,timeoutMs:90000});
// Accept only PASS. The evidence directory must be new and outside the repository.
```

```powershell
node tools/ymcc_sim/global/update-download-signals/index.mjs --evidence-dir <fresh-dir>
node --test tools/ymcc_sim/global/update-download-signals/index.test.mjs
```

Options: `--repo-root`, `--timeout-ms` (500..300000), `--pwsh-path`. Exit 0 means PASS, 1 means contract/behavior FAIL, 2 means BLOCKED. An old evidence directory cannot be reused. Missing cached tooling, source, actual compiled-input hashes, native feedback, a complete child receipt, or timeout cannot pass. Source bytes are hashed at esbuild reads and compared with the initial and final snapshot. The existing gated Windows Job owns only these test children and descendants.

The gate preserves and runs the four original test files and assertions: scheduler (30), predownload manager integration (8), service/game admission (8), successor (11): 57 original checks. These original tests use a fake clock and memory WebView IPC. The original integration deliberately calls the explicit install API, whose reply is an inert memory fixture; no native handler, file write, executable or installation runs. New automatic consumer samples prohibit any install command independently of PASS labels.

23 additional behavior samples execute real `updatePredownloadService.ts`, `updatePredownloadScheduler.ts`, `updatePredownload.ts`, `updateManager.ts`, `yeman.ts`, `ipc.ts` and exported `APP_VERSION`. Only game admission is injected using the original service-test boundary. Network/user-storage traps record forbidden attempts for every bundle; all must be zero. No actual game process, user settings/session/token, downloaded package, C installation path or native handler is accessed. Imported bridge modules not called in this slice remain compiled inputs, not behavioral acceptance of their functions.

Scenarios cover single preparation/notice, bounded failure delays, offline/resume, concurrent deduplication, disabling while checking, stopping while refresh is pending, retry after initialization failure, bad policy, cancellation/target changes during presentation, fractional persisted retry counts, schedule-write failure, game pause, newer subscription beating an old read failure, invalid UI receipt, replacement-provider epochs, setting-write failure feedback, literal native activity admission, idle resync preserving a healthy transfer, unknown download POST ownership, cancellation timeout with no replay, recovery from matching native completion proof and rejection of late original replies, and the exported-client version/manifest fence. The version test uses actual `APP_VERSION` and a derived newer synthetic version; it is not a release-package/export/installation verification.

The new consumer samples use synthetic target identities and memory request/response fields marked `source-derived-fixture`. Fake clocks do not prove native HTTP idle timeouts, Range resume, hashing, disk atomicity or physical OS sleep. Unknown transfer completion remains pending until the same operation's actual memory native state says downloaded/inactive; no wall-clock retry sends the POST again. UI feedback is the reactive public provider seam; full SettingsView rendering is outside this slice.

Product fixes are confined to three authorized files: scheduler policy commit/validation and ownership/target/notification fences; service acceptance of literal true activity feedback; provider snapshot validation, read/write revision failure feedback. Existing manager ownership logic was directly executed and preserved. A final idle-resync regression preserves the original healthy-transfer behavior while adding strict negative activity acknowledgment.

Descriptor: `network.update-predownload-signals`, domain `network`, scope `behavioral-mock`, kind `node`, timeout 90000, required results `update-download/result.json` and `update-download/worker-result.json`. Registration performs no probing or IO. Game policy source is reference-only because its status is injected; all actual compiled inputs, vendor module reads, bundle hashes and the injected-boundary source hash are in the result.

Results expose `sourceChanges`, `sourceHashes`, `actualCompiledInputs`, `executedProductModules`, `inputConsumerStateFeedback`, `originalTests`, `ioGuards`, `compatibility`, `sourceVerification` and `remainingGaps`. Required cases, actual request counts, matching completion proof and forbidden commands are independently validated. Tools test false-success receipts, unknown POST replay, deleted proof, rejected activity, missing original/compiled receipts, isolated actual source-byte drift, stale evidence, missing runtime, no child receipt and real bounded timeout.

2026-10-20：同一消费者入口增加后台无进展/桥接恢复、清理缓存与晚到启动/清单/下一版探测、清理静默期及手动下载保护。worker 必须提供 15 组稳定性回执；游戏恢复场景执行正式 gamePolicyTarget/GamePolicyHysteresis，仅注入其底层 gamedetect IO，覆盖首次失败后恢复、游戏进入、短暂缺失、退出确认失败后再成功、原生活动握手失败后周期放行及游戏中手动下载。原有 23 场景和 57 断言保留，合同拒绝缺少新增回执或 IO guard 的假 PASS。实际网络和本机安装均不执行。
