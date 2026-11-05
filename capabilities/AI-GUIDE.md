# HC079：OXP3 WMI 最终准入与 OXP3／MiniPro 灯光取样锁已修，普通主线包已导出

更新 2026-11-05T07:41:05.273+08:00（香港时间）。本批 PARTIAL，明确方法级软件范围及实际普通包身份 ACCEPTED_SOFTWARE_SCOPE；全家族未完成。

精确 Intel OneXPlayer3 的 WMI 阈值写入在最终预读后复核原准入，原WMI组105断言通过，旧实现被shutdown零写入断言拒绝；不向APEX借用该协议，不启用旁路。OXP3／X2 Mini Pro 灯光共享owner在原短锁内取得一致状态/路径副本，最终匹配与Active发布同事务，真实未归还操作保留准确身份；原owner定向191检查通过，旧函数被锁及债务身份断言拒绝。旧切片缺符号、整数HANDLE适配编译失败及准备错误均保留，未冒称业务通过。

普通包0.0.38：`G:\YeManCC-Work\Deliverables\YMCC-Mainline-Quick-TestPackage-20261105-071507-hc079-oxp3-wmi-final-admission-ordinary\YeManCC.zip`，110513513 B，SHA256 `9db50648693f649c964b4d53c2e1151ec1680b105848520e9c45067583550c5b`；617文件、114 DLL名，固定HC/共享运行库零变化。实际电池PE/PDB25源码文档、30灯光来源、1674声明构建来源（1672冻结+2原版本输出）及ZIP已核；声明来源不冒称完整编译闭包。新电池组件已采用回主线。Fan45来源不变，未改Fan业务或跑Fan夹具。

APEX灯光实机效果、0.12固件充电拒绝和全家族确切协议缺额仍开放；无新机型别名、寄存器替换、固件放门或EC8启用。整包夹具NOT_RUN、formalReleaseAccepted=false、未部署、无硬件/应用操作。结果 `G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\observations\HC-CODEX-079-ONEX-UPSTREAM-CONSUMER-AUDIT\FINAL-RESULT.json`；裁决 `G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\reviews\HC-CODEX-079-ONEX-WMI-AND-LIGHTING-OWNER.json`。本段为导出后导航更新，不改冻结快照或旧结论。以下历史保留。

---

# HC078：现有 EC 阈值写入补最终准入，已入普通主线包；APEX 充电与全家族继续

更新 2026-11-05T07:05:48.311+08:00（香港时间）。本批 PARTIAL，方法级软件验证及实际整包身份 ACCEPTED_SOFTWARE_SCOPE。EcProvider 在总线等待和最终预读之后、真正阈值写入之前复用现有准入；软件旁路停止/挂起归还原语义保留。原 OneX 组165断言通过，旧实现被 open/stop 断言预期拒绝；没有全包夹具或硬件验收。

当前普通包 0.0.38：`G:\YeManCC-Work\Deliverables\YMCC-Mainline-Quick-TestPackage-20261105-063606-hc078-ec-write-admission-ordinary-r2\YeManCC.zip`，SHA256 `195deecf90210f059aaa7d39f6eca93e053947c70be539d355c2cc3f48e6764d`，110512898 B；617文件、114 DLL名，无固定HC/共享运行库差异。新电池PE/PDB25份源码文档已核对，实际组件已同步主线。Fan45份现选来源不变，未重编Fan、未跑Fan夹具。

APEX HIDv2灯光单色/彩虹/亮度/关闭仍在包中，等待新实机日志；当前0.12固件充电拒绝未放松，没有启用EC8或替换寄存器。Super X已消费风扇/充电从整机未映射摘要中排除，灯光与其它型号逐域缺口仍开放。原首次总库stale前门FAIL及旧码反例保留。

结果：`G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\observations\HC-CODEX-078-ONEX-REMAINING-SOURCE-AUDIT\FINAL-RESULT.json`；裁决：`G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\reviews\HC-CODEX-078-ONEX-EC-FINAL-WRITE-ADMISSION.json`。本段为导出后导航更新，不改冻结源码快照或旧结果。以下历史保留。

---

<!-- BATTERY-CAPABILITY-AUDIT-20261105 -->
> **2026-11-05 充电域现势：** [全部充电能力复核](BATTERY-CAPABILITY-AUDIT.md)及[逐类来源/条件/现场证据](BATTERY-CAPABILITY-AUDIT.json)已进入本唯一能力总库。当前 42 组身份、12 条协议；新 HC 90 个可识别类中 41 个充电候选、40 个有 YMCC 路线，另补 GPD Mini 与 Super X；SuiPlay Windows 未开放。OXP3 WMI、Super X 75–100% 双传输已存在于源码，不沿用旧“未适配”结论。最新 02:33 用户包名虽为 onexplayer3，DMI 上报 APEX，已明确 BIOS/EC=0.12；新会话 2 次查询因 EC 主版本 0 不满足继承 X1 的 >=1 条件而在 provider 打开前拒绝。旧包 12 次查询及当时缺版本的证据保留；Linux Apex ACPI8 0xE5 与 HC X1 EC16 0x04A3 仍不可互推，此实机失败保持 open。ROG/MSI 按用户反馈保留，不扩大为全型号实测。审计表不参与运行准入；仍由原 battery policy → routes → 生成总库 → 原 Controller/provider 消费。
<!-- /BATTERY-CAPABILITY-AUDIT-20261105 -->

# HC067：OneX 消费事实修正已入当前普通主线包，全家族继续

更新 2026-11-04T22:35:31.132345+08:00（香港时间）。本批 **ACCEPTED_SOFTWARE_SCOPE**；原全家族目标 active/未完成。

G1 基类/AMD/Intel、OneXFly/F1Pro、X1Mini 共6条既有彩虹目录事实，按实际 writer 标为固定速度；5个具体型号+1个来源基类，没有改 frozen 声明位或新增型号/路线。原浏览器12场景在720/800精确通过，选择负例4项与来源策略坏例4项检出。首轮SVG加载准备失败为0场景，修原loader后仅续作同12项；旧失败/费用保留，未跑无关生命周期/整组。

