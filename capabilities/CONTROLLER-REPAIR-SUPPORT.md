# 虚拟手柄修复：能力总库适配支持

<!-- VC-CAPABILITY-R11-INPUT-CONTRACT:BEGIN -->
## R11 现场输入契约分层（2026-10-26）

Win5/OneX/AYA继续使用原总库及既有机型记录。R11 Win5为GPD G1618-05；公共`045E:028E`及`IG_00`不能单独推断家族、USB root或停启权限。首份导出writer revision78记录选定root与两条location paths且仍有active；补充导出revision118为active=null。两者仅表示各自导出时点，不能拼成当前归还成功。原ZIP成员与本地提取已逐字节核对。

后续输入合同分开提供五组证据：

1. **物理身份、完整性和位置**：原HID/game instance、选定physical root、明确库存完整性、location paths及当前代次；未知完整性保留UNKNOWN。
2. **采集与Host回执**：新读序列/完成时点、sourceLive、frameAccepted、neutralAccepted；只证明对应输入阶段。
3. **隐藏配置读回**：确切selected paths、计划完整性、配置前后读回、当前所有权及外来/继承项。
4. **刷新与写后归还**：原writer/attempt身份、in-flight/terminal、Disable/Enable结果及分段计时、原请求与归还readback；writer清空、helper结束和配置归还分别判定。
5. **外部可见性**：Steam等独立观察及对应时点/代次；本地Host成功不等于屏蔽成功，当前外部状态保留UNKNOWN。

Win5原日志同时存在持续采集/accepted与后续live但publication-suppressed段；Steam原日志可见物理slot0及虚拟slot1。主任务窄修仅涉及GPD现有profile/当前R3请求下原writer结束且归还readback结束时避免多余inverse，硬件效果仍待验。Disable/Enable计时为共用诊断；OneX/AYA、MSI/ROG权限均不扩大，ROG/MSI本轮OFF未覆盖。R9软件PASS仍绑定其原源码快照，不能自动沿用到R11变更。

[现场字段、原行与契约JSON](G:/YeManCC-Work/Testing/YMCC/Evidence/CapabilitySupport-VirtualController-20261025/gpd-family-hc-20261026/R11-INPUT-CONTRACT-SUPPORT.json)。库侧本次只回写支持合同，未做产品编辑、型号登记、编译、测试器执行或设备操作。
<!-- VC-CAPABILITY-R11-INPUT-CONTRACT:END -->


<!-- VC-CAPABILITY-R9-SHARED-CONSUMER:BEGIN -->
## R9 共用消费者软件证据（2026-10-26）

已核对修复负责人R9原结果与交接哈希：**family-integration 10场景/329检查 PASS**，GPD(G1618-05)、OneXPlayer(ONEXPLAYER X2)、Ayanero(AYANEO 3) × SD/Edge/Elite共9个独立实例，均经原实际Start、fresh frame、唯一ON final、实际OFF/受控managed子进程自然退出、唯一OFF final；实体SteamDeck禁止模拟SD的第10个负例通过。运行时deviceClass保持空，DMI来自本支持JSON的受控fixture叶子；验证的是可选OEM缺失/未知model下共用标准输入消费者，机型catalog resolver及真实硬件仍未验收。

OneX/AYA nativeForeground继续Unsupported，无MSI模式写权限；这些边界有实际断言。原OFF归还回归5场景/168检查通过；disable失败后只读status补强的40场景差分保持既有设备写入序列不变，真实veto actor和失败后现场状态仍未知。详细范围、原文件SHA、DMI来源与断言见 [R9软件消费者支持JSON](G:/YeManCC-Work/Testing/YMCC/Evidence/CapabilitySupport-VirtualController-20261025/gpd-family-hc-20261026/R9-SOFTWARE-CONSUMER-SUPPORT.json)；[R9交接](G:/YeManCC-Work/Testing/YMCC/Evidence/VC-Win5Return26-R9/DELIVERY.json) 保留旧失败与剩余项。

