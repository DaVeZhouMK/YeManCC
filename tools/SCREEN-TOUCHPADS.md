# 屏幕触摸板（主线）

2026-10-16 当前状态补注：本文2026-10-11章节中的“默认关闭／双重opt-in”描述为当时历史状态。当前native/build_native.bat默认编入独立Touch/Pen source，正常owner初始化默认请求该route，显式legacy/conflict可关闭。18:47微星归档的导出状态报告nativeMousePassCompiled/Requested/Active均true、runtimeError0；仅为导出时点，不是故障同段或闪烁验收。当前证据见[原生入口隔离测试R36](../docs/screen-touchpad-native-ingress-simulation-r36.md)。
- 触摸反馈提供“关闭／最小震动反馈／中震动反馈／强震动反馈”。最小档保持原 10% 请求力度，中档 20%（+100%），强档 30%（+200%）；新配置／缺失字段默认中档，明确保存的关闭或任意档位保持不变，四个配置档独立记忆。三档均沿用 18ms 脉冲、短边 5% 距离刻度、45ms 最短起震间隔；MSI A1M 仍为原有 0/193 开关量，不能保证三档振幅。每触点固定状态 O(1)，无新线程或轮询，不积压补震。鼠标穿透保持已验收行为不变。详见 `docs/screen-touchpad-haptic-tiers-20261011.md`。
- 2026-10-11 新增事件驱动的显示栈恢复：当前台 topmost 窗口重叠并位于控件之上时，成对恢复触摸输入层／边框及功能键，不抢焦点、不重建输入源、不重复上传边框。游戏式窗口隔离复现通过，不等于实际游戏／独占全屏验收，未覆盖安装版；实现、固定构建基线及最终证据见 `docs/screen-touchpad-lowcpu-and-game-visibility-20261011.md`。

- 自动隐藏：关闭／15秒／30秒／45秒／60秒／120秒，默认 60 秒，四档分别保存。触摸板和专用按键共用闲置计时，超时完全不可见，触摸原区域恢复保存的显示透明度，持续按住不隐藏。YMCC 呼出键与背部按键维持原状。自动隐藏不改保存透明度、不停止输入区域、不改变鼠标穿透。源码和未验证边界见 `docs/screen-touchpad-auto-hide-20261014.md`。

入口：控制器 → 四个输出控制器档位下方 → 屏幕触摸板。四档都固定显示，使用“关闭／双触摸板／单触摸板”下拉菜单。新配置／缺失档位的预设：SteamDeck 默认双触摸板，左右均为 SteamDeck 原生映射；PS5 默认单触摸板，映射为 PS5 原生触摸板；关闭虚拟手柄与 Xbox 档默认关闭。已保存的关闭状态和自定义配置不被新预设覆盖。

关闭虚拟手柄、SteamDeck、PS5、Xbox 四档各自记忆布局、左右映射、单板映射、透明度、缩放和鼠标灵敏度；保存在现有 yeman-settings.json 的 screenTouchpads.profiles 中。旧的单份配置仅迁入 SteamDeck，不复制到其他档；旧版 enabled=true 升级为双板，enabled=false 为关闭，保持已有左右映射／透明度／缩放，profile schema 为 3。切档时释放触点，未完成的保存仍写入原档。

- 双板左侧可选当前档位的 SteamDeck／PS5／PS4 左触摸板 (Steam内设置)、WASD、方向键、鼠标、关闭。原生映射仅在对应虚拟 target 开启时可选。
- 双板右侧可选当前档位的 SteamDeck／PS5／PS4 右触摸板 (Steam内设置)、鼠标、关闭。PS4 复用 PS 档配置，但显示 PS4 名称和对应能力。
- 单板只有一个底部居中的圆角横向长方形和一个映射选项：WASD、方向键、鼠标；PS5／PS4 已开启时可选择对应原生触摸板。同一 HWND 支持两个独立触点，第三指不替换现有触点。PS5／PS4 同时支持双板：两个输入 HWND 分别映射同一硬件触摸板的左右半区，不是两块独立硬件板；点击仍共用一个按压位。
- 单板比例参考 Sony 编写的 Linux hid-playstation DualSense 有效触摸坐标 1920×1080（16:9），不是未经测量的外壳毫米尺寸声明。100% 高度与之前方形触摸板一致，宽度按比例增加。底部改用当前显示器工作区作为整组三排的缩放基准：专用键底边在任务栏上方固定留 12 像素；单板水平以底边中点对齐、双板水平以各自底部外角对齐，并共同为下方专用键保留高度及间距。
- 左 WASD／方向键 + 右鼠标可同时使用；键盘支持斜向，中心死区释放按键。
- 鼠标相对移动、轻点单击；SteamDeck 模式轻点产生短点击，滑动不附带点击。无底部按住条或拖拽按下功能。
- 横向边距仍按原值增加 200%（24 → 72 像素），Y 呼出键的上边距仍为 120 像素。底部布局不再以屏幕底边固定减 120 像素：整组专用键底边固定在 MONITORINFO.rcWork.bottom - 12 像素，背键／触摸板随之向上排布；触摸板与上方背键／下方专用键的两段垂直间距均从 20 → 40 像素（本次 +100%），随 50%～200% 缩放。背键左右间隙仍为 10 像素并随缩放，不加倍。
- 新增“触摸板缩放”滑条，默认 100% 沿用之前的基准大小，范围 50%–200%、步长 5%，支持手柄／键盘操作且不加速跳档；单板和双板复用同一缩放滑条，四档独立保存，旧配置缺少 scale 时默认 100%。尺寸变化前释放触点及模拟按键，使用缩放后的完整区域归一化触摸坐标，边框仍保持单像素细线，十字与接触光点之外保持透明。
- 默认透明度 80%。显示 YMCC 蓝色单像素圆角边框与中央细十字线（双板正方形／单板横向长方形），无文字和背景填色；接触时显示随手指移动的青蓝色光点与明显晕光。透明度作用于这些视觉元素，按下时沿用可见度 ×1.5、上限 100% 的反馈规则。输入 HWND 与不可交互、不激活、鼠标穿透的逐像素 Alpha 显示 HWND 分离；边框与十字同图缓存，光点单独缓存。图案以外为 ARGB=0，首次上传成功后才显示。100% 透明时全部视觉消失但仍接收触摸。全部 HWND 在原主程序内。2026-10-13 的十字/光点修改尚未运行验证，详见 docs/screen-touchpad-stall-cpu-contact-20261013.md。
- 只有 SteamDeck 虚拟手柄开启时才允许 SteamDeck 左右触摸板映射；其他三档可独立使用键盘／鼠标。SteamDeck 模式输出独立左右坐标、触摸／点击状态和合成压力；效果由 Steam Input 布局决定，不声称具备真实力感或触觉硬件。
- 对应档位中，虚拟手柄未开启时仍显示原生预设的中文名称，但该菜单项禁用，触摸浮层仍受已有原生目标许可控制；非对应档位不提供原生映射。
- 2026-10-11 鼠标点击穿透已接入正常构建和默认 owner 启动：触摸／笔由独立受控输入源接收，普通鼠标点击直接命中触摸板下方的窗口，输入区域和边框均不挡鼠标，不转发／重放点击。保留 `--screen-touchpads-native-input-legacy` 和构建环境 `YEMAN_SCREEN_NATIVE_INPUT_CANDIDATE=0` 作为旧路径回退。当前交付验收按用户澄清的普通鼠标点击要求；静止双指并行鼠标不再作为该交付前置，也未被宣称硬件通过。详见 `docs/screen-touchpad-mouse-clickthrough-20261011.md`。
- 不向本程序自己的设置窗口发送模拟游戏按键。先切换到目标窗口后操作触摸区域。
- 原生触摸浮层在现有 YeManCC.exe 内；报告提交仍由现有 InputHost 完成。没有额外功能 EXE。

## 2026-10-08 14:22 需求落地

- 正式启用 PS5／PS4 双触摸板左右原生映射菜单，不使用模拟鼠标替代。新 PS 配置继续默认单板；切换成双板后，缺失档位预设的左右映射均为原生触摸。已保存的键鼠／原生混合映射不会因关闭、单板、双板来回切换而被重新覆盖。
- 新配置／缺失档位的专用按键默认“都开启”：SteamDeck 为 Steam＋三点（mask 3），PS5 为 PS＋静音（mask 3），Xbox 为 Guide（mask 1）；PS4 共享 PS 配置但仅显示／输出 PS 键。关闭虚拟手柄档仍关闭专用键；YMCC 呼出及背键仍默认关闭。
- 只为缺失字段或缺失档位提供默认值；已保存的 mask 0、旧 specialEnabled=false、以及只开一个键的配置全部保留，不在加载界面时改写设置。
- 底层的同一 BUS writer、目标开关许可、触点释放及中性帧规则保持不变。本批没有实机 Steam 游戏消费验证，没有安装／更换驱动，也没有覆盖安装版。

## PS5／PS4 原生通路

