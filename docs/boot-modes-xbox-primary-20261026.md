# 四模式立即设置 Windows Xbox 主应用 · 2026-10-26

用户截图显示“选择主应用=无”，同时“启动时进入Xbox模式”开关为开。旧主线只写StartupToGamingHome，明确保留GamingHomeApp，因此不能满足此前“Xbox或无”的要求。旧通过回执只覆盖启动标量，缺少主应用选择，是实质配置缺陷；新反例用旧代码重放可拒绝该实现。旧资料与失败原件保留。

| 模式 | 选择主应用 GamingHomeApp | StartupToGamingHome | 写入时机 |
|---|---|---:|---|
| 1 全局 | 保持原值 | 保持原值 | 不处理这两项 |
| 2 Xbox全屏 | Xbox | 1 | 设为开机模式时立即 |
| 3 Steam大屏 | 无（空REG_SZ） | 0 | 设为开机模式时立即 |
| 4 Xbox＋Steam | Xbox | 1 | 设为开机模式时立即 |

Xbox的AUMID为Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App，已由本机Microsoft.GamingApp包清单的Application Id和windows.gamingApp扩展只读核对。没有注册Steam AUMID、AnyFSE或YMCC主页。模式2/3/4仍按原合同修复OEM DeviceForm=46；全局不修改。Steam advanced的EarlyStart服务和BAT准备合同沿用原实现。

## 链路

BootModesPanel的“设为开机模式”调用chooseBootMode。落盘预设和next-boot输入偏好后，当前调用中的Windows开机配置步骤传入显式xbox/none；yeman.writeGamingHomeStartup原串行队列立即调用gamingHomePolicy，再经原registry.write读回。不会把这两项延到restoreSelectedBootMode或重启后才设置。读取、预览和选择卡片本身不写系统；用户确认设为开机模式才处理。手柄、鼠标和其它运行配置仍按原启动边界处理。

策略增加可选selection参数；缺省时独立旧开关保留原home语义。当前四模式显式传入选择，不会因旧参数缺省再次漏写。两值同属已有备份/回读/条件回退：启用时先选Xbox，再开启动；清空时先关启动，再选无。原类型不合法、备份失败、写入/回读失败均如实报错，经bootNotice提示待确认，不冒充完成；回退不覆盖并发第三方不同值。

## 验证和边界

复用原settings.boot-modes-runtime和settings.boot-modes-file-ui，以及原early_start_service_selftest。运行时/实际UI存储消费者的Windows端口现在执行真实gamingHomePolicy，仅注册表终端为内存。逐模式和16种互切均断言在模拟重启前主应用已正确；全局保留任意原值，手柄不热切。原policy消费者追加缺失/空/Xbox/第三方home、标量已开但home为空、所有失败点/半写失败/回读/并发/回退和旧实现负例。

原生产registry.read/write支持REG_SZ及DWORD，无新增原生命令。额外在隔离HKCU测试键编译执行实际两段处理函数，检查Xbox字符串、空REG_SZ、DWORD读回；不写生产GamingConfiguration、OEM、服务或输入设备。最终结果和具体计数以本次证据RESULT.json为准。

证据根G:\YeManCC-Work\Testing\YMCC\Evidence\BootMode-XboxPrimary-20261026。未部署安装版、未改本机Windows主页选择、未重启Steam/Xbox或Windows，不宣称实机开机或虚拟屏蔽验收。前端改动需经正常导出装入用户测试包。
