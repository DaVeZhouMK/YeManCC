# 触摸板的公共 Steam／睡眠接入（2026-11-13）

屏幕触摸板及专用按键不维护自己的 Steam 会话或电源事实。Steam 身份来自 `g_steamSettingsObserver.identitySnapshot()`；菜单继续使用原 `nativeSteamMenuButton`、`nativeSteamMenuGameRequest` 和公共传输。未知账户仍可发送功能键，不改变焦点、不补键盘快捷键。游戏关联通过公共 `game_steam_context`；控制器光标消费者另保留公共 `steamGameLinkGet()` 的有效、当前、精确 native-primary 及游戏／Steam 进程出生校验，历史关联本身不授予隐藏资格。

原生公共 PowerLifecycle 是唯一电源来源。输入使用纯内存 `captureSharedPowerLifecycle` 准入；排队动作、QAM 票据和唤醒恢复捕获完整 48 字节 `SharedPowerV1`，执行时调用 `sharedPowerLifecycleCurrent`。同 generation 的通知序号、suspendSerial、阶段或准入变化也会撤销旧请求；单独 publicationRevision 更新不取消健康请求、不退款。

唯一公共投影读取完整共享快照，包括 NonReady 的真实 suspendSerial。`SteamControllerCursor::power(const SharedPowerV1&)` 占原 bit4，`screenpads::ownerSharedPowerChanged(const SharedPowerV1&)` 占 bit64，Lossless 保留 bit32。触摸板不再直接消费 WM_POWERBROADCAST。若 NonReady／Ready 被合并，递增 suspendSerial 仍会撤下旧输入资源；启动后原发布入口提供首次当前事实。每次 publication 只交付一次已尝试消费者，新 publication 停止旧后缀。

输入准入关闭时拒绝新触摸、移动和按钮按下，原有键／点击／虚拟触点仍可释放。触摸板恢复沿用最多三次修复、五秒窗口和两次健康检查；重复 Ready 不重开额度。控制器光标保留原输入否决及期限，延迟电源投影不能抹掉鼠标操作。没有新增线程、轮询或定时器，也没有提高触摸板刷新率；停用触摸板的 snapshot 保留原原子量快速返回。

2026-11-15 后继：SD 光标策略19完全取消 SD 的软件光标补画，包括 CURSOR_SUPPRESSED、触点形状桥接和离手后 retained。只有明确的直接 Mouse 触点可以使用原补偿；纯 SD 不启动光标观察或 BUS 轮询，切回 SD 撤下旧位图，迟到消息也不复活。`cursorDeckSoftwareCursorEnabled=false`、`cursorDeckContactShapeBridgeEnabled=false`，force版本3。Steam／Windows 自身光标仍由原生决定，输入报告、方向、点击、震动和鼠标穿透沿用原路径。当前53项定向回执及旧头反例见同证据根 `sd-cursor-owner-20261115`，尚未部署。

后续现场已更新：用户在 ROG `20261115-115123` 确认 SD 不再强制出现鼠标，原件位于 `sd-cursor-owner-20261115/field-success-20261115-115123`。上段“尚未部署”只描述当时源码验证阶段。随后五份 Win5／ROG 导出仍为策略19，补画查询、显示、上传均为0；Win5 `184416` 有两次唤醒健康检查通过，窗口恢复错误为0。源端 `nativeTransportReady=false` 仍有记录，它表示虚拟输入目标当时未准入，不能用窗口健康代替目标就绪，也不能扩大为触摸窗口故障。

2026-11-16 CPU 后继：BUS 快照仍先核公共电源、原互斥锁及当次虚拟目标，再对空触点／禁止输出提前返回。持续原生滑动不读取时钟，仅活动点击读取时间，保持60ms点击和延迟首帧至少一次按下。没有新增镜像状态、线程或定时器；手势、震动、光标及刷新率保持。定向原件在 `recent-audit-cpu-20261116`：首轮26项有效／4项夹具准备错误、修正后仅续4项及一项无输入观测、原16项按键和5项非法选择分别记录，不拼整父通过。该后继已入主线源码，整套构建、部署和掌机CPU降幅尚未验证。

证据位于 `Testing/YMCC/Evidence/Touchpad-Wake-Recovery-20261111/shared-steam-power-20261113`，显式隐藏修正另见同级 `cursor-hidden-policy-20261113`。各 epoch 分别有 owner53、菜单 JS148、排队／QAM14、光标12、追加 native-primary6、公共投影7及原显式隐藏35项回执，不拼成默认完整父组。撤掉共享校验的控制在两条旧请求断言失败。最终完整 main 翻译单元编译通过，未链接整套产品、未部署，游戏画面和真实睡眠效果仍待匹配版本实测。
