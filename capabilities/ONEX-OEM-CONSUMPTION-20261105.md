# HC089：X2组现有消费链已核，保留各型号输入差异

更新 2026-11-05T05:51:51.659951+00:00。本批只审当前已捕获源码及固定HC协议；X2、X2MiniPro和精确OneXPlayer3已有厂商输入owner/session/producer接线，不能概括为只有注册。MiniPro的M1/M2走F15/F16 canonical键盘链，vendor rear发布明确排除此类；X2 Home是点击，OneXPlayer3的Home/G-Key为真实held。这些差异需保留，避免新增重复生产者或套Apex协议。

16段/483行当前消费链引用已保全，协议数据逐项结果见 `G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\observations\HC-CODEX-089-ONEX-X2-LIVE-LIBRARY-CONSUMPTION-AUDIT\agent-protocol\REPORT.md`。厂商目录/按键接线不证明标准GAME物理根、slot或虚拟输出；源停止/取消代码也不证明实际I/O归还。本轮没有执行产品、测试、构建、导出或硬件，原Od/47case和用户Apex初步成功均不外推到本快照。

消费端已确认接收HC088，当前优先修原夹具/核收语义，家族GAME扩大前会给总库明确API合同。库侧继续承接该合同，不提前添加未消费API或另建机型表。正文 `G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\observations\HC-CODEX-089-ONEX-X2-LIVE-LIBRARY-CONSUMPTION-AUDIT\CURRENT-CONSUMPTION-REPORT.md`；机器消费链 `G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\observations\HC-CODEX-089-ONEX-X2-LIVE-LIBRARY-CONSUMPTION-AUDIT\CURRENT-CONSUMPTION-TRACE.json`。下列历史与全家族未决项保留。

---

# OneX 家族背键／专用键接入与 OP3 现场核查（2026-11-05）

## 交付后普通动作链核验（仍未实机验收）

现有原生快捷动作测试的当前依赖抽取已补齐，未改产品控制代码；Legion motion、模型观察与录制通知是独立内存边界，不替代各自专项。复用原producer增加精确`onex-consumption`分组，默认all只执行该组一次并保留原断言。当前主线当次运行3766项检查通过：21个OneX机型/别名入口、77个登记操作ID、普通/Steam Deck/Xbox三模式下231个按键-模式案例。三类取消（录制、退出、规则代次）分别231例通过；无保存规则不自动添动作。代表动作执行至已有`os.openTouchKeyboard`回调，OS/设备实际效果为内存边界，不能推成实机键盘已弹出。X2/真正OP3通过其独立ready/发布入口测试，未编造旧HID输入报文。

将原生消费者故意改为“虚拟关闭就return”的私有源码可以编译，随后在普通模式实际动作断言失败。原共有all模式保留F7归属断言失败，没有给整套夹具或正式发布打绿。之后共享main.cpp发生其他修改，重取原producer的发出源码，只规范化输出路径后与已执行C++完整相等；只作原结果引用，不冒充又一次执行或整个当前主线通过。详见`shortcut-closure-repair/SELECTED-CONSUMER-AUDIT.json`和`CURRENT-SCOPE-REFERENCE.json`。

HC084恢复了持久MCU电机等级的64字节来源帧，但Linux/HHD低等级字段不同；只有等级5帧一致。Windows该方法的report-ID包装、硬件等级回读与归还仍未知，等级0不能当作停止。已核对原件SHA并记录`MOTOR-SOURCE-INTAKE.json`；未新增写者或修改初始化。仍以手动XInput请求/停止日志及实机结果验证当前无震动原因。本轮没有重新导出包，继续使用已交付测试包。


## 09:32 实机失败续修：家族短按显示闭环与手动震动取证

用户确认实际没有震动，且专用按键按下均未亮起；这两项实机验收均未通过。范围为 OneXPlayer 家族，精确机型仍各用自己的能力表与协议。用户称 OP3 的本档 DMI 为 ONEXPLAYER APEX，不改写身份、不套用真正 OP3/X2 的 HID 初始化。

档案 `YeManCC-logs-20261105-093203-OP3-震动失败-专用按键失败-虚拟手柄失败-充电失败.zip`，SHA256 `7ecbe6c1a3b1fec552891a2cb4b8f86d34caffd1fdfc9eea209f7a62058dadbc`。本次 PID10692；原生 EXE SHA 匹配09:07主线包 `4ab1dbb1e296237ab7ebbcc68bdeb136156b713a97e931449bd6d515847e3da5`。

