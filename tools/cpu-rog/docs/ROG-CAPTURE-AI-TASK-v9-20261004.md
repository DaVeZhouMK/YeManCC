# ROG采集AI执行书 · v9 · 2026-10-04

## 0. 两台机器、三个角色，不能混算

- 本机：有源码，裁决AI审核；本机执行AI制作主线修复、构建候选和完整产品。
- ROG：无源码，采集AI只执行可信工具/载荷、核验运行事实、采集、回写。不修改YMCC产品源码，不自己裁决优化通过。
- 本机工具离线测试/本机mock/本机CPU观察，**均不是ROG工具回归、ROG产品回归或ROG CPU A/B**。已有ROG数据尚未形成被接受的完整回归/对比基线，不能重命名为新baseline。

用户等风扇、手柄/gyro软件结束后启动产品批次。本工具包可提前准备和做无产品副作用的验证；不因此启动新产品测试或冻结它们。

## 1. 搬运内容与路径

只复制本工具包的新子目录或其ZIP到ROG；**不要把整个旧CPU目录（含settings备份和历史数据）一并复制**。

工具使用自身目录定位脚本，不依赖G盘、DaVe用户名、本机路径、源码、Node或.NET SDK。Windows PowerShell 5.1可运行；PowerShell 7也可用。执行AI须读取相邻配置示例与脚本参数，不凭文书猜参数名。

主要入口：相邻`Invoke-ROG-CPU.ps1`。默认validate只查工具；preflight查目标/权限；observe采实际状态未知的观察窗；capture要求真实场景证据；status/start/stop/restart仅按授权管理精确可信YMCC。

产品不在此工具包内。正式baseline/candidate路径及完整身份由本机交回，ROG填入真实ExePath和ExpectedSha256；不能把自己从未知文件读到的hash当成已获信任的产品来源。只读发现可以登记实际路径/hash，不代表启动/替换授权。

## 2. 首次在ROG执行的顺序

1. **工具包验证**：校验manifest/文件身份；运行包内离线脚本验证，单独记录这是ROG上的工具回归，不是产品回归。失败不采正式数据，回写错误与exit。
2. **preflight**：记录真实计算机别名/CPU/逻辑处理器数、PS版本、权限与高完整性、精确YMCC root路径/hash/PID/创建时间、归属WebView2和Host。不要自动开启功能或修改settings。
3. **启停能力烟测（授权后）**：status→正常stop并确认owned子树收敛→唯一新root及前端ready→restart。实际失败如实回写，不根据“无UAC”或仅命令成功判通过，不误关另一实例。
4. **内部AI能力**：从当前可信产品验证实际AI入口和runtime readback。Fan有AI专用虚拟握手源码及本机完整主体mock证据，不能武断说YMCC完全不具备；旧安装产品/旧Host不支持时写PRODUCT_UPDATE_REQUIRED/CAPABILITY_UNVERIFIED。手柄/gyro按最终主线域内协议核验，配置ACK/旧flag不能代签运行。
5. **采集器真实烟测**：先短观察窗核对唯一进程总和、成员、读权限/缺失、原始CPU差/monotonic间隔、collector自耗、日志清单。标为tool/runtime smoke，不作为正式S0或优化通过。
6. 前面实际通过，才做下面正式矩阵。对unknown/unavailable可保留observe事实，但不得用observe冒充S0–S4、baseline或A/B通过。

## 3. 正式场景与A/B

| 场景 | 请求 | 必须证明的事实 |
| --- | --- | --- |
| S0 | Fan/虚拟手柄/gyro均off | 三功能off运行读回，不仅settings=false |
| S1 | Fan on，其余off | 当前owned Host握手、控制active与sequence |
| S2 | 手柄on，Fan/gyro off | 实际虚拟目标/owned InputHost及输入帧活动 |
| S3 | gyro on，Fan off | 真实/模拟sensor source明确，gyro帧推进；手柄依赖如实记录 |
| S4 | 三功能组合 | 三链确实active、共享PID不双算 |

