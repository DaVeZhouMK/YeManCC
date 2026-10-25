# R33：实际 HC 保持语义、源触点边界与现场证据阻塞

2026-10-16，Asia/Hong_Kong。上一目标轮R32为progress：SDK官方raw/idle缺陷、HC实际库身份及旧后端保持源链已经核实，但持续闪烁根因与改善仍未证明。本轮完成剩余有界静态工作，整体未完成。

## 新闭合的实际 HC 字节

固定HC a250 bundled DLL SHA91a1851e…，有dirty构建身份；本轮直接从该字节核4函数，共2400B，不借用干净bf9d或R29源码等价。

- controller HandleTransfer收到gate deadline结果时，继续在状态锁下复制已保存state，构建完整报告；仅另一类context结束走nil。无新输入且没有deadline或信号时等待。没有自动清touch/XY的timeout分支。初始状态未审，结论以已有有效state为前提。
- 被动重复保持touch、XY、pressure和保存Frame/counter。新一次相同非nil state提交仍可原子递增counter；不能把这种producer提交和被动重复混淆，也未追全部外层C ABI调度。
- 实际controller路径传0x40给builder，builder分配64B并写header byte3=64，返回完整 `01 00 09 40`。因此当前64B格式与这份HC实物相符，旧679/header56不能作为改YMCC的依据。

完整函数地址、原始字节、COFF全局error身份和指令窗口见研究根DirtyHcHoldReview/REPORT.md、EXCERPTS.json、RECEIPT.json。没有外推server期限、端点pending、实际上线或实机正常证明，没有照搬其定时重复。

## 新确认的 SD 源边界条件

Core只保存每侧当前Pad；若一次已采到A触摸之后，非tap UP和B的DOWN均在下一次BUS snapshot之前交付，S0/S1可能均是touch=true，源pointer id又不在HID报告内，中间释放可以消失。直接Mouse在B的DOWN重建lastX/Y基准，两者行为不同。

这是源码顺序推导，不是测试或仿真。需要实际UP/DOWN，不能单独解释同一id连续MOVE时反复闪烁；未证现场发生过。现有raw与期望比对可对两者都得到touch=true，因此零错配不排除源侧边界合并。总events和native transport取消计数不能补正常UP/DOWN的按序历史。

外层SDK还可合并已提交的中间状态，因此仅强插一份源UP不能保证端点实际观察到。没有据此延迟手势、插入UP、增加刷新、改click/force或扩大故障。完整逻辑与边界见NativeContactBoundaryReview/REPORT.md。

## 当前状态重核

主线ScreenTouchpadReport.cs仍为88b5cf…，UsbipTransportObservation.cs仍为9cf03e…，固定SDK仍为bd42a99…。当前Host Program.cs与R30冻结编译输入逐字节相同，SHA3f3aae…；不是只复述旧审查结论。

native/main.cpp与R31结束冻结副本的静态diff共29个hunk；原29595–43332输入块中有15项变动，范围在物理XInput槽位/准入控制段原31529–32249。原39400–43332的组帧/提交连续段没有变化，不据此宣称整个controller运行行为不变；并行修改全部保留。详细hunk和哈希在CurrentSourceDriftReview。

固定SDK USBIP AcceptLoop明确设置socket.NoDelay=true，因此没有依据将本服务端缺少TCP_NODELAY当作新的传输原因。既有Send完整字节和队列机制静态证据继续保留，但不能代替真实发送间隔/驱动完成。

用户指定微星归档目录仍12份ZIP，最新仍18:47包且SHA与原分析精确相同。没有新现场数据覆盖闪烁的同一次持续滑动。当前目录库存和生产哈希见CURRENT-INPUT-INVENTORY.json；不推断未查询位置的数据状态。

## 完成与受阻审计

| 要求 | 当前证据 | 判定 |
| --- | --- | --- |
| 查明当前持续闪烁的实际原因 | 生产代码、协议、实际HC、旧日志均已检查；缺故障同段最终touch/XY交付 | 未达到 |
| 修补且证明闪烁改善 | R28单写者修补保持，旧idle缺陷获上游确认；无对应新现场验收 | 未达到 |
| 保持现有可用操作、低CPU和原报告频率 | 本轮运行时源码/SDK未改，无新timer/线程/刷新/平滑 | 本轮范围内保持；不冒充实机回归 |
| 当前材料与并行修改可核验 | SDK/Host/source hash、实际DLL函数字节、主线差分与ZIP库存均已记录 | 本轮静态交付完成 |
| 不做测试 | 未运行测试、fixture、仿真、SDK/产品/设备/输入/进程内存操作 | 遵守；只有静态源码/文件分析 |

同一阻塞条件在R31、R32与本轮均存在：同一故障手势缺最终交付／消费证据，无法判定是源边界、后端时序还是数据之后的转换。它不是三种不同阻塞，也不是把未测当通过。

现有静态工作未发现可安全归因为当前持续闪烁的新行为修补。继续重做显示、改header/pressure/counter、补帧或移植参考库都缺因果依据；新一轮同类静态盘点也不会补出故障瞬间的运行值。按用户不做测试的约束，本任务不能在本机自行制造相应现场。本轮请求将整体目标置blocked，等待能覆盖同一持续滑动的数据或其它改变该证据边界的外部资料；目标未完成、不标complete。

没有新编译、发布、导出、部署或SDK替换。维护audit13 areas/397 candidates/1既有task_view_helper.cpp缺锚/0unmapped。所有子审查已complete并关闭，无等待中的工具、进程或代理。