- 已知缺口：原 `GamepadVisualizer.vue` 只接 `gamepad.state` 周期快照；Turbo、Keyboard、Orange 的真实键盘边沿约1ms，能够进入原生观察但在快照前释放，因而界面不亮。现在家族统一消费已有 `oem.keys.observed`，按精确 family/deviceClass/schema/能力目录过滤，用现有180ms显示反馈，独立于held状态。隐藏/换机型/销毁时清理，不执行快捷动作、不改Host输出。
- 接口闭环：`oem.keys.get` 原回复缺少deviceClass，状态接口却包含它。已为预览/录制目录回复增加相同精确机型身份，并由真实原生IPC回复经生产规范化函数进入Vue验证；不是仅给内存桩补字段。
- 实体显示：总库已有 `physicalKeyIds`，现在预览也消费该字段。APEX上三颗专用实体键与两颗背键各自显示；组合/长按仍保留原录制和动作ID，预览投影到参与的实体键。其他机型保留各自实体键数量与名称，未统一成APEX三键。
- 快捷动作：导出的 `input.buttonMapping.rules={}`；未有已保存专用键规则，不能据此宣称配置动作执行失败或擅自添加默认绑定。同一 `onex-oem-input-edge` 增加 configuredRuleCount、matchingRuleCount、actionStatus，明确无规则/录制拥有/等待执行；actionExecutionProven仍为false。
- 震动：16次请求与XInput调用均返回0，但用户确认没有震动。实际输出与读取都来自随包OpenXInput代理。尚无证据证明代理关闭通讯、OEM软件拦截、固件禁用或停止过早，不切换所有家族输出协议、不盲写XInputEnable/B3。B3 motor级别只在已审HC的X1Mini初始化中有依据，未扩大到APEX/OP3。
- 新取证只在 OneX 家族手动震动请求执行：同模块的只读 XInputGetCapabilities回执、物理身份episode/epoch、source/power/topology和pulseGeneration进入原 gamepad-vibrate；原停止函数增加onex-manual-feedback-stop，记录实际elapsedSinceWriteMs、zeroWriteAttempted、zeroApiAccepted与取消原因。取证读取安排在非零马达写入前，读取返回后重新核对来源/电源，避免取证延长已启动的脉冲；原写入/停止机制未变，接口和capability均不代表马达实效。无需额外收集脚本，普通导出即可保留。

软件验证：最终真实原生目录→生产规范化→Vue组件177项通过，涵盖21个OneX机型/别名入口；保留G1/G1AMD/G1Intel三条明确无OEM chord的空目录，其余按各自已登记能力反馈。最初140项内存目录验证及124项部分真实目录验证均保留，不取代最终目录闭环。修改前的真实Visualizer在同一套回放中失败，能检出不亮/实体重复显示。最终固定源码原生接口/录制1106检查、49个全家族机型/别名通过；移除真实目录API的deviceClass字段的语义反例由49项原断言检出（编译和运行正常结束后得到失败结果）。原XInput停止消费者日志开/关各77场景386检查通过，新增OneX手动证据不改变脉冲和零写，其他家族不产生OneX取证。旧历史对照各21场景失败保留。它们是限定范围软件回放，设备I/O为false，未替代完整正式门或实机验收。

首次原生录制因其他任务同时修改main.cpp而来源一致性失败，原raw保留；后续在固定源验证。XInput最初缺历史基线/本机SDK INCLUDE缺项的启动失败原件保留，采用原91a4历史基线及已安装26100SDK显式环境后通过，没有改系统SDK或重签原失败。正式夹具历史FAIL/UNKNOWN仍保留。本轮未改充电、风扇、MSI/ROG协议，也未新增HC DLL。OneX家族整体目标继续ACTIVE，震动仍为FIELD_FAILED_ROOT_CAUSE_UNKNOWN。

测试包已通过原生/前端完整编译与标准打包，交付 `G:/YeManCC-Work/Deliverables/YMCC-OneX-Family-KeyFeedback-20261105-101518/YeManCC.zip`，SHA256 `ae4cc2d46fac2ad604c7c1c4f2623d96ca5f18a7f68f005f869b5fffe25d205b`。114个DLL名称与09:07包一致；已审34库在源、Release和ZIP共102份逐个大小/SHA核对通过，未新增HC DLL。尚未实机验收，震动仍未修好。

详细原件：`G:/YeManCC-Work/Testing/YMCC/Evidence/OneXField093203-20261105`。测试包另有独立来源/编译/完整打包回执；未部署、未操作设备。


当前交付为源码修复及限定范围软件验证，未部署、未操作设备，实机动作和游戏消费仍待确认。用户所称 OP3 的这份日志，当前固件／机型身份为 **ONEXPLAYER APEX / HandheldCompanion.Devices.OneXPlayerApex**。保留用户命名与原始 DMI 的区别；没有为适配按键而改写整机身份或给 APEX 套 OP3 协议。

