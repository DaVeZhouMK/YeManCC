# 正式组件与手柄导航回归

复用既有 Inner-Pages-Import 的真实组件/engine与内存IO夹具，固定在仓库内。`Run-Ui.ps1 -EvidenceDir <新的源码目录外证据目录>` 会将夹具复制到本轮目录，构建当前源码、运行1280×720/800导航、再执行原tools/gamepad_ui_release_gate.mjs，最后归还自建server。全局入口以Windows Job约束整个子进程树。

夹具只负责明确的内存数据/系统边界。具体业务能力由各领域真实消费者回归；这里验证实际组件、交互与正式导航engine。新产品接口需要同步维护内存夹具的明确合同，缺导出时构建失败。源码或夹具漂移、导航失败、缺回执、清理未确认均不能通过。

Playwright复用现有Codex依赖，可通过YMCC_SIM_BROWSER_RUNTIME_ENTRY指定已有runtime的require定位文件；缺依赖保持失败，不安装或降级成静态检查。无需运行掌机AI或额外安装验证包。

2026-10-19：加入滚动稳定性回归。真实指针点击覆盖十页及设置/启动/监控分区、下拉选回与滑块提交；焦点丢失后的方向/A恢复须留在当前视野，弹窗取消保持底页位置。仅路由改变按既有规则回到标题。检测同时检查裁切外壳的滚动偏移，避免只看app-content遗漏整窗位移。

2026-10-19：全页导航增加滚动、体感及开启保护场景后，两次公共运行在持续推进且尚无断言错误时被旧240秒进程总预算终止；controller.ui-navigation总预算调整为480秒。保留每次操作的原超时、全部场景及断言、源码稳定性和自建server清理门；这只改变离线测试进程预算，不改变YMCC控制行为。

2026-10-20：现有 `controller.ui-navigation` 维护完整工具声明，固定 Node、PowerShell、esbuild、Playwright 与 Edge，共2302文件、33完整目录，沿用3072上限。登记枚举只读清单和校验路径，运行时在编译前后核对全部字节及目录成员；保留实际CJS、构建输入及自有浏览器退出记录。原有界面、导航、源码门和自建server清理保持；server原Kill(true)不算自然退出。

可复用合同：`node --test tools/ymcc_sim/global/ui-navigation/tool-inputs.test.mjs`。完整界面测试仍用现有global入口选中 `controller.ui-navigation`；用清单指定的绝对Node与PowerShell启动，提前清除Node/.NET启动注入环境并固定PowerShell模块目录。合同测试里的浏览器对象仅检查拒绝条件，不是浏览器执行证据。完整PowerShell包声明不等于逐个外层已加载程序集的观测；OS加载器和原生轮盘host/Overlay/TDP/所有权链仍需对应验证。

2026-10-21：共享工具文件固定为清单中声明的绝对工具路径；产品及夹具源码仍取所选源码根。修复冻结副本只绑定少数Node别名时缺少`.pnpm`相对路径的问题，542文件、29目录和esbuild入口只改变路径写法，2302文件哈希、33完整目录检查和3072上限不变。无需复制整个node_modules，也不回退到未登记文件；缺文件、字节或目录成员变化仍拒绝运行。


2026-10-22：恢复设置→设备的正式 BatteryChargeSettings 消费入口。此前单独组件/Controller回归通过仍漏掉父页面已不再挂载组件的断点。全页回归新增 settings/battery-charge 与 /settings/device 必需状态，执行真实父页→组件→bridge，内存端口只替代硬件传输；覆盖读取、选择不写、显式应用、返回再入不重放和非活动分区停止查询。删除父页消费组件必须在 SETTINGS_CHARGE_ENTRY_MISSING 断言失败；这不替代真实 G3E 固件读回验收。
2026-10-22：G3E/Xbox全屏相关陀螺仪模拟回归接回 gyroNavigationCases.mjs → gyroFullscreenCases，默认全页 verify 同时执行。使用同一正式 Vue 模拟窗口和完整生产 engine，内存边界独立控制 nativeWidth（运行模式）、DOM焦点、正式窗口/电源事件、已准入运动贡献与手柄状态。覆盖原生窗口已接管但 document.hasFocus=false、短暂DOM blur、真实native blur、隐藏/最小化/睡眠及不自动续玩、safe-zero不提升为运动、FPS瞄准/开火、普通H5焦点规则。旧源码必须在正向右转样本下失败（原版错误走自动行驶并向左）；这只证明软件路径的前后对照，不证明Xbox模式是日志实机故障的唯一原因或Steam/游戏已消费。原故障ZIP与本轮证据放在任务目录，不写入夹具或产品。

