# YMCC 控制台 Mainline20：原链路镜像、真实 EXE 列表及运行中导入修复

日期：2026-10-09（香港）。权威源码：`G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC`。

## 当前结论与边界

1–7 已落入主线源码并完成编译／回归。小黄鸭按用户最新裁决，撤掉前一轮尚未验收的侧栏独立控制动作，只保留薄镜像适配器。

**本轮没有完成真实候选 → 真实游戏 → Steam → 小黄鸭的运行闭环，也没有宣称物理插帧/FPS 已确认。**带调试参数的真实候选启动请求被执行环境拒绝，启动脚本没有落盘，我未启动候选；未替换安装版。真实文件读取、原业务模块的隔离测试都不是实机运行验收。

## 1–5：显示与电源来源

- AC/DC 配置仍由原 EXE 配置模型分别记忆；页面只渲染当前电源来源的一侧，不删另一侧数据。
- 复用原 `powerSourceMode` 和插拔／电源稳定事件；打开镜像时增加一次只读快照修复漏通知。未知来源不同时显示两侧，也不制造 AC/DC。
- 通知版本保护：晚到的旧快照不能覆盖最新插拔事件；无新增常驻电源轮询。
- 删除超线程旁指定小字，控件仍是原方案值而不是硬件回读。
- “手柄”改为“虚拟手柄”，“无操作（遵循全局）”改为“全局方案”，底层数据值 `follow` 不变。
- 移除风扇下方的宿主来源／首次修改／方案值长段说明；保留错误、恢复中、未确认游戏等必要状态提示。来源元数据仍在协议中，不再占用页面说明行。
- Y 图标用固定尺寸 inline-flex、水平／垂直居中和明确行高。这里只通过源码／控件契约测试，**没有新的真实 Steam 像素布局截图验收**。

## 6：小黄鸭真实列表与原链路镜像

### 已只读识别到的真实文件

`C:\Users\DaVe\AppData\Local\Lossless Scaling\Settings.xml`

共 9 个 Profile，7 个无 EXE 路径的模板，以及用户真实导入的两个条目：

| 原标题 | 原 Path | AutoScale 配置 |
|---|---|---|
| Cyberpunk2077.exe | `D:\Game\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe` | true |
| WARNO.exe | `D:\Game\WARNO NORTHAG\WARNO.exe` | true |

文件 SHA-256：`332412aff63a5cab72eb7cef0b75e979f895c3eae1c48911cac1da7fff1eb4a9`。
本轮针对这两个条目只读，没有重新导入或改写用户文件。`AutoScale=true` 不等于程序当前已启动，也不等于当前已插帧。

### 链路

```text
YMCC 原 GameQuickActions / QuickAppView
                       └─ quickapp.setLosslessScalingEnabled
Steam 镜像 game.setField(losslessScaling)
                       └─ 薄适配器 setLosslessFromMirror
                          └─ 同一个 quickapp.setLosslessScalingEnabled
                             ├─ 开：原 oneClickFrameGen → -path / -auto
                             └─ 关：原 quickapp 公共关闭入口 → 正常退出精确 LS 进程
```

关闭公共入口补在 YMCC 原 `quickapp.ts` 模块，原两个页面也接入同一公共入口；没有在侧栏另写一套 FSR 控制器。镜像模块没有 `shell.run`、`shell.execute`、文件写入、AutoScale 补丁、UI 点击或快捷键动作。

- 使用小黄鸭自己的 EXE 列表，不新增 YMCC 小黄鸭配置表，不生成侧栏方案。
- 完整路径优先；唯一同名 EXE 可识别；同名歧义拒绝任意挑选。
- 开关显示从原 owner 的只读观察得到：精确 LS 进程、LS 自己的选中配置和 Scale 按钮状态。
- 不可读时显示不可操作的未知状态，不把保存的 AutoScale 当作已开启；不把未知状态制造成硬件关闭。
- 开启后的启动／缩放延迟只做有限状态等待，不重发 `-auto`，不增加后台轮询。
- 已确认属于另一 EXE 的会话不会被关闭。
- 保留原游戏 PID／创建时间／路径身份、共享快捷动作锁、电源代际和私有源文件快照校验。
- 控件回读只证明小黄鸭自身控件状态，**不是 GPU 输出或 FPS 测量**。

