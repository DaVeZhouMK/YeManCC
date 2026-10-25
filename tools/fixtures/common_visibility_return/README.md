# Common visibility return：真实消费函数回归入口

入口：`node tools/common_visibility_return_selftest.mjs`。它编译并执行当前 `native/main.cpp` 中逐字提取的真实 `hidHideRestorePhysicalTransaction` 及其精确依赖，固定保留106的 **11场景／97断言**。默认没有Build专用candidate；生产函数的共同 `ownedTargetsReturned` gate就是被测对象。只运行私有夹具，不编译完整main、不include新target runtime、不读取installed AppData、不操作设备或启动真实CLI/Host。

```powershell
node tools/common_visibility_return_selftest.mjs --out G:/<private>/common-return-fresh01 --toolchain G:/<existing>/production-link100
```

`--out` 必须是生产仓库外、尚不存在的绝对私有目录；已有证据拒绝覆盖。`--toolchain` 必须显式传入已有捕获工具链（如production-link100），不安装或下载任何工具。默认运行当前函数的Release、Checked，以及共同gate回退为旧逻辑的Release语义负例。

可选旧main对拍：

```powershell
node tools/common_visibility_return_selftest.mjs --out G:/<private>/common-return-compare-fresh01 --toolchain G:/<existing>/production-link100 --before-main G:/<frozen>/source/native/main.cpp
```

旧main另行逐字提取并运行Release／Checked：旧gate须在5个原业务场景触发共10项断言失败；若传入的before已具有共同gate则要求全部通过。before和当前版本、gate回退负例均比较11个场景的所有driver mutation、命令、pipe请求和配置结果；不允许候选通过改变修改范围来通过回归。若行为／场景有意改变，需评审并同步契约，不能屏蔽断言或任意扩大提取范围。

## 真实源与编译绑定

每次先冻结当前main及五个显式头文件／JSON依赖，再提取26个连续源码块：四个真实数据结构、真实restore、原global sweep、CLI owner/RAII/执行器、快照capture/严格解析、精确路径/应用辅助函数及carried责任操作。只在原文外插入`#line`；每块原行号、UTF-8字节偏移、长度与SHA-256在`EXTRACTION.json`核对。无YMCC_FAULT_INJECT或restore短路。未来main新增的include不被追随；未适配的新依赖应使编译/提取失败并留下证据，不能自动加载生产runtime来“修好”测试。

before优先使用其同级source树中这五项冻结依赖；没有同级文件时使用当前明确列出的依赖，并在`SNAPSHOT.json`／根`SOURCES.json`记录回退来源。因此单独before-main对拍证明指定consumer控制流，不等于整个旧产品TU或全部旧依赖回归。

每个变体保留`GENERATED.json`、`COMMAND.json`、编译和运行日志、实际`sourceDependencies`、`COMPILE-CLOSURE.json`、exe哈希及`imports.log`。cl/link/dumpbin身份与捕获manifest绑定；每个实际编译输入必须匹配本轮冻结源码或捕获工具链哈希，未知输入拒绝。Release使用/O2 /MT；Checked使用/Od /RTC1 /MT，不冒称C++算术溢出检查。静态导入只允许KERNEL32且拒绝真实进程／driver／pipe调用导入；CRT私有报告文件IO允许，不宣称跟踪全部动态CRT模块。

本轮fixture／runner／README快照保存在`entry-source/`，源码运行中改变会拒绝交付并保留FAILED。语义负例只有实际编译并以原10项业务断言失败、退出1才算捕获；编译失败、崩溃或夹具异常不能算成功。最终根RESULT、READY给出被测main完整哈希及所有路径。运行full95／74或生产target runtime回归不属于此入口。

## 外部叶隔离

- 真实CLI executor的Win32属性、CreateProcess、pipe、wait、exit、close、sleep叶都重定向至假句柄与受控driver；真实CLI owner、deadline及drain控制流仍执行。绝不启动HidHideCLI或Host，不接触已安装路径。
- HIDHIDE_QUERY的外部请求返回假回执；实际ViaPipe和严格JSON解析仍执行，包括坏回执拒绝。完整pipe transport协议不在本测试重跑。
- board authority是受控事实，无能力库／实际硬件发现。真实MSI fence、transaction mutex与PS ownership数据类型使用明确冻结头文件。
- logger／cache失效和独立MSI持久化callback是记录端口，不写AppData；物理cycle是记录端口且11场景均不需要到达。它们不证明完整MSI落盘或物理重枚举。
- 仅使用ASCII测试标识；转换适配器明确拒绝非ASCII，不调用Win32编码API。不声称覆盖生产Unicode转换。
- 未来提取代码若直接调用CreateFile/WriteFile/DeviceIoControl/CreateProcess/动态查找等未批准叶，会因显式禁用映射而编译失败。所有实际目标路径只存于假driver内；唯一真实报告输出由夹具CRT写入本轮私有目录。

## 11个场景与结算层次

| 场景 | 应有结果 |
|---|---|
| 非MSI：虚拟absent、CLI false、owned path仍hidden | 返回false、保留journal |
| 非MSI：读回UNKNOWN、CLI拒绝 | 返回false、保留journal |
| 非MSI：Host ACK UNKNOWN、坏pipe回读 | 返回false、保留journal |
| 空责任、无source、无journal的正常OFF | 返回true、零driver修改／CLI命令 |
| MSI：CLI false、owned path仍hidden | 保留原严格拒绝 |
| MSI：读回UNKNOWN、CLI拒绝 | 保留原严格拒绝 |
| MSI：exact path与owned app均已归还 | 返回true、清journal，foreign及resident cloak保留 |
| MSI：path已归还但owned app仍注册 | 返回false、保留journal |
| 非MSI：ACK accepted但owned仍hidden | 返回false、保留journal |
| 非MSI：ACK失败但配置明确已移除 | 以exact配置回读结算，不把ACK失败等同未修改 |
| 非MSI：carried责任接回后仍hidden | 返回false、保留重建journal |

所有case保留真实consumer产生的回执、journal前后／clear观察、实际到达假driver修改叶、查询及pipe轨迹。ACK、owned配置归还与游戏信号必须分开：physicalReturnObserved保持UNKNOWN、externalAccess保持unverified；空journal无上述字段时也不推断游戏可用。

原非MSI global sweep／cloak-off仍可能到达假叶，必须如实记录。此入口要求各版本修改轨迹一致；它既不扩大也不授权旧sweep。本gate只证明本轮精确owned path配置责任，未扩展非MSI应用／全局cloak结算。通过测试不能将旧Restore直接当安全cold-cleanup入口，磁盘record/session也不提供该权限。HC上层Unhide(void)可解释旧“命令已发”语义，却不能把已知commandfalse＋still-hidden称为return proof。