这是软件消费者证据，102条总库源记录及硬件通过数量保持原值。库侧本轮仅核验和回写合同，没有重复编译、运行消费者、出包、部署或操作硬件。
<!-- VC-CAPABILITY-R9-SHARED-CONSUMER:END -->


<!-- VC-CAPABILITY-GPD-HC-20261026:BEGIN -->
## Win5 拒绝事件：GPD 家族与 HC 源码对照（2026-10-26）

已核对 **21个GPD源类（20 factory）及共同控制器链，27个HC关键文件均匹配受控索引**。最新Win5 PID8632三次拒绝为实体XUSB节点disable的`CR_REMOVE_VETOED(23)`；同会话五次虚拟切换本地final成功。HC走USB `CyclePort`，YMCC当前GPD走同devnode `CM_Disable/Enable`，两者不能视为等价；0x4为`CM_DISABLE_UI_NOT_OK`，PERSIST为0x8。Win5背键固件门不应扩大成整机虚拟准入门。

[完整对照报告](G:/YeManCC-Work/Testing/YMCC/Evidence/CapabilitySupport-VirtualController-20261025/gpd-family-hc-20261026/GPD-HC-COMPARISON.md) · [逐类/结论JSON](G:/YeManCC-Work/Testing/YMCC/Evidence/CapabilitySupport-VirtualController-20261025/gpd-family-hc-20261026/GPD-HC-COMPARISON.json)。继续沿R9补失败后同节点只读状态，具体veto者及HC同现场效果仍未知。本轮仅支持文档与证据，未改运行、未部署、未操作设备、未跑夹具。
<!-- VC-CAPABILITY-GPD-HC-20261026:END -->


<!-- VC-CAPABILITY-2325:BEGIN -->
## 最新支持：2026-10-25 23:25 修正导出

已按本轮 G3E PID10124、ROG PID8316、Win5 PID8632 与创建身份排除旧会话。总库复查仍为 **92 冻结类＋10 审查扩展＝102 条源记录，18 条共享 OEM 消费关系**；逐机型内容、alias、OEM profile/keyTable 与上次相同。最新源码锚点已更新；这不是 102 款硬件通过。

| 本轮机器 | 库侧核对 | 保留边界 |
|---|---|---|
| G3E | 五个本地 final 成功；实际背键准备的 modelId／firmware1041／catalog 哈希匹配，M1/M2/sync 写回执成功 | process-cache-hit 的空字段不表示丢失机型；首次发现失败不允许补固件放行；映射物理效果未验 |
| ROG Xbox X | 五个本地 final 成功；实际 XInput 来源保留 | PID1B4C HID refresh 仍 deferred；本地成功不等于外部隔离 |
| Win5 | 五个本地 final 成功；实际 XInput 来源保留 | native refresh helper exit6 仍存在；不将 GPD 停启配方复制给 OneX／AYA |

**Host 身份纠正：** 清单 `hostExe/hostDll/hostSha256/hostDllSha256` 实际属于 `PowerControl/fan-host-v2/YeManFanHost`，其中 DLL 磁盘哈希为 `5c235b39…`，不能当虚拟 InputHost。已分别从三机当前会话的实际 `input-host-runtime-binary-identity` 提取四次 HELLO：各自观察到 InputHost DLL disk SHA `6574f4c0…`、loaded MVID `89696b43-14d3-43b6-88bf-7260eb5895f7`、SDK verified disk SHA `8fe7bdb2…`。三机一致来自 12 条各自 HELLO，仍不证明加载图像完整字节或外部游戏消费。

新暂停诊断要同时读 `pauseReasonMask` 与 `pauseReasonObservedMask`：未观测的位、null、历史累计 mask 均不能当当前 false；`cleanupEvidence` 在 ON final 中为空是原输出条件。字段与源码说明见支持 JSON 的 `repairDataContractResponse.diagnosticSemantics/hostIdentityRoles`。

