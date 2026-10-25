# AMD 核显 FSR 全局升级（2026-10-24）

本功能按所有品牌的 AMD 核显能力工作，入口为“设置 → 设备 → AMD 核显 · FSR 全局升级”。不按厂商、机型名单或 AMD CPU 品牌准入写入。第三方设备说明仅作研究参考，不生成专属品牌开关、驱动链接或默认配置分支。

## 能力与官方参数

1. Windows SetupAPI 只读取当前显示设备的 PCI 身份和驱动信息；不为识别创建 DXGI/D3D 设备。
2. AMD 官方 ADLX `IADLXGPU::VendorId` 必须是 `1002`，`IADLXGPU::Type` 必须成功返回 `GPUTYPE_INTEGRATED`。
3. `IADLX3DSettingsServices3::GetFidelityFXSuperResolution` 获取该核显的官方 FSR 升级接口；`IsSupported` 成功返回 true，且 `IsEnabled` 返回可读布尔值后才允许应用。
4. 只用 `IADLX3DFidelityFXSuperResolution::SetEnabled(adlx_bool)` 写入该核显开关，再读取同 PNP/驱动版本的实际值核对。没有写入 AMD 私有注册表参数，也不连带设置 RSR、AFMF 或其他显卡功能。

未知名称的 AMD 核显，只要官方类型和能力检测成功也能使用。AMD 独显、NVIDIA/Intel、仅有 AMD CPU 或类型未确认均不能写入。旧驱动下的核显名称提示仅用于展示“可检查 AMD 驱动”，不声明已支持 FSR 4.1，不替代官方能力检测。

界面统一提供 AMD 官方驱动下载入口。驱动未提供能力时显示通用说明；不承诺更新驱动就一定使所有核显支持。实际升级模型/版本由驱动决定，接口没有返回 FSR 模型版本，保持 `versionConfirmed=false`。游戏仍需兼容并在游戏内开启 FSR。

## 静态页面与默认一次

- App 启动和其他设置分区零 FSR 检测/应用；只有进入且实际可见的设备页才创建页面会话和执行一次检测。
- 内置默认 `schema=1, defaultEnabled=true, devices={}`。首次进入设备页且核显/驱动确认可用时，按每个 PNP 记录执行一次默认开启。之后手动关闭会保留；应用重开、页面重进、更新驱动不会重放已尝试的默认写入。
- “重新检测”只读，静置及重复焦点事件不轮询、不重写；无功能定时器、常驻线程、后台自动恢复或额外服务。
- `gpu.fsr.page` 仅改变页面会话元数据，在 UI owner 同步处理；耗时的 `get/initialize/set` 使用原有后台 IPC 队列。切走、隐藏、睡眠或退出后旧会话不准入新的驱动写入，旧响应不再更新界面。
- 默认尝试标记在实际写入前原子保存，避免异常/超时后自动重放。标记之后取消也不重新给予自动写入机会，可由用户显式切换。
- AMD 的同步 API 已经进入执行后不能安全强行撤销；页面关闭阻止尚未开始的后续写入并丢弃晚到展示，不执行反向恢复或自动重试。

配置位于 `yemancc_data_dir()/amd-fsr-upgrade.json`，不覆盖其他模块设置。文件锁跨进程互斥，句柄按作用域释放。

## 低占用与依赖

`deps/adlx-sdk/SOURCE.json` 固定官方未修改的头文件依赖、许可及 SHA256，版本 `2.0.0.125`，提交 `32b5a740d42295c5dfe9026b9f52683da0f3af91`。不随包携带 AMD 驱动 DLL。

AMD 官方 v1.5（提交 `d9f04a9bba022d6cf6333f005dd540b4ad19fb63`）首次提供 FSR 接口。运行前先读取 System32 `amdadlx64.dll` 的文件版本资源，已知旧于 1.5 时直接返回不可用，不执行旧库入口或初始化。新库仍检查官方 QueryFullVersion 和实际接口，不凭版本号就报支持。

请求结束按官方生命周期先释放 feature/GPU/service 引用，再按本功能初始化所有权执行 ADLXTerminate、FreeLibrary；显示设备列表、注册表句柄及配置文件锁均有作用域清理。页面取消使用现有 IPC 和事件，不加轮询。

## 本轮实际验证

证据目录：`G:/YeManCC-Work/Testing/YMCC/Evidence/AmdFsrUpgrade-20261024/`，当前回执见 `RESULT.json`。

- 73 项实际生产核心断言通过：一次默认、重复/100 次页面循环、手动 OFF、配置异常、失败读回、类型和身份拒绝、多适配器、AMD 核显/NVIDIA 独显混合、AMD 独显拒绝、未知型号核显按官方能力准入，以及页面退出时的取消和晚到回调。
- 实际组件/正式 gamepad engine：720/800 两档共 8 状态、256 次方向检查、24 项交互检查，无页面/导航错误；自有浏览器和本地服务退出已确认。覆盖其他分区零请求、静态页无轮询、页面取消、旧驱动提示及通用 AMD 驱动入口。
- Vue 类型检查通过；原生 main.cpp 检查和当前 runtime 实际编译/链接通过。
- 本机实际只读 25 次：线程保持 4，首次 Windows 元数据缓存建立后的句柄保持 106，私有配置锁可重新取得，AMD 库没有留下加载引用。静置 10,000 ms，CPU 时间增量 0 ms。首次只读 62 ms；25 次读取总计 2,547 ms、CPU 1,000 ms，均为显式压力读取，不是后台负载。Windows CPU 时间计数有粒度限制，不能解释为整机/整个 YMCC 永远零占用。
- 冷启动时 Windows 元数据/版本查询建立进程缓存，句柄从 87 到 106；保留该成本，不宣称冷启动零资源变化。原直接加载旧 ADLX 的资源检查为 FAIL，原件 `StaticPage/resource-cpu.json` 保留。独立阶段诊断确认旧库会带起额外资源；当前按官方最低版本预检跳过旧库，25 次读取资源无增长。

当前电脑 ADLX 为 1.4，未提供新 FSR 接口；真实写入次数为 0，没有修改安装版 YMCC 或实际显卡配置。兼容 AMD 核显上的实际 SetEnabled/游戏画质、实物手柄和完整正式发布验收仍为未验收。新版驱动自己的资源行为需在支持设备上记录，不能用本机旧库跳过路径代替。

## 官方依据

- AMD 官方 ADLX SDK：
  https://github.com/GPUOpen-LibrariesAndSDKs/ADLX/tree/32b5a740d42295c5dfe9026b9f52683da0f3af91
- AMD 官方 v1.5 FSR 接口：
  https://github.com/GPUOpen-LibrariesAndSDKs/ADLX/blob/d9f04a9bba022d6cf6333f005dd540b4ad19fb63/SDK/Include/I3DSettings3.h
- AMD 官方示例（同一 Get/IsSupported/IsEnabled/SetEnabled 调用链）：
  https://github.com/GPUOpen-LibrariesAndSDKs/ADLX/blob/32b5a740d42295c5dfe9026b9f52683da0f3af91/Samples/CPP/3DGraphics/FidelityFXSuperResolution/mainFidelityFXSuperResolution.cpp
- AMD 26.10.1 发布说明（仅作支持演进依据，实际使用以驱动接口为准）：
  https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-26-10-1.html
