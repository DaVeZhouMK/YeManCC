# 开机模式夹具

固定模式为 global、xbox、steam、xbox-steam。各检查共用原登记、原执行器和核心证据层，不由普通导出自动触发。

## 正式用例

| canonical ID | 入口与范围 | 预算 |
|---|---|---|
| settings.boot-modes-runtime | boot_modes_selftest.mjs；正式 bridge/model 的提交、升级、游戏专属优先、失效代次、回滚、失败回执、CPU 延迟读取及启动所有权，系统 owner 为内存端口 | 60 秒 |
| settings.boot-mode-startup-boundary | boot_mode_startup_selftest.mjs；正式原生启动 JSON 策略、G3E 重复旧标识／进程重放负例、旧 identity 只读观察与新生产者的隔离 HKCU 跨进程检查 | 60 秒 |
| settings.boot-modes-browser | boot_modes_browser_selftest.mjs；正式 PowerView/Panel/Workbench 与原手柄引擎，720/800 的自然方向、A/B、下拉、滑块、模态框、条件控件、宽度及错误重试；boot bridge 为受控内存端口 | 60 秒 |
| settings.boot-modes-file-ui | boot_modes_file_ui_selftest.mjs；新增的 UI→真实 repository/bridge→实际文件→原生新开机→新前端读取边界 | 60 秒 |

新增 file-ui 用例补足原 UI 内存 bridge 无法证明文件持久化的覆盖，不复制原导航或原原生负例为新父门。它属于 settings 模块的显式 module 检查，未扩大默认 minimum 或普通导出测试范围。完整声明输入在 global/adapters.mjs；本轮消费者发现的输入、编译器与原生 helper 身份也写入证据。

## 四模式文件 UI 轮次

每种分辨率 32 个场景，共 64 个：无配置文件 4 个，旧配置 4 个，四个当前模式到四个目标模式的全部 16 种组合，已有自定义配置 4 个，以及保存失败重试、损坏主文件兼容备份恢复、损坏且无备份、未来配置版本 4 个。独立检查预览不落盘、选择自动提交而不依赖编辑保存、当前运行手柄不热切、同一次 Windows 启动不重放、首次新启动应用及随后重复调用不重复恢复、无关数据保留。

四个无配置场景还进入实际编辑器并按 B 返回；返回焦点失败会被保留，同时继续独立的存储断言，避免缺掉首次初始化链路。推荐初值与用户选择分开验证。选择时以已经配置的预设为准，不能把非全局模式一律断言成 SD；原过宽的 SD policy assertion 已撤回。自定义场景现在通过实际下拉、一次保存、重开编辑器及启动选择验证，用户选关闭、Xbox、PS5 或 SD 都应保留。推荐默认只作为初始化观察，不能锁住正常修改。

## 隔离与资源

系统/服务/注册表/硬件 owner、应用级命名 mutex 和 InputHost 准入隔离。实际文件的 Win32 read/metadata/atomic write 与 JSON delta、baseline、CAS 代码均从生产 native/main.cpp 提取，原生输入启动策略直接使用生产头文件。启动 helper 每轮只启一个私有进程，逐条消费相同请求；每请求 6 秒截止，结束关闭，不启动安装版。

工作文件在核心层 Workspace/BootModeFileUI 的唯一短路径目录中，避免测试证据的多层长路径污染 Win32 文件结果。每个场景的实际主文件、备份、损坏归档和 IO trace 复制回该用例证据；源码、生成原生源码、编译日志和请求计数保留。原生文件消费者与前端重建共享一个生成模块文件，只复用编译产物字节，每次仍导入独立模块状态；不算测试缓存命中。

脚本在执行前后核对真正消费的源码。source drift、超时、已知产品失败都不能放行；修正测试目录或进程成本时保留先前回执，不放宽预算或删除断言。浏览器方向回放不是实物手柄、设备枚举、真实 Windows 重启或部署验收。

## 执行

从核心入口显式指定这四个 ID，以 -Optimized -Mode run -Rerun 运行。原例和新增文件边界分别产出结果；历史失败与这轮真实执行分开。新增 ID 和 supporting ports 不增加第二套全局测试入口。

## 2026-10-24 G3E 启动身份补覆盖

原生生产者使用 `native/boot_mode_identity.h`，已纳入原 `settings.boot-mode-startup-boundary` 输入闭包。同次进程重开稳定性与隔离 hive 重置后身份变化分开核验；只创建和删除唯一 `HKCU\Software\YeManCC-Tests\BootIdentity-*`，不触碰生产 HKLM。包含旧 loader 标识在真实重启后重复的策略负例，以及 v1→v2 保守迁移和下一次启动应用。

早先仅注入不同 boot 字符串、检查同次跨进程稳定，漏掉设备的真实新开机身份重复。隔离 hive 重置仍不能代替物理重启／混合关机／实物手柄验收。详见 `docs/boot-modes-boot-identity-20261024.md`；实际报告、原失败和源码哈希继续保留在核心证据层。

同轮用户续补“全局桌面模式默认启动”：原 runtime 用例消费真实 desktopModeStartup 模块，检查未记录时只建一次、已有任务、主动关闭、配置／任务失败及回退、并发顺序、设置代次；四模式均保留该独立开关。浏览器夹具从已完成应用默认初始化的任务状态开始，检查实际 UI 默认 ON、正式手柄关闭及页面重挂后仍 OFF。默认初始化本身由前述真实模块验证，浏览器内存初值不能冒充系统任务创建验收。