## 灯光串口身份补证与震动继续核对

目标继续为OneX家族背键、专用键、灯光、震动适配，未完成。上一轮三实体键计数是已完成的软件修正；本轮继续处理现场灯光缺口。

`FanLab/LightSetter/OneXLightingSession.cs` 的原串口拒绝消息现增加同一次已枚举行的 `observedHardware`：row、DeviceId、VID、PID及protocolAdmitted=false。最多保留前8行、每字段100字符；设备路径分隔符经JSON转义保留，控制字符及异常getter不会破坏原拒绝。没有追加WMI或扫描、不按COM3猜测、不修改命中CH340与COM格式的原选择规则，不新增HC DLL。它仍经原LightSetter异常JSON和native backendNote进入普通日志。

复用原 `LightSetterSelfTest --onex-serial-discovery`，候选27场景132检查通过；把该候选测试链接回真实旧session时编译成功，但在“普通串口失败应保留硬件身份”原断言处失败。错误芯片、通用端口、大小写、COM格式、歧义和观察上限等原场景保留；这是日志缺口修正，灯光写入本身仍未实机通过。

原 `controller.xinput-feedback-lifecycle` 在当前源码执行通过：诊断关闭／开启各76场景381检查，9类编译语义坏例检出，源与工具身份复核无漂移。实际执行物理源识别、写入、归零／失败保留与停止排程的内存端点，不当成实体马达或游戏反馈验收。

继续执行原 `controller.game-feedback-writer-consumer` 遇到历史工具失配，原FAIL均保留：先是只接D／Build路径，现已准入配置的Testing/YMCC/Evidence子目录，原Build nonce、归档字节、来源与拒绝条件保留，3个定向schema测试通过；再是旧writer仍要求24输入／19编译项和Task<int>，现与当前父链对齐26输入／21实际编译项、真实HostTerminalCleanup/CompactFrameReplyBinding与LoopResult，独立编译和来源核对通过，三项writer专属Xbox故障选择保留。完整游戏链仍停在native生成器旧power字面锚点：父模板已改为POWER_CONTEXT／PROTOCOL_POWER_CONTEXT／ROG_IDENTITY等真实上下文，尚未接齐，未把它当产品控制bug或游戏成功。下一步从固定来源补真实定义，禁止替换成恒真／成功桩。

证据为 `G:/YeManCC-Work/Testing/YMCC/Evidence/OP3Light1105/INTAKE.json`，native震动原运行在同层OV5；游戏链失败OG5／OG5B／OG5C／OG5D完整保留。未部署、未开设备或官方前端，整体goal保持active。

## 02:33:24 新包反馈与实体键计数修正

新档案 `YeManCC-logs-20261105-023324-onexplayer3.zip`，用户确认背键已能识别，且本机只有三颗专用实体键。最新进程 PID15104 的正常 `onex-oem-input-edge` 已记录 Keyboard 以及两颗背键。当前仍以现场 APEX 精确身份消费，未将产品名称强制改成新代 OP3 协议。

数量错误已经用实际状态函数复现：旧版将六个操作条目直接当六颗硬件，三颗键全按仍显示 3/6。原目录中的 Turbo、Keyboard、Orange 是三颗实体键；Keyboard＋Orange、Turbo＋Orange 与 Orange 长按是既有固件操作，须继续保留原 ID、录制与动作能力。

总库现对精确 APEX 描述发布 `physicalKeyIds`：基础键指自身，两个组合分别指两颗参与键，长按指 Orange。`oem.keys.get` 与 `oem.keys.status` 共用同一来源；原规范化器校验引用存在且同属专用键类别，显示层按实体 ID 去重。三颗实体键与全部六个操作分别回放都显示 3/3；Orange 与其长按一起仍为 1/3，背键依旧独立 2 颗。没有删除组合条目、改写已存规则、交换左右或更换采集协议；X1/X2/独立 OP3 分支均不套用 APEX 分组。

最新原检查：显示原入口共30项、实际端点8次执行／6次语义拒绝；总库消费者Release/Checked各41,349检查、保留原12次坏例拒绝；类型检查退出0。首次新断言缺少std::set头造成夹具编译失败，改用原闭包已有vector/sort后正常组通过；其后长证据路径触发MSVC包含路径限制，改用核心层短证据目录执行同一原ID通过。两次失败原件与费用均保留，没有放宽断言。新计数尚待下一包现场确认；本轮不再要求用户重新证明背键已识别。

