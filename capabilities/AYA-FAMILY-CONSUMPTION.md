# AYA家族能力总库与AIR1S现场反馈

本表由现有能力总库、固定HC源码和用户2026年11月6日20:08:48日志逐项对照。实际机器为AYANEO AIR 1S / Ryzen 7 7840U，精确类AYANEOAIR1S已经接入。当前已核范围没有发现该机型的能力位、CEc协议或电池表绑定错误，不为旧包错误创建新的AYA写入器。

本批修正库侧过时的统一“hc-compat-callback”摘要：原注册表的EvidenceMode继续保留；当前继承EC writer、profile调用和虚拟Open/Close各自能提供什么证据，另按源码条件记录。查找仍从HcDeviceCapabilityCatalog.Bind("fan", FanRoutes)进入，执行和实时准入留在原Host。机器列表是来源与消费导航，不是第二份运行时注册表。

## 家族差异

| 来源分组 | 实际接入与边界 |
| --- | --- |
| CEc：2/2S、AIR/1S/Lite/Pro、Flip KB/1S KB、Flip DS/1S DS、KUN | 原GenericDuty/GenericEcControl。仅当control/duty/profile均继承IDevice、同一owned transport/metadata当前有效时，使用现有InheritedEcFanWriter。 |
| CEii：AIRPlus AMD/Mendocino/Intel、Slide | 原AyanEOCEiiModeRegister及vendor setter，不进通用继承writer。Mendocino/Intel低字节duty0x02与其它分支0x04分开。 |
| 6个既有扩展：3、NEXT2、AIR2、AIRPro1S、Flip11、AIRPlusNxx | 已有PinnedFanSourceCatalog消费；精确来源/寄存器按各自描述，不从AIR1S继承。 |
| 2021/Pro、NEXT/Lite/Lite4500U | 本题风扇/灯光/充电三域未声明，不按AYA家族名补空注册。 |

固定根24类中20个factory，加6个既有逐域扩展，共26个factory来源。静态源码中fan21、lighting15、battery21；这些是绑定数，不是实机通过数。Flip DS两型的灯光被源码明确撤销；AIR2/AIRPro1S/Flip11/AIRPlusNxx的灯光有声明但缺精确当前消费者，继续记录缺口，不新增无法消费的条目。

## AIR1S现场能证明的事实

最新YMCC PID7676、Host PID6312存在初始化、profile/启用和5秒keepalive200记录。固定HC精确识别AIR 1S，OpenLib初始化返回true。历史会话中睡前/退出Close后出现release-unproven、hc-close-returned-physical-unknown；它们按原runId和时间保留，不能转移为最新会话故障或当前主线复现。

导出清单的Host DLL是5c235b39…、payload ee6c11e8…；清单只证明导出时磁盘文件，并非加载模块证据，也不与所有历史会话原子对应。当前Program已存在InheritedEcFanWriter、OpenInheritedEcFanDevice、CloseObservedInheritedEcFanDevice；是否已进入用户正在运行的Host须另绑定源码/成品/加载回执。

原oem.power-hook-unavailable仅表示固定HC没有观测hook，不能单独当作硬件失败或新的准入门。EC布尔值返回true只证明该传输返回成功，HTTP200、IsOpen、profile返回或transport清理不证明转速和物理OEM归还。

## 必须保留的生命周期合同

AIR1S→AIR→CEc的virtual Open先走原基类和RGB hold，virtual Close先RGB release再base.Close内SetFanControl(false)。当前观察器必须沿同一device/transport，不能跳过派生Open/Close，也不能另开驱动。Profile控制/占空比当前继承writer消费原ECRamDirectWriteByte布尔值并复核owner/epoch/metadata；false/unknown/superseded不能记为成功。

当前CloseObservedInheritedEcFanDevice仍调用原virtual Close；生命周期返回与transport retired不是一次新的可核验control=false收据。缺该次release证据仍按原release settlement保持unknown，不能因库侧更新摘要改成归还通过。CEii及厂商profile override不使用此通用分支。

风扇AI负责继续核对历史失败与现有消费实现、必要原夹具和同源正常组件入包；本能力库负责人只修本表/来源/交接，不代写Fan Host或新增旁路。

