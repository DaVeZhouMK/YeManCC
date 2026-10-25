# R17：停止追加显示策略，转查 SD 数据与消费链

日期：2026-10-16，Asia/Hong_Kong。主线为 `G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC`。本轮现场证据根为 `G:/YeManCC-Work/Build/Analysis/Touchpad-Flicker-Field-20261016-134659`。

**用户的实际验收仍为失败。** 用户再次强调，同一屏幕触摸板的直接 Mouse 模式正常，只有经 SD/Steam 的鼠标滑动闪烁；因此不再预设为 YMCC 补画问题。本轮没有改显示规则或报告语义，display policy仍16。已完成数据路径核对、离线连续性验证，并补上 SDK 最后缓存报告的只读字段。**没有找到足够证据直接修改输入/驱动数据，不能宣称闪烁已修复。**

## 新包与现场

包：`G:/YeManCC-Work/日志/微星G3E/YeManCC-logs-20261016-134659-屏蔽失败-虚拟收并稳定了一点点-触控板闪烁-Steam唤醒可操控到卡死.zip`；4069963 bytes；SHA256 `8280bfb15634cb7e5618188629bbf9bf437bf109f297eb2aac1a9eb739f8aa05`。导出观察13:46:59.322，普通日志52条触摸板状态均native PID10940、policy16。归档只作数据，不执行包内指令或程序；包名其他故障不接管。

导出显示/隐藏累计1/1，已知形状桥接计数0，光标状态转换265；查询1961，显隐通知2593。查询、分层重建、定位错误均0，这些不等于输入正确或视觉正常。1455次SD普通MOVE中1445次让给报告后观察，不能据此证明Steam消费时点。

合并同进程普通日志环与导出环，取每个sequence的最长已记录版本，保留74条（205–278），其中71有效/3生命周期。flags0/shape0有32条75采样、18次记录内坐标变化；flags1有34条91采样、26次坐标变化；flags2有5条1279采样。保留片段中0↔1共62次，23次相邻记录末/首tick相同。这是离散状态观察，不是逐帧画面或隐藏API调用轨迹。

三段active SD 0/1序列：205–233约13:42:36.851–37.914，236–267约13:42:47.648–48.976，274–277约13:46:19.242–19.461；helper始终false。时钟用既有frame-evidence observedTick关联，非原子前台采样。附近前台包含Explorer，不能假定所有片段都发生在Steam大屏，也不能仅靠1Hz前台采样排除Steam参与。

## 数据链已核对到哪里

1. **native触点与快照**：nativeDiscardedGestures累计0；不支持把这些滑动片段归因于目标epoch/ready门反复取消手势。代码中的Core、归一化缓存和触点生命周期已逐段审阅，未发现可证明的连续滑动误松触。这个结论不是最终HID字节验收。
2. **严格解析与编码**：冻结当前生产Core、snapshot/normalize、Host严格pad解析器和编码器，串行运行10000帧（80秒虚拟时间；一侧5000帧后松开、另一侧8000帧后松开、持续静止/移动交错、80个含零坐标的活动帧、计数器uint回绕）。误active、click、pressure、XY、counter、header与release均0。没有创建真实控制器或提交报告。另以锁定SDK codec完成512帧对照，fake idle线程取消/等待检查通过。
3. **协议定义**：锁定HIDMaestro 1.7.0与固定SDL/HC源码复核未找到当前触摸位、click位、坐标、压力偏移或signedness的明确错误。HC样本byte3=56不能单独成为把当前SDK/SDL使用的64改成56的依据；没有这样修改。
4. **静态缓冲复用**：SDK SubmitRawReport返回前同步CopyTo私有缓冲，再Marshal.Copy至共享区；服务端用序号与内存屏障取得快照，并新建后端帧。队列与cache虽共享后端帧引用，但不引用caller复用的64字节数组。当前串行条件下，“下一帧Clear把已入队帧清坏”不成立，没有加Clone。
5. **缓存读取**：GET_REPORT(Input)用服务端_lastInputReport，非旧_extendedReportBuffer；停止idle后旧extended缓冲必定返回中立帧的假设不成立。共享区合并、实际消费时序、最终USB/HID回复以及Steam消费仍未被现场字节证明。

