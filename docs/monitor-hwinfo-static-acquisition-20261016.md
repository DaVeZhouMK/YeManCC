# HWiNFO 内部静态配置与 HWiNFO 优先 FPS（2026-10-16）

最终要求：上游 HWiNFO、YMCC 唯一接口及全部消费端一起优化；优先减少上游无用工作，现有监控数据必须保持可见；HWiNFO FPS 必须保留，只在缺失时用 RTSS。用户后续明确不需要 AMD 快照开关，因此本轮最终不增加任何前台采集开关或预设，也不默认启用尚未实测的数据裁剪。

上一轮 `hwinfo-ready-progress-20261016` 完成内置稳定模板与生产者更新确认，本轮启动已核对实际源码及其 ROUND-RESULT，分类为 progress。本轮未修改唯一硬件采集入口、采样线程或硬件采样频率。持续优化目标保持 active；本轮不代表 HWiNFO 高 CPU 已经解决。

## 最终保留的内部配置

PowerControl/YeManHWiNFO.ps1 的配置准备路径读取全YMCC同一个 yeman-settings.json 内的可选 `rtss.monitorAcquisition`，不是新建配置数据库或为监控新增采集器。该静态内部控制点可用于后续已验证的上游调整，没有常驻配置轮询。

配置文档缺失或未启用管理时，其他采集选项保持目的INI现状，只增加必要的 PresentMon=1。明确存在内部管理配置时，才读取 SnapshotPolling 布尔值。即使旧配置中 presentMon=false，也始终生成 PresentMon=1。任何路径都不把关闭HWiNFO FPS作为节能措施。

内部配置在预检及退出后的重检查中读取，最多16MiB，严格UTF8与对象/schema/布尔校验；错误在停止进程之前报告。原稳定567字节模板中的基础键保持原值，动态生成中追加必要FPS项；原周期参数1–3秒、编码保留、限长、两份固定备份、复制验证、回滚、互斥和提权机制不变。

由此更新上一轮“完整生成输出与参考逐字节相等”的断言：现在检查参考的每个基础键仍相等，并只增加明确的必要FPS键。健康start仍不会写运行中的INI，静态选项只在原恢复/restart事务内配置，不随HUD字段或显示开关反复变化。

## AMD 开关的影响与撤回

本机版本内置说明表明 SnapshotPolling 针对AMD Zen，改变整包/逐核采样方式，部分线程级项目可能不可用。现在核心占用图块来自Windows，因此它的直接采集源不受此HWiNFO开关改变；CPU频率/电压等HWiNFO项是否在具体机器上完整保留，仍需实际运行核对，不能只凭设置名称保证无影响。

最终做法是先保留当前采集路径，不自动启用快照模式。后续只有在原统一数据层证明当前展示所需字段完整时，才将优化固定到内部配置。没有把未验证的数据缺失当作可接受节能。

中途制作的前台AMD开关、采集预设、新bridge和对应UI测试已从产品源码撤回；MonitorOutputSettings.vue恢复为轮次起点的精确字节，保留其他任务修改。相关研究源文件和测试只留在本轮Build目录的superseded-ui-proposal/；SUPERSEDED-UI-PROPOSAL.json明确它们不是最终产品。旧ui-preview页面标注“已取消的界面提案”，不作为本轮交付预览。

## HWiNFO FPS 优先，RTSS 兜底

冻结的native/monitor_frame_output.h存在实际优先级问题：只要RTSS FPS新鲜，就覆盖HWiNFO的有效FPS。这与用户保留更适合其插帧口径的HWiNFO FPS要求冲突。

现在原生统一输出先检查HWiNFO的原测量时间、有效期和数值（至少1FPS）；有效则保留HWiNFO。其数据不存在、过期或低于1FPS时，才使用原有有效RTSS观察。HWiNFO新测量恢复后自动回到它。没有在消费端再分出另一套优先级。

冻结/当前测试使用相同数据：HWiNFO=180、RTSS=89，旧版显示89，新版显示180；HWiNFO期限到期后真实RTSS=88救援，随后HWiNFO=200恢复时显示200。数值为合成回执，只验证选择规则，不是实际插帧游戏验收。

FPS选择和帧时间曲线独立，原有RTSS快速曲线仍可用，没有用1000/FPS倒算帧时间。1% LOW只沿真实HWiNFO证明和期限，RTSS救援不会刷新或补造它。实际统一hub/model及两个已有显示协议测试保持这个选择，重复包装时间不能延长旧数据。

## 上游证据和边界

官方主页、下载页和version-history可读取；官方论坛本轮均403，web工具也未返回可引用内容，未补造引用。主要证据来自用户安装配置、本机程序研究副本与官方压缩包的静态核对。

安装程序为UPX压缩。本轮仅对独立研究副本使用官方UPX发布工具解包，读取配置表、内置说明和选项分支；未执行HWiNFO副本或修改安装原件。UPX工具摘要与GitHub官方资产摘要相符。官方hwi_854.zip中的HWiNFO64.exe与本机原件逐字节相同，哈希及过程在UPSTREAM-EVIDENCE.json和UPX-DOWNLOAD-PIN.json。

本机静态表记录PresentMon默认1、SnapshotPolling默认0，并存在对应设置UI读写；PresentMon有跳过初始化的执行门。静态代码、写INI成功和模拟恢复，都不是上游CPU下降或实际机器数据完整性的证明。本轮没有承诺其他版本、架构及所有机器的选项行为。

稳定模板中EC、SMART、ATAStats、SMBus、GPUI2C等原本已经关闭，不能重复计作新收益。本轮最终既未关闭PresentMon，也未自动启用SnapshotPolling。细化单传感器/整组Monitoring禁用还需验证稳定ID、实例、设置格式及设备插拔，不能凭名称写未知注册表键。

## 验证

当前生产逻辑新增/本轮测试：

- 实际PowerShell生产函数30项：内部静态配置、必要FPS、旧禁用值修正、显示选择独立、健康保存不重启、提权/回滚与文档边界。
- 实际Output头冻结/当前各14/16项：复现错误FPS优先级，验证HWiNFO主源、RTSS救援、恢复回切、LOW期限和独立快速曲线。
- 实际统一hub/model/两个已有协议6项：原生包逐级消费与过期，不允许消费端改回RTSS优先。

回归：恢复29、内置模板/推进44、配置合并26、周期确认48、实际BAT参数/回执41；快速帧发布接线8项；最终vue-tsc --noEmit通过。当前检查合计248项，冻结版用例另记。中途界面/bridge的20项及浏览器12项已作为被撤回提案留档，不计入最终实现验收。

原整页布局测试曾完成12项/120组测量；最终页面精确恢复起点字节，不把已撤回控件当作最终UI交付。未进行完整应用打包、部署、真实HWiNFO/YMCC/RTSS/游戏重启、新硬件采样、CPU百分比压测或12小时到期实测。两份安装INI只读哈希仍保持不变。

最终净改动为PS配置逻辑、必要帧输出头、相应测试及本报告。native/main.cpp未写入，并行任务修改未覆盖；局部补丁在冻结基线核验可应用且应用后字节与交付文件一致。

证据目录：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-producer-budget-20261016/。ACQUISITION-RESULT.json、FRAMES-RESULT.json、CONSUMER-RESULT.json、regressions/、UPSTREAM-EVIDENCE.json、LOCAL-INI-UNCHANGED.json、SUPERSEDED-UI-PROPOSAL.json、ROUND-RESULT.json与SCOPED-CHANGES.patch记录最终和留档范围。上游实际收益与字段完整性尚未闭合，持续目标保持active。