17个具体型号的34项体感/标准输入描述已注明真实WinRT/SensorAPI/XInput消费者，9 Reviewed/8 Legacy优先owner保持；实际端点、样本、提交与物理验收仍UNKNOWN，不新增OneX控制器HID IMU。

普通包 `G:\YeManCC-Work\Deliverables\YMCC-Mainline-Quick-TestPackage-20261104-221226\YeManCC.zip`，0.0.38，110454408 B，SHA256 `ff14e9e24fd842279de00cdee8ab73b5e9e4c70ef83cc3f85d370674ccdd8fd8`。一次原ordinary自然exit0，独立核616实际文件、1645声明来源（1643冻结字节+2原版本生成输出）、90固定HC/模块、114 DLL名称，Fan45来源/57归档。Fan只重编灯光目录摘要常量配套组件，未重跑旧Fan业务；原灯光Core924576…复用，实际helper/PDB来源与载荷核对。当前同源电池及灯光成品已机械采用回主线，不再次编译；电池原生成器当前实际29输入包含native/power_source_state.h，不能复用旧28输入假设。普通fixtureValidation=NOT_RUN、formalReleaseAccepted=false，无部署/硬件操作。

新Linux primary493f34ea补证Air/新X1Mini Pro的board vendor/name谓词，Windows HID caps/report映射仍未知；Linux8位EC与Windows16位SIO不互推。SuperX充电固定LpcIO模块/API已核，实际Windows读UMA04A3、写SIO04A3且范围75..100；公共void写包装丢弃失败回执，尚需checked双传输消费者。没有启用该充电路线。下一有界实现 `G:\YeManCC-Work\Testing\YMCC\Evidence\HC-Code-Capability-Upgrade-20261031\Codex\HC-CODEX-067-ONEX-SUPERX-CHARGE\NEXT-BOUNDED-CONSUMER-PLAN.json`，Codex执行，Trae/旧goal/定时任务不恢复。

结果 `G:\YeManCC-Work\Testing\YMCC\Evidence\HC-Code-Capability-Upgrade-20261031\Codex\HC-CODEX-067-ONEX-SUPERX-CHARGE\FINAL-RESULT.json`；裁决 `G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\reviews\HC-CODEX-067-ONEX-CONSUMER-SPEED-AND-COVERAGE.json`。以下历史保留，不将局部通过标为全家族完成。

---

# HC066 OXP3 电池 WMI 清理修正已入普通包，本轮收口

更新 2026-11-04T21:19:24.292022+08:00（香港时间）。本批 **ACCEPTED_SOFTWARE_SCOPE**；全家族仍 active/未完成，本轮不再分配新批次。

既有 OXP3 selector 在歧义、空对象、重复引用和枚举失败时显式执行具体 ManagementObject.Dispose；原精确 1 组含新 hidden-Dispose 边界通过，保留旧拒绝/清理断言。实际 WMI 关闭拒绝、硬件与性能仍 UNKNOWN/NOT_RUN。

普通包 `G:\YeManCC-Work\Deliverables\YMCC-Mainline-Quick-TestPackage-20261104-210648\YeManCC.zip`（0.0.38，110435764 B），SHA256 `80c777a5b709054c98e95d33d8d54fe69aa5c5521e7edce9aac7fa72383daf6f`。已独立核 616 实际文件、1644 声明构建来源、90 固定 HC/模块、114 DLL 名称及实际电池载荷来源。来源是 20:57 已冻结且总库一致的主线批次，加原生成器精确重建的两份电池绑定；后续并行原生电池读取/Providers/测试改动保留在当前主线，未宣称入本包，也未用本包旧来源电池 DLL 覆盖它。Fan45 来源组件与既有灯光核心复用，没有重跑其业务验证。普通夹具 fixtureValidation=NOT_RUN、formalReleaseAccepted=false，无部署/硬件操作。

两次失败原件与费用保留；首轮来源归因已由 FIRST-FAILURE-ATTRIBUTION-ERRATUM.json 精确校正：native/battery_charge_client.h 在生成绑定后更新，复原旧头正好得到原9b457431…标识；Providers/InProcess 属更晚并行版本。后续版本要按原工具依赖顺序同步总库/消费者/电池身份，见 Codex/observations/HC-CODEX-066-LATER-SOURCE-IDENTITY.json；不恢复 Trae、goal 或定时任务。

结果 `G:\YeManCC-Work\Testing\YMCC\Evidence\HC-Code-Capability-Upgrade-20261031\Codex\HC-CODEX-066-BATTERY-WMI-CLEANUP\FINAL-RESULT.json`；裁决 `G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\reviews\HC-CODEX-066-OXP3-BATTERY-WMI-CLEANUP.json`。Air/新 X1Mini Pro 的 Windows 端点身份/报告长度及消费缺口继续保留，未把局部交付标为全家族完成。下文历史原样保留。

---

<!-- HC065-BANKED-WMI-CURRENT -->
**2026-11-04 X2/OneXPlayer3 风扇WMI释放修正已入普通主线包，全家族继续。** 实际公开Dispose、保留原端点/枚举器与代次责任经原精确6项/124检查通过；Fan组件45来源/43编译输入已采用，未为本次发布重跑风扇用例或构建。原发布的设置基线失败经审查已有可选字段的补缺合同解决：默认值快照只加false，原门未改，旧开/关及未知值保留的3项实际内存消费者通过。一次新source epoch普通publisher自然exit0；旧失败原样保留。包 `G:\YeManCC-Work\Deliverables\YMCC-Mainline-Quick-TestPackage-20261104-202619\YeManCC.zip`，SHA256 `a5e2758118ea00335829d2fc2c79fb99caef16432dde334b3369fd5c7cd8c1db`；616实际文件/114 DLL名字/90固定HC与模块已核。1643编译输入中1641与初始冻结字节一致，另2份是原版本生成器对冻结0.0.38输入的精确输出，原manifest不改。完整结果 `G:\YeManCC-Work\Testing\YMCC\Evidence\HC-Code-Capability-Upgrade-20261031\Codex\HC-CODEX-065-ONEX-REMAINING-CONSUMERS\FINAL-RESULT.json`。普通导出仍NOT_RUN/formalReleaseAccepted=false，未部署或测硬件。Air/新MiniPro的Windows身份/report证据及OXP3充电WMI拒绝清理缺口继续处理，未声明全家族完成。
<!-- /HC065-BANKED-WMI-CURRENT -->

