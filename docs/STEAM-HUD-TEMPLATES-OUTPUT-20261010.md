# YMCC 监控三路输出与 H5 模板第一版 — 2026-10-10

## 本轮交付与边界

在已成功的 Steam 原生游戏子窗口路线基础上继续做模板/设置页，不重复切游戏、发键、挂探针或现场截图。本轮浏览器是明确的离线示例环境，不冒称游戏内渲染验收，更不是部署一个桌面置顶网页。

- 新统一监控页：**Steam 原生 / YMCC 自绘 / RTSS 三选一**，另有总显示开关。控制面板留在YMCC，不加Decky控制台。
- 原生Steam继续位置、详情等级、文字大小20—140%/10步、文字对比度/饱和度0—100%/10步、背景不透明度0—100%/10步。原生位置的“关闭”搬为统一总开关，六个可见位置的原枚举保持不变。
- YMCC沿用H5角落信息块，默认左上角、100%大小、86%底板。提供横向信息条、轻量胶囊布局；位置和三个外观旋钮沿用Steam语义，YMCC部分支持更小步长；细化圆角/内容间距。
- RTSS只有20—240%大小控制，移除模板选择与软件退出开关。选中RTSS用原W横版；切到另一路只加载Empty.ovl隐藏OSD，不结束RTSS、卸载钩子、改锁帧或复位。
- 共28项独立开关，图表/标量独立，关闭单项不带掉其它项；当前选择可导出JSON，导出不含示例读数。
- 原有脏的RtssView.vue、SteamMonitorSettings.vue及原.ovl均保持本轮开始时的字节；源路由仅把监控页切到新增MonitorOutputView.vue。原有FPS/性能调度代码没有改动，安装版没有替换。

## 为什么没有照搬80个数据源成80个输出

已读取 `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\PowerControl\RTSS-Overlays\YeManOBS-W-1.ovl`：80 sources、74 layers。

- CPU普通/P-core/Zen5/perf与不同GPU的频率读数，有大量硬件适配别名；新显示模型只保留统一字段，不重复堆数字。
- Layer39/40是CPU1—8/16 usage负载柱；新模板保留按数组个数自适应的核心柱状图，不把频率当作柱高。真实核心索引应由统一采集接口定义，不据HAL编号擅自推断物理核心/线程身份。
- Layer54 GPU D3D利用率条与Layer1帧时间曲线也保留为独立可选项。
- CPU固定x-0.49等公式没有复制。没有复制HAL/HwInfo provider，没有新增读取共享内存或采集硬件的代码。
- 网络上下行、磁盘读写、电池各项拆为独立可选项。温度/频率/内存默认避免设备重复；高级各核心频率保留可选，默认关。

逐项来源/待扩展字段见 `G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\templates-20261010\OVL-SOURCE-MAPPING.json`。

## 数据真实接入边界

本轮统一快照映射器只映射目前 `MonitorSnapshotView.top/fps` 已有字段：FPS、1% Low、CPU功耗/温度/总负载/频率、GPU占用/功耗/频率，以及有电池时的电量/充放电功率/剩余时间。时间由本机显示格式化，不增加硬件读取。

核心负载数组/各核心频率、最高核心负载、GPU温度、RAM/VRAM用量/总量、真实帧时间样本、网络/磁盘速率，目前这个统一快照尚未提供。浏览器可先展示这些**明确的fixture**，实数据模式则显示“—”或未提供，不合成假读数，也不另起HWiNFO/HAL采集来补齐。正式补字段应由YMCC已有单采集者扩展统一快照并共用既有节拍。

6秒新鲜度按CPU/GPU所在top和FPS所在fps的各自时间检查，较新FPS不能复活旧CPU；hwDown、未来时间和无电池未知项不冒充有效数据。没有用1000/FPS伪装真实逐帧耗时；缺失的曲线样本不填零连线。

## 三选一切换安全模型

选择/调外观保存意图，**应用输出**才切换显示。当前回执状态与选中意图分开，防止模板预览被误读成当前游戏已显示。

- 串行快照每次意图，先确认能力，再确保RTSS后台运行（仅缺失时调用既有隐藏启动路径）。
- break-before-make：先确认Steam隐藏、RTSS Empty加载/读回、可用YMCC自身载体关闭，才启用一个目标。
- Steam必须实时回执、非pending并读回目标位置；文件保存、排队、超时不冒充已切换。
- 任一隐藏步骤不确定就停止，不再启用另一路，不自动重启Steam/RTSS补证。
- 总显示关也保留RTSS后台；这里没有停止RTSS、改FPS、改自动启动任务或游戏设置的API。
- **尚未注册正式YMCC常驻输出适配器**：现有opt-in测试探针不当作产品API。默认在任何显示/进程写入前拒绝YMCC应用，保留当前输出，并说明模板已保存但载体待接。浏览器模拟适配器仅存在测试入口里。

因此，当前完成的是模板/设置页/切换协调与现有Steam-RTSS桥的源码接线，不是正式YMCC Decky长期运行部署。开机/唤醒的后台强开托管也需在后续已有生命周期所有者中接入，不能把当前“应用输出时确保启动”冒称持续守护已经完成。

## 验证

- 48项纯模型/来源/字段/失效/独占路由测试通过。
- 20项真实Vue组件+真实新bridge/coordinator的离线浏览器测试通过：28开关、三路互斥、低alpha字层不淡化、模板导出、缺少适配器/Steam排队时不启用目标、保留后台、保存失败反馈、1280/720/420无页面溢出、原Dropdown的合成手柄事件。后者不替代实体手柄游戏输入验收。
- 12项既有原Steam/RTSS UI/独立帧率回归通过，输出重定向到任务目录，原测试/原组件未改。
- 全源vue-tsc --noEmit与隔离目录Vite正常构建通过。既有大chunk与动态/静态import提示保留，不宣称消失。
- 预览包明确拒绝把真实WebView2 IPC/RTSS进程脚本打进去；CSP connect-src none，所有操作只改浏览器内存模拟回执。
- 新增产品运行时EXE/DLL/采集器=0；仅当前预览开发服务运行本地Node。

测试目录中的两次早期离线harness错误保留：相对import漏mock时WebView2不可用而拒绝操作；另一轮误发不存在的gp-confirm事件，随后改为原Dropdown gp:dropdown-open/nav事件。这不是现场游戏测试失败，未触碰游戏。

人工已检查默认角落信息块、四档透明度、横条/胶囊及窄屏截图，没有把浏览器截图当新的Steam像素验收。

## 产物与后续

目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\templates-20261010`。

- `ymcc-corner.json` / `ymcc-bar.json` / `ymcc-capsule.json`：显示字段与外观模板，不带fixture数据。
- `browser\index.html`：可交互离线浏览器预览。
- `TEMPLATE-MODEL-SELFTEST.json` / `browser\BROWSER-SELFTEST.json` / `legacy-ui-regression\results.json`。
- `normal-web-build`和`NORMAL-WEB-BUILD.log`：构建输出，未部署。

下一步是在不频繁游戏验证的前提下，把这份显示模型接到已成功的Decky原生子载体，复用YMCC统一快照/已有relay，补当前缺失的核心/内存等字段与后台生命周期托管；只有载体生命周期或尺度处理变化才需要集中做一次游戏验收。不新增第二套采集、不通过浏览器预览假装常驻接通。