屏幕 WM_POINTER → 现有两触点纯状态机 → 现有 BUS 快照 → 原生 FocusFrameContent → 优化 pipe JSON → Host 严格解析 → 既有 BuildDualSenseDataOnlyReport → SubmitRawReport。DS 点坐标写入 data-only 报告 32..39（两个 4 字节触点），点击写入 data[9] bit1，抬起置 contact bit7；不会把原生触摸替换成 Windows 鼠标，不改变摇杆／扳机／IMU／Edge 背键。原生触摸输出由 SteamDeck、DualSense／Edge、DualShock4 personas 接受，Xbox 拒绝触摸扩展。PS4 使用既有 BuildDs4V2DataOnlyReport，1920×942 触摸坐标、data[32] 包计数／data[33] 时间戳、data[34..41] 两触点、data[6] bit1 共用点击；PS5 使用 1920×1080 坐标。双板归一化限制左侧 X<0、右侧 X>0，编码后分别位于 0..959 和 960..1919；单板仍可使用全宽。切档、切布局、缩放、目标关闭时释放接触和点击；不新增线程、EXE 或轮询。

协议参考（开发者文档）：https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c ，DS_TOUCHPAD_WIDTH／HEIGHT、dualsense_input_report、dualsense_touch_point 和 DS_BUTTONS2_TOUCHPAD。调试证据保存一份原始参考文件。网页协议与消费者对照不能替代本机 Steam 或游戏的实际消费观察；自动测试不替用户启动／切换生产手柄。

## 性能边界

原生 WM_POINTER / 独立输入源事件驱动，固定两触点状态；只发送键盘按下／释放边沿和有效鼠标增量。触摸层不增加线程、周期轮询、低级输入钩子或 WebView；自动隐藏使用最多一个按需到期计时器，到期即撤销，隐藏后不计时；未启用/不可用的原生快照提前返回。开启时复用现有 BUS 单写者，在同一次提交的目标锁内校验 Host 就绪和 epoch；故障或会话变化撤销旧触点，恢复后重新落指。原生触点全部松开时不附加触摸字段。本 target 首次合法 FRAME／NEUTRAL 前完成 SDK idle 的取消／等待交接，此后普通手柄与屏幕输入统一由现有 BUS 提交原始报告；新 target 会话重新交接。边框与十字位图仅尺寸变化时重建，按下/松开和透明度变化复用源图调整常量 Alpha；MOVE 不重绘整板，接触光点只在整像素坐标变化时移动。位移判定比较平方，独立输入源直接使用事件自身时间戳。UI 滑条沿用 180ms 合并保存；不轮询 IPC。本轮未测量这些变更的 CPU 净收益。

## 安全测试

控制器页面不再提供“模拟触摸自测”，也不注册该公开 IPC；保留现有 EXE 的开发回归 CLI，避免删除验证能力。

`tools/verify-screen-touchpads.ps1` 还会在现有主程序中使用 Windows InjectTouchInput 生成真正的双指触摸消息，验证左右独立操作、WASD、方向键、鼠标增量、透明命中、不抢焦点、关闭释放和空闲无重绘。该测试在自己的短生命周期浮层中运行，不再创建可见的深色测试底板，键鼠输出为 dry-run；不创建手柄、不安装驱动、不修改用户设置。

InputHost 的 `--selftest-steamdeck-touchpads` 对拍 512 组随机普通帧与锁定 HIDMaestro 1.7.0 编码器，并检查触摸坐标、左右点击、压力、释放和严格解析。未使用原生触摸的会话保留原有 SubmitState 路径。锁定 SDK 的 SubmitRawReport 不更新自带空闲泵；首次触摸时通过 SHA 锁定版的三个内存字段取消并 join 该空闲泵、延续最后包序号，不修改 DLL。自测使用未初始化的内存对象和一个 BCL 测试线程验证交接，不创建设备。

构建、验证、正式打包分别使用现有主线 tools/build-workspace.ps1、tools/verify-screen-touchpads.ps1、tools/package-release.ps1。构建／打包不会覆盖 C:\SOFT\YeMan 的安装版。

## 历史验证范围（不含 2026-10-13 本轮源码修改）

- 单板的中心几何、16:9 参考比例、50／100／200% 实际窗口、同窗口双指／滑动／原生点击／释放；其他档位单板键盘映射，以及布局切换释放。
- DualSense 与 Edge 共 1024 帧触点编码及普通按钮／IMU／Edge 背键不变性；PS4 另有 512 帧半区坐标、共享点击、释放与 PS／IMU／帧计数不变性检查。原生组装 → 优化 pipe → 生产 Host 解析 → PS5 单板六帧闭环；PS5／Edge／PS4 双板另外九帧闭环。
- 四档入口统一名称、三选项布局下拉、单板只显示一个映射、原生许可过滤、手柄打开布局菜单、各档布局与映射恢复。

- 同源纯状态机、四档独立配置、逐像素预乘 Alpha 和透明中心检查；真实 Windows 触摸注入／窗口生命周期检查。
- 真实上传完成（UpdateLayeredWindow 成功）后的中心 ARGB=0，默认 80% 透明时边框 Alpha=51，100% 时边框及中心全部透明。原生回归还对两侧各 9 个真实桌面合成像素进行隐藏／显示／隐藏前后对比，验证可见中心确实透出原画面，不使用填色测试底板。
- 512 帧锁定 SDK 字节对拍；原生帧组装 → 已优化 pipe JSON → 生产 Host 解析 → HID 报告的三帧独立触摸／释放闭环。
- 实际 Vue SFC 渲染四种状态（关闭、SteamDeck、WASD+鼠标、方向键+鼠标），不使用另写的 UI 模板。
- 生产 Vue 控件和真实手柄导航引擎的浏览器回归：SteamDeck 鼠标灵敏度步进、四档保存／恢复、快速切档保存原档、原生 Deck 许可，以及移除公开自测入口。
- 现有 SteamDeck／PS／Xbox 报告和 Host 生命周期回归。
- 正式主线构建及原有优化继承门，正式完整包导出／ZIP 一致性门。
- CPU 结果是隔离原生浮层和快照热路径的测量，不是整个 YMCC／Steam 的 CPU 百分比；不能把计时分辨率内的 0ms 增量解释为绝对零开销。

以上自动测试不安装驱动、不改本机 Steam 布局、不把测试键鼠输出发送到用户窗口。实际游戏中的鼠标／菜单／视角行为仍由 Steam Input 布局决定；本轮没有替用户更改游戏布局或运行游戏。

## 屏幕专用与背部按键