本轮证据：`Testing/YMCC/Evidence/CapabilitySupport-VirtualController-20261025/new-export-2325`，含 ZIP／成员／原行哈希、实际 HELLO、机型对照和来源检查。已直接交给“虚拟手柄”修复任务；全家族物理验证与 OneX／AYA 缺口继续保留，未新增运行分配器或每帧中转。
<!-- VC-CAPABILITY-2325:END -->


全库覆盖补充（2026-10-25）：[102条源码/扩展逐行覆盖](CONTROLLER-MODEL-COVERAGE.md) · [机器可读映射](CONTROLLER-MODEL-COVERAGE.json)。原七个重点模型之外，现已列全92冻结类（80 factory）和10审查扩展；包含未重启首轮／重启后第二轮三机日志差异与G3E实际背键库消费回执，数量不表示修复完成。补充18条原生共享消费类关系，避免误把CPU子型合并判成漏接；R5软件交付仍有范围限制。联合目标仍 ACTIVE。

日期：2026-10-25，Asia/Hong_Kong。当前对接聊天：**虚拟手柄**。机器可读字段、取样源码哈希和锚点见 [CONTROLLER-REPAIR-SUPPORT.json](CONTROLLER-REPAIR-SUPPORT.json)。

本页供现有修复负责人定位、适配与交接；不增加运行分配器、硬件写入口、安装白名单或夹具。此轮只核对当前源码和已有回执，没有重跑历史测试、导出或操作设备。main.cpp 等文件正在并发修复，JSON中的hash是支持信息采样身份，不是新源码放行证书。

## 先区分四层证据

| 问题 | 总库提供什么 | 还必须到哪里确认 |
|---|---|---|
| 这台是什么机型 | canonical class、冻结来源、扩展身份与既有OEM目录选择 | `native/main.cpp::ymccResolveFamily`，核对实际DMI及已审查别名 |
| 实体手柄有没有产生合法输入 | 标准provider绑定、OEM配方与协议选择 | 实际VID/PID/usage/interface/container、来源标签、read sequence和当前代次 |
| 虚拟手柄是否启动成功 | 总库不提供驱动就绪判定 | 所选persona/profile的backend、SDK创建、端点到达及Host回执 |
| 实体手柄是否真的屏蔽 | 总库不提供外部可见性证明 | HidHide实际接口库存/配置读回、系统栈生效与Steam等外部观察 |

四层不能互相代替。`capabilityId`、已连接掩码、设备管理器中的一个驱动包、创建回执和屏蔽设置均只证明自己的阶段。

## 已有入口：修复沿原消费者走

| 范围 | 能力总库入口 | 生产消费者与约束 |
|---|---|---|
| 标准按键、摇杆、扳机 | `HcControllerInputCapabilityCatalog.h::bindStandardInput` | 目前只绑定`xinput`、`xinput-any-connected`、`msi-dinput`；XInput需连接mask。`inputHostFrameSubmissionSample`、`input_frame_submission.h`保留实时准入 |
| OEM背键、专用键 | `HcOemInputCapabilityBinding.h::InputBindingCache::bind` | `oemUnifiedInputBindingFor`复用原profile/catalog；接口、来源代次、按下/松开、撤销由native负责 |
| MSI背键配置 | `HcControllerConfigurationCapabilityCatalog.h::bindRear/consumeRear` | 仅既有MSI DInput准备路线；不属于GPD/OneX/AYA通用配置器，native键盘写入仍非自动启用 |
| 体感配方 | `HcMotionCapabilityCatalog.h` | 不可变矩阵/协议选择；实际传感器、配对、新鲜度和校准仍实时验证 |
| 手柄侧体感 | `HcControllerMotionCapabilityCatalog.h::ModelBindingCache` | 当前声明的Legion协议范围，不能当作Win5/OneX/AYA通用解码器 |
| 物理震动 | `HcControllerFeedbackCapabilityCatalog.h::bindPhysicalFeedback` | 按当前实体writer种类绑定；Host收到游戏输出、native转发、实际震动分开记录 |

