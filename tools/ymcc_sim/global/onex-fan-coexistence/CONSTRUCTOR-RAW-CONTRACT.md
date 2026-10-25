# 原机型构造链的元数据验收

`constructor-raw-contract.mjs` 消费固定 PE reader 的实际元数据结果，检查原 HC08a 镜像与原 `constructor-leaf-ports` 生成物。它不加载 HC，也不执行构造器。

本轮锁定的输入属于 `HandheldCompanion, Version=0.32.4.0` 的实际 DLL 元数据；版本由精确镜像读取，不根据目录名字推断。它不是全部产品当前运行版本的声明。

验收范围为86个类型、128个选定方法：七个原构造期 IO／组合叶子允许变化，其余121个方法保持带作用域操作数、局部变量及异常区域。80个工厂构造入口（含1个未启用入口）和21个 GamepadMotion 原 PInvoke 声明均在选定集合中。原模块初始化入口单独保留；没有将它抹掉或当成空入口。

七个允许的叶子为：主板查询、主板缓存静态组合、ManagerFactory静态组合、SettingsManager.GetProperty、OneX固件能力读取、SteamDeck.Open拒绝、物理内存读取。各叶子的完整签名、导入程序集及IL逐条锁定在 `constructor-raw-basis.json`。反射导入System.Object属于固定CoreLib；原有System.Collections与HC本地成员作用域分别保留，不作名称归一化。

`constructor-raw-contract.test.mjs <实际元数据SUMMARY.json> <新证据目录>` 在已读取的元数据上运行15个有限控制，包括原构造器变化、模块初始化变化、原native声明变化、错误导入scope和重复签名拒绝。控制数据的修改不产生PE，有限门不是89场景的业务执行证明。

原 `constructor-proof.mjs` 的89场景、四组故障、原预算、native数学接口、79个启用构造器的实际运行仍由原合同约束。当前元数据通过不授权这些运行，不替代后续精确加载上下文、构造期副作用、真实GamepadMotion数学库和清理回执的检查。

原 `ConstructorInventory` 的方法引用遍历属于保守上界，会把ldftn委托目标、泛型方法和类型静态构造参考纳入集合。生成后重新盘点有492个候选本地方法；完整签名读取实际覆盖512个重载方法。这个数不是执行覆盖、设备数量或待修bug数，不能据此声称所有方法必定执行。读取选择器必须从最后的 `::` 分开类型与方法，并在方法段寻找参数开括号，避免将返回类型的 `modreq(...)` 误当作方法参数。
