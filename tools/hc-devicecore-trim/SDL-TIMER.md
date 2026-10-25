# SDL passive timer 修复交付 — 2026-10-18

状态：**READY-FOR-HEADLESS-PASSIVE-TIMER-INTEGRATION**。本次只替换 HC，Modern / Controls / SDK 文件与签名身份不变。已消除已知 dummy 默认字段初始化阶段的 SDL 计时原生边；完整 SDL native 消费闭包由并行审计负责，不能将本证据外推为所有 SDL 功能可删除。

最终文件：`D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/sdl-timer/final/candidate/HandheldCompanion.dll`。

| 项目 | 值 |
|---|---|
| 新 SHA256 | `110156b16a20a903029c8f4632b2f6c25de542284827bcf686518865cfbd70ac` |
| 输入 HC SHA256 | `c0e7201b099b66e447ddae67dc5e6e6c04e1594fbec4689cbdb2baf320ebfa15` |
| 输入 / 输出字节 | `2,774,016 / 2,774,016` |
| 输出 MVID | `84534c38-43c5-6f08-02f7-d4f7ee983591` |
| 程序集绑定身份 | `HandheldCompanion, Version=0.32.4.0, Culture=neutral, PublicKeyToken=null` |
| 签名 | unsigned；Authenticode `NotSigned` |
| candidate runtimeId | `YMCC-DEVICECORE-SDL-TIMER-20261018-110156b16a20a903` |
| policy SHA256 | `a1317b548f14c6b6c40ea74f8fed65fd2d83c2c5cd3c52785ab9ada202b552c1` |

机器交接清单位于 `sdl-timer/final/DELIVERY.json`，本工具目录同内容副本为 `SDL-TIMER-RESULTS.json`。主控需更新 HC pin 并生成自己的正式派生 manifest；上游或旧 profile 哈希不能授权新 HC。整个 `runtime-no-sdl` 只为有界探针准备，包含此前 scratch 的其他 native 文件，不能整体覆盖主控当前 runtime。

## 唯一批准的原代码变化

原方法 `System.Void HandheldCompanion.Controllers.SDLController::.ctor()`，token **`0x0600284c`**。policy 同时固定输入文件哈希、原构造语义 SHA256、两个 IL 偏移、原 MemberRef token 与完整签名。

| 位置 | 原调用 | 新调用 | 原因 |
|---|---|---|---|
| `IL_0022`，原 MemberRef `0x0a0010b3` | `ulong SDL3.SDL.GetPerformanceCounter()` | 内部 helper `GetCounter()`，MethodDef `0x060036d4` | 默认字段初始化直接取得同域 BCL 原始 QPC ticks，避开 SDL3.SDL 静态初始化及 native delegate |
| `IL_002d`，原 MemberRef `0x0a0010b4` | `ulong SDL3.SDL.GetPerformanceFrequency()` | 内部 helper `GetFrequency()`，MethodDef `0x060036d5` | 取得相同 QPF ticks-per-second，维持原 `conv.r.un; conv.r4; stfld freq` |

helper 为程序集内部的 `HandheldCompanion.YmccDeviceCore.BclPerformanceTimer`，没有字段或 cctor：

```text
GetCounter():   call int64 Stopwatch.GetTimestamp(); conv.u8; ret
GetFrequency(): ldsfld int64 Stopwatch.Frequency; conv.u8; ret
```

仅追加 1 个 TypeDef、2 个 MethodDef、1 个 Stopwatch.Frequency MemberRef 及所需字符串/签名；复用 HC 已有 Stopwatch TypeRef / GetTimestamp MemberRef / System.Runtime AssemblyRef，不增加运行 DLL。原默认构造的方法头、局部签名、EH、RVA 和其余指令保持原样，只有两个 4-byte call 操数位置被替换。

**带真实 gamepad 参数的 SDLController 构造完全保留**，其中原 SDL counter/frequency 调用也保留。控制器协议、设备/EC 方法、输入/输出/事件泵、其他 SDL 调用均不改。SDL3-CS 元数据继续保留，FrozenSet 的 nested SDL enum 类型仍由它提供；仅解析 nested enum 类型不等于调用外层 SDL3.SDL 的静态计时成员。

