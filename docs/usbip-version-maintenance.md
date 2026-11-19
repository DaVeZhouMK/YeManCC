# USB/IP 随 HIDM 配套升级

本入口维护随包 HIDMaestro SDK 所要求的 USB/IP，不另设在线更新服务或 updater EXE。复用现有 HIDMaestroSetup.exe。当前批准的原版 HIDMaestro 2.0.0 内嵌 USB/IP 0.9.8.1；目标由批准 DLL 的 literal 元数据及当前 OS 架构资源读出，同时核验 SHA256、MVID 和资源。升级 SDK 后须重新核验这些内部成员。

版本分为系统应用登记/usbip.exe 与服务所指向的 UDE 驱动文件。HIDM 的 raw-driver 更新可能把 UDE 更新到 0.9.8.1，却仍保留旧版 0.9.7.7 程序/卸载登记及 filter；不能因此直接把整套旧产品标 current 并跳过卸载。对于核验清单中的旧产品，旧 SYS 或当前批准 SDK 的 SYS 均可作为驱动身份依据，但仍须核验产品、客户端版本、官方卸载路径与文件身份，并通过占用/权限/本次开机防重复门。只读观察只报告 update-found；自动准备才执行官方卸载。登记同版但 UDE 文件未确认则 unverified，保留现场。报告字段 udeDiskMatchConfirmed 只证明服务选中的 UDE 磁盘文件，不证明 filter、已加载内核字节或完整手柄消费。

只有已装产品明确更旧且属于核验清单、服务指向的 SYS 是已核验旧版或当前批准 SDK 资源、产品登记/usbip.exe/官方卸载路径一致，并满足管理员、当前 owner 和空闲条件，才允许自动卸载。相同、更高、未知、不完整或正在使用的安装均保留。现有 SDK 的 since-boot 使用保护及原始 VHCI 读取检查继续生效；不能把吞异常的空结果视作空闲。版本正确只说明无需升级，不表示虚拟手柄已经启动。

生产顺序为：模块维护门 → 原 gamepad setup 门 → USB/IP 维护门 → 再次核验 owner、版本和空闲 → 持久事务 → SDK Transport 锁内启动官方 Inno 卸载器 → 等待卸载返回 → 提示重启 Windows。官方卸载脚本要求重启，即使返回 0 也不在本次开机直接安装。Inno 卸载任意非 0（包括 3010/1641）均为未完成，不能套用 MSI 的成功码。

重启后，下次启动准备仅在共享启动证据确认已跨 Windows 启动、保存的配套版本与批准 SDK 一致、旧产品/服务/UDE 与 filter 驱动包/后端均已缺失时，由 HIDM 原 static InstallUsbipBackend 接续安装，再核对真实服务 SYS 散列及后端。新记录以机器易失的 windows-volatile-v2 启动会话身份为准；loader BootIdentifier/sequence 可能在真实重启后重复，不能单凭其重复拒绝接续，也不能在 V2 相同、缺失或损坏时借较新 loader 元组越过限制。旧记录继续使用原有保守兼容与基线迁移规则。调用 SDK 安装前已释放 Transport 锁，避免 SDK 工作线程互等。成功接续后退休历史 continuation，之后用户自行卸载不会触发旧升级命令。残留、身份改变或无法证明缺失均停止并提示，不循环重启/安装。

机器共用事务位于 %ProgramData%/YeManCC/UsbipDriverMaintenance，限定 SYSTEM/Administrators 写入、Users 读取；独占随机临时文件写入后原子替换。loader 启动信息按进程缓存，V2 启动会话由正常管理员准备入口在共享门内建立并读取；Host 和状态查询只读，不增加轮询。新的破坏性事务必须先取得有效 V2 启动会话身份；不以 uptime 大小推断新记录的重启，因此重启后晚打开也能识别。旧记录保留原有 loader 元组及保守 uptime 兼容，不把旧格式或未知状态当作已证实的新 V2。不能确认重启或记录损坏时保留未确认状态。

卸载观察最多 300 秒；超时不强杀系统驱动事务、不并发安装，持久 fence 阻止本次开机后续 USB/IP create。每次开机、同一 SDK/旧版/目标组合只有一次已开始的破坏性尝试；取锁失败等尚未执行卸载的拒绝不消耗预算。只读 pnputil 进程和输出排空均有有限预算。

