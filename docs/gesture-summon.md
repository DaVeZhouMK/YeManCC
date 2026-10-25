# 手势呼出（未启用）

状态：未启用。停用原因：影响 Windows 功能。

设置页面移除手势呼出入口；窗口位置卡片标题为“窗口位置”，仅保留左右位置选择。不会把已停用功能转移到其他设置栏目。

## 未启用保护

- 前端 ui.gestureSummonEnabled 默认值及归一化结果均为 false。旧配置 true 在读取后也表现为关闭；直接通过 setUiSettings 提交 true 会保存为 false，不影响其他字段。
- 原生 kFeatureEnabled 固定为 false。EdgeSurface::refresh 强制关闭，不能因旧配置、强制刷新、左右切换或恢复消息创建触摸区域。
- 手势呼出的消息处理入口也检查未启用状态，排队或过期消息不能执行呼出。
- 未启用时的刷新不读取手势配置，不增加轮询、定时器、自动恢复任务或触摸采集资源。
- window.gestureSummonStatus 记录 featureEnabled=false、enabled=false、active=false、disabledReason="影响 Windows 功能"。原有状态字段保持兼容。

## 保留实现与 Windows 边界

native/gesture_summon_core.h 的识别器、native/gesture_summon.h 的触摸区域实现及未挂载的 GestureSummonSettings.vue 组件保留，供后续评估；当前没有用户可达入口，也没有可通过配置开启的路径。

此前提交 Windows 边缘手势屏蔽请求及扩展为全屏根窗口的实现已移除；当前进一步完全停止创建手势输入区域。此功能不写 Windows 全局手势策略、通知设置或触摸偏好。

手柄组合键、专用按键及其他呼出机制不属于此次停用范围。

## 验证与证据

- tools/gesture_summon_native_selftest.mjs：保留纯识别回归，并验证旧 true、强制刷新、左右切换、空窗口及排队消息均不能激活功能。
- 本轮从实际 native/main.cpp 抽取状态、刷新和消息分支进行隔离编译回放，检查未启用原因、无配置读取和无呼出执行；不是完整安装版验收。
- 设置页面使用实际组件及正式手柄引擎做 1280×720／1280×800 隔离回放，检查入口移除及左右位置选择保留。
- 原有 touch／idle／browser 手势启用测试保留为历史工具；其中启用行为预期不适用于当前未启用版本，不据此声明当前触摸路由已验收。

本轮证据位于 Build/Tasks/Gesture-Summon-Inactive-20261018。撤销 Windows 手势屏蔽的上轮证据位于 Build/Tasks/Gesture-Windows-Restore-20261018，历史区域及识别设计可在该目录与本轮 before 副本查阅。

主线源码变更不代表安装版已替换；隔离回放不等同于实物手柄或实体触摸验收。