## 其它能力与验证边界

AIR1S陀螺仪已命中reviewed Windows配置，并有处理采样收据；日志仍标sampleFreshnessProven=false，不能称当前Host/游戏消费或物理轴向验收通过。日志没有充电/灯光运行事务，不能用默认设置或battery payload证明它们成功/失败。现有六能力位按名称保持；HC枚举252与总库重排枚举125属于不同枚举，不能数值直接相等比较。

机器合同与逐项来源见[AYA-FAMILY-CONSUMPTION.json](AYA-FAMILY-CONSUMPTION.json)，现场原件/SHA见该文件引用的HC098证据。此批只读核32份来源身份与108份ZIP文件SHA，未运行新夹具/构建/部署/硬件。全家族及本机实际归还效果未完成。


## 已证消费端记账缺口：消费端软件窄修已落实

更新 2026-11-07T01:51:09.178919+08:00。原Program `9e91c59ef18e0f271b71404900c139c304e9cd7722ce7c23f55d6b8f9445b6c4` 的RestoreOemCore catch丢弃异常释放verdict问题，已由原风扇消费端修复。当前Program `197f073350242a27e94a0a211151a3c6c81bc886f1af1f1dd3eda9db1c9d7e12` 沿原ClassifyReleaseAction写回现有action/evidence/writeEvidence，原throw、CEc virtual Close顺序与物理unknown保持。净差异仅此catch与两条内嵌旧断言同步；没有新增AYA协议、writer或旁路。

原fan.unified-capability-roundtrip两个定向case（legacy/initial-default-guard、asus/failure-cases）574检查通过；28份原输入身份与当前源码一致。纠正后的同一原消费者编译精确前像，实际在attempted-release-failure业务断言失败。该反例selectionVerified=false来自业务断言提前退出，不能把它报成完整选定集合通过；先前构建选源错误、字面量错误和FAIL原件均保留。原PackageSource25项通过。库侧未重复执行测试或构建，也未改执行账。

组件 `candidate-aya-release-accounting-20261107` 已选入主线：Host DLL `7bd62d3f9d1aaaa7ea2141b34873848449735a63cd2f933fa8af95080dfc03d7`；payload manifest `b42bf5dcaabbd880c1e1031e3fad136ae120e133b8cf3cba6e28001c6944c1e8`；source bundle `9413636f346b3ef3d2fe16363cd51973c967e2fb6369fb1ed05f291357dcee27`。库侧核9份payload、57份bundle文件及45份来源输入（41 Compile、2 project、2 Content）的当前/归档SHA。原verifier所称43源码包括41 Compile与2 project，不扩大为完整编译器依赖闭包。source bundle中的prepared-not-promoted是封存时的历史元数据，当前选择由实际主线配套及最终promotion/verifier回执证明，不篡改旧字段。

本项状态仅为“消费端软件窄修已落实、组件已选定”。历史旧Host Close失败根因继续UNKNOWN；最新AIR1S会话在此次改动前已有软件enable成功，不能把它转成新修复效果。整包导出、安装、真实睡眠/关闭与物理OEM归还未验收。promotion后并行native源码有差异已保留，本项不代签完整当前主线构建。具体消费回执见JSON中原AYA-HC098-RELEASE-EXCEPTION-ACCOUNTING项，初始待修观察和先前封存均保留。


## AYA controller and feedback repair (2026-11-07)

Controller task evidence is additive to the existing family/fan records. AYA short keyboard edges now reach the existing recorder and UI observations. NEXT 2 has four separately classified rear inputs; AYANEO 3 keeps an independent operation catalog pending physical rear attribution. Existing saved IDs and firmware protocols are preserved.

Software evidence: 2,999 native recorder checks, 35 AYA catalog/alias/extension browser cases, and 609 presentation checks. These are not distinct physical model acceptance counts or proof of configured action execution. The default full browser suite retains its earlier failed assertion. No hardware operations, new package, HC DLLs or deployment. Family goal remains active.

The immutable matrix and source-ready receipt are referenced by `controllerFeedbackRepair20261107` in the JSON. Pending items include configured-action consumption, AYANEO 3 rear attribution, existing lighting lifecycle, physical vibration and independent charging-indicator protocol/readback. This controller entry changes no fan consumer contract or selected fan component.