- 触摸板设置提供三个等宽、手柄可到达的下拉菜单：YMCC 呼出、专用按键、背部按键，位于缩放滑条上方。YMCC 呼出有关闭／左侧呼出／右侧呼出三个选项；SteamDeck 专用按键有关闭／只开启 Steam／只开启三点／都开启；PS5 对应关闭／只开启 PS／只开启静音／都开启。背部按键只保留四个选项：SD 为 L5+R5／L4+R4／全开启／全关闭，PS5 为 LFN+RFN／LB+RB／全开启／全关闭；移除单键和混合数量的菜单选项。默认均关闭，不更改已有触摸板配置。
- 四档分别保存 `summonPosition`、`specialMask`、`rearMask`，复用原配置节，并兼容旧的 `summonEnabled`、`specialEnabled`、`rearEnabled` 开关。Xbox 与关闭虚拟手柄档不允许背部按键，关闭虚拟手柄档只允许 Y 呼出键。触摸板布局关闭时，三类按钮仍可独立开启；此时共用缩放／透明度滑条仍可用。
- 第一排左右背键、第二排原触摸板、第三排专用键。背键左右各两格，两格连同间隙的总宽度等于对应触摸板。第三排每侧只有一个专用键，宽度与单个背键完全一致，严格对齐内侧一格：左侧占右半格，右侧占左半格，外侧半格留空。按钮始终以双触摸板的几何位置／宽度为基准；切换单触摸板不改变按钮位置。按最新需求，整组三排在任务栏安全布局的基础上，再按当前触摸板高度上移 30%（小屏幕会限制上移量，确保整组可见）；触摸板为下方专用键预留高度和加倍的纵向间隙。预留的两格位置不变，但每侧只显示一个背键时，无论键名是 L4、L5、R4、R5，还是 LFN、LB、RFN、RB，都将该键的实际输入和视觉窗口移到内侧一格，与专用键横向对齐；每侧双开时恢复原来的固定左右位置。键 ID 和实际 BUS 输出位不随位置变化。旧配置中的已移除组合保留原数据，下拉显示“选择组合”提示，不在打开设置时自动追加其它按键。背键两块间隙不加倍，外沿不超过对应触摸板。小屏幕／大缩放时同时限制两行按键高度，保留触摸板实际缩放尺寸；专用键底边与任务栏顶边的距离为 12 px 加上本次上移量。工作区、分辨率与 DPI 变化复用现有窗口消息刷新，无定时轮询。Y 呼出可选左上角或右上角，水平边距为 72 px，顶部间距从 72 px 增至 120 px。
- 图标从用户给出的参考图提取为纯白 Alpha 遮罩，Y 复用现有应用图标且移除背景；SD 背键为 L4/L5/R4/R5 纯白文字。没有背景填色，只有蓝色单像素圆角轮廓和居中纯白图标。透明度同时控制轮廓与图标，100% 时仍可触摸。全部浮层仍在已有主 EXE 中，使用已验证的无重定向输入 HWND／独立分层视觉 HWND。
- SD：Steam、Quick Access 三点、L4/L5/R4/R5；PS5：PS、Mute；PS5 Edge 额外 LFn/LB/RFn/RB；PS4：仅 PS，不提供 Mute 或 Edge 背键；Xbox：Guide。原生按键必须对应虚拟 target 已开启；关闭 target 即刻阻断 BUS 快照并释放／隐藏，Y 呼出不依赖 target。
- `screenButtons` 为独立可选六位整数：Guide、QuickAccess/Mute、L4/LFn、L5/LB、R4/RFn、R5/RB。按住即按下，松手／取消／切档／关闭／缩放即释放，可与原有触摸和普通按键并存。BUS 仍为唯一提交者，中性隔离帧归零，不增加线程／周期任务／键鼠替代输出。
- Steam Deck 真实数据位：Steam 13、QAM 50、L4 41、L5 15、R4 42、R5 16；首次专用键同原生触摸一样交接锁定 SDK 的空闲泵，保持单一报告序列。PS5 data-only `data[9]`：PS bit0、Mute bit2，Edge LFn/RFn bit4/5、LB/RB bit6/7；Xbox 通过现有 Guide 状态链路。严格 Host parser 按 persona 限定可用位，DS4 只接受 Guide 位，缺省不附加字段。
- Y 在有效触摸松手时发送现有 UI-owner 消息，通过 `window.summon` 使用与默认 LB+RB 一致的前台捕获、窗口恢复／全高右侧吸附、WebView 同步和游戏识别入口；不再强制 `SW_MAXIMIZE`，避免 Windows 最大化与全高吸附布局冲突。其他既有呼出方式不改变原窗口大小行为；不模拟键盘快捷键。
- 浮层视觉窗口必须从对应输入窗口读取实际屏幕 RECT，再通过原生分层上传同步位置与大小，不能读取视觉窗口自身的初始 (0,0) RECT。回归检查四档实际显示框／触摸框相等、缩放与透明度重绘后仍相等，并从视觉框中心注入真正 Windows 触摸；Y 测试通过隔离消息接收窗口验证默认呼出请求且未携带强制最大化命令，不触发用户应用。预览必须用实际视觉 RECT，不再仅用输入 RECT。
- 三个设置菜单使用现有共享 Dropdown 控件，复用深色外观、手柄导航和视口安全弹层定位。浏览器回归覆盖 SD／PS 的全部专用与背部组合、YMCC 左／右／关闭、能力门控和手柄确认打开菜单。
- 自动回归包含状态／几何、真正 Windows 触摸注入、图标 Alpha、三个下拉菜单的手柄导航与各档独立保存，以及 native 事件状态 → BUS 快照 → 已优化 pipe JSON → Host 严格解析 → HID 的 134 个组合夹具。新专用／背键的实际 Steam／游戏行为仍需用户实机确认；用户此前确认的 SD／PS5 触摸成功不等同于新按钮已完成实机确认。

## 图标缩小与右侧素材修正

- PS5 的 PS／静音／LFn／LB／RFn／RB 图标按之前显示的线性尺寸缩至 50%；图标缩小自身不改变按钮的圆角命中范围或真实按键链路；位置按最新任务栏安全布局计算。
- 原参考图的 RFn 与 RB 必须包含完整底部轮廓。RFn 源裁切扩展至 y=325（旧 y=284 截断底部），RB 裁切至图片底部 y=730（旧 y=669 截断）；从完整源图重新生成白色 Alpha 遮罩，同时校验源图安全边距与编译遮罩／PNG 一致性。
- Steam／三点以此前展示的大图标为基准缩至 30%；STEAM 改为字体绘制，三点改为解析圆形抗锯齿覆盖，不再缩放低分辨率位图。SD 背部 L4/L5/R4/R5 文字缩至原字体大小的 80%。
- 共用 Y 改用字体绘制，按此前展示的 Y 图标缩至 70%。字体按实际墨迹包围盒重新居中，非字体图标使用双线性 Alpha 采样，避免最近邻放大锯齿。PS5 的图标安全边距／半高／中心对齐继续在原生上传路径验证。
- 背景仍完全透明；透明度同时控制白色图标和蓝色圆角轮廓，缩小图标不会缩小触摸区域。

## 原生输入分流与视觉保留层接续

- 完整跨进程鼠标穿透仍未通过：监控式透明样式会连触摸一起穿透，原生独立输入控制器已接到触摸但还不能使当前 Win32 输入 HWND 对鼠标跨进程穿透。
- 已消除同样轮廓/图标的重复透明层上传；位置变化移动缓存帧，尺寸/透明度/图标变化更新整帧，显示/DPI 事件强制重建。保持触摸许可和输入输出路径不变。
- Beta 仅作 reflection-only 静态分析，可借鉴双缓冲/dirty flag，不照搬常驻计时器。
- 当前缓存 20 项及自有窗口真实合成像素保留验证通过，不等于真实触屏闪烁或鼠标穿透完整完成。见 `docs/SCREEN-NATIVE-INPUT-AND-FLICKER.md` 和两项新 verifier。
### 2026-10-11 自有子窗口候选

新增 `verify-screen-touchpads-native-topology.ps1 -DiagnoseOnly`：全零 Alpha（非 WS_EX_TRANSPARENT）父 HWND + 自有 Composition/受控 Touch/Pen 子 HWND，22 拓扑样本中两个初步联合样本通过；原生动态 72、同步生产处理函数的动态 72（含坐标和 UI 线程检查）通过。单指静止/取消/关闭恢复通过，双指静止仍缺有效验收：普通 Win32 双指静止正对照也被合成流合并，不能用滑动样本取代。

它是隔离候选，不是生产输入适配；完整核心候选及生产 featureAcceptancePassed 仍 false。未启动虚拟目标、未转发鼠标、未开 UIAccess、未部署，Beta 保持只读；SD/PS5 无虚拟目标触控板映射限制和既有功能键独立动作不变。详见 `docs/SCREEN-NATIVE-INPUT-AND-FLICKER.md`。视觉缓存 20 与独立功能键/核心 184 本轮复核通过。
### 原生受控子窗口组件接线（仍默认关闭）

候选已整理为 `native/screen_touchpad_input_surface.h`，通过 `YMCC_SCREEN_NATIVE_INPUT_CANDIDATE` 编译选项与默认 false 的运行时请求，接入生产 refresh/cancel/shutdown；旧 WM_POINTER 和独立源共用许可/Core/output，未重放鼠标。实际组件动态 72、双面板移动/生命周期 24、映射 5、所有权 12 通过；固定源码主 EXE 构建、核心 97 / WM_POINTER 427 / wire 通过，缓存/功能键/光标回归通过。

直接静止双指正对照仍不足，14 个不移动坐标的报告/落指顺序试验不能验收成功。完整候选和生产 gate 仍 false，普通安装版未启用/未部署，按钮路由不变。复现 `verify-screen-touchpads-native-component.ps1 -DiagnoseOnly`；默认命令必须失败闭合。完整边界见 `docs/SCREEN-NATIVE-INPUT-AND-FLICKER.md`。
### 静止选项诊断与人工输入入口

注册触摸窗口/FINETOUCH/WANTPALM 8 样本、掌心拒绝/Touch-only/提前捕获 4 样本未补足静止双指；生产没有采用这些试验设置。组件新增 24 项来源观察，合成输入确认为 IMO_INJECTED，不误标为硬件。

`verify-screen-touchpads-native-physical.ps1` 默认仅编译；人在触屏前准备好后显式 `-Manual`，在自有窗口做两阶段约 20 秒的真实双指 + 鼠标对照，框外鼠标不能代替框内穿透；无输入注入/真实绑定输出/虚拟目标/安装版改动。交互路径当前未执行，不能算硬件或整个功能 PASS。详见 `docs/SCREEN-NATIVE-INPUT-AND-FLICKER.md`。
### 按钮候选接线与冷启动修复

候选覆盖七个按钮输入 Surface，复用原 buttonEvent / BUS / 独立动作 / Y owner 消息；88 个跨进程按钮样本通过，原许可与无虚拟输出限制不改。实际 Core 键鼠输出向自有接收进程验证 12 组 + 独立触摸轻点映射，不能拿这种映射 SendInput 点击冒充鼠标穿透。

修复相同配置 shutdown/reinitialize 没有重建 HWND；在透明输入上传前处理独立输入源已排队的注册/几何工作，修复首笔触摸丢失，不靠固定延时或 InputEnabled getter。触摸板/按钮零等待等 24 个冷启动样本通过，generation/尺寸回调防护失败闭合。