2026-10-23 用户调整：完整版本与维护详情移到设置 → 设备 → USB/IP 配套，只有该分区 active 时挂载读取。控制器及陀螺仪内页使用同一 UsbipUpgradeStatus.vue 的 errorsOnly 展示，仅 failed、admin-required、reboot-required 显示简短错误及处理原因；current、newer-preserved、not-installed、update-found、busy、进度与 unverified 均不占界面空间，版本偏低本身不当作使用错误，恢复正常后提示移除。前端不因此改写升级、驱动或开关逻辑。订阅仍为 gamepad.driverSetup；查询上限 1500ms，无常驻轮询、无下载模态框、不锁其它页面、不要求物理手柄。native UI/IPC 只读不可变缓存并合并请求已有串行 worker 观察；文件读取不在 UI 线程。进程退出后再读取一次最终阶段，覆盖“第一次读取后才写终态”的竞态。模块更新后的延迟启动继续交回原 inputHost 生命周期队列，不创建 detached Start 线程。

前端按 session/sequence 合并有效报告，拒绝旧代次及乱序，允许权威读取修复同序号损坏。真实进度设 180000ms 一次性单调时钟 watchdog；重复事件或隐藏/重显不续期。超时只将展示改为待确认，不取消系统事务、不重试安装或改写用户开关。失活、停止、卸载清理读取、订阅、abort 回调及计时器，迟到回复不得复活。

维护验证入口：

- 已登记全局模拟器：controller.usbip-version-upgrade 与 controller.usbip-upgrade-observation。
- node tools/usbip_upgrade_selftest.mjs <全新证据目录>：真实 C# consumer、原维护门、批准 SDK 元数据、只读 OS 启动身份及自有子进程等待；指定删除较新版保护的负例必须失败。
- node tools/usbip_upgrade_robustness_native_selftest.mjs <全新证据目录>：原 native 观察、缓存读取、合并请求和停止门；指定删除退出后最终读取的负例必须失败。
- node tools/usbip_upgrade_status_selftest.mjs --out <全新证据目录>。
- node tools/usbip_upgrade_status_browser_selftest.mjs --out <全新证据目录>：真实组件/IPC、720/800 片段及 200 次启停资源核对。
- 既有 HidHide 回归、Vue 类型检查、完整 native 翻译单元和 InputHost/helper 编译；真实组件与正式 engine 的全页 720/800 UI 门分别记录。

证据统一位于 G:/YeManCC-Work/Testing/YMCC/Evidence/Usbip-Hidm-Recovery-20261023。REPORT.md 和 ROBUSTNESS-DELIVERY.json 区分当前源码软件结果、历史失败及未完成的实机验收。不自动操作安装版 YMCC、Steam、驱动、真实睡眠或重启；软件模拟和编译不能证明真实驱动升级成功。本轮无导出或部署。


## 2026-10-24 终态缓存与页面只读刷新

启动维护完成后，原 gamepad.driverSetup.get 只返回启动快照，不能反映后续安装状态；同一可见状态下重新获得焦点也不会触发可见性边沿。现修正为：USB/IP 展示组件在挂载/页签重入、隐藏后显示，以及原生 focus/restored/maximized/summoned 事件时请求一次带 refreshUsbip 的查询。重复同批刷新合并，退出清理自有订阅与在途读取；没有常驻轮询。

这类查询强制走原 IPC IO 池，UI 和手柄生命周期 worker 均不等待版本扫描。已有安装子进程运行期间仍只观察原子进程，禁止并发启动版本检查。终态时复用随包 HIDMaestroSetup.exe 的 --usbip-observe 模式：仅核验已认证 owner/root、批准 SDK 元数据/驱动哈希、产品登记及已有重启 fence；不进入 bootstrap、安装互斥、写报告、卸载、安装、清理事务或 SDK 控制器构造。产品登记版本与当前配套相同、且服务选中的 UDE SYS 匹配配套散列，才显示 current；旧产品即使 SYS 已匹配也显示 update-found，登记版本相同而 SYS 不匹配为 unverified。较旧但已核验的安装仅显示“版本可更新”，不当作手柄使用失败，也不启动进度 watchdog。

单个只读 helper 由原 runCapture job 管理，预算 5000ms，超时清理的只会是本次只读子进程。展示读取仍有原 1500ms 上限，若扫描晚于前端等待，后台新结果通过原 gamepad.driverSetup 事件更新。native 每次只允许一个观察，1000ms 内重复需求共用结果；状态未变不递增序号、不重复广播。owner/session/root 不匹配、损坏/不完整输出不得替换已确认状态。应用退出后不提交观察；已要求重启的同应用会话不能借只读检查解除重启状态。