## 时钟、位宽与版本依据

本地 native SDL3.dll：2,840,576 B，FileVersion `3, 4, 12, 0`，SHA256 `2a2704678bf6c9c6a944270ab35079df76f5afe92b780394ed72d9c8218b98d8`。本地 SDL3-CS.dll：1,300,480 B，FileVersion `3.4.10.0`，SHA256 `23cc2258b6f5229c20ff631f518c23f8736e8f4d8bfa388601672d12299a20a9`。未升级或替换 SDL 二进制。

官方源码按实际版本固定到 `libsdl-org/SDL release-3.4.12` 与 `dotnet/runtime v10.0.11`，下载的纯文本及 SHA256 见 `sdl-timer/source-evidence/receipt.json`，分别为 `67c4262b6fcac462a1a36fb8ca6e114b180899ca875479087e1a43868a1799be` 和 `b27177caa994a5d3a683cdb59885f98fdc961b2edf5058b5ce9aa806076868f0`。SDL Windows counter/frequency 直接调用 QueryPerformanceCounter/QueryPerformanceFrequency；对应 Stopwatch Windows 实现使用相同 API。该源码核对是语义依据，未宣称重新构建或证明本地 SDL 的完整源码 provenance。

计数保持原始 QPC ticks，频率保持 ticks/second；不换算为 TimeSpan、纳秒、毫秒或 Stopwatch.ElapsedTicks。`conv.u8` 为 unchecked 64-bit 位转换，保持 `long` 的完整 64 位，与 SDL 对 `LARGE_INTEGER.QuadPart` 的 Uint64 转换相符；不使用溢出检查或截断到 32 位。高位转换 fixture 覆盖 0、1、long.MaxValue、long.MinValue、-1。原频率保存为 Single 的转换及精度边界不变。

实际 Windows QPF 为 10,000,000 ticks/second；候选 helper 与系统 QPF、Stopwatch.Frequency 相同；128 次系统 QPC 前后包围测试、10,000 次连续单调采样及相对时间单位测试均通过。该变更按 Windows headless profile 验证。

## 构造安全边界

已审阅真实默认构造、IController 构造、DummyXbox360Controller / SDL.Xbox360Controller 构造、ControllerManager cctor 以及相关 GamepadMotion / ManagerFactory 初始化。完整 IController 构造会创建 GamepadMotion，后者进入 `GamepadMotion.dll!CreateGamepadMotion`（方法 token `0x06002123`），并进一步触及 ManagerFactory；ManagerFactory cctor 首先访问 `Tablet.TabletDevices`，再构造多个管理器。它们不能认定为纯计时构造，因此**没有执行完整 SDLController / Dummy / ControllerManager cctor**。

软件 fixture 读取实际候选构造 IL，完整重放 **IL_0000 至 IL_0034 的 57 字节字段初始化段**，在 `IL_0039` 的基类准备之前停止。对象由 RuntimeHelpers.GetUninitializedObject(DummyXbox360Controller) 分配，立即 SuppressFinalize；没有调用其构造。唯一适配是为 object 参数加入受审类型转换；原字段/方法 token 均解析到实际 Path 加载的候选。重放保留 IntPtr.Zero、两组 FrozenSet.Empty、counter、frequency 与原转换，随后直接检查真实字段值。

该边界正面验证了 dummy 链中已知 SDL 计时边已经消除；它不是完整 dummy、ManagerFactory 或设备生命周期验收。没有运行设备构造、EC 操作、SDLInit、控制器启动/事件泵、产品 Host、窗口或渲染。

## 最终验证与证据

以下路径相对 `D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/sdl-timer/final/`。

