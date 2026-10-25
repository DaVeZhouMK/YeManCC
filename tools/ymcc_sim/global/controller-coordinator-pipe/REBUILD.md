# 可再生的 controller coordinator prerequisite 工序

本文件描述可复用代码接口。本批固定源、D 编译目录和 SHA 只存在于各批生成的 JSON/账本；它们不是下批测试代码的永久依赖。无需安装版、掌机或外部节点。使用选定完整固定源码及其已验证的 input bindings，在新的 owned 目录重建。

```powershell
# 均替换为下一批实际绝对路径；runRoot 必须不存在，且不能与 repoRoot 重叠。
$repoRoot = '<选定完整固定源码根>'
$runRoot = '<新的 owned 输出根>'
$bindings = '<对应已验证 input-bindings.json>'
$review = '<本次隔离审查 JSON>'
$refresh = '<已整合工具根>/tools/ymcc_sim/global/controller-coordinator-pipe/refresh-prerequisites.mjs'
node $refresh --stage prepare --repo-root $repoRoot --out-root $runRoot --input-bindings $bindings
# 可在 prepare 额外传 --previous-source-manifest <上一实际编译 source-manifest.json> 生成差异。
# 审阅选定源码 AGENTS、HC-UPGRADE-REGRESSION、原 fixture 和差异后，生成 review。
node $refresh --stage build --repo-root $repoRoot --out-root $runRoot --input-bindings $bindings --safety-review $review
node $refresh --stage bind --repo-root $repoRoot --out-root $runRoot --input-bindings $bindings --safety-review $review
node $refresh --stage verify --repo-root $repoRoot --out-root $runRoot --input-bindings $bindings --safety-review $review
```

prepare 按既有 capture manifest 的明确成员列表校验选定 repoRoot 并复制，不重新建立全库清单。它生成本批 root/profile 声明、选定源码和工具 SHA、PREPARE.json、SOURCE-DELTA.json，不执行编译或子夹具。输入 binding 必须证明 captureComplete、snapshotIntegrityVerified、creationStatus=PASS，manifest 和 native/main.cpp 的身份必须相符。源码成员未知、重定向或与绑定不同即拒绝。

PREPARE.json.reviewRequired 给出 review 必须匹配的 sourceRoot、bindingSha256、nativeBuilderSha256、managedBuilderSha256。review 还必须有 status=REVIEWED_BEFORE_EXECUTION、isolated=true、realSdkLoaded=false、hardwareIo=false、normalProductMainExecuted=false、osSleep=false，并匹配该批数据。这些是先读原夹具并证明边界后的审查事实；prepare 本身不授予执行。实际设备/SDK/SetupAPI/PnP/安装/已有 Host 或 Steam/真实系统睡眠一律不在本接口执行范围。无法证明原 fixture 隔离时保留具体阻塞，不试运行。

build 调用原 r3-managed-builder.mjs 与原 tools/input_coordinator_lifecycle_selftest.mjs，不创建控制器模型。选定 repoRoot 经 prepare 捕获为本批 overlay/repo，原构建器仍从该捕获副本的模块位置选取全部原源码；未修改原 native builder 或 B46 产品/fixture。托管构建根从本批声明读取。TEMP/TMP、dotnet CLI home、NuGet 路径限制在 owned 输出。

原构建顺序为 positive（原 --suite all）、cancellation、psOwnership、psBinding、admission、inheritance、switch-return、switch-owner。正例的原 12 native modes 后运行 full-pipe；三个 pipe negative 复用同一新 SDK-free managed DLL。四个 lifecycle mutations 继续各自的原模式。build 的“原工序执行完”不等于原 negative/public 合同通过。

bind 调用原 bind-r3-fixtures.mjs，并对原 --suite all 收据生成保留 source path/SHA/原第12索引的 full-pipe 精确投影；原完整 RESULT.json 不改。新 manifest 同时绑定该投影、原完整收据、新 EXE、actual source snapshots、generated inputs、compiler /showIncludes 及链接输入。新 pin 只指向本批新构件。没有更改旧二进制 expected SHA 或重签旧收据。

verify 仍依次执行原 positive、cancellation、psOwnership、psBinding、lifecycle 叶入口；失败原样保留。没有调用公共 registry/frontdoor/audit，这些由父端最终整合后负责。原完整 fixture 的新组数不能直接替换旧断言合同；原负例新添失败不能自动列入允许集合。

| 工序 | 原预算 |
|---|---:|
| 单次 managed 编译工具命令 | 60000 ms |
| native 原编译 | 300000 ms |
| native builder 各 native mode / full-pipe | 45000 / 90000 ms |
| pipe 原叶入口外层 / 其实际进程 | 120000 / 60000 ms |
| lifecycle 原叶入口外层 / 各实际进程 | 90000 / 45000 ms |
| inspection / hard cap | 10000 / 30000 ms |
| 每 case 输入限额 | 640 |

核验材料保留 refresh-process-ledger.json、原 stdout/stderr、source-manifest.json、RESULT.json、evaluated-inputs.txt、runtimeArtifacts、actual compiler include/link/tool ledger、managed receipt 的实际 loadedAssemblies 与输出 SHA。编译工具声明/编译观测/实际托管加载分别记账，不把声明当成已加载，也不把全 TU 编译当成全部分支执行。

原未知源与安全前置拒绝、原 case 顺序、断言、negative 与预算保持。若下一批真实源码变化导致原语义失败，交付 failure；不修产品或自动放宽门。本批接口已用实际固定绑定成功 prepare 至另一新 owned 根，并验证未知源、缺隔离审查、输入输出重叠和外部 artifact 均拒绝；未重复整套编译来制造第二套 PASS。
