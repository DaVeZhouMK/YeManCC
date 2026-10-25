# Steam 原实例联系协议：维护用静态合同

此文是Steam升级维护入口的补充，适用于排查非Steam游戏在睡眠后失去真实关联。完整目标仍是同一原游戏实例真实输入和Continue；现有R47尚未通过实机验收。本文不新增生产API、运行开关或恢复动作，自动应对Steam升级按用户要求延期。

## 先确定所分析的版本与实例

2026-10-15用户更新/重启后的磁盘FileVersion为11.12.30.07。steamclient64.dll SHA256 `e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e`、renderer64 `cbde033f76ac070ad8b7d25328e8acfbbabae6e40487f40dfcf6203b6c932c0b`，与已缓存官方1791918634样本逐字相同。SteamUI.dll为 `1b9457943205dc3af0bd53463ba331cbd2a381388699705c02271aac7a2b1252`。这是固定磁盘历史证据，不保证未来最新，也不证明当前加载内存与磁盘一致。

旧Windows178样本地址不得用于新版；例如renderer通知字段的虚槽已从90/38/B0变成A0/40/C0。操作权限仍须由OS PID/birth/path/session、原Steam PID/birth/root/session/account、完整GameID、YMCC单PID与精确tracker/共享候选及开关/睡眠代际联合证明，磁盘版本或PID数字单独不授予操作。

## 分开维护四条真实链

| 层 | 本轮直接证据 | 不能据此断言 |
| --- | --- | --- |
| renderer自然通知 | 初始化及对象首次通知，PID32、主/Overlay两个GameID64，37B共享流 | 有消息调用就成功投递、输入恢复或Running续租 |
| Steam原生消费 | CSteamEngine消费同名PIDStream；具体type分流，已有记录的Overlay PID/状态辅助过程 | 能以外部重发消息重新绑定已丢失记录 |
| UI原生反馈 | 1020079/12B→OnAppStartedRendering置dirty→RunFrame条件调用IClientUtils::SetFocusedWindow；1020078/12B→GameStreamManager::OnAppStartedProcess | callback名称是实际渲染成功回执；两个事件就是AppLifetime Running |
| 原生Running发布 | 单独CUser记录生命周期和AppLifetime1020030/20B，另有UI本地overview/列表投影 | focus缓存、Overlay PID或GameStream通知等于真实Running |

SetFocusedWindow新版本的具名Map/typed实现已独立绑定：直接可见engine缓存写入与1040015/20B通知；其opaque helpers/消费者未穷尽，故既不把它当已证原实例恢复API，也不作全局无其他副作用断言。UI消息中的PID/GameID没有OS birth/path/session或账号。

## 37B长度与消费副作用

Windows旧已核CSharedMemStream的Put/Get第三参数25是十六进制37字节长度，不是selector。Get推进共享读位置并减少可读字节，可能预读，因此不调用真实Get作为只读探针，不抢取Valve原pipe/callback。不同模块/平台的同步对象vtable槽不能直接互换。

Linux32 client CUser packer也为37B：+0 PID32，+4/+12两64bit槽，+20固定type1，+24为原mode==2?2:1，+28..36零。Linux64 renderer初始化是type2/mode2；须区分具体发送者，不能把所有37B都当同一事件。

## 已处理标志、fork与锁定对象

Windows179 CBaseOverlayRenderer构造清+79/+7a，首次通知后置+79=1，Put返回不控制置位；某些明确资格拒绝分支也会置位。实际CD3D12Renderer继承base offset0并走该构造，已核资源清理不提供已证同PID睡眠reset。其他writer、别名与opaque callee仍未知，不宣称已穷尽全部reset。

Linux64的56470强制初始化路径比较当前getpid与保存PID，相等则返回，日志明示进程fork。它是PID变化后的重初始化，不能称同原PID睡眠自愈；另一renderer producer的同PID生命周期仍待核对。不得因为Linux实际体验较好就把这条fork分支移植成YMCC唤醒策略。

