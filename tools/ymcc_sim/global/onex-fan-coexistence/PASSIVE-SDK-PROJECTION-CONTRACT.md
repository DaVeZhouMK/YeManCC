# 固定 SDK 的被动投影登记元数据合同

此合同仅核对固定 `Microsoft.Windows.SDK.NET` 与 `WinRT.Runtime` 的12个模块／类型登记方法。它读取已生成的 PE 元数据报告，不加载或执行目标 DLL。

已审路径为模块入口 → ProjectionInitializer / ProjectionTypesInitializer → ComWrappersSupport 两个 Register 方法 → TypeNameSupport 两个 Register 方法。相关静态构造只分配字典、列表、弱表和数据映射；终端调用逐条锁定到BCL的集合、字符串比较器和Type/Assembly读取。完整IL、类型作用域及镜像SHA都在 `passive-sdk-projection-basis.json` 中，旧报告重复出现的方法也必须一致。

校验通过只说明这些固定方法构成被动托管登记路径。它不批准调用SDK功能、创建UISettings、执行WinRT/COM激活、设备初始化或任意新方法。运行准备还须绑定实际构造消费者、自己的输出目录、加载上下文、依赖和native数学接口；不能单独拿这份合同启动一般Host。

`validatePassiveProjectionMetadata(reports)` 要求四份精确metadata-only报告、两镜像、完整12方法、已审本地调用闭包和精确BCL终端。新增模块入口、未知功能调用、calli/ldftn/PInvoke、镜像或作用域变化均拒绝。后续SDK升级必须读取新字节并重新审查差异，不能只替换basis中的SHA。

`passive-sdk-projection-contract.test.mjs <新证据目录>` 运行12个有限控制。控制只修改JSON模型，不生成PE或执行SDK；它们不是原89场景的业务运行证明。
