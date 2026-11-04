# 壹号本陀螺仪适配核查

本文件是原机型能力总库的来源及实际消费审计，不是第二份运行时机型表。原登记和分配仍由 hc-motion-policy.json、HcMotionCapabilityCatalog 与原生消费者负责。

目标机DMI为 ONEXPLAYER X2Mini PRO，AMD Ryzen AI Max+395。对应 OneXPlayerX2MiniPro / ymcc.device.OneXPlayerX2MiniPro，不与旧MiniPro、OneXFly/F1Pro混用。最新HC固定为 f2e58468d9917189d4be7ccfc242385ce2c3035b，16份相关流程源码及14份机型资源已核Git blob。

本轮源码接入、实际离线消费与全系对账已完成；未把此结果扩大为安装版或物理验收。

## 本轮开发范围

用户明确允许“不等日志反馈，直接从HC源码测挖穿连通”。本轮继续原源码与真实离线消费者接通，不把新实机日志作为开发前置；物理恢复不作通过声明。

## 已修的生产缺口

HC WindowsSensorManager 会按传感器类型查找。YMCC原Provider仅枚举全类别，ERROR_NOT_FOUND后直接退出。本轮保留原完整类别路径，仅在未找到或空集合时补 gyro/accel 标准类型枚举，再交给原唯一源或明确来源对选择。权限、其他枚举错误、不完整请求类型结果、无效读数、退役代次均不制造可用运动输出。OneX不借用MSI私有字段布局。

原Provider实际poll→sample已通过Release/Checked各72项adapter和35项legacy core检查；原34项adapter断言保留，新增38项。旧header在同组扩展断言中如预期出现25项失败。样本、诊断、owner归还、停止迟到与下一代恢复均记录在 [采集回执](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/SensorTypeFallback/REPORT.md)。没有新增高频扫描或改动运行库。

## 型号、轴向与总库对账

冻结family共17类，加X2/X2MiniPro/3三个已审扩展，共20类。G1、Mini、X1为无factory基类；17个具体型号中16个最新矩阵语义一致。Apex最新AxisSwap为X:Y、Y:X、Z:Y，重复Y并缺Z，不可导入，保留当前gyro=y,-x,z、accel=-y,x,-z的原有效矩阵，不臆造上游修正值。

| 具体机型 | 当前路线 | gyro轴 | accel轴 | 最新HC对比 |
|---|---|---|---|---|
| OneXPlayer2 | Legacy | -x,-z,-y | x,-z,-y | 语义一致 |
| OneXPlayer2Pro | Legacy | -x,-z,-y | x,-z,-y | 语义一致 |
| OneXPlayer3 | Reviewed | -y,-z,x | -y,-z,x | 语义一致 |
| OneXPlayerApex | Legacy | 见逐类审计 | 见逐类审计 | 上游非双射，保留原有效矩阵 |
| OneXPlayerG1AMD | Reviewed | x,z,y | x,-z,y | 语义一致 |
| OneXPlayerG1Intel | Reviewed | x,z,y | x,-z,y | 语义一致 |
| OneXPlayerMiniAMD | Reviewed | x,-z,y | x,z,y | 语义一致 |
| OneXPlayerMiniIntel | Reviewed | z,-x,-y | x,-z,-y | 语义一致 |
| OneXPlayerMiniPro | Reviewed | x,z,y | x,-z,y | 语义一致 |
| OneXPlayerOneXFly | Legacy | x,-z,y | -x,-z,y | 语义一致 |
| OneXPlayerOneXFlyF1Pro | Legacy | x,-z,y | -x,-z,y | 语义一致 |
| OneXPlayerX1AMD | Legacy | x,-z,y | x,-z,-y | 语义一致 |
| OneXPlayerX1Intel | Legacy | x,-z,y | x,-z,-y | 语义一致 |
| OneXPlayerX1Mini | Reviewed | x,z,y | x,-z,y | 语义一致 |
| OneXPlayerX1Pro | Legacy | x,-z,y | x,-z,-y | 语义一致 |
| OneXPlayerX2 | Reviewed | z,x,-y | z,x,-y | 语义一致 |
| OneXPlayerX2MiniPro | Reviewed | y,-z,-x | -x,-z,y | 语义一致 |

每配置50条DMI样本均执行原准入；身份别名和DMI样本不是同一计数。639项来源/独立数学审计通过；详细继承、资源选择、源SHA、两配置实际观测见 [全系报告](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/FamilyAudit/FAMILY-AUDIT.zh-CN.md)。

## 执行证据与当前边界

- 原机型/矩阵/配对消费者：Release/Checked各114950项，非完整传感器线程。
- 原校准总线实际调用、GamepadMotion DLL及隔离文件API：51机型，两配置各4191项/1224场景；原来源移除、保存回执伪造负例均检出。
- 原native JSON提交到真实Host report：每配置7202行/223264项，12个原负例执行检出。外层整文件漂移失败保留；重组实际编译输入逐字节证明相关消费者仍当前，不把该外层结果改为PASS。
- 连续链已经执行：原Provider.poll（fake COM：类别0x80070490→标准类型成功）→实际ingress/raw sink→cache/配对/机型矩阵→真实GamepadMotion→实际动作/摇杆合并→当前typed serializer→真实Host解析及原始report。Release/Checked各native18项、Host4帧72项；5类断链各两配置均检出。开启且触发满足时原始右摇杆=(12000,11000)，动作关闭/触发释放=(0,0)，来源退役后摇杆及gyro全零。详见[连续回执](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/IngressToHost/CONTINUOUS-RECEIPT.md)。
- 连续例交付终端为内存wire，未运行完整inputHostSubmitPad管道/target生命周期、HostMain或实机。动作关闭保留有效raw gyro报告；来源退役才将两者清零。该区别已在真实Host中核实。
- 最终复核当前49个实际motion片段、全系源输入及连续链编译闭包一致；并行主文件位置变化和测试README/状态文档变化单列，旧固定偏移检查失败原件保留，不为此重复执行业务测试。

