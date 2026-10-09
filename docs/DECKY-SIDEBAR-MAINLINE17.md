# YMCC Decky 侧边栏：主线离散滑块（Mainline17）

日期：2026-10-09（Asia/Hong_Kong）。唯一维护源码位于主线，不维护隔离源码副本。

## 页面调整

- 六项游戏字段：交流电方案、电池方案、核心调度、超线程、手柄、陀螺仪，全部使用 Steam 原生 `DFL.SliderField`。
- 全局风扇仍是一个开关、三个预设；风扇预设也改为原生离散滑块。
- 将原有 `choices` 顺序映射为整数索引，`step=1`、`validValues=steps`、`minimumDpadGranularity=1`。不新增业务档位，不改原 EXE 配置格式。
- 标题/字段文字 12px、当前值 11px、状态说明 11px。左侧字段名不挤压换行。紧凑间距仅作用于 `.ymcc-decky-sidebar .ymcc-decky-slider`。
- 调节区放在游戏名称之后、状态说明和 Steam AppID 之前。
- 删除弹出下拉及其 45 秒会话保活。QAM 隐藏即断开，不保留虚构菜单会话。
- 滑块提交携带渲染时的原会话、代际、revision 和 EXE 身份；延迟事件不得跨游戏/重绑定误写。越界、分数、无变化、无当前值、不可操作状态均拒绝提交。

## 置顶的边界

1. **YMCC 页内调节区：已置于顶部。**
2. **Decky 插件列表：固定版本 Loader 提供 `pluginOrder` 和 `sortPlugins`；可通过 Loader 插件设置排序。此次没有改写已有 Loader 用户偏好，也没有以只有一个插件的测试声称多插件排序通过。**
3. **Steam 快捷栏中的 Decky 总入口：固定 Loader 的 `TabsHook.render` 使用 `existingTabs.push(tab)` 追加，没有插件侧公开排序参数。此次没有修改 Loader 二进制、重排 Steam 原生标签或增加全局 DOM/CSS 补丁。总入口未置顶。**

排序结论来自已锁定的原厂源代码快照：`SteamDeckHomebrew--decky-loader/decky-loader-75563316f9119ee7e36be7f43885be65e877fad0/frontend/src/components/PluginView.tsx`、`components/settings/pages/plugin_list/index.tsx` 和 `tabs-hook.tsx`。原生滑块参数来自归档的 `@decky/ui 4.12.1` `src/components/SliderField.ts`。这些归档只读，不作为隔离开发源码维护。

## 验证与交付范围

证据目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Sidebar\validation`。

- `MAINLINE17-PLUGIN-PRESENTATION.json`：13 项生产 TSX 契约检查，使用原宿主内存快照；不是实际 DFL 布局验收。
- `MAINLINE17-QAM-VISIBILITY.json`：12 项可见性检查，真实锁定版上下文 Hook 和生产客户端，惰性 React/socket；无菜单保活。
- `mainline17-web-regression.txt`：完整主线镜像、EXE 模型、风扇生命周期、loopback、原源码保持、打包规则、vue-tsc、Vite 和插件产物新鲜度回归。
- `MAINLINE17-QAM-ACCEPTANCE.json` / `MAINLINE17-QAM-FINAL.png`：真实 Windows Steam QAM、7 个原生滑块、0 个下拉，测得 12px 标签和 4px 上下内边距；通过 CDP 原生鼠标输入选“仅小核”，关闭并重开侧栏仍保留。
- `MAINLINE17-NATIVE-UI-FINAL.json`：受控新内存宿主中只有 1 条 `game.setField(corePolicyMode, only-small)`，原 EXE 内存保存 1 次，停止后 watchers=0、控制器退出码=0。C++ 原生发现/注入，未通过 JavaScript 写入凭据。传输重绑定不是系统睡眠验收。
- 探索性 UI 会话与受控新宿主分开记录；不把探索性会话的保存次数混作“一次明确输入只保存一次”的证据。
- `MAINLINE17-INVARIANTS-AND-RESOURCES.json`：344 个受保护主线文件和 73 个选定真实配置指纹均未变化，额外核对的 25 个安装目录配置也未变化；Steam 正常退出，Loader 随之停止，运行时解包残留 0。

唯一外部 Loader 仍为 13,992,293 字节。插件脚本为 14,323 字节，比前一版增加 572 字节。6 个外部交付文件（Loader、脚本、描述文件、许可证）合计 14,051,457 字节；没有新增外部 DLL/辅助 EXE，没有重新制作正式发布 ZIP。

**未完成的真实验收：安装版 YMCC 硬件接管、物理手柄方向输入、真正睡眠恢复、CSSLoader 共存、Steam 总入口置顶。此次未启动产品 YMCC EXE、未替换安装版、未写真实方案、未操作真实 TDP/Fan。**
