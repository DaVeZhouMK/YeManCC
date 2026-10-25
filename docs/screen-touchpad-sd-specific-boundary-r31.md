# R31：SD 专有触摸路径的范围与未闭合边界

2026-10-16，Asia/Hong_Kong。用户对照：同一屏幕触摸板的直接 Mouse 正常；虚拟手柄模拟鼠标、摇杆模拟鼠标正常；只有虚拟 SD 触摸板经 Steam 合成鼠标时闪烁。保留该人工反馈，不当成本轮工具实测。

结论：该对照降低公共鼠标／公共虚拟传输整体故障的优先级，要求优先核对 SD 接触生命周期、完整报告数据与交付、SD 专属鼠标转换。它不能独立证明数据正确，也不能独立证明 Steam 软件错误。当前没有持续闪烁的唯一根因；没有新行为修补。

## 已核源码与精确解释

- native/screen_touchpads.h 的 Mouse 分支在 Core.event 中根据当前位置与 lastX/Y 算相对位移，保留小数；Deck 分支保留接触，Core.snapshot／normalizeContact 产生 active、click、绝对 XY、pressure。InputHost/ScreenTouchpadReport.cs 把这些值写入64B报告。两者最终都是鼠标，不代表中间数据或状态机相同。用户的直接 Mouse 对照也覆盖共同屏幕输入入口；仅摇杆对照不能单独覆盖物理触屏接触的影响。
- setContext 对 profile、target availability、控制目标等实际变化才 PostMessage(kRefreshMessage)。稳定相同输入不先写 false 再写 true，也不每轮重复 refresh。configure 只有配置／几何等实际变化才 release／refresh；透明度单独更新不释放触点。主线 production setContext 调用在 inputCaptureRefreshRealStickSettings，其他已查命中为 fixture/setup。WM_DPICHANGED、WM_DISPLAYCHANGE、SPI_SETWORKAREA 是独立真实重建入口，没有依据推断它们每 MOVE 触发。
- snapshot 的原子快门在 enabled、nativeMode、availability、activeProfile/appliedProfile 不满足时会直接返回空，早于 observeNativeTransportLocked；因此 discardedGestures=0 不能证明所有 omit 门从未经过。只确认此代码边界，未证现场跳变或错误；没有移除任何准入门。
- profileState(cfg, requestedProfile) 从 live state 出发，再覆盖返回的 persona/config 和对应 bank 的 availability。screenTouchpads.get 返回 requested persona；日志中 disabled/steamdeck 相邻查询不能当作实际 setContext 在反复切换。Vue 的 refresh/get 接收状态；并非每次读取都会调用 set。

## 辅助鼠标端点的限定复核

固定 SDK SHA bd42a99bcb260435ce25796c54a4b792f8a2ced6ab78659c0cf926011663938e、固定 profile SHA 2dde58d8d350be981eccbd7593b38ab017ea11a42de3a86a9aa7ab395525af77。复用 R28 的离线反编译，不执行 SDK。

profile 确有 if0/EP0x81 鼠标、if1/EP0x82 键盘、if2/EP0x83 vendor controller。UsbipEmulatedDevice 构造函数用 HidInterfaceNumber 选择 primary；InputPumpLoop 只从 PendingFor(primary) 取请求或向共同 frameQueue 放完整报告；HandleSubmit 只在请求 endpoint==primary 时从 frameQueue 取报告，否则保留对应端点 pending。该类两处正常 SendInterruptInReply 调用分别在上述 pump 和受 primary 条件约束的 HandleSubmit。因此没有在这条持续 interrupt 输入路径中找到“同一份 pad 状态同时转成另一条 legacy mouse movement”生产者；不能仅因复合设备有鼠标描述符就认定双鼠标。

2026-10-16 R35纠正：此前将后面的共同GET_REPORT分支误认为可由非primary接口到达。固定SDK在非primary特殊分支之后显式SendError(-32)并return，辅助接口不会落入该分支。R35真实SDK/USBIP隔离测试确认if0/if1 GET_REPORT(Input)均status=-32/length0，if2才返回当前完整64B；参见screen-touchpad-protocol-mixed-input-r35.md。原先的辅助接口vendor缓存泄漏怀疑撤回；未修改SDK、HID描述符或控制返回。

## 两项并行审查结论

AdmissionReview 确认物理 source 判退可升级整帧 semanticNeutral，间接撤销仍按住的屏幕 Deck 触点；它是已证条件耦合、不是已证误判。撤销后同一触点留在反馈路径，准入恢复不会自行重建，所以不能单独解释同一持续触点反复 touch1/touch0。没有移除安全门。完整 frozen source、C1 边界、main.cpp 并行 drift 映射见 G:/YeManCC-Work/Build/Analysis/Touchpad-Producer-R31-20261016/AdmissionReview/REPORT.md。

BetaProducerReview 对用户指定 Beta 两个二进制的限定静态审查定位 trackpad→Win32 SendInput，没有可比的 virtual SD touch/raw producer。结果为 NO_COMPARABLE_SD_PRODUCER；不将其当作正常 SD 参考。SDT 物理 Deck→X360/DS4 同样不是 virtual SD producer；真正可比的 HC/libviiper 源码交付边界继续引用 R29，未绑定其产品 DLL 或实际运行。

## 当前可说与不可说

R28 首份合法 FRAME/NEUTRAL 前的单写者接管修补保持；没有证据把它当作已接管后持续闪烁的充分解释。R30 的原有稀疏 producer/pump 元数据保持，不能冒充实际端点完成或 Steam 接收。

已有18:47包的 raw touch 记录与主要 cursor 闪烁段不重合；严格编码对源帧一致也不等于最终交付一致，不能据此宣布数据无误。下一个必要因果边界仍是同一次持续滑动中，SD 接触位／XY 是否连续通过最终交付；不能用增加显示层策略、补帧、平滑或另一份稀疏缓存替代这个边界。

本轮仅静态源码、已有证据及离线二进制审查；未运行测试、fixture、仿真、产品／SDK、输入／设备或进程内存操作，未编译、发布、导出、部署或替换 SDK。生产行为和运行时 CPU 开销新增为0，保留并行任务修改。只读 Steam audit：13 areas /397 candidates /1 missing anchor native/task_view_helper.cpp /0 unmapped；缺锚不属于本任务。目标仍 active、未完成、未验收。
