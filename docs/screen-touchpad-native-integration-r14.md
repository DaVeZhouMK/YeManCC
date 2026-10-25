# R14：policy15 完整原生主程序集成编译

日期：2026-10-16，Asia/Hong_Kong。本轮没有新增生产代码改动，补齐上轮触摸板修正的程序级编译证据。

从主线冻结281个来源文件（含原生头/源、资源、JSON和WebView2静态加载库依赖）后，按native/build_native.bat中的主程序参数编译main.cpp、dsu_server.cpp并链接YeManCC.exe，启用原有原生Touch/Pen及主线配方中的MSI诊断定义。只读battery contract检查通过。

编译使用独立FrozenSource和Output目录，没有使用/覆盖Build/App/Native或安装目录。并行任务当时的其他主线代码包含在冻结快照中；该产物不是仅含触摸板改动的独立发布分支，也不是前端和所有helper齐备的可安装包。

## 结果

- 实际资源编译、两翻译单元编译和主程序链接成功，最终构建退出0。
- 281个冻结来源的编译后哈希保持；触摸板两个头与R13最终候选哈希一致，当前主线这两头仍一致。
- 产物为x64 Windows GUI，大小8687616字节。
- 管理员manifest已经嵌入并提取比对，requestedExecutionLevel=requireAdministrator、uiAccess=false与源清单一致。
- 产物含policy、move-submit-order、motion-gate等预期诊断字段字符串；这只是集成身份辅助检查，不是程序运行或触摸效果证明。

EXE SHA256：`31fd28292c8811f32886a84fd1dcc29b68d4833c37ea0bcded06bde0a20fefbc`。

## 证据和上限

证据根：`G:/YeManCC-Work/Build/Analysis/Touchpad-Integration-Build-R14`。source-manifest.json逐文件保存来源路径和哈希；build-shell.cmd记录实际命令；build.log/build-exit.json保存执行结果；Output/pe-headers.txt和embedded.manifest保存PE/清单核验；receipt.json为汇总。

尚未启动产物，没有注入输入、移动真实系统光标、重启Steam/游戏、部署或覆盖运行程序。本轮不改变触摸板输入、点击、震动、按键、焦点或鼠标穿透。未开展其他任务的监控/风扇排查。

此证据将“只验证头文件夹具”推进到完整原生主程序可编译链接，仍不能证明Steam实际鼠标输出的视觉闪烁已改善。当前源码为policy15，现场整体闪烁验收保持未完成，goal不标完成。


## 2026-10-16 后续发布包只读核对（R15）

其他主线任务已生成0.0.35完整包`Mainline/Release/Packages/YeManCC.zip`，SHA256为`16296f2dad302fdd6910ea69577741dced9091c6917e4420136b1ccb7517c7d9`。构建清单、ZIP导出身份、native EXE及10个触摸板头哈希已核对，确认包含policy15，不再仅有前述独立集成EXE。仍未由本任务启动/部署或获得新版现场视觉验收。

本轮6项retained链复核：隐藏松手观察按原24ms预算工作；明确标注的延迟输出反例说明不能直接改成只等显隐事件；完全Stop后125000次BUS无取时/查询/绘制。完整计数、反例与发布包身份见`G:/YeManCC-Work/Build/Analysis/Touchpad-Release-And-Retained-Review-R15/REPORT.md`及receipt.json。无生产补丁。