### 背键／专用键实际消费层补证（2026-11-07）

复用原测试生产器，真实规则评估、派发、串行取消、键盘 worker 与 UI mailbox 共 3969 检查通过，覆盖 35 个 AYA 类／别名／扩展入口的 147 个操作。只有“接纳成功”而不消费动作的独立副本产生 1470 个预期失败。来源与原件见 JSON 的 actionConsumerEvidence；实体来源、编辑器保存和马达响应仍未验收，目标 active。没有修改虚拟手柄控制链、风扇合同或机型准入。


### AYA 灯光事件恢复与充电灯边界（2026-11-07）

原 native 增加启动、拔电、解锁及 committed Ready 的灯光接续；仅恢复用户已开启的保存设置，复用原总库路由与 setLed，AYA 写入串行化，旧请求／机型／电源／退出重新核验。定向 19 场景／148 检查、原选择与 ROG／MSI 51 入口／339 检查通过，去掉 Ready 接续的反例被拒绝。真实主线 MSVC /Zs 语法检查通过，1560 个编译输入已绑定；完整成品链接／发布／硬件仍未验收。

HC 的充电／睡后变红注释只支持 RGB 恢复合同，不证明全家族充电指示灯共享电路，更不构成独立充电灯 setter/readback。没有新增该能力位或 HC DLL。完整五项目标继续 active，来源与原件见 JSON 的 lightingLifecycleRepairEvidence；原风扇合同与既有记录保留。


#### AYA3／NEXT2 灯光所有者独立接续补充

有效灯光请求现可接续自身 owner，不等待输入采集启动；旧子进程归还检查仍保留。适配器类型改用总库原公开 sourceAdapter 查询，避免把扩展 Route 默认字段当成真实适配器。最终 20 场景／157 检查与原 51 灯光入口／339 检查通过，缺 Ready、缺 owner 两个控制均被拒绝；完整原生 /Zs 再次通过。来源见 JSON 的 independentOwnerRearm。实机、独立充电灯与震感仍未验收，完整目标 active。


### AYA 手动震动回执与重复请求修复（2026-11-07）

真实回执 ok=false 现会在 AYA 界面显示阻塞并停止后段；重复点击、迟到回执和两段强度受同一测试请求约束。已受理不等于马达已响应。原 native 回执闭包 1311 检查、AYA 浏览器 58 检查、原默认浏览器 140 检查通过；旧界面 32 项断言被拒绝，vue-tsc 退出 0。原生后端／其他家族路线未改，实机震感与完整五项目标仍未完成，见 JSON 的 manualVibrationReceiptEvidence。


## 背键／专用键：真实编辑器保存到原生消费（2026-11-07）

本轮仅按键与震动范围续作；新增的是三个既有生产器的可选定向入口及证据，不新增 HC DLL、不修改虚拟输出／灯光／充电灯控制。

AIR 1S、NEXT 2、AYANEO 3 共 22 个目录操作，在 1280×720／800 保存 44 份真实编辑器规则，221 项浏览器检查与 440 项生产版原生解析／求值／派发／键盘 worker 检查通过。虚拟输出关闭。丢弃解析结果的合成负例编译成功并产生 132 项预期失败。目录操作数不等于实体键数。

[本轮报告](G:/YeManCC-Work/Testing/YMCC/Evidence/AyaFamily-20261107/editor-saved-actions/REPORT.zh-CN.md) 与 [原始证据回执](G:/YeManCC-Work/Testing/YMCC/Evidence/AyaFamily-20261107/editor-saved-actions/SOURCE-READY-RECEIPT.json)。浏览器存储仍为生产规则标准化与 CAS 判定的内存适配器，原生最终发键也是内存记录；真实磁盘事务、OEM 所有权副作用、实体源及游戏接收未验收。此前 actionConsumerEvidence 的“编辑器未覆盖”标记仍是原轮历史边界，本节补充本轮覆盖，不回写旧原件。

当前没有发现需追加修改的按键协议或保存解析 bug。已有短边沿修复与震动请求失败反馈继续保留；实际按键效果和马达震感仍需用户现场验证。失败调用、选择器问题和夹具编译失败均已保留，详见本轮 CORRECTIONS.json。目标 active。