<!-- HC064-SUPERX-UMA-CURRENT -->
**2026-11-04 Super X 风扇当前批已入普通主线包，按用户要求收口停手。** 原 FanCapabilityCatalog 保留九条 HC 基线及原匹配，新增独立来源 Super X，仅消费风扇。真实 Host 曲线、原 source session、UMAInterface 原始 EC16 地址、唯一端点、严格字节与写后读回已接通；清理失败保留原资源/代次。原夹具18个精确身份、572条通过检查来自保留前10项和后8项续作，原失败保留，不宣称一次整组通过。来源绑定 Fan 组件 candidate-onex-superx-uma-20261104 已构建采用；45份来源、1640冻结编译输入、616包内实际文件已核对，90份固定HC/模块、114 DLL名字保持。包 `G:\YeManCC-Work\Deliverables\YMCC-Mainline-Quick-TestPackage-20261104-165414\YeManCC.zip`，SHA256 `0e4be09038d463714b75515e1ddd367e9a45764fd18a671af40000dd86a4d1bb`。来源事实见 `upstream/xmax-6d728795/SuperXFan.source-facts.json`；正式记录 G:\YeManCC-Work\Docs\Tasks\HC\Capability-Library-TraeCode-CN\Codex\reviews\HC-CODEX-064-ONEX-SUPERX-UMA-ORDINARY-EXPORT.json。普通包仍 fixtureValidation=NOT_RUN/formalReleaseAccepted=false；未部署，实机风扇效果与全家族未验收；未启用Super X充电/灯光/RPM，不再安排后继批。
<!-- /HC064-SUPERX-UMA-CURRENT -->

<!-- ONEX-FAMILY-BIDIRECTIONAL-20261104 -->
当前 ONEX 全家族正反核对见 [ONEX-FAMILY-COVERAGE.md](ONEX-FAMILY-COVERAGE.md) / [机器可读表](ONEX-FAMILY-COVERAGE.json)。固定20类/具体17类/21精确product标签与现有17条Fan路线口径不同；四项有界修正不等于全家族完成，当前goal active。
本批四修正已进入普通主线包 `YMCC-Mainline-Quick-TestPackage-20261104-122113`（ZIP SHA `ad4d3651…`），当前选定Fan `candidate-onex-receipt-20261104`：Host `1850065d…`、Backend `414cd82e…`，payload `b5a00517…`、source bundle `23832fda…`；以覆盖表中的完整SHA/实际包裁决为准。下文旧批次的“当前”文字仅适用于其历史epoch；硬件/全家族未验收。此导航状态是包冻结后补记，不为此另起构建。
<!-- /ONEX-FAMILY-BIDIRECTIONAL-20261104 -->

# 能力总库AI导航与初步检查说明

<!-- HC-CODEX-034-X2MINIPRO-ACTUAL-CONSUMER -->

**2026-11-02：X2MiniPro的新组件实机Open拒绝已回到现有消费链修正；最终R2已进入R16主线整包并有安装回执，业务夹具未运行、实机仍待复测。** 17:20（日志+08:00）确认加载a398/00bc后仍报`PINNED_FAN_EXISTING_OWNER`，后续重试停在`HOST_NOT_OPEN`；初始control实际字节未记录，不能断言就是Manual=1，也不能把031的源码/身份核对当17类运行通过。

当前选定`candidate-source-entry-20261102-r2`：Host `f0bbbe082157ae18de8f604191f2a7061a143dc3210fed7b4f61586e95dc75e5`，Backend `119b93b2ca3aaafb6a21b672f96893805f7cafecc6230fec729bfa10a3f12e9b`，payload清单`ed11d11f1c690f6b1303ee2128765dcd5b947e55c028dddc08b12d7da037f453`，source bundle `61ca5b7cc24d862dea055370eb3c3ff509bc95b73fd44af38a01a655eadfd8aa`。43份当前/归档源码及模块身份已核，组件Build/配对不等于业务fixture通过；旧1682/其中shipping25不转授。r1过渡组件ae166/70a62及原家族a398/00bc只保留各自历史。

已有source consumer按每个descriptor的automatic/manual合法值识别模式，不再从一个manual位推断另一进程独占；Nxx仍为0/A5，未知值继续拒绝。首写之前先完成guard/endpoint核验，再紧邻记债和Write；未发写取消只Dispose，已发/部分失败保留原归还债。前端失败Open的占位撤回仅限同意图/进程/session/epoch、未ack原Open、无power操作且source资源已安全退还，不授予写/lease/当前代。

仍只完成现有消费端：X2MiniPro为SuperIO16/255，旧MiniAMD100、旧MiniPro255及各家族精确合同保留；未消费的0470温度/duty reader不接线，原HWiNFO输入不变，无新增模型/provider/隐藏开关。新业务用例尚未执行；原fan300秒账以`HC-CODEX-021-LEGACY-FAN-PROFILE-RETURN/PROCESS-029-onex-family-current-audit.json`的297.839466999962秒为已定位原件，不重置、不借灯光。CPU1 R16已Build0/Package0并于2026-11-02T10:05:27.2684336Z自然结束；整包SHA `5866707e3fed5fd569cfbfb2c304c098ede783b9f34459340555578ae3d9cbaa`，621成员/10个fan文件逐SHA一致，本机11个安装文件字节一致且安装回执保留配置。9个前端当前源码与prebuild及final build清单一致；R16未留这些前端原始源码副本，不冒称原归档compiler证明。这四份后补源文档不倒写成旧Build已编译/ZIP已携带；现场初始control字节仍UNKNOWN、业务NOT_RUN、转速/温控/性能未验。证据：`G:/YeManCC-Work/Testing/YMCC/Evidence/HC-Code-Capability-Upgrade-20261031/Codex/HC-CODEX-034-X2MINIPRO-LIVE-CONSUMER-REFUSAL/`；Fan修复来源：`G:/YeManCC-Work/Testing/YMCC/Evidence/Fan-X2MiniPro-Failure-20261102-172409/`。

<!-- /HC-CODEX-034-X2MINIPRO-ACTUAL-CONSUMER -->