本轮证据与新独立核收见 `G:/YeManCC-Work/Testing/YMCC/Evidence/OP3Keys1105/field-023324`；原始测试在同层 `OP3C1105`。前一轮 `completion-audit.json` 保留原样，本轮记录为该子目录 `COUNT-INTAKE.json`。

家族整体目标仍active：同一进程的三次灯光请求均因期望CH340而只观察到“USB 串行设备 (COM3)”拒绝，尚缺同次发现的DeviceId/VID/PID以判定端点，不能直接放行任意COM3；8条手动震动请求有实体XInput提交成功，但马达实感与游戏反馈仍未证明。原事件、行号及后续所缺证据在 `family-feedback-intake.json`。

## 日志已证明的断点

档案：`G:/YeManCC-Work/日志/其他/YeManCC-logs-20261105-onexplayer3-虚拟手柄和背部按键-011001.zip`。精确 SHA、原行号和原事件见核心层 `OP3Keys1105/field-evidence.json`。

- `onex-back-buttons` 共 44 条：0x22、0x23 各 11 次按下及 11 次松开。
- `oem-producer-edge` 的 `onex-hid` 同样有 44 条，状态均为 queued。这证明物理报告已到生产者队列，不能推出快捷动作已执行。
- 11 次 `oem-key-usage-query` 均选择 APEX；其旧目录只有 Turbo、Keyboard、Orange 及三个组合键，没有 l4／r4。因此界面状态会认为“总库无背键”，`gamepadOemPressedKeys` 也无法把已收到的背键 mask 转成目录键名。
- 导出的 Host 规范样本里还存在 1 条 `backButtons=2`；所以不能把这份资料解释为整条虚拟输出始终无背键信号，更不能从单条 SDK 样本推断游戏消费成功。
- 用户再次确认 Keyboard 和前端键均已按过，界面没有识别。补查既有 `focus-trace.log/input-edge-window` 后，128 条非注入键盘观察按当前 APEX 表可匹配 Keyboard 8 次、Turbo 8 次、Orange 6 次，以及 Win+D 的 Orange 长按编码 1 次；前三类 22 次匹配的观察持续时间都是 0～16ms。原行号、时间和证据局限见 `dedicated-field-replay.json`。这修正了前轮“没有足够专用键事件”的不完整读取；不能再让用户重复证明自己按过。
- 源码有两个确定缺口：按键页仅消费定时 `gamepad.state.oemPressedKeys`，短促按下／松开可完全落在两个快照之间；`nativeKeyboardShortcutHandleOemEdge` 在录制时直接返回，录制只剩实时采样，实际已排队的短键也无法录入。使用真实旧入口，Turbo／Keyboard／Orange 三项都编译后语义失败；原结果在 `dedicated-before-clock-seam/RESULT.json`。
- 上述键盘证据是组合与时序观察，不单凭标志、设备路径的事后查询判定发起进程，也不把它当快捷动作或游戏消费回执；官方前端是否抢焦点／覆盖仍未实机确定。

## 修复和统一消费链

保留唯一原链：`machineIdentity.deviceClass → HcOemInputCapabilityBinding/InputBindingCache → 原 native profile 与 OemCatalogRef → oem.keys.get / oem.keys.status → 录制、按键状态、现有规则消费者`。

1. APEX 原键目录补入继承的两颗 HID 背键 l4／r4。上游 `OEMChords.Clear()` 清的是键盘组合表，不会删除独立 HID 采集器里的背键。原采集器已发布这些位，不增加新的设备写入或第二个 reader。
2. APEX Orange 保留 0x21 HID 能力及 Win+G 键盘兼容；`captureRoute` 明确记录双来源，不把本轮实际看到的 Win+G 误写成已观察到 0x21。原六个组合条目及存储 ID 保留。
3. 现有 `HcOneXVendorInputContract.h::descriptorFor` 同时提供 `captureRoute`，给精确已准入的 X1／Mini／APEX 返回 HID 或键盘兼容来源；`oem.keys.get` 和普通 `oem-key-usage-query` 使用同一描述。没有新机型分配表、轮询、服务或 HC DLL。
4. X2／X2 Mini Pro／OP3 继续走各自原生会话，普通专用键和背键的能力查询不依赖开启虚拟目标。独立游戏背键输出仍受现有人格、映射、固件和游戏消费约束；本轮没有自动分配用户快捷动作。
5. OneX 现有 UI 事件入口按有效来源／规则代接收短按，录制时直接消费排队的按下和松开，仍遵守录制中不执行动作、失焦／所有者切换退出及独立 producer 权威。其他家族的入口行为保持原样。
6. 对当前 OneX 的键盘和 vendor 队列边沿发布 `oem.keys.observed`，按键页按同一家族、精确 deviceClass 和当前可采集键目录累计“已收到”反馈，不把短按延长成虚假的 held 状态，也不变更 Host 输出。普通导出常开保留 `onex-oem-input-edge` 的已识别键、来源、规则代和录制状态；`actionExecutionProven=false` 明确不能当动作成功。


