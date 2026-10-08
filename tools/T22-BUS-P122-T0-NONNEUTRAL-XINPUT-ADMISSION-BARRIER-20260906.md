# T22 BUS-P122：非中性 XInput 输入与后台乱按安全闸门

日期：2026-09-06
状态：SOURCE-FIXED / BUILD-DEPLOY-VERIFIED / SAFETY-BARRIER-PROVEN / PHYSICAL-AXIS-FAULT-UNRESOLVED

## 1. 安全处置

用户反馈 YMCC 后台手柄乱按。已先停止旧 YMCC 与 recovery service，避免继续消费输入；随后只读复测真实 XInput，再完成源码安全修正与重新部署。

## 2. 只读证据

停止 YMCC 后，xinput_physical_observation_probe.ps1 持续 5 秒观察到：

- connected=true，connectedSlots=0
- buttons=0，leftTrigger=0，rightTrigger=0
- leftX=2586，leftY=-32503
- rightX=0，rightY=0
- buttonObserved=false，axisObserved=true

首尾样本均保持同一非中性左摇杆状态。该证据证明实体 XInput 源在 YMCC 退出后仍有持续左轴值；它不是 YMCC 前端伪造的按钮状态。本轮没有对应 buttons 非零、LT 非零或 RT 非零的只读证据；随机 RT/LT/Start 仍是未知。

## 3. 明确偏移与修正

当前源码新增的 50ms GP_STATE_FALLBACK_TIMER_ID 会在 Raw Input 缺失时重复调用 gamepadEval。如果设备在连接时已经处于非中性，导航层会按持有/重复规则看到该轴。

修正文件：native/main.cpp。

新增连接中性准入门：

physical source first observed → publish connection snapshot to UI → awaiting-neutral → no semantic action admitted → first coherent neutral sample → semantic admission may resume

实现：

- 新连接或重连时设置 g_inputReleaseRequired=true；
- 页面连接态仍可发布，不隐藏真实设备；
- 在 gpAnyShortcutHeld()==false 前，gamepadEval 不进入 global shortcut/UI semantic；
- 断开/重连重新建立该门；
- 不添加任意摇杆死区，不篡改 HC 轴值，不生成虚拟手柄。

这是生命周期安全边界修正，不是把物理轴错误值校准为中性。

## 4. 构建与运行验证

native/build_native.bat → BUILD_OK。
tools/deploy-installed.ps1 → DEPLOY_OK。
安装 native `C:\SOFT\YeMan\YeManCC\YeManCC.exe` bytes=`2263552`，SHA-256=`F3B89973EB4D5DAB6FE83C08973A8398315092E954BCD0A418CD439DF6ACADF6`。

部署后新实例记录了：

- gamepad-xinput-state: connected=true, connectedMask=1, primarySlot=0, source=xinput
- gamepad-neutral-admission: status=awaiting-neutral
- gamepad-state-replay: reason=webview-render-ready

观察窗口内没有 gamepad-ui-input-emitted；neutral gate 生效。页面连接态与 semantic 准入已分离。

## 5. 未知与后续缺口

- 物理手柄为什么长期输出 leftY 约 -32503、leftX=2586：需要 Windows 游戏控制器、另一台主机或换手柄做物理 A/B；不能由源码猜硬件、驱动校准或上游注入。
- RT/LT/Start 随机按钮是否真实存在：当前 XInput 只读证据不支持，必须单独回传 buttonMask、leftTrigger、rightTrigger、packet 样本。
- 游戏消费者是否收到该后台物理轴：需要独立 consumer observation，不能从 YMCC native 日志推导。

在缺口闭合前，不把随机按钮问题标记为已解决，也不重新接入 HidHide/XInput 阻断或虚拟手柄掩盖它。