当前观察与启动维护分别保存：新当前状态用同 driverSetupSession 的更高 sequence 发布，原始启动维护结果保存在 usbipMaintenanceHistory 和原报告/日志中；driverSetup 及 HidHide 的结果不被只读状态覆盖。不会把“手柄能用”推断为“升级成功”。

截图中的 Access to the path is denied 只能说明一次维护发生访问异常，尚不能归因于 Xbox 全屏。读 SDK/SYS、写状态报告或受保护事务、SDK 内部阶段均有可能；本机既有报告与驱动核对未复现同类权限错误。权限调查保持 UNKNOWN_INSUFFICIENT_FAILURE_EVIDENCE，不放宽 ACL，也不借刷新重试驱动事务。

本轮证据：G:/YeManCC-Work/Testing/YMCC/Evidence/Usbip-Hidm-Recovery-20261023/status-refresh-20261024/REPORT.md。仅软件回放/编译，未导出、部署或操作实机驱动与 Xbox。


## 2026-10-31 当前主线：旧产品卸载、真实重启续装与归档回归

旧版产品登记/客户端仍为 0.9.7.7、但 UDE 磁盘文件已被 HIDM 迁移为当前资源时，仍按核验后的旧产品处理。不能只因 UDE 文件匹配而提前 current。未确认同版驱动为 unverified；较新、未知或身份不匹配的安装保留。旧产品卸载前还要在 SDK Transport 锁内重新核对产品、SYS、客户端、官方卸载器和 owner/占用条件。

本机已实际跑通：2026-10-31 08:34（UTC+08）官方卸载返回 0 → 同 boot 原应用再次启动仍为 REBOOT_REQUIRED、没有重复卸载或提前安装 → 用户 09:01:57.500 自行重启 → 正常 YMCC 于 09:02:28.456 自动续装完成，READY / 0，USB/IP 产品和 usbip.exe 均为 0.9.8.1。UDE/filter 服务均 Running，UDE 磁盘文件匹配批准 HIDM 2.0 资源。09:29 的更新 helper 只读回查为 current / udeDiskMatchConfirmed=true，前后维护记录和准备报告散列不变。

本次 loader GUID 重复而 V2 改变，程序正确识别 previous-boot-proven;normal-continuation。原 pending 原字节退休到 pending.json.previous-boot.completed；未手动清理记录、手工安装或由助手重启。保留 29 个旧版程序文件备份及签名有效的 0.9.7.7 官方恢复安装器。

归档末端复用内部 CompletePreviousBootRecord；正式 ConfirmCurrent 与 SelfTest 消费同一生产方法。该内部 seam 只接受调用方传入的路径/core/target/boot tuple/V2，不成为产品配置项。原 canonical controller.usbip-version-upgrade 本轮一次实际运行 196 项（原 144 + 新增 52），原维护门 8 项、等待检查 3 项通过；入口实测 5022.7805 ms。原较新版保护负例保留，新增跳过归档身份守卫、把移动改成 JSON 重写的两个定向负例均由原断言抓住。证据只代表这些输入身份和范围，不把旧 144 项回执写成新源码通过，也不自动启用整轮发布测试。

新增归档场景在自有临时目录直接预置 .previous-boot：匹配新 V2 归档保持 BOM/空格/CRLF；同 V2、缺失有效 V2、错 core/target、坏 JSON、check-only 和重复确认保持对应的拒绝/无操作边界。记录缺失 V2 的断言分别覆盖 null/无字段且 loader 重复；原 legacy 无 V2 而不同 loader tuple 的兼容未改。

前半段 pending → previous-boot 仍由 native 入口管理，涉及真实 V2 准备、受保护目录和 legacy 迁移。没有在 Fake 复制迁移算法；该文件转换未作为临时目录端到端软件通过项。其当前实机证据及共享纯启动判定断言继续保留。

正反向核对：只有已核验旧产品及安全条件满足才卸载；先持久化再启动变更；卸载任何非零、超时、owner 消失或残留未清均不并发安装；同 boot 已开始尝试不因重开应用更新预算；重启接续必须匹配原 core/target 和启动证据；SDK 返回后还要验证驱动与后端；只读查询不修改维护状态。版本及磁盘状态不能替代内核加载映像、游戏输入/OFF 或群友 G3E 的整体验收。