## 最新 OP3 对照

截至本轮在线核验，HC `OneXPlayer3.cs` 最新修改提交为 `f2e58468d9917189d4be7ccfc242385ce2c3035b`（2026-10-30），与本地已审查文件逐字节一致，SHA256 `e3a6275e34db52fa60c7b5a421655af24d8580611fe9ebb875986817891809dc`。查询与下载身份见 `latest-op3-source.json`。

OP3：0x22／0x23 为 M1／M2，0x24 为 Home，0x25 为 G-Key；Home／G-Key 有真实按下与松开，不能降成旧 X2 的一次点击，也不能按 APEX Orange 解释。现有 OP3 第三页初始化及 `kOneXX3Keys` 已包含该适配，本轮核对并保留，没有更换冻结 HC 运行库。APEX 则保留自己的初始化、0x21 Orange 及既有键位方向；没有统一交换家族左右。

## 家族边界

| 已有机型路线 | 背键来源 | 专用键来源与边界 |
|---|---|---|
| X1 基类／AMD／Intel／Pro | 原 vendor HID 两颗背键 | Special／Keyboard HID，Turbo 键盘；保持原采集与初始化 |
| X1 Mini | 原 Mini vendor HID | 保持 Mini 自己的初始化，不借用 X2 Mini Pro |
| APEX | 原 vendor HID 两颗背键，本轮补目录 | Orange 0x21 HID＋原键盘兼容；保留原组合表 |
| X2 | 原 X2 vendor HID | Home 的一次点击与键盘来源保持原定义 |
| X2 Mini Pro | 原 F15／F16 键盘背键 | 维持 Mini Pro 独立键表及初始化 |
| OP3 | 原新代 vendor HID | Home／G-Key 真实按下和松开，0x25 单独消费 |
| 旧 Mini／Mini Pro、OneXFly／F1Pro、2／2Pro | 不凭家族名补入未知背键能力 | 保留各自已接入的键盘专用键与原限制 |
| G1 | 不新增未经证实的 OEM 按键能力 | 保留原空目录 |

库内 17 条冻结 OneX 源类路线及 X2／X2 Mini Pro／OP3 三条已审查扩展分别检查来源；9 条已存在背键来源的路线做了实际查询／按键名消费者回放。没有修改 MSI／ROG 控制实现。

## 本轮验证

- `controller.unified-oem-input-consumers`：Release／Checked 各 41,338 检查，原 6 类编译故障的 12 次语义拒绝保留。新增字段场景覆盖 9 条背键路线、OP3 Home／G-Key、APEX 两颗背键以及虚拟目标关闭时的原键盘消费者。
- 删除候选中 APEX 的两条目录项，再运行同一实际消费者：能够编译，但在“所有已准入 OneX 背键机型应暴露两颗背键”处明确失败。证据为 `apex-missing-keys-negative/result.json`。
- `controller.native-and-virtual-feature-status`：29 项显示检查，实际只读 endpoint 8 次编译执行，包含 6 次保留的语义拒绝；实际 APEX 源目录送进现有状态函数，在虚拟 OFF／ON 均不再落入“总库无背键”，收到 l4／Orange 后分别只提升对应已观察键。
- 原生录制原夹具扩展现有消费者边界：737 项检查／33 个机型与别名，通过。实际旧入口的三项短按录制反例均失败，候选通过；包括真实队列消费／日志白名单、过期代次拒绝、失焦和独立窗口所有者退出、MSI 不进入 OneX 新增强化、当前／过期 vendor receipt、录制中不执行动作。动作执行器是内存边界，不冒充真实快捷动作完成。
- `vue-tsc --noEmit` 退出 0。首个 `.cmd` 入口因内部找不到 node 失败，改用已配置 Node 直接运行同一 vue-tsc 入口通过；保留首个失败日志。
- 原 OneX reader／停止归还：Release／Checked 各 10 场景、890 检查，通过；保留旧代拒绝、按下／松开及取消回收。
- 当前 X2／OP3 WindowsTransport 的原 4 个指定场景（collection-filtering、reader-events、stop-immediate-release、disabled），Release／Checked 均通过。明确是四项指定范围，不是完整应用验收。