SteamOverlayRunning命名对象的存在检查有真实CreateEventA/CloseHandle副作用，存在性也参与Steam内部判断。外部持有句柄会改变对象寿命，不能作为无副作用锁定或真实输入/Continue证明；当前没有加入此策略。

## 证据入口与下一缺口

工作区研究根为`G:/YeManCC-Work/Docs/Research/Sleep/SteamNonSteamRecovery-20261011`。优先读`WINDOWS179-CONTACT-RESET-20261015/CONTINUATION.md`及当前`External-AI-Results/TASK.md`、`Codex/REVIEW.md`，不凭本文固定revision猜最新交接。

- `WINDOWS179-CONTACT-RESET-20261015`：新版renderer/D3D12构造、标志、资格与清理，有界169项。
- `STEAMUI179-OVERLAY-STARTED-BRIDGE-20261015`与`STEAMUI179-FOCUS-METHOD-IDENTITY-20261015`：新版真实callback→焦点方法身份，分别194/156项。
- `STEAMUI179-QUIT-RENDERING-CONSUMER-20261015`：目录名是早期候选标签，实际1020078被证为OnAppStartedProcess；240项不证明全局Running副作用。
- `TRAE025-LINUX-RENDERER-REVIEW-20261015`：Linux64初次发送与fork门，56项；另一producer仍缺证。
- `POST-USER-RESTART-20261015`：更新后OS身份与磁盘缓存配对；现场会变化，使用前重新核验。

检查数字只是对应静态范围，不相加成系统兼容率或实机案例。必要下一证据是同原实例的自然再次发布/合法恢复入口、真实账号/管道/命名空间以及实际输入和Continue；未闭合前不写PID/Running、不清私有标志、不重放37B、不卸载重注入、不调用未知私有接口，不以反复构建代替实测。


## 2026-10-15新增：Linux同PID、不同renderer对象的条件式重建

固定1791918634 Linux64 renderer e6ff619a...已独立核真实VulkanSteamOverlayDestroySwapChain与Present导出：同map/key删除旧CVulkanRenderer，下一Present缺key才新建对象，base ctor清首次处理+79后具备再通知机会；存在key返回旧对象不清flag。它与fork/PID变化不同，也不是同一保留对象reset。真实Vulkan layer caller、睡眠触发和实际stream接受仍未证明；不能直接移植Windows槽位、清私有flag或重发37B。证据 `TRAE028-LINUX-SAMEPID-OBJECT-CYCLE-20261015`，101静态项/12FDE不等于实机恢复。前文“另一producer待核”应按此有界新结论接续，剩余缺口是真实caller/睡眠因果。

R48仅补lifetime observer本地计数与身份，原账号/准入/单PID/tracker/权限边界保持。原游戏实际输入和Continue仍需独立现场证据。


## 2026-10-15新增：Windows179也有条件式图形对象重建

`WINDOWS179-RENDERER-OBJECT-RECONNECT-20261015`核到真实SDK/登记绑定的ResizeBuffers1处理路径：已有D3D12 swapchain record且实际队列参数齐备时，Steam mapping替换renderer对象；下一Present1经同manager/getter进入commonFrame，重新具备首次通知机会。6完整root/10parts/922instr/94静态项通过。这修正“Windows完全没有已核自然对象重建路径”的说法，但不是已证睡眠因果、Steam运行关联恢复或安全的YMCC对外接口。不得强制游戏resize、伪造参数、清私有flag或重放stream。旧178 Steam接收端合同不能代替当前179 type1/4的record语义，后者另核。