2026-10-22：G3E 唤醒日志的直接反例为原生 Ready 后前端仍 powerReady=false。gyroPowerRecoveryCases 通过独立的 __uiPowerFixture 只读端口与实际 startUiPowerReconciliation 根入口、生产手柄串行准入队列重放；电源事件与原生状态独立变化。覆盖漏最终提交、迟到同代 pending、跨代旧事件/快照、真实睡眠不可释放、恢复不自动续玩、无电源提交/输入重启/硬件写入。720/800 每档四项为发布门必需用例。正式 lifecycle.ui-power-recovery 执行生产 uiLifecycle 与 App 接线检查，覆盖乱序、无效状态、inputReady/hardwareWritesAllowed、事件合并及根卸载取消。H5 预览仍只提供内存业务边界，不能据此宣称 G3E 实机或游戏消费已通过。

2026-10-22：lifecycle.ui-power-transport 复用正式 api.ts、ipc.ts、uiLifecycle.ts，只有 WebView 消息边界为内存端口。覆盖原生事件分发、恢复时 pending 被拒绝、实际 IPC 限时、旧 id 回复、跨代状态以及根卸载；禁止所有非 power.lifecycle 命令。与 UI 全页回归和 lifecycle.ui-power-recovery 配套，不替代物理 G3E 或实际游戏消费验收。

2026-10-23：设置动态预览改为直接显示壁纸，无壁纸使用可暂停的轻量渐变动画。backgroundPreviewCases 在 720/800 执行正式 SettingsView、实际组件与完整 engine：拖动期间不落盘、松手保存、pointercancel 回退、手柄 A 进入/保存、四方向调节、B 取消、肩键不切走、居中与隐藏/切页资源释放。新增检查写入原 release gate。视频样本仅供明确选择的测试场景，加入既有只读 server 资源白名单与源码/载荷哈希。同步当前开机模式页的折叠通用设置入口，并复用正式 performanceSchedule 的纯转换函数；未把缺失 API 降级为空实现。此回归不替代安装版 GPU 或实物手柄验收。

同轮全页验收发现当前开机模式页的独立通用设置 scope 可进入却无法返回上方模式卡。PowerView 显式共享页面 scope，并把 details summary 纳入既有手柄焦点；其他 SettingsSections 默认隔离不变。通用服务/工具状态继续做全页可达和可返回检查，入口改用正式 A 操作，不删失败或缩小受测范围。

2026-10-23：原四驱赛车小游戏由原创轻量暴力摩托替换，清理 live race.ts、旧方向盘组件、旧赛车引用和四项旧赛车自测；FPS 靶场保留。新的 motorcycle 模型/绘制/车把通过 gyro.motorcycle；gyro.preset-save 覆盖原生推进版本后的同编辑对象有界重读和拒绝跨游戏写入。gyroPresetSwitchCases 在真实 GyroMotionView 内注入 native CAS 冲突、持有写入并连续选择预设，要求最终为用户最后一次选择；旧页面须失败。默认720/800导航同时验证X实际命中AI、RT加速、LT刹车、B返回与场景切换。原有电源/全屏/safe-zero检查转接到摩托实际输出，未把旧检查置空。历史 frozen B43 夹具继续引用自己的旧快照，不重签为当前产品通过。
