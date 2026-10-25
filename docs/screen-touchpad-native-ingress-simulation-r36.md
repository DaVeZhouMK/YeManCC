# 触摸板R36：原生触摸入口与时间戳隔离测试

本轮不再只调用Core::event：当前生产header原样编译，实际执行handlePointer、反馈路由、capture loss、Core、native transport观测和外层snapshot，再用原样机械提取的生产帧装配和快速序列化产生完整requestText。之后进入R34已封存的真实Host/SDK/USBIP/socket夹具，未改写请求。

## 已执行结果

- 实际handlePointer隔离段：10组场景、401条事件记录、399帧，1,211检查0失败。
- 原生序列化399条请求逐字节与通用JSON路径一致；23份释放态省略touchpads，没有把诊断字段强加回body。
- Host/SDK/socket选定测试17项通过，含399原生入口帧和已有时序/故障控制。后者若未匹配R34原case名会用合成fallback，不当本轮原生事件的独立时序证据。
- 独立从原始Hex/USBIP头核固定生命周期、整数字面坐标、压力、普通域中性、counter及case内时钟，4,313检查0失败；320帧连续MOVE无意外松触。

场景覆盖普通及延迟投递的连续滑动、DOWN/UP同一UI轮次处理的长按/短tap、source32回绕、新ready前的过期DOWN、ready/epoch取消后旧MOVE、canceled与旧ID capture loss、前台反馈路由及新触点。过期/取消场景有意验证释放和不复活，它们不是本轮发现的产品异常。没有把普通连续MOVE通过扩大成真实OS事件调度已正确。

## 隔离边界

只直接调用生产handlePointer，受控参数和时钟，未创建HWND或真实Windows消息循环/触摸输入。调用的InvalidateRect已替换为无副作用记录，GetCursorInfo为固定无游标替身；输入/窗口创建/PostMessage/hook/桌面查询尝试均0。适配器与WinRT getter的有界独立研究见同研究根InputAdapterReview，不能把getter替身称真实WinRT派发。

没有运行真实设备创建、VHCI/HID completion、Steam鼠标合成或画面测试，没有提高刷新率、平滑、修改产品手势或运行时CPU链。

## 当前结论

R34–R36已把选定正常源、序列化、Host准入、普通混合字段、固定SDK数据/USBIP方法、原生消息入口分段接通。仍未复现用户的单次持续滑动闪烁，也没有同次现场最终交付/消费证据。旧18:47归档的raw触摸观察与18:42主要cursor操作段不重合，不能把模拟成功或非同段raw零错配当作现场数据无误。

本轮保留已有R28单写者修补；没有新增可归因当前持续闪烁的产品行为修补。全部测试/证据在Build/Analysis/Touchpad-Producer-R36-20261016；NativeIngress/Final、NativeIngress/FastSerializer及HostTransport/Final保存各段输出。任务整体尚未完成。

适配器独立13cases/73checks通过，正常Timestamp单位与当前source契约一致；异常getter/nonfinite终止坐标可能丢UP/cancel的条件反例保留，但未证现场因果、无生产修改。18:47微星包的export state已报告steamdeck下nativeMousePassCompiled/Requested/Active=true、runtimeError0；它只是导出时点，不能覆盖18:42主要滑动。当前build/owner默认请求独立route，旧2026-10-11的默认关闭说明仅作历史记录。全量可重复入口RUN-INGRESS-SIMULATION.ps1已再次跑通。