当前179 Steamclient接收端亦已核：type1/4早期1020079 callback之后，直接BSetOverlayPIDForGame按原PID+完整GameID或完整GameID fallback查既有记录；缺record返回false，后续数值Overlay状态调用不代表Running成功。另state helper仅修改既有record。早期callback/opaque下游仍未知，因此只能说这两条直接helper不能补建，不能全局断言自然新通知绝无间接作用。证据 `WIN179-PIDSTREAM-RUNNING-RECIPIENT-20261015`（父任务复验161项/3root/9parts/870instr）。这使下一项集中到真实原record存活/重建，而非反复发Overlay通知。


## 2026-10-15新增：当前游戏环境不能替代原Steam身份

本机普通模式原2077的SteamGameId/SteamOverlayGameId为shortcut32，同次reporter保留完整64；SteamAppUser两者也不同，UI选中的登录账号资料只与reporter匹配。游戏加载的Steam API标为GSE，其固定binary存在三个数字键写环境路径；具体执行时刻/账号写者未证。不从游戏当前环境重选GameID、账号或恢复目标。

固定179 launch源端type2 AppId本来取高32，不能由完整ID低24=0推断AppId0；另外两个GameID默认独立完整64。重登扫描另要求账号字符串、非零AppId、uint32资料/flag及安装路径。实际V_atoi十进制路径对高位AppId仍非零，不能臆称溢出为0；真实表成员/路径/睡后拒绝仍unknown。

最新研究接续在研究根LOCAL2077-ENVIRONMENT-REWRITE-20261015/CONTINUATION.md与WIN179-SHORTCUT-LAUNCH-ENVIRONMENT-20261015。该样本没睡眠，旧大屏版本亦不同，不泛化全部非Steam/Linux。metadata存在、Running列表移除、CUser record删除分开；恢复继续联合原YMCC单PID、tracker、OS PID/birth/path/session和可信Steam UI账号。未加入env写回、SDK修改或新私有恢复调用。


## 2026-10-15新增：原UI账号选择后的CUser现场记录对照

当前179/e904…模块的完整SteamID getter经typed同槽/实际字节核验，可沿有限用户向量对原Steam UI完整账号做唯一64位匹配，读取该账号同完整GameID的原生记录。研究目录LIVE-WIN179-CUSER-ACCOUNT-20261015已完成本机普通Steam非Steam2077一次授权20秒启动/正常关闭对照：CUser记录0→1→0，运行5份主PID27040与OS创建身份、精确tracker、原生UI4一致；关闭时一份header竞态诚实标unknown后再观测稳定0。完整账号值未持久化，不读其它账号records；原进程/模块/账号/向量前后复核仍不构成原子快照或登录代际lease。

这是只读现场诊断路线，尚未集成运行时、未睡眠、未证明输入/Continue或恢复接口。下一次原实例仍活但UI11时，先区分真实原生record/primary仍在、已丢/换实例与竞态，不以UI行或历史console ProcID替代CUser记录。新版本hash不匹配即unknown，不从低32账号补造完整ID，不按焦点换目标，不重新启动游戏凑Running。详见研究目录REPORT/CONTINUATION/SUMMARY。


## 2026-10-15新增：真实Vulkan caller及原生Return分支

TRAE032-LINUX-REAL-VULKAN-CALLER-20261015已核真实layer Destroy/Present到renderer绑定调用和arg13公式，已正式回执；Destroy miss亦可callback，初始化repstosq后dword为P+68，pointee类型/消费及睡眠因果未知。原task要求Destroy后同数值handle再Present过严，已改revision18只补真实Create成功后新key登记→已核Present，保留同原OS游戏实例，允许GPU对象句柄合法变化。

WIN179-RAISE-WINDOW-SAFETY-20261015把本机固定179 Apps.RaiseWindowForGame登记/真实vptr接到IClientUserMap134、CUser同槽：返回1/2/3分支与同GameID其它PID尝试已核，原始结果语义及下游仍有边界。调用完成不是原实例输入/Continue证明，现有YMCC原PID/birth/path postflight仍必要。4root/10parts/558instr为静态/只读接线，不是远端MSI模块身份或无任何启动副作用的全局证明，未调用该方法或改运行时。
