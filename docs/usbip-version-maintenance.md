# USB/IP 随 HIDM 配套升级

本入口维护随包 HIDMaestro SDK 所要求的 USB/IP，不另设在线更新服务或 updater EXE。复用现有 HIDMaestroSetup.exe。当前批准的原版 HIDMaestro 2.0.0 内嵌 USB/IP 0.9.8.1；目标由批准 DLL 的 literal 元数据及当前 OS 架构资源读出，同时核验 SHA256、MVID 和资源。升级 SDK 后须重新核验这些内部成员。

只有已装产品明确更旧、旧 SYS 属于核验清单、产品登记/usbip.exe/官方卸载路径一致，并满足管理员、当前 owner 和空闲条件，才允许自动卸载。相同、更高、未知、不完整或正在使用的安装均保留。现有 SDK 的 since-boot 使用保护及原始 VHCI 读取检查继续生效；不能把吞异常的空结果视作空闲。版本正确只说明无需升级，不表示虚拟手柄已经启动。

生产顺序为：模块维护门 → 原 gamepad setup 门 → USB/IP 维护门 → 再次核验 owner、版本和空闲 → 持久事务 → SDK Transport 锁内启动官方 Inno 卸载器 → 等待卸载返回 → 提示重启 Windows。官方卸载脚本要求重启，即使返回 0 也不在本次开机直接安装。Inno 卸载任意非 0（包括 3010/1641）均为未完成，不能套用 MSI 的成功码。

重启后，下次启动准备仅在确认 Windows BootIdentifier 已改变、保存的配套版本与批准 SDK 一致、旧产品/服务/UDE 与 filter 驱动包/后端均已缺失时，由 HIDM 原 static InstallUsbipBackend 接续安装，再核对真实服务 SYS 散列及后端。调用 SDK 安装前已释放 Transport 锁，避免 SDK 工作线程互等。成功接续后退休历史 continuation，之后用户自行卸载不会触发旧升级命令。残留、身份改变或无法证明缺失均停止并提示，不循环重启/安装。

机器共用事务位于 %ProgramData%/YeManCC/UsbipDriverMaintenance，限定 SYSTEM/Administrators 写入、Users 读取；独占随机临时文件写入后原子替换。每次进程仅进行一次只读 Windows 启动身份查询，不增加轮询。新的破坏性事务必须先取得有效 BootIdentifier；不再用 uptime 大小推断新记录的重启，因此重启后晚打开也能识别。旧未发布记录仅保留保守 uptime 兼容。不能确认重启或记录损坏时保留未确认状态。

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

这类查询强制走原 IPC IO 池，UI 和手柄生命周期 worker 均不等待版本扫描。已有安装子进程运行期间仍只观察原子进程，禁止并发启动版本检查。终态时复用随包 HIDMaestroSetup.exe 的 --usbip-observe 模式：仅核验已认证 owner/root、批准 SDK 元数据/驱动哈希、产品登记及已有重启 fence；不进入 bootstrap、安装互斥、写报告、卸载、安装、清理事务或 SDK 控制器构造。当前 SYS 匹配配套散列才显示 current；登记版本相同而 SYS 不匹配为 unverified。较旧但已核验的安装仅显示“版本可更新”，不当作手柄使用失败，也不启动进度 watchdog。

单个只读 helper 由原 runCapture job 管理，预算 5000ms，超时清理的只会是本次只读子进程。展示读取仍有原 1500ms 上限，若扫描晚于前端等待，后台新结果通过原 gamepad.driverSetup 事件更新。native 每次只允许一个观察，1000ms 内重复需求共用结果；状态未变不递增序号、不重复广播。owner/session/root 不匹配、损坏/不完整输出不得替换已确认状态。应用退出后不提交观察；已要求重启的同应用会话不能借只读检查解除重启状态。

当前观察与启动维护分别保存：新当前状态用同 driverSetupSession 的更高 sequence 发布，原始启动维护结果保存在 usbipMaintenanceHistory 和原报告/日志中；driverSetup 及 HidHide 的结果不被只读状态覆盖。不会把“手柄能用”推断为“升级成功”。

截图中的 Access to the path is denied 只能说明一次维护发生访问异常，尚不能归因于 Xbox 全屏。读 SDK/SYS、写状态报告或受保护事务、SDK 内部阶段均有可能；本机既有报告与驱动核对未复现同类权限错误。权限调查保持 UNKNOWN_INSUFFICIENT_FAILURE_EVIDENCE，不放宽 ACL，也不借刷新重试驱动事务。

本轮证据：G:/YeManCC-Work/Testing/YMCC/Evidence/Usbip-Hidm-Recovery-20261023/status-refresh-20261024/REPORT.md。仅软件回放/编译，未导出、部署或操作实机驱动与 Xbox。
