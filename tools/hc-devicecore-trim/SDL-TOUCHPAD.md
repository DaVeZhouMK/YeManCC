# SDL passive timer + zero touchpad guards 交付 — 2026-10-18

状态：**READY-FOR-HEADLESS-PASSIVE-TIMER-AND-ZERO-TOUCHPAD-INTEGRATION**。追加在主控已接入的 timer HC `110156…` 上，只替换 HC 文件；Modern / Controls / SDK 不变。两个已确认的被动 touchpad sink 已切断，保留所有非零句柄的原 SDL 调用、控制器协议与设备/EC 逻辑。

最终文件：`D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/sdl-touchpad/final/candidate/HandheldCompanion.dll`。

| 项目 | 值 |
|---|---|
| SHA256 | `08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583` |
| 输入 timer HC SHA256 | `110156b16a20a903029c8f4632b2f6c25de542284827bcf686518865cfbd70ac` |
| 输入 / 输出字节 | `2,774,016 / 2,774,016` |
| 新 MVID | `80cb2df3-7340-9bd5-a675-3df4f6dc631d` |
| 绑定身份 | `HandheldCompanion, Version=0.32.4.0, Culture=neutral, PublicKeyToken=null` |
| 签名 | unsigned；Authenticode `NotSigned` |
| 派生 runtimeId | `YMCC-DEVICECORE-SDL-PASSIVE-GUARDS-20261018-08a386607a0e5e72` |
| guard policy SHA256 | `0fa56d72b748a1789ced768c6913cf93b336de6ea7e04ff839ba96e64132a7c7` |

权威机器清单：`sdl-touchpad/final/DELIVERY.json`，本工具目录副本 `SDL-TOUCHPAD-RESULTS.json`。主控按此更新 HC pin 与正式派生 manifest；scratch 的 runtime-no-sdl 为软件验证目录，不能整体覆盖主控已经另行精简的生产 runtime。

## 精确代码增量

本轮政策固定输入文件哈希及两个原方法的 token、完整名称、逐指令语义 SHA256：

- `0x06002847`：`int SDLController.GetTouchpads()`。
- `0x06002848`：`int SDLController.GetTouchpadFingers(int touchpad)`。

每个方法仅前置下面 **10 字节 / 5 指令** 的 guard，然后接完整原 IL：

```text
ldarg.0
ldfld native int SDLController::gamepad
brtrue.s ORIGINAL_IL
ldc.i4.0
ret
ORIGINAL_IL:
  ... 完整原始查询和返回字节 ...
```

零 gamepad 直接在 HC 返回 0；非零先通过 guard，再执行字节相同的原查询体。原 `SDL3.SDL.GetNumGamepadTouchpads` / `GetNumGamepadTouchpadFingers` MemberRef 与调用参数保留，非零路径不伪造结果、没有 SDL shim。

带 guard 的两个 tiny method body 附加在现有末节尾部，只修改两条 MethodDef 的 RVA 和 MVID；原方法 token/签名/属性不变，原 body 字节留在旧位置但不再作为这两个方法的入口。没有新增类型、方法或运行 DLL，没有新增资源变化。此前 default constructor 的两处 BCL timer 重定向与两个 helper 逐字节保留。

相对 `110156…`：14,037 个现有方法中 **14,035 个原始 body/RVA/语义不变、2 个有意 guard**；12,795 方法体总数不变。完整 metadata 除 MVID heap 值和两条 MethodDef RVA 外相同；117 个 FieldRVA 初值、全部资源、AssemblyRef 与其他 token 保留。相对 Modern 阶段 `c0e720…` 的累计批准差异仍仅为：default ctor 两处计时 call、两条内部 BCL helper，以及本轮两个 getter guard。

证据：`candidate/verification.json`、`methods.json`、`guard-il.json`。原带真实 gamepad 参数的构造、控制器事件泵、传输协议、连接/震动/灯光/传感器方法及设备/EC 代码完全保留。

