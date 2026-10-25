# Steam睡眠策略：Linux / Gamescope与Windows的证据边界

维护日期：2026-10-14。当前运行时为`steam-wake-round36-dispatch-state-recheck`。本文件是实现方向和证据分级，不声称已达成真实手柄输入/Continue，不新增开关或设备控制。

## YMCC的角色

YMCC可以借鉴Gamescope的真实状态反馈闭环，在Windows上承担**原实例校验和有限恢复协调**：固定睡前游戏的OS PID/birth/path/Windows session、YMCC单PID完整规则、精确Steam tracker/native主PID和Steam原进程/账号；只读比较醒后状态；通过已核验的原生入口有限恢复；再读取真实状态，单独验证手柄输入和Continue。

当前YMCC没有实现Gamescope compositor、XWayland/Wayland输入路由或其向Steam发布的焦点backchannel。读到PID/窗口、配置或Running行不代表游戏实际收到输入。不能把记录正确状态等同于Steam消费反馈成功。

## 官方源码与两条方向

2026-10-14本轮实时读取Windows x64与Linux ubuntu12正式manifest：版本均1791918634，steamui_websrc_all包文件名和SHA256相同，SHA256 aedfabf3da1fbbb76026ce43f203570a8793a06aae6701345ec7579758878534。sp.js仍与先前SHA256核验缓存5a28c25041d07e69cf591a414519e0cf970b6884c1a0106659dcd9f1fb9586f9一致。共享UI包不表示各平台原生输入/Overlay/进程会话相同，不把当天快照当未来最新。

Gamescope本轮固定上游commit `2b5cf191d1047d6895516a834de8a72ac1c342f3`（上游提交时间2026-10-14T02:37:15Z）。steamcompmgr.cpp在SteamControlled路径根据实际候选窗口选择focus，可用XRes取得PID，并向Steam发布当前焦点窗口/App信息。它单独区分基础窗口、输入焦点和键盘焦点；基础focus窗口存在不保证input focus存在。该代码是平台集成证据，不能当作非Steam游戏两次睡眠必恢复的测试。

Steam UI的`EnsureCorrectGameOverlayState`则在`IN_GAMESCOPE`为真时，把合成模式/菜单/页面推导出的Overlay状态通过`SteamClient.System.NotifyGameOverlayStateChanged`通知系统。函数在非Gamescope环境直接返回。这里是**Steam UI→系统**的反馈；不是Gamescope统一的游戏重挂接API，也不是已核验Windows睡眠恢复接口。没有证据允许YMCC在Windows强行复制此调用，更不能改IN_GAMESCOPE或伪造通知。

官方Steam Input Gamepad Emulation说明：Windows由Steam Overlay挂接XInput、DirectInput、RawInput、Windows.Gaming.Input并提供模拟控制器；macOS/Linux由驱动提供模拟输入。这证明依赖链不同，不证明此次Windows失败必为Overlay，也不证明全部Linux环境没有同类问题。需区分SteamOS/Gamescope、Linux桌面、原生游戏/Proton及真实睡眠测试。

## 已知机制与未证明结果

| 机制 | 当前已知 | 不能据此宣称 |
| --- | --- | --- |
| Valve RaiseWindowForGame | 已验证Windows“返回原运行游戏”的实际入口，YMCC按原实例准入；不是Launch | 已恢复丢失的Steam Input/Overlay会话或真实Continue |
| Valve ScopeRunningApps | 已核验函数从本机native overview派生运行列表；只在已运行且缺列表时用 | 将native local11改回Running，或重新挂接已丢会话 |
| Windows原PID窗口返回 | 用真实原实例/窗口校验进行有限foreground请求 | 窗口在前台等于输入到达或Steam关联恢复 |
| Gamescope状态backchannel | Linux compositor提供真实焦点/输入路由信息，Steam UI存在专用平台分支 | Windows有同等反馈入口，或强发通知即可睡眠自愈 |
| 隐藏Overlay判别 | Valve导航cache可回退last context，静态类型不足以证明交互；Hidden合成与系统UI覆盖仍需区分 | 单凭DOM hidden、/apprunning或composition cache即可安全忽略保护 |