每场景至少3次；建议warmup15–30s、稳定段60–120s，visible/hidden/minimized分别固定。记录实际页面、窗口、电源模式、AC/DC、曲线、输入/传感器频率与日志/调试开关。

A/B只比较ROG同机同条件、主线交回且身份明确的baseline/candidate，交错重复。先保存开始/结束实际readback和sequence，后标稳定段。mock与real-hardware分册；缺真实功能，不签实机S4。不要拿本机旧15秒mock窗与ROG旧S0相减计算下降。

## 4. 完整进程CPU和日志口径

按唯一PID＋creationTime归属YMCC本体、全部owned WebView2 browser/renderer/GPU/utility/crashpad、FanHost、InputHost和owned辅助exe。另一安装实例排除；断链/权限拒读/PID复用明确不确定，不能静默漏掉或补0。

gyro可能在本体或共享InputHost，不一定有独立exe，按一次进程CPU计；线程/功能诊断不双计总量。

cores=ΣΔ累计CPU秒/Δ实际monotonic wall秒；oneCore%=100×cores；machine%=100×cores/逻辑处理器数。保存原始序列、读状态、间隔和成员变化。整体分位数先逐样本求和，不相加组件p95。实际/期望样本数、missed和jitter相互解释，计数差不直接当掉样，missed=0不直接当完美覆盖。

日志清单包含路径/存在/长度/时间/读取结果，区分空文件、日志关闭、过期和字段缺失。对照内置日志、collector、任务管理器须同窗口同归一化口径；保留短窗峰值和启停成本，不只看长窗平均。

## 5. 权限、虚拟测试与禁止事项

- 只对可信YMCC启停/重启和受限AI操作，临时RunAs worker应有真实权限证据且正常退出。无UAC不代表当前AI已提权；拒绝时回写，不换通道绕过工具政策。
- 优先正常退出；不得启用force fallback、抢陌生端口/lease或按exe名称批量结束进程。
- 不创建SYSTEM/服务/计划任务/ACL代理，不安装/替换产品或驱动，不自动真实睡眠，不新增未知EC/OEM操作；设备/部署测试按原用户授权与域内裁决。
- AI Fan客户端只在精确会话/owned Host/零物理写协议下运行；普通与隔离namespace不可当任意文件代理，mock不证明RPM/OEM/HC物理交还。
- 不提供绕过AI能力门的旧手填JSON/不受控flag调用。能力暂未证实先报告，不猜测“产品没有功能”。

## 6. 失败和回写

每次唯一session/scene/repeat目录，不覆盖旧结果。失败也回写：工具/产品身份、请求动作、实际权限、阶段、错误码/exit、已产生文件、未完成项。缺目标、hash错、权限失败、端口占用、读失败不得产出“成功但CPU=0”。

回传只打包本次Results：manifest、summary、samples、process表、collector自耗、实际feature/握手、生命周期、日志清单、执行报告及工具回归记录。不回传token、leaseId、authorization/confirm、原凭据、settings备份、WebView profile或私密原始命令行。新工具使用白名单输出；外国/无关应用命令行也不得泄露。

## 7. 四种验收状态分开写

- localToolValidation：本机软件工具验证，不能升级为ROG通过。
- rogToolRegression：必须真实在ROG运行工具验证/预检/烟测后写。
- rogProductRegression：真实产品启停/功能/Host链证据，不等于工具解析成功。
- rogCpuAB：完整矩阵、同条件原始CPU与稳定性证据；最终胜负由本机裁决AI审核。

**此包制作时后三项均为NOT_RUN。** 不自动写rogRegressionPassed=true，不把复制成功、已装产品或旧S0×3当新回归完成。

## 8. 一次回报格式

ROG机器与工具版本/manifest；baseline/candidate产品身份；实际权限/启停；场景runtime readback与mock/physical范围；完整进程成员与计量质量；每窗统计与原始数据位置；失败/未验项；本次Results包路径。收齐后交本机裁决，不自改产品、不宣布优化通过。