## NULL 句柄语义与实际原库 oracle

按本地 native SDL FileVersion `3, 4, 12, 0` 固定官方 `release-3.4.12/src/joystick/SDL_gamepad.c`，文件 SHA256 `4f88ce1abfc0f556a2d92e024eaafd765b9843a7c7c9a0c66882350e72d06335`。源码与 URL 回执在 `sdl-touchpad/source-evidence/`：两个函数均先置 `result=0`，无效 SDL_GetGamepadJoystick 返回 NULL，结果保持 0。

已按用户授权另外启动纯软件 oracle 进程 **28640**，只加载固定本地 SDL3.dll（SHA256 `2a2704678bf6c9c6a944270ab35079df76f5afe92b780394ed72d9c8218b98d8`），恰好调用：

```text
SDL_GetNumGamepadTouchpads(NULL)             -> 0
SDL_GetNumGamepadTouchpadFingers(NULL, 0)    -> 0
```

共两次真实原生 NULL 查询；没有 SDL_Init、设备枚举、非零句柄调用、HC/Host 执行。报告 `validation/native-sdl-null-oracle.json` 记录加载期间实际 SDL 模块路径及卸载后无模块；该 oracle 是唯一允许 SDL native 的独立探针。其他候选和负例进程均拒绝全部非框架 native。

guard 保留返回计数语义，并有意跳过零句柄下 SDL 的锁及错误状态等副作用。headless 消费方以返回计数判断无物理触控板，不读取该错误状态；非零路径保留原 SDL 行为。此前 BCL counter/QPF 原始单位与 64 位转换的证明继续见 SDL-TIMER.md，且新候选再次通过全部计时检查。

## 安全的实际 passive 执行验证

零句柄正例进程 **32532**，runtime 目录递归物理无 SDL3.dll，入口为真实 `AssemblyLoadContext.LoadFromAssemblyPath`。HC、SDL3-CS 与其他非框架程序集均装有 native deny resolver，ALC unmanaged fallback 也末端拒绝。前后 processModules 均无 native SDL3.dll；SDL3-CS 托管映像单独记录，不混同 native。

对实际 `DummyXbox360Controller`、`DummySwitchProController` 类型分别 GetUninitializedObject 并立即 SuppressFinalize；不调用构造。只准备纯托管的被动状态：gamepad=0、HasGyro=false、FrozenSet.Empty、空 BCL List。反射核对 receiver 的实际虚分派落到 SDLController 实现，再执行：

- 实际 GetTouchpads：返回 0。
- 实际 GetTouchpadFingers：分别以 int.MinValue、-1、0、1、int.MaxValue 调用，全部返回 0。
- **实际未修改的 SDLController.InitializeInputOutput 全方法**（token `0x0600284f`），两个 receiver 均成功返回，按钮/轴集合保持空。

共 **14 项**真实实现操作，native attempts **0**。GetTouchpads=0 跳过 fingers/触控板列表分支；HasGyro=false 跳过 gyro 列表；HasButton 只查询已准备的空 FrozenSet，剩余字典遍历为托管逻辑。此处不再只以 timer 前缀代替该虚方法验证。

两类独立负例：

- 原 timer-only HC `110156…`，进程 **44328**：对相同两个 receiver 的 getter 与 InitializeInputOutput 零路径，共 **6 次**原 SDL 导入被明确拒绝。
- 新候选非零哨兵 `0x1234`，进程 **37660**：两个 receiver 的两条 getter 共 **4 次**均到达原 SDL3 导入并立即被拒绝；没有放行非零句柄到真实 native，未用设备或假返回掩盖原路径。

对应报告为 `validation/passive-zero-handle.json`、`baseline-110156-zero-handle.json`、`nonzero-sentinel-native-denied.json`，记录 operation、receiver、参数、导入库、resolver 安装对象、attempts 与模块列表。

