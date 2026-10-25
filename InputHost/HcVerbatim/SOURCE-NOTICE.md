# HcVerbatim 来源记录与退出说明

2026-10-18：此目录的8份 C# 副本因没有当前产品消费者而退出编译。InputHost 项目文件未改，其他 HC 来源算法、机型兼容键和第三方依赖未因此删除。

原文件头记录来源为 HC revision `06c0b954`，源路径指向 `deps/handheldcompanion-runtime/source/Inputs`、`Controllers/ControllerState.cs` 与 `Utils/InputUtils.cs`。这是原文件的来源标记，不把它误作当前整个冻结源码库的修订证明。各原文件 SHA256、完整来源头和归档位置见 `REMOVED-SOURCES.json`；原代码保存在本次任务的只读前像中，未复制成会再次被默认编入的 `.cs` 文件。

本次只移除这组未消费副本，不修改其他位置的上游来源、作者及许可声明，也不改变仍保留的派生代码应遵守的上游许可。原8份文件头均无独立许可全文；来源头已逐字保留在记录中，未重新声明为 YMCC 原创。

验证覆盖离线编译、保留类型和方法的元数据/IL 对照、普通包跨程序集引用、源码反射/协议搜索及纯软件解析回放。未运行 InputHost Program、设备、驱动或真实虚拟输出。

复现工具：`tools/hc-verbatim-cleanup/README.md`。实际删除清单与测试回执见本任务 `HANDOFF.md`。
