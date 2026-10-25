# Monitor notice host 生命周期候选（2026-10-22）

本 adapter 直接执行未改写的 `src/bridge/monitorNoticeHost.ts`、实际 `ipc.ts`、快捷反馈 channel、notice state、更新通知 presenter 和 frameRate 常量模块。仅 WebView postMessage/事件、DOM event/theme、Date/定时器是内存端口。没有产品源码补丁，也没有把内存窗口当作真实 HWND/COM。

`monitor.notice-host` 分类为 **persistent**：popupEnabled ON 后，宿主在任何反馈到达前便预持有惰性窗口；反馈到期后窗口及八个订阅仍然存在。实际 `expiry-retains-persistent-host` 场景证明这种跨反馈持有。单条提示的短寿命不改变宿主分类。

## 独立运行

导入同一树的公共 capability-lifecycle 模块后，运行：

```powershell
node tools/ymcc_sim/global/monitor-notice-lifecycle/index.mjs --repo-root <只读源码根> --evidence-dir <新的仓库外证据目录>
```

候选树暂未导入公共模块时，允许显式 `--lifecycle-gate-module <公共候选index.mjs绝对路径>`；缺省始终解析 `../capability-lifecycle/index.mjs`，不存在静默内置替代。所有输出目录必须明确给出、位于产品仓库之外、尚未存在。此候选不修改公共 runner/catalog/registry，主代理须导入并注册后再从正式 global 入口验收。

本批次附带 `run-candidate.mjs behavior-r5` 外层命令，保留实际子进程 PID、自然退出码、stdout/stderr 和入口哈希。运行在用户指定 D 盘独占根，主线 G 只读。本批当前产品结果固定保留 FAIL/exit 2。

## 观测与结果

每个真实 case 都有唯一 id/name、passed、逐项检查、实际资源计数和完整 trace。正常退出要求窗口、定时器、订阅、IPC pending、native pending 操作、owner 引用、state 条目和 updater presenter 归零。真实 IPC 的 pending 完成来自原 `setLogSink`；native 操作单独计数，所以 2 秒 JS timeout 不会被误写成 native close 已完成。模块级 WebView message listener 是共用 IPC 基础设施，明确列在 infrastructureExcluded，不属于当前宿主的八个订阅。

回执按公共门导出的七个 requirement 各选一个原始 case，绑定公共 `lifecycleContractFingerprint`；全部 21 cases 与其他正/负路径仍完整保留。每条 recovery 的健康来自独立窗口账本与当前 generation/nonce/ready 比较，覆盖起点、1 秒、5 秒、30 秒；观察者不会调用 ready/update/prepare 或代产品恢复。30 秒是本夹具声明的有限观察窗，不代表真实墙钟或产品承诺的恢复期限。

当前具名反例：

- `stop-close-false`、`stop-close-error`、`stop-close-hold`：JS owner/订阅已撤销，但窗口仍为 1；挂起 close 在原 IPC timeout 后还保留 1 个 native 操作。产品未执行所有权受限的强制销毁 fallback，相关字段 false。
- `late-create-before-timeout-close-false`：stop 之后 create 的实际 continuation 到达；后续 close 返回 false，窗口保留。
- `late-create-after-real-ipc-timeout`：原 `invoke` 10 秒 timeout 已结束 prepare，11 秒才送达的真实 native create 回执被 IPC 丢弃，留下没有 JS owner 的窗口。
- `sleep-owner-loss-no-self-heal`、`transient-carrier-loss-no-self-heal`、`transient-status-error-no-self-heal`：用户仍 ON、故障端口已恢复，30 秒内无独立健康恢复。
- `feedback-driven-recreation-loses-triggering-receipt`：反馈触发重建，窗口健康恢复，但触发它的反馈未进入 `monitorNotice.frame`；下一条反馈可成功送达，正向控制单列。
- `transient-feedback-burst-has-no-retry-budget`：800 ms 内八次输入产生八次失败 prepare。有限探针未观察到可验证的产品限额及重置边界；不推断数学上的无限重试，也不凭空设三次之类产品政策。respectRecoveryLimits 保持 false/未证明。