**未通过及未执行的边界保留：** 旧独立协议测试受 MSVC `crtdbg.h` 环境缺项阻塞；旧完整应用夹具先遇到 CRLF／LF 身份比较，再遇到后来加入的灯光 owner API 未定义内存端点，未作为本轮通过，探索性比较修改已撤回。综合入口还报告了并行 Steam 自定义库登记身份差异，本轮没有修改该登记。这些记录均保留在证据根，不把其失败写成已通过，不借此改 ROG 或灯光逻辑。

## 交付和下次现场判定

源码已就绪；本轮没有构建或部署安装包，不会改变目前群友正在运行的版本。下次候选正常导出后，先看 `oem-key-usage-query` 的 deviceClass、l4／r4 的 backIndex 与 captureRoute，再对照 `onex-oem-input-edge`、原 HID／producer edge、规则动作及 Host／游戏回执。APEX 仍用 APEX 路线；真正 OP3 仍用 OP3 路线。本轮已确认存在相匹配的专用键盘信号；新包的实际界面／录制、官方前端是否覆盖、规则是否执行和游戏是否消费分别验收，仍未确认的保留 UNKNOWN。

核心证据：`G:/YeManCC-Work/Testing/YMCC/Evidence/OP3Keys1105`。最终源码身份、通过和阻塞原件见 `completion-audit.json`；这里是导航和审查记录，不是新增运行分配表。


## 后续固定快照测试包交付（2026-11-05）

三颗实体专用键与六种操作已在 physicalKeyIds 分开计数；组合与长按的既有规则继续保留。用户已确认背键可识别，新包三键计数仍待实机确认。身份继续沿用现场 APEX，未把独立 OneXPlayer3 协议覆盖为 APEX。

完整测试包：G:\YeManCC-Work\Deliverables\YMCC-OneX-Keys-TestPackage-20261105-035647\YeManCC.zip（110495073 字节，SHA256 2511a66e14192b5aee81cbbf20c3ab23871df0f4bbe383c2ec73f55ae50b5416）。详情与源码身份见 G:/YeManCC-Work/Testing/YMCC/Evidence/OP3Delivery1105/DELIVERY-RECEIPT.json。原生、前端、InputHost、LightSetter 已编译，原完整 ZIP 身份及 34 DLL 共 102 处核对通过；完整夹具 NOT_RUN，未部署、未启动产品或官方前端。

本包固定于本轮源快照。之后主线发生并行修改，最终比较如实保留 STALE；这些后来改动未被本包验收，不能把此包称为此刻最新主线。第一次构建入口缺 Git 身份与第二次缺发布模板的失败原件都保留；之后用相同源码的规范目录和已核验的发布模板完成原构建／打包入口，没有修改发布门。

灯光增加同次串口枚举 DeviceId／VID／PID 诊断，仍未凭 COM3 通用名称准入。马达 backend 回执不是物理或游戏反馈证明；原游戏反馈整链夹具尚缺真实 native 上下文。家族目标继续 active。早期“尚未构建”是当时记录，交付状态以本节和交付回执为准。


## 当前源码后续核对与发现日志（2026-11-05）

当前独立灯光任务已接入 APEX HID-v2 源码，与上节固定计数包的旧串口路线分开。旧归档不包含新灯控端点字符串，但不是全机 HID 清单，不能据此判硬件不存在。本轮在实际 native 选择函数增加 onex-lighting-discovery 普通事件：同次枚举、四条候选、260 字符路径、有/无描述符及 VID/PID/用途页/报告长度/明确拒绝；只限 APEX，不新增查询、写入、计时器或匹配放宽。

原 tools/hc_lighting_native_consumer_selftest.mjs 的 --case apex-discovery 12 场景、67 检查通过；同源已编译“缺日志”反例被 ordinary-log-receipt-required 拒绝。AYA 无 OneX 事件、诊断故障不改选择均有实际内存场景。第一次错误抽取前置声明的编译失败保留。没有新 Full 或全局整轮。

原游戏反馈组合生成器已去掉失效的旧文字锚点，复用原父级身份/power/隔离状态；父级四段生成代码与历史原件逐字节相同。组合准备明确只剩 inputHostRogOffExecutionDefersSuccess 的真实 cleanup/pause/busy/wake/source 闭包，编译和业务仍未准入，不作手柄运行失败结论。

本轮只交源码与定向证据，新诊断不在上一个 ZIP。完整四项目标继续 active。机器可读回执：G:/YeManCC-Work/Testing/YMCC/Evidence/OP3Followup1105/INTAKE.json；报告：G:/YeManCC-Work/Testing/YMCC/Evidence/OP3Followup1105/REPORT.md。


## OneX 完整配套测试包已交付（2026-11-05）