完整候选与安装版启用仍未验收：静止双指需人工硬件对照，`-Manual` 未执行。详见 `docs/SCREEN-NATIVE-INPUT-AND-FLICKER.md`；默认 verifier 仍拒绝放行，源码固定候选在 `native-button-input\candidate-snapshot-v2`。
### 正式 owner/startup/恢复接线（仍双重 opt-in）

`screen_touchpad_owner.h` 与正式 main 已接线：仅显式候选构建 + 精确启动参数请求新输入，默认/冲突/明确旧路径不激活，不改保存配置、Persona 或 BUS/InputHost。resume 只在 UI owner 排队一次受控源重建，epoch 防迟到/重复，原电源策略不改。

真实标准 build_native 配方默认/opt-in 均构建成功；两种 owner 夹具各 34、实际主 EXE 早返回自测共 14 次通过，旧指针 427/core/wire 通过。候选视觉 23/旧视觉 20 通过，不冒充全屏/真实触屏闪烁。人工 `-Manual` 未执行，完整目标仍缺硬件与正常产品验收；源码/构建与记录见 `native-owner` 证据和 `docs/SCREEN-NATIVE-INPUT-AND-FLICKER.md`。
### 2026-10-13 SteamDeck 触摸／按压隔离（只做静态修改，未运行验证）

SteamDeck 普通接触只发送 touch+XY，合成压力由 8192 改为 0，轻点仍发送 click+32767；新落指撤销本侧上一点按残留，避免下一滑动继承旧点击与坐标。投影缓存包含映射模式，防止切换 Deck/Sony 后复用另一模式的压力。没有新增线程／timer，也不改反馈档位、PS5/PS4、鼠标／键盘、穿透或前台焦点。

本轮没有新增原生长按点击，也没有擅自删掉轻点。Valve 参与维护的公开 HID 驱动确认触摸／点击／压力是独立信号，但不是完整 Steam Input 点击算法开源。低 CPU 长按方案和未验证边界见 `docs/screen-touchpad-touch-press-separation-20261013.md`；本轮未测试、未编译、未部署。
### 2026-10-13 触摸板按下视觉反馈

按住触摸板时，蓝色边框可见度按当前值 ×1.5，上限 100%；松开/取消后恢复保存的透明度。默认透明度 80% 即可见度 20%，按住时可见度 30%（有效透明度 70%）。完全透明仍保持不可见。它是接触的视觉反馈，与原生 Click/Pressure、鼠标按键和震动档位无关，不写回配置。

双板独立变化；Sony 单板上任一已接收触点仍按住就保留增强，最后一指 UP/取消才恢复。自有前台的仅反馈路径同样参与，关闭震动仍有视觉反馈。新透明度设置、显示恢复和分层样式修复复用同一个有效透明度计算。

只在 DOWN/UP/取消/释放或既有配置/显示刷新时同步；有效透明度未变化时不创建 GDI/重绘，不给普通 MOVE 或 BUS 添加绘制工作，不新增线程或计时器。此阶段中间区域保留零 Alpha；后续十字/接触光点见下节。现有鼠标穿透窗口样式不变。本轮按此前要求不运行测试、编译、实机命令或部署；只做源码及差异静态检查。

### 2026-10-13 卡死隔离、CPU 优化与细十字/接触光点

原生触摸跟随 Host 故障、禁写和目标 epoch 撤销旧状态，恢复后要求重新落指；旧指保留仅反馈路由到 UP，避免恢复后重放坐标或轻点。没有新增虚拟写入者或强制回中线程；驱动本身卡住后的最后报告不由本层保证清零，尚不能承诺完全消除所报告的持续漂移。

移动判定改为距离平方；边框/十字按几何缓存，按下亮度变化复用位图；独立输入源用 PointerPoint 自身时间戳，少一次 GetPointerInfo；光点为最多两张小缓存，移动不重绘整板、不新增 timer。细十字 1px、约半边框 Alpha，接触光点跟随手指并在 UP/取消后隐藏；使用已有显示层穿透/不激活约束。

重新查阅 Microsoft Holding 与 Valve Long Press 文档；本轮不新增长按、不删除轻点，不把软件长按当作 Deck 压感。没有运行测试、构建、实机输入或部署，也未测量修改后 CPU；既有测试仅调整了十字对应的像素期待。旧进程只读 CPU 观察与完整证据边界见 `docs/screen-touchpad-stall-cpu-contact-20261013.md`。

### 2026-10-13 接触光点放大与晕光增强

按用户截图中约 80px 的 Windows 触摸圆环作参考，100% 触摸板缩放下，蓝色主体直径改为 104px（约大 30%），含外围晕光直径 144px；继续跟随现有触摸板缩放。晕光从主体边缘平滑向外衰减，增强源 Alpha 至 210，主体保持 245，再乘现有按下透明度。完全透明仍不可见。保持按下显示、随指移动、抬起隐藏及显示层穿透约束。图案缓存继续复用，仅创建/尺寸改变时生成，不增加 MOVE 光栅化、动画或 timer。只改源码与说明，未测试、编译或部署。

### 2026-10-13 十字限定触摸板、按键垂直间距再次加倍

修正虚拟按键复用触摸板 outlinePixel 后误画内部十字的问题：共用的 outlineBorderPixel 仅绘制圆角边框，按键使用它保留原图标；触摸板独立在边框基础上添加十字。光点和晕光仍只属于触摸板。

触摸板与上方背键、下方功能键的间距基准均由 20px 改为 40px，随 50%–200% 缩放对应 20–80px。沿用现有工作区内布局和短屏高度限制；水平间距不变。没有新增绘制循环、线程或定时器。现有几何自测中的旧数值仅同步到 40px，未运行测试、编译或部署。

### 2026-10-14 触摸板与专用按键自动隐藏

增加“自动隐藏”下拉：关闭、15秒、30秒、45秒、60秒、120秒，缺失字段/新配置默认 60 秒，明确保存的关闭保留。复用四档配置与原保存流程。闲置超时只隐藏触摸板/专用键视觉层，原透明输入区域仍可唤醒并正常操作；保持按住不隐藏，抬起重新计时。保存透明度和震动档位不被改写，YMCC 呼出键与背部按键维持原状。

UI 所有者线程最多一个到期计时器；MOVE 不反复重设，隐藏后撤销，无 BUS 或 WebView 轮询。隐藏状态接入显示恢复和光点路径，旧定时消息按 ID 拒绝。仅完成源码与静态复核，未测试、编译、部署或测量 CPU；详见 `docs/screen-touchpad-auto-hide-20261014.md`。

### 2026-10-14 SteamDeck 滑动光标读取合并（源码阶段）

固定 SDK 1.7.0 的静态反编译确认 RawReport 不更新 idle 状态；已有的一次性停止 idle writer 逻辑继续保留。Deck 输入仍由唯一 Host 发送触摸/坐标/压力，Steam Input 异步产生鼠标移动，虚拟设备的震动输出是独立回传方向。

修正连续 Deck MOVE 每次立即 GetCursorInfo 并不断推迟 24ms BUS 补读的问题：仅合并这些 MOVE 的光标查询，让既有期限自然到期；落指/抬指/取消与直接模拟鼠标路径保留即时处理。仍为原 24ms 补读和 80ms 接管阈值，不增加 BUS/HID/显示刷新率、线程、计时器、输入注入或 MOVE 位图重建。

状态增加 cursorDeckDeferredMoves、cursorFlagsObserved、cursorLastFlags、cursorFlagTransitions，cursorPolicyVersion 为 3；仅累计已有查询观察到的状态，无逐事件日志。未测试、编译、部署、测量 CPU 或获取本轮故障现场，不能据此认定闪烁已消除。根因候选、原始资料和证据边界见 docs/screen-touchpad-deck-flicker-20261014.md。


### 2026-10-14 不连续光标信号防护与可选 Deck 轻度平滑（平滑已于同日撤销）

沿用 24ms 光标补读与 80ms 接管窗口；GetCursorInfo 失败保留旧画面但重置稳定计时，两次有效 SHOWING 采样间隔超过 72ms 或时钟回退也重新确认。flags=0 仍尊重应用隐藏。缓存软件光标位置，窗口位置通知使缓存失效，减少重复 GetWindowRect。策略版本为 4，增加查询失败与采样间断计数；没有新线程/定时器或更高刷新率。

SteamDeck 映射增加“滑动平滑：关闭/轻度”，缺失配置默认关闭。轻度使用按源时间的 One Euro 风格自适应滤波，按归一化坐标计算，共享两轴平滑系数；低速较稳、快速减小平滑，截止频率范围 30–120Hz 为算法参数，不是报告或显示刷新率。仅随原有触摸事件运行，不分配堆内存、不在 BUS 内循环插值；重复坐标保持输出，手指停止后不追赶旧目标。点击分类/震动/光点保留原始坐标，点击报告沿用最后输出坐标。重落指、取消/故障、配置切换清空滤波；异常时间或超过 100ms 间隔重新取当前点。

未测试、构建、部署或测量 CPU；不能据此宣称解决全部闪烁或还原 Valve 私有处理。详见 docs/screen-touchpad-signal-filter-20261014.md。


