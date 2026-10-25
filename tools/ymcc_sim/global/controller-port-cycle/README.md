# 端口归还的实际消费者回归

`getControllerPortCycleAdapters()`提供`controller.external-port-cycle`。通过原global入口执行原PS/csproj/SelfTest-only入口，真实编译`InputHost/ExternalControllerPortCycle.cs`和`ParentProcessLifetime.cs`，实际调用生产ExecuteVerified，但所有owner、board、location、prepare终端由原SelfTest注入。

实际399个原场景通过。自有源码副本去掉unknown-board拒绝后，原断言准确报unknown board result/exact counts；编译失败或仅退出非零不能替代该负例。原脚本/入口/断言不变，故障明确为人工当前源破坏而非历史版本。

原构建的TEMP/TMP在进程创建前指向本case私有目录；实际DLL/PDB、构建日志、deps/运行文件和原输出均被保存并封入sourceHashes，公用读取器会拒绝生成物字节变化。工具链复用既有inputhost协议适配器的明确.NET输入，不另装依赖或驱动。

新目录不运行有效Run请求，不查询真实板卡/USB，不创建虚拟target、不执行端口变更或修改安装。实际PnP/物理归还、原生发起端的真实进程边界仍需分别验收。

```powershell
node tools/ymcc_sim/global/controller-port-cycle/run.mjs --evidence-dir <新的目录>
node --test tools/ymcc_sim/global/controller-port-cycle/runner.test.mjs
```