普通 stop、重复 stop、timeout 前晚到 create 成功关闭、停止后的 power/theme/feedback、已撤销 DOM/时钟回调、原 updater 在途 frame 晚回及 stale ready 均分别执行。OFF 的原始功能场景证明不自愈、不接收更新提示，实际晚到回调被投递，desiredEnabled=false、restartsAfterExit=0。因此 explicitOffStaysOff observation 按校准后的公共规则为 true。原测量仍保留 8 个常驻宿主控制订阅和 1 项去重历史，用于宿主控制及再次显式开启；OFF 不要求销毁这些监听。资源全清与 owner 撤销仍用于退出/stop 验收，native 强制销毁未验状态不变。

`verify-delivery.mjs` 检验本批 21 cases/10 原失败、实际 timeout trace、健康观察、源码捕获、合同指纹和非零退出保留。其成功仅表示夹具交付结构与真实失败原件可核对，**不把产品改签 PASS**。未来修产品后应重新审查本批验证脚本的历史失败期待，而不是让新运行冒充本批原件。

## 复用与未验范围

复用 monitor-signals/common.mjs 的实际 SHA helper 与两套既有 adapter 描述，沿用 monitor-input 的原源码 esbuild 消费模式；三份旧 descriptor 的原 timeout 120000/90000/180000 ms 被记录且未修改。现有 monitor-input 渲染/原生 cases 及 monitor-signals 三套 case 在本次没有重新执行，原文件身份前后核对；未削减断言、预算或另改公共注册。

`src/bridge/monitorHudHost.ts` 外层所有权组合、真实 notice renderer/Vue/geometry、native Carrier、HWND、COM、系统 Timer、完整 App unmount 均只是参考或未验，本批不把它们算进执行覆盖。六个产品模块按实际原字节编译，意味着这些模块及所调用函数执行，不代表每个导出函数全覆盖。只捕获这六个 unit 输入，不冒称完整冻结仓库或正式 global 集成回归。

未启动安装 YMCC、Host Main、设备、真实窗口输入或实际系统睡眠。native 强制销毁从未执行/验证；内存故障证明的是前端宿主在这些边界回执下的行为，不能证明实机已经发生或原生 shutdown 不会最终清理。

## 完整运行工具绑定（r5）

本轮在构建前直接调用主线 bundled-node-tool-authority.mjs 的 verifyBundledImplementation 与 lockBundledNodeTools(root)，保留原3072成员预算、完整预声明包图、Node、esbuild入口及实际平台service exe。锁定完成后才 require 被绑定的compilerEntry，并观察实际spawn的路径、SHA、PID及service版本参数；额外CJS加载成员须位于原工具图。业务场景结束后原 verifyBundledToolScope 重建/核对完整scope，另核对六个helper来源；before/after完整authority和实际加载记录写进 provenance.toolBinding 及独立tool-binding.before/after.json。adapter只声明六个静态helper路径，getter不扫描。

任何原锁定/复核拒绝都保留 tool-binding.refusal.<stage>.json，不放宽图、预算或环境门；precompile拒绝会在构建前产生BLOCKED_TOOL_BINDING，cases为空且明确业务未运行，原r4的真实FAIL仍保留。postrun拒绝使当前总结果FAIL，不能被业务计数掩盖。若无拒绝，原21个业务case及43条断言全部保持。r1至r4仅入口哈希不构成完整工具闭包证明，后续导入使用本轮完整回执。工具图是预声明文件闭包与实际service/CJS加载观察，不冒称OS加载模块图或原生产品验收。

<!-- MONITOR-NOTICE-BOUNDED-RECOVERY-20261022 -->
## 后续产品恢复修复（2026-10-22）