- `validation/timer-prefix-no-sdl.json`：全新进程 **14196**；runtime 递归物理无 SDL3.dll；候选 `LoadFromAssemblyPath` / GetTypes **1,768**（原 1,767 + helper）；helper 与实际 57-byte 前缀通过；HC / SDL3-CS / 其他非框架程序集 native import 均安装末端拒绝 resolver，attempts **0**；进程模块前后均无 native SDL3.dll。`AllSdlNamedModules` 单独记录保留的 managed SDL3-CS.dll，避免把它混同为 native SDL3。
- `validation/baseline-prefix-no-sdl.json`：另一个全新进程 **44308**，相同物理无 SDL3 runtime、相同前缀；原 HC 在 SDL3-CS 的 SDL3 DllImportResolver 上被拦截 **1 次**，符合负例预期。基类构造未进入，无 native SDL3 载入。这两项从不在同一 golden 进程执行。
- `validation/path-load-gettypes.json`：独立实际 Path 加载，全部 HC GetTypes **1,768** 成功。
- `validation/actual-callback-path-comparison.json`：原 **22 项** HC memory-ACPI 封包、默认表与句柄释放对照全部通过，native 末端阻断，无真实设备构造。
- `validation/modern-headless-construction.json`：此前 Modern/Controls 严格身份及 **7 个已审阅纯构造**继续通过，native attempts 0；保留原件同样存在的一项 legacy Modern 类型缺依赖限制。
- `candidate/verification.json`、`methods.json`、`metadata-tables.json`：14,035 原方法中 **14,034 不变、1 个仅两调用操作数改写**；12,793 原方法体的 RVA/头/locals/EH 均保留，未批准的方法 IL 逐字节及语义相同；**所有原 metadata 表行逐字节相同**（MVID 值所在 GUID heap 更新）；全部原类型/成员 token、AssemblyRef、117 个 FieldRVA 初值、3 个嵌入资源容器与原生资源/重定位保持一致。
- `candidate/helper-il.json`、`candidate-call-audit.json`：准确记录新增 helper，以及仍保留在带参数构造中的两处 SDL 计时调用。
- `constructor-chain-audit.json`：完整审阅 IL 和 GamepadMotion native module/entrypoint 证据。
- `validation/negative-wrong-input.json`：错误输入哈希被拒绝，无候选写出。

早期 `attempt-01/timing-prefix-no-sdl.json` 将 managed SDL3-CS 模块误计入 native 名称过滤，产生了一次探针失败；原始结果保留。后续过滤精确匹配 native SDL3.dll，并完整报告所有 SDL 命名模块；最终全新进程验证见上。首次候选 DLL 与最终候选 hash 一致，没有通过改库绕过该诊断。

## 离线复现与写域

```powershell
$tool = 'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/hc-devicecore-trim'
& "$tool/build-sdl-timer.ps1" `
  -OutputDirectory 'D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/sdl-timer/reproduce-01'
```

输出必须为新空目录。默认只读输入是已封存的 Modern 闭包 runtime；脚本按源 manifest SHA `77afd0479f8d9d936dcb8358efa7ee65d4e5e630dc7f643b321ba4f697977513` 逐文件核验并复制到独立 input-snapshot，按 policy 核验 HC `c0e720…`，使用可信本机 Mono.Cecil 离线构建，调用 `sdl-timer-patch <input-hc> <policy-json> <new-output-directory>`，再实际编译运行四个探针。原件负例与候选正例分别创建独立 OS 进程；脚本检查报告并根据实际结果生成 DELIVERY。

如要直接重用本次冻结输入，增加 `-InputRuntime 'D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/sdl-timer/final/input-snapshot'`。重现候选 SHA 在 attempt-01 与 final 两次构建中一致。官方源码下载仅用于独立报告证据，复现脚本无网络依赖。

本次新增 SdlTimerAudit.cs、SdlTimerPatch.cs、build-sdl-timer.ps1、profiles/sdl-passive-timer.policy.json、probes/SdlTimerProbe/* 与本报告；Program.cs 新增两个有界命令，README/CHANGED-FILES 更新。完整源哈希回执在 `sdl-timer/final/source-receipt.json`。只写工具目录与 binary-trim scratch，未修改主控 profile / 消费者 / sourcepin / 生产 payload / 共用导出构建文件。