## YMCC 原“一键导入”写入修复

修复位置是 `quickapp.ensureLsProfile`，不是侧栏写文件：

1. 先验证原 XML；坏文件不关闭程序，不毁坏有效备份。
2. 若 LS 正运行，校验精确 PID＋创建时间＋路径，正常退出并有界等待。
3. **退出后重新读取最新 XML**，避免把 LS 最后保存的内容覆盖回旧快照。
4. 复用原模板和原导入算法；备份、提交前再次源校验、原子写入、提交后回读。
5. 原先运行的 LS 在完成或写入失败后恢复为程序运行状态；原先关闭时不额外开启。
6. 正常关闭失败／关闭到托盘时，不强杀、不继续写配置，明确要求从托盘正常退出再重试。

这针对“运行中配置驻留／占用”风险补齐了原交易顺序。已在**原函数＋模拟文件占用／进程边界**中复现未保护写入失败并验证修复，不把它算作本机真实运行中写入通过。真实实机原因和闭环仍待候选验收。

## 7：Steam 大屏内页入口

- 文案：**Steam大屏插件【三点键呼出】**。
- 未保存过选择时，原 native 初始化默认启用；明确保存的 false 保留。
- 删去原环境说明小字和额外页面环境查询；失败／缺资源／需重启等仍通过原提示区显示，不静默执行远程安装。

## 验证证据

证据目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Console20\validation`

- `MAINLINE20-LOSSLESS-OWNER.json`：19 项原 owner 故障／状态／镜像委托检查；真实两 EXE 文件只读指纹。
- `MAINLINE20-POWER-SOURCE.json`：10 项原电源事件、迟到快照、未知来源与释放检查。
- `MAINLINE20-PLUGIN-PRESENTATION.json`：18 项 TSX 控件、单侧 AC/DC、禁用回调、标签和图标样式契约。
- `mainline20-web-regression.txt`：类型检查、完整控制台回归、原 Fan 生命周期、Windows native broker loopback、Vite 和最终插件新鲜度通过。
- `MAINLINE20-LS-PS-SYNTAX.json`：4 个从原 owner 生成的脚本语法检查；未执行脚本。
- `MAINLINE20-GLOBAL-CACHE.txt`：原全局缓存／配置回归通过。
- `MAINLINE20-NATIVE-BUILD.txt`：主线 native compile/link 成功；未执行产品 EXE。
- `MAINLINE20-NATIVE-SELFTESTS.txt`：5 组原 native fixture，27＋26＋33＋32＋28＝146 项；无真实 Steam／硬件／用户配置操作。
- `MAINLINE20-SOURCE-PRESERVATION.json`：剥离 12 段控制台增量后，native 原业务体字节保持；没有回退并发窗口定位工作。

另外，原 QuickApp 浏览器 51 项、游戏策略隔离回归通过。测试假数据和用户真实文件读取分开记载。

## 候选与未完成的实机验收

- 候选：`G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Console20\Build\App\Native\YeManCC.exe`
- 同批前端：旁级 `..\dist`（从新鲜 Web 构建复制），不能单独拿旧安装前端覆盖它。
- 候选仍读取原安装位置的业务配置；启动可能执行原 CPU 自启／风扇恢复行为，之前已获用户启动许可，但本轮实际启动请求被执行环境拒绝。
- 安装位置目前没有完整 `PowerControl\decky` 资源；没有擅自部署或替换。仅有候选 EXE 不等于 Steam 插件已部署。
- 待验收：真实 LS 运行时原一键导入、两个真实游戏 EXE 切换、原链路开关与真实 Steam 页面布局。不得用本轮隔离测试代替这些项。

验收时应先正常退出已有 YMCC，再打开候选，避免继续测试旧程序；不要只替换 EXE 而保留旧前端。当前另观察到 YMCC PID 39048，来源未能确认，未接管或关闭它，也不据此声明本轮候选已运行。