**目前没有一条方案已满足本任务完整实机验收。** 因此不能写“Gamescope反馈是唯一已知自愈方法”，也不能把其余已核验机制说成全是未知。准确表述：入口/静态契约的已知程度各不相同，完整Windows恢复效果尚未证明。

## 后续代码推进条件

优先核验Windows真实会话/焦点反馈与原游戏身份的联系，以及隐藏Overlay保护是否有可复现误拦。新路径须具备：正确的Windows接口方向/平台适用性、精确GameID及原PID/birth/path/session绑定、用户交互保护、发送前重验、真实状态回读，并遵守原60s权限与单默认on/Off。

现有代码未直接调用NotifyGameOverlayStateChanged/SetComposition，也未把R35诊断用于授权。本轮策略核验不新增此调用、不改保护。若将来证实窄范围误拦，再写必要回归与隔离编译；未知函数/状态继续拒绝。不能通过正版AppID伪装、锁定Running/Overlay信号、写列表、全局forceinputappid或重新启动来代替恢复。

2026-10-14用户进一步授权联合适配虚拟手柄并打通消费层。消费链按宿主输出、系统独立读取、Steam配置/路由和原游戏实际消费分别取证；复用现有虚拟手柄任务的协议和回执，协调具体写集。此授权不将设备/宿主自报成功当游戏输入，也不覆盖并行源码。HidHide、风扇、MSI黑名单、共享睡眠调度/游戏阀门/暂停所有权仍保留原边界。用户的Linux成功观察作为有价值的比较线索，未得到环境/日志前不补写成所有Linux或同一实例两次睡眠的验收。

## 一手来源和本轮记录

- Steam Input官方说明：https://partner.steamgames.com/doc/features/steam_controller/steam_input_gamepad_emulation_bestpractices
- Linux manifest：https://client-update.akamai.steamstatic.com/steam_client_ubuntu12
- Windows manifest：https://client-update.akamai.steamstatic.com/steam_client_win64
- Gamescope固定源码：https://github.com/ValveSoftware/gamescope/blob/2b5cf191d1047d6895516a834de8a72ac1c342f3/src/steamcompmgr.cpp
- 工作区证据：`Docs/Research/Sleep/SteamNonSteamRecovery-20261011/WEB-LINUX-WINDOWS-SESSION-20261014`，包含HTTP/hash回执、官方资料缓存、固定源码及相关函数。

升级继续先读STEAM-UPGRADE.md和docs/steam-integration-map.json；源码身份/编译/静态模拟均不算真实游戏输入或Continue。R35窗口只读证据仍在ROUND35-WINDOW-ACTIVITY-EVIDENCE；R36发送前重验与隔离构建见ROUND36-DISPATCH-STATE-RECHECK。平台研究不被冒充运行时恢复接口或实际输入验收。

## 2026-10-14 联合虚拟手柄消费链

用户明确授权在本任务中联合参考本机虚拟手柄，必要时适配消费链。设备恢复与Steam运行关联分开判定；任何一层成功均不代替后续层。

| 层 | 正面证据 | 成功仍不能证明 |
| --- | --- | --- |
| YMCC/InputHost输出 | 原宿主/输出代际的实际报告提交、传输回执 | 系统或游戏已读取该报告 |
| 系统读取 | 同一设备及输出代际的独立API读取与变化 | Steam选择正确配置、原游戏正在读取 |
| Steam接收/路由 | 真实控制器、配置选择、原GameID/窗口会话与时间一致 | 原游戏消费输入或Continue正确 |
| 原游戏消费 | 同一PID/birth/path/session下真实输入的响应或可验证消费证据 | 其他游戏/设备族群也通过 |

仅时间相近不能把不同宿主、设备、睡眠代际或游戏实例拼成同一链；跨进程可见性可能不同，helper的XInput读取不能冒充原游戏读取。无消费层观测时保留unknown。消费证据及工具由独立智能体收口到VIRTUAL-CONTROLLER-CONSUMER-20261014，Steam/Gamescope的两个源合同研究分别在GAMESCOPE-FOCUS-PROTOCOL-20261014与STEAM-WINDOWS-FEEDBACK-CONTRACT-20261014。

