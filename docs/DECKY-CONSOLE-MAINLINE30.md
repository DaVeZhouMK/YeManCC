# Decky 六模块恢复（Mainline30，未部署完成）

2026-10-10，Asia/Hong_Kong。活动目标已变更：触摸板鼠标穿透/移动闪烁保持暂停；只修 Decky 缺项。不得继续运行 MousePassCrossProcess 或 RealTouch 光标实验。

## 用户目标与已确认根因

用户要 TDP + 锁帧 + 风扇 + LosslessScaling 一键插帧 + 虚拟手柄 + 触摸板；不是为了短单页删功能。现场生产 PID34024 / Native5DAE... / plugin8055...，真实 Steam 已连接，运行游戏0，控制台只有5个下拉，notice为“YMCC 当前没有确认游戏”。

旧渲染把 TDP/电源挡位、LS、虚拟手柄放在 snapshot.game 条件中，无游戏则整块消失；不是页面长度剪裁。

## 当前源改动

- 六模块恒定顺序、恒定标题（data-ymcc-module: tdp,frames,fan,lossless,virtual,touchpads），连接/无游戏/游戏三状态都保留。未知数据说明待读取，不虚构业务值。
- TDP模块沿原电源挡位，显示原各档W数、当前AC/DC。无游戏接原全局调度，有游戏仍原专属；停止切换3秒后提交，期间切换重新计时。
- 新 global.setField 只接受 acMode/dcMode/padPersona，无游戏才允许。原调度队列/原输入CAS服务拥有实际写入；无新硬件writer/policy。输入CAS带queued isCurrent guard；拒绝游戏出现、锁定、源/修订变化、电源恢复/跨侧；Private admission 在Relay WeakMap不公开。
- Native broker、TS Relay、client、host 同步扩展协议；不能只部署前端。
- Global虚拟手柄沿主线人格/映射/陀螺仪联动、SteamDeck处理JoyXoff、清runtime shortcut。状态未active只说已保存/待原输入服务确认，不假称设备已启用。
- settingsRepository增加事件订阅onInputSettingsChanged，仅原持久化commit实际input变化时通知，无轮询。
- LS入口一直可见；未确认游戏时禁用并解释，不编造目标EXE。有XML而无当前Profile时允许on，仍由原setLosslessScalingEnabled -> oneClickFrameGen创建/启动；off仍拒绝无原Profile或非本EXE的会话。空XML/状态不明确仍禁用。
- 风扇保持原全局owner，触摸板保持全局，不加入专属。核心/SMT/gyro/变速留在六模块后的高级调节，不删旧功能。

## 验证与产物

根 G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Console30。
- Global host/relay/adapter 10 PASS（GLOBAL-CONTROLS.json）：真源代码、仅IO内存边界，无EXE、无帧率写；侧拒绝/源变化/游戏出现/电源/无效值均拒绝。
- Presentation 21 PASS（新增六标题三状态恒定、无游戏global调度/虚拟、原3秒延迟）。Frame/touchpad21、client31、power debounce16通过。
- Vue type-check通过。
- Vite build成功，Build\App\Web；保留原大chunk/混合导入告警。
- Native compile/link成功，Build\App\Native\YeManCC.exe，产品候选尚未运行。
- Source-built七组Native16+27+26+33+24+32+28=186 PASS，UIcontroller编译成功（允许根仍为Input27，勿传Console30 fixture home绕过身份allowlist）。
- 新插件 Build\Plugin\dist\index.js SHA256 25061018A0C226DD5B779381B0EC824C95BB7580AEB8FA278ED973040B0E363C，34366 bytes。

## 仍需做，不能mark complete

1. 核对实际TDP模块原电源挡位是否满足用户语义；不应擅自用假数值或另起TDP writer。当前仍原档位+W，不是另一个手动W滑条。
2. 用当前匹配native controller/current插件的真实Steam内存fixture验证六模块，尤其游戏->无游戏、当前AC/DC、3秒global调度、global虚拟CAS、LS无Profile的一键原owner。不得只沿用旧GUI测试。
3. LS原owner现有memory fixture默认已有profile；需要补无当前profile但XML有效的真实原owner回归。
4. 所有源检查/Native Broker实际global命令链路正负例需补齐；目前旧七组通过但不等于新global端到端已通过。
5. 匹配Native/Web/plugin scoped备份部署，保持原活动数据根不迁移，普通权限第三方Loader。实际产品只读六模块截图/下拉/滚动、各必需项可达/合理禁用原因及配置hash前后验证。
6. source里还包含Input29的native cancel scope/qam placement尝试（源码0C6C候选旧全量Escape不稳定且未部署）。六模块版本实际验收可能继续暴露该问题，保留失败而不偷换直接调用。
7. 当前真实产品未退出/未替换，仍34024，Steam25164。上轮旧restore scripts PID/Hash均不能直接照执行；先重新验证live身份/旧安装pin。异常测试finally必须清自有Controller/Loader并恢复产品，不能留用户断连。