不要每帧加载JSON或经本页转发。只缓存身份/协议等不可变选择，保留现有采集→规范帧→提交最短路线。

## 本轮重点：Win5

- native机型识别分支：厂商包含GPD、主板包含`G1618-05` → `HandheldCompanion.Devices.GPDWin5`，冻结总库ID为`hc.device.GPDWin5`。当前识别实际使用子串判断；这不是新增任意GPD产品的许可。
- 标准手柄与背键分开。`native_foreground_source_policy.h`的GPD标准设备候选为`045E:028E`，仍须真实root/container及非虚拟身份关联；不能凭这个通用VID/PID锁定某一个实体手柄。
- 背键为`2F24:0137`或legacy`2F24:0135`、UsagePage=`FF00`、Usage=`0001`。`gpdBackBind`与`gpdBackParse`是原消费者；这不是USBIP虚拟端点，也不是标准XInput主通道。
- `ReadFile`原始缓冲区含1字节Report ID。当前解析先偏移一次，body[7]为Gamepad、body[8]为L4、body[9]为R4，掩码`0x69`。不要把HC的payload字节号直接套到未去Report ID的缓冲区。
- 键盘兼容表仍有`Ctrl+Win+O`、`Win+D`、`F13`、`F14`、`F15`。后两者解释为背键需Win5 profile、当前背键接口与代次门；当前producer权威覆盖及松开处理见`oemProducerAuthorityMask`等原消费者。双击/双事件应按HID与keyboard的实际来源回执查，不能全局屏蔽F14/F15或只按事件数量判重。
- 冻结HC的`IsHidSupported`阈值写作`Version(0x01,0x11)`；当前native的HID绑定本身并没有建立固件查询证明。不要把“命中背键接口”说成“已确认新固件/驱动”。

## OneX与AYA：不能整家族套用

`OneXPlayerX2/X2MiniPro/3`、`AYANEO3/AYANEONEXT2`是已审查native扩展，不在旧`06c0b954`冻结device表。`hc-motion-policy.json::reviewedExtensionClasses`另有记录，部分当前配方来自a250b19a审查扩展。`InputBindingCache`在总库model为空时仍保留原native选择器；**model为空不能被新修复误判为整条输入路线不可用**。也不能为消除空值，把这些机型冒充旧机型。

| 扩展class后缀 | 当前精确主板产品值（经native规范化） | 注意事项 |
|---|---|---|
| OneXPlayerX2 | ONEXPLAYER X2 | 不借用X1全部vendor协议 |
| OneXPlayerX2MiniPro | ONEXPLAYER X2MINI PRO | 专用键角色与X2不同 |
| OneXPlayer3 | ONEXPLAYER 3、ONEXPLAYER-3 | 不接受X2额外PID路径 |
| AYANEO3 | AYANEO 3、AB07 | 不直接套CEc/NEXT2键盘集 |
| AYANEONEXT2 | NEXT II、NEXT 2、AYANEO NEXT II、AYANEO NEXT 2、AB09 | 独立NEXT2 profile，不能继承旧NEXT |

OneX上述分支厂商精确值为`ONE-NETBOOK TECHNOLOGY CO., LTD.`或`ONE-NETBOOK`；AYA上述分支为`AYADEVICE`、`AYANEO`或`AYA`。其他AYA机型仍查看自己的原识别分支，不把此表当全家族型号清单。

OneX X2系列vendor采集入口为`onex_x2_protocol.h`、`onex_x2_session.h`及native原producer。`matchesCollection`限定`1A86:FE00`，X2/Mini另可使用`1A86:1305`，OneXPlayer3不继承该额外PID；Usage=`FF00:0001`。X2协议logical id `0x22=L4`、`0x23=R4`；其他型号/旧X1编号不能混用。`0x21/0x24`的Home/Keyboard角色随型号改变，沿`modelForClass/mapButton`消费。

