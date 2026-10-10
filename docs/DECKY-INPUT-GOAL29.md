# 活动目标：Input29 进度与可直接继续的证据

日期：2026-10-10，香港时间。完整目标未完成，未缩小。上一目标回合为 progress：修改原生取消作用域、原生标签所有权、增加失败基线与实际验收证据；不是单纯状态复述。

## 真实产品已恢复；失败候选未留在安装目录

- 当前真实产品 PID 34024，C:\SOFT\YeMan\YeManCC\YeManCC.exe；Native SHA256 5DAE15C55FA50B8BCE2041A280FDFE58D3DDA74BEF2D3012A13C54A4653E7A4B（含 Input28 cursor fix）。
- Steam 25164，未在本回合重新启动。所有产品退出通过 QQ 主窗口 WM_USER+6，未强制退出 Steam、产品、游戏。
- 最终实际生产插件自动回退到最后验证的 Input28：8055FAFE1E3BBB6644E7CB07E38961F364CCB6FF5A104EABE773BD888403DA2C。Input29 尚未全套通过，不宣称已部署完成。
- YEMAN_POWER_CONTROL_DIR 仍为 G:\YeManCC-Work\Mainline\Temp\ai-cpu-bundle-R11-rebuild\PowerControl；没有迁移、清除环境变量或重置业务配置。
- 当前实际产品只读/真实共享页重载重新 8 PASS，验证采用本目录 PRODUCT-RESTORED29.json 的真实 PID。当前实际只读键盘探测：屏幕触摸板菜单普通 CDP keyDown/keyUp Escape 关闭并回到 combobox，无业务 mutation；CURRENT-KEY-DELIVERY.json。

## 原生取消作用域

1. 原 Source Root 是普通 div，取消仅存在条件挂载的 listbox。真实追踪 FOCUS-TRACE.json：选项有焦点，Window 级 keydown 已被 Steam preventDefault，未到 React Root key capture；取消由原生 SDK 处理。
2. 最初尝试 useLayoutEffect 提前聚焦，确定性模型通过，但实际 Console22 与 Console23 均失败。该方法已撤回；同步 DOM focus 模型不是充分的原生焦点树证据，相关错误要求已从测试移除。REJECTED-LAYOUT-UI-RESULTS.json 保留拒绝证据。
3. 当前 Source 改为常驻、focusable=false 的 DFL.Focusable scope；已注册的稳定 onCancel 回调通过 latest props ref 判断是否打开。关闭状态返回 false，让 Steam 的正常返回继续；打开状态取消本菜单。仍用原 useEffect 在原生选项注册后聚焦，不添加轮询/定时器。
4. 新原生作用域 4 回归在旧代码失败，当前 anchored dropdown 10 PASS。
5. 原生作用域候选 ABA0... 实际 Console23 完整 AC/DC 帧率 + 全局触摸板 10 PASS 一次（GOAL29-SCOPE-CONSOLE23-SESSION.json，servedProductionScriptSha256 必须核对）。之后同候选 Console22 就绪失败；不能说稳定、全通过。

## 标签旧内容/旧 Provider 与晚卸载

- 通过完整 pinned upstream TabsHook + pinned patcher 复现：same-count reused native array 保留上一模块 Content；新模块正常卸载再重装还会重复追加默认 Decky home。NATIVE-TAB-OWNER-BASELINE.txt 是真实 primary-source 自测失败。
- 当前 G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\decky-plugin\src\qamPlacement.ts 增加 beforePatch：只在本插件条目缺失/旧内容时，用当前 native prototype render 在未挂载 scratch array 建立自己条目的当前 provider；只将自己的条目放回实际数组，不修改 foreign/Decky home。
- 注册描述对象保留 identity；旧 handle.dispose 只移除自己的注册与自己的 content receipts，不移除新模块同 ID 的注册。正常其它 before/after patches 保留。
- placement 12 PASS，包含新旧 owner/provider 及重装不重复 home、晚卸载不移除新 owner。

## 当前 Source 和失败范围

- 当前源/任务插件 dist/index.js：0C6C84A3356C9E9BE490DA97395A034F8848FB81A26F6729ADE651168E4BDAA4，30936 字节。Source package 位于 G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Input29\Build\Plugin，未作为当前生产安装版本。
- 当前 Source JS 132 PASS：client31、visibility15、diagnostics8、presentation19、placement12、debounce16、frame/touchpad21、anchored10。Vue type-check / own diff-check PASS。
- 最终 OWNER 候选连续四组：Console23 与 Console22 实际 Escape 仍不稳定，菜单打开且 activeRole=option；Owner SMT/LS/Fan 3 PASS、LS failure rollback PASS。FULL-MEMORY-UI-RESULTS.json 与 GOAL29-OWNER-*-SESSION.json 保留，全部控制器原入口正常退出 code0。
- 不能用一次 Scope Console23 10 PASS 代替当前 0C6C 全量通过。下一步应在同一失败 Console23 样本记录普通 keyDown、先 keyUp 清状态、rawKeyDown、只观察 Window keyboard 事件（不要修改 React props）；必要时比较真正 Windows SendInput。已有 FOCUS-TRACE 添加观察后通过，但未建立这与标准送键失败的因果，禁止继续无依据更改聚焦模型。

## 鼠标穿透准备（未执行、未实现）

- 已新建并编译 G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\screen_touchpads_mouse_pass_cross_process_probe.cpp。
- EXE：G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Input29\MousePassCrossProcess\probe.exe。
- 自有另一个进程窗口、原生产 header、实际 SendInput / InjectTouchInput、dryRun 禁止触摸板向应用发键鼠。四种测试：生产 baseline、无条件 HTTRANSPARENT、layered WS_EX_TRANSPARENT、透明 layered + CLIENT touch hit testing。
- 尚未运行，不能声称哪一种通过。应先完成 Decky，按原目标顺序再执行跨进程验证；不得把同线程透明、绘制层透明、转发点击或 UIAccess 提权冒充实现。
- 现有 Input28 cursor fix 已部署且 104 模型 + 73 Windows touch/GDI PASS，仍需受控真实安装版验证。

## 继续工作入口与安全恢复

- 证据根：G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Input29\validation。
- Native UI fixture 仍用允许的 Input27 artifact root / compiled controller，但 explicit payload 指向 Input29。管理员仅运行 native supervisor；Loader 保留 verified-shell-primary / child-token-verified、unelevated。
- 原窗口正常退出 helper 里 PID 为对应历史 PID，使用前必须从 PRODUCT-RESTORED29.json 和活进程重验证并生成新的 scoped helper；不得执行旧 PID。
- G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Input29\Build\restore-product29-admin.ps1：新候选失败时务必 -UseLastVerifiedPlugin；会用 InstalledPluginBackup 的 8055... 回退，不以失败候选替换生产。当前 guard 支持 0C6C 的候选源 pin 和原生 5DAE...。
- 所有隔离 session terminal 后再恢复真实产品；不并行运行两个 Loader owner，不终止其它 Node/PowerShell。