以上 21 cases/10 FAIL 是原始交付历史。首提示修复后曾为 12 PASS/9 FAIL；现主线增加事件触发的有限恢复，每轮最多 3 次 status/prepare，自动退避 250/1000 ms，已创建窗口的 ready 等待上限沿用 10000 ms。健康状态不轮询。仅当前 owner 的匹配 ready、用户明确 OFF→ON、较新的已提交电源代次恢复尝试额度；重复 ready/prefs/feedback/旧 power 不能绕过限额。原生 recoveryBlocked 和未确认 create 的不确定性优先阻断。停止、OFF 和电源 pending 取消定时器，迟到回调有版本检查；未重放快捷动作。

原 21 cases/43 个检查条件、检查名及顺序均保留。原预算场景的测量原先固定 declared limit 为 null，现从实际 host.inspect().recovery.policy 读取，核对真实 status 次数及产品 attempts；不存在策略或执行次数超限仍输出 null 并使原检查失败。memory-boundary 的 status 补充实际 Carrier 已有的 ready/recoveryBlocked 字段；仅新回归显式使用 deferReady 故障口，旧场景默认事件顺序保持。

另用同字节实际消费者 bundle 执行 recovery-selftest.mjs 的 16 项运行检查，包含完整耗尽、100 次反馈、同代/旧代不续额、OFF/stop/迟到计时器、真实 ready 后才续额、无 ready 的三次封顶、当前唤醒重核与健康零轮询。复用 pending-feedback-selftest.mjs 的 8 项回归。可用 --expected-host-source 配合同一原产品历史 bundle 执行负对照；这是历史 source 控制，不是当前准入。

未确认 create 的单项检查只证明不重复创建：它保留 stop 后 native 资源未确认销毁的已知缺口，明确记录 afterProductStop，再由夹具作独立 teardown，不将其作为产品销毁通过。原公共 5 个关闭/晚 create 销毁失败仍是完整生命周期阻断；不把本批内存 WebView/时钟恢复称为原生 Carrier/HWND/COM 或实机睡眠验收。详细证据继续放在唯一 round63-current-sdk2-wiring 入口。
<!-- /MONITOR-NOTICE-BOUNDED-RECOVERY-20261022 -->

<!-- MONITOR-NOTICE-NATIVE-LEASE-20261022 -->
## 原生租约撤销与资源销毁补充（2026-10-22）

前面的 16 PASS/5 FAIL 保留为修复前历史。本轮新增 reserve→create→retire 原生协议：只保留一个单调递增且 JSON 安全的租约及 nonce，内存固定；create 必须持当前未撤销的租约。close/retire 在任何窗口或 COM 清理前撤销权限；旧租约迟到 create 被拒，旧 retire 无法销毁更新的 owner。reserve 本身不建窗，因此 reserve 响应超时不会留窗口。

前端优先正常 close，然后用精确 leaseId/nonce 撤销；创建尚未确认时直接撤销。retire 最多两次、每次原 IPC 2000ms 预算，不新增轮询；必须同时收到 leaseRetired/resourcesDestroyed/ownedOnly 才清掉不确定性，并中止对应原 create 的 JS 等待。两次 retire 也超时时保持失败和原生资源未知，禁止建新窗，不将“发出撤销”写成已销毁。补充控制单独保留这个双故障未验路径。

Carrier 在 DestroyWindow 失败时保留 State/ HWND 以便精确重试，输入 HWND 亦如此；创建/导航/消息回调改持 weak State，关闭后不能靠旧回调保活或再引用已销毁 Carrier。实际 Win32 隐藏窗口夹具 native-lease-selftest 执行生产头、原 main 的 reserve/create/retire 原文处理器，核对窗口、计时器与真实 COM ABI 测试替身引用。它绑定既有 3072 工具限额、完整原生工具图与编译前的产品包含文件，并保留编译依赖/链接图；两个单 guard 破坏负例应失败。它没有使用实际 WebView2 控制器，不代签实际 COM 回调/宿主退出或真实睡眠。