<!-- HC-CODEX-025-EXISTING-CONSUMERS-FIRST -->

**2026-11-02 用户最新要求：对不消费的不接入，任务很多，优先完成消费端。** 只推进已有真实页面/API/运行链的消费，以及必要初始化、停止归还、失败与换代依赖。总库/来源代码中的声明不自动触发新reader、writer或UI；source0470温度和duty寄存器遥测当前无生产调用，只记来源、不接线、不作为当前fan消费阻塞，现有HWiNFO共享内存曲线保持。决定见 `G:/YeManCC-Work/Docs/Tasks/HC/Capability-Library-TraeCode-CN/Codex/decisions/HC-CODEX-025-consumers-first.json`。

旧021守卫并非仍只覆盖9条OneX：当前实际源码已覆盖59条既有legacy callback路线（9＋50）。024原二进制1682项中包含436项legacy消费检查；raw入口6轨迹明确注入终端且未执行真实HC raw方法，不能当物理验收。42当前编译源pin与已选且已导出的组件一致，既有证据不重跑、不相加。旧2/G1四类void丢raw bool的静态审计未发现现有Compat消费者冒称物理成功，不为它新造结果writer或消费者。封存见 `HC-CODEX-025-ONEX-CONSUMPTION-AUDIT/LEGACY-PROFILE-CONSUMER-CHECKPOINT-SEAL.json`、`REPORT.md`。

024实际Build、Package及后源检查已成功（后源通过2026-11-02T04:43:26.489399Z），621个成品文件与fan-host-v2配套10文件核对通过；固定留档ZIP f4497a92…7931a。后续其它主线源码变化另记，不倒写这次成功，也不把旧收据作为后来活动源码的fresh证明。下一综合生产者由CPU1统一负责。原普通导出夹具/current SourceOnly/global parent仍NOT_RUN，原fan297.839466999962/300与灯光TIMEOUT不重置；全家族硬件/性能和完整已启用能力目标仍OPEN。当前证据入口为 `G:/YeManCC-Work/Testing/YMCC/Evidence/HC-Code-Capability-Upgrade-20261031/Codex/HC-CODEX-025-ONEX-CONSUMPTION-AUDIT/VERIFICATION.json`。

<!-- /HC-CODEX-025-EXISTING-CONSUMERS-FIRST -->


<!-- HC-CODEX-024-ONEX-FAMILY-SELECTED -->

**2026-11-02：Mini Pro、G1、旧 Mini AMD 的本轮修正已实际选入主线风扇组件。** 新组件 `candidate-onex-family-20261102-r1`，Host `a3984a2f…10471`、清单 `00bc9025…44d85`。G1按随包HC精确模块/类型/写者补全冷合同；旧MiniAMD沿实际C4/C5写者正规化到100，MiniIntel及旧MiniPro仍255，X2MiniPro独立SuperIO16/255保持。原锁定HC夹具1682项通过，含25项真实shipping08a内存合同及无硬件继承写者检查；这不代表实机、全17分支生命周期或完整总库通过。原风扇300秒同账累计297.839466999962秒，灯光原TIMEOUT不重置。

用户已明确要求合格修正直接主线导出，不能长期仅留私有候选。本批没有新增隐藏开关，原精确机型、写许可、lease及停止归还保护继续保留。普通整包的实际结果见 `G:/YeManCC-Work/Testing/YMCC/Evidence/HC-Code-Capability-Upgrade-20261031/Codex/HC-CODEX-024-ONEX-FAMILY-FAN/EXPORT/EXPORT-TERMINAL.json`；该文件只在实际Build→Package及后源/载荷检查终态后写入。整包默认夹具NOT_RUN；023的旧25项PackageSource不移借给新载荷。首次完整构建已因并行ButtonMappingView源码变化被原身份门拒绝，失败保留；新快照需重新构建。固定104、冻结1048及原runtime123保持。全部ONEXPLAYER风扇继续优先，原完整库目标OPEN。

<!-- /HC-CODEX-024-ONEX-FAMILY-SELECTED -->


<!-- HC-CODEX-020-ONEX-APPLIED-CURRENT -->

**2026-11-02：九条既有OneXAcpi风扇路线补上无软件控制时的默认回调守卫。** 复用总库原strategy，不新增型号表或transport；回调进入前记归还责任，异常/未知归还、旧设备实例及清理未结算时保留。原HC虚拟Close仍执行，关闭和真实队列换代后的迟到回调不会制造责任。修复前原夹具已检出A1初始Hardware多余回调；当前合并源码原SourceOnly1276、锁定HC变体1299通过，其中各含新增78项软件消费检查，不跨范围相加。共享STA诊断并行改动已保留，并对合并后的当前源码重新编译/执行及核对完整PDB身份。

这是外层软件消费修复；冻结真实A1 EC正路径未运行，冻结Close内部逻辑未替换，硬件/性能/安装和全方法等价仍不作通过。固定104、冻结1048和原运行库123保持；原风扇300秒同账约189.89秒，灯光240秒耗尽状态不重置。见[HC-CODEX-020执行记录](G:/YeManCC-Work/Docs/Tasks/HC/Capability-Library-TraeCode-CN/Codex/executions/HC-CODEX-020.json)。完整102类/485方法目标继续OPEN。

<!-- /HC-CODEX-020-ONEX-APPLIED-CURRENT -->


<!-- HC-CODEX-019-METHOD-CURRENT -->

**2026-11-02 方法对账更新：** 固定源码的59个风扇声明已逐项与冻结Roslyn方法身份核对：31个声明相同、16个变化、12个无同签名项。13个控制方法的应用标志仍需与实际profile、Close及代次链核对；源码相同不代表该方法已执行。MSI已有结构化152/212与双块表回执，不能用上游无检查写法替换。见[方法级来源记录](G:/YeManCC-Work/Testing/YMCC/Evidence/HC-Code-Capability-Upgrade-20261031/Codex/HC-CODEX-019-METHOD-CONSUMPTION/FAN-METHOD-RECONCILIATION.json)。

灯光旧待办已更正：AYANEO3/NEXT2的native owner、固定CEc组合和typed总库准入已在007–009实现。原完整托管TIMEOUT、助手总库/生成检查NOT_RUN及UI仅源码等价仍保留，240秒预算未重置。当前没有新增夹具执行或完整验收结论；102类/485方法及全库目标继续OPEN。