`native_foreground_source_policy.h`当前只有MSI/ROG/GPD，OneX/AYA返回Unsupported。标准输入、OEM键和虚拟输出可以有自己的已有路线，但不能据此声称已具备相同的原生前台隔离。`oem_vendor_stack.h`也没有为OneX/GPD/AYA自动列出一整套官方服务/进程；空列表不证明官方软件缺席或屏蔽成功，不猜名称自动终止。

## 驱动与屏蔽：交给原修复负责人适配

`GamepadPrerequisites/Program.cs`明确把当前准备工作限定为UMDF包观察/准备，复合USBIP目标可用性另由选定backend负责。当前`InputHost/Program.cs::CreateTargetWithBackendWarning`在准备探测失败时保留warning，仍调用SDK创建；本说明不重新引入“探测不到就阻断全部persona”的旧总门。

- warning不等于SDK创建一定失败，SDK创建成功也不等于实体被屏蔽。
- 按选定persona/profile核对backend，保持ROG/MSI已有路线，不按Win5品牌造第二套安装器。
- 检查`gamepad_driver_setup.h`、InputHost创建/端点回执及HidHide实际接口库存。USBIP残留、UMDF包、虚拟端点、标准实体输入、背键vendor HID分别列证据。
- 不因同厂商/同USB parent就把整个复合设备全部隐藏/禁用；仍需实际接口角色、container、非虚拟来源与真实归还证据。
- 若修复需要新增总库字段，先明确“谁生产、哪个实际消费者读取、何时撤销”；不要把动态驱动就绪/硬件可用性写死在静态能力行。

## 对接方当前线索：switch成功不等于屏蔽成功

修复方最新进展报告“名单写入/读回成功、实体refresh helper失败，后续switch仍判success”。本轮没有重复解析其现场日志，以下仅是已经核对的当前源码语义：

- `native/main.cpp`的`hidhide.visibility-snapshot`把`physicalHiddenByHidHideMeaning`明确写为`hidhide-allowlist-membership-only`，`filterAttached`与`externalVisibility`仍为unknown。
- `native/virtual_switch_verification_runtime.h::finalLogLocked`把`physicalReturnVerified`、`gameInputVerified`写为false，`resultScope=local-switch-verification`。
- ON成功分支来自`currentOutputProvenLocked(view)`，理由为`current-intent-host-and-fresh-source-verified`，属于本地当前Host/输入/提交证据。

因此需由修复负责人继续区分：真实控制/归还结果是否误放行，还是上层把本地switch成功误读为实体屏蔽已生效。不能只见success就签署外部隔离；也不能为了修复隔离而把正常虚拟目标输出和同目标中性归还一起判无效。本支持任务不改这些执行判据。

## 已有结论与保留缺口

G3E：用户已于2026-10-21确认Steam背键方向正常；YMCC显示遵从总库正面位置，M1右→R5、M2左→L5。保留`controllerOemPresentation.ts`的按总库侧别转换，不按M1/M2序号全局交换生产映射。

R8：`Testing/YMCC/Evidence/ControllerCalibration-20261025-lifecycle-r8/COORDINATOR-ACCEPTANCE.json`记录范围内旧读重放和可控取消/回收已验软件路径，`fullApplicationExitAccepted=false`。`ReadProgress/sourceForNewRead`用实际规范API读取序号而不是按钮变化判断新读；同目标中性归还不可被源丢失阻塞。永久MM/COM/SDK/内核/Close阻塞、完整进程限时退出、真实设备归还和整机CPU/温度仍不能宣称完成；本轮没有重测或改签R8。

总库侧保留三项待适配信息：OneX/AYA原生前台隔离未实现；审查扩展不在旧冻结device表；驱动就绪属于运行态证据。此次不扩大这些范围，仅将已启用消费的正确入口交给修复负责人。

## 修复方回报可用的最小字段