内存端口按同一单调租约和请求准入模型模拟旧请求的撤销；被撤销的原生操作不再有执行权限，迟到传输回调仍按原测试投递。原 21 cases/43 条检查条件、检查名与顺序完整保留。退出 fallback 测量读取实际 cleanup 计数与三项撤销回执；nativeForceDestroyExecuted 继续 false，原生执行证据独立审查。附加 recovery16 的原未确认 create 缺口项升级为更强的“撤销成功→旧 create 被拒→新租约恢复”检查，升级前失败回执原样保留；pending8 和 lease7 另行执行。

具体证据仍写入唯一 round63-current-sdk2-wiring 目录。正式全局门只验其实际消费者范围；最终主线完整构建、真实 WebView2、安装版及实物睡眠结果仍须对应证据，不能从内存 21 PASS 或隐藏窗口夹具推定。
<!-- /MONITOR-NOTICE-NATIVE-LEASE-20261022 -->


<!-- MONITOR-NOTICE-REAL-WEBVIEW-20261022 -->
## 真实 WebView2 补充（2026-10-22）

独立 real-webview-selftest 在从未切换到前台的私有桌面上使用已安装的明确固定 WebView2 155.0.4283.62。完整916个运行库文件在编译前、执行前后核对成员及哈希；26包749个Node文件、1368原生工具与生成物共3045项，保留原3072限额。生产Carrier实际创建9个CompositionController，19项检查通过：正常close、精确retire、持有真实完成回调后撤销、Carrier析构后迟到完成、旧代完成不影响新owner、三轮重复。透明转发只观察真实方法与延迟回调投递，非控制器行为替身。每个实际Close调用一次且HRESULT0，引用/State/原生owner/hook释放，所属浏览器在Job清理前自然退出。

real-webview-negative-selftest只把生产完成lambda额外加一份强State捕获，原夹具/预算不变，真实创建3个控制器后在State应先于迟到回调释放的检查实际exit2；没有外层超时或强杀。原get_CoreWebView2在Close后仍可返回接口，之前把它当销毁证明的错误测试假设已纠正，原失败保留，改查真实Close、引用和浏览器退出。完整当前main以原生产宏/O2/MT/bigobj对象编译通过，1380个真实编译输入均有编译前哈希；第一次漏扫12个capabilities头的独审失败仍保留。没有链接或运行最终主程序。

本夹具使用本地CSP握手页；尚未执行真实产品Vue通知页、完整应用unmount或任意COM硬挂起。浏览器参数是限制，不是OS网络/设备隔离或完整模块图证明。原公共21/43、七项软件要求和实际全局INCOMPLETE/exit10仍各守范围。此README追加改变了旧公共回执的一个文档输入，旧回执保留为该次源快照，不冒称整个回执仍对当前18文件全新。共享生命周期executedTests=0是原测试明确规定的审计合同：该模块只审已有回执，不启动业务测试；撤回此前计数缺口解释，不改共享门。证据见唯一round63-current-sdk2-wiring目录NOTICE-REAL-WEBVIEW-COMPLETED-INDEPENDENT-REVIEW-20261022.json。人工睡眠仍未放行。
<!-- /MONITOR-NOTICE-REAL-WEBVIEW-20261022 -->


<!-- MONITOR-NOTICE-REAL-RENDERER-20261022 -->
## 产品实际通知页补充（2026-10-22）

