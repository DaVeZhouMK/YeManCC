# Decky / 触摸板目标进度：Input28

日期：2026-10-10，Asia/Hong_Kong。完整目标仍为活动状态，未完成、未缩小。

## 当前真实运行状态

- 已恢复真实 YMCC：29532，安装路径 C:\SOFT\YeMan\YeManCC\YeManCC.exe。
- Steam 正常退出后重新启动：25164；退出前通过真实 DFL 确认运行游戏为 0。未强制结束 Steam、游戏或产品。
- 真实自有 Loader 树：3012（父）→26280（PyInstaller 子），父进程来自 YMCC 29532，均来自原活动数据根。不是两个独立 Loader。
- Native SHA256：5DAE15C55FA50B8BCE2041A280FDFE58D3DDA74BEF2D3012A13C54A4653E7A4B。
- 最终插件 dist/index.js SHA256：8055FAFE1E3BBB6644E7CB07E38961F364CCB6FF5A104EABE773BD888403DA2C，30098 字节。
- 原 YEMAN_POWER_CONTROL_DIR 保持 G:\YeManCC-Work\Mainline\Temp\ai-cpu-bundle-R11-rebuild\PowerControl；未迁移、清空或重置配置。
- 只读验收前后 yeman-settings.json 哈希同为 9fbcbfd53a4e50c566ab150d7252cf1746920aed20a3db0b43ce1bf4f4c5d93d。此结论仅指该只读验收窗口；文件部署窗口的配置哈希也相同，但不能由此声称产品正常启停过程中绝无自身持久化。

## 镜像连接提示与本轮修复

1. 恢复工作时上一轮隔离故障测试已经正常退出真实产品。Steam 仍保留旧会话，现场实际是“连接已中断”，不是要求用户输入一个网址。
2. 部署已编译的 Input28 Native/Web 和匹配插件，恢复原真实产品、普通权限 Loader、原生认证 bootstrap、原业务镜像。
3. 实际共享页重载发现旧面板残留。插件增加 pagehide 与 Loader onDismount 共用的幂等卸载：释放自己的 socket、期限、监听器、注册和弱引用数组回执，不清理其它插件。旧代码新增卸载自测实际失败；修复后通过。
4. 最终真实产品只读/重载验收 8 项 PASS：接通原产品；5 个等宽对齐入口；支持入口向下展开；Escape 回焦；不支持的入口拒绝操作；隐藏重开；真实共享页重载自动接线。重载后只有一个可见面板，隐藏残留 0，无 JavaScript 测试凭据注入。
5. 上一轮新增的操作失败 notice 保存及失败 ToggleField 定向重建已部署。真实 DFL + 原生 Broker + 原始 LS owner（仅内存 IO 边界）故障回滚 PASS，已确认拒绝后复选框恢复 false，错误提示保留，不重放操作。
6. Native 日志位于 C:\Users\DaVe\AppData\Local\YeManCC\decky-sidebar.log，记录资源、启动阶段/错误码、bootstrap 上下文、peer、快照，不记录 token/endpoint 内容。测试 CDP 也增加关闭/错误即时报告与保留原调用栈，避免连接已关闭却只等到无调用位置的超时。

## 已通过的当前源/版本验证

- Native 七套：16+27+26+33+24+32+28 = 186 PASS。
- JS：client 31、QAM visibility/lifecycle 15、diagnostics 8、presentation 19、placement 10、power debounce 16、frame/touchpad 21 = 120 PASS。
- Vue type-check、插件构建与自己修改范围的 diff 检查通过。
- 最终插件真实 Steam/DFL 内存业务 Console22：12 项 PASS，包含核心顺序/向下菜单、陀螺仪/焦点、变速、专属不返回全局、电源尾沿 3 秒延迟、统一宽度。
- 最终插件真实 Steam/DFL 原始 SMT/LS/Fan owner：3 组 PASS，仅内存文件/设备/进程边界。
- 最终插件真实 LS 失败回滚 PASS。

## 屏幕鼠标光标修复

- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\screen_touchpad_cursor.h：系统报告 CURSOR_SHOWING 但 hCursor 为空时不再误交接/隐藏已有缓存。有效形状才开始交接；明确隐藏仍尊重；正常抬手可保留已有光标。
- 新案例在旧代码失败，修复后模型/GDI 104 PASS，真实 Windows InjectTouchInput + GDI 73 PASS。该 Header 已编译进当前安装 Native。
- 没有修改 ShowCursor 计数、全局钩子、驱动或增加常驻定时器。

## 仍未完成，不可宣称全链路已经通过

1. 最终插件 Console23（AC/DC 帧率 + 全局触摸板的完整可写交互）复验尚未通过。早期样本因 CDP 连接关闭失去调用位置；改善日志和就绪观测后，最新样本明确停在第一个屏幕触摸板菜单的 Escape 取消，菜单仍开，未进入其后 AC/DC 写入步骤。不能把 Input27 的 10 项通过沿用为 Input28 的通过。
2. 正确的隐藏/重新打开时序、等待真实当前 native/DFL 控制台连续稳定 3 秒并未消除此问题。保留失败证据，不重复无依据重试；后续应直接查真实 options DOM 焦点、捕获键事件及 Steam focus tree，区分测试送键与实际菜单逻辑问题。
3. 输入层“仅接收触摸、鼠标穿透”仍未实现。已有不同 UI 线程的真实 Windows 实验：GetCurrentInputMessageSource/extra 在 WM_NCHITTEST 中均不可区分鼠标/触摸；无条件 HTTRANSPARENT 未让鼠标到达目标，且丢失 touchpad pointer。该实验没有部署到产品。
4. 仍需在真实安装版的受控窗口验证光标修复，以及不同进程目标的鼠标/触摸双向验收。不以绘制层透明或转发点击冒充鼠标穿透。

## 证据

全部最终证据在 G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Input28\validation：
- PRODUCT-RESTORED28.json
- ACTUAL-PRODUCT-READONLY-ACCEPTANCE.json / ACTUAL-PRODUCT-AFTER-RELOAD.png
- FINAL-NATIVE-SELFTESTS.txt / FINAL-JS-SELFTESTS.txt
- PAGEHIDE-BASELINE.txt
- FINAL-CONSOLE22-UI.json / FINAL-CONSOLE22-SESSION.json
- FINAL-EXTRA-OWNERS-UI.json / GOAL28-SETTLED-OWNERS-SESSION.json
- FINAL-TOGGLE-FAILURE-UI.json / GOAL28-FINAL-FAILURE-SESSION.json
- CONSOLE23-REVALIDATION-INCOMPLETE.json / GOAL28-SEQUENCED-CONSOLE23-SESSION.json
- CursorBaseline、CursorFixed、CursorRealTouch、MousePassProbe（该最后目录的 unconditional-experiment.json 是修正注入压力后的有效实验）

所有最新隔离控制器已由 stop-request 原入口退出，terminal code 0；真实产品已恢复。不得再拿旧产品 PID 或 terminal fixture 验收当前链路。
