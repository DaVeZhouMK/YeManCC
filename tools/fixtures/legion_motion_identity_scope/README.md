# Legion identity scope isolated validation

只测试当前 main 的身份 scope，无生产改动、消费者重复测试、真实 DLL/HID/注册表。runner 从冻结 main 只读提取真实函数，保存源行号/SHA256/快照，并编译、执行受控内存边界：

- 完整 InterfaceIdentity、实际 model/hardware/collection predicate、虚拟 marker、完整祖先 walker。
- 完整 evict、invalidate、generationStillCurrent 及真实 cache declarations/TTL。
- RawInput resolver 的原始 cache-hit/route-refresh prefix；cache miss 在夹具边界直接返回 false，绝不声称执行完整 RawInput resolver。
- RawInput 原始 fill/generation/eviction block 在显式测试 wrapper 中执行。
- 完整 default physical candidate enumeration 和 selected Observe 入口；后者在原 cache prefix 的 default Unsupported 命中上实际早退。

CreateFile/HID caps/SetupAPI/CM/RawInput path/XUSB route/clock 均由内存 stub 提供受控返回值，真实准入判断保留生产函数。日志/性能为 effect sinks。完整 RawInput miss-enumeration/InterfaceIdentity 调用点只绑定 source-only，不作为运行通过；真实 hide transaction/driver、注册表、硬件/完整消费者均未执行。

默认 scope 未提供 flag（依赖真实默认 false）：FFA0/1 vendor 为 NotGamepad、默认 candidate 枚举无 hide candidates，Observe 不改 selected。capture=true 仅四精确 Lenovo Legion model+对应 VID/PID+FFA0/1 获得局部准入，仍过原虚拟 marker/base 祖先证明。

同 HANDLE default Unsupported / capture Physical 分表独立；真实 invalidate 同时清表且递增 generation；真实 hit/fill fence 拒绝失效结果；真实 evict 只改指定 namespace；capture cache hit 不刷新 XUSB，default 原路由照常刷新。测试范围/未执行部分在 source-bindings.json 和 REPORT.md 明示。

运行 `node tools/legion_motion_identity_scope_selftest.mjs`；使用已有 MSVC Windows x64 Release/Checked，生成文件只在 workspace `Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionFamilies/IdentityScope/<fresh>/`。编译失败/崩溃不能算变异体检出。
