# R26：18:47现场包覆盖边界与Steam更新门

2026-10-16。继续虚拟SteamDeck由Steam合成鼠标的闪烁任务；直接Mouse正常这一对照保留。整体目标未修复、未验收。此次仅文件分析和静态研究，没有生产行为修改、功能测试、仿真、应用/设备执行或部署。

用户说明18:47包来自其他任务的导出，版本/字段缺项不能证明本任务诊断失败。主线源码已包含R18逐成功raw返回统计、R22 logs.exportAll消费端导出接线；源码存在不是现场包已携带或本轮已发布。源码与标准构建接线回执为任务根MAINLINE-SOURCE-WIRING.json。

## 新现场包确有需要的触摸板反馈

193条cost记录中190条有累计steamDeckPadSubmissions，按Host实例/epoch/target generation分组，不将累计快照相加。有效raw观察段最终3793次返回、7个边沿、4次touch变化、2次click变化；touch/click/XY/pressure与期望错配、counter不连续、坏帧及诊断错误均0，单enabled span、边沿无丢失。它检查实际构造报告对源帧期望，并不检查Steam是否收到或每次坐标的物理合理性。

只有两段左板接触，125ms/16份与281ms/36份报告，其中第一段有click；没有右板touch边沿。32对Host末报告/SDK缓存值相同，但都是接触外的非原子快照。193条SDK缓存对象有192条可解码，采样点touch均false，不能拿1Hz采样漏过短接触当作SDK自动清touch。

cursor环内主要移动约18:42:32–36；上述raw观察段到18:47:05.555才开始累计，两次接触约18:47:14。两者没有重合，且cursor环没有左右pad身份。早段滑动不能借后段短触摸判定正常。原生详细门在18:43:42.668开启；最早USB-IP保留快照约18:43:52，均晚于主要移动。精确SourceTick、约算墙钟与17ms offset变化边界在Field184736/ANALYSIS.json。

震动摘要软件侧共107次sent、failed0、dropped0，均medium/MSI，硬件震感未验收。fatal文件只有3条orderly shutdown-command，不称作触摸板崩溃。存储VDF仍17477B/SHA d1e7cf9364a7e108ab62110efa592e4a9eecfa8e36ad76b8f251e9299b84e4f6；不证明所选布局。新Steam消费端文件在此包缺席，只界定证据范围。

## 最近source gate与binding边

恢复并复核上次中断worker的文件：同CTrackpadSource的base初始化T68=1、Ta9=0、Tbc=1；factory具名pad0/1/2路径传mask10/20/08。slot28/38/40是已核a9 override生产者。enabled wrapper按a9或(a8&Bac)算T68，再清a9，false跳过copy/update，未直接写touch0；不能将false gate当成收到neutral。三份新完整根共781B，见SourceGateReview/REPORT.md。

额外追65dd8a直接helper6699c0完整172B：传入RDX=T+8，callee的[RDX+68]实际为T+70，并非更新门T68；它再将T+8传给绑定子对象虚函数，具体子对象读gate与否仍未知。这关闭一个offset误配候选，不能据此证明数据端正常或归因闪烁。证据在BindingGateReview。

固定abstraction A+b0的生产者/传播进一步闭合：open中R14是入口info，不是已证的A+7c；67719d从A+8重新加载分类，6771a0写info+34，674fd0先保留原分类info+38、对info+34做5→4后整体复制，A+b0取得原值。已核Neptune分类路径产生4/5，不满足66hex特殊分支。固定EControllerType名称表的66hex为controller_legion_go_s，4/5同名controller_neptune；字段直接传给该名称函数的typed边未捕获，现场对象/其它writer也未核，不能推广所有Deck绝不进入。该结果降低特殊分支候选优先级，不是根因或修复。完整指令/边界见ReportGateReview/GateProducerFollowup/REPORT.md。

## 执行边界与下一步

继续Steam消费端的实际selected source、归一化后XY/touch及特殊分类的typed链，不加刷新率、轮询、平滑、虚构位移、延迟UP或显示补丁。此轮新增运行时CPU为0；整体Host混合工作CPU不能作触摸板归因。未覆盖共享Release、导出其它任务产物或更改Steam设置。完整记录在G:/YeManCC-Work/Build/Analysis/Touchpad-Data-R26-20261016，现场详见Field184736/REPORT.md。
