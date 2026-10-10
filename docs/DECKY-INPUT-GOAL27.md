# 活动目标进度：Decky 全链路 → 屏幕触摸板鼠标穿透/移动闪烁

日期：2026-10-10（香港时间）。目标仍为完整原目标，未完成/未缩小范围。

## 当前部署及真实状态

- 正常退出旧 YMCC 30320，经原 WM_USER+6/app.exit 退出入口备份更新到 Bootstrap26，再进行真实 Steam UI 验收。
- Steam 33856 通过官方 -shutdown 正常退出（启动前确认无运行中的 Steam 游戏）；YMCC 进入 waiting-steam，所有自有 Loader 消失。重新启动 Steam -gamepadui，Steam 当前 PID 7244，实际控制台重新连通。
- 编译并安装 Input27 启动竞态补丁及对应 Web 前端。当前真实 YMCC PID 24720；安装 EXE SHA256：7EA43B103B328FF2F530441BF922BEFFB9A4A6244BAC640F8C65B38319A88C73。
- 当前真实 Loader 18228（父）/28004（PyInstaller 子），均是正常的一个自有 Loader 进程树，不是两个独立 Loader。日志确认 verified-shell-primary / child-token-verified。Steam 不属于 Loader Job。
- 真实产品重载验收再次 PASS：ACTUAL-PRODUCT-READONLY-ACCEPTANCE.json；当前控制台有真实独立 AC 帧率及四个全局触摸板入口。配置 yeman-settings.json 在该只读验收前后哈希相同。
- 实际标签 DOM 顺序复核：100019,0,3,4,5,7,6,999，控制台排第一，原 Decky 入口保留。截图已生成，但截图的合成帧与 DOM 标签位置可能不同步，不能只凭截图判断顺序；FINAL-PRODUCT-TAB-ORDER.json 记录实际 DOM 顺序。
- 保留用户 YEMAN_POWER_CONTROL_DIR 指向原活动数据根，没有配置迁移或清除变量。

## 本轮修复

1. 上轮共享 JS 上下文事件接线已在真实产品证明有效；没有 JavaScript 测试凭据替换真实产品连接。
2. 实际 Steam 重启暴露：进程出现时其 CDP listener 可能尚未出现，原 Runtime 对 live-debug-unavailable 不开有限重试。增加失败自测，旧代码确实失败；将该临时原因加入现有 1/3/8/20 秒窗口，未新增常驻轮询/端口扫描。取消窗口和永久网络暴露错误仍拒绝。
3. UI 对该有限启动等待不再要求用户重复重启/开关。
4. 原生测试七组 16+27+26+33+24+32+28 = 186 PASS；产品及 UI controller 编译/链接 PASS；类型检查、Vite、诊断 8、bootstrap 表达式 9、原客户端 29 PASS。
5. 测试监管器的退出后 stdin 持有问题修复：native worker/Job 已终止后，Node 关闭自己的 stdin。仅旧的、已确认 native controller 终止的自有 Node 22908 做了精确身份校验后清理；未终止 Steam 或其它 Node 服务。管理员 PowerShell 读取 Node UTF8 JSON 必须显式 -Encoding UTF8。
6. 原生构建日志改为显式 UTF8，避免 Windows PowerShell Tee-Object 附加 UTF16 导致不可读。

## 真实 Steam 可写菜单验证（业务/设备边界内存隔离）

不是伪称真实硬件已操作，所有设备 IO、LS 文件/进程和业务文件写入均被内存夹具隔离；实际 Steam/DFL 控件、当前产品脚本、原生 Runtime/Broker/bootstrap 和原业务适配器参与。

- MAINLINE22-REAL-UI-ACCEPTANCE.json：13 步 PASS。核心向下菜单大核为主第一/仅小核最后、四个陀螺仪预设与取消焦点、变速八选项、首次专属整套记忆/去掉全局、电源末端 3 秒去抖两次移动只提交一次、菜单等宽对齐。
- MAINLINE23-REAL-UI-ACCEPTANCE.json：10 步 PASS。真实滑条 AC/DC 独立值、上限/不锁帧/恢复、四个全局触摸板下拉及条件映射、无游戏仍可全局设置、整套专属 RTSS 数据，未写入触摸板 EXE 方案。
- GOAL27-EXTRA-OWNER-UI.json：3 组 PASS。SMT 原 EXE 保存；LS 开/关调用精确原 quickapp owner，原生成列表 XML 不变、只一次原开启和一次身份确认的正常关闭；风扇开/挡位/关调用原 FanHostLifecycle 和 NoIoHost，交还 lease，仍是全局，不成为 EXE 设置。
- 普通和管理员 Loader/来源校验沿用原安全契约；所有临时测试 Loader 已随自有 Job 清理，真实产品已恢复。
- Fan 第一轮夹具错误（调用不存在的 observeFeatureCapability）已保留 ATTEMPT1 证据，纠正为原 recordFanHandshake 的内存观测语义后重新跑；不是通过改产品 owner 让测试通过。

## 仍需继续的工作（不能宣称完整目标完成）

- 对当前 Input27 真实安装程序再做一次冷启动 Steam 的竞态复核（前轮真实开关已通过，但当时是 Bootstrap26；新逻辑的稳定同会话迟到 listener 已由精确 Runtime 测试覆盖）。
- 对可写操作拒绝/失败后的实际 DFL 控件回滚再验收。第一轮 Fan 夹具失败时曾观察到原 owner active=false 但控件临时保持 checked=true，应以新的可控故障试验确认是否为真实 ToggleField 乐观状态回滚问题，不能忽略它。
- 屏幕触摸板输入 HWND 鼠标穿透尚未实现：tools/SCREEN-TOUCHPADS.md 明确说明仅绘制层穿透，输入层保留 WM_POINTER 通路。
- 鼠标移动闪烁尚未修复/实机验收。重点看 native/screen_touchpad_cursor.h：cursorSyncInfo 在 CURSOR_SHOWING 分支先 handoff/hide，再处理 hCursor==NULL 的缓存。它可能与“NULL 时保留旧图像”的注释不一致；需先新增失败用例并用实际 Windows 触摸/鼠标事件复现，不只改一个延迟常数。
- 鼠标穿透验证必须使用不同线程/进程的底层目标窗口，证明真实鼠标点击到达底层、真实触摸仍到达触摸板；不能用转发 click 或只看绘制层的透明样式替代。
- 不改 UIAccess/UAC/服务权限，不让 Loader 提权，不强杀游戏。新输入修复应先用自有 Win32 窗口/合成触摸来验证，再接入产品，保留原 InputHost/BUS 单写者和 HWND/GDI 所在线程约束。

主要产物目录：G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Input27\validation。
