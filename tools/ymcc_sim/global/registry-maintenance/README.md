# 全局注册器清单维护合同（2026-10-20 r1）

本目录只服务 `extra-adapters.test.mjs` 的四个既有注册维护测试。期望来源是人工审查后版本化的完整归一化清单；测试比较精确成员、精确数量、Windows 路径大小写归一后的唯一性和来源。期望值不调用消费者的 `sourceFiles`、`artifactFiles`、`toolchain` 或 adapter factory。

`lists.v1.json` 固定两类 authority：原合同内的具名声明数组与目录成员、原工具 manifest 和完整预声明 Node package graph。生成/审查过程在 D 交付根保存，测试运行时不重新生成合同。`$REPO` 是本次被测源码根，`$PRIMARY` 是显式绑定的原工具图根，`$NODE` 是该图的真实 Node 包根；跨根替换不能通过工具清单比对。snapshot 还核对原图 SHA、全部包/成员/哈希/边及 authority 文件身份。

| ID | 旧来源断言 | 当前完整来源 | 完整工具 |
| --- | ---: | ---: | ---: |
| controller.game-motor-private-pipe-consumer | 64 | 68 | 1011 |
| controller.game-feedback-writer-consumer | 103 | 107 | 1020 |
| battery.native-host-roundtrip-consumer | 52 | 57 | 938 |
| battery.ui-native-recovery-consumer | 原工具断言 2438 | 89 | primary 2449；B41 原图 2879；B40 原图 2878 |

两个手柄消费者共同增加四个正式依赖：

- `native/input_host_recovery_health.h`
- `native/virtual_switch_verification_runtime.h`
- `tools/fixtures/controller_game_motor_pipe/lifecycle-context.mjs`
- `tools/fixtures/controller_game_motor_pipe/native-stage-evidence.mjs`

motor 的 D 历史 after 合同恰为 64；writer 的同份历史 after 合同为 102，旧维护测试已额外明确要求 `tools/fixtures/xinput_identity_feedback_context.mjs`，所以旧断言 103 已含该项，当前 107 相比该断言新增的是上述共同四项。

电池 Host 的五个新增来源由历史 UI 的完整 76 项来源冻结清单逐项比对确认：

- `capabilities/HcSecondaryDisplayCapabilityCatalog.cs`
- `native/BatteryHost/HostDomainRouter.cs`
- `native/BatteryHost/SecondaryDisplayController.cs`
- `tools/fixtures/battery_native_host_roundtrip/router-contract-probe.mjs`
- `tools/fixtures/battery_native_host_roundtrip/router-contract.test.mjs`

HostDomainRouter/SecondaryDisplayController 通过原合同的 `native/BatteryHost` 目录扩展收集，router 两个测试通过 fixture 目录扩展收集；catalog 是固定声明增加。它们是原消费链随源码维护增加的依赖。此次不改变任何生产来源或业务断言。

UI 相比原 76 项源码产物，继承这五项并增加六项工具绑定/构建维护来源：`build-tools.mjs`、`build-tools.test.mjs`、`snapshot-node-binding.mjs`、`snapshot-node-binding.test.mjs`（均在 battery_ui_native_recovery fixture），以及 global 的 `snapshot-node-tools.mjs`、`source-snapshot.mjs`。因此普通主线保留原 2362 个外部工具，源码产物 76→87，2438→2449。registry 额外加入 runner/completion 两个来源，所以 evidenceInputs=89，而 artifactInputs 中源码产物=87。

snapshot 使用原完整工具图：原 2362 项中 323 个 Node 成员按原图映射，补齐其余 426 个图成员。父代理 D 中保存的 B41 原图还有 4 个 authority 文件，外部项 2792，加本版 87 个源码产物为 2879；D 中保存的 B40 原图以 SimWorking-B40 为 primary，只有 3 个 authority 文件（未声明 verifierSource），外部项 2791，加同样 87 个源码产物为 2878。两图均为完整的 26 包、749 个文件，图 SHA 分别固定为 7e43a96b59efd917440674a704e9ad56fbc2090e4fc17a1f76dbcdc72277ba67 和 5b6ec616459f6ffbc88dbf734fb8faaf9a2408b653d6385e0b913a2a603b9f94。合同按显式图身份选择精确清单，不从待测行的工具数量推导 expected。本轮只从 D 证据重建历史 B40 工具路径作合同夹具，未读取 B40 文件系统、加载 B40 代码、执行 B40 或批准其产品源码；不以普通主线 2449 替代任一 snapshot 原图。

保留原单例注册、原调用入口/resultFiles、原必要成员/排除成员、scope、原工具数及工具唯一性检查；以完整清单核对取代 UI 的过期数字时，精确数量和唯一性由合同同样强制执行。未改任何阈值或工具输入；没有 `>=`、跳过或依靠删成员凑数量。

父端合入后可只运行这四个原维护用例，以及本目录的 `contract.test.mjs`。D 交付根的 `validate-final.mjs` 通过只读模块 overlay 运行同一测试及 snapshot 描述符夹具：所有写入在 D，仅执行描述符/清单核对，不运行业务 consumer、编译器、Host、浏览器、产品、系统睡眠或设备。snapshot 夹具是测试用合成 authority 外壳，消费原完整工具图，不是新固定源码验收。

清单或真实声明需要维护时，先审查原合同、manifest 和工具图，写新版本完整清单与差异原因，再更新测试合同。不得在测试中从被测 factory 自动生成 expected，或通过削减图成员、放宽数量或去重实际返回值掩盖漂移。注册器 `extra-adapters.mjs` 及父代理三个新 ID 段未修改。
