# Shared runtime 的无 HC 接线认证

统一入口：

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests lifecycle.shared-runtime-no-hc -EvidenceDir <源码目录以外的新证据目录>
```

此场景复用正式 `InputHost`、`BatteryHost` 项目与 `RuntimeSharedDependencies`，不创建替代 Host 模型。在不包含 HC 目录的隔离副本中发布两个实际项目，核对各自真实解析的引用、复制的共享 DLL 和依赖策略正负控制。只运行依赖策略夹具，不启动产品 Host 或读取设备。

2026-10-19 认证复现并修复了验证器的两个断点：冻结上游原件迁入 `deps/hc-runtime-upstream` 后仍查旧生产目录；BatteryHost 接入总库后，隔离构建没有复制实际项目的四个能力库 Compile 链接。当前验证器根据同一 runtimeId 与 manifest SHA 解析冻结开发原件，保留历史来源字段；已存在但损坏的旧原件不能回退绕过。隔离源码直接按两个正式项目的 Compile Include 取值并记录哈希。

9项控制中含7个明确拒绝：原件manifest改动、迁移身份不符、上游路径越界、已存在坏历史manifest、缺少实际链接源码、链接路径越界及未解析MSBuild表达式。实际发布同时验证全部五个外部链接源码、八个共享DLL、依赖策略回执以及执行前后源码一致性。外层全局入口另记录实际原件与工具身份。

该场景证明软件引用、构建与共享依赖拒绝合同，不证明实际按键、物理屏蔽、虚拟目标提交、EC充电或完整安装程序可用。`GamepadMotion.dll` 与 BatteryHost 保留的 `Resources/inpoutx64.dll` 是单独依赖，不能因本场景通过而删除。