<!-- /HC-CODEX-019-METHOD-CURRENT -->


更新：2026-11-02；原统计快照为2026-10-20，Asia/Hong_Kong。本指南整理现有入口，不新增运行时目录、分配器或验收门。机器可读索引：[AI-INDEX.json](AI-INDEX.json)；任务关系与数量口径：[OVERVIEW.md](OVERVIEW.md)。

当前虚拟手柄修复支持：[适配说明](CONTROLLER-REPAIR-SUPPORT.md) · [机型、路线与来源快照](CONTROLLER-REPAIR-SUPPORT.json) · [全部102条源码/扩展覆盖](CONTROLLER-MODEL-COVERAGE.md)。覆盖 Win5、OneX、AYA 的已有入口及驱动／屏蔽证据边界；只供维护检索，不是新的运行分配表或验收证书。

2026-11-05 OneX 背键／专用键消费核查：[现场 APEX 与最新 OP3 分支、已修漏项、实机边界](ONEX-OEM-CONSUMPTION-20261105.md) · [机器可读交接](ONEX-OEM-CONSUMPTION-20261105.json)。用户包名不覆盖 DMI；APEX 已有 HID 背键须进原总库目录，OP3 Home／G-Key 保留真实按下与松开。 用户已按 Keyboard／前端键而 UI 未识别：旧日志已找到 0～16ms 组合；OneX 短边沿已接录制与独立观察反馈，常规导出查 onex-oem-input-edge；02:33新日志确认背键可识别。三颗实体键与六个固件操作现用physicalKeyIds分开，组合／长按保留，计数3/3待新包确认。 灯光串口拒绝已补同次DeviceId/VID/PID普通日志，原准入不变；实体震动原76/381两模式通过，完整游戏链仍受旧native夹具上下文阻塞，续作见OP3Light1105/INTAKE.json。 后续计数/日志核查见同一报告：内置APEX操作自检6→8，OneX队列日志补齐Turbo/组合/长按/Home/G-Key并加家族边界；30项UI与8次原生执行通过，原诊断工具门BLOCKED单列，补充原生737检查及旧源84项反例不冒充注册PASS；原源码轮未打包；后续已复用正常结束的普通主线包，79份OneX运行源／617包文件身份通过，114 DLL名单不变。当前交付 G:\YeManCC-Work\Deliverables\YMCC-OneX-CountTrace-TestPackage-20261105-070219\YeManCC.zip；新包实机四项仍待普通日志及用户观察，完整诊断工具BLOCKED不改判。

## 1. 最短阅读顺序

1. 先读[当前启用范围](ACTIVE-INTEGRATION-SCOPE.json)和[用户决定](USER-SCOPE-DECISIONS.json)，确认是已启用、只登记、暂缓还是明确排除。
2. 查[接入状态](INTEGRATION-STATUS.json)和[执行路线](EXECUTION-CHAINS.json)，按domain读取实际消费者、file/anchor、归还义务、问题编号和来源限定证书。
3. 用AI-INDEX.domains找到策略输入、手写绑定和消费合同。先定具体问题再读大JSON；不要每次加载整个历史记录。
4. 按锚点查看实际消费者。总库负责描述与选择，原native/Host/后端负责设备操作和生命周期；绑定成功不代表允许写入。
5. 选择既有检查入口。实际运行时不能通过读取本指南／索引／OVERVIEW增加每帧中转。

## 2. 问题到入口

| 要处理的问题 | 首先查哪里 | 下一步 |
|---|---|---|
| 机型识别、别名或继承冲突 | hc-device-catalog.json、HcDeviceCapabilityCatalog.cs | 追sourceBaseline与冻结输入；通过原生成器更新派生表 |
| 已登记但功能没效果 | EXECUTION-CHAINS的domain消费者 | 跟到真实writer/decoder、实时准入、停止归还，不能只补注册 |
| TDP／手动CCD／CPU电压偏移及CPU与功耗范围 | TDP-INTEGRATION.md、uxtu-tdp-policy.json、tdp执行域／performance资料域 | 原总库生成器/定向生成器更新既有消费者；应用请求范围不等于固件最大值；已启用手动CCD/CPU offset沿Settings原消费；旧启动/自动电压调度/GPU不自动启用 |
| 风扇或体感高频开销 | 对应手写绑定、缓存与实际消费函数 | 只缓存不可变选择；保留新鲜度、所有权、代次与同等处理量 |
| 手柄、背键、运动和反馈混淆 | CONTROLLER-CONSUMPTION及具体回执合同 | 外接实体来源与掌机DMI分开，原始观察/解码/写入/物理效果分开 |
| HC升级影响 | tools/hc_capability_maintenance.mjs、HC-UPGRADE-REGRESSION.md | 按受影响已启用消费选旧场景；保留故障原件与版本身份 |
| 只有方法声明或未启用副屏 | MODEL-METHOD-CONSUMPTION、ACTIVE-INTEGRATION-SCOPE | 只记录缺口，不将helper或元数据存在解释成执行授权 |
| 汇报数量 | OVERVIEW-COUNTS及其sourcePins | 分开92类/80factory、397声明、路线/规则/配方、24项软件验收 |