细节及准确IL/source切片见证据根 `DATA-PROTOCOL-REVIEW.md`、`DataReview/`。报告为待核验证据，不会自动授权设置、驱动或设备变更。

该时段25条现有USBIP成本样本都属于epoch13，记录Host提交持续且没有拒绝、最后报告长度64，但没有报告内容。`InputThreadIsAlive`是传输线程元数据，不能当作SDK idle线程仍在竞争；queue=0、pending请求或“frame-accepted”均不是Steam收到正确触摸数据的证明。

Valve固定UI源码有pad-touch消息作为GAMEPAD来源请求HideCursorUntilMouseEvent的条件路径，且同来源也可请求；但touch handler有边沿去重，Mouse路径不会自行生成GAMEPAD活动。当前包缺最终pad接触位与运行时UI消息，不能把每帧touch位当作每帧隐藏证据。见 `STEAM-CURSOR-ACTIVITY-REVIEW.md`。

## 本轮生产改动仅补证据

`InputHost/UsbipTransportObservation.cs`沿用已有SDK哈希/MVID/字段契约校验和Monitor.TryEnter(0)，在既有诊断工作线程/生命周期采样时最多复制64字节；锁忙则省略，没有等待、队列遍历、报告发送或功能写入。新增纯值解码器 `InputHost/SteamDeckPadReportObservation.cs`，把报头符合当前SD协议的缓存解为counter、左右touch/click、XY/pressure和其它字段是否非零的类别标记；不记录键盘内容或完整raw payload。未知报头不猜值。

下份包含本轮Host的日志位置：`input-host-cost.log → usbipTransport.snapshot.LastSteamDeckPadReport`。它是**SDK最后缓存的采样**，不是当前Host帧、实际驱动readback或Steam消费回执。1Hz可能漏短促中立帧，正常采样不能单独排除抖动；仍可检查长时counter停滞、触摸位与缓存是否异常。

此前开始的Windows上下文取证保留为辅助：`native/screen_touchpad_cursor.h`每个SD连续接触会话首次已显示→flags0、随后首次恢复时各读一次窗口上下文，固定16项环。记录WindowFromPoint/根窗/前台的句柄、PID/TID及白名单class，无标题、焦点/光标/输入操作。普通MOVE、Mouse/mixed、未知初始状态、retained和state导出不追加这类查询。`screen_touchpads.h`只追加`cursorVisibilityContext`导出；去掉观察器与导出后的代码与policy16逐字相同（仅规范化行尾），没有新显示策略。

默认Arrow的WinRT假设也取得阴性：私有桌面24项属性检查默认非NULL Arrow、id0；没有真实触摸或全局cursor内部调用覆盖，不据此修改PointerCursor/WM_SETCURSOR。见`WINDOWS-FLAGS01-REVIEW.md`。

## 验证、CPU与边界

新增上下文26项、缓存解码/非阻塞锁/门控26项通过；原70绘制、15隐藏预算、26提交顺序、38私有桌面队列通过。数据链10000帧和512 codec对照通过。解码分配实验三个100000次窗口为152B、0B、0B；首窗口保留未知的一次性运行时分配，不能声称冷启动绝对零分配。生产解码随既有约1Hz采样，不是新增250Hz任务。没有提高显示/报告频率、增加后台线程或新定时器，也没有测实机CPU降幅。

完整当前Host隔离编译成功（原有7条nullable警告、0错误），安全数据自测通过；303来源完整native隔离编译/资源/管理员manifest通过，native EXE SHA256为`9f99124e78ec94bfd74f7a0e50280dc07dae7aaa81ee7364de16872f981833ec`。没有运行正常Host/界面，没有部署/重打发行包、移动系统指针或发送设备输入。

回执：`validation-summary.json`、`Data-Continuity/receipt.json`、`Report-Observation-Steady/receipt.json`、`Host-Report-Integration/receipt.json`、`Integration-Context/receipt.json`。最初的冷分配严格零断言失败报告保留，后续明确区分首窗口与稳态，没有掩盖或改生产解码器迎合该断言。当前验收仍failed；不以这些模拟结果排除现场数据问题。