1. canonical机型与实际来源，不用DMI代替外接手柄身份。
2. 实体VID/PID/usage/interface/container，以及排除虚拟源的结果。
3. inputSource、实际read sequence、power/topology/source代次。
4. 选中persona/profile、SDK/载荷身份、backend warning、SDK创建和端点到达各自结果。
5. HidHide/隔离目标范围及读回；外部可见性无观察时保留UNKNOWN。
6. 双事件对应的producer、HID/keyboard边沿、事件序号及归还情况。

以上字段用于复用已有取证，不新建逐帧日志、轮询或测试总门。按2026-10-23夹具成本规则选择已有最小检查；不要为了阅读支持说明重跑整轮。

## 修复方五类数据需求：逐字段答复

修复方 `latest-three-215x/CAPABILITY-DATA-REQUEST.json` 的五类、20 个字段已逐项回复在同名支持 JSON 的 `repairDataContractResponse.domains[].fieldResponses`。原请求及哈希保留于证据目录；答复只索引现有事实与运行字段，不新增分配器、writer、日志轮询或准入门。

| 请求域 | 总库提供 / 原消费入口 | 必须保留的未证明项 |
|---|---|---|
| canonical-source-binding | 102 条源记录、10 扩展、18 共享 OEM 消费关系；原 selected-source 接口库存 | `physicalIdentityCache` 仍为诊断缓存，`physicalIdentitySampleBindingProven=false`；不能拼成当前帧的物理设备证明 |
| current-input | `InputCaptureGamepadSnapshot` → `bindStandardInput` → `sourceForNewRead`；read sequence、时间及 source/power/topology 代次 | 按键不变和 XInput packet 不变不等于旧读；slot 不是根设备身份；同目标完整中性归还仍保留 |
| visibility-and-refresh | 原 `hidHideVirtualPhysicalTargets` / `nativeForeground` 策略与 MSI auxiliary 路线 | 角色库存不授权全 sibling 隐藏；OneX/AYA 缺专属刷新协议；ROG 1B4C HID 变更限制保留；名单读回不等于外部隔离 |
| return-expectation | 原 `Origin` 责任记录、ColdEnvironment、LaunchScope 与传输归还消费者 | 区分配置归还、端点消失、helper 结束、实体当前读数与外部送达；无 Host 不等于已归还；R5 没有完成首次 hide/强退闭环 |
| power-and-intent | 原帧 ticket、wake Work、kernel eventFileTime 与 `pausedLocked` | 焦点中性、录制、真实睡眠分别阅读；旧请求超时不由后来唤醒改签成功；磁盘 SHA 不是加载字节证明 |

同时核对保留 `inputHostSteamDeckSelfOutputBlocked` / `virtualTargetPolicy::rejectionReason`：被识别的物理 Steam Deck 不再模拟 steamdeck persona。其他掌机及其他 persona 沿用原准入；可选背键/体感缺失不阻断标准虚拟手柄。


### 复用已有读取归属，避免重复发现

支持 JSON 的 `repairDataContractResponse.readOwnerTokenReuse` 已补充原读取归属入口：`inputCaptureStampFeedbackSourceLocked` 给同一采样携带 `feedbackSource`、可选 `axisCalibrationDeviceKey` 和连续性代次。对应的反馈／校准消费者复用现有上下文，不需要为读取本库另加设备扫描。

`SourceToken.ready()` 只表示后端类型存在；`identityRefresh::Proof` 的旧兼容分支可能 `valid=true/legacyFallback=true`，仍没有设备路径证明。XInput 校准键只在已有发现给出 physical 分类与非空路径时存在；MSI 键来自当前主源接口。缺键保持空，不能按 slot／名字补造。

这些可选 token／校准键不成为标准虚拟输入的新准入条件，也不能代替实体接口归属、隐藏名单或外部消费证据。现有日志中的 `physicalIdentitySampleBindingProven=false` 保持原证据含义。