新版 HC 纯代码升级统一跟踪 [HC-UPSTREAM-UPGRADE.json](HC-UPSTREAM-UPGRADE.json)。HC 冻结源与运行库不替换，新机型与能力修正回到上表原总库条目和消费者；上游清单、候选、登记数量均不产生运行权限。2026-10-31 已固定 f2e58468（上游102源类）：OneXPlayer3 新版轴向与 Home/G-Key（含 0x25、真实按住/松开、canonical bit63）已经原协议、会话、应用、Windows、模型查询夹具验证。灯光四区/顺序/第二组/预设/brightness0 已由灯光夹具验证协议与动作；共享 vendor HID 的 owner 准入/代次/期限/停止归还已按 PARTIAL_OWNER_SOFTWARE_TESTED_LIVE_UNKNOWN 实现并由原 owner 函数与 helper lease/writer 夹具分别验证：命名交接受权记录、代次化撤销事件、等待不持输入锁、单一 owner 预约、撤销后不再写、超时留债且旧回执不能释放新 owner。终止后仍未 signaled 的 helper 会保留其准确进程句柄/代次 token；owner tick、下一次准入和 stop 结算必须持续留债，直到该句柄被观察到 signaled 后才结清一次，迟到或其它代次子进程不得释放新 owner。每次准入还携带唯一 admission 编号，同一未变化设备上的连续租约不会复用 token/event；旧 token 不能重入或释放新租约。请求期限与 owner 撤销通过 linked token 进入准入、设备打开、写入序列和 in-flight IO；生产父等待/终止/观察归还函数、完整 ymccLightSetterApply 父函数与回执接受门已直接执行：父函数用受控 inert 子进程覆盖接受回执、spawn 失败、超时后实际 signaled 归还、截断、读错误、读超时、无 JSON、非零退出、子进程 not-ok 与身份/总库/operation 不匹配；真实子进程句柄的未归还债务阻塞准入/写入/重绑，并在观察到归还后单次结清、重新准入；完整父函数还通过受控无 PROCESS_TERMINATE 句柄使 TerminateProcess 失败，保留实际未 signaled 子进程/token，writePending=false 时 tick/准入/写入/重绑拒绝，实际归还后单次结清、重新准入并拒绝旧 token。真实 Oxp3WindowsHidTransport 写入以“已发出”屏障建立 pending：直接拥有的 CancelIoEx 取消该 OS 写入并返回成功（记录 [0]），Program owner/deadline 取消完成 pending 写入并如实记录终态 CancelIoEx 结果（运行时已取消后为 [1168] ERROR_NOT_FOUND）、句柄清理与零后续普通写；deadline 取 1500ms，先于 2000ms transport/心跳边界，并自建该代次事件。完整父函数测试只读引用现有固定 device-core runtime（HandheldCompanion.dll 08a38660… 及其 runtime.json 身份），不创建合成 DLL 或复制运行库。软件范围仅剩真实 HID 端点；物理 LED/固件 UNKNOWN 独立；不得把输入 Session/Windows 夹具或分离的 owner/helper 夹具当作实机灯光所有权证明。充电及全域差异仍待办：OXP3 充电已按固定 f2e58468 的 X2 WMI EC 路线接入原 BatteryHost（root\WMI SuRwECRegInterface ReadECReg/WriteECReg，0x4A3 limit、0xA304 打包、独立读回、源范围 0..100 vs 产品 50..100），原 62 组控制、36 页案例与 6 组实时联动通过；生产 Oxp3WmiEc 适配器的方法名、打包参数、hex 响应/status、缺失/畸形结果与调用失败路径已在受控外部内存方法端子执行，WMI 枚举与真实 WMI/EC 未执行；0x4A4 bypass 仅源映射、产品 UI 不可表达，保持明确 gap；真实 WMI/EC 与物理截止仍 UNKNOWN。硬件/Steam/游戏结果保持 UNKNOWN。

Codex接手当前记录见 [HC-UPSTREAM-UPGRADE.json](HC-UPSTREAM-UPGRADE.json)：固定新版102类／485个方法声明已分类，运行根仍保持冻结92类／397方法与既有域扩展。AYANEO3/NEXT2的SuperJoy摇杆协议依赖和内存会话验证已建立，独立原文报文oracle、停止/取消/代次/单读者与重试边界分列；实际端点owner、CEc其他灯区和联合归还尚未接通，两个机型不会通过旧HC工厂回退获得灯光能力。原LightSetter148项、新依赖326项和实际helper总库567项分别记账，不合成全库验收。

SuperJoy Windows读写端子已在原夹具的受控异步命名管道完成16项OS层验证，覆盖ID0到64字节载荷、in-flight读取消、关闭／后继隔离及三次300ms期限；现势DLL/PDB与16份编译源码相符。真实HID Open/descriptor未执行，native owner、CEc其他灯区与联合归还仍open，不添加hidapi运行库、不授予路线准入。见 [HC-UPSTREAM-UPGRADE.json](HC-UPSTREAM-UPGRADE.json) 中HC-CODEX-005。

SuperJoy的native/C#跨进程合同已升级ABI2（1344字节），同时绑定父进程及指定子进程的PID/创建时间。原ABI1曾接纳异helper的真实软件FAIL保留；ABI2原夹具14组场景通过，异helper在I/O前拒绝；访问拒绝的终止负例保留实际进程句柄/令牌并阻止新租约，观察退出后仅结清一次。native真实入口在旧HC文件检查/建子进程前拒绝未接通机型。Header/读取者已验证，产品正向端点选择/生命周期连接与CEc其他灯区仍open；冻结CEc私有方法不能冒充新版可覆写方法。见 [HC-UPSTREAM-UPGRADE.json](HC-UPSTREAM-UPGRADE.json) 的HC-CODEX-006。

固定来源风扇当前见 HC-CODEX-018：九条 source descriptor 通过原 FanCapabilityCatalog 的 MatchPinnedFanSource/AcquirePinnedFanSource 进入实际 Host 的 PinnedSourceFanSelection、Consumer、Engine 与 Transports。bridge/fanApi 校验来源身份与 generation，fanSessionEvidence/native fan_response_contract 分列资源关闭与自动归还。原夹具完整正反链和实际序列化回执已验证，代码仍受原许可门控制；不能把 source model class 写成旧 runtime factory，也不能把它当全库/硬件/性能完成。

## 3. 运行绑定的引路表