## AYA 前置键盘来源观察与正常原生构建（2026-11-07）

复用 model-adaptation.log 的原有界生产者、去重、刷新与导出，为 AYA 当前 profile 的 F13–F24 增加普通低层事件、注入低层事件、Raw Input 三类首次来源观察；不采普通可打印字符，不把功能键原子当完整背键，不接纳注入事件，不切 OEM 模式。原始按键消费函数与前轮逐字节一致。

原日志生产器的可选 aya-keyboard-source：16 行为／37 来源接入检查通过；移除观察投递的反例编译成功并产生 6 个预期失败。冻结源码按原 build_native.bat 完整编译链接通过，主程序及四个既有配套产物均有 SHA；未构建前端／完整包、未启动产品／部署／操作设备。旧夹具结果没有冒充新一轮执行。

[本轮报告](G:/YeManCC-Work/Testing/YMCC/Evidence/AyaFamily-20261107/keyboard-source-witness/REPORT.zh-CN.md) · [来源与二进制回执](G:/YeManCC-Work/Testing/YMCC/Evidence/AyaFamily-20261107/keyboard-source-witness/SOURCE-READY-RECEIPT.json)。

官方 AIR 手册将摇杆灯环用于充电提示；AYANEO 2 图示另列顶边 Power indicator 与摇杆氛围灯。该发现要求灯位分型，不能据此增加独立充电灯 setter/readback 或外推 AIR1S／AYA3。独立控制仍 UNKNOWN，完整背键、专用键、灯光、震动、充电灯五项目标保持 active。

关于“退出 AYASpace 后映射保持”的前次直接断言已撤回到 UNKNOWN；官方硬件级键鼠／多布局资料没有直接证明退出后的行为。固件包仅静态检查，其说明操作没有执行。


## 当前 LightSetter 候选与充电灯边界（2026-11-07）

当前 FanLab/LightSetter 重新发布成功，24 个 Compile 源输入均与主线源码匹配。新 DLL/PDB 与现有 PowerControl/light-setter 产物不同；旧 payload 的 PDB 仍证明其 24 个源文档与当前源码匹配。新产物留在独立证据目录，未替换安装目录、未运行设置器。

AYANEO 3／NEXT 2 的 SuperJoy HID+CEC 路线和其它 AYA 的 HC/CEc 路线保持原边界；协议完成不等于物理灯效已观察。AIR 手册与 AYANEO 2 手册显示不同灯位语义，独立充电灯 setter/readback/owner 仍 UNKNOWN。AYASpace 3.2 安装器仅作静态 PE 检查，没有执行或从字符串猜协议。

[本轮灯光产物报告](G:/YeManCC-Work/Testing/YMCC/Evidence/AyaFamily-20261107/lighting-payload-current-v2/REPORT.zh-CN.md) · [来源回执](G:/YeManCC-Work/Testing/YMCC/Evidence/AyaFamily-20261107/lighting-payload-current-v2/SOURCE-READY-RECEIPT.json)。目标 active。


## 灯光回执 UI 诚实化（2026-11-07）

原生灯光回执明确带有 `physicalEffectVerified=false`，前端却只判断 `ok` 后显示“掌机灯光已应用”。现已改为：只有实体确认字段为 true 才显示已取得实体确认；协议成功但无实体证明显示请求完成、实体效果待手动确认。失败回执仍显示失败。

这次只改类型、反馈文字和灯光浏览器回归断言，没有改任何 AYA／ROG／MSI／通用灯光 writer、owner、EC/HID 或恢复逻辑。vue-tsc、能力自测、6 组灯光浏览器回归、140 项默认反馈回归和 58 项 AYA 反馈回归均通过。实体灯效仍需现场确认。

[回执报告](G:/YeManCC-Work/Testing/YMCC/Evidence/AyaFamily-20261107/lighting-feedback-truthful-v2/REPORT.zh-CN.md) · [原始证据](G:/YeManCC-Work/Testing/YMCC/Evidence/AyaFamily-20261107/lighting-feedback-truthful-v2/SOURCE-READY-RECEIPT.json)。