## 本回合分类

Progress：根因确认、六模块新源、全局原owner协议、正负例、原生/Web/plugin成功构建。编译会话33986与7775已拿到exit0，不再重启/等待旧句柄。


## 2026-10-10 12:17 本轮实证更新（目标仍 active）

- 发现并修复真实 Steam 重启漏判：ActiveProcess.pid 保持0、账户/根不变时原 observer 的快路径永久漏发现。原 Win32 回归明确失败；现有共享 observer 增加已验证 Steam 路径/进程出生时间的 top-level window creation/show 事件唤醒，并在原线程泵消息，不增加周期扫描、不写 Steam 注册表。42项绿色，空闲CPU0ms；Source-built Native七组在原 allowlisted Input27根复验186项通过。Console30根的自测拒绝为路径护栏，不放宽其身份白名单。
- 源Y改为 native SVG，真实选中/未选中CSS与标签尺寸吻合。最新安装包SVG实际截图已看见垂直居中。
- 原 native bootstrap + Broker + DFL + memory业务 owners 的六模块实际交互14项PASS：含全局TDP末次切换后3秒仅一次提交、global native whitelist、原input CAS、独立global AC FPS、AC/DC切换、触摸板全局、四gyro预设、原fan生命周期、原LS启停、三次hide/reopen。支持取决于原persona，专用/背部保留但不支持时禁用，不冒充缺功能。
- 控制台实际测试改用稳定DOM边界采样，避免用旧100ms硬延迟向滚动中的过期坐标发鼠标。保留原失败证据；原native业务fixture2的启动和第一次菜单取消验收没有额外gyro mutation。
- 完整匹配 Native/Web/plugin 已scoped备份部署：真实产品PID28648，Native DB41D59FBFC76F0EC41218757E7AEE4CAE2565809122494A444E6D8ADE47F82C；plugin 7AD547E6B936B8B38199732FBAD25F821C14D7AC2087358666914994AAB5A347。活动根保持原G:\YeManCC-Work\Mainline\Temp\ai-cpu-bundle-R11-rebuild\PowerControl；默认C:\SOFT\YeMan\PowerControl的同名插件也同步。文件部署前后业务配置SHA相同；原生自有Loader从管理员YMCC经 verified-shell-primary / child-token-verified 降权，未提高第三方权限。
- 真实已安装产品只读10項PASS：六模块、统一下拉、nativeY中心、cancel exact focus、三次重开、插件单独reload无重复Decky主页、共享页reload后新的原生认证回执/真实快照、配置hash前后相同。切换Steam后contextGeneration会从新CEF context重计；验证已改为本次明确reload后的新receipt/ready快照，不能跨旧context取最大值。
- 真实Steam已正常关闭/重启：30772 -> 31048，YMCC PID28648始终未变；原运行版的waiting-steam漏接不再出现。新Loader30808（另有PyInstaller子32608）来自原活动根，verified降权，六模块复验再次10項PASS。
- 实际截图：Build\Tasks\YMCC-Decky-Console30\validation\PRODUCT-ACTUAL-Y-AND-MODULES.png。Steam CDP surface screenshot超时不代表进程停，改用已验证Steam SDL foreground的真实屏幕截取并裁剪侧栏；不绘制替代图。
- 新用户空资源目录矩阵已通过 exact production inspector +真实SHA：缺资源拒绝、六个捆绑资源、首次Steam-debug标记前置条件、准备后的ready。Release分类规则通过。**这不是完整新用户installer执行通过**；还需完整发布/安装路径验证和最终scope审计，不能mark complete。
- 触摸板鼠标穿透/移动闪烁实验继续暂停；本轮未继续任何跨进程mouse-pass/real-touch probe。