### 2026-10-14 根据快速滑动反例移除平滑，限制 Deck 双光标绘制

用户报告开启轻度平滑后大幅滑动明显更闪，故删除设置入口、配置输出、Pad 滤波状态与算法头文件，Deck 坐标恢复原路径。载入旧配置时只丢弃 deckSmoothing 字段，其他设置继续解析，后续正常保存不再写出该字段。没有直接编辑用户配置或替换运行程序。

检查发现原 Deck 路径在 CURSOR_SHOWING 且有有效系统形状后仍保留软件光标等待 80ms；两套光标的异步位置可能在快速移动时分离。纯 Deck 触摸现改为该有效样本到达即撤下软件光标；SUPPRESSED 才恢复补画，未知形状仍沿用安全缓存、flags=0 继续隐藏。Mouse 或混合的 Mouse/Deck 触摸保留原有宽限。24ms 查询预算不变，无滤波、预测、额外鼠标输入或后台线程。

策略版本 5，增加 cursorDeckOnlyTracking / cursorDeckSystemHandoffs / cursorDeckHandoffGraceMs=0；既有 cursorHandoffGraceMs=80 表示 Mouse 宽限。交接仍只能依据离散采样，不能保证采样之间完全无重叠或空窗；如果显隐高速交替，软件层会随所有者切换，这是需实机观察的边界。未测试、构建、部署或测量 CPU；详见 docs/screen-touchpad-no-filter-cursor-ownership-20261014.md。


## 2026-10-14 屏幕按键共用触摸反馈、按下外发光、谨慎处理报错

屏幕触摸板、YMCC 呼出、专用键和背部键共用已有触摸反馈设置；仅按下有效触点时申请一次现有短脉冲，按钮 MOVE 不追加震动。最小/中/强仍为 10%/20%/30%，默认中档；沿用现有 18ms 脉冲、45ms 最小间隔、80ms 队列期限和物理设备选择，不增加工作线程或定时器。按钮接触状态占独立位，触摸板与按钮互相松开时不会误撤销另一方的反馈。取消、关闭、布局/模式切换均释放状态；自有 UI 的映射输入抑制继续保留，物理触摸反馈独立。

触摸板和屏幕按钮按住时按原规则提高 50% 可见度，上限 100%，同时显示沿圆角轮廓衰减的蓝色外发光。100% 缩放时外侧绘制余量为 16px，跟随 50–200% 缩放。只有原有不可交互、不激活的显示 HWND 扩边；输入窗口、命中面积、间距和鼠标穿透策略不变。十字只属于触摸板。正常与按住图像在尺寸/图标变化时缓存，按下/释放切换已有位图，普通 MOVE 不光栅化、不重新绘制字体；按钮接触位掩码也仅在边沿更新。保持原自动隐藏和接触光点。

用户实测功能正常，因此本轮报错修正只处理确定的状态/提示问题：设置 IPC 在磁盘提交成功后返回 settingsSaved=true；前端不把之后的原生窗口错误描述为未保存；Windows HRESULT 使用十六进制显示。原有 refresh 重建整个窗口集合时重新累计当次错误，成功后旧 windowError 不再永久残留；当前失败依然保留，不屏蔽 E_ABORT，不因错误自行重试或提高刷新率。screen_touchpad_input_surface.h 的输入同步实现已恢复为本轮前的原始字节，不改 ProcessEvents、输入源创建/取消或正常使用流程。没有足够现场证据判断用户那次提示是否伴随实际功能故障，不能把报错直接归因于虚拟手柄卡死。

本轮仅源码编辑与静态差异审查；既有几何断言仅随显示层余量更新，未运行测试、编译、硬件输入或部署。留档：Build/Analysis/ScreenTouchpad-ButtonFeedback-Recovery-20261014。


## 2026-10-14 SteamDeck 光标显隐观察与原位置预算（policy 7）

纯 Deck 触摸增加 UI 线程 WinEvent 光标显隐通知，合并后读取当前状态；注册失败保留补读。隐藏及时处理，新的显示位置与原 24ms 周期共用预算，已显示的补画不会被显隐通知驱动连续移动。生命周期按 generation 拒绝旧消息，双触摸板交替不重装，卸载失败保留一个句柄有限清理且不重复注册。固定 64 条内存记录随原有状态导出，热路径不逐事件写文件。保留 policy 6 位置/缓存优化和原触摸输入链路。

源码已写入主线，未测试、编译、部署或测量 CPU；尚无本版实机证据，不能宣称闪烁已改善。实现与证据边界见 docs/screen-touchpad-cursor-visibility-20261014.md。


## 2026-10-14 触摸板与屏幕按键事件链 CPU 优化

成功触点事件只汇总一次Core状态，配置与路由合并读取；无键鼠变化时跳过输入数组准备，但震动先行发布。按键事件共享配置和命中结果，映射执行前保留实时目标/Profile检查。光圈普通MOVE减少重复窗口样式查询，显示/上传/抬升前及原WM_STYLECHANGED/WM_ENABLE修复仍保留；同面双触点只在单次调用内共享窗口范围。保留输入报告、光标刷新、触感、穿透和设备恢复逻辑，无新增线程/计时器。

未运行测试、编译、部署或CPU测量，不能给出占用降幅。详细变化与边界见 docs/screen-touchpad-button-cpu-20261014.md，交付留档 Build/Analysis/ScreenTouchpad-Button-CPU-20261014。


## 触摸板CPU与SD显示模拟修正（policy 8）

自身前台的屏幕按键MOVE在映射已清空后不再重复锁定/推进standalone代际；首次需要撤销、待处理动作、Y键接触及触觉释放仍保留。实际头文件模拟10,000MOVE的重复撤销从10,000降至0。纯Deck系统接管后SHOWING+空句柄不复活旧补画；UP查询偶发失败保留已有效补画并沿原24ms期限复查。49项光标、16项按键、10项事件时间与44项震动资源模拟/回归通过。无真实输入、部署或CPU百分比测量；未证明所有实机闪烁已消除。详见 docs/screen-touchpad-cpu-cursor-simulation-r1.md。


## 反馈关闭时跳过反馈专用路径的距离步进

FeedbackOnlyContacts继续维持触点/视觉/UP，但只有strength>0才执行Motion::move；默认参数保留原调用行为，反馈开启算法未改。实际头文件模拟关闭反馈10,000MOVE的距离状态更新10,000→0，25项通过；上轮按键16项回归通过。没有真实输入或CPU百分比测量；cursorPolicyVersion仍为8。详见 docs/screen-touchpad-feedback-off-cpu-r2.md。


## 触摸板CPU量化与policy8长序列验证（R3）

实际handler配合外部API替身，Before/After按A/B/B/A各7轮测量：按钮重复清理优化在隔离路径约37–38%耗时减少；触摸板feedback-off完整处理路径变化约±2.5%、样本重叠，不能确认显著加速。这不是应用CPU占用百分比。另16种Core/报告投影负载输出稳定。当前光标49原回归＋6长场景共55通过，包括36,000MOVE共享24ms预算、通知突发、双指/代际、失败恢复与延迟队列；没有新确定失败，不追加生产补丁。详见docs/screen-touchpad-cpu-measurement-r3.md。


## 原生光标layered损坏恢复（policy 9）

真实隐藏桌面复现：cursor窗口丢失WS_EX_LAYERED后，样式消息中立即补回flags仍可能让后续UpdateLayeredWindow持续error87。现保持即时穿透/不激活修复及隐藏，在下一次原有上传前用busy guard于原样式回调外对同HWND做一次layered off/on，再校验和上传。失败保留active/retained与pending沿原预算重试，成功后不逐帧重置。新增状态计数cursorLayeredResetPending/Resets/Failures。

同一原生测试Before25项4失败、After25通过；58项离线回归通过；修正后真实WinRT跨进程64轮1038断言/1600命中通过。输入桌面未切换，未注入输入、未部署，不把此异常恢复等同于用户SD实机全部消闪。详见docs/screen-touchpad-native-layered-recovery-r4.md。


## 2026-10-15 实机无改善日志与显示顺序恢复（policy 10）

170538 实机归档确认运行 policy9，未出现 layered reset/error87；用户目测无改善继续作为未通过视觉验收。旧环丢弃中途纯坐标变化，且 SetWindowPos 移动未计数，不能由零 position-only moves 或相同端点断言静止。现使用原采样更新固定64段的运动摘要，并补齐位置/抬高成功与失败计数；没有额外查询或逐事件日志。

私有桌面原生复现外部置顶恢复只抬高 input/outline，导致 contact 被边框压住、cursor 留在 external 下方。现只在既有恢复确实抬高窗口后恢复已可见的 contact/cursor 顺序，不复活隐藏层、不动几何/焦点/触点，不增加普通MOVE抬高。最终同夹具 Before25项四失败、After25/25；光标70/70；完整WinRT64轮1038断言/1600命中及12项源码边界检查通过。未部署、未测量实机CPU百分比，未证明该缺陷解释全部滑动闪烁。详见 docs/screen-touchpad-field-flicker-stack-r5.md。