real-renderer-selftest.mjs在同一私有桌面、明确固定运行库及生产Carrier上构建并执行未修改的src/monitorNoticeOutputEntry.ts、MonitorShortcutNotice.vue、正式IPC及实际样式。只将原产品HTML的源码引用换成构建产物并添加私有CSP；不导入App/手柄engine/设置/采集器。使用同一个3072限额，完整工具/runtime/生成物3050项。原真实原生19项断言、顺序及预算不变，增加17项真实页面检查，共36PASS：原生反馈实际显示及尺寸/数值、前后端错nonce拒收、页面旧seq拒收、显式隐藏、到期移除、再次显示、合成pagehide调用实际产品dispose/Vue卸载、卸载后迟到消息无DOM重建。9个真实控制器Close各一次且HRESULT0，原生owner/hook清零，浏览器在自有Job清理前自然退出。

real-renderer-negative-selftest.mjs仅在构建中将产品frame准入guard增加true，主线源文件不改；原生EXE、HTML和CSS与正例字节相同。真实控制器已ready、native已接纳反馈后，页面显示检查实际失败，worker exit2，非超时强杀。负例3051项仍低于原3072限额。此负例的浏览器退出由自有Job兜底隔离，没有独立自然退出结论。证据NOTICE-REAL-RENDERER-COMPLETED-INDEPENDENT-REVIEW-20261022.json保存在唯一接线入口。这里证实独立通知renderer的真实事件/DOM与资源链路，未运行完整YMCC App或整机睡眠，任意COM硬挂起/最终EXE导出身份及全局覆盖仍开放。
<!-- /MONITOR-NOTICE-REAL-RENDERER-20261022 -->

<!-- MONITOR-NOTICE-NATIVE-GLOBAL-INTEGRATION-20261023 -->
## 原生 lease 退出组件接入 global（2026-10-23）

新增唯一 ID `lifecycle.monitor-notice-native-lease`，由 `native-adapter.mjs` 声明，`native-global.mjs` 包装原 `native-lease-selftest.mjs/cpp`。它是既有 persistent `monitor.notice-host`（`lifecycle.monitor-notice-host`）的原生退出/准入组件；没有创建独立 one-shot 功能，没有提交完整 persistent 生命周期证明，不能代替 TS/IPC、恢复或真实 renderer/WebView2 验收。声明中的 `lifecycleComponent` 保留 persistent 归属；不伪造要求全部恢复证据的 `lifecycleContract`/`lifecycleProof`。全量生命周期验收仍按原门处理。

```powershell
& G:\YeManCC-Work\Testing\YMCC\Run.ps1 -Domain global -Mode plan -Tests lifecycle.monitor-notice-native-lease -EvidenceDir G:\YeManCC-Work\Testing\YMCC\Evidence\MonitorNativeIntegration-20261023\Build\plan
& G:\YeManCC-Work\Testing\YMCC\Run.ps1 -Domain global -Mode run -Tests lifecycle.monitor-notice-native-lease -EvidenceDir G:\YeManCC-Work\Testing\YMCC\Evidence\MonitorNativeIntegration-20261023\Build\run
```

原 scope `SOURCE_MEMBERS` 和 `toolDeclaration` 原样复用，3072 工具/产物上限、明确编译器/SDK/链接工具、原源码/生成物哈希校验、120s 编译与15s native 执行预算均不改。包装器依次实际调用原正例10 case 和两个原 mutation，每次使用新建自有输出目录，不复用旧二进制或旧 PASS。只编译该夹具；不构建整库。`native-adapter.mjs` 明列原 fixture、`monitor_desktop_hud.h` 的 quote 源依赖以及 `native/main.cpp` 原 reserve/create/retire handler 切片边界。实际 MSVC dependency/link graph、逐文件编译前 pin、EXE pin 和原 handler 源/切片 pin 留在每个子目录。

正例必须原 native exit0、10/10，owned HWND 和 controller 数为0，`unownedDestroyAttempts=0`。两个坏例的原 `native-result.json` 保持 `status=FAIL`、原 native exit2；包装器要求原 selftest 自然退出0（表示它完成并检测到破坏），且原失败 ID/错误消息完整遮罩精确匹配：