## R36发送前同步重验

生产JS已复现：`await inputSnapshot(false)`完成前的读数可能被同一microtask间隙的锁屏/菜单/键盘/Overlay、另一App焦点、控制器断开、编辑器变化或native overview变化取代。旧代码先发送RaiseWindowForGame，之后才发现变化。R36保留配置查询的有限异步流程，但最后无配置状态读取为同步；在发送前重查原目标overview/GameID、焦点、控制器、编辑器、账号和交互保护，无await穿过最终授权检查。

17组生产脚本回归含11种既有漏验的间隙变化、账号/GameID原保护、稳定原实例与已发送后的变化。恢复动作本身未增加，未增加重放/预约/权限期限；原实例校验及Off/代际取消保持。该修正保护真实用户交互，不被声明为实机第二次睡眠故障的全部根因或完整自愈。

## 两份源码研究收口后的补充边界

Gamescope固定源码的GAMESCOPE_FOCUSED_APP是input AppID，GAMESCOPE_FOCUSED_APP_GFX是base/GFX AppID，GAMESCOPE_FOCUSED_WINDOW是base window；keyboard焦点另有真实路由。全局GAMESCOPE_INPUT_COUNTER由keyboard/pointer/touch活动更新，不是原游戏gamepad消费ACK。其session.active、display_sleep与EIS device_resume也各有不同用途；EIS路径为pointer/keyboard，不能只凭resume名称认作虚拟Xbox恢复。

Valve UI的真实FocusChangeEvents回调会更新窗口关联，但仅在focusable AppID集合成员增删时自动ScopeRunningApps；相同集合反复到达不保证重新派生列表。当前已核验的bounded rescope机制仍只从真实native Running状态派生，不得改native11或注入伪focus event。

Windows固定1788652215原生SteamUI.dll确有NotifyGameOverlayStateChanged绑定，Plat_IsGamescope检查使用断言，断言之后可继续调用。UI1791918634的调用路径在非IN_GAMESCOPE直接return与该原生事实不可混淆。已核验参数/调用链没有原PID重挂契约，不能强发候选来伪装修复；也不能写成所有Windows实现都必然noop。已有Overlay GameID/PID callback是可读缓存线索，不能证明freshness或实际消费。

两份研究的固定源码/字节/版本证明及Windows尚缺的原PID映射与hook/controller-mapping恢复契约分别在GAMESCOPE-FOCUS-PROTOCOL-20261014和STEAM-WINDOWS-FEEDBACK-CONTRACT-20261014。有趣能力已记录供后续独立判断，未扩大运行时设备/路由权限。

## 消费链计数已修正的范围

生产计数原先使用sinceBoot累计达到50作为唤醒持续证据，会跨代借用旧帧。现只在当前真实power generation及采样host epoch中累计50个accepted；neutral/nonNeutral分开，旧全局累计独立标注。15组真实生产片段before/after回归通过，局部字节补丁未触及片段外内容。schema2明确host-only及gameInputUnverified；它没有改变设备恢复、报告发送或Steam路由。

分析器27组离线回归把host自报、独立OS观测、Steam配置与原游戏消费分开；时间/身份/代际不充分时unknown，明确分歧时contradicted。Steam恢复generation、共享power generation与宿主epoch保留各自命名空间。工具和计数的正确性帮助定位消费断点，不被写成实际消费层已打通。


## R37 ACK到消费链的精确证据边界

R37在真实SUBMIT_FRAME调用前、既有提交mutex内取绑定协议power/epoch/sequence和run/host/request引用；接受后沿用严格fast validator或已解析DOM回执，在既有首帧/50帧/有界拒绝事件投影`ackIdentity`。绑定协议power=1与当前共享power=3可以同时有效，不能用后者替代ACK身份。计数阈值与中性帧区分继续保留；没有新增逐帧JSON、I/O或锁，身份投影失败不改变接受结果。