## 2026-10-15 新接触光点与静止补画光标顺序（policy 11 / R6）

实际WinRT宿主+两个自有下层进程的组合夹具确认：UP保留光标后重新落指、另一块Deck板落指，光点成功抬高其owned组，光标XY缓存却未失效，静止时可留在光点/输入组下。现只在光点show/raise成功且补画已可见时使原位置缓存失效，由紧接着的原同步恢复顺序；普通MOVE路径不变。四种尺寸同夹具Before90项中八项顺序失败，After90/90，112原生命中通过；四组400稳定MOVE没有额外光标查询/位置提交/恢复抬高。既有70光标、25外部顺序、64轮1038断言/1600命中与12源码边界回归通过。

夹具在专用STA worker清理Composition/DispatcherQueue，确认队列完成后COM退出，main join后CloseDesktop；未确认清理的结果不会通过。未部署、未注入真实输入或测量CPU百分比；policy11仍待实机消闪验收。详见 docs/screen-touchpad-deck-recontact-stack-r6.md。


### 2026-10-15 R7：触点几何缓存与热路径 CPU 调用

根窗口 RECT 改为 owner-thread 事件失效缓存，可见性仍逐次检查；移动/缩放/DPI/生命周期/强制刷新保持重新读取，失败保留原隐藏和重试。实际 WinRT 私有桌面同夹具142项，Before恰好8项复用失败，After142/142；800次稳定MOVE根矩形查询降为0，光标查询/上传/定位次数保持。此为CPU调用量证据，不是进程CPU降幅或SD闪烁改善结论。入口 `tools/screen_touchpad_geometry_cpu_native_selftest.ps1`；详情 `docs/screen-touchpad-root-geometry-cpu-r7.md`；证据 `G:/YeManCC-Work/Build/Analysis/Touchpad-Geometry-CPU-R7`。


### 2026-10-15 R8：Deck显示通知放在本帧报告尝试之后

稳定Deck active/retained的原BUS显示请求从snapshot前置移到SUBMIT_FRAME尝试结束并离开传输锁后，消除通知先于该帧提交的窗口；Mouse/混合模式保留原位置。24ms、pending、报告/映射/震动/UP/cancel不改；UI→BUS模式选择用atomic，普通MOVE不重复原子store。policy12/submitOrder1，geometry1/evidence1保留。时序模拟Before准确4项失败、After19/19；原有70光标、142原生、64轮1038断言1600命中及源码8+12项通过。Host回执不代表Steam已合成鼠标，尚无实机闪烁改善结论。入口`tools/screen_touchpad_report_order_simulation.ps1`与`tools/screen_touchpad_report_order_sourcecheck.mjs`，详情`docs/screen-touchpad-deck-submit-order-r8.md`；证据`G:/YeManCC-Work/Build/Analysis/Touchpad-Submit-Order-R8`。


### 2026-10-15 R9：原生UI/BUS队列与窗口消息CPU

真实私有桌面消息队列20/20验证R8通知顺序、200次BUS机会合并及取消后不再查询；contact窗口仅在需要的消息分支读取GWLP_USERDATA，800次MOVE区间2388→0次读取，原生163/163且绘制/查询/定位计数保持。policy12/submitOrder1/geometry1/evidence1不变。直接Mouse路线保持；这些是隔离调度和CPU调用量证据，尚无现场闪烁改善结论。入口`tools/screen_touchpad_native_queue_selftest.ps1`、`tools/screen_touchpad_contact_message_cpu_selftest.ps1`，详情`docs/screen-touchpad-native-queue-cpu-r9.md`；证据`G:/YeManCC-Work/Build/Analysis/Touchpad-Native-Scheduling-R9`。


### 2026-10-15 R10：policy12实机系统0/1交替与低CPU来源取证

新19:35包确认policy12仍闪；保留266ms连续activeDeck63个系统flags0/1样本、62次交替，helper全false。主线policy13只在active helper已隐藏且原deadline未到时跳过无法改变画面的查询，不延长恢复；重放72→18调用且无绘制变化。原回调增加64项定长来源TID记录，显式导出至多8个去重线程的当前PID归属并声明lateResolution，不冒充隐藏API调用方。最终15项、70光标、163原生、20队列、19提交与1038断言1600命中通过；未声称现场闪烁改善。详情`docs/screen-touchpad-field-flags01-r10.md`；入口`tools/screen_touchpad_hidden_cursor_budget_selftest.ps1`与`tools/screen_touchpad_hidden_cursor_sourcecheck.mjs`；证据`G:/YeManCC-Work/Build/Analysis/Touchpad-Flicker-Field-20261015-193544`。


### 2026-10-15 R11：WinRT NULL class cursor原生反证

真实私有桌面/WinRT子父窗口分派WM_SETCURSOR，没有复现NULL child清空光标：原class保留HAND/主动NULL；设置私有child class为ARROW的正对照确实改变形状，确认消息链生效。初始8项错误假设失败保留，最终202/202；不实施无证据的child WM_SETCURSOR拦截，生产仍policy13。fixture确实在私有线程调用SetCursor测试形状并恢复，不注入输入/移动指针/切换显示桌面；实际现场自动路由与Steam消费仍未验证。入口`tools/screen_touchpad_setcursor_route_selftest.ps1`，详情`docs/screen-touchpad-setcursor-route-r11.md`；证据`G:/YeManCC-Work/Build/Analysis/Touchpad-Cursor-Route-R11`。


### 2026-10-15 R12：SD 静止落指的旧坐标补画

23:16新包确认policy13仍闪，最后64条中15段SUPPRESSED采样无运动却显示旧箭头，随后系统SHOWING开始移动。policy14只让新SD补画在既有采样观察到实际OS坐标变化后开始，静止DOWN/UP不显示旧箭头；授权跨失败重试保留，非SUPPRESSED含SHOWING+NULL清理，已有visible及Mouse/mixed保留。无新增查询、轮询/线程/定时器或输入，24ms/提交顺序不变。最终70光标、15预算、19提交、20原生队列、167原生消息、146原生几何、64轮1038断言1600命中通过。新的压缩现场端点回放入口为`tools/screen_touchpad_motion_gate_selftest.ps1`；它不恢复未记录帧，不是实机视觉验收。详情`docs/screen-touchpad-stationary-cursor-r12.md`，现场及回执`G:/YeManCC-Work/Build/Analysis/Touchpad-Flicker-Field-20261015-231639`。未部署，不能宣称全部连续滑动闪烁已解决；按键按用户指示等另一AI证据。

专用对照最终59/59；冻结policy13保留14项新规则失败。80个已知端点模型中旧箭头显示/隐藏15/15→0/0，不能外推实机闪烁比例或CPU百分比。最终回执和完整范围见上述R12文档。


### 2026-10-16 R13：普通SD MOVE提交前采样旁路与CPU

policy14的普通MOVE尾部UI请求仍可抢先消费位置槽。policy15在BUS提交后观察路径活跃时把普通MOVE显示留给BUS；无活动或48ms门槛后保留原24ms UI兜底，新手势/模式切换/Stop清理活动状态。复用Core已有dispatchTick，不改取时位置或输入值，稳定MOVE省重复状态写入。原API替身26项Before准确4项差异失败、After26/26；真实私有队列38项Before准确5项新增失败、After38/38且原20项保留；3000MOVE额外取时3000→0，不外推CPU百分比。70光标、15预算、59静止门槛、167原生触点消息、1038断言1600命中、12输入边界通过。入口`tools/screen_touchpad_report_order_simulation.ps1`、`tools/screen_touchpad_native_queue_selftest.ps1`；详情`docs/screen-touchpad-move-submit-order-r13.md`；证据`G:/YeManCC-Work/Build/Analysis/Touchpad-Move-Order-R13`。未部署，真实Steam消费与现场整体闪烁仍未验收。按键仍等另一AI证据。


### 2026-10-16 R14：完整原生主程序集成与新版现场资格

本轮无生产补丁。冻结281个来源，按主线参数实际编译main.cpp+dsu_server.cpp并链接x64 Windows GUI主程序，资源和管理员manifest比对通过；触摸板来源与R13 policy15完全一致。产物仅供集成验证，未启动/部署，非完整前端/helper发行包。新23:30与00:06归档均policy13，不能判定policy14/15实机效果。详情`docs/screen-touchpad-native-integration-r14.md`；编译回执`G:/YeManCC-Work/Build/Analysis/Touchpad-Integration-Build-R14/receipt.json`；两包只读复核`G:/YeManCC-Work/Build/Analysis/Touchpad-Field-Policy15-Audit-20261016/REPORT.md`。整个现场闪烁验收未完成，等待新版实际运行证据，未接管按键/监控/风扇。


### 2026-10-16 R15：已有完整发布包的policy15资格与retained恢复边界