- `skip-retired-create-admission`：仅 `retirement-before-create-revokes-native-admission-without-a-window` 和 `exact-main-reserve-create-retire-handlers-enforce-native-lease-and-request-shape`；对应原错误分别是 `Late retired create regained native admission`、`Exact main request admission was bypassed`。
- `ignore-destroy-failure`：精确两项 `first-destroywindow-failure-keeps-owner-for-exact-retirement-retry`、`input-window-destruction-failure-also-retains-and-retries-owner`；原错误分别为 `Failed DestroyWindow was reported as success`、`Failed input window destruction reported success`。

缺失或额外失败、case名/数量变化、超时/信号/强杀、源/工具漂移均使包装器 FAIL。global 仅把独立 `monitor-notice-native-lease/result.json` 作为结构化必需结果；它持有全部原生原件的路径/SHA256和原状态，不将坏例原件列为普通正例 resultFiles，也不把原件重写为 PASS。

三条路径均使用 `initialize(nullptr, noticeOnly)`。正例与 skip-retired 的 create 在真实 WebView2 可用性检查之前/处拒绝；即使省去 retired guard，空 environment 也会阻止创建真实渲染器。其余 case 只附着本次自有、隐藏、无激活的 HWND/Win32 timer 和 ABI `TestController` COM 替身；DestroyWindow 探针拒绝非自有 HWND。这里不启动已安装 YMCC/Steam、真实 WebView2、设备或 OS 睡眠。坏例自身暴露的资源计数仍保留；只有正例的清零结果可作该组件正向证据。真实 renderer/WebView2 仍须原 prepare/exact-review/runtime 合同，不纳入这个 ID。

`extra-adapter-index.json` 仍只是 source-bound routing identity；刷新需实际物化并核验所有 factory 的 ID/domain/profiles/lifecycleDeclared，保留既有全部 ID 和顺序。索引源 hash 的刷新不提升任何旧运行/产品/工具回执为当前有效执行证据。实施脚本和本项证据分别位于核心 `Workspace/MonitorNativeIntegration-20261023`、`Evidence/MonitorNativeIntegration-20261023`。
<!-- /MONITOR-NOTICE-NATIVE-GLOBAL-INTEGRATION-20261023 -->
### ignore-destroy 推断纠正（2026-10-23）

最初“应仅失败 first-destroywindow 一项”是待验证推断。实际原生运行 `2026-10-22T19-06-01-401Z-3187564f` 的 `d/native-result.json` 原样保留了两项失败：原 `native/monitor_desktop_hud.h` 的单一 guard 同时检查 `s->hwnd` 与 `s->inputWindow` 是否仍为窗口；原 `native-lease-selftest.cpp` 的两个 case 分别拒绝一次主 HWND、input HWND 的 DestroyWindow，并精确要求原 close 不得报告成功。仅移除此同一 guard 会使这两条原 error 分别出现。原自测/产品未改；此前全局 `EXACT_NATIVE_FAILURE_MASK` FAIL 及其源 pin 继续保留。

主代理在本任务既有授权内复核源码和原始回执后，包装器仅把 ignore-destroy 的精确完整遮罩改为上述两项 ID/error 对。任何第三项失败、缺失其中一项、错误 reason、case 名/数量变化或非自然退出仍拒绝；不是按“出现了目标失败”放行。新一轮结果必须来自新建输出下的实际编译/执行，不回写或重判旧全局 FAIL。纠正依据与新执行见核心 Evidence/MonitorNativeIntegration-20261023/MaskCorrection-20261023。

原生lease组件现明确绑定真实产品源码、驻留窗口/周期timer/COM引用资源类别和case-local结果文件，同时绑定公共生命周期门源文件。分类属于持续的 monitor.notice-host；组件PASS只反映该原生退出范围。父级需在合同中明确接入并完成原有全部退出、健康恢复与限制观测，缺失项继续INCOMPLETE。
