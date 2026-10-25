# 监控近期优化的完整 Native 集成编译验证（2026-10-16）

本轮完成当前完整 Native 主程序的独立编译和链接，并把编译输入与前几轮已执行的监控测试逐项绑定。没有新增产品改动，没有因为未发现新错误而修改稳定的数据有效性规则。上一轮 frame-curve-array-budget-20261016 已实际修改源码并完成字节/分配对照，属于 progress；本轮新增的是完整应用 Native 上下文证据。

持续目标仍 active：编译链接通过不等于安装版监控数据/全部机器运行效果通过，也不能证明整个 HWiNFO CPU 问题已全部解决。本轮未执行新编译的程序，未部署、重启或获取新的真实硬件样本，没有 CPU 百分比测试。

## 本轮数据链路检查

检查了原生 Windows 电池、单一风扇回执缓存、RTSS Global 上限、帧输出与前端 FPS 可见性/有效期代码。当前没有确认需要新增补丁的数据错误：

- 电池设备读取失败会清理当前来源；功率、剩余能量和时间仍需各自有效，不把缺失率伪造为真实放电。
- 风扇只消费已认证控制回复中的 YMCC 期望百分比，带 epoch、来源、真实 sampledAt 和 6 秒期限，读路径不新增 HTTP/设备访问。
- RTSS 上限只用既有安装 owner 的 Global 路径，变化需要分离时刻的相同内容确认；缺失/锁定/不完整文件不会变成无限制。
- 帧输出继续 HWiNFO Presented 优先、RTSS 救援；显示入口仍要求至少 1 FPS、同一目标存活及真实来源推进，重复运输不续期。

这些是本轮当前源码核对，非新采样、非新的跨机实测，也不将“未找到错误”当成所有路径的运行验收。本轮没有改变上述语义或刷新周期。

## 固定输入和真实构建范围

从主线建立独立依赖闭包快照：246 份源码/本地头/库/资源，共 21,128,504 字节。包括完整 main.cpp、dsu_server.cpp、全部递归本地引用、WebView2 头及 x64 静态库、JSON、图标、版本和清单。HWiNFO PowerShell 额外仅作来源绑定，不执行。

每个快照文件都记录 SHA-256，捕获完成再次核对主线；编译完成后逐项复核快照未变，且此时 246 份主线输入仍全部相同。没有声称多个共享文件的捕获是原子 Git 提交；这些证据只能绑定本次固定输入。

快照准备第一次将 WebView2.h 中引号包含的 rpc.h/rpcndr.h/objidl.h/oaidl.h/EventToken.h 误归为缺失的本地头。它们实际由 Windows SDK 提供；确认对应 SDK 路径后重新捕获，尚未发生任何编译或产品修改。没有为此伪造 SDK 头。

构建直接使用快照内 native/build_native.bat 的主程序三条实际 cl 命令，保留生产定义、C++20、UTF-8、MT、O2、bigobj、MSI 诊断定义和正常触摸输入定义。沿用实际 WebView2LoaderStatic.lib/WindowsApp.lib 及生产链接库清单。编译 main.cpp、dsu_server.cpp，编译 app.rc 后链接 Windows x64 主程序，最后由 mt 嵌入原 app.manifest。

全部输出位于独立证据目录的 output/，没有写 Build/App/Native、C:/SOFT/YeMan 或其他任务输出。没有调用会继续构建外部 helper、桥接插件或部署载荷的后续批处理步骤；没有声称已重建前端、独立恢复服务、FanHost/InputHost 或 RTSS helper。

## 构建结果

完整 Native 主程序编译和链接退出码 0，无编译/链接错误。日志只有 Windows SDK dinput.h 的 DIRECTINPUT_VERSION 未显式定义时默认采用 0x0800 的提示；本轮没有为消除提示扩大输入相关改动。

静态 PE 解析确认 x64、Windows GUI 子系统、图标资源和版本资源存在，最终 requireAdministrator 清单已嵌入。未执行 PE。

主要产物：

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| YeManCC.exe | 9,080,832 | 8fc106022c569758406064a0a1c205c8e407a644dc0e744af18604cfdfd10218 |
| main.obj | 86,677,689 | 266184f9ebb0538b27454735935e931721ca0d34ff744e9f51ede65971680955 |
| dsu_server.obj | 39,001 | bc3d3d12cf735eb06f1ca85f1a5472fb0a7b650e62608dd74b4492bcbf8133f4 |
| app.res | 14,652 | facb4abdbb3b30e440e799059d0355debf4f39693d4d11b5c5b065456bfa9170 |

这份 exe 仅为集成验证产物，未组装完整安装/更新包，不能直接代替正式导出流程。

## 与前轮行为证据的绑定

不能只引用历史结果中的 main.cpp 全文件哈希：其他任务可能修改共享源码。本轮从实际先前已编译的 copy.cpp 取证，逐段确认本次快照内真实 parser（剥离只用于计数的插桩）、reader 和发布器与之相同。原有 source-copy-reuse 的 1 MiB 复用/32 MiB scratch 和时间新鲜度头也相同。

monitor_frame_output.h 与上一轮曲线数组测试记录的生产头逐字节相同；monitor_rtss_frame.h 和 monitor_frame_time.h 与对应冻结依赖相同。PowerControl/YeManHWiNFO.ps1 与重复 RTSS 转读裁剪轮次的已测试源码逐字节相同。

这把既有 16 个完整原生输出对照、复制路径检查、帧包 968 份字节一致性及相关来源/失效检查与本次完整 Native 构建连接起来。历史测试数量没有重新计为本轮新运行，也没有把旧机器归档冒充本轮实际机器观测。

## 完成范围和剩余要求

本轮已确认：近期采集复用、帧包复用/数组优化和当前监控头能在完整 Native 主程序里共同编译、链接；对应监控源与已有行为回归实现一致。原生程序之外的全部 helper/前端打包和安装版运行未验证。

上游 HWiNFO 策略实际采用、真实共享内存长期有效性、安装版的全部显示/控制联动与跨机数据仍需继续核对。用户已要求不做后续 CPU 百分比测试，因此本轮也没有用占比或整机耗时替代事实；后续仍应根据实际工作量/数据完整性推进。

本轮产品代码零修改，仅新增本文和 Build 证据。所有其他任务修改保留，未暂存、未提交、未部署。

证据目录：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/native-monitor-integration-20261016/。

主要文件：SOURCE-MANIFEST.json、BUILD-SCOPE.json、build-frozen-native.bat、NATIVE-BUILD.log、BUILD-PROCESS-RESULT.json、SOURCE-STABILITY.json、TEST-SOURCE-BINDINGS.json、NATIVE-INTEGRATION-RESULT.json、output/、PATCH-CHECK.json、ROUND-RESULT.json。
