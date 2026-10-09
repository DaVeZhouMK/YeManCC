# YMCC 控制台 — Mainline18 验收说明

日期：2026-10-09（Asia/Hong_Kong）。维护源码：`G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC`。

## 本轮界面

- 页标题、插件返回的显示名以及主线 Steam 设置页文案改为 **YMCC 控制台**；内部插件 ID/目录仍用 `ymcc-sidebar`，避免改动身份与安装路径。
- AC、DC、核心调度、陀螺仪保留四个离散滑块。
- **手柄恢复原生下拉**。仅确认选择后提交一个原 EXE 值，不经滑块中间状态逐项切换。Windows 的下拉可能转移到 Steam 主窗口，因而仅手柄菜单持有一个最长 45 秒的可见性租约；确认、取消、超时及卸载都会释放。
- **超线程改为开关**：只提交原 `hyperThreadPolicy` 的 `on/off`。已有 `default` 状态不自动写配置，显示“Windows 默认（未显式设定；不是硬件关闭状态）”，不伪称硬件回读为关闭。
- **风扇只保留全局开关**。当前挡位从原 YMCC 全局设置读取并显示；侧栏不再提交 `fan.setPreset`，不创建侧栏专用风扇方案。原风扇宿主和全局控制路径未改。
- 小号文字和局部紧凑间距保留，手柄选择框也设为 12px。

## 通知上方的入口

已在真实 Windows Steam 中验证实际标签顺序：**`999,0,3,4,5,7,6`**。

- 999 是原 Decky 入口，0 是通知。移动的是承载 YMCC 的现有 Decky 入口，**不是**复制一个 YMCC 独立入口/页面，也不是给页面内容置顶。
- 排序发生在固定 Windows Loader 的 `TabsHook.render` 返回链，使用其原生 DFL `afterPatch`。保持原 tab、panel、可见性回调及所有其他标签相对顺序不变。
- 卸载插件会移除补丁并恢复原位置；真实卸载/重载已验证：卸载后通知在前、Decky 在底部，重载后 Decky 回到通知上方。
- 不修改 Loader EXE，不安装 Quick Tab 或其他插件，不添加常驻 DOM 观察器、排序轮询或全局 CSS 视觉排序。
- 生命周期收据使用有界弱引用（最多 32 个）；淘汰活收据前恢复旧数组。补丁结构/枚举不匹配时不安装排序，保留调节功能并显示提示。
- **兼容边界**：这是针对锁定 Loader 私有接线的适配，不是 Steam 官方公开排序 API。未来 Loader/Steam 结构变化需要重新验证；未宣称 CSSLoader 完整共存已通过。

## 其他插件源码核查

- 核查 `moi952/decky-quick-tab` 主分支，固定到提交 `1b5dd9dd8a27033e9e3f6e39ef5e60b1fd38bb6c`。其 `src/patches/qamTabPatch.ts` 在真实 QAM 的 React 标签数据层处理顺序，不靠 CSS 对图标换位置。
- 同时只读检查了既有归档中的 Decky Loader、PowerControl 和 CSSLoader 源码。
- 上游下载仅作为审计证据存于 Build，**未执行、未安装、未捆绑其插件代码**。本轮排序实现是自己的窄范围适配；现有 Loader/API 许可证保持不变。
- 上游文件及来源记录：`G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Sidebar\validation\MAINLINE18-UPSTREAM-SOURCE`。

## 验证证据

均位于 `G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Sidebar\validation`：

- `MAINLINE18-PLUGIN-PRESENTATION.json`：16 项生产 TSX 契约检查，原宿主内存快照。
- `MAINLINE18-QAM-VISIBILITY.json`：14 项真实锁定版可见性 Hook/生产客户端惰性检查，包括手柄菜单转窗、取消、超时和 ACK 后释放。
- `MAINLINE18-QAM-PLACEMENT.json`：10 项生产排序 helper 与原版 DFL patch-stack 检查，涵盖单入口、恢复、他人补丁、未知结构、其他插件相对顺序及有界资源。
- `mainline18-web-regression.txt`：完整主线镜像/原 EXE/原 Fan 生命周期/loopback/打包规则/Vue 类型检查/Vite 构建/插件新鲜度回归。
- `MAINLINE18-QAM-ACCEPTANCE.json`：真实 Steam、实际原生控件，4 个滑块、1 个手柄下拉、2 个开关；实际 tab999 在通知前。通过原生鼠标输入设置超线程 `on` 和手柄 `elite`，原 EXE 内存保存共 **2 次**，关闭重开后保留。没有 fan preset 请求，也没有 Fan 保存。
- `MAINLINE18-NATIVE-UI.json`：C++ 自行发现、身份校验及凭据注入；非 JavaScript 注入。业务边界仍完全是内存宿主，未启动产品 YMCC EXE。
- `MAINLINE18-QAM-INITIAL.png` / `MAINLINE18-QAM-PREVIEW.png`：首次实际渲染的截图，证明名称、控件类型和顶部入口；**不是最后一次开关/手柄已选状态截图**。后续截图接口超时，未误报截图成功。
- `MAINLINE18-QAM-ATTEMPT1..4.json` 保留工具失败/恢复过程：截图超时、CDP await Promise 被回收、最初查找了错误的菜单窗口。最终根据真实主窗口里的菜单完成选择；过期菜单没有产生额外保存。
- 真实会话的 600 秒上限触发了 fixture 清理，控制器退出码 0、watchers 0；Steam 另经 `Steam.exe -shutdown` 正常关闭。**本轮不把 fixture 的期限清理当作 Steam 退出观察器自动停止 Loader 的新验收。**

## 保持与资源

- 73 个选定真实配置指纹未变，未替换安装版，未操作 TDP/Fan 硬件。
- 344 个被跟踪原文件中，SteamView 只发生两处显示名称替换；另观察到 4 个非本任务的并发图标修改，已单列记录并保持，未覆盖或回退。不能声称“全部原文件均未变”。
- Steam、Loader 和测试控制器无残留；运行时解包文件 0。
- 外部交付仍是 1 个 Loader EXE、0 个新 DLL/辅助 EXE；脚本 17,769 字节，6 个外部文件合计 **14,054,897 字节**。相比 Mainline17，脚本增加 3,446 字节，外部合计增加 3,440 字节。
- 未制作正式发布 ZIP；未重新替换安装版/执行产品 EXE。

未完成的真实验收：物理手柄输入、系统实际睡眠恢复、真实硬件接管、CSSLoader 完整共存。
