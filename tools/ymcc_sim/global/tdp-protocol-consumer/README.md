# Original TDP helper protocol consumer

`getTdpProtocolConsumerAdapters()`提供单项`performance.tdp-protocol-consumer`，交父协调者串行登记到现有global入口。本域不修改shared registry/CLI或实际产品helper。

原`tools/tdp_protocol_selftest.py`与`PowerControl/pawnio/YeManTdpCtl.py`按原字节复制到本轮私有目录。原五组检查直接调用原helper的AMD家族/邮箱路由、Intel MSR编码、CLI/daemon单目标和客户端allowlist。原脚本中的硬件发送fake保持原样；产品helper只导入，Main、daemon进程、PawnIO驱动与实际硬件不运行。

```powershell
node tools/ymcc_sim/global/tdp-protocol-consumer/run.mjs --evidence-dir <不存在的绝对输出目录>
node --test tools/ymcc_sim/global/tdp-protocol-consumer/runner.test.mjs
```

复用现有host-process.ps1受控Windows Job启动Pythonfixture，原组结果必须自然返回；超时/启动错误不是负例成功。Python固定使用-I -S -B和全新无缓存路径，保留原assert，隔离用户site及外部pycache，记录已使用的运行时模块/字节。运行期审计拒绝驱动/DLL动态加载、真实子进程、网络、注册表、链接及本case外文件变更及除固定Python运行库之外的外部文件读取；日志仅写scratch。受控job自身创建的是测试进程，不是实际TDP产品进程。

真实负例只在冻结helper副本把Intel单目标PL2最小增量从1改成2；原selftest及其断言不动。必须得到原check_intel_single_targets中calls[-1]断言的AssertionError和退出1，编译/环境错误、其他断言、零退出不能充当检出。

worker观察实际exec的两个代码对象，保存marshal字节、原源hash、原输出和实际函数帧。代码定义帧包含类/模块，不代表类的硬件实例被运行；真正禁止动作由审计单列。assert语句行到达次数只作执行观察，不当成独立功能数。正例必须完整执行原五组且原PASS标记存在。

正式result的sourceHashes绑定原输入副本、原报告、两个正/负组的实际代码对象和日志；外部Python/Node/PowerShell及已使用运行时文件以artifactInputs锁定，前后均核对。全部软件证据不证明PawnIO/MSR/SMU/WMI实际接受、OEMCPU/OEMGPU已统一绑定、现场功耗或已安装helper可用。
