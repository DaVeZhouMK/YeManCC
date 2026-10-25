# 独立监控窗口崩溃恢复 — 2026-10-17

已在主线源码增加 WebView ProcessFailed 处理。本机安装目录与导出快照均未修改。

## 已知故障与处理范围

10:01 的旧实例屏幕证据是 STATUS_ACCESS_VIOLATION 渲染错误页；07:19 的 Crashpad 记录为 renderer 0xc0000005。硬件统一缓存当时仍正常。二者不足以确定底层驱动、更新或某次 Hook 是崩溃根因。此补丁处理“独立 HUD 渲染进程坏掉后错误页长期置顶、无自主恢复”的明确缺口，不声称消灭所有 WebView 崩溃原因。

## 生产改动

- 仅独立监控承载新增 ProcessFailed 观察：处理浏览器进程退出、HUD renderer 退出/无响应；不因 GPU/utility 独立进程事件直接重建。
- 故障时立即隐藏并清除 ready/armed/layout。既有显示修复和数据 frame 均不能重显故障窗口。
- WebView 回调仅提交到既有 Win32 updateMessage；离开回调后关闭确切 id/nonce 对应的 HWND/controller。若投递失败，已有租约定时器处理关闭。没有新增定时器。
- 既有前端接到确切 closed 回执后，按原 create/ready/layout/painted 握手重建，不复用旧数据帧、位置确认或已过期租约。
- 原生 Carrier 保存本次 YMCC 进程的失败预算：累计三次故障后停止（最多两次自动重建）。预算不会因新 HWND、JS owner 重建或 status/initialize 调用而清零；重启 YMCC 才重新开始。可见状态包含 rendererFailures/recoveryBlocked。
- 观察器注册、获取 WebView 实例和导航拒绝等初始化失败也进入同一限次退场路径，避免恢复后再因创建失败无限重试。
- 新观察器使用 weak_ptr；退役 owner 清空指针，晚到事件不能关闭新窗口。现有唯一数据层、鼠标/触摸穿透、置顶逻辑及游戏输入路径保持原有边界。

## 验证

- 原生完整头文件的真实 fail/refresh/frame/status/close/proc 路径，在独立屏幕外 Win32 HWND 上执行：42 项通过；额外 5 项接线检查通过。环境为 inert COM，未杀真实 WebView 进程。
- 实际 host/coordinator/router/owner 联动，合成原生回执：12 项通过；故障两次重开、第三次关闭、无数据订阅残留、外部事件之后无自发重试。
- 既有控制时钟/租约 33 项、输入穿透接线 14 项通过。
- 冻结 298 个原生输入，按正常原生编译选项完整编译 main.cpp：退出码 0。COMPILE.log 与 SOURCE-PINS.json 保存编译范围。独立测试已链接，不启动产品界面。
- 本轮额外复核 TDP 连续性：10 项通过。
- 未部署、未重启 YMCC/HWiNFO/游戏，未注入真实 renderer 崩溃。因此实机崩溃后恢复效果仍待正常后续成品验证。

## CPU 边界

没有新增采集、轮询或刷新定时器；失败预算为固定 unsigned，正常运行只多一个布尔所有权检查。此处不把测试循环或分配计数描述为整进程 CPU 百分比。另一个内存行分类候选的长测回退已明确拒绝合入，证据位于 memory-row-kind-20261017/REJECTION.md。