| 范围 | 手写入口 | 可定位符号 | 真正执行及实时边界 |
|---|---|---|---|
| 机型/别名 | [HcDeviceCapabilityCatalog.cs](HcDeviceCapabilityCatalog.cs) | Find / Bind / CreateTypeCache | 实际消费者保留可用性、租约和写许可 |
| 风扇 | [FanCapabilityCatalog.cs](FanCapabilityCatalog.cs)、[PinnedFanSourceCatalog.cs](PinnedFanSourceCatalog.cs) | ResolveAsusCurve / AcquireAsusCurve / MatchPinnedFanSource / AcquirePinnedFanSource | FanLab/real-host/Program.cs及PinnedSourceFanSelection/Consumer/Engine/Transports；source归还由fanSessionEvidence和native/fan_response_contract独立消费；旧HC合同见FAN-RETURN-CONSUMPTION |
| 体感 | [HcMotionCapabilityCatalog.h](HcMotionCapabilityCatalog.h) | bind / primaryKind / consume | native/main.cpp采样配对、新鲜度、坐标处理与最终发布 |
| OEM按键 | [HcOemInputCapabilityBinding.h](HcOemInputCapabilityBinding.h) | InputBindingCache::bind | native/main.cpp的hook/raw来源、key-up与释放 |
| 标准输入 | [HcControllerInputCapabilityCatalog.h](HcControllerInputCapabilityCatalog.h) | bindStandardInput | native/main.cpp -> InputHost/Program.cs；连接与最终提交仍实时验证 |
| 手柄体感 | [HcControllerMotionCapabilityCatalog.h](HcControllerMotionCapabilityCatalog.h) | bindModel / ModelBindingCache | native/main.cpp的Legion解码、lease/bank及校准 |
| 实体反馈 | [HcControllerFeedbackCapabilityCatalog.h](HcControllerFeedbackCapabilityCatalog.h) | bindPhysicalFeedback | native/main.cpp的实际writer、zero/stop、撤销与归还重试 |
| MSI背键配置 | [HcControllerConfigurationCapabilityCatalog.h](HcControllerConfigurationCapabilityCatalog.h) | bindRear / consumeRear | 保留精确型号/固件；native键盘路线只记录 |
| 灯光 | [HcLightingCapabilityCatalog.cs](HcLightingCapabilityCatalog.cs) | Require / IdentityMatches | native分流与FanLab/LightSetter实际写入、结束与清理 |
| 充电 | [HcBatteryCapabilityCatalog.cs](HcBatteryCapabilityCatalog.cs) | Bind / Resolve | src/views/SettingsView.vue 的 device 分区 → BatteryChargeSettings.vue → batteryCharge.ts → native/BatteryHost/Policy.cs、Controller.cs 的读回、冲突、恢复 |
| 未启用副屏 | [HcSecondaryDisplayCapabilityCatalog.cs](HcSecondaryDisplayCapabilityCatalog.cs) | 只读审查入口 | 先查范围；编译/登记不激活UI或设备写入 |

表中符号供检索。具体file/anchor维护在EXECUTION-CHAINS及AI-INDEX.domains，不把文档行号写成永久接口。新注释以AI-NAV标记定位；源文件已有的缓存和生命周期解释完整保留。

## 4. 文件按职责分列

物理路径保持现有编译与工具引用，按以下逻辑分组。AI-INDEX.files覆盖本目录全部文件，每个文件只属于一个主职责组；域映射可以交叉引用同一文件。数量快照是带日期的报告，不作为新增能力权威。

### 范围与用户决定

- [ACTIVE-INTEGRATION-SCOPE.json](ACTIVE-INTEGRATION-SCOPE.json)：本轮9类已启用能力与停止规则（含10月31日获准接回的现有TDP）。
- [USER-SCOPE-DECISIONS.json](USER-SCOPE-DECISIONS.json)：用户排除及性能/接线边界。

### 接入状态与执行路线

- [EXECUTION-CHAINS.json](EXECUTION-CHAINS.json)：实际消费者、锚点、生命周期、测试引用和路线问题。
- [INTEGRATION-STATUS.json](INTEGRATION-STATUS.json)：按9域维护当前状态与来源限定证书。

### 生成输入策略

- [hc-battery-policy.json](hc-battery-policy.json)：Reviewed generator input for battery。
- [hc-controller-configuration-policy.json](hc-controller-configuration-policy.json)：Reviewed generator input for controller-configuration。
- [hc-controller-feedback-policy.json](hc-controller-feedback-policy.json)：Reviewed generator input for controller-feedback。
- [hc-controller-input-policy.json](hc-controller-input-policy.json)：Reviewed generator input for controller-input。
- [hc-controller-motion-policy.json](hc-controller-motion-policy.json)：Reviewed generator input for controller-motion。
- [hc-lighting-policy.json](hc-lighting-policy.json)：Reviewed generator input for lighting。
- [hc-model-method-policy.json](hc-model-method-policy.json)：Reviewed generator input for model-method。
- [hc-motion-policy.json](hc-motion-policy.json)：Reviewed generator input for motion。

### 派生数据快照

- [MODEL-METHOD-CONSUMPTION.json](MODEL-METHOD-CONSUMPTION.json)：397条Devices方法声明分类；不是逐方法执行证明；生成入口 tools/hc_model_method_consumption.mjs。
- [hc-device-catalog.json](hc-device-catalog.json)：冻结机型事实、别名、声明与各域派生登记；生成入口 tools/hc_capability_catalog_generate.mjs。
- [hc-performance-metadata.json](hc-performance-metadata.json)：HC CPU/GPU来源资料；现有TDP通过同总库策略接回，见[TDP-INTEGRATION.md](TDP-INTEGRATION.md)；生成入口 tools/hc_capability_catalog_generate.mjs。

### 生成代码

