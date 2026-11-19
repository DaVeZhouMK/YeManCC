# 外置优先

`input.ownership.externalControllerPriority` 是唯一持久化开关，默认关闭。
控制器页直接通过输入设置 CAS 保存全局意图，游戏专属、输出人格、开机模式不另存一份。
现有玩家的全局值保持；不从旧开机模式预设猜测或重新启用。

开机模式的捕获、读取、预设迁移、应用及失败回滚都排除该字段。
旧记录中的副本从模式文档移除，不修改全局输入配置。
开机模式编辑器不再提供模式级“外置优先”；控制器页保留唯一开关。

功能仍由原输入生命周期 worker 消费，不新增线程或轮询。
经核验的外接 XInput 手柄稳定可用后，先释放本机虚拟输出，再以持久归还记录登记本机手柄的
HidHide 屏蔽和准确 USB 根刷新；外接手柄直通，不用它创建另一只虚拟手柄。
本机未开启虚拟手柄时也可使用。外接断开或用户关闭后，原归还链恢复本机手柄及所选模式。
新鲜度、电源代次、唯一身份、外接可见性、第三方屏蔽及未确认归还的原有约束继续适用。

ROG Xbox 型号的内置身份沿用主线 DMI 识别的对应关系：

| 主线设备类型 | inputHidKey | 物理 USB 根 |
| --- | --- | --- |
| XboxROGAlly | rog-xbox:0x1B4C | VID_0B05、PID_1B4C |
| XboxROGAllyX | rog-xbox:0x1ABE | VID_0B05、PID_1ABE |

必须同时满足已解析机型、型号输入键、物理 VID/PID 和准确 USB 根，仍要求唯一内置候选。
不能仅凭 Xbox 名称、槽位 0、按键活跃或通用 045E:028E 认定内置。
其他机型继续走既有 MSI 权威或完整内置 USB 端口链；桌面机器不据外接 Xbox 手柄获得内置资格。

本 owner 查询 USB connector 属性使用零访问权限的只读查询；失败仍为未知。
共享 `externalPriorityUsbPortKind` 的旧接口和其他调用者保持原语义，避免改变其它虚拟手柄修复路线。

2026-11-08 定向证据位于
`G:/YeManCC-Work/Testing/YMCC/Evidence/ExternalPriorityGlobal-20261108T152038142736Z`。
复用 `external_controller_priority_runtime_selftest.ps1`、`boot_modes_selftest.mjs --filter "external priority global memory"`
和 `boot_modes_browser_selftest.mjs --external-priority-global`；后者仅测试受影响的实际开机模式组件和原手柄 engine，
不扩大默认完整测试。内存设备测试、浏览器回放及语法编译均不替代物理掌机或实际游戏验收。