此前“新发现日志未入上一计数包”保留为历史；本次已完成包含该日志、APEX HID-v2 helper、按键消费与当前 OneX 虚拟源策略的整包。原完整发布入口自然退出 0，源/产物/完整 ZIP、HC 87 文件、34 DLL / 102 处、114 个 DLL 名单检查通过。无新 DLL、无部署，FullAcceptance 与实机仍未授予通过。

交付：G:\YeManCC-Work\Deliverables\YMCC-OneX-Integrated-TestPackage-20261105-043536\YeManCC.zip（110511053 B，SHA256 bf8b631dd73b2fbc18baa9c0eb88493b0e4b9c84d197dfde66c46212265f6a4c）。Build ID 78e18dc406c2f9f963b8d8f12b9c9ea34999b351f0b6d811994688db10983329。报告 G:/YeManCC-Work/Testing/YMCC/Evidence/OP3Integrated1105/REPORT.md；机器回执 G:/YeManCC-Work/Testing/YMCC/Evidence/OP3Integrated1105/DELIVERY-RECEIPT.json。

原 12/67 定向 APEX 发现检查的三段函数和两个依赖与本包固定源精确相同，未重复实跑。最终源比较如实保留两处后继测试/构建工具变化的 STALE，运行代码在该次比较中未变化。完整家族目标继续 active：新包三键计数／动作、灯控实际端点与效果、游戏和物理马达反馈分别待现场；原共享游戏反馈上下文缺项和历史 FAIL 保留。普通 YMCC 导出即足够下一轮取证，测试说明随包单独提供。


## OneX 动作队列取证包（2026-11-05）

新增 `oem-shortcut-dispatch` 普通事件，只对 OneX L4/R4/Special/Orange/Keyboard 规则产生。它记录动作队列准入/拒绝、actionId、source epoch 和 WMI fallback 状态；`accepted=true` 只表示进入现有串行队列，不表示动作已执行或游戏已消费。现场按 `onex-oem-input-edge` → `oem-shortcut-edge/emit` → `oem-shortcut-dispatch` → `oem-shortcut-edge-delivery` 分层判断。

最新测试包：G:\YeManCC-Work\Deliverables\YMCC-OneX-ActionTrace-TestPackage-20261105-052000\YeManCC.zip（6d140ebbe96f8bd59229eddd7840d9f8aa0c0409b316663ba20504d1c1758832）。快捷键回归 7701/0；完整夹具 NOT_RUN，实体按键、灯效、物理/游戏震动仍待现场。


## 三实体键计数复核与动作日志补齐（2026-11-05，后续源码）

再次逐行核对原 `YeManCC-logs-20261105-023324-onexplayer3.zip`：原始身份仍是 APEX，旧清单把三颗实体键的六个操作当成六颗键；背键现已获用户确认能够识别。现有 `physicalKeyIds` 去重和六个动作 ID 保留，不能为了显示 3 个而删除组合／长按，也不能将 APEX 改绑成 OneXPlayer3。当前普通查询/UI原用例实际执行通过：30项呈现检查、8次原生执行（含6次语义反例）。新实机 3/3 尚未验收。

本轮另修两处确定的小缺口：内置 `catalog-onex-apex` 离线自检仍期待旧 6 条，已按实际目录改为 8 条操作（6 专用操作＋2 背键），不把它冒充实体键数量；`oem-shortcut-dispatch` 原先漏 Turbo、组合／长按、Home／Function／G-Key，且共享 L4/R4 等位没有家族判断。现在只限 OneX 家族，涵盖各机型现有的 12 个 canonical 操作 ID；不是一台机器有 12 颗键。增加 familyId/deviceClass 和 queueAdmissionOnly/actionExecutionProven=false。执行函数及原队列准入／拒绝分支未改。

原精确 `controller.input-diagnostics` 入口因旧 tsc shim 固定哈希变化而 BLOCKED，没有改 pin 或冒报通过。保持同一个原生诊断夹具和逐字节源码抽取，补充原生编译／执行得到候选 558 项诊断＋179 项键盘检查零失败，旧源码对照确实检出 84 项新诊断失败；6 家族×14掩码×2队列回执共168场景。这个补充结果不代替原注册用例的工具闭包／完整快捷键验收。旧完整快捷键夹具的 Legion/录制闭包编译错误、两次补充 shell 准备失败均保留；未据此改动无关家族或工具授权。

本轮证据：`G:/YeManCC-Work/Testing/YMCC/Evidence/OneXCountAudit-20261105/RESULT.json`。没有新整包、部署、设备／注册表／官方前端操作或新HC DLL。06:00历史包已有三键计数，但不含本轮补齐的日志和家族边界，不能继续宣称它与当前源码完全一致。灯光实体效果、物理/游戏震动、配置动作执行和 Steam 外部消费仍待普通现场日志及用户观察；家族目标保持 active。