- [HcBatteryCapabilityCatalog.generated.cs](HcBatteryCapabilityCatalog.generated.cs)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcControllerConfigurationCapabilityCatalog.generated.h](HcControllerConfigurationCapabilityCatalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcControllerFeedbackCapabilityCatalog.generated.h](HcControllerFeedbackCapabilityCatalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcControllerGameFeedbackCapabilityCatalog.generated.cs](HcControllerGameFeedbackCapabilityCatalog.generated.cs)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcControllerInputCapabilityCatalog.generated.h](HcControllerInputCapabilityCatalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcControllerMotionCapabilityCatalog.generated.h](HcControllerMotionCapabilityCatalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcDeviceCapabilityCatalog.generated.cs](HcDeviceCapabilityCatalog.generated.cs)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcLightingCapabilityCatalog.generated.cs](HcLightingCapabilityCatalog.generated.cs)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcMotionCapabilityCatalog.generated.h](HcMotionCapabilityCatalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [hc-device-catalog.generated.h](hc-device-catalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。

### 手写运行绑定

- [AsusFanCapabilitySession.cs](AsusFanCapabilitySession.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [FanCapabilityCatalog.cs](FanCapabilityCatalog.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcBatteryCapabilityCatalog.cs](HcBatteryCapabilityCatalog.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcControllerConfigurationCapabilityCatalog.h](HcControllerConfigurationCapabilityCatalog.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcControllerFeedbackCapabilityCatalog.h](HcControllerFeedbackCapabilityCatalog.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcControllerInputCapabilityCatalog.h](HcControllerInputCapabilityCatalog.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcControllerMotionCapabilityCatalog.h](HcControllerMotionCapabilityCatalog.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcDeviceCapabilityCatalog.cs](HcDeviceCapabilityCatalog.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcLightingCapabilityCatalog.cs](HcLightingCapabilityCatalog.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcMotionCapabilityCatalog.h](HcMotionCapabilityCatalog.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcOemInputCapabilityBinding.h](HcOemInputCapabilityBinding.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcSecondaryDisplayCapabilityCatalog.cs](HcSecondaryDisplayCapabilityCatalog.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。

### 消费与回执合同

- [CONTROLLER-CONSUMPTION.json](CONTROLLER-CONSUMPTION.json)：手柄输入/运动/配置/反馈/屏蔽的分阶段合同。
- [CONTROLLER-GAME-MOTOR-RECEIPT.json](CONTROLLER-GAME-MOTOR-RECEIPT.json)：游戏电机请求到物理writer的软件回执。
- [CONTROLLER-GAME-OUTPUT-OBSERVATION.json](CONTROLLER-GAME-OUTPUT-OBSERVATION.json)：Host原始游戏输出观察与归属。
- [CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json](CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json)：既有SteamDeck经典震动回执。
- [CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json](CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json)：既有Xbox/XInput震动回执。
- [FAN-RETURN-CONSUMPTION.json](FAN-RETURN-CONSUMPTION.json)：风扇归还、释放义务及历史/当前证据边界。
- [SECONDARY-DISPLAY-CONSUMPTION.json](SECONDARY-DISPLAY-CONSUMPTION.json)：未启用副屏的保留入口与禁止激活边界。

### 维护入口说明

- [AI-GUIDE.md](AI-GUIDE.md)：按维护问题定位文件、符号与检查方式。
- [AI-INDEX.json](AI-INDEX.json)：可检索的文件分组、域入口与工作流；不是运行时分配表。
- [OVERVIEW.md](OVERVIEW.md)：任务关系、类型数量和验收边界。
- [README.md](README.md)：总库入口和历史记录；先读顶部AI导航。

### 文档数量快照

- [OVERVIEW-COUNTS.json](OVERVIEW-COUNTS.json)：2026-10-20数量与来源哈希；数字更新需重读其sourcePins。

## 5. 生成与修改边界

- 手写绑定（runtime-binding）可以维护逻辑和注释；*.generated.cs、*.generated.h及派生catalog/performance元数据通过原生成器维护。
- MODEL-METHOD-CONSUMPTION由独立方法提取器导出registry.json；审查后再按原流程采用。它分类声明，不逐方法证明已执行。
- hc-*-policy.json是经审查的输入。改策略须同时检查实际消费者；生成成功不自动启用新功能。
- JSON保持合法JSON，不塞行内注释。AI解释放Markdown、手写C#/C++注释及文档索引，不污染数据schema。
- 总生成器可能更新native/main.cpp和灯光生成部分等目录外输出；先看tools/hc_capability_catalog_generate.mjs，不把整次生成视为“只改capabilities”。共享代码遵循原单写负责人。
- 本次整理不移动文件、不改签B40旧回执。新增注释会改变源码字节；保留原来源证据，下次适用性审计按实际输入处理。

## 6. 检查方式及其证明范围

以下命令从仓库根目录执行，OUT替换为源码树外的新证据目录。它们是现有可选工具入口，按任务选用，不要求每次说明修改都编译或全量复跑。

只读引用与已启用范围计划：

```text
node tools/hc_capability_maintenance.mjs --out OUT
node tools/hc_capability_maintenance.mjs --out OUT --candidate-catalog REVIEWED_CANDIDATE_JSON
```

这检查原消费者锚点、冻结输入、问题记录和候选差异，并生成计划；不运行实际产品功能，不加载HC。无candidate时也不证明升级后全部正常。

确需核对生成一致性时使用：

```text
node tools/hc_capability_catalog_generate.mjs --check --out OUT
node tools/hc_model_method_consumption.mjs --check --out OUT
pwsh -NoProfile -File tools/verify-unified-capability-catalog.ps1 -EvidenceDir OUT
```

这些入口会构建Roslyn提取工具／核对派生结果；不是零成本文件浏览，也不是硬件验收。最后一个调用既有生成检查和维护检查，不必把三条全都重复跑一遍。

真实消费回归使用EXECUTION-CHAINS中的原ID与tools/ymcc_sim/HC-UPGRADE-REGRESSION.md。索引中的existingTestReferences只是导航；未启用副屏、历史全局引用和未触发依赖门不因此被自动加入本轮执行计划。原runtime/ABI/profile变化门、UI导出门、Host启动与物理权限保持。

## 7. 本轮初步检查

整理前，既有hc_capability_maintenance只读检查PASS：9域、16条路线问题记录引用有效。整理后核对JSON、全文件分类、路径／锚点、README导航、注释前后原字节与生成文件不变，并重用同一维护工具做一次整理后引用检查。该检查说明导航和既有引用一致，不新增功能通过、当前整树通过、实机通过或CPU百分比。


充电入口维护（2026-10-22）：总库能匹配机型、独立组件测试通过，并不能证明父页面仍挂载消费组件。本次恢复 SettingsView 的设备分区，按活动分区挂载 BatteryChargeSettings。全页门必须包含 `settings/battery-charge`、`/settings/device`；`templates/batterySettingsCases.mjs` 执行真实父页→组件→bridge，删除父页组件在 `SETTINGS_CHARGE_ENTRY_MISSING` 失败。硬件协议仍由原 BatteryHost 管理，G3E 继续使用 Get_AP/Set_AP；不要为修复页面断线修改阈值编码。候选交付同时检查 battery-host.payload.json、Host --identity 与 native/battery_host_contract.h 的 sourceId，重建后核对全部载荷字节。UI模拟、原生传输模拟、候选文件与实机固件验收分开记录。