**没有执行完整 Dummy / SDLController / IController 构造，也没有执行 ManagerFactory / ControllerManager cctor。** IController 构造仍创建 GamepadMotion 并进入 `GamepadMotion.dll!CreateGamepadMotion`，之后访问带 Tablet/manager 初始化的 ManagerFactory；这些超出纯软件安全范围。受控对象上的实际 InitializeInputOutput 验证不等价于完整构造或生命周期验证。未构造 EC/IDevice、运行 SDLInit/产品 Host、访问硬件、创建窗口或渲染。

## 其他实际回归及独立审计继承

- `validation/path-load-gettypes.json`：HC 全部 **1,768** 运行时类型成功。
- `validation/actual-callback-path-comparison.json`：原 **22 项** memory-ACPI callback 封包/默认表/释放对照通过，无真实设备构造。
- `validation/timer-prefix-no-sdl.json`：另一个全新进程 **39008**，原 BCL timer helper、57-byte 字段初始化段、QPC 包围、频率/位宽转换验证继续通过，native attempts 0。
- `validation/modern-headless-construction.json`：Modern/Controls 严格签名身份与 **7 个纯构造**继续通过，native attempts 0；既有 legacy Modern 一类型缺依赖边界不变。

独立 receiver 审计只读快照为 `independent-sdl-remaining.readonly-snapshot.json`，SHA256 `ddd80c5560a25216073971609bfe3d636555ea44df4a63120a58076023e084ac`。原报告绑定 c0，其 receiver 图为 **59 states / budget 400，frontier 空**；先前 timer parity 证明加本次逐方法比较，将其非改动方法继承到最终候选。该审计列出的被动 SDL 计时、touchpad sinks 已由批准差异切断。它和本软件验证共同适用于既定 headless profile，未宣称整个保留 HC 的所有 SDL 功能都可无 SDL 工作。

消费方继续不得启动 ControllerManager.Start / pumpThreadLoop、LoadGamepadMappings、ReopenSDLGamepads、完整 Stop，或调用保留的实际 SDL gamepad 构造/连接/传感器/Tick/震动/灯光路径；这些活动路径原样保留以维护原协议能力，需要 native SDL。SDL3-CS 元数据继续保留供 nested enum/delegate/type 引用。

## 离线复现、交接与写域

```powershell
$tool = 'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/hc-devicecore-trim'
& "$tool/build-sdl-touchpad.ps1" `
  -OutputDirectory 'D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/sdl-touchpad/reproduce-01'
```

新输出目录必须为空。默认输入为已封存 timer runtime-no-sdl，源 manifest SHA256 `b27799e0786bb757ee9b118ed7f10cb1b90a12f88632d015bbd6da4d6d7456ce`；脚本逐文件 hash 复制独立快照，policy 再固定 HC `110156…`。如复用本次冻结输入，传 `-InputRuntime 'D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/sdl-touchpad/final/input-snapshot'`。原库 oracle 使用另一个参数 `-OriginalSdlPath`，默认指向先前冻结的本地 3.4.12，程序另行固定其 SHA256。

底层命令 `sdl-touchpad-guard <input-hc> <policy-json> <new-output-directory>`。封装脚本离线编译工具和五个探针，分别以新 OS 进程执行 oracle/原件负例/哨兵/候选零路径，完成实际 Path、callback、Modern、timer 复验，核对报告后生成 DELIVERY。attempt-01 与 final 两次生成候选的 hash 相同。

本轮新增 SdlTouchpadGuard.cs、build-sdl-touchpad.ps1、profiles/sdl-zero-touchpad.policy.json、probes/SdlTouchpadProbe/*，Program.cs 加有界命令；更新 README、变更清单与本报告。源哈希回执见 `sdl-touchpad/final/source-receipt.json`。只写授权工具目录和 binary-trim scratch，未改主控 profile / 消费者 / sourcepin / 生产 payload / 共用构建导出文件。最终 hash 交接后停止写入，由主控统一提升。