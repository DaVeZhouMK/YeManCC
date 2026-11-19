# 悬浮显示、触控板、监控、通知与配置写入风暴回归

四路显示不是一棵统一的显示树。监控、通知、触控板及主 UI 浮层分别拥有窗口；它们竞争 Windows 置顶顺序，并共享部分原生 UI 线程处理时间。保留原有呼出、导航、隔离、手柄读入和配置来源，本次只加固现有显示生命周期与保存回执。

## 产品修正

- `native/game_overlay_presentation.h`：消费者验证前预留唯一入场所有权；排除已退休监听句柄；在可能重入的消费者回调及窗口操作之后核实原会话；旧回调的失败或异常不能关闭后继。待处理检查消息携带会话所有权，旧消息投递失败不能清掉后继消息。原有三次有限维护及事件合并保留，没有新增常驻轮询。
- `native/game_overlay_surface.h`：归还失败时保留隐藏窗口；下一次明确呼出只尝试一次恢复，再挂载原浏览器。子窗口恢复或销毁尚未确认时，不能遗忘所有权。
- `native/game_overlay_display_tree_lease.h`：子窗口样式恢复失败时保留原始租约信息，允许下一次明确恢复重试。
- `native/main.cpp`：配置已经落盘后的 Steam 壁纸、手柄光标及菜单刷新，分别走原有 `attemptRefresh`。刷新异常产生警告，不能把成功保存伪装成写入失败，也不能阻止后续消费者刷新。

## 运行

所有输出目录必须是新的绝对路径，不能覆盖已有证据。工作目录为仓库根目录。

```powershell
node tools/game_overlay_storm_win32_selftest.mjs --out G:\path\carrier
node tools/game_overlay_surface_storm_win32_selftest.mjs --out G:\path\surface
node tools/overlay_layers_storm_win32_selftest.mjs --out G:\path\layers
```

前三项使用私有或屏幕外自有窗口，成功的窗口调用仍进入 Windows。四路竞争夹具使用实际监控/通知载体、触控板可见性代码、浮层载体和原生同进程遮挡判断，包含真实监控指针接收窗口、丢失抬起的取消、通知到期、触控板释放、消息投递失败及最终资源归还。私有桌面从不切到前台。触控板的真实输入设备和浮层浏览器内容不在该夹具中启动。

界面仓库风暴测试及原输入入口回归使用已固定的 Node 与 Vue 工具：

```powershell
& G:\YeManCC-Work\Environment\Toolchains\nodejs\node-v22.14.0-win-x64\node.exe tools/settings_write_storm_selftest.mjs --out G:\path\settings
& G:\YeManCC-Work\Environment\Toolchains\nodejs\node-v22.14.0-win-x64\node.exe tools/controller_input_entry_lifecycle_selftest.mjs --out G:\path\controller
$env:YMCC_BASELINE_EVIDENCE='G:\path\native-settings'
node tools/settings_baseline_native_selftest.mjs
node tools/ymcc_sim/global/monitor-notice-lifecycle/native-lease-selftest.mjs --out G:\path\notice
```

前端测试执行实际仓库/API/IPC，原生配置服务是明确的内存端口。游戏就绪、电源就绪、调度等也由夹具提供；原生锁、提交、原子写入与异常数据恢复由另一原生设置夹具及隔离文件验证。前端超时只表示未确认，已经提交的写入不能回滚；迟到回执不能覆盖后续成功配置。

完整原生编译验证：

```powershell
node tools/overlay_storm_native_compile.mjs --out G:\path\native-build
```

此工具冻结当前源、编译原有三个原生翻译单元、链接并嵌入原管理员清单，保存匹配的私有符号与依赖记录。只验证编译，不运行或部署产品，不是完整发行打包流程。

## 本轮证据

证据根目录：`G:\YeManCC-Work\Testing\YMCC\Evidence\OverlayWriteStorm-20261117`。

- `carrier-before`、`carrier-begin-verify-before`：实际复现入场重入、旧监听事件、退休上下文及准备阶段验证继续操作后继。
- `surface-before-2`、`surface-restore-faults-before`：复现返回失败后不能再次呼出、子窗口恢复与窗口销毁失败时遗忘所有权。
- `settings-native-before`：复现落盘后刷新抛异常导致成功回执丢失。
- `carrier-final`：24 组原生载体回归；含 20 万次事件请求、8 线程 40 万次请求、256 个会话、故障与异常边界、维护次数上限。
- `surface-final`：10 组实际显示生命周期/原呼出入口回归；含 96 次挂载归还、400 次原入口开关、拒绝后不重复打开普通窗口，以及失败后的明确重试。
- `four-layers-final`：96 轮四路竞争、28.8 万次外部事件；队列有限收敛、各层显隐互不取消、监控接触取消、无需激活、无设备输入注入，结束时监听资源归零。
- `settings-frontend-final`：400 个并发提交、提交时取快照、120 次写入中17次失败后继续、80个旧代请求取消、3秒超时迟到回执、错误回执及后续恢复。
- `settings-native-after`：37组，1261项隔离文件读写检查，包含提交后9类刷新异常边界。
- `controller-entry-regression-8`：原13组58项正向检查及三项故意破坏生产逻辑的负向控制，均符合预期。
- `notice-native-regression`：原10组实际窗口/定时器/COM释放及通知租约回归。
- `native-full-final`：最终完整原生编译/链接结果及冻结源。

其他目录保留夹具建立过程中的编译、依赖固定和就绪端口适配失败。这些不计为产品缺陷；`controller-entry-regression-6` 是就绪端口未适配造成的未完成测试，已停止该自有测试进程，后继 `-8` 完整通过。

这些证据不证明独占全屏游戏的实际像素、物理手柄消费、游戏隔离、跨机器或长期硬件稳定性。本轮未控制安装中的 YeManCC、Steam或游戏，也未改动用户配置和设备状态。实机范围继续采用用户日志确认。
