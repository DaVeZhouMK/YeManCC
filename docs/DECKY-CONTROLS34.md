# Decky 控件与真实风扇状态接线（2026-10-10）

## 已完成的三个变更

### 1. 虚拟手柄与主线四档一致

全局和专属都只有：关闭虚拟手柄（disabled）、SteamDeck（steamdeck）、PS5（dualsense-edge）、Xbox（elite）。不再提供 PS5 标准、PS4、Xbox360 或“全局方案”第五项。旧配置不迁移、不清空；旧 persona 可以保留读取，但不能作为新菜单选择。专属的整体记忆规则保持，未专属时显示真实默认值，首次选择按原 owner 记忆整套。

### 2. 风扇不因侧栏过快判定而回退

根因：旧镜像把 owner.start + apply 一直当作未完成 RPC，客户端四秒结果确认窗口到期就清快照、触发 ToggleField 回退；allowed=true/writeReady=false 的原生命周期中间阶段也被过早拒绝。

现沿原 FanHostLifecycle 登记用户开启意图，立即回复 applied=false/pending=true，界面显示“已开启，等待 YMCC 风扇真实信号”。慢启动不再占用侧栏 RPC；其它控件可操作，风扇挡位切换可保存并合并为最新一次用户选择，原 owner 就绪后下发。镜像不增加启动超时、不轮询、不自动重试，不另开 FanHost/硬件 writer；原生命周期的权限、电源、真实写入与恢复门全部保持。

- “开”是用户控制意图，不是假定硬件已就绪。只有原 owner 的控制结果才结束等待。
- 真正的 owner 失败仍显示未确认提示，不把等待超时伪装成关闭或硬件成功。
- 明确关闭时立即取消开启意图，OEM 交还本身若慢也不占住 UI；pending 回执不是交还成功证明。
- 意图代次令旧 start/apply/disable 回执不能覆盖新选择。退出 mirror 时忽略迟到回执，不新建硬件清理通路。
- 原 FanView 业务函数未改变。

### 3. 陀螺仪全局与专属都显示开关加四预设

“陀螺仪”开启/关闭 + “陀螺仪预设”下拉：FPS射击/fps、赛车/racing、自定义/custom、Steam/steam。入口不再放在只对已确认游戏显示的高级行里；无游戏时也显示，沿原全局 input CAS 保存，有专属时仍走原整套专属 owner。关闭时保存选中的预设记忆；虚拟目标关闭、不支持或原主线锁住时如实禁用，不创建新输出目标。

原 gyroMotionParamsForPreset 的纯参数展开提取为共用模块，专属 overlay 和全局 mirror 复用同一函数与既有 presets，未另造预设参数表。全局保存仍用原设置版本比较并交换、原 input.shortcutRuntime.clear，不添加第二个 gyro writer。

## 验证范围

- 生产 Client→Relay→Host→adapter 的纯内存端到端：5 项；60 秒模拟慢启动/慢 OEM 交还不引发客户端回退，等待期间可修改原全局陀螺仪。
- 真正原 FanHostLifecycle + 延迟冷启动的 NoIoHost 边界：3 项；确认一次握手/一次 lease、最新原预设与关闭均由原 owner 处理。
- fan/relay mutation：44；生产 TSX presentation：30；全局：15；专属 gyro：23；游戏 adapter：50。
- 原只读：32；帧率/触摸板：28；客户端：31；电源三秒尾沿：16；专属隔离：30；Vue 触摸板渲染通过。
- 类型检查、完整 Native/Web/Decky 构建与完整发行链通过。最终 ZIP 430 文件逐文件哈希一致。
- 最终 ZIP 真实插件 factory 的 SVG Y 验证：5 项；居中图标与原标签布局修复保留。
- 导出器 37 项回归保留，没有把严格导出规则加入程序启动逻辑。

## 部署边界

本批只生成完整测试包，未替换 C:\SOFT 安装文件。没有触发真实风扇/陀螺仪写入，没有创建真实虚拟手柄，没有删除缓存，没有重启 Steam。真实 ROG 机器的启动与操作仍需使用本批包验收，不能凭模拟回归宣称硬件已经通过。长期安装验收 goal 没有恢复或标记完成。

## 本批包与证据

- 完整包：G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Controls34\FullRelease-20261010-154338\Release\Packages\YeManCC.zip
- 版本：0.0.35
- buildId：df1abd9d6d1598e35a4e62aeb05e2e419c6b0e7813607525075c4ae167607892
- ZIP SHA256：ef7070c11d9bac476f2443677dc922ce9211f79bd56438fe1ab45736293ae40f
- Native SHA256：f565e94890cfc7059e936188f28d4fe17c2b9604a1286e6575976736332d849d
- Plugin SHA256：120d8b6674d4257ccd143c6f5fa945a59808a9ee9decc4d76b38c88ca6bea2be
- 状态与验证：G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Controls34\validation\CURRENT-STATE34.json
