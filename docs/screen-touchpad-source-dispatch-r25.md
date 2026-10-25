# R25：补齐管理对象来源，核触摸源按钮复制和更新门

2026-10-16。上一轮R24为实际进展：接通固定Windows本地HID/SDL读取链并检查后处理。本轮继续收窄数据链，**未确认闪烁根因，没有生产修复或实机验收**。直接Mouse正常、虚拟Deck由Steam合成鼠标闪烁仍是本任务依据。

## 四个后处理callee的明确直接写入边界

完整核对63d430/63c5d0/63d990/63d750，共2511B。入口输入U=B+0x24或B+0x58，17条直接U store的实际width换算后均未覆盖buttons/touch19/20、pad XY或pressure；最远stop为B+0x8c exclusive，pressure恰从8c开始。边界来自实际指令，而非循环stride猜测。

具名assert源路径是Valve controller_gyroprocessing.cpp，处理内容有陀螺仪/姿态子块。保留U的寄存器从建立到最后store保持不变，没有新的LEA/MOV显式转交保存U给其它指针参数；内部calls使用栈/state/标量，部分CRT残余寄存器效应未完全闭合，不把“17条直接写不重叠”变成无条件全链无副作用。详情BufferWriteReview/REPORT.md和WRITE-BOUNDS.json。

另核与现场body完全相同的stored VDF：唯一gyro_to_mouse group29在preset0标gyro inactive，preset1未引用。raw触摸报告36..42缺省，与原SDK状态编码同样先清buffer且无该字段，不能作为新触摸路径独有差异。此比较不是IMU协议正确或runtime布局证明，不改全局gyro/quaternion/用户设置；见DataCrosscheck。

## 管理对象来源已接上

共同父根614d40的615438直接调用factory parent5fa440，6159fe直接调用normalize caller60e990，两处RCX明确来自同一R12；入口及循环重建都使R12=原始入口RCX-8。由1373条指令和到达定义记录，不再用相同stride/member offset推断同对象。

可与R24完整factory的成功RSI->RAX别名、已知三张vptr和成员存储连接，条件性接通CSteamControllerAbstraction/Chell/SteamOSHandheld消费路径。但现场具体索引、factory分支、其它member writer和对象寿命未证明，不以静态共同owner替代现场选型。详情ManagerJoinReview/REPORT.md与REGISTER-ORIGIN-LEDGER.json（新增完整根5912B）。

## 找到触摸源缓存的精确按钮writer

true消费者入口60ed60把同一normalized B传65db70，后者从D+0xc8列表取T，再以T/B调用slot48。若当前T确为已核RTTI表12e55e8的CTrackpadSource，该slot明确绑定66f570；同表slot78绑定672990。

66f570中：

```
T.buttons = B.buttons                   // 66f5e2: qword[T+30]=qword[B]
if (u16[B+20]>1000) T.buttons |= bit25
if (u16[B+22]>1000) T.buttons |= bit24
then call same T/B -> virtual78 ->672990
```

仅追加bit25/24，不与touch19/20或click17/18相交。随后672990按左右pad提取bit19/20到T+27，XY来自同B。**这关闭了此wrapper在复制后额外mask掉稳定touch位的直接机制。** 复制确实执行、当前列表项为目标类型以及当时B值仍是前提；不能从decoder处稳定直接跳到整个source稳定。

## 一个新的具体数据条件：更新门

在复制之前，wrapper先计算并保存：

```
T[68] = (T[a9] != 0) || ((T[a8] & B[ac]) != 0)
T[a9] = 0
```

另需source enable T[bc]!=0。门不成立时跳过这次buttons复制和virtual78更新；本wrapper没有显式把T+30或T+27清零。**跳过更新不等价于收到松触包，也不等价于下游一定继续使用旧touch。** 这个门可使“报告具备touch”与“source本次得到更新”出现区别，值得继续追a8/a9、B+ac的生产者及T+68在后续消费中的使用。

SourceDispatchReview已完整核65db70/66f570共1422B并复用具名RTTI/原始touch读点，未重开Kalman或显示。仍缺D+0xc8所选元素实际类型/构造链、实际门值和发生次序；先前列表项或前置未知对象也可能处理同B。没有把这个新门宣布为现场触发原因。

## 当前结算与下一边

本轮改变了三处判断：四gyro callee直接跨界覆盖触摸的候选缩小；factory/caller同manager的静态来源缺口闭合；normalized.buttons到触摸源缓存的条件性copy/gate明确。下一可核的是source列表/类型与a8/a9更新门、B+ac分类字节及后续T+68用途，而非重复SDT/header/idle/空读或gyro直接写入。

R20最终坐标rangeguard仍是未绑定现场的清touch候选；R22父机全局门值0只限定该次采样。现场最新归档仍到14:34，没有新R18逐提交/R22消费端证据，缺失不能当作输入正常或新诊断失败。

仅新增文档、索引和静态证据，无生产运行逻辑/频率/滤波/压力/UP/显示/设置更改，无新增运行时CPU工作。未运行目标程序、进程读取、设备、测试/数值仿真、编译或部署；共享其它任务修改保留。worker封存48/48及47/47文件核对一致，Steam索引13区域396候选0缺失0未归类。验证回执只证明上述静态来源和文件完整性，不能替代消闪验收。