授权1131519/R33包实际有两份独立reader日志，之前提取范围遗漏了它们。现已保留探针start、亲本PID/精确FILETIME、UTC/tick及API类型。症状墙钟窗口69次DI/WGI成功读取、8次值变化，只证明观察器读到了特定端点；这些端点尚未绑定InputHost输出与Steam控制器，亦没有原游戏消费回执。不能凭相近时间、同名设备或控制器数拼成端到端成功。

分析器按日志原始数字token保留超过2^53的FILETIME/u64，明确区分枚举、成功state读取和读取值变化。同PID不同观察启动、父出生冲突、stop后行分开；缺精确亲本join或实际端点映射时保持unknown。当前缺口是ACK→同一虚拟OS端点→同一Steam控制器生命周期/配置→原实例实际响应，另需真实Continue。R37仅补可靠接线和证据，不宣称消费链已完成；运行身份、测试、隔离构建与剩余要求见`ROUND37-ACK-CONSUMER-CONTRACT`。


## R38亲本身份与可用端点名称桥

独立reader已保留parent PID与完整出生时间，旧native boot仅PID/墙钟无法精确连接。R38在已有boot事件只读一次本进程GetProcessTimes，将FILETIME逐tick原样作为字符串导出。分析器以同PID、完全相等出生时间、唯一先前boot及有效观察生命周期建立“native是reader亲本”的局部关系；身份失败保持unknown，不能按时间接近或同generation借证据。该关系不证明共享wake、虚拟端点、Steam消费或原游戏响应。

端点源核对已确认：targetPnpDetail包含五组精确PnP角色候选；DI GetProperty(DIPROP_GUIDANDPATH)及同路径CM property回读能连接interface与PnP名称。分析器此前未保留这些来源，补离线连接可以识别同device-instance标识名候选。缓存字段、多root、API失败、截断和重建必须保留边界。尤其RECREATE_TARGET允许同一host/协议tuple下Dispose+Create，逻辑targetId或同PnP字符串不能当端点incarnation；WGI nonRoamableId也没有已验证的PnP/DI桥。后续控制器生命周期代码必须从真实创建边界取得，不能从ACK序号或VID/PID推造。

本轮实机仍1131519/R33，R38源码/fixtures/隔离构建不是新实际输入或Continue。详细源身份和最小后续写集在R37 endpoint-contract-map/report；R38最终构建及剩余目标以本轮CONTINUATION/REQUIREMENT-AUDIT为准。

## R39 实际创建tuple与局部对象寿命

已补原创建command日志缺requestId/bound protocol power及同tuple recreate不可区分两个具体证据缺口。发送前实际envelope在既有lock下捕获；宿主SDK创建的独立ordinal/token在失败/释放前闭合。分析器只保持本行local ACK与对象自报，不能把它变成OS设备寿命、WGI/Steam controller或原游戏消费。恢复动作、Gamescope研究边界与原实例/账号权限未改变。

操作者本机无手柄、远程控制在且不能睡眠。只读现场基线：Cyberpunk2077 PID2148/birth134364476905738337由Steam PID30460启动，真实tracker添加原PID；Steam local Running4/listtrue/controllers[]、XInput4槽1167。这证明观测时启动关联，不证明手柄输入或睡眠恢复。下一样本由操作者经Steam库启动原生游戏，独立绑定并对比；不把第二样本当第一次睡前原实例。

## R40 先建立运行期关系，再接消费与睡眠

2026-10-14用户明确将当前阶段转为持续联系原游戏与Steam。本轮已增加独立原句柄/账号/完整GameID的只读关系记录、生命周期状态、连接退避及统一消费者入口，见[STEAM-RUNNING-LINK.md](STEAM-RUNNING-LINK.md)。这层不消费睡眠capture、不接管共享调度/游戏阀门/暂停，不通过焦点换目标；原58.5/60秒操作边界保留。公开一手来源没有本轮所需的非SteamRunning keepalive契约，Gamescope读/写方向和Steamworks边界详见ROUND40-PERSISTENT-STEAM-SESSION/GAMESCOPE-SOURCE-CHECK.md。持续观察不等于native重新关联，实际部署后的长时间联系尚待验证，实际输入和Continue仍未验收。