保留的证据入口：

- 实机卸载与重启续装：[验收报告](G:/YeManCC-Work/Testing/YMCC/Evidence/USBIP-AutoUninstall-20261031-01a146ff/POST-REBOOT-REPORT.zh-CN.md)、[重启后封存](G:/YeManCC-Work/Testing/YMCC/Evidence/USBIP-AutoUninstall-20261031-01a146ff/DELIVERY-POST-REBOOT.json)。
- 当前归档软件覆盖：[覆盖报告](G:/YeManCC-Work/Testing/YMCC/Evidence/USBIP-CompletionArchive-20261031-01a146ff/REPORT.zh-CN.md)、[196 项封存](G:/YeManCC-Work/Testing/YMCC/Evidence/USBIP-CompletionArchive-20261031-01a146ff/DELIVERY.json)。
- 模拟环境独立收录：G:/YeManCC-Work/Testing/YMCC/Evidence/FixtureIntake-20261030/Continuation-20261031/USBIPAutoUninstallIntake-20261031/。默认发布不因本节自动运行夹具。

## 2026-11-01 配置风暴与事务记录加固

`native/UsbipMaintenanceState.cs` 统一了 pending、attempt、Host backend attempt 与升级端报告的读取和写入。维护记录最大4096字节，`.usbip.json`报告最大256KiB；JSON最大深度16，拒绝重复字段、非法编码、错根、错类型及越界字段。文件真正缺失与目录/父路径类型错误、拒读、占用分开；损坏或未来schema保持未确认，不授予安装/卸载或预算续期。状态读取不写记录。

前次升级记录不可读或不支持时，不能退到首次安装路径。合法重启证据和源身份仍按原规则判断；错误解除或明确替换为可信有效记录后，原入口重新读取即可重试。没有凭空恢复原始驱动状态或把坏记录清为成功。

事务文件采用唯一CreateNew临时、Flush(true)、临时独立读回、源比较、发布和再次读回；覆盖前原字节保全到内容SHA命名备份。正常未知扩展属性保留。完成当前会话记录不再删除唯一原件，而是移动到`.owned-completed-<SHA>.json`证据；历史previous/completed不同但合法时先保全到`.ymcc-history-<SHA>.json`，不妨碍后续健康升级。未知、冲突或未来归档目标则保持两边并拒绝覆盖，明确解除故障后才能重试。

当前软件证据：原文件迁移消费者95场景通过；原升级消费者2320检查、原6个负例通过；原后端消费者166检查、原5个负例通过；文件链另4个撤销保护反例被抓住。计数互有重叠，不累加为独立故障数。原60场景基线曾34失败，之后新增写入与恢复边界才扩到95；不声称改前同95。测试均在独占Evidence文件根，启动证据/ACL/SDK与设备端口为fake，未安装或卸载系统驱动，未操作真实ProgramData、设备、YMCC或Steam。

跨进程gate保留原实现；最后来源检查至发布/删除间仍不是任意非协作writer的OS级CAS。真实磁盘满、断电及永久kernel IO挂起未验收，短写/flush/晚到损坏为明确注入。该次软件通过不能替代实机重启、内核载入和手柄消费验证。


## 2026-11-08 BUS 升级链正反复核

当前生产修复与原夹具均已在主线。本轮核对升级消费者12个源码输入、文件消费者6个源码/4个生成输入/13个实际片段仍匹配已保存证据；原四份封存清单142项未变。InputHost/Program.cs整文件已有其它任务修改，其三个USB/IP准备方法及另外五项依赖仍匹配原准备子链；不据此宣称整个当前Host已重测。

正向链仍为：确认干净缺失时首次安装；已核验旧产品经准入、持久attempt/pending后官方卸载，等待真实重启，再核残留并调用原HIDM续装。安装返回还须核对真实产品登记、UDE文件、后端及owner，才可完成和归档。旧登记配新UDE不提前current；同版驱动未确认、未知/混装/较新版、占用或损坏记录继续保留。

反向边界包括：非零卸载/超时不接着安装，重开应用/Host不刷新同boot预算，无法证实新boot不越过重启限制；安装前再核环境，部分安装不能假完成，外来/未知记录不清除，写入及独立回读失败不回报成功。页面查询继续只读。系统卸载超时不强杀；超时后的未确认事务不与新安装并行。