[机器可读审计](ONEX-MOTION-ADAPTATION.json)保存精确回执与来源；[HC→YMCC源码链](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/HC-TO-YMCC-SOURCE-CHAIN.md)仅为导航，不冒充执行结果。

## 已收到的现场事实

2026-11-02 16:39:04+08:00、YMCC0.0.38.0日志高频门已开，3348条完整采集样本中gyro/accel序号均为0；WinRT双0，SensorAPI类别枚举0x80070490。驱动包清单存在Bosch bmi160.inf，只证明包记录。mode=on为触发模式，动作开关及Windows/控制器直读分支分开解释。详见 [现场核查](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/FIELD-REPORT.md)。本轮未改安装版、驱动或固件。

<!-- FIELD-FOLLOWUP-205945 -->
## 2026-11-02 20:59:45+08:00 实机反馈

本次实机仍未恢复。PID16900在20:53:55真实执行了新增category→type路径；类别、gyro类型、accel类型均返回0x80070490，WinRT也为0/0。当前进程2693条完整sample的双sequence均0，没有处理回执。高频门已开。

该进程20:53:55—20:59:45的351条每秒状态均无Host，动作与persona保持关闭，未形成游戏输出验收。关闭动作不解释底层查找失败，采集初始化已实际执行。程序版本资源0.0.37.0不能单独推翻新增分支已执行的运行事实。

保留先前源码离线消费通过，但当前物理恢复调查重新OPEN；不能用fake类型成功情形代替这次两type都找不到的实机事实。未据此认定驱动缺失/损坏、硬件不存在或Xbox全屏干扰。新报告见[实机后续核查](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/FieldLog-205945/FIELD-FOLLOWUP.json)。
归档设备证据已复核：Bosch BMI160仅为早于本进程的驱动包记录，未记录传感器在线节点、实际INF/Service绑定及ProblemCode，物理根因仍未确定。
<!-- /FIELD-FOLLOWUP-205945 -->

<!-- DEVICE-DIAGNOSTICS-20261102 -->
## 实机采集缺口的源码修补

普通日志导出已新增 windows-sensor-devices.json：只读采集 Sensor 类设备节点、父链、问题码、实际绑定INF/Service与服务查询结果，1500ms预算、256KiB上限；接口枚举明确未采集。没有增加采样/后台轮询、权限请求或驱动/服务控制，原缓存查询不枚举合同保持。

原诊断入口 Release/Checked 各418项通过，包含生产collector与实际导出/打包片段的注入边界验证；真实WindowsOps编译链接但未调用，完整应用构建/安装/实机仍未运行。[交付及边界](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/DeviceDiagnostics/DELIVERY.json)。初始抽取锚点和编译失败原件保留。

另收到LGO 21:38:40日志：其WinRT回调增长，所选控制器IMU记录间歇零值，与X2无传感器候选不是同一故障层；不混用两家能力策略。[陀螺仪对比](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/FieldLog-LGO-213840/GYRO-COMPARISON.json)；[LGO风扇路线审计](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/FieldLog-LGO-213840/FAN-FAILURE-REVIEW.json)仅为问题定位，未改风扇。
<!-- /DEVICE-DIAGNOSTICS-20261102 -->

<!-- DEVICE-CHANGE-RECOVERY-20261102 -->
## 设备变化后的缺失来源恢复

已复用原STA补上ONEXPLAYER缺失Windows来源的设备变化重发现，弱owner/绑定/电源代次提示、250ms合并、每次至少间隔2s；健康/显式来源、未归还Provider与原唤醒重试不被替换。没有第二个轮询器，也不放宽样本或输出准入。原Resubscribe撤销失败漏paused标记的问题由原31项实际电源消费者检出并修复。

原诊断入口Release/Checked各617项（418旧项+199新增）通过；原电源31项及3负例按预期，整文件漂移FAIL与独立13函数当前闭包复核并存。正式C++20语法退出0但ADLX11头未预登记；后续原生隔离编译/链接退出0，main与任务栏头的并行漂移保留，完整当前源码身份/安装/实机仍不作PASS。[交付和完整边界](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/DeviceChangeRecovery/DELIVERY.json)。
<!-- /DEVICE-CHANGE-RECOVERY-20261102 -->

<!-- FIELD-234039-AND-FROZEN-CANDIDATE -->
## 23:40:39实机反馈与冻结原生候选

X2新日志PID7660、2207条完整sample双序号0，原typed分支执行后双0x80070490。没有新设备节点快照，不能验收后来诊断/恢复路径，实机仍OPEN。[新反馈](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/FieldLog-234039/FIELD-FOLLOWUP.json)。

后续已固定611文件快照并实际生产编译/链接主程序，550项实际仓库输入逐SHA不变，管理员manifest核对；候选未安装、不代表捕获后的全部主线改动。能力库有界来源/导出owner询问已发，等待原owner回复；原综合导出仍需消费当前源和完整载荷，不另行发布候选。[候选交接](G:/YeManCC-Work/Testing/YMCC/Evidence/OneX-Gyro-20261102/DeliveryCandidate/HANDOFF.json)。
<!-- /FIELD-234039-AND-FROZEN-CANDIDATE -->