## 后续整包交付：复用已完成的当前普通主线构建

上一节“本轮未打包”保留为当时状态，现已推进到整包交付。原普通发布 `20261105-063606-hc078-ec-write-admission-ordinary-r2` 的真实进程身份核对后，已观察到其自然exit0与PACKAGE_OK；没有重启它或新开publisher。原 source/build/archive验收再次通过，617文件；79份OneX运行相关源码与当前主线逐字节一致，DLL名单114与06:00历史包相同，文件名单无增减，没有新增HC DLL。

测试包：G:\YeManCC-Work\Deliverables\YMCC-OneX-CountTrace-TestPackage-20261105-070219\YeManCC.zip

SHA256：195deecf90210f059aaa7d39f6eca93e053947c70be539d355c2cc3f48e6764d

测试说明：G:\YeManCC-Work\Deliverables\YMCC-OneX-CountTrace-TestPackage-20261105-070219\测试说明.md

本包包括三实体键计数和补齐后的OneX队列日志。原生日志的accepted仍只是队列准入。已纠正测试说明：press可在edge的emit=true表达派发，不要求每次另有emit行；delivery主要是错误／取消事件，缺行不证明成功或失败。没有规则的日志不能证明“配置动作失败”。

机器回执：G:/YeManCC-Work/Testing/YMCC/Evidence/OneXCountTraceDelivery-20261105/DELIVERY-RECEIPT.json；完整核对见同目录REPORT.md。普通构建完整夹具NOT_RUN／formalReleaseAccepted=false；原工具闭包BLOCKED和旧失败不改判。未部署或操作实际设备。四项能力的实体／外部消费继续待普通日志与用户观察，goal active。


## 11:24:41 新日志：背键、专用键、震动限定范围

用户最新裁决：本任务负责 OneXPlayer 家族背键、专用键、震动，虚拟手柄修复交另一任务。新日志已匹配 10:32 包；背键接口有绑定，本次未见 L4/R4 有效按下。两次 Win+D 被解释为 Orange 长按，但同期 Raw Input 接口的导出时祖先含网易 UUYCInput，不能证明实体 OEM 来源或认定阻塞。匹配规则数为 0。四次震动均到 125ms 正常停止，实际震感待用户说明。未改控制源码、协议、设备或新建测试包；家族目标保持 active。

详细报告：G:/YeManCC-Work/Testing/YMCC/Evidence/OneXField112441-20261105/REPORT.zh-CN.md；机器可读证据：同目录 KEYS-AND-VIBRATION-REVIEW.json。此前 FAIL/UNKNOWN 和实机验收边界保留。


原厂偏好源码续查：已恢复 `userData/db/core.db` 的 `setting.vibrationLevel` 及 APEX CommonHid 调档／附带测试调用链；原厂 load() 会改写配置，后端关闭可直接返回，设置失败后仍可能保存请求值。保存等级不能当硬件回读，也不能将 Windows 菜单映射开关当 OEM 按键解锁。详见同一 11:24:41 证据目录 `OEM-SAVED-PREFERENCE-AND-INPUT-BOUNDARY.md` 与 `OEM-PREFERENCE-SOURCE-REVIEW.json`。本轮仅源码取证，无产品控制改动；日志侧新证据链仍待后续配套构建，不追认为当前包验收。


## 配套取证测试包交付：2026-11-05 12:23:48 固定源码

日志侧 SOURCE_READY 后，已完成原 publish.ps1 FastRelease 的配套 Native/Host 构建及 ZIP 核对。取证包：G:\YeManCC-Work\Deliverables\YMCC-OneX-KeyEvidence-20261105-122348\YeManCC.zip；SHA256 9b5bd9b851b4aa938d50ee35cbfb96e58bb09e5cdf006f1dacd8afd2a9e47fc2。Native SHA256 fc0f6cc8e081ae5dbe74a7f15e08995c25edb03c49f087b8ea453d5c0ca6c2ce。114 个 DLL 路径与前包相同，41 个审查替换文件在源码／Release／ZIP一致且与现场基包相同；没有新增 HC DLL。测试说明与配套回执在交付目录。完整夹具 NOT_RUN，未部署，硬件未验收；没有改变马达档位或按键控制策略。源快照中三份能力导航的并行变化及构建环境失败均保留原件；此包绑定固定源码，不宣称覆盖后来主线变化。目标继续 active，下一步为实体按键、保存动作后消费和实际震感验证。