只读确认主线0.0.35完整ZIP构建/导出身份和native EXE匹配，10个触摸板头均包含当前policy15；无重新打包/部署。6项隔离检查：隐藏retained60秒2500次原预算查询、无绘制；松手16ms后suppressed坐标变化而无visibility事件时，原24ms观察可恢复，event-only模型漏掉；Stop后125000次BUS无取时/查询/绘制。保留恢复行为，无生产补丁，不声称CPU降幅或实机闪烁结果。证据`G:/YeManCC-Work/Build/Analysis/Touchpad-Release-And-Retained-Review-R15`，说明追加在R14集成文档。


### 2026-10-16 R16：本次Contact已知形状的显示交接

09:42新包确认policy15仍闪。三次Contact已返回有效箭头但仍SUPPRESSED，旧的静止门槛禁止补画，到首个SHOWING记录间隔93/125/63ms。policy16允许本次Contact确认的形状在当前OS坐标桥接，NULL首触点/独立Periodic或Visibility不新授权，SHOWING/flags0/Stop/模式与隐藏新手势清理；上传失败不翻出不同旧缓存。24ms/提交后观察/48ms MOVE兜底保持，无新计时器、滤波或输入。独立75项Before59/75且16项确实失败，After75/75；70/15/26/59、38原生队列、167触点CPU、1038断言1600穿透命中通过，298来源完整原生隔离编译通过。压缩端点显示决策改善不等于逐帧或整段实机闪烁验收；未部署/重打完整ZIP。详情`docs/screen-touchpad-contact-shape-r16.md`；证据`G:/YeManCC-Work/Build/Analysis/Touchpad-Flicker-Field-20261016-094240`。


### 2026-10-16 R17：转查SD数据链，停止追加显示策略

13:46现场确认policy16仍失败，合并74条中62次0/1转换对应helper隐藏；用户强调直接Mouse正常，优先查SD数据/消费。10000帧native快照→生产严格解析→编码及512 stock codec对照通过，不等于实机输入正确。锁定SDK同步复制caller并新建后端帧，排除静态64B数组被后续Clear通过引用污染；GET_REPORT读_lastInputReport，非旧extended缓存。生产仅扩展现有1Hz被动SDK读取：最多复制64B并输出LastSteamDeckPadReport的counter/touch/click/XY/pressure，锁忙省略，无新线程/轮询或输入；辅助窗口上下文每会话最多两次，display policy仍16。新增26上下文/26缓存观察、70/15/26/38回归、完整Host和303来源native隔离编译通过。未部署；整体闪烁未解决，1Hz缓存采样也不证明Steam消费或排除短暂抖动。详情`docs/screen-touchpad-data-chain-r17.md`；证据`G:/YeManCC-Work/Build/Analysis/Touchpad-Flicker-Field-20261016-134659`。


### 2026-10-16 R18：真实SDK私有内存取帧与逐提交边沿

继续SD数据方向，显示/协议不改。实际锁定SDK SubmitRawReport/TryReadInputFrame绑定私有362B块，21项、2万串行/5万并发写入均无坏帧，未构造设备或进入socket/InputPumpLoop。新增已有日志门内的逐raw成功返回统计：32边沿环、expected与wire触摸/click/XY/pressure比对、counter、缺口/溢出与最新报告，经原1Hz窗口输出steamDeckPadSubmissions；补齐R17纯采样漏短off的盲点，无每帧分配/I/O/取时、新线程或频率变化。24纯统计、9完整Host实际内存提交、512codec及producer143/连续性226各Release+Debug通过，完整Host编译通过。0x81/82无额外mouse/key生产；GET_REPORT旧缓存晚于新interrupt的条件交错待实机请求证据，未改SDK。详情`docs/screen-touchpad-sdk-backend-r18.md`；证据`G:/YeManCC-Work/Build/Analysis/Touchpad-Data-Backend-R18`。未部署，实机闪烁未验收。

R18补充：实际锁定SDK HandleSubmit私有队列7次调用、10项通过，0x81/82隔离及0x83 FIFO/pending状态正确；connection为空，不包含发送字节/次序。证据同根`Queue-20261016-150453`。


## 2026-10-16 Steam Deck Tools 数据与所有权对照（R19）

按用户要求继续数据方向，固定官方 SDT 默认分支 commit `deee7fa887c2864ee3b54eba2c32b2f58b547349`，追到实体 HID 读取、触点门控/新接触基准、Desktop 鼠标与 Steam Input 分支。touch/click/XY 与 YMCC 偏移一致；SDT Desktop 默认为本地鼠标输出，Steam 分支使用实体 Deck，没有可直接移植的虚拟 SD 报告生产链。发现其右压力轴声明与自身结构不一致、随附 HID Read(int) 忽略读取长度，均不照搬。未合并 lizard PR 与物理固件故障陈述单独保留，不据此给虚拟设备加250ms重发或改压力/释放。新增14:34包没有R17/R18逐包字段，不能作为连续数据或修复验收。此轮仅研究/文档，不新增测试、运行时负担或部署；闪烁未解决。详情 `docs/screen-touchpad-steam-deck-tools-r19.md`；来源/静态对照/原日志摘要在 `G:/YeManCC-Work/Build/Analysis/Touchpad-Data-R19-20261016`。


## 2026-10-16 Windows Steam 原生接收与鼠标生成对照（R20）

继续数据消费方向。固定官方缓存steamclient64（SHA e9045710...）核到输入buffer、type9 counter/字段归一化、CTrackpadSource touch/click与pressure分离，以及CInputGenerator直接SendInput/具名SecureDesktopInput共享通道两个出口。10块2098B对官方包逐字节核验，未执行Steam代码。失败三段移动并非每段都伴随已记录的layout/Lizard重载，不用该候选统一解释；原native helper关闭时闪烁事实保持。父机静态binary不是微星loaded image证明，虚调用桥和实际出口尚未闭合。不改压力/坐标/UP/显示/配置，无生产代码或运行时CPU增加、测试或部署；实机闪烁仍未解决。详情 `docs/screen-touchpad-windows-consumer-r20.md`；证据 `G:/YeManCC-Work/Build/Analysis/Touchpad-Data-R20-20261016`。


R20独立复核补充：CTrackpadSource完整根存在预测后坐标超[-32767,32767]/unordered就清touch的路径；20单元6566B滤波复核有外推/校正，首样本直接路径保域，未证明实际后续overrun。Host触点稳定不能排除Steam内部清touch；固定0.005系数不等于必须200Hz，不改频率。当前最终normalize已限幅，不以Core内部-32768修正生产。详见R20报告和NativeReview/FILTER-REVIEW.md。


## 2026-10-16 现场布局精确恢复与selector纠偏（R21）

只读发现本机desktop VDF与13:46/14:34现场body均17477B且SHA d1e7cf9364...完全相同，补出preset0/1右pad active group14/19均absolute_mouse，全文件无显式mouse_smoothing。固定binary两类字典确认setting43/key与SourceMode4字面；对应CAbsoluteMouseMode构造设置43当前/默认int20，除40得0.5，但不据stored缺override宣布runtime为20或0即滤波off。另追T+b8 selector来源，枚举域未归属，撤回把它与SourceMode4/5同数值直接相连的解释；R20主线文档加注，封存字节不变。OR-hold全局门仍未知，独立dump值0不代替本机/现场值。不改生产、设置或频率，无测试、构建、部署或新增CPU工作。详情 `docs/screen-touchpad-field-layout-r21.md`；证据 `G:/YeManCC-Work/Build/Analysis/Touchpad-Data-R21-20261016`。


## 2026-10-16 Steam消费端导出门值（R22）

父机Steam20044一直存在；更正R20混合表格省列导致的进程缺失误判。同用户同session的有限只读观测核到loaded代码/名称指针，repeat/slowq/fastq均0、两个种子向量0，共464B，不代表MSI历史故障或所选controller。新增steam_touchpad_consumer_evidence.h只在既有logs.exportAll及virtual详细日志门内读取固定profile、身份/模块/代码/名称契约，输出steam-touchpad-consumer.json；关闭门无进程/模块/hash/内存工作，无新timer/线程/滑动工作，不变输入、显示或配置。selector仍只追到P+30，枚举域未定。读取路径16RPM/386B，绑定检查最多3次、常规输入零新增工作；独立/W4/WX与317输入完整原生隔离编译/链接均通过，未执行应用或设备测试。编译与边界见 `docs/screen-touchpad-consumer-export-r22.md` 和任务根 `G:/YeManCC-Work/Build/Analysis/Touchpad-Data-R22-20261016`。未部署，闪烁未验收。


## 2026-10-16 数据连续性、协议头与旧Host实物核对（R23）

官方SDL3.2.0要求ucLength64，Linux56注释不足以改01 00 09 40；固定Windows normalize完整2623B及read94B核到重复counter保留字段返回true、空read在本方法不清touch；USBIP managed无缺帧超时补neutral。按现场业务SHA+loadedMvid找到13:46/14:34精确Host，离线C#/IL确认两者均有首次pad Cancel/Join100ms后raw提交，不能默认旧包缺idle交接。新增具体候选是adapter短读：read门仅>=4B且预清buffer，若新counter短body才可能把缺字段作0；尚未证明adapter/现场发生。没有生产修改、CPU新增、测试/仿真/编译/部署，闪烁未解决。详见docs/screen-touchpad-data-continuity-r23.md；完整证据Build/Analysis/Touchpad-Data-R23-20261016。