本机只读回查产品及usbip.exe均0.9.8.1、UDE/filter均Running、UDE磁盘SHA匹配批准HIDM资源；未读取内核已加载映像。10月31日真实卸载、用户重启及自动续装的原件继续保留。本轮没有操作驱动或已安装应用，也没有新出包。

本轮工具身份核对保留REVIEW_REQUIRED：本机dotnet.exe和Windows PowerShell与历史不同，原10.0.400 SDK当前缺失。升级2320检查/6组反例、文件95场景/2412检查、Host166检查/5组反例均为历史实跑引用，不计作本轮PASS，亦不合计为独立覆盖。原FAIL/UNKNOWN与工具差异原样保留，不为封存重复编译或绕过身份门。

证据及交接：`G:/YeManCC-Work/Testing/YMCC/Evidence/USBIP-MainlineCloseout-20261031-01a146ff/reconciliation-20261108/REPORT.zh-CN.md`、`RECHECK.json`和`DELIVERY.json`。继续复用原controller.usbip-version-upgrade、原pending-file-transition及原Host后端准备入口；没有新增默认测试、ID或发布门。下一次整包须同源构建HIDMaestroSetup与YeManInputHost；当前源码核对不代签新包、内核加载、完整输入或真实睡眠验收。


## 2026-11-09 当前来源与原始证据核对

原卸载/重启续装修复及故障用例继续保留于主线。当前12项升级源码、13个文件片段和3个Host准备方法匹配原验证输入；4份原交付的142个封存文件未变。Host整文件的其它修改未被回滚，方法相等只作原范围引用。本机只读确认产品/客户端0.9.8.1、UDE/filter Running且无删除标记，UDE磁盘身份匹配批准资源。

旧产品配新UDE仍须核验后卸载；持久事务先于变更，卸载后真实重启再核残留及续装，完整产品/UDE/后端/owner确认后才归档。同boot防重复、坏/外来记录保留、非零/超时不并发安装及只读状态不退款的边界不变。本次没有生产控制改动、驱动操作、新业务测试或编译；原工具差异及FAIL/UNKNOWN不重标为PASS。当前整包及内核加载/游戏输入/睡眠仍分别验收。

证据：`G:/YeManCC-Work/Testing/YMCC/Evidence/USBIP-MainlineCloseout-20261031-01a146ff/reconciliation-20261109/REPORT.zh-CN.md`。复用既有模拟器原ID与独立核收，不增加默认父门或整轮重跑。

## 2026-11-15 BUS 卸载与升级链收尾复核

本轮继续原项目，未重复修改已经落主线的控制代码。实际重新核对12项升级输入、13处文件处理片段、3个Host准备方法及142项历史封存文件；所选生产逻辑仍匹配原软件验证来源，整个Host文件的并行修改不回滚，也不冒称整个当前Host重新通过。

原只读核收器报告的工具清单差异继续保留为 `REVIEW_REQUIRED` 原件；当前 `inputhost-protocol/toolchain-inputs.json` 与此前 `ManagedToolchain-20261110` 已核收的路径迁移版本相同。这只是已解释的来源差异，不代表本轮重新校验全部工具、运行原夹具或取得缓存资格。历史FAIL、UNKNOWN、负例及工具差异不改写，普通导出仍为夹具 `NOT_RUN`。

正向仍为已核验旧产品准入、先持久事务、官方卸载、真实重启、核残留、原HIDM续装，再核产品/UDE/后端/owner并归档；反向仍拒绝同boot重复、非零或超时后的并发安装、未知启动/坏记录/外来记录、残留与部分安装假完成。系统卸载等待超时只停止后续链，不强杀系统事务。当前只读产品/客户端均0.9.8.1，UDE/filter Running且无删除标记；磁盘身份不是内核已加载映像或游戏消费证明。

当前核收及模拟夹具交接在 `G:/YeManCC-Work/Testing/YMCC/Evidence/USBIP-MainlineCloseout-20261031-01a146ff/reconciliation-20261115/`。没有新测试ID、默认发布门、驱动操作、整包构建或部署；下次自然整包仍须同源构建HIDMaestroSetup与YeManInputHost。真实断电、永久内核IO挂起、任意非协作writer的OS级CAS和完整输入/睡眠验收继续单列，不能以本节保证所有环境必然成功。
