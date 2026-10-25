# 触摸板原生队列与消息读取 CPU 优化 R9（2026-10-15）

## 当前结论

继续保留用户的关键对照：YMCC 直接模拟 Mouse 不闪；虚拟 SteamDeck 经 Steam Input 合成鼠标才闪。R8 后移 Deck 显示通知已取得真实 UI/BUS 线程、PostMessage 和 layered HWND 的隔离验证；这仍未执行真实 Host/Steam 合成或验证屏幕扫描输出，不能宣布现场闪烁已改善。

本轮额外落实两处明确 CPU 冗余，修改仅涉及 native/screen_touchpad_cursor.h 与 native/screen_touchpad_contact_visual.h。没有改报告内容、Core/触点/点击映射、震动、焦点、24ms预算或用户设置。主线仍为 policy12 / submitOrder1 / geometry1 / evidence1。

## 修改及量化证据

1. cursorSync 开头发现 active/retained 都为空时直接返回。取消已在 UI 撤下画面，旧 BUS 消息随后到达不再读取 GetCursorInfo，也不消耗后续预算。真实队列夹具先暂停 UI、排队通知、在恢复处理排队消息前取消触点：Before多一次查询，After为零；没有复活光标的现象，不把这处冗余扩大为漂移/闪烁根因。稳定 active/retained 路径多一次或两次原子读取，未测真实进程CPU百分比。

2. contactVisualProc 的 GWLP_USERDATA 读取由无条件入口移入 WM_STYLECHANGED/WM_ENABLE/WM_NCDESTROY 分支。仍即时读取活指针，不跨事件缓存。普通位置/绘制/命中消息不再读取未使用的数据；样式/启用修复和销毁清理保持。

四种尺寸、8组各100次输入MOVE的实际原生对照：GWLP_USERDATA读取由 2388 降为 0。每种尺寸第一组99次实际定位产生297次旧读取，第二组100次实际定位产生300次旧读取，因此不是简单假设“一个MOVE必有三条消息”。额外NCHITTEST/鼠标激活/指针激活返回值保持，启用/透明样式损坏仍读取活状态并恢复不可交互，销毁读取由3次收窄为必须的1次。

两侧 renderCounters 完全相同：接触光点移动816、上传12、边框上传42、光标查询29、光标上传1、窗口定位20、position-only提交4。R7的稳定根RECT查询仍为0。

## 真实消息队列验证范围

新增 tools/screen_touchpad_native_queue_selftest.cpp / .ps1：独立 UI 线程绑定从未显示的私有桌面，BUS在另一条真实线程调用原调度API。PostMessage/GetMessage/DispatchMessage、光标窗口及GDI图像/位置均为真实调用；控制输入Core是最小适配器，光标观察、时钟、Host/Steam提交阶段和WinEvent注册由夹具替换。没有触摸/键鼠注入、真实Steam进程、真实系统光标位置更改或显示桌面切换。

| 相同最终夹具 | 结果 | 意义 |
| --- | --- | --- |
| R8之前policy11 | 20项，恰好5项失败 | 4项提前查询时序 + 1项取消后无用查询 |
| R8主线、本轮修改前 | 20项，恰好1项失败 | 4项R8调度已通过；仅取消后查询冗余 |
| 本轮主线 | 20/20 | 显示通知顺序与取消后的查询均符合约束 |

20项包含Mouse/混合模式维持原时点、retained、双向模式切换、原生窗口的不可交互和命中、UI暂停期间200次BUS机会只合并一次查询、恢复读取最新观测、cancel后不显示和空闲请求不做查询。它不能证明Steam在本次通知之前已经更新坐标，也不能代表实际OS时钟下的刷新率/视觉验收。

初次Queue-Before还出现两项retained断言失败：UP本身会向第一个fence之后继续投递同步消息，初始夹具把“首个fence已到达”误当成“setup派生工作已完成”。夹具修正为只对setup命令按pending有界排空，再开始测量；特意测试的暂停/排队场景不使用此排空。产品未因此更改。该初次失败证据原样保留，最终相同夹具见Queue-Before-Verified、Queue-Policy11和Queue-After。

微软一手消息语义核对：GetMessage可先派发非排队的sent消息，posted消息通过消息队列/循环分派。因此这里使用同窗口posted fence，而非SendMessage充当排队完成屏障；setup继续派生的新posted消息还需要显式检查pending。官方来源：https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmessage 与 https://learn.microsoft.com/en-us/windows/win32/winmsg/about-messages-and-message-queues 。

## 其他回归

- 触点窗口消息CPU同夹具163项：Before恰好12项无用读取断言失败，After163/163；包含原R7/R6的移动/缩放/隐藏/重试/强制更新/重新落指/第二板/UP/cancel。
- 光标实际头文件/API替身70/70、R8时序替身19/19。
- 完整WinRT私有桌面跨进程鼠标穿透64轮、1038断言、1600命中。
- Core/路由/输入所有权/穿透源码12/12；本轮两文件精确逆向Delta2/2。
- 所有原生回执确认UI线程退出、私有桌面关闭和输入桌面保持；WinRT夹具另外确认Composition/queue关闭、自有下层进程退出。没有部署、Steam/游戏重启、真实输入或设备变更。

## 留档与复现

证据目录：G:/YeManCC-Work/Build/Analysis/Touchpad-Native-Scheduling-R9。

Queue-Before为早期setup fence诊断，最终使用Queue-Policy11 / Queue-Before-Verified / Queue-After。Contact-Before / Contact-After保存完整冻结头文件、实际编译产物、163项和前后计数；Cursor-Regression / Report-Order-Regression / Native-Mouse-Pass为本轮最终回归。Before/native / After/native / production.diff / production-mutation-receipt.json / production-boundary-sourcecheck.json / delivery-receipt.json记录窄改动与来源。CPU-SIDECAR-REVIEW.md记录只读候选审查，不能把其源码频率关系当作实测数据。

复现：tools/screen_touchpad_native_queue_selftest.ps1 -OutputDirectory <fresh-dir>；旧头文件比较加 -CursorSourceDirectory <native-dir> -ExpectedFailures <对应receipt中的名称列表>。tools/screen_touchpad_contact_message_cpu_selftest.ps1 -OutputDirectory <fresh-dir>；旧基线加 -BaselineDirectory <Before/native> -ExpectBaselineFailure。两个runner严格核对预期失败集合与实际清理，拒绝把夹具失败当作通过。

截至本轮结束，本机日志目录最新仍为2026-10-15 17:05:38的policy9归档。缺少包含policy12的新现场证据；当前不具备宣布用户所见闪烁已解决的条件。