## 2026-10-16 Windows本地读取链与后处理直接写（R24）

追到protocol handler->CHIDDeviceLocal->具名动态函数对象/GetProcAddress/SDL3.dll->默认SDL Windows ReadFile/GetOverlappedResult。SDL3磁盘与同官方包成员完全相同；正常empty返回0、错误返回错误、完成按实际长度剥零ID并copy，不伪报64B，现场其它provider/动态API/真正短读仍未知。候选caller是持久B，false guard未直接清touch；三个post目标1269B直接写未清touch/XY/pressure，但四个B内部指针callee范围未核、manager join未闭合。只文档/静态证据，无生产改动/CPU新增/测试/编译/部署，目标未修复。详见docs/screen-touchpad-windows-read-adapter-r24.md；证据Build/Analysis/Touchpad-Data-R24-20261016。


## 2026-10-16 陀螺子块写入与触摸源copy/gate（R25）

四gyro callee2511B/17直接输入store换算止于B+8c exclusive，不重叠touch/XY/pressure；具名controller_gyroprocessing源与stored gyroinactive/SDK和raw均缺省quat交叉核验，不改IMU。共同parent614d40闭合factory/caller同manager。找到66f5e2 B.buttons->T+30完整copy，仅追加24/25位不清touch；同T/B转672990。copy/update受T+68=(a9!=0)||((a8&B[ac])!=0)与enable门控，false跳过更新不是收到零touch；source列表实际类型/门值/下游用途仍待核。只文档/静态证据，无生产改动/CPU新增/测试/编译/部署，目标未修复。详见docs/screen-touchpad-source-dispatch-r25.md；证据Build/Analysis/Touchpad-Data-R25-20261016。


## 2026-10-16 18:47现场覆盖边界与source更新门（R26）

18:47包含190份累计raw诊断快照，实际覆盖3793次成功返回、两段左板125/281ms接触，无touch/click/XY/pressure错配或counter断续；cursor主要移动段约18:42:32–36不在同段覆盖，不能推断闪烁非数据问题。1Hz SDK缓存均无touch不能反证短接触被清零。用户说明来自其它任务导出，不将新消费端缺项当故障；主线R18/R22源码及标准构建接线已核，非本轮发布回执。新增source gate初始化/override生产者与binding helper有效偏移复核，false gate非touch0；特殊66hex域按实际producer继续，不数字硬连。仅文档/静态证据，无新运行时CPU、测试/仿真/编译/部署，目标未修复。详见docs/screen-touchpad-field-coverage-and-source-gate-r26.md；证据Build/Analysis/Touchpad-Data-R26-20261016。

R26特殊分支补充：6771a0源为A+8而非前一callee返回；info原分类经保存与bulk copy进入A+b0，已核Neptune4/5路径不等66hex。固定EControllerType名称表66hex=controller_legion_go_s，但该字段直接typed绑定/现场选中未核，降级候选，不改数据。GateProducerFollowup已由主任务整合封存。


## 2026-10-16 原始坐标清touch与时间元信息（R27）

用户纠偏后，本轮新增Steam消费端第4global/79B谓词取证已撤回主线header，恢复既有R22三global/两代码读取器。R27静态数据与compile-only候选保留于Build/Analysis/Touchpad-Data-R27-20261016，不作为当前包能力或修复。后续回到屏幕触摸事件→BUS→Host→SDK实际输出源码，先完整核虚拟手柄生产端。研究记录docs/screen-touchpad-raw-coordinate-cutoff-and-timing-r27.md。


## 2026-10-16 R28：虚拟 Deck 生产端复核与首次提交单写者

用户纠偏后暂停 Steam 客户端逆向，核当前 native 输入、最终快照、快速序列化、pipe、Host hash/解析与 SDK 写入。确认首次屏幕输入前 Host SubmitState 与 SDK 4ms idle 可并发：已把已有 Cancel/Join 提前到该 target 第一份合法 FRAME/NEUTRAL，全部沿用原 raw builder，删除 SubmitState 旁路；只改 ScreenTouchpadReport.cs，保留并行任务。固定 SDK/profile 已逐字段静态对照。独立冻结源码 compile-only 通过，0 errors/7 条未修改 Program.cs 的 CS8602。未运行功能测试、仿真、设备、应用或部署；没有 CPU 净收益数据。此缺口不能解释接管后持续闪烁，闪烁未定位。详见 docs/screen-touchpad-producer-chain-single-writer-r28.md 与 Build/Analysis/Touchpad-Producer-R28-20261016。


## 2026-10-16 触摸板 R29：端点等待与参考保持链

已绑定实际托管USBIP后端、复核18:47的193条对象观察和完整64B组包→服务端回复；没有无新输入自动touch0路径。固定usbip-win2源码对超容量回复报错，不正常截短成功；现场已装SYS尚未绑定。补齐同commit VIIPER服务器后，参考端有pending读的6ms deadline→当前完整状态→RET_SUBMIT→socket Write链闭合，counter保持，但未绑定HC内置DLL或运行验收。直接照搬会增加本案约8ms Host节奏下的报告数，因此不改刷新/补帧/SDK。仅静态研究和已有日志投影，无生产行为、测试、仿真、设备、导出或部署；持续闪烁未定位。详见docs/screen-touchpad-endpoint-hold-and-driver-length-r29.md与Build/Analysis/Touchpad-Producer-R29-20261016。


## 2026-10-16 触摸板 R30：生产者/pump低开销进度字段

7个优先旧包的定向覆盖没有shared producer/pump序号、生产idle状态或endpoint completion；drivers.xml补得USBIP-WIN2 UDE包版本21.14.27.907，但非运行SYS绑定。主线仅UsbipTransportObservation.cs在原约1Hz/稀疏生命周期只读采样附加ProducerProgress：前后producer/pump序号、idle IsAlive、等待句柄是否非零。可选shape失败保留原queue/64B，序号不冒充发布/发送完成；无映射解引用、SDK方法、每FRAME工作或新调度。正常capture新增最多6次GetValue/1次IsAlive/2次取时，未测CPU。独立静态复核及40输入冻结compile-only通过，0error/7条未改Program可空警告；未测试/仿真/设备/导出/部署，闪烁未定位。见docs/screen-touchpad-producer-progress-metadata-r30.md与Build/Analysis/Touchpad-Producer-R30-20261016。


## 2026-10-16 触摸板 R31：SD 专有路径与普通鼠标对照

保留用户的直接Mouse／虚拟手柄鼠标／摇杆鼠标正常、仅SD触摸路径闪烁对照。静态复核区分profileState请求视图与真实setContext，稳定上下文不重复refresh；物理source判退可撤销屏幕触点，但同一触点不自动重建，不能单独解释持续反复闪烁。固定SDK正常interrupt只向primary 0x83交付，未见双路legacy mouse生产者；辅助接口控制GET_REPORT的使用者未证，不作完整排除。指定Beta只定位到SendInput，不能当正常virtual SD参考。无新行为、CPU调度、测试、仿真、编译、导出或部署；闪烁根因仍未知。见docs/screen-touchpad-sd-specific-boundary-r31.md和Build/Analysis/Touchpad-Producer-R31-20261016。


## 2026-10-16 触摸板 R32：SDK 上游 raw/idle 缺陷与 HC 实际版本

官方HIDMaestro v1.10.0（2026-10-02）源码及发布说明独立确认旧raw/idle混入中立帧；主线R28单写者接管维持，不能把此证据当已接管后现场闪烁根因。HC本机候选实际native库绑定Alia5/VIIPER679f7e0…，父已核该版6ms server缓存保持与header56；不同于R29新源0ec的gate/worker，未移植Hz或改header。辅助GET_REPORT显式响应可达，但微软官方未建立自动持续鼠标回退。仅静态研究／版本绑定，无生产修改、测试、仿真、编译、导出、部署或新增CPU；目标仍未完成。详见docs/screen-touchpad-sdk-idle-upstream-and-hc-binding-r32.md与Build/Analysis/Touchpad-Producer-R32-20261016。


## 2026-10-16 触摸板 R33：实际HC保持字节与现场证据边界

固定HC dirty库四函数/2400B机器码确认：deadline保留现有touch/XY/counter，实际完整64B/header64；旧库header56不是改当前格式的依据。源码另确认快速非tap UP/DOWN可在两次快照间合并，但未证现场且不能解释单id连续MOVE。当前Host与R30冻结输入相同、native组帧提交段与R31相同，最新微星包仍18:47原SHA。无新运行时改动/CPU/测试/编译/导出/部署；故障同段最终交付缺口连续R31–R33未解，目标未完成，等待现场证据。详见docs/screen-touchpad-hc-hold-contact-boundary-r33.md和Build/Analysis/Touchpad-Producer-R33-20261